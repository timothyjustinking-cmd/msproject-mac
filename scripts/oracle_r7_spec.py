#!/usr/bin/env python3
"""Round 7, item 6: the spec lists' "pairs still needed" that Project's COM can make.

One pair per entry, each with its own control and null and the g3 chain (open or
build a base, apply any SETUP, save a SEED, reopen it, save the CONTROL, make ONE
edit, save the EDIT, reopen both for facts, the control's NULL from a copy with
Application.UserName only).  The setup (a note, a resource, a percent) is part of the
control, as in oracle_w34_notes.py, whose seed-and-reopen shape this repeats.

  source list : pair  (base; calculation)  edit

  note-richtext.md:231
    note-tab            synthetic-base; auto     Tasks(2).Notes = text with a TAB
    note-paragraphs3    synthetic-base; auto     Tasks(2).Notes = three paragraphs (CRLF)
    note-cjk            synthetic-base; auto     Tasks(2).Notes = text with two CJK glyphs
    note-same-session   synthetic-base; auto     Tasks(2).Notes set, then replaced, before
                                                 the first save of the edit
    note-kept           synthetic-base; auto     control holds a note on task 2; the edit
                                                 changes Tasks(3).Duration only
    note-assignment     wave30-assign-add; auto  the assignment on Task 1 gets Notes
  var-meta.md:355
    note-shrink         synthetic-base; auto     a long note on task 2 replaced by a short one
    note-clear-two      synthetic-base; auto     notes on tasks 2 and 3 both cleared
    fill-second-edit    wave31-fill; auto        an edit save edited AGAIN: Task A's Start
                                                 constraint moved, then Fill Down again
  table-growth.md:308
    insert-two-milestones deep outline; auto     two InsertMilestoneTask calls, both with
                                                 the cursor on row 4, before one save
    resource-delete-last   wave34-resource-add-third; auto   Resources(3).Delete()
    resource-delete-middle wave34-resource-add-third; auto   Resources(2).Delete()
    calendar-delete     synthetic-base; auto     a base calendar added in the control,
                                                 then BaseCalendars(<it>).Delete()
    table-resource-column synthetic-base; auto   TableEdit on the RESOURCE Entry table
                                                 (TaskTable=False) while the task table is
                                                 applied
    table1-column-replace wave31-tables; auto    TableEdit replacing a column of the SECOND
                                                 task table (Review); Entry stays applied
    table1-date-format  wave31-tables; auto      TableEdit DateFormat=5 on Review
  calendar-tbkdcal.md:184
    resource-rename     wave34-resource-add-third; auto   Resources(2).Name changed
    calendar-add-odd    synthetic-base; auto     BaseCalendars.Add("Odd Cal")   (7 characters)
    calendar-add-even   synthetic-base; auto     BaseCalendars.Add("Even Cal")  (8 characters)
    resource-add-fourth wave34-resource-add-third; auto   Resources.Add("R4")
    (delete the resource: made as resource-delete-last / -middle above)
  scheduler-save.md:157
    calc-assign-1       two leaves + 1 assignment; MANUAL   CalculateProject (A 2400 -> 4800)
    calc-assign-2       two leaves + 2 assignments; MANUAL  CalculateProject (A 2400 -> 4800)
    calc-9d-to-10d      two leaves; MANUAL       CalculateProject (A 4320 -> 4800: 9 -> 10 days)
    (an assignment's work moving with the recalculation is what calc-assign-1 and -2
     show; the 0-assignment point is calc-baseline in oracle_r7_measure.py)
  view-stream-214.md:295
    table-column-width  synthetic-base; auto     TaskTables(1).TableFields(2).Width set
  document-summary-information.md:187
    dsi-total-work      wave30-assign-add; auto  Tasks(2).Assignments.Add(TaskID=2, ResourceID=2)
    dsi-dst-move        two leaves; auto         ProjectStart moved from 05 Jan 2026 (DST
                                                 on) to 06 Jul 2026 (DST off)
    dsi-percent-same-length synthetic-base; auto Tasks(1) automatic at 40 %, edit sets 50 %
  props-records.md:287
    wbs-mask-length3    synthetic-base; auto     WBSCodeMaskEdit("W", 1, 0, 3, ".", True, True)
    wbs-mask-dash       synthetic-base; auto     WBSCodeMaskEdit("W", 1, 0, 2, "-", True, True)
                                                 (the measured call is W, 1, 0, 2, ".")

Pairs NOT made, with the reason, are printed by --list and recorded in LANE-A.md.

Calculation mode: AUTOMATIC, set explicitly after the first open of each session and
read back, except the three calc-* pairs, which are MANUAL exactly as the wave-35
Calculate oracle makes them (built under automatic, then manual with one pending
change; control and edit saved in manual).  The sources (oracle_w34_notes,
oracle_w31_fill, oracle_w30_*) set none or automatic; no source sets manual for these.

Dates: the two-leaf bases keep the wave-35 source's naive ProjectStart; the DST move
and the fill's constraint date use oracle_w3.utc() so Project receives the wall-clock
value (the source note: the machine is UTC+11, naive datetimes are converted).  The
DST pair measures anything only when the host time zone observes daylight saving;
the JSON records the host's UTC offset on 05 Jan and 06 Jul 2026.

Files under C:/w35/r7-spec with neutral Author, Manager, Company, UserName and Title;
names wave36-r7-<pair>-control.mpp, -control-null.mpp and wave36-r7-<pair>.mpp.
Facts, by ID and UniqueID and note LENGTH only (no name or note text is read back), go
to build/r7/oracle_r7_spec.json.  Nothing is written to tests/data.

A pair whose COM call raises keeps no file and is recorded as raised, so an
unsupported member (BaseCalendar.Delete, TableField.Width, Assignment.Notes) costs one
log line and no fixture.

    python scripts/oracle_r7_spec.py --list
    python scripts/oracle_r7_spec.py [pair ...]

With no pair every pair runs.  The script refuses to run if a target exists.
"""

