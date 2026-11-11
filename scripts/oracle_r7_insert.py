#!/usr/bin/env python3
"""Round 7, item 3: the one-factor insert set (spec/writers/insert.md section 4).

Decoding Project's insert needs a pair set that varies ONE factor at a time (kind
only, position only, level only).  The four committed wave 35 G3 insert pairs vary
kind, position and level together and have their own controls, so they stay the
held-out checks.  This script makes ONE shared control and one edit file per pair:

  pair             call             cursor row  row level  factor it isolates
  task-first       InsertTask             1       1       the reference of all three
  milestone-first  InsertMilestoneTask    1       1       KIND only  (vs task-first)
  task-last        InsertTask             6       1       POSITION only (vs task-first)
  task-level2      InsertTask             2       2       LEVEL only (vs task-first)

The base is the wave 35 G3 'deep' outline, built through Project exactly as
oracle_w35_g3.build() does it: six automatic tasks, no links, no resources,
Task A (level 1) > Task B (2) > Task C, Task D (3); Task A > Task E (2); Task F (1).
Rows 1 and 6 are both level 1 (first and last), and rows 1 and 2 are the first
rows of their sibling groups at level 1 and level 2.  ONE caveat is unavoidable
with one shared control: no row can be at two levels, so the level pair compares
row 1 with the adjacent row 2 (also first in its group); its row ID differs by
one.  The coordinator should read it as "level, with the ID shifted by one".

Chain: build the base through Project (automatic calculation), save a SEED, reopen
it and save the CONTROL.  Every later file comes from a FRESH OPEN OF A COPY OF THE
CONTROL with Application.UserName only (no neutralise: a revision bump would
separate an edit from the null): the NULL saves it with no edit, and each pair makes
its ONE call at the cursor row and saves.  Edit and null therefore have the same
shape, and the null estimates the per-save noise of exactly that shape.

Calculation mode: AUTOMATIC (Application.Calculation = 1 after FileNew, as the
wave 35 G3 deep base does), read back after every open and reset if it differs.

Names (under the neutral root C:/w35/r7-insert), the control shared:
  wave36-r7-insert-control.mpp, wave36-r7-insert-control-null.mpp,
  wave36-r7-insert-<pair>.mpp.
Trace specs need Ctl = wave36-r7-insert-control and Null = wave36-r7-insert-control-null
for every pair (the specs6 default "<target>-control" does not apply).

Facts, by ID and UniqueID only (no name is read), include each new row's UniqueID,
ID, level, Manual flag and the project's NewTasksCreatedAsManual, and go to
build/r7/oracle_r7_insert.json.  Nothing is written to tests/data.

    python scripts/oracle_r7_insert.py --list
    python scripts/oracle_r7_insert.py [pair ...]

With no pair every pair runs (the control and null are made once, on the first run).
The script refuses to run if a target for a requested pair exists.  A later run for
other pairs reuses the existing control and null only with --reuse-control.
"""

import argparse
import datetime as dt
import hashlib
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def find_root():
    """The repo root, whether this file sits in scripts/ or in build/r7/scripts/."""
    directory = HERE
    while True:
        if os.path.isfile(os.path.join(directory, "scripts", "oracle_lock.py")):
            return directory
        parent = os.path.dirname(directory)
        if parent == directory:
            sys.exit("cannot find the repo root (scripts/oracle_lock.py)")
        directory = parent


ROOT = find_root()
sys.path.insert(0, os.path.join(ROOT, "scripts"))

from oracle_lock import oracle_session, log  # noqa: E402
from oracle_privacy import AUTHOR, neutralise  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w35/r7-insert"
FIXTURE_DIR = NEUTRAL + "/fixtures"
WORK = NEUTRAL + "/work"
OUT = os.path.join(ROOT, "build", "r7")
RESULT = os.path.join(OUT, "oracle_r7_insert.json")
TITLE = "Synthetic R7 Insert Fixture"
CONTROL = FIXTURE_DIR + "/wave36-r7-insert-control.mpp"
NULL = FIXTURE_DIR + "/wave36-r7-insert-control-null.mpp"
SEED = WORK + "/insert-seed.mpp"


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


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def facts(app, project):
    out = {"new_tasks_created_as_manual": prop(project, "NewTasksCreatedAsManual"),
           "tasks": {}}
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


def new_rows(before, after):
    """Rows of `after` whose UniqueID is not in `before`."""
    known = {row["unique_id"] for row in before["tasks"].values()}
    return [row for row in after["tasks"].values() if row["unique_id"] not in known]


def build(app):
    """oracle_w35_g3.build(): the 'deep' outline, automatic, no links."""
    app.FileNew()
    project = app.ActiveProject
    neutralise(app, project, TITLE)
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


def check_automatic(app, entry, where):
    mode = prop(app, "Calculation")
    entry.setdefault("calculation_reads", {})[where] = mode
    if mode != 1:
        try:
            app.Calculation = 1
        except Exception as exc:  # noqa: BLE001
            entry["calculation_set_error"] = repr(exc)
        entry["calculation_reads"][where + "_after_set"] = prop(app, "Calculation")
    log(f"calculation at {where}: {mode!r}")


def select_row(app, row):
    app.SelectTaskField(Row=row, Column="Name", RowRelative=False)


def insert_task(app, project, row):
    select_row(app, row)
    returned = app.InsertTask()
    return {"call": f"SelectTaskField(Row={row}, Column='Name'); InsertTask()",
            "returned": val(returned)}


