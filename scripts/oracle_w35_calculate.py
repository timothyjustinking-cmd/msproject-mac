#!/usr/bin/env python3
"""Wave 35 (W35-1.2): three Calculate Project pairs, one factor each.

wave30-calculate is the baseline: two automatic unassigned tasks, A -> B linked
FS+0, calculation MANUAL, A's duration changed from 2400 to 4800 minutes (the
pending change), then F9.  W35-1.2 enables Calculate Project for ONE shape, an
unlinked automatic leaf, and needs two more pairs before it may be enabled for
linked files.  Each pair here differs from the baseline by one factor, and the baseline itself
(`linked`) is re-measured in the same session so version drift cannot hide in a
comparison with the wave-30 files:

  linked    the baseline (A -> B, FS+0), rebuilt under automatic calculation
  unlinked  the baseline without the link (the shape W35-1.2 enables)
  lag       the baseline with a lag of 480 minutes (one day) on the link
  snet      the baseline with a Start No Earlier Than constraint on B, dated
            after A's new finish so it binds

For each pair the script builds the project in manual calculation, saves a
seed, reopens it (the first save after FileNew is not reopen-stable, W30-2.4),
saves the CONTROL, runs Application.CalculateProject() and saves the EDIT, then
makes the control's NULL save from a copy of the control.  All files are written
under the neutral root C:/w35/fixtures-calculate with neutral Author, Manager,
Company, UserName and Title; COM facts go to build/w35/W35-1.2-calculate.json
(not committed).  Rows are named "Task A" and "Task B": synthetic, no client
content.

    python -I scripts/oracle_w35_calculate.py

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

NEUTRAL = r"C:/w35"
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-calculate")
NULL_DIR = os.path.join(NEUTRAL, "fixtures-calculate-null")
OUT = os.path.join(ROOT, "build", "w35")
WORK = os.path.join(NEUTRAL, "calculate-work")  # the null-save source copy stays off the repo
RESULT = os.path.join(OUT, "W35-1.2-calculate.json")

PAIRS = ("unlinked", "linked", "lag", "snet")


def control_name(pair):
    return f"wave35-calculate-{pair}-control.mpp"


def edit_name(pair):
    return f"wave35-calculate-{pair}.mpp"


def null_name(pair):
    return f"wave35-calculate-{pair}-control-null.mpp"


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
    out = {
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "manual": prop(task, "Manual"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "duration": prop(task, "Duration"),
        "percent": prop(task, "PercentComplete"),
        "constraint_type": prop(task, "ConstraintType"),
        "constraint_date": prop(task, "ConstraintDate"),
        "total_slack": prop(task, "TotalSlack"),
        "critical": prop(task, "Critical"),
        "predecessors": prop(task, "Predecessors"),
    }
    return out


def facts(app, project):
    out = {"calculation": prop(app, "Calculation"), "tasks": {}}
    for index in range(1, project.Tasks.Count + 1):
        out["tasks"][str(index)] = task_facts(project.Tasks(index))
    out["project_start"] = prop(project, "ProjectStart")
    out["project_finish"] = prop(project, "ProjectFinish")
    out["project_duration"] = prop(project, "Duration")
    return out


def build(app, pair):
    app.FileNew()
    project = app.ActiveProject
    neutralise(app, project, f"Synthetic Calculate {pair} Fixture")
    # Build under AUTOMATIC calculation so every row is scheduled from the
    # project start (a first run built under manual calculation left both rows
    # at today's date, which made F9 relocate the whole project and not just
    # apply the pending change).  Manual calculation starts only after the
    # rows exist, so the one pending change is A's duration.
    app.Calculation = 1  # pjAutomatic
    project.ProjectStart = dt.datetime(2026, 1, 5, 8, 0)

    first = project.Tasks.Add("Task A")
    first.Manual = False
    first.Duration = 2400
    second = project.Tasks.Add("Task B")
    second.Manual = False
    second.Duration = 1440
    if pair == "lag":
        second.TaskDependencies.Add(first, 1, 480)  # FS, one day of lag
    elif pair != "unlinked":
        second.TaskDependencies.Add(first, 1, 0)  # FS, 0 lag
    if pair == "snet":
        second.ConstraintType = 4  # pjSNET
        second.ConstraintDate = dt.datetime(2026, 1, 26, 8, 0)

    # The pending change: with calculation manual Project stores A's new
    # duration but does not move anything that depends on it.
    app.Calculation = 0  # pjManual
    first.Duration = 4800
    return project


def measure(app, pair, entry):
    seed = os.path.join(FIXTURE_DIR, f"wave35-calculate-{pair}-seed.mpp")
    project = build(app, pair)
    entry["pending_facts"] = facts(app, project)
    app.FileSaveAs(seed)
    app.FileClose(0)

    app.FileOpen(seed)
    project = app.ActiveProject
    app.Calculation = 0
    entry["control_facts"] = facts(app, project)
    first_start = entry["control_facts"]["tasks"]["1"]["start"]
    if not first_start.startswith("2026-01-05"):
        sys.exit(f"refusing: task 1 of the {pair} control starts {first_start}, "
                 "not at the project start")
    if entry["control_facts"]["tasks"]["1"]["duration"] != 4800:
        sys.exit(f"refusing: the {pair} control lost the pending duration")
    app.FileSaveAs(os.path.join(FIXTURE_DIR, control_name(pair)))

    app.CalculateProject()
    entry["edit_facts"] = facts(app, project)
    app.FileSaveAs(os.path.join(FIXTURE_DIR, edit_name(pair)))
    app.FileClose(0)
    os.remove(seed)

    for key, name in (("control", control_name(pair)), ("edit", edit_name(pair))):
        app.FileOpen(os.path.join(FIXTURE_DIR, name))
        entry[key + "_reopen"] = facts(app, app.ActiveProject)
        app.FileClose(0)

    # The control's null save: open a COPY, save without an edit.
    copy = os.path.join(WORK, control_name(pair))
    shutil.copyfile(os.path.join(FIXTURE_DIR, control_name(pair)), copy)
    app.FileOpen(copy)
    app.UserName = "Fixture Builder"
    app.FileSaveAs(os.path.join(NULL_DIR, null_name(pair)))
    app.FileClose(0)


def main():
    for pair in PAIRS:
        for name in (control_name(pair), edit_name(pair)):
            path = os.path.join(FIXTURE_DIR, name)
            if os.path.exists(path):
                sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(NULL_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)

    result = {"version": None, "pairs": {}}
    with oracle_session() as app:
        try:
            with Guard(1800):
                result["version"] = prop(app, "Version")
                for pair in PAIRS:
                    entry = result["pairs"].setdefault(pair, {})
                    measure(app, pair, entry)
        finally:
            # measure() leaves calculation manual; do not leave the operator's
            # Project that way, whether the run finished or failed.
            try:
                app.Calculation = 1  # pjAutomatic
            except Exception as error:  # noqa: BLE001
                log(f"could not restore automatic calculation: {error}")

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for pair in PAIRS:
        entry = result["pairs"][pair]
        log(f"{pair}: control {entry['control_facts']['tasks']}")
        log(f"{pair}: edit    {entry['edit_facts']['tasks']}")


if __name__ == "__main__":
    main()