import argparse
import datetime as dt
import hashlib
import json
import os
import shutil
import sys
import time

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
from oracle_w3 import Guard, utc  # noqa: E402

NEUTRAL = r"C:/w35/r7-spec"
FIXTURE_DIR = NEUTRAL + "/fixtures"
NULL_DIR = NEUTRAL + "/nulls"
WORK = NEUTRAL + "/work"
OUT = os.path.join(ROOT, "build", "r7")
RESULT = os.path.join(OUT, "oracle_r7_spec.json")
DATA = os.path.join(ROOT, "tests", "data")
TITLE = "Synthetic R7 Spec Fixture"

LONG_NOTE = "A long synthetic note line for the shrink pair. " * 6
TAB_NOTE = "Column one\tcolumn two after a tab."
PARAGRAPHS_NOTE = "First paragraph.\r\nSecond paragraph.\r\nThird paragraph."
CJK_NOTE = "Glyph test \u4e2d\u6587 end."

SKIPPED = [
    ("note-richtext.md:231 item 5, a note on a calendar",
     "Project's object model has no Notes member on a calendar (only task, resource "
     "and assignment); the assignment half is note-assignment"),
    ("var-meta.md:355 item 4, a block of zero payload",
     "no editor command is known to produce a zero-length block; it is a search for "
     "a command, not a pair"),
    ("scheduler-save.md:157 item 3, Auto Schedule on a manual task",
     "already captured: wave30-task-mode-auto is manual-to-automatic on the "
     "synthetic base; the question is a comparison with the Calculate pairs"),
    ("view-stream-214.md:295 item 1, rename a view",
     "no COM setter for a view's name is known, and the Views collection access "
     "pattern is recorded to wedge Project (core/viewstate/viewstate.h); the "
     "second-change and second-cell pairs of oracle_r7_font.py answer replace-vs-append"),
    ("view-stream-214.md:295 item 2, delete a view",
     "needs Application.OrganizerDeleteItem on a named file, which can touch the "
     "global template; not attempted by a script that must not run COM"),
    ("view-stream-214.md:295 item 3, a second cell-style change on another cell",
     "made as second-cell in oracle_r7_font.py"),
    ("document-summary-information.md:187 item 2, a DurationFormat other than 21/53",
     "no verified COM setter for the project row's duration format, and the "
     "duration-unit constants are not in the repo (the type library was not read)"),
    ("props-records.md:287 items 1 and 2",
     "already measured and struck through in the list"),
]


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


