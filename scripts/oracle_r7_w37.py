#!/usr/bin/env python3
"""Round 7, wave 37 recordings: one-factor pair sets for Calculate, Set Baseline and Notes.

The operator's word, Wed 11 Nov 2026: all the required recordings are made before the
Project 2021 pass in the trace VM ends.  Wave 37 builds three writers (Notes, Set Baseline,
Calculate with links, lags and constraints).  Each rule needs pairs that vary ONE factor
at a time, with at least two values per factor: one pair derives the rule, another is held
out to test it.

Every pair has its own control, edit and null (the g3 chain: build or open a base, save a
SEED, reopen it, save the CONTROL, make ONE edit, save the EDIT, reopen both for facts,
the control's NULL from a COPY of the control with Application.UserName only).  A setup
(an existing baseline, a note) is part of the control.  Pair id = <group>-<factor>-<value>;
fixtures wave36-r7-w37-<pair id>[-control|-control-null].mpp under the neutral root
C:/w35/r7-w37.

GROUP A, Calculate Project (calculation MANUAL, exactly oracle_w35_calculate.py: built under
AUTOMATIC calculation, then manual calculation with ONE pending change, A's duration; the
control and the edit are saved in manual).  Base: two leaves A then B ("Task A", "Task B"),
automatic, linked A -> B (FS+0) unless the factor says otherwise, project start Mon 05 Jan
2026 08:00, A 2400 changed to 4800, B 1440.  The edit is Application.CalculateProject().
  link        fs ss ff sf             link type A -> B, lag 0
  lag         0d (= link-fs) p1d p3d m1d p50pct    lag on an FS link (minutes; "50%")
  constraint  snet snlt fnet fnlt mso mfo, each with two dates, on B (type then date, as
              the wave-35 snet pair); one date binds or conflicts after A's new finish
              and one does not
  weekend     2d 3d 4d                project start Thu 08 Jan 2026, A 1d pending 2d, 3d, 4d
  calendar    0114 0115 (none = link-fs)   a daily non-working exception on a weekday inside
              A's new span (Wed 14 Jan, Thu 15 Jan), added to the project calendar
  preds       one two                 a third task C (7d); B follows A only, or A and C

GROUP B, Set Baseline (calculation MANUAL by default: the committed wave-30 pair was made
in manual mode and the round-4 trace showed that opening the file in automatic mode
recalculates it, so the mode is a factor: mode-automatic).  Base: tests/data/synthetic-base.mpp
(three tasks, no links, one placeholder assignment per row) on a working copy, unless the
factor is the assignment count, which uses a project built in this session (three automatic
tasks).  The edit is Application.BaselineSave(All, Copy, Into).
  scope       all (reference, Baseline, entire project; = into-baseline = mode-manual)
              selected-1 (row 2 selected) selected-2 (rows 1 and 2 selected)
  into        baseline1 baseline2 baseline10  (BaselineSave(True, 0, n); n read back)
  rebaseline  same (a baseline exists; the same save again) changed (Task 1's duration
              changed after the first baseline) into1 (Baseline exists; save Baseline1)
  assign      none resource-only one two   (built project: 0 assignments and 0 resources;
              0 assignments and 1 resource; 1 assignment; 2 assignments)
  interim     1 2  Set Interim Plan, Start1/Finish1 and Start2/Finish2.  The Into code is
              not in the repo (no type library), so a scratch project is probed first
              (Copy 0 and 1, Into 11..30 then 1..10; the first call that fills Start1 and
              Start2 is used) and the probe is recorded in the JSON
  mode        automatic   the reference with AUTOMATIC calculation

GROUP C, Notes, beyond oracle_r7_spec.py (calculation AUTOMATIC, set explicitly): note text
length on Tasks(2).Notes of the synthetic base, 30 / 300 / 600 / 1200 characters, and a note
on the PROJECT SUMMARY task (30 and 600 characters).  Tasks(2) with the 30-character text
is the reference for the summary-task pairs.

Calculation mode is stated per pair in --list.  Facts, by ID and UniqueID and note LENGTH
only (no name or note text is read back), go to build/r7/oracle_r7_w37.json.  Nothing is
written to tests/data.

    python oracle_r7_w37.py --list
    python oracle_r7_w37.py --group A|B|C [--utc]
    python oracle_r7_w37.py [pair id ...]

With no argument every pair runs.  The script refuses to run if a target exists.  --utc
sends the dates through oracle_w3.utc() (use it if a control guard refuses because a date
read back is not the date sent).
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
from oracle_w3 import Guard, utc  # noqa: E402

NEUTRAL = "C:/w35/r7-w37"
FIXTURE_DIR = NEUTRAL + "/fixtures"
NULL_DIR = NEUTRAL + "/nulls"
WORK = NEUTRAL + "/work"
OUT = os.path.join(ROOT, "build", "r7")
RESULT = os.path.join(OUT, "oracle_r7_w37.json")
DATA = os.path.join(ROOT, "tests", "data")
BASE_FILE = "synthetic-base.mpp"
USE_UTC = False

LINK_CODE = {0: "FF", 1: "FS", 2: "SF", 3: "SS"}


def dtv(value):
    """A date as Project receives it: naive as the wave-35 oracle sends it, or UTC-neutral."""
    return utc(value) if USE_UTC else value


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


def stamp(value):
    return value.strftime("%Y-%m-%d %H:%M:%S")


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def make_text(length):
    """Synthetic ASCII note text of exactly `length` characters (no RTF specials)."""
    unit = "Synthetic note sentence for the length pairs, number %02d. "
    out = ""
    number = 1
    while len(out) < length:
        out += unit % number
        number += 1
    return out[:length]


# ---- facts -------------------------------------------------------------------

def slot_name(k, kind):
    if kind == "start":
        return "BaselineStart" if k == 0 else f"Baseline{k}Start"
    return "BaselineFinish" if k == 0 else f"Baseline{k}Finish"


def dependencies(task):
    out = []
    try:
        for index in range(1, task.TaskDependencies.Count + 1):
            dep = task.TaskDependencies(index)
            row = {"type": prop(dep, "Type"), "lag": prop(dep, "Lag")}
            try:
                row["from_uid"] = val(getattr(dep, "From").UniqueID)
                row["to_uid"] = val(getattr(dep, "To").UniqueID)
            except Exception as exc:  # noqa: BLE001
                row["ends_unreadable"] = repr(exc)
            out.append(row)
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"
    return out


def notes_len(obj):
    try:
        return len(obj.Notes)
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def task_facts(task, baselines):
    out = {
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "outline_level": prop(task, "OutlineLevel"),
        "manual": prop(task, "Manual"),
        "duration": prop(task, "Duration"),
        "work": prop(task, "Work"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "percent": prop(task, "PercentComplete"),
        "total_slack": prop(task, "TotalSlack"),
        "critical": prop(task, "Critical"),
        "constraint_type": prop(task, "ConstraintType"),
        "constraint_date": prop(task, "ConstraintDate"),
        "predecessors": prop(task, "Predecessors"),
        "dependencies": dependencies(task),
        "notes_len": notes_len(task),
        "assignments": prop(task.Assignments, "Count"),
    }
    if baselines:
        out["baseline_start"] = {str(k): prop(task, slot_name(k, "start"))
                                 for k in range(0, 11)}
        out["baseline_finish"] = {str(k): prop(task, slot_name(k, "finish"))
                                  for k in range(0, 11)}
        out["interim_start"] = {str(k): prop(task, f"Start{k}") for k in range(1, 11)}
        out["interim_finish"] = {str(k): prop(task, f"Finish{k}") for k in range(1, 11)}
    return out


def facts(app, project, group):
    baselines = group == "b"
    out = {"calculation": prop(app, "Calculation"), "tasks": {},
           "resources": prop(project.Resources, "Count")}
    for index in range(1, project.Tasks.Count + 1):
        out["tasks"][str(index)] = task_facts(project.Tasks(index), baselines)
    try:
        summary = project.ProjectSummaryTask
        out["summary"] = {"id": prop(summary, "ID"),
                          "unique_id": prop(summary, "UniqueID"),
                          "start": prop(summary, "Start"),
                          "finish": prop(summary, "Finish"),
                          "duration": prop(summary, "Duration"),
                          "notes_len": notes_len(summary)}
        if baselines:
            out["summary"]["baseline_start"] = prop(summary, "BaselineStart")
    except Exception as exc:  # noqa: BLE001
        out["summary"] = f"<unreadable: {exc}>"
    try:
        out["calendar_exceptions"] = project.Calendar.Exceptions.Count
    except Exception as exc:  # noqa: BLE001
        out["calendar_exceptions"] = f"<unreadable: {exc}>"
    out["project_start"] = prop(project, "ProjectStart")
    out["project_finish"] = prop(project, "ProjectFinish")
    return out


# ---- group A: Calculate bases ------------------------------------------------

CALC_DEFAULT = dict(start=dt.datetime(2026, 1, 5, 8, 0), a_orig=2400, b_dur=1440,
                    pending=4800, link=(1, 0), constraint=None, exception=None,
                    third=None)
THIRD_DURATION = 3360  # 7 days: between A's old (5 d) and new (10 d) length


def add_link(second, first, ltype, lag, entry):
    """TaskDependencies.Add, with the Predecessors text as the fallback for a % lag."""
    before = second.TaskDependencies.Count
    try:
        second.TaskDependencies.Add(first, ltype, lag)
        entry.setdefault("link_methods", []).append(
            f"TaskDependencies.Add(type {ltype}, lag {lag!r})")
        return
    except Exception as exc:  # noqa: BLE001
        entry.setdefault("link_methods", []).append(
            f"TaskDependencies.Add raised {type(exc).__name__}: {exc}")
        if not (isinstance(lag, str) and lag.endswith("%")):
            raise
    if second.TaskDependencies.Count != before:
        return
    text = f"{first.ID}{LINK_CODE[ltype]}+{lag}"
    second.Predecessors = text
    entry["link_methods"].append(f"Predecessors = {text!r}")


def build_calc(app, pair, p, entry):
    """oracle_w35_calculate.build(): AUTOMATIC, rows, link, constraint, then MANUAL and
    one pending change on A."""
    app.FileNew()
    project = app.ActiveProject
    neutralise(app, project, f"Synthetic W37 {pair} Fixture")
    app.Calculation = 1  # pjAutomatic
    project.ProjectStart = dtv(p["start"])
    if p["exception"] is not None:
        day = p["exception"]
        project.Calendar.Exceptions.Add(Type=1, Start=utc(day), Finish=utc(day),
                                        Occurrences=0, Name="Closure", Period=1)
    first = project.Tasks.Add("Task A")
    first.Manual = False
    first.Duration = p["a_orig"]
    second = project.Tasks.Add("Task B")
    second.Manual = False
    second.Duration = p["b_dur"]
    third = None
    if p["third"] is not None:
        third = project.Tasks.Add("Task C")
        third.Manual = False
        third.Duration = THIRD_DURATION
    if p["link"] is not None:
        add_link(second, first, p["link"][0], p["link"][1], entry)
    if p["third"] == "two":
        add_link(second, third, 1, 0, entry)
    if p["constraint"] is not None:
        ctype, when = p["constraint"]
        second.ConstraintType = ctype
        second.ConstraintDate = dtv(when)
    app.Calculation = 0  # pjManual
    first.Duration = p["pending"]
    return project


def expected_dependencies(p):
    count = 1 if p["link"] is not None else 0
    if p["third"] == "two":
        count += 1
    return count


def check_calc_control(p, control):
    """oracle_w35_calculate's guards on the control, per the pair's shape."""
    tasks = control["tasks"]
    first = tasks["1"]
    if not str(first["start"]).startswith(p["start"].strftime("%Y-%m-%d")):
        return f"task 1 of the control starts {first['start']}, not at the project start"
    if float(first["duration"]) != float(p["pending"]):
        return f"the control lost the pending duration ({first['duration']!r})"
    second = tasks["2"]
    deps = second["dependencies"]
    if not isinstance(deps, list) or len(deps) != expected_dependencies(p):
        return (f"task 2 has {deps!r} dependencies, expected "
                f"{expected_dependencies(p)}")
    if p["constraint"] is not None:
        ctype, when = p["constraint"]
        if str(second["constraint_type"]) != str(ctype):
            return (f"task 2 constraint type is {second['constraint_type']!r}, "
                    f"expected {ctype} (run with --utc if only the date differs)")
        if second["constraint_date"] != stamp(when):
            return (f"task 2 constraint date is {second['constraint_date']!r}, sent "
                    f"{stamp(when)} (run with --utc)")
    if p["exception"] is not None and control["calendar_exceptions"] != 1:
        return f"the calendar holds {control['calendar_exceptions']!r} exceptions, not 1"
    return None


def calculate(app, project):
    returned = app.CalculateProject()
    return {"call": "CalculateProject()", "returned": val(returned)}


# ---- group B: Set Baseline ----------------------------------------------------

INTERIM = {}  # slot -> (copy, into), filled by probe_interim()


def baseline_slot_set(task, k):
    value = getattr(task, slot_name(k, "start"))
    return isinstance(value, dt.datetime)


def interim_slot_set(task, k):
    value = getattr(task, f"Start{k}")
    return isinstance(value, dt.datetime)


def probe_interim(app, record):
    """Find the BaselineSave(Copy, Into) that fills Start1 and Start2 in a scratch project
    that is never saved."""
    record["attempts"] = []
    app.FileNew()
    try:
        project = app.ActiveProject
        neutralise(app, project, "Synthetic W37 Interim Probe")
        app.Calculation = 1
        project.ProjectStart = dtv(dt.datetime(2026, 1, 5, 8, 0))
        for name, dur in (("Task A", 2400), ("Task B", 1440)):
            task = project.Tasks.Add(name)
            task.Manual = False
            task.Duration = dur
        probe_task = project.Tasks(1)
        candidates = list(range(11, 31)) + list(range(1, 11))
        for copy in (0, 1):
            for into in candidates:
                if 1 in INTERIM and 2 in INTERIM:
                    break
                try:
                    app.BaselineSave(True, copy, into)
                except Exception as exc:  # noqa: BLE001
                    record["attempts"].append(
                        {"copy": copy, "into": into,
                         "raised": f"{type(exc).__name__}: {exc}"})
                    continue
                hit = [k for k in range(1, 11)
                       if interim_slot_set(probe_task, k) and k not in INTERIM]
                record["attempts"].append({"copy": copy, "into": into,
                                           "interim_slots_newly_set": hit})
                for k in hit:
                    INTERIM[k] = (copy, into)
    finally:
        try:
            app.FileClose(0)
        except Exception as exc:  # noqa: BLE001
            record["close_error"] = repr(exc)
    record["found"] = {str(k): list(v) for k, v in sorted(INTERIM.items())}


def select_row(app, row):
    app.SelectTaskField(Row=row, Column="Name", RowRelative=False)


def selection_tasks(app):
    try:
        return app.ActiveSelection.Tasks.Count
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def edit_baseline_all(app, project):
    app.BaselineSave(True, 0, 0)
    return {"call": "BaselineSave(All=True, Copy=0, Into=0)"}


def edit_baseline_selected(rows):
    def edit(app, project):
        select_row(app, rows[0])
        info = {}
        if len(rows) == 2:
            app.SelectTaskField(Row=rows[1], Column="Name", RowRelative=False,
                                Extend=True)
        count = selection_tasks(app)
        info["selection_tasks_count_after_extend"] = count
        if count != len(rows):
            select_row(app, rows[0])
            app.SelectTaskField(Row=rows[0], Column="Name", RowRelative=False,
                                Width=0, Height=len(rows) - 1)
            count = selection_tasks(app)
            info["selection_tasks_count_after_height"] = count
        if count != len(rows):
            raise RuntimeError(
                f"ActiveSelection.Tasks.Count is {count!r}, not {len(rows)}")
        log(f"selected {rows}: ActiveSelection.Tasks.Count = {count!r}")
        app.BaselineSave(False, 0, 0)
        info["call"] = (f"rows {rows} selected; BaselineSave(All=False, Copy=0, Into=0)")
        info["selection_tasks_count"] = count
        info["baseline_start_after"] = {
            str(i): prop(project.Tasks(i), "BaselineStart")
            for i in range(1, project.Tasks.Count + 1)}
        return info
    return edit


def edit_baseline_into(slot):
    def edit(app, project):
        app.BaselineSave(True, 0, slot)
        task = project.Tasks(1)
        if not baseline_slot_set(task, slot):
            raise RuntimeError(f"BaselineSave(True, 0, {slot}) did not fill "
                               f"Baseline{slot}Start on task 1")
        return {"call": f"BaselineSave(All=True, Copy=0, Into={slot})",
                "baseline0_set": baseline_slot_set(task, 0)}
    return edit


def edit_interim(slot):
    def edit(app, project):
        if slot not in INTERIM:
            raise RuntimeError(f"the probe found no BaselineSave call for Start{slot}")
        copy, into = INTERIM[slot]
        app.BaselineSave(True, copy, into)
        if not interim_slot_set(project.Tasks(1), slot):
            raise RuntimeError(f"BaselineSave(True, {copy}, {into}) left Start{slot} unset")
        return {"call": f"BaselineSave(All=True, Copy={copy}, Into={into}) "
                        f"(Set Interim Plan, Start{slot}/Finish{slot})",
                "baseline0_set": baseline_slot_set(project.Tasks(1), 0)}
    return edit


def setup_baseline0(app, project):
    app.BaselineSave(True, 0, 0)
    if not baseline_slot_set(project.Tasks(1), 0):
        raise RuntimeError("the setup baseline did not fill BaselineStart on task 1")


def setup_baseline0_then_change(app, project):
    setup_baseline0(app, project)
    task = project.Tasks(1)
    try:
        before = float(task.Duration)
    except Exception:  # noqa: BLE001
        before = 2400.0
    task.Duration = before + 960.0  # two more working days, in minutes


def build_assign(app, pair, count, resource, entry):
    """Three automatic tasks built in this session: `count` assignments of one resource."""
    app.FileNew()
    project = app.ActiveProject
    neutralise(app, project, f"Synthetic W37 {pair} Fixture")
    app.Calculation = 1  # pjAutomatic
    project.ProjectStart = dtv(dt.datetime(2026, 1, 5, 8, 0))
    tasks = []
    for name, dur in (("Task A", 2400), ("Task B", 1440), ("Task C", 960)):
        task = project.Tasks.Add(name)
        task.Manual = False
        task.Duration = dur
        tasks.append(task)
    if resource:
        crew = project.Resources.Add("Crew A")
        for task in tasks[:count]:
            task.Assignments.Add(TaskID=task.ID, ResourceID=crew.ID)
    entry["assignments_total"] = sum(
        int(prop(t.Assignments, "Count")) for t in tasks)
    return project


# ---- group C: notes -------------------------------------------------------------

def edit_note_task2(length):
    def edit(app, project):
        text = make_text(length)
        project.Tasks(2).Notes = text
        return {"call": f"Tasks(2).Notes = <synthetic text, {length} characters>",
                "note_length": len(text), "notes_len_after": notes_len(project.Tasks(2))}
    return edit


def edit_note_summary(length):
    def edit(app, project):
        text = make_text(length)
        project.ProjectSummaryTask.Notes = text
        return {"call": f"ProjectSummaryTask.Notes = <synthetic text, {length} characters>",
                "note_length": len(text),
                "notes_len_after": notes_len(project.ProjectSummaryTask)}
    return edit


# ---- the pair table ----------------------------------------------------------------

PAIRS = {}
ALIASES = {
    "a-lag-0d": "a-link-fs",
    "a-calendar-none": "a-link-fs",
    "a-constraint-none": "a-link-fs",
    "b-into-baseline": "b-scope-all",
    "b-mode-manual": "b-scope-all",
    "c-note-task-short": "c-note-len-short",
}


def add(group, factor, value, **spec):
    pair = f"{group}-{factor}-{value}"
    spec.update(group=group, factor=factor, value=value)
    PAIRS[pair] = spec


def add_calc(factor, value, text, **over):
    p = dict(CALC_DEFAULT)
    p.update(over)
    add("a", factor, value, text=text, calc="manual", base=("calc", p),
        edit=calculate)


for _value, _type in (("fs", 1), ("ss", 3), ("ff", 0), ("sf", 2)):
    add_calc("link", _value, f"link type {LINK_CODE[_type]}, lag 0", link=(_type, 0))
for _value, _lag, _text in (("p1d", 480, "FS +1d"), ("p3d", 1440, "FS +3d"),
                            ("m1d", -480, "FS -1d"), ("p50pct", "50%", "FS +50%")):
    add_calc("lag", _value, f"lag {_text} (0d is a-link-fs)", link=(1, _lag))

# constraint: (name, PjConstraint, ((mmdd tag, datetime), (mmdd tag, datetime)))
CONSTRAINTS = (
    ("snet", 4, ((1, 26, 8), (1, 12, 8))),
    ("snlt", 5, ((1, 12, 8), (2, 2, 8))),
    ("fnet", 6, ((1, 29, 17), (1, 14, 17))),
    ("fnlt", 7, ((1, 14, 17), (2, 3, 17))),
    ("mso", 2, ((1, 26, 8), (1, 12, 8))),
    ("mfo", 3, ((1, 29, 17), (1, 14, 17))),
)
for _name, _ctype, _dates in CONSTRAINTS:
    for _month, _day, _hour in _dates:
        add_calc("constraint", f"{_name}-{_month:02d}{_day:02d}",
                 f"{_name.upper()} on B, 2026-{_month:02d}-{_day:02d} {_hour:02d}:00 "
                 f"(PjConstraint {_ctype})",
                 constraint=(_ctype, dt.datetime(2026, _month, _day, _hour, 0)))

for _value, _pending in (("2d", 960), ("3d", 1440), ("4d", 1920)):
    add_calc("weekend", _value,
             f"start Thu 08 Jan 2026, A 1d changed to {_value}",
             start=dt.datetime(2026, 1, 8, 8, 0), a_orig=480, pending=_pending)
for _value, _day in (("0114", 14), ("0115", 15)):
    add_calc("calendar", _value,
             f"non-working day Jan {_day} 2026 inside A's new span (none is a-link-fs)",
             exception=dt.datetime(2026, 1, _day, 0, 0))
for _value in ("one", "two"):
    add_calc("preds", _value,
             f"third task C (7d); B follows {'A only' if _value == 'one' else 'A and C'}",
             third=_value)

# group B
add("b", "scope", "all", text="BaselineSave(All=True, Copy=0, Into=0), reference",
    calc="manual", base=("file", BASE_FILE), edit=edit_baseline_all)
add("b", "scope", "selected-1", text="Baseline for the selected task, row 2 only",
    calc="manual", base=("file", BASE_FILE), edit=edit_baseline_selected((2,)))
add("b", "scope", "selected-2", text="Baseline for the selected tasks, rows 1 and 2",
    calc="manual", base=("file", BASE_FILE), edit=edit_baseline_selected((1, 2)))
for _slot in (1, 2, 10):
    add("b", "into", f"baseline{_slot}", text=f"Baseline {_slot}, entire project",
        calc="manual", base=("file", BASE_FILE), edit=edit_baseline_into(_slot))
add("b", "rebaseline", "same",
    text="a baseline exists; the same save again", calc="manual",
    base=("file", BASE_FILE), setup=setup_baseline0, edit=edit_baseline_all)
add("b", "rebaseline", "changed",
    text="a baseline exists; Task 1's duration changed; saved again", calc="manual",
    base=("file", BASE_FILE), setup=setup_baseline0_then_change,
    edit=edit_baseline_all)
add("b", "rebaseline", "into1",
    text="a baseline exists; Baseline 1 saved beside it", calc="manual",
    base=("file", BASE_FILE), setup=setup_baseline0, edit=edit_baseline_into(1))
for _value, _count, _resource, _text in (
        ("none", 0, False, "built project, no resource, no assignment"),
        ("resource-only", 0, True, "built project, one resource, no assignment"),
        ("one", 1, True, "built project, one assignment"),
        ("two", 2, True, "built project, two assignments")):
    add("b", "assign", _value, text=_text, calc="manual",
        base=("assign", _count, _resource), edit=edit_baseline_all)
for _slot in (1, 2):
    add("b", "interim", str(_slot),
        text=f"Set Interim Plan into Start{_slot}/Finish{_slot}", calc="manual",
        base=("file", BASE_FILE), edit=edit_interim(_slot), probe=True)
add("b", "mode", "automatic", text="the reference with AUTOMATIC calculation",
    calc="automatic", base=("file", BASE_FILE), edit=edit_baseline_all)

# group C
for _value, _length in (("short", 30), ("300", 300), ("600", 600), ("1200", 1200)):
    add("c", "note-len", _value, text=f"Tasks(2).Notes, {_length} characters",
        calc="automatic", base=("file", BASE_FILE), edit=edit_note_task2(_length))
for _value, _length in (("short", 30), ("600", 600)):
    add("c", "note-summary", _value,
        text=f"ProjectSummaryTask.Notes, {_length} characters (Tasks(2) at 30 characters "
             "is c-note-len-short)",
        calc="automatic", base=("file", BASE_FILE), edit=edit_note_summary(_length))

# rename c-note-len-short pairs consistently with the alias table
PAIRS["c-note-len-short"]["alias_note"] = "also serves as c-note-task-short"

NOT_MADE = [
    ("A: constraint types with a deadline date or a task calendar",
     "not asked; the deadline (Task.Deadline) is a separate field"),
    ("A: lag in elapsed days (1ed)",
     "not asked; only working-day lags and one percentage lag"),
    ("B: Set Baseline with RollUp or ToSummaries",
     "needs an outline with summary tasks; the synthetic base has none"),
    ("B: interim plan Copy from a baseline rather than from the schedule",
     "the Copy code is not in the repo; the probe tries Copy 0 and 1 only"),
    ("C: a note on an outline summary task",
     "the synthetic base has no outline; only the project summary task is made"),
    ("C: where the second var block starts",
     "the block size is not known to this lane; 30, 300, 600 and 1200 characters are made"),
]


def group_of(pair):
    return PAIRS[pair]["group"].upper()


def control_path(pair):
    return f"{FIXTURE_DIR}/wave36-r7-w37-{pair}-control.mpp"


def edit_path(pair):
    return f"{FIXTURE_DIR}/wave36-r7-w37-{pair}.mpp"


def null_path(pair):
    return f"{NULL_DIR}/wave36-r7-w37-{pair}-control-null.mpp"


def seed_path(pair):
    return f"{WORK}/{pair}-seed.mpp"


def copy_path(pair):
    return f"{WORK}/{pair}-control-for-null.mpp"


def targets(pair):
    return (control_path(pair), edit_path(pair), null_path(pair))


def discard(pair):
    for path in targets(pair) + (seed_path(pair), copy_path(pair)):
        if os.path.exists(path):
            os.remove(path)


def set_mode(app, calc):
    app.Calculation = 0 if calc == "manual" else 1


def build_base(app, pair, entry):
    spec = PAIRS[pair]
    base = spec["base"]
    if base[0] == "calc":
        entry["base"] = "built: two leaves " + json.dumps(
            {k: (stamp(v) if isinstance(v, dt.datetime) else v)
             for k, v in base[1].items() if k not in ("constraint", "exception")})
        return build_calc(app, pair, base[1], entry)
    if base[0] == "assign":
        entry["base"] = f"built: three tasks, {base[1]} assignment(s), resource {base[2]}"
        project = build_assign(app, pair, base[1], base[2], entry)
        set_mode(app, spec["calc"])
        return project
    staged = working_copy(os.path.join(DATA, base[1]), "r7-w37-" + pair)
    copy = f"{WORK}/{pair}-base.mpp"
    shutil.copyfile(staged, copy)
    app.FileOpen(copy)
    project = app.ActiveProject
    neutralise(app, project, f"Synthetic W37 {pair} Fixture")
    set_mode(app, spec["calc"])
    entry["base"] = base[1]
    return project


def measure(app, pair, entry):
    spec = PAIRS[pair]
    group = spec["group"]
    entry.update(group=group.upper(), factor=spec["factor"], value=spec["value"],
                 text=spec["text"], calculation_mode=spec["calc"])
    project = build_base(app, pair, entry)
    setup = spec.get("setup")
    if setup is not None:
        setup(app, project)
    entry["pending_facts"] = facts(app, project, group)
    app.FileSaveAs(seed_path(pair))
    app.FileClose(0)

    app.FileOpen(seed_path(pair))
    project = app.ActiveProject
    set_mode(app, spec["calc"])
    entry["control_facts"] = facts(app, project, group)
    if spec["base"][0] == "calc":
        refusal = check_calc_control(spec["base"][1], entry["control_facts"])
        if refusal:
            raise RuntimeError(refusal)
    app.FileSaveAs(control_path(pair))

    entry["edit"] = spec["edit"](app, project)
    entry["edit_facts"] = facts(app, project, group)
    app.FileSaveAs(edit_path(pair))
    app.FileClose(0)
    os.remove(seed_path(pair))

    for key, path in (("control", control_path(pair)), ("edit", edit_path(pair))):
        app.FileOpen(path)
        entry[key + "_reopen"] = facts(app, app.ActiveProject, group)
        app.FileClose(0)

    # The control's null save: open a COPY, save without an edit.
    shutil.copyfile(control_path(pair), copy_path(pair))
    app.FileOpen(copy_path(pair))
    app.UserName = AUTHOR
    app.FileSaveAs(null_path(pair))
    app.FileClose(0)
    os.remove(copy_path(pair))

    entry["files"] = {label: {"bytes": os.path.getsize(path), "sha256": sha256(path)}
                      for label, path in (("control", control_path(pair)),
                                          ("edit", edit_path(pair)),
                                          ("null", null_path(pair)))}
    entry["edit_differs_from_control"] = (
        entry["files"]["edit"]["sha256"] != entry["files"]["control"]["sha256"])
    log(f"{pair}: edit differs from control: {entry['edit_differs_from_control']}")
    return True


def print_list():
    print(f"neutral root {NEUTRAL}")
    counts = {}
    for pair, spec in PAIRS.items():
        counts[spec["group"].upper()] = counts.get(spec["group"].upper(), 0) + 1
        extra = f"  [{spec['alias_note']}]" if spec.get("alias_note") else ""
        print(f"{pair}: {spec['text']}; calculation {spec['calc']}{extra}")
        print(f"    -> {os.path.basename(control_path(pair))}, "
              f"{os.path.basename(null_path(pair))}, {os.path.basename(edit_path(pair))}")
    print("pairs per group: " + ", ".join(f"{g} {n}" for g, n in sorted(counts.items()))
          + f"; total {len(PAIRS)}")
    print("aliases (the same build as the pair named, not made twice):")
    for alias, target in ALIASES.items():
        print(f"  {alias} = {target}")
    print("NOT made:")
    for item, reason in NOT_MADE:
        print(f"  {item}: {reason}")


def main():
    global USE_UTC
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--list", action="store_true",
                    help="print the pairs and do nothing else")
    ap.add_argument("--group", choices=("A", "B", "C", "a", "b", "c"),
                    help="run one group")
    ap.add_argument("--utc", action="store_true",
                    help="send dates through oracle_w3.utc()")
    ap.add_argument("pairs", nargs="*")
    args = ap.parse_args()

    if args.list:
        print_list()
        return
    USE_UTC = args.utc

    pairs = tuple(args.pairs)
    if args.group:
        pairs += tuple(p for p in PAIRS if p.startswith(args.group.lower() + "-"))
    if not pairs:
        pairs = tuple(PAIRS)
    for pair in pairs:
        if pair in ALIASES:
            sys.exit(f"{pair} is an alias of {ALIASES[pair]}; run that pair")
        if pair not in PAIRS:
            sys.exit(f"unknown pair {pair}")
        base = PAIRS[pair]["base"]
        if base[0] == "file" and not os.path.isfile(os.path.join(DATA, base[1])):
            sys.exit(f"base missing: tests/data/{base[1]}")
        for path in targets(pair):
            if os.path.exists(path):
                sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    for directory in (FIXTURE_DIR, NULL_DIR, WORK, OUT):
        os.makedirs(directory, exist_ok=True)

    result = {"version": None, "pairs": {}}
    if os.path.exists(RESULT):
        with open(RESULT, encoding="utf-8") as handle:
            result = json.load(handle)
    result["utc_dates"] = USE_UTC
    with oracle_session() as app:
        with Guard(600 + 150 * len(pairs)):
            result["version"] = prop(app, "Version")
            app.UserName = AUTHOR
            if any(PAIRS[p].get("probe") for p in pairs):
                record = {}
                try:
                    probe_interim(app, record)
                except Exception as exc:  # noqa: BLE001
                    record["raised"] = f"{type(exc).__name__}: {exc}"
                result["interim_probe"] = record
                log(f"interim probe found {record.get('found')}")
            for pair in pairs:
                entry = result["pairs"].setdefault(pair, {})
                try:
                    ok = measure(app, pair, entry)
                except Exception as exc:  # noqa: BLE001
                    entry["edit"] = f"<failed: {type(exc).__name__}: {exc}>"
                    ok = False
                    try:
                        app.FileClose(0)
                    except Exception:  # noqa: BLE001
                        pass
                    discard(pair)
                log(f"{pair}: {'ok' if ok else 'FAILED'} {entry.get('edit')}")
            try:
                app.Calculation = 1  # leave the operator's Project automatic
            except Exception as exc:  # noqa: BLE001
                log(f"could not restore automatic calculation: {exc}")

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")


if __name__ == "__main__":
    main()
