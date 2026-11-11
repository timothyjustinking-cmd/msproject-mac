#!/usr/bin/env python3
"""Round 7, item 5: the measuring queue (wave 35b brief, "Queued for the next MEASURING wave").

Seven pairs, each with its own control and null (the g3 chain: build or open a base,
save a SEED, reopen it, save the CONTROL, make ONE edit, save the EDIT, reopen both
for facts, the control's NULL from a copy with Application.UserName only).

  pair                 base / shape                                        calculation
  sort-duration-asc    the wave-35 viewstate five-task shape (Task A       automatic
                       summary with B and C, then D and E; durations
                       1440 2400 960 4800 1920):
                       Sort(Key1='Duration', Ascending1=True,
                       Renumber=False, Outline=True)   (R35G2-4)
  sort-name-asc-reorder the same shape with the names in REVERSE order     automatic
                       (rows named Task E, D, C, B, A), so an ascending
                       Name sort really reorders rows; the committed
                       Name-ascending pair left the order untouched
                       (R35G2-4)
  calc-baseline        the wave-35 'unlinked' Calculate pair rebuilt in    manual (as the
                       THIS session: A (2400) then B (1440), unlinked,     source: built
                       A's duration changed to 4800 under manual           automatic, then
                       calculation, then CalculateProject()                manual; control
                       (the same-session reference, as the wave-35         and edit manual)
                       script re-measured its baseline)
  calc-tie             as the baseline but B is 4800 too, so after the     manual
                       change both leaves finish together
                       (RW35-W3, R35bG0-5: the tie)
  calc-swapped         as the baseline with the rows' durations swapped:   manual
                       A 1440, B 2400 changed to 4800, so the leaf that
                       ends the project is the SECOND row (RW35-W3,
                       R35bG2-1: the swapped order)
  assign-effort-off    wave30-resource-add.mpp (a second resource, three   automatic
                       tasks): Task 1 switched to automatic with
                       EffortDriven = False, then Assignments.Add(
                       TaskID=1, ResourceID=2)  (R35G0-5, the reference)
  assign-effort-on     the same with EffortDriven = True (R35G0-5: the     automatic
                       effort-driven assignment; differs from the
                       reference by that one factor)

Sources: scripts/oracle_w35_calculate.py (build, the pending change, the manual
control, CalculateProject, the guards on the control), scripts/oracle_w35_viewstate.py
(the five-task build and the Sort calls) and scripts/oracle_w30_resource_pairs.py
(assign_add on wave30-resource-add.mpp).  The wave-30 assign_add pair itself used a
manual task; the two assign pairs here set Task 1 to automatic and vary effort-driven
only, so they do not replace it.

Fixture names under the neutral root C:/w35/r7-measure: wave36-r7-<pair>-control.mpp,
wave36-r7-<pair>-control-null.mpp and wave36-r7-<pair>.mpp.  Facts, by ID and
UniqueID only (no name is read), go to build/r7/oracle_r7_measure.json.  Nothing is
written to tests/data.

    python scripts/oracle_r7_measure.py --list
    python scripts/oracle_r7_measure.py [pair ...]

With no pair every pair runs.  The script refuses to run if a target exists.
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

from oracle_lock import oracle_session, log, working_copy  # noqa: E402
from oracle_privacy import AUTHOR, neutralise  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w35/r7-measure"
FIXTURE_DIR = NEUTRAL + "/fixtures"
NULL_DIR = NEUTRAL + "/nulls"
WORK = NEUTRAL + "/work"
OUT = os.path.join(ROOT, "build", "r7")
RESULT = os.path.join(OUT, "oracle_r7_measure.json")
DATA = os.path.join(ROOT, "tests", "data")
ASSIGN_BASE = "wave30-resource-add.mpp"


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
    out = {"calculation": prop(app, "Calculation"), "tasks": {}}
    for index in range(1, project.Tasks.Count + 1):
        task = project.Tasks(index)
        out["tasks"][str(index)] = {
            "id": prop(task, "ID"),
            "unique_id": prop(task, "UniqueID"),
            "outline_level": prop(task, "OutlineLevel"),
            "summary": prop(task, "Summary"),
            "manual": prop(task, "Manual"),
            "effort_driven": prop(task, "EffortDriven"),
            "type": prop(task, "Type"),
            "duration": prop(task, "Duration"),
            "work": prop(task, "Work"),
            "start": prop(task, "Start"),
            "finish": prop(task, "Finish"),
            "percent": prop(task, "PercentComplete"),
            "total_slack": prop(task, "TotalSlack"),
            "critical": prop(task, "Critical"),
            "assignments": prop(task.Assignments, "Count"),
        }
    out["project_start"] = prop(project, "ProjectStart")
    out["project_finish"] = prop(project, "ProjectFinish")
    return out


# ---- bases built through Project -----------------------------------------------

SORT_DURATIONS = (1440, 2400, 960, 4800, 1920)


def build_sort(app, reverse_names):
    """oracle_w35_viewstate.build(deep=False): A (summary) > B, C; D; E."""
    app.FileNew()
    project = app.ActiveProject
    neutralise(app, project, "Synthetic R7 Sort Fixture")
    app.Calculation = 1  # pjAutomatic
    project.ProjectStart = dt.datetime(2026, 1, 5, 8, 0)
    names = ("Task A", "Task B", "Task C", "Task D", "Task E")
    if reverse_names:
        names = tuple(reversed(names))
    tasks = []
    for name, dur in zip(names, SORT_DURATIONS):
        task = project.Tasks.Add(name)
        task.Manual = False
        task.Duration = dur
        tasks.append(task)
    tasks[1].OutlineIndent()  # row 2 under row 1
    tasks[2].OutlineIndent()  # row 3 under row 1
    return project


# (first leaf duration, its pending duration, second leaf duration, pending row)
CALC_SHAPES = {
    "calc-baseline": (2400, 1440, 4800, 1),   # A 2400 -> 4800, B 1440
    "calc-tie": (2400, 4800, 4800, 1),        # A 2400 -> 4800, B 4800
    "calc-swapped": (1440, 2400, 4800, 2),    # A 1440, B 2400 -> 4800
}


def build_calc(app, pair):
    """oracle_w35_calculate.build() for the unlinked shape, with the shape's
    durations: built under AUTOMATIC calculation, then one pending change under
    MANUAL calculation."""
    first_dur, second_dur, pending, pending_row = CALC_SHAPES[pair]
    app.FileNew()
    project = app.ActiveProject
    neutralise(app, project, f"Synthetic R7 {pair} Fixture")
    app.Calculation = 1  # pjAutomatic
    project.ProjectStart = dt.datetime(2026, 1, 5, 8, 0)
    first = project.Tasks.Add("Task A")
    first.Manual = False
    first.Duration = first_dur
    second = project.Tasks.Add("Task B")
    second.Manual = False
    second.Duration = second_dur
    app.Calculation = 0  # pjManual
    (first if pending_row == 1 else second).Duration = pending
    return project


# ---- the edits ------------------------------------------------------------------

def sort_duration_asc(app, project):
    app.Sort(Key1="Duration", Ascending1=True, Renumber=False, Outline=True)
    return {"call": "Sort(Key1=Duration, Ascending1=True, Renumber=False, "
                    "Outline=True)"}


def sort_name_asc(app, project):
    app.Sort(Key1="Name", Ascending1=True, Renumber=False, Outline=True)
    return {"call": "Sort(Key1=Name, Ascending1=True, Renumber=False, "
                    "Outline=True)"}


def calculate(app, project):
    returned = app.CalculateProject()
    return {"call": "CalculateProject()", "returned": val(returned)}


def assign_resource_2(app, project):
    task = project.Tasks(1)
    assignment = task.Assignments.Add(TaskID=task.ID, ResourceID=2)
    return {"call": "Tasks(1).Assignments.Add(TaskID=1, ResourceID=2)",
            "assignment": {
                "task_unique_id": prop(assignment, "TaskUniqueID"),
                "resource_unique_id": prop(assignment, "ResourceUniqueID"),
                "units": prop(assignment, "Units"),
                "work": prop(assignment, "Work"),
                "start": prop(assignment, "Start"),
                "finish": prop(assignment, "Finish"),
            }}


# ---- the pairs -------------------------------------------------------------------

# pair -> dict(base, edit, calc, text)
PAIRS_SPEC = {
    "sort-duration-asc": dict(
        base="build-sort", reverse=False, edit=sort_duration_asc, calc="automatic",
        text="Sort Duration ascending on the five-task shape"),
    "sort-name-asc-reorder": dict(
        base="build-sort", reverse=True, edit=sort_name_asc, calc="automatic",
        text="Sort Name ascending on the five-task shape with reversed names"),
    "calc-baseline": dict(
        base="build-calc", edit=calculate, calc="manual",
        text="Calculate Project, unlinked A 2400 -> 4800, B 1440 (same-session baseline)"),
    "calc-tie": dict(
        base="build-calc", edit=calculate, calc="manual",
        text="Calculate Project, both leaves finish together (tie)"),
    "calc-swapped": dict(
        base="build-calc", edit=calculate, calc="manual",
        text="Calculate Project, the second row is the leaf that ends the project"),
    "assign-effort-off": dict(
        base="file-assign", effort=False, edit=assign_resource_2, calc="automatic",
        text="Assign resource 2 to an automatic, not effort-driven Task 1 (reference)"),
    "assign-effort-on": dict(
        base="file-assign", effort=True, edit=assign_resource_2, calc="automatic",
        text="Assign resource 2 to an automatic, effort-driven Task 1"),
}
PAIRS = tuple(PAIRS_SPEC)


def control_path(pair):
    return f"{FIXTURE_DIR}/wave36-r7-{pair}-control.mpp"


def edit_path(pair):
    return f"{FIXTURE_DIR}/wave36-r7-{pair}.mpp"


def null_path(pair):
    return f"{NULL_DIR}/wave36-r7-{pair}-control-null.mpp"


def seed_path(pair):
    return f"{WORK}/{pair}-seed.mpp"


def targets(pair):
    return (control_path(pair), edit_path(pair), null_path(pair))


def discard(pair):
    for path in targets(pair) + (seed_path(pair),):
        if os.path.exists(path):
            os.remove(path)


def build_base(app, pair, entry):
    spec = PAIRS_SPEC[pair]
    kind = spec["base"]
    if kind == "build-sort":
        entry["base"] = "built: five-task shape" + (
            ", reversed names" if spec["reverse"] else "")
        return build_sort(app, spec["reverse"])
    if kind == "build-calc":
        entry["base"] = "built: unlinked two-leaf shape " + json.dumps(
            CALC_SHAPES[pair])
        return build_calc(app, pair)
    # file-assign: wave30-resource-add.mpp on a copy
    staged = working_copy(os.path.join(DATA, ASSIGN_BASE), "r7-measure-" + pair)
    copy = f"{WORK}/{pair}-base.mpp"
    shutil.copyfile(staged, copy)
    app.FileOpen(copy)
    project = app.ActiveProject
    neutralise(app, project, "Synthetic R7 Assign Fixture")
    app.Calculation = 1  # pjAutomatic
    entry["base"] = ASSIGN_BASE
    if project.Resources.Count < 2:
        raise RuntimeError("the assign base has fewer than two resources")
    task = project.Tasks(1)
    task.Manual = False
    task.EffortDriven = spec["effort"]
    entry["task1_after_setup"] = {
        "manual": prop(task, "Manual"),
        "effort_driven": prop(task, "EffortDriven"),
        "type": prop(task, "Type"),
    }
    return project


def check_calc_control(app, pair, project, entry):
    """oracle_w35_calculate's guards on the control, per the pair's shape."""
    first_dur, second_dur, pending, pending_row = CALC_SHAPES[pair]
    tasks = entry["control_facts"]["tasks"]
    for row in ("1", "2"):
        if not str(tasks[row]["start"]).startswith("2026-01-05"):
            return f"task {row} of the control starts {tasks[row]['start']}, not at the project start"
    if tasks[str(pending_row)]["duration"] != pending:
        return (f"the control lost the pending duration "
                f"({tasks[str(pending_row)]['duration']!r}, expected {pending})")
    return None


def measure(app, pair, entry):
    spec = PAIRS_SPEC[pair]
    project = build_base(app, pair, entry)
    entry["pending_facts"] = facts(app, project)
    app.FileSaveAs(seed_path(pair))
    app.FileClose(0)

    app.FileOpen(seed_path(pair))
    project = app.ActiveProject
    if spec["calc"] == "manual":
        app.Calculation = 0
    else:
        app.Calculation = 1
    entry["calculation_mode"] = spec["calc"]
    entry["control_facts"] = facts(app, project)
    if spec["base"] == "build-calc":
        refusal = check_calc_control(app, pair, project, entry)
        if refusal:
            entry["edit"] = f"<refused: {refusal}>"
            app.FileClose(0)
            discard(pair)
            return False
    app.FileSaveAs(control_path(pair))

    try:
        entry["edit"] = spec["edit"](app, project)
    except Exception as exc:  # noqa: BLE001
        entry["edit"] = f"<raised: {type(exc).__name__}: {exc}>"
        app.FileClose(0)
        discard(pair)
        return False
    entry["edit_facts"] = facts(app, project)
    app.FileSaveAs(edit_path(pair))
    app.FileClose(0)
    os.remove(seed_path(pair))

    for key, path in (("control", control_path(pair)), ("edit", edit_path(pair))):
        app.FileOpen(path)
        entry[key + "_reopen"] = facts(app, app.ActiveProject)
        app.FileClose(0)

    # The control's null save: open a COPY, save without an edit.
    copy = f"{WORK}/{pair}-control-for-null.mpp"
    shutil.copyfile(control_path(pair), copy)
    app.FileOpen(copy)
    app.UserName = AUTHOR
    app.FileSaveAs(null_path(pair))
    app.FileClose(0)

    entry["files"] = {label: {"bytes": os.path.getsize(path),
                              "sha256": sha256(path)}
                      for label, path in (("control", control_path(pair)),
                                          ("edit", edit_path(pair)),
                                          ("null", null_path(pair)))}
    entry["edit_differs_from_control"] = (
        entry["files"]["edit"]["sha256"] != entry["files"]["control"]["sha256"])
    log(f"{pair}: edit differs from control: {entry['edit_differs_from_control']}")
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--list", action="store_true",
                    help="print the pairs and do nothing else")
    ap.add_argument("pairs", nargs="*")
    args = ap.parse_args()

    if args.list:
        print(f"neutral root {NEUTRAL}")
        for pair, spec in PAIRS_SPEC.items():
            print(f"{pair}: {spec['text']}; calculation {spec['calc']}")
            print(f"    -> {os.path.basename(control_path(pair))}, "
                  f"{os.path.basename(null_path(pair))}, "
                  f"{os.path.basename(edit_path(pair))}")
        return

    pairs = tuple(args.pairs) or PAIRS
    for pair in pairs:
        if pair not in PAIRS_SPEC:
            sys.exit(f"unknown pair {pair}")
        if PAIRS_SPEC[pair]["base"] == "file-assign" and not os.path.isfile(
                os.path.join(DATA, ASSIGN_BASE)):
            sys.exit(f"base missing: tests/data/{ASSIGN_BASE}")
        for path in targets(pair):
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
            app.UserName = AUTHOR
            for pair in pairs:
                entry = result["pairs"].setdefault(pair, {})
                try:
                    ok = measure(app, pair, entry)
                except RuntimeError as exc:
                    entry["edit"] = f"<refused: {exc}>"
                    ok = False
                log(f"{pair}: {'ok' if ok else 'FAILED'} {entry.get('edit')}")

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")


if __name__ == "__main__":
    main()