def notes_len(task):
    try:
        return len(task.Notes)
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def facts(app, project):
    out = {"calculation": prop(app, "Calculation"), "tasks": {},
           "resources": prop(project.Resources, "Count"),
           "base_calendars": prop(project.BaseCalendars, "Count")}
    for index in range(1, project.Tasks.Count + 1):
        task = project.Tasks(index)
        out["tasks"][str(index)] = {
            "id": prop(task, "ID"),
            "unique_id": prop(task, "UniqueID"),
            "outline_level": prop(task, "OutlineLevel"),
            "manual": prop(task, "Manual"),
            "duration": prop(task, "Duration"),
            "work": prop(task, "Work"),
            "start": prop(task, "Start"),
            "finish": prop(task, "Finish"),
            "percent": prop(task, "PercentComplete"),
            "notes_len": notes_len(task),
            "assignments": prop(task.Assignments, "Count"),
        }
    for index in range(1, project.Resources.Count + 1):
        resource = project.Resources(index)
        out.setdefault("resource_rows", {})[str(index)] = {
            "id": prop(resource, "ID"),
            "unique_id": prop(resource, "UniqueID"),
        }
    out["project_start"] = prop(project, "ProjectStart")
    out["project_finish"] = prop(project, "ProjectFinish")
    return out


# ---- bases built through Project ---------------------------------------------

def build_twoleaf(app, first_dur, second_dur, pending_dur, assignments, final_mode):
    """oracle_w35_calculate.build() for the unlinked shape: two automatic leaves
    built under AUTOMATIC calculation; optionally real assignments; then, for the
    calc pairs, MANUAL calculation and ONE pending change on A."""
    app.FileNew()
    project = app.ActiveProject
    neutralise(app, project, TITLE)
    app.Calculation = 1  # pjAutomatic
    project.ProjectStart = dt.datetime(2026, 1, 5, 8, 0)
    first = project.Tasks.Add("Task A")
    first.Manual = False
    first.Duration = first_dur
    second = project.Tasks.Add("Task B")
    second.Manual = False
    second.Duration = second_dur
    if assignments:
        resource = project.Resources.Add("Crew A")
        first.Assignments.Add(TaskID=first.ID, ResourceID=resource.ID)
        if assignments > 1:
            second.Assignments.Add(TaskID=second.ID, ResourceID=resource.ID)
    if final_mode == "manual":
        app.Calculation = 0  # pjManual
        first.Duration = pending_dur
    return project


def build_deep(app):
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
    for index, indents in ((1, 1), (2, 2), (3, 2), (4, 1)):
        for _ in range(indents):
            tasks[index].OutlineIndent()
    return project


# ---- setups (part of the control) ---------------------------------------------

def setup_note2(text):
    def setup(app, project):
        project.Tasks(2).Notes = text
    return setup


def setup_notes_2_and_3(app, project):
    project.Tasks(2).Notes = "Note two to clear."
    project.Tasks(3).Notes = "Note three to clear."


def setup_task1_forty(app, project):
    task = project.Tasks(1)
    task.Manual = False
    task.PercentComplete = 40


def setup_odd_calendar(app, project):
    project.BaseCalendars.Add("Odd Cal")


# ---- the edits ------------------------------------------------------------------

def set_note2(text):
    def edit(app, project):
        project.Tasks(2).Notes = text
        return {"call": "Tasks(2).Notes = <synthetic text>", "note_length": len(text)}
    return edit