def insert_milestone(app, project, row):
    select_row(app, row)
    returned = app.InsertMilestoneTask()
    return {"call": f"SelectTaskField(Row={row}, Column='Name'); "
                    "InsertMilestoneTask()",
            "returned": val(returned)}


# pair -> (call, cursor row, level of that row, the factor it isolates)
EDITS = {
    "task-first": (insert_task, 1, 1, "reference"),
    "milestone-first": (insert_milestone, 1, 1, "kind only, against task-first"),
    "task-last": (insert_task, 6, 1, "position only, against task-first"),
    "task-level2": (insert_task, 2, 2,
                    "level only, against task-first (row 2 is first in its group)"),
}
PAIRS = tuple(EDITS)


def edit_path(pair):
    return f"{FIXTURE_DIR}/wave36-r7-insert-{pair}.mpp"


def open_control_copy(app, tag):
    copy = f"{WORK}/{tag}.mpp"
    shutil.copyfile(CONTROL, copy)
    app.FileOpen(copy)
    app.UserName = AUTHOR
    return app.ActiveProject


def make_control(app, result):
    entry = result.setdefault("control", {})
    project = build(app)
    entry["pending_facts"] = facts(app, project)
    app.FileSaveAs(SEED)
    app.FileClose(0)
    app.FileOpen(SEED)
    check_automatic(app, entry, "seed reopen")
    entry["control_facts"] = facts(app, app.ActiveProject)
    app.FileSaveAs(CONTROL)
    app.FileClose(0)
    os.remove(SEED)
    return entry


def make_null(app, entry):
    open_control_copy(app, "control-for-null")
    check_automatic(app, entry, "null open")
    app.FileSaveAs(NULL)
    app.FileClose(0)
    entry["null_file"] = os.path.basename(NULL)


def make_pair(app, pair, entry, control_facts):
    call, row, level, factor = EDITS[pair]
    entry["factor"] = factor
    entry["cursor_row"] = row
    entry["cursor_row_level_expected"] = level
    project = open_control_copy(app, f"{pair}-edit")
    check_automatic(app, entry, "edit open")
    before = facts(app, project)
    entry["open_facts"] = before
    actual = before["tasks"][str(row)]["outline_level"]
    entry["cursor_row_level_actual"] = actual
    if actual != level:
        app.FileClose(0)
        entry["edit"] = f"<refused: row {row} is at level {actual!r}, expected {level}>"
        return False
    try:
        entry["edit"] = call(app, project, row)
    except Exception as exc:  # noqa: BLE001
        entry["edit"] = f"<raised: {type(exc).__name__}: {exc}>"
        app.FileClose(0)
        return False
    after = facts(app, project)
    entry["edit_facts"] = after
    entry["new_rows"] = new_rows(before, after)
    app.FileSaveAs(edit_path(pair))
    app.FileClose(0)
    app.FileOpen(edit_path(pair))
    entry["edit_reopen"] = facts(app, app.ActiveProject)
    app.FileClose(0)
    entry["file"] = {"bytes": os.path.getsize(edit_path(pair)),
                     "sha256": sha256(edit_path(pair))}
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--list", action="store_true",
                    help="print the pairs and do nothing else")
    ap.add_argument("--reuse-control", action="store_true",
                    help="use the control and null already under the neutral root")
    ap.add_argument("pairs", nargs="*")
    args = ap.parse_args()

    if args.list:
        print(f"neutral root {NEUTRAL}; calculation: automatic; one shared control")
        print(f"control {os.path.basename(CONTROL)}, "
              f"null {os.path.basename(NULL)}")
        for pair, (_, row, level, factor) in EDITS.items():
            print(f"{pair}: cursor row {row} (level {level}); {factor}; "
                  f"-> {os.path.basename(edit_path(pair))}")
        return

    pairs = tuple(args.pairs) or PAIRS
    for pair in pairs:
        if pair not in EDITS:
            sys.exit(f"unknown pair {pair}")
        if os.path.exists(edit_path(pair)):
            sys.exit(f"refusing: {edit_path(pair)} already exists "
                     "(inspect the pair first)")
    control_there = os.path.exists(CONTROL) and os.path.exists(NULL)
    if args.reuse_control:
        if not control_there:
            sys.exit("--reuse-control: the control or its null is missing")
    else:
        for path in (CONTROL, NULL):
            if os.path.exists(path):
                sys.exit(f"refusing: {path} already exists "
                         "(inspect it, or pass --reuse-control)")
    for directory in (FIXTURE_DIR, WORK, OUT):
        os.makedirs(directory, exist_ok=True)

    result = {"version": None, "pairs": {}}
    if os.path.exists(RESULT):
        with open(RESULT, encoding="utf-8") as handle:
            result = json.load(handle)
    with oracle_session() as app:
        with Guard(1800):
            result["version"] = prop(app, "Version")
            app.UserName = AUTHOR
            if not args.reuse_control:
                entry = make_control(app, result)
                make_null(app, entry)
                entry["control_sha256"] = sha256(CONTROL)
                entry["null_sha256"] = sha256(NULL)
            control_facts = result.get("control", {}).get("control_facts")
            for pair in pairs:
                entry = result["pairs"].setdefault(pair, {})
                ok = make_pair(app, pair, entry, control_facts)
                log(f"{pair}: {'ok' if ok else 'FAILED'} {entry.get('edit')}")

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")


if __name__ == "__main__":
    main()
