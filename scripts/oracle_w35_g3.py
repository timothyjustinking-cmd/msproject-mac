#!/usr/bin/env python3
"""Wave 35 (G3): the wave-34 deferrals, one factor per pair.

Measures what Project 2021 writes to the file for the commands wave 34 deferred.
This script holds the insert pairs of W35-3.1 (Insert Summary and Insert
Milestone on a non-trivial outline) and the Set Baseline pair of W35-3.2; later
G3 items add their own pair kinds here.

The base is built through Project under automatic calculation: the three-level
outline of the wave-35 view-state script (Task A > Task B > Task C, Task D;
Task A > Task E; Task F), all automatic, no links, no resources.  Each pair
saves a seed, reopens it, saves the CONTROL, makes ONE edit and saves the EDIT;
the control's NULL save is made from a copy.  All files are written under the
neutral root C:/w35/fixtures-g3 with neutral Author, Manager, Company, UserName
and Title.  COM facts go to build/w35/W35-3-g3.json (not committed); every row is
recorded by ID and UniqueID only.

    python -I scripts/oracle_w35_g3.py [pair ...]

With no argument every pair in PAIRS runs.  The script refuses to run if a
target fixture already exists.
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
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-g3")
NULL_DIR = os.path.join(NEUTRAL, "fixtures-g3-null")
OUT = os.path.join(ROOT, "build", "w35")
WORK = os.path.join(OUT, "g3-work")
RESULT = os.path.join(OUT, "W35-3-g3.json")


def select_row(app, row):
    app.SelectTaskField(Row=row, Column="Name", RowRelative=False)
    return {"selected_row": row}


def insert_milestone_at(row):
    def edit(app, project):
        info = select_row(app, row)
        app.InsertMilestoneTask()
        info["call"] = f"SelectTaskField(Row={row}); InsertMilestoneTask()"
        return info
    return edit


def insert_summary_at(row):
    def edit(app, project):
        info = select_row(app, row)
        app.InsertSummaryTask()
        info["call"] = f"SelectTaskField(Row={row}); InsertSummaryTask()"
        return info
    return edit


def set_baseline(app, project):
    # The Set Baseline dialog's defaults: Baseline (0), Entire project.
    app.BaselineSave(True, 0, 0)  # All=True, Copy=pjCopyCurrent, Into=pjIntoBaseline
    return {"call": "BaselineSave(All=True, Copy=0, Into=0)"}


# pair -> (edit function, base kind)
EDITS = {
    "baseline-set-nested": (set_baseline, "deep"),
    "insert-milestone-nested": (insert_milestone_at(4), "deep"),
    "insert-summary-nested": (insert_summary_at(4), "deep"),
    "insert-milestone-first": (insert_milestone_at(1), "deep"),
    "insert-milestone-last": (insert_milestone_at(6), "deep"),
}
PAIRS = tuple(EDITS)


def control_name(pair):
    return f"wave35-g3-{pair}-control.mpp"


def edit_name(pair):
    return f"wave35-g3-{pair}.mpp"


def null_name(pair):
    return f"wave35-g3-{pair}-control-null.mpp"


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


def facts(app, project):
    out = {"tasks": {}}
    for index in range(1, project.Tasks.Count + 1):
        task = project.Tasks(index)
        out["tasks"][str(index)] = {
            "id": prop(task, "ID"),
            "unique_id": prop(task, "UniqueID"),
            "outline_level": prop(task, "OutlineLevel"),
            "outline_number": prop(task, "OutlineNumber"),
            "summary": prop(task, "Summary"),
            "milestone": prop(task, "Milestone"),
            "manual": prop(task, "Manual"),
            "duration": prop(task, "Duration"),
            "start": prop(task, "Start"),
            "finish": prop(task, "Finish"),
            "assignments": prop(task.Assignments, "Count"),
        }
    return out


def build(app):
    app.FileNew()
    project = app.ActiveProject
    neutralise(app, project, "Synthetic G3 Fixture")
    app.Calculation = 1  # pjAutomatic
    project.ProjectStart = dt.datetime(2026, 1, 5, 8, 0)
    names = ("Task A", "Task B", "Task C", "Task D", "Task E", "Task F")
    durations = (1440, 2400, 960, 4800, 1920, 1440)
    tasks = []
    for name, dur in zip(names, durations):
        task = project.Tasks.Add(name)
        task.Manual = False
        task.Duration = dur
        tasks.append(task)
    # A (1) > B (2) > C, D (3); A > E (2); F (1)
    for index, indents in ((1, 1), (2, 2), (3, 2), (4, 1)):
        for _ in range(indents):
            tasks[index].OutlineIndent()
    return project


def measure(app, pair, entry):
    edit_fn, _kind = EDITS[pair]
    seed = os.path.join(FIXTURE_DIR, f"wave35-g3-{pair}-seed.mpp")
    project = build(app)
    entry["pending_facts"] = facts(app, project)
    app.FileSaveAs(seed)
    app.FileClose(0)

    app.FileOpen(seed)
    project = app.ActiveProject
    entry["control_facts"] = facts(app, project)
    app.FileSaveAs(os.path.join(FIXTURE_DIR, control_name(pair)))

    try:
        entry["edit"] = edit_fn(app, project)
    except Exception as exc:  # noqa: BLE001
        entry["edit"] = f"<raised: {type(exc).__name__}: {exc}>"
        app.FileClose(0)
        os.remove(seed)
        os.remove(os.path.join(FIXTURE_DIR, control_name(pair)))
        return False
    entry["edit_facts"] = facts(app, project)
    app.FileSaveAs(os.path.join(FIXTURE_DIR, edit_name(pair)))
    app.FileClose(0)
    os.remove(seed)

    for key, name in (("control", control_name(pair)), ("edit", edit_name(pair))):
        app.FileOpen(os.path.join(FIXTURE_DIR, name))
        entry[key + "_reopen"] = facts(app, app.ActiveProject)
        app.FileClose(0)

    copy = os.path.join(WORK, control_name(pair))
    shutil.copyfile(os.path.join(FIXTURE_DIR, control_name(pair)), copy)
    app.FileOpen(copy)
    app.UserName = "Fixture Builder"
    app.FileSaveAs(os.path.join(NULL_DIR, null_name(pair)))
    app.FileClose(0)
    return True


def main():
    pairs = tuple(sys.argv[1:]) or PAIRS
    for pair in pairs:
        if pair not in EDITS:
            sys.exit(f"unknown pair {pair}")
        for name in (control_name(pair), edit_name(pair)):
            path = os.path.join(FIXTURE_DIR, name)
            if os.path.exists(path):
                sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    for directory in (FIXTURE_DIR, NULL_DIR, WORK, OUT):
        os.makedirs(directory, exist_ok=True)

    result = {"version": None, "pairs": {}}
    if os.path.exists(RESULT):
        with open(RESULT, encoding="utf-8") as handle:
            result = json.load(handle)
    with oracle_session() as app:
        with Guard(1800):
            result["version"] = prop(app, "Version")
            for pair in pairs:
                entry = result["pairs"].setdefault(pair, {})
                ok = measure(app, pair, entry)
                log(f"{pair}: {'ok' if ok else 'FAILED'} {entry.get('edit')}")

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")


if __name__ == "__main__":
    main()