def edit_note_same_session(app, project):
    project.Tasks(2).Notes = "First text set in this session."
    project.Tasks(2).Notes = "Second, longer text set in the same session."
    return {"call": "Tasks(2).Notes set, then replaced, in one session"}


def edit_note_kept(app, project):
    project.Tasks(3).Duration = 1920
    return {"call": "Tasks(3).Duration = 1920 (the note on task 2 untouched)"}


def edit_note_assignment(app, project):
    task = project.Tasks(1)
    for index in range(1, task.Assignments.Count + 1):
        candidate = task.Assignments(index)
        if candidate.ResourceUniqueID == 2:
            candidate.Notes = "Assignment note."
            return {"call": "Tasks(1).Assignments(<resource uid 2>).Notes = "
                            "<synthetic text>"}
    raise RuntimeError("task 1 has no assignment for resource uid 2")


def edit_note_shrink(app, project):
    project.Tasks(2).Notes = "Short."
    return {"call": "Tasks(2).Notes = 'Short.'", "before_length": len(LONG_NOTE)}


def edit_note_clear_two(app, project):
    project.Tasks(2).Notes = ""
    project.Tasks(3).Notes = ""
    return {"call": "Tasks(2).Notes = ''; Tasks(3).Notes = ''"}


def edit_fill_second(app, project):
    info = {}
    project.Tasks(1).ConstraintDate = utc(dt.datetime(2026, 1, 19, 8, 0))
    app.SelectTaskField(1, "Start", False, 0, 1)
    try:
        info["selection_tasks"] = app.ActiveSelection.Tasks.Count
    except Exception as exc:  # noqa: BLE001
        info["selection_error"] = repr(exc)
    app.FillDown(True)
    info["call"] = ("Tasks(1).ConstraintDate = 2026-01-19 08:00; "
                    "SelectTaskField(1, 'Start', False, 0, 1); FillDown(True)")
    return info


def edit_insert_two_milestones(app, project):
    for _ in range(2):
        app.SelectTaskField(Row=4, Column="Name", RowRelative=False)
        app.InsertMilestoneTask()
    return {"call": "twice: SelectTaskField(Row=4, Column='Name'); "
                    "InsertMilestoneTask()"}


def delete_resource(index):
    def edit(app, project):
        project.Resources(index).Delete()
        return {"call": f"Resources({index}).Delete()",
                "remaining": project.Resources.Count}
    return edit


def edit_calendar_delete(app, project):
    project.BaseCalendars("Odd Cal").Delete()
    return {"call": "BaseCalendars('Odd Cal').Delete()",
            "remaining": project.BaseCalendars.Count}


def edit_table_resource_column(app, project):
    returned = app.TableEdit(Name="Entry", TaskTable=False, Create=False,
                             OverwriteExisting=True, FieldName="Text1",
                             NewFieldName="Text1", Title="Text1", Width=12,
                             ColumnPosition=0)
    return {"call": "TableEdit(Name='Entry', TaskTable=False, Create=False, "
                    "OverwriteExisting=True, FieldName='Text1', "
                    "NewFieldName='Text1', Title='Text1', Width=12, ColumnPosition=0)",
            "returned": val(returned)}


def edit_table1_column(app, project):
    returned = app.TableEdit(Name="Review", TaskTable=True, Create=False,
                             OverwriteExisting=True, FieldName="Text1",
                             NewFieldName="Text1", Title="Text1", Width=12,
                             ColumnPosition=0)
    return {"call": "TableEdit(Name='Review', TaskTable=True, Create=False, "
                    "OverwriteExisting=True, FieldName='Text1', "
                    "NewFieldName='Text1', Title='Text1', Width=12, ColumnPosition=0)",
            "returned": val(returned)}


def edit_table1_date(app, project):
    returned = app.TableEdit(Name="Review", TaskTable=True, Create=False,
                             OverwriteExisting=True, DateFormat=5)
    return {"call": "TableEdit(Name='Review', TaskTable=True, Create=False, "
                    "OverwriteExisting=True, DateFormat=5)",
            "returned": val(returned)}


