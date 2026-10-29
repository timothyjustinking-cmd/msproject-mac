#!/usr/bin/env python3
"""Wave 30 (G3): the Project tab's three measured save pairs.

The brief (docs/handoff/wave30-brief.md section 2, G3) asks for a Project
measurement of Calculate Project, Set Baseline 0 for the entire project, and
the WBS code definition.  Each pair is (control, Project's save after ONE
edit), and each control gets a no-edit null save from
scripts/oracle_w30_null_saves.py; the W29-1.0b mask in
tests/save_pair_noise.h turns those into effect bytes.

  * Calculate Project.  A fresh project in MANUAL calculation (this Project
    starts in manual mode; the script sets it explicitly), two AUTOMATIC rows
    with durations 5 d and 3 d, one FS link A -> B, no resources assigned, no
    progress, the project calendar.  A's duration is changed to 10 d while
    calculation is manual: the file now stores a pending change -- A's finish
    moved but B's dates did not.  The SEED is saved, reopened (this build's
    first save after FileNew is not reopen-stable, W30-2.4) and saved as the
    CONTROL; Application.CalculateProject() (Project's F9) runs, and the
    EDIT is saved.  COM must show B still at the stale date in the control
    and moved in the edit; the script refuses to certify the pair otherwise.

  * Set Baseline.  synthetic-base (three tasks, no links, no progress, one
    placeholder assignment per row, the project calendar), opened on a copy,
    saved as the CONTROL, then Application.BaselineSave(All=True,
    Copy=pjCopyCurrent(0), Into=pjIntoBaseline(0)) -- the Set Baseline
    dialog's "Baseline" / "Entire project" defaults -- and saved as the EDIT.

  * WBS.  The same synthetic base, saved as the CONTROL, then
    Application.WBSCodeMaskEdit(prefix "W", level 1, ordered numbers, length
    2, separator ".", generate, verify unique) and Application.WBSCodeRenumber(
    All=True) -- the Define Code / Renumber shape -- and saved as the EDIT.

All files are saved under the neutral root C:/w30/fixtures-g3 with neutral
Author/Manager/Company/UserName/Title.  Files are only read back after
FileClose; COM facts and Project's own reopen facts go to
build/w30/W30-G3-project-pairs.json (not committed).  Rows are named
"Task A"/"Task B" -- synthetic, no client content.

    python -I scripts/oracle_w30_project_pairs.py

The script refuses to run if any target fixture already exists.
"""

import datetime as dt
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log  # noqa: E402
from oracle_privacy import neutralise  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w30"
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-g3")
WORK = os.path.join(ROOT, "build", "w30", "g3-work")
OUT = os.path.join(ROOT, "build", "w30")
RESULT = os.path.join(OUT, "W30-G3-project-pairs.json")

CALC_CONTROL = "wave30-calculate-control.mpp"
CALC_EDIT = "wave30-calculate.mpp"
CALC_SEED = "wave30-calculate-seed.mpp"
BASE_CONTROL = "wave30-baseline-control.mpp"
BASE_EDIT = "wave30-baseline-set.mpp"
WBS_CONTROL = "wave30-wbs-control.mpp"
WBS_EDIT = "wave30-wbs-code.mpp"

SYNTHETIC_BASE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")


def val(v):
    if isinstance(v, dt.datetime):
        return v.strftime("%Y-%m-%d %H:%M:%S")
    if isinstance(v, (int, float, bool, str)) or v is None:
        return v
    return repr(v)


