#!/usr/bin/env python3
"""Wave 31 (G0, W31-0.4): Project's Fill Down of Start over two leaf rows.

R30-4: Fill Down is an unmeasured .mpp write.  This script measures the pair:
a synthetic project with two top-level AUTOMATIC leaf rows (no links, no
resources, no progress), the project calendar, Task A given a Start of
2026-01-12 08:00 (Project stores a Start No Earlier Than constraint on an
automatic row) and Task B left at the project start 2026-01-05 08:00.  The
selection covers the Start cells of rows 1-2; Application.FillDown() copies
A's start down onto B.

The file is saved as the SEED, reopened (this build's first save after
FileNew is not reopen-stable, W30-2.4) and saved as the CONTROL; then the
fill runs and the EDIT is saved.  A no-edit save of the control becomes the
committed null save (W29-1.0b), so tests/save_pair_noise.h can tell the
fill's effect bytes from per-save noise.

All files are saved under the neutral root C:/w31/fill with neutral
Author/Manager/Company/UserName/Title; rows are named "Task A"/"Task B".
Facts and the reopen checks go to build/w31/W31-0.4-fill-pair.json (not
committed).  The script refuses to run if the target fixtures already exist.

    python -I scripts/oracle_w31_fill.py
"""

import datetime as dt
import hashlib
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

NEUTRAL = r"C:/w31"
FIXTURE_DIR = NEUTRAL + "/fill"
WORK = os.path.join(ROOT, "build", "w31", "fill-work")
OUT = os.path.join(ROOT, "build", "w31")
RESULT = os.path.join(OUT, "W31-0.4-fill-pair.json")

CONTROL = "wave31-fill-control.mpp"
EDIT = "wave31-fill.mpp"
NULL_FIXTURE = "wave31-fill-control-null.mpp"
SEED = "wave31-fill-seed.mpp"

PROJECT_START = dt.datetime(2026, 1, 5, 8, 0)
A_START = dt.datetime(2026, 1, 12, 8, 0)
A_DURATION_MINUTES = 2400  # 5 d on the standard calendar
B_DURATION_MINUTES = 1440  # 3 d


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


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
        "constraint_type": prop(task, "ConstraintType"),
        "constraint_date": prop(task, "ConstraintDate"),
        "deadline": prop(task, "Deadline"),
        "summary": prop(task, "Summary"),
        "milestone": prop(task, "Milestone"),
        "critical": prop(task, "Critical"),
        "total_slack": prop(task, "TotalSlack"),
        "resource_names": prop(task, "ResourceNames"),
    }


def facts(project):
    out = {"tasks": {}}
    for index in range(1, project.Tasks.Count + 1):
        out["tasks"][str(index)] = task_facts(project.Tasks(index))
    out["project_start"] = prop(project, "ProjectStart")
    out["project_finish"] = prop(project, "ProjectFinish")
    out["project_duration"] = prop(project, "Duration")
    return out


def reopen(app, name):
    app.FileOpen(os.path.join(FIXTURE_DIR, name))
    result = facts(app.ActiveProject)
    app.FileClose(0)
    return result


def expect(condition, message):
    if not condition:
        raise SystemExit("refusing: " + message)


