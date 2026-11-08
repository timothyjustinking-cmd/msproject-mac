#!/usr/bin/env python3
"""Wave 35 (G3): the wave-34 deferrals, one factor per pair.

Measures what Project 2021 writes to the file for the commands wave 34 deferred.
This script holds the insert pairs of W35-3.1 (Insert Summary and Insert
Milestone on a non-trivial outline), the Set Baseline pair of W35-3.2 and the note-generator pair of W35-3.3; later
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


def wbs_renumber(app, project):
    app.WBSCodeRenumber(True)
    return {"call": "WBSCodeRenumber(All=True)",
            "wbs": {str(i): prop(project.Tasks(i), "WBS")
                    for i in range(1, project.Tasks.Count + 1)}}


def delete_assignment(app, project):
    """Remove the one assignment of task 1 (resource UniqueID 2), as the wave-30
    assign_delete pair does."""
    task = project.Tasks(1)
    for index in range(1, task.Assignments.Count + 1):
        candidate = task.Assignments(index)
        if candidate.ResourceUniqueID == 2:
            candidate.Delete()
            return {"call": "Tasks(1).Assignments(<resource uid 2>).Delete()",
                    "remaining": task.Assignments.Count}
    raise RuntimeError("task 1 has no assignment for resource uid 2")


def set_task_note(row, text):
    def edit(app, project):
        project.Tasks(row).Notes = text
        return {"call": f"Tasks({row}).Notes = {text!r}",
                "notes": {str(i): prop(project.Tasks(i), "Notes")
                          for i in range(1, project.Tasks.Count + 1)}}
    return edit


def font_edit(row, column, label, call, extend_to=None):
    """Select the Name (or another) cell, optionally extend the selection down to
    `extend_to`, then apply one Font call."""
    def edit(app, project):
        app.SelectTaskField(Row=row, Column=column, RowRelative=False)
        if extend_to is not None:
            app.SelectTaskField(Row=extend_to, Column=column, RowRelative=False,
                                Extend=True)
        returned = call(app)
        return {"call": f"SelectTaskField(Row={row}, Column={column!r})"
                        + (f" extended to row {extend_to}" if extend_to else "")
                        + f"; {label}",
                "returned": val(returned)}
    return edit


# Font32Ex(Name, Size, Bold, Italic, Underline, Color, Strikethrough,
#          CellColor, Pattern, Reserved) as build/w29/cellformat_probe.py calls it.
def font32(name, size, color=0):
    return lambda app: app.Font32Ex(name, size, False, False, False, color,
                                    False, 0, 0, False)


# pair -> (edit function, base kind)
EDITS = {
    # W35-3.4: one factor against the wave-30 Bold pair (the Name cell of row 1
    # of tests/data/synthetic-base.mpp, FontBold(True)).
    "font-bold-row2": (font_edit(2, "Name", "FontBold(True)",
                                 lambda app: app.FontBold(True)),
                       "file:synthetic-base.mpp"),
    "font-bold-start": (font_edit(1, "Start", "FontBold(True)",
                                  lambda app: app.FontBold(True)),
                        "file:synthetic-base.mpp"),
    "font-bold-range": (font_edit(1, "Name", "FontBold(True)",
                                  lambda app: app.FontBold(True), extend_to=2),
                        "file:synthetic-base.mpp"),
    "font-bold-off": (font_edit(1, "Name", "FontBold(False)",
                                lambda app: app.FontBold(False)),
                      "file:wave30-textstyle.mpp"),
    "font-italic": (font_edit(1, "Name", "FontItalic(True)",
                              lambda app: app.FontItalic(True)),
                    "file:synthetic-base.mpp"),
    # FontUnderline is not callable through late binding (first run: "'bool'
    # object is not callable"), so the underline pair uses Font32Ex.
    "font-underline": (font_edit(1, "Name", "Font32Ex('Calibri', 11, Underline=True)",
                                 lambda app: app.Font32Ex("Calibri", 11, False, False,
                                                          True, 0, False, 0, 0, False)),
                       "file:synthetic-base.mpp"),
    "font-size": (font_edit(1, "Name", "Font32Ex('Calibri', 14)",
                            font32("Calibri", 14)),
                  "file:synthetic-base.mpp"),
    "font-name": (font_edit(1, "Name", "Font32Ex('Arial', 11)",
                            font32("Arial", 11)),
                  "file:synthetic-base.mpp"),
    "font-colour": (font_edit(1, "Name", "Font32Ex('Calibri', 11, Color=255)",
                              font32("Calibri", 11, color=255)),
                    "file:synthetic-base.mpp"),
    # W35-3.4: Renumber on its own (Project > Properties > WBS > Renumber), on
    # the wave-30 three-task file before a code is defined and after.
    "wbs-renumber-default": (wbs_renumber, "file:wave30-wbs-control.mpp"),
    "wbs-renumber-defined": (wbs_renumber, "file:wave30-wbs-code.mpp"),
    # W35-3.5: remove the assignment from the file the port saved (the port's
    # Assign Resources output on wave30-assign-add-control.mpp, task 1, resource
    # uid 2, built by tests/roundtrip_assignment_growth).
    "assign-remove-port-saved": (delete_assignment,
                                 "port:C:/w35/port/port-assign-add.mpp"),
    # W35-3.3: a note added on a file whose existing note was written by an
    # older RichEdit build (wave-30, Riched20 16.0.20430): does the new note's
    # generator string come from the machine or from the file?
    "notes-generator": (set_task_note(3, "Generator probe line."),
                        "file:wave30-notes.mpp"),
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


def measure_port(app, pair, entry):
    """W35-3.5.  The control is a file the PORT saved (`port:<path>`), unchanged
    byte for byte: Project never saves it, so the pair measures what Project's
    edit does to the port's own output.  The null save is Project opening and
    resaving that file."""
    edit_fn, kind = EDITS[pair]
    source = kind[len("port:"):]
    control = os.path.join(FIXTURE_DIR, control_name(pair))
    shutil.copyfile(source, control)
    entry["base"] = "port-saved file " + os.path.basename(source)

    copy = os.path.join(WORK, f"{pair}-edit.mpp")
    shutil.copyfile(control, copy)
    app.FileOpen(copy)
    project = app.ActiveProject
    # The port's file is already neutral; neutralise() would change the project
    # properties and bump the revision counter, which the null save (UserName
    # only) does not, so the edit save would differ from the null by one revision.
    app.UserName = "Fixture Builder"
    entry["control_facts"] = facts(app, project)
    try:
        entry["edit"] = edit_fn(app, project)
    except Exception as exc:  # noqa: BLE001
        entry["edit"] = f"<raised: {type(exc).__name__}: {exc}>"
        app.FileClose(0)
        os.remove(control)
        return False
    entry["edit_facts"] = facts(app, project)
    app.FileSaveAs(os.path.join(FIXTURE_DIR, edit_name(pair)))
    app.FileClose(0)

    app.FileOpen(os.path.join(FIXTURE_DIR, edit_name(pair)))
    entry["edit_reopen"] = facts(app, app.ActiveProject)
    app.FileClose(0)

    copy = os.path.join(WORK, control_name(pair))
    shutil.copyfile(control, copy)
    app.FileOpen(copy)
    app.UserName = "Fixture Builder"
    app.FileSaveAs(os.path.join(NULL_DIR, null_name(pair)))
    app.FileClose(0)
    return True


def measure(app, pair, entry):
    edit_fn, kind = EDITS[pair]
    if kind.startswith("port:"):
        return measure_port(app, pair, entry)
    seed = os.path.join(FIXTURE_DIR, f"wave35-g3-{pair}-seed.mpp")
    if kind.startswith("file:"):
        # An existing committed (synthetic, neutral) fixture, opened on a copy.
        source = os.path.join(ROOT, "tests", "data", kind[len("file:"):])
        copy = os.path.join(WORK, f"{pair}-base.mpp")
        shutil.copyfile(source, copy)
        app.FileOpen(copy)
        # The opened file carries the session's real identity until a save;
        # neutralise it before ANY save (W35-3.3 first run leaked LastAuthor).
        neutralise(app, app.ActiveProject, "Synthetic G3 Fixture")
        entry["base"] = kind
        entry["pending_facts"] = facts(app, app.ActiveProject)
    else:
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