def prop(obj, name):
    try:
        return val(getattr(obj, name))
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def task_facts(task):
    return {
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "manual": prop(task, "Manual"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "duration": prop(task, "Duration"),
        "percent": prop(task, "PercentComplete"),
        "total_slack": prop(task, "TotalSlack"),
        "critical": prop(task, "Critical"),
    }


def baseline_facts(task):
    return {
        "id": prop(task, "ID"),
        "baseline_start": prop(task, "BaselineStart"),
        "baseline_finish": prop(task, "BaselineFinish"),
        "baseline_duration": prop(task, "BaselineDuration"),
        "baseline_work": prop(task, "BaselineWork"),
        "baseline_cost": prop(task, "BaselineCost"),
    }


def facts(project):
    out = {"tasks": {}}
    for index in range(1, project.Tasks.Count + 1):
        out["tasks"][str(index)] = task_facts(project.Tasks(index))
    out["project_finish"] = prop(project, "ProjectFinish")
    out["project_duration"] = prop(project, "Duration")
    out["wbs"] = {str(index): prop(project.Tasks(index), "WBS")
                  for index in range(1, project.Tasks.Count + 1)}
    out["baselines"] = {str(index): baseline_facts(project.Tasks(index))
                        for index in range(1, project.Tasks.Count + 1)}
    return out


def reopen(app, name):
    app.FileOpen(os.path.join(FIXTURE_DIR, name))
    result = facts(app.ActiveProject)
    app.FileClose(0)
    return result


def open_copy(app, source, name):
    os.makedirs(WORK, exist_ok=True)
    copy = os.path.join(WORK, name)
    shutil.copyfile(source, copy)
    app.FileOpen(copy)
    return copy


def measure_calculate(app, result):
    app.FileNew()
    project = app.ActiveProject
    neutralise(app, project, "Synthetic Calculate Fixture")
    app.Calculation = 0  # pjManual: the pending change is ours to leave stale
    project.ProjectStart = dt.datetime(2026, 1, 5, 8, 0)

    first = project.Tasks.Add("Task A")
    first.Manual = False
    first.Duration = 2400
    second = project.Tasks.Add("Task B")
    second.Manual = False
    second.Duration = 1440
    second.TaskDependencies.Add(first, 1, 0)  # FS, 0 lag

    # The pending change: with calculation manual Project stores A's new
    # finish but does not move the linked successor B.
    first.Duration = 4800
    result["calculate_pending_facts"] = facts(project)

    seed = os.path.join(FIXTURE_DIR, CALC_SEED)
    app.FileSaveAs(seed)
    app.FileClose(0)

    # This build's first save after FileNew is not reopen-stable (W30-2.4):
    # reopen the seed and save the control from it.
    app.FileOpen(seed)
    project = app.ActiveProject
    app.Calculation = 0
    before = facts(project)
    result["calculate_control_facts"] = before
    if before["tasks"]["2"]["start"] != "2026-01-05 08:00:00":
        raise SystemExit(
            "refusing: the pending change did not survive the reopen "
            f"(B starts {before['tasks']['2']['start']})")
    path = os.path.join(FIXTURE_DIR, CALC_CONTROL)
    app.FileSaveAs(path)

    app.CalculateProject()
    result["calculate_edit_facts"] = facts(project)
    if result["calculate_edit_facts"]["tasks"]["2"]["start"] == \
            before["tasks"]["2"]["start"]:
        raise SystemExit("refusing: F9 did not move the successor")
    path = os.path.join(FIXTURE_DIR, CALC_EDIT)
    app.FileSaveAs(path)
    app.FileClose(0)
    if os.path.exists(seed):
        os.remove(seed)

    result["calculate_control_reopen"] = reopen(app, CALC_CONTROL)
    result["calculate_edit_reopen"] = reopen(app, CALC_EDIT)


def measure_baseline(app, result):
    open_copy(app, SYNTHETIC_BASE, "baseline-work.mpp")
    project = app.ActiveProject
    neutralise(app, project, "Synthetic Baseline Fixture")
    result["baseline_control_facts"] = facts(project)
    path = os.path.join(FIXTURE_DIR, BASE_CONTROL)
    app.FileSaveAs(path)

    app.BaselineSave(True, 0, 0)  # All=True, Copy=pjCopyCurrent, Into=Baseline
    result["baseline_edit_facts"] = facts(project)
    path = os.path.join(FIXTURE_DIR, BASE_EDIT)
    app.FileSaveAs(path)
    app.FileClose(0)

    result["baseline_control_reopen"] = reopen(app, BASE_CONTROL)
    result["baseline_edit_reopen"] = reopen(app, BASE_EDIT)


def measure_wbs(app, result):
    open_copy(app, SYNTHETIC_BASE, "wbs-work.mpp")
    project = app.ActiveProject
    neutralise(app, project, "Synthetic WBS Fixture")
    result["wbs_control_facts"] = facts(project)
    path = os.path.join(FIXTURE_DIR, WBS_CONTROL)
    app.FileSaveAs(path)

    app.WBSCodeMaskEdit("W", 1, 0, 2, ".", True, True)
    app.WBSCodeRenumber(True)
    result["wbs_edit_facts"] = facts(project)
    path = os.path.join(FIXTURE_DIR, WBS_EDIT)
    app.FileSaveAs(path)
    app.FileClose(0)

    result["wbs_control_reopen"] = reopen(app, WBS_CONTROL)
    result["wbs_edit_reopen"] = reopen(app, WBS_EDIT)


def main():
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    for name in (CALC_CONTROL, CALC_EDIT, BASE_CONTROL, BASE_EDIT,
                 WBS_CONTROL, WBS_EDIT):
        path = os.path.join(FIXTURE_DIR, name)
        if os.path.exists(path):
            sys.exit(f"refusing: {path} already exists (inspect the pair first)")

    result = {"version": None}
    with oracle_session() as app:
        with Guard(1200):
            result["version"] = str(app.Version)
            result["calculation_mode"] = prop(app, "Calculation")
            measure_calculate(app, result)
            measure_baseline(app, result)
            measure_wbs(app, result)

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for name in (CALC_CONTROL, CALC_EDIT, BASE_CONTROL, BASE_EDIT,
                 WBS_CONTROL, WBS_EDIT):
        path = os.path.join(FIXTURE_DIR, name)
        log(f"fixture {name} ({os.path.getsize(path)} bytes)")
    for key in ("calculate_control_reopen", "calculate_edit_reopen",
                "baseline_control_reopen", "baseline_edit_reopen",
                "wbs_control_reopen", "wbs_edit_reopen"):
        log(key + ": " + json.dumps(result[key]["tasks"], sort_keys=True))
        log(key + " wbs: " + json.dumps(result[key]["wbs"], sort_keys=True))


if __name__ == "__main__":
    main()