def main():
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    for name in (CONTROL, EDIT, NULL_FIXTURE):
        path = os.path.join(FIXTURE_DIR, name)
        if os.path.exists(path):
            sys.exit(f"refusing: {path} already exists (inspect the pair first)")

    result = {"version": None}
    seed = os.path.join(WORK, SEED)
    if os.path.exists(seed):
        os.remove(seed)

    with oracle_session() as app:
        with Guard(1200):
            result["version"] = str(app.Version)
            app.FileNew()
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Fill Fixture")
            app.Calculation = 0  # pjManual while the rows are set up
            project.ProjectStart = PROJECT_START

            first = project.Tasks.Add("Task A")
            first.Manual = False
            first.Duration = A_DURATION_MINUTES
            second = project.Tasks.Add("Task B")
            second.Manual = False
            second.Duration = B_DURATION_MINUTES
            # Typing a Start on an automatic row: Project stores a Start No
            # Earlier Than constraint (pjSNET = 4).  B keeps the project start,
            # so the fill has a visible effect.
            first.ConstraintType = 4
            first.ConstraintDate = A_START
            app.Calculation = 1  # pjAutomatic: the control is fully calculated
            result["calculation"] = prop(app, "Calculation")
            result["setup_facts"] = facts(project)
            log("setup facts: " + json.dumps(result["setup_facts"],
                                             sort_keys=True))
            # COM returns Task.Start in local time but ProjectStart and
            # ConstraintDate in UTC (the machine is UTC+11), so compare the
            # formatted local values, never the datetime objects.
            expect(val(project.Tasks(1).Start) == "2026-01-12 08:00:00",
                   "Task A did not take the start date")
            expect(val(project.Tasks(2).Start) == "2026-01-05 08:00:00",
                   "Task B did not keep the project start")
            expect(project.Tasks(1).Summary is False and
                   project.Tasks(2).Summary is False,
                   "a row reads back as a summary")

            # This build's first save after FileNew is not reopen-stable
            # (W30-2.4): reopen the seed and save the control from it.
            app.FileSaveAs(seed)
            app.FileClose(0)
            app.FileOpen(seed)
            project = app.ActiveProject
            app.Calculation = 1
            before = facts(project)
            result["control_facts"] = before
            expect(before["tasks"]["1"]["start"] == "2026-01-12 08:00:00",
                   "the control's Task A start did not survive the reopen")
            expect(before["tasks"]["2"]["start"] == "2026-01-05 08:00:00",
                   "the control's Task B start did not survive the reopen")
            control_path = os.path.join(FIXTURE_DIR, CONTROL)
            app.FileSaveAs(control_path)

            # Project's Fill Down: select the Start cells of rows 1-2 and fill.
            # SelectTaskField(Row, Column, RowRelative, Width, Height): Height
            # counts rows in ADDITION to the active field, so Height=1 gives
            # two rows (the docs' Width and Height order is easy to mix up).
            app.SelectTaskField(1, "Start", False, 0, 1)
            try:
                result["selection_tasks"] = app.ActiveSelection.Tasks.Count
                result["selection_field"] = prop(app.ActiveSelection, "FieldID")
            except Exception as exc:  # noqa: BLE001
                result["selection_error"] = repr(exc)
            app.FillDown(True)
            after = facts(project)
            result["edit_facts"] = after
            log("edit facts: " + json.dumps(after, sort_keys=True))
            log("selection: " + json.dumps(
                {k: result[k] for k in result if k.startswith("selection_")},
                sort_keys=True))
            expect(after["tasks"]["2"]["start"] == "2026-01-12 08:00:00",
                   "Fill Down did not copy Task A's start onto Task B")
            expect(after["tasks"]["1"]["start"] == "2026-01-12 08:00:00",
                   "Fill Down moved the source row")
            expect(after["tasks"]["2"]["manual"] is False,
                   "Fill Down left Task B manual")
            edit_path = os.path.join(FIXTURE_DIR, EDIT)
            app.FileSaveAs(edit_path)
            app.FileClose(0)
            if os.path.exists(seed):
                os.remove(seed)

            result["control_reopen"] = reopen(app, CONTROL)
            result["edit_reopen"] = reopen(app, EDIT)
            expect(result["control_reopen"]["tasks"]["2"]["start"] ==
                   "2026-01-05 08:00:00",
                   "the committed control does not reopen at the project start")
            expect(result["edit_reopen"]["tasks"]["2"]["start"] ==
                   "2026-01-12 08:00:00",
                   "the committed edit does not reopen with the filled start")

            # W29-1.0b: the null save of the control, from a copy (never open
            # a committed fixture in place).
            copy = os.path.join(WORK, "null-work-" + CONTROL)
            shutil.copyfile(control_path, copy)
            null_path = os.path.join(FIXTURE_DIR, NULL_FIXTURE)
            if os.path.exists(null_path):
                os.remove(null_path)
            app.FileOpen(copy)
            project = app.ActiveProject
            null_before = {"author": str(project.Author),
                           "manager": str(project.Manager),
                           "company": str(project.Company),
                           "title": str(project.Title),
                           "tasks": project.Tasks.Count}
            app.FileSaveAs(null_path)
            null_after = {"author": str(project.Author),
                          "manager": str(project.Manager),
                          "company": str(project.Company),
                          "title": str(project.Title),
                          "tasks": project.Tasks.Count}
            app.FileClose(0)
            result["null_metadata_before"] = null_before
            result["null_metadata_after"] = null_after
            app.FileOpen(null_path)
            result["null_facts"] = facts(app.ActiveProject)
            app.FileClose(0)
            result["null_sha256"] = sha256(null_path)
            result["null_bytes"] = os.path.getsize(null_path)

    for name in (CONTROL, EDIT, NULL_FIXTURE):
        path = os.path.join(FIXTURE_DIR, name)
        result.setdefault("fixtures", {})[name] = {
            "path": path,
            "sha256": sha256(path),
            "bytes": os.path.getsize(path),
        }

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for name, entry in result["fixtures"].items():
        log(f"fixture {name} ({entry['bytes']} bytes, "
            f"sha256 {entry['sha256'][:16]}...)")
    for key in ("control_facts", "edit_facts", "control_reopen", "edit_reopen",
                "null_facts"):
        log(key + ": " + json.dumps(result[key]["tasks"], sort_keys=True))


if __name__ == "__main__":
    main()