def edit_resource_rename(app, project):
    project.Resources(2).Name = "Renamed Resource Two"
    return {"call": "Resources(2).Name = <synthetic name>"}


def add_calendar(name):
    def edit(app, project):
        project.BaseCalendars.Add(name)
        return {"call": f"BaseCalendars.Add({name!r})", "name_length": len(name)}
    return edit


def edit_resource_add_fourth(app, project):
    project.Resources.Add("R4")
    return {"call": "Resources.Add('R4')", "resources": project.Resources.Count}


def edit_calculate(app, project):
    returned = app.CalculateProject()
    return {"call": "CalculateProject()", "returned": val(returned)}


def edit_column_width(app, project):
    field = project.TaskTables(1).TableFields(2)
    before = prop(field, "Width")
    field.Width = 30
    return {"call": "TaskTables(1).TableFields(2).Width = 30",
            "width_before": before, "width_after": prop(field, "Width")}


def edit_dsi_total_work(app, project):
    task = project.Tasks(2)
    task.Assignments.Add(TaskID=task.ID, ResourceID=2)
    return {"call": "Tasks(2).Assignments.Add(TaskID=2, ResourceID=2)",
            "project_work": prop(project.ProjectSummaryTask, "Work")}


def edit_dsi_dst_move(app, project):
    before = prop(project, "ProjectStart")
    project.ProjectStart = utc(dt.datetime(2026, 7, 6, 8, 0))
    return {"call": "ProjectStart = 2026-07-06 08:00 (UTC-neutral)",
            "project_start_before": before,
            "project_start_after": prop(project, "ProjectStart")}


def edit_percent_50(app, project):
    project.Tasks(1).PercentComplete = 50
    return {"call": "Tasks(1).PercentComplete = 50 (was 40)"}


def wbs_mask(length, separator):
    def edit(app, project):
        returned = app.WBSCodeMaskEdit("W", 1, 0, length, separator, True, True)
        return {"call": f"WBSCodeMaskEdit('W', 1, 0, {length}, {separator!r}, "
                        "True, True)", "returned": val(returned)}
    return edit


# pair -> dict(base, edit, setup, calc, source)
#   base: ("file", name) | ("twoleaf", first, second, pending, assignments, mode)
#         | ("deep",)
PAIRS_SPEC = {
    "note-tab": dict(base=("file", "synthetic-base.mpp"), edit=set_note2(TAB_NOTE),
                     calc="automatic", source="note-richtext.md:231 #1"),
    "note-paragraphs3": dict(base=("file", "synthetic-base.mpp"),
                             edit=set_note2(PARAGRAPHS_NOTE), calc="automatic",
                             source="note-richtext.md:231 #1"),
    "note-cjk": dict(base=("file", "synthetic-base.mpp"), edit=set_note2(CJK_NOTE),
                     calc="automatic", source="note-richtext.md:231 #2"),
    "note-same-session": dict(base=("file", "synthetic-base.mpp"),
                              edit=edit_note_same_session, calc="automatic",
                              source="note-richtext.md:231 #3"),
    "note-kept": dict(base=("file", "synthetic-base.mpp"),
                      setup=setup_note2("Existing note kept."),
                      edit=edit_note_kept, calc="automatic",
                      source="note-richtext.md:231 #4"),
    "note-assignment": dict(base=("file", "wave30-assign-add.mpp"),
                            edit=edit_note_assignment, calc="automatic",
                            source="note-richtext.md:231 #5"),
    "note-shrink": dict(base=("file", "synthetic-base.mpp"),
                        setup=setup_note2(LONG_NOTE), edit=edit_note_shrink,
                        calc="automatic", source="var-meta.md:355 #1"),
    "note-clear-two": dict(base=("file", "synthetic-base.mpp"),
                           setup=setup_notes_2_and_3, edit=edit_note_clear_two,
                           calc="automatic", source="var-meta.md:355 #2"),
    "fill-second-edit": dict(base=("file", "wave31-fill.mpp"),
                             edit=edit_fill_second, calc="automatic",
                             source="var-meta.md:355 #3"),
    "insert-two-milestones": dict(base=("deep",), edit=edit_insert_two_milestones,
                                  calc="automatic", source="table-growth.md:308 #1"),
    "resource-delete-last": dict(base=("file", "wave34-resource-add-third.mpp"),
                                 edit=delete_resource(3), calc="automatic",
                                 source="table-growth.md:308 #2; "
                                        "calendar-tbkdcal.md:184 #2"),
    "resource-delete-middle": dict(base=("file", "wave34-resource-add-third.mpp"),
                                   edit=delete_resource(2), calc="automatic",
                                   source="table-growth.md:308 #2; "
                                          "calendar-tbkdcal.md:184 #2"),
    "calendar-delete": dict(base=("file", "synthetic-base.mpp"),
                            setup=setup_odd_calendar, edit=edit_calendar_delete,
                            calc="automatic", source="table-growth.md:308 #2"),
    "table-resource-column": dict(base=("file", "synthetic-base.mpp"),
                                  edit=edit_table_resource_column, calc="automatic",
                                  source="table-growth.md:308 #3"),
    "table1-column-replace": dict(base=("file", "wave31-tables.mpp"),
                                  edit=edit_table1_column, calc="automatic",
                                  source="table-growth.md:308 #3"),
    "table1-date-format": dict(base=("file", "wave31-tables.mpp"),
                               edit=edit_table1_date, calc="automatic",
                               source="table-growth.md:308 #3"),
    "resource-rename": dict(base=("file", "wave34-resource-add-third.mpp"),
                            edit=edit_resource_rename, calc="automatic",
                            source="calendar-tbkdcal.md:184 #1"),
    "calendar-add-odd": dict(base=("file", "synthetic-base.mpp"),
                             edit=add_calendar("Odd Cal"), calc="automatic",
                             source="calendar-tbkdcal.md:184 #3"),
    "calendar-add-even": dict(base=("file", "synthetic-base.mpp"),
                              edit=add_calendar("Even Cal"), calc="automatic",
                              source="calendar-tbkdcal.md:184 #3"),
    "resource-add-fourth": dict(base=("file", "wave34-resource-add-third.mpp"),
                                edit=edit_resource_add_fourth, calc="automatic",
                                source="calendar-tbkdcal.md:184 #4"),
    "calc-assign-1": dict(base=("twoleaf", 2400, 1440, 4800, 1, "manual"),
                          edit=edit_calculate, calc="manual",
                          source="scheduler-save.md:157 #1 and #4"),
    "calc-assign-2": dict(base=("twoleaf", 2400, 1440, 4800, 2, "manual"),
                          edit=edit_calculate, calc="manual",
                          source="scheduler-save.md:157 #1 and #4"),
    "calc-9d-to-10d": dict(base=("twoleaf", 4320, 1440, 4800, 0, "manual"),
                           edit=edit_calculate, calc="manual",
                           source="scheduler-save.md:157 #2"),
    "table-column-width": dict(base=("file", "synthetic-base.mpp"),
                               edit=edit_column_width, calc="automatic",
                               source="view-stream-214.md:295 #1"),
    "dsi-total-work": dict(base=("file", "wave30-assign-add.mpp"),
                           edit=edit_dsi_total_work, calc="automatic",
                           source="document-summary-information.md:187 #1"),
    "dsi-dst-move": dict(base=("twoleaf", 2400, 1440, None, 0, "automatic"),
                         edit=edit_dsi_dst_move, calc="automatic",
                         source="document-summary-information.md:187 #3"),
    "dsi-percent-same-length": dict(base=("file", "synthetic-base.mpp"),
                                    setup=setup_task1_forty, edit=edit_percent_50,
                                    calc="automatic",
                                    source="document-summary-information.md:187 #4"),
    "wbs-mask-length3": dict(base=("file", "synthetic-base.mpp"),
                             edit=wbs_mask(3, "."), calc="automatic",
                             source="props-records.md:287 #3"),
    "wbs-mask-dash": dict(base=("file", "synthetic-base.mpp"),
                          edit=wbs_mask(2, "-"), calc="automatic",
                          source="props-records.md:287 #3"),
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


def set_mode(app, calc):
    app.Calculation = 0 if calc == "manual" else 1


def build_base(app, pair, entry):
    spec = PAIRS_SPEC[pair]
    base = spec["base"]
    if base[0] == "twoleaf":
        _, first, second, pending, assignments, mode = base
        entry["base"] = f"built: two unlinked leaves {first}/{second}, {assignments} assignment(s)"
        return build_twoleaf(app, first, second, pending, assignments, mode)
    if base[0] == "deep":
        entry["base"] = "built: wave 35 G3 deep outline"
        return build_deep(app)
    staged = working_copy(os.path.join(DATA, base[1]), "r7-spec-" + pair)
    copy = f"{WORK}/{pair}-base.mpp"
    shutil.copyfile(staged, copy)
    app.FileOpen(copy)
    project = app.ActiveProject
    neutralise(app, project, TITLE)
    set_mode(app, spec["calc"])
    entry["base"] = base[1]
    return project


def measure(app, pair, entry):
    spec = PAIRS_SPEC[pair]
    entry["source_list"] = spec["source"]
    entry["calculation_mode"] = spec["calc"]
    project = build_base(app, pair, entry)
    setup = spec.get("setup")
    if setup is not None:
        try:
            setup(app, project)
        except Exception as exc:  # noqa: BLE001
            entry["edit"] = f"<setup raised: {type(exc).__name__}: {exc}>"
            app.FileClose(0)
            discard(pair)
            return False
    entry["pending_facts"] = facts(app, project)
    app.FileSaveAs(seed_path(pair))
    app.FileClose(0)

    app.FileOpen(seed_path(pair))
    project = app.ActiveProject
    set_mode(app, spec["calc"])
    entry["control_facts"] = facts(app, project)
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


def host_offsets():
    """The host's UTC offset in hours on 05 Jan and 06 Jul 2026 (DST evidence)."""
    out = {}
    for label, stamp in (("2026-01-05", (2026, 1, 5, 8, 0, 0, 0, 0, -1)),
                         ("2026-07-06", (2026, 7, 6, 8, 0, 0, 0, 0, -1))):
        seconds = time.mktime(stamp)
        local = time.localtime(seconds)
        out[label] = (local.tm_gmtoff or 0) / 3600.0
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--list", action="store_true",
                    help="print the pairs and do nothing else")
    ap.add_argument("pairs", nargs="*")
    args = ap.parse_args()

    if args.list:
        print(f"neutral root {NEUTRAL}")
        for pair, spec in PAIRS_SPEC.items():
            base = spec["base"]
            where = base[1] if base[0] == "file" else base[0]
            print(f"{pair}: base {where}; calculation {spec['calc']}; "
                  f"from {spec['source']}")
        print(f"{len(PAIRS_SPEC)} pairs.  NOT made:")
        for item, reason in SKIPPED:
            print(f"  {item}: {reason}")
        return

    pairs = tuple(args.pairs) or PAIRS
    for pair in pairs:
        if pair not in PAIRS_SPEC:
            sys.exit(f"unknown pair {pair}")
        base = PAIRS_SPEC[pair]["base"]
        if base[0] == "file" and not os.path.isfile(os.path.join(DATA, base[1])):
            sys.exit(f"base missing: tests/data/{base[1]}")
        for path in targets(pair):
            if os.path.exists(path):
                sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    for directory in (FIXTURE_DIR, NULL_DIR, WORK, OUT):
        os.makedirs(directory, exist_ok=True)

    result = {"version": None, "pairs": {}, "host_utc_offset_hours": host_offsets()}
    if os.path.exists(RESULT):
        with open(RESULT, encoding="utf-8") as handle:
            previous = json.load(handle)
        previous["host_utc_offset_hours"] = result["host_utc_offset_hours"]
        result = previous
    with oracle_session() as app:
        with Guard(3600):
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
