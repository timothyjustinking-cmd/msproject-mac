#!/usr/bin/env python3
"""Wave 35 (G2): view-state persistence pairs, one factor each.

Measures what Project 2021 writes to the file when the view state changes:

  sort      Application.Sort on the task sheet (key, direction)
  table     Application.TableApply (the applied task table)
  outline   Application.OutlineShowTasks (the outline level shown)
  zoom      Application.ZoomTimescale (the timescale zoom)

The base is built through Project under automatic calculation: a summary with
two children, then two top-level leaves, all automatic, no links.  Each pair
saves a seed, reopens it, saves the CONTROL, applies ONE view change and saves
the EDIT; the control's NULL save is made from a copy.  All files are written
under the neutral root C:/w35/fixtures-viewstate with neutral Author, Manager,
Company, UserName and Title.  COM facts go to build/w35/W35-2-viewstate.json
(not committed).  Rows are named "Task A".."Task E": synthetic.

    python -I scripts/oracle_w35_viewstate.py [pair ...]

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
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-viewstate")
NULL_DIR = os.path.join(NEUTRAL, "fixtures-viewstate-null")
OUT = os.path.join(ROOT, "build", "w35")
WORK = os.path.join(OUT, "viewstate-work")
RESULT = os.path.join(OUT, "W35-2-viewstate.json")


# Each edit is a function (app, project) -> dict of what it did.
def sort_name_desc(app, project):
    app.Sort(Key1="Name", Ascending1=False, Renumber=False, Outline=True)
    return {"call": "Sort(Key1=Name, Ascending1=False, Renumber=False, Outline=True)"}


def sort_name_asc(app, project):
    app.Sort(Key1="Name", Ascending1=True, Renumber=False, Outline=True)
    return {"call": "Sort(Key1=Name, Ascending1=True, Renumber=False, Outline=True)"}


def sort_duration_desc(app, project):
    app.Sort(Key1="Duration", Ascending1=False, Renumber=False, Outline=True)
    return {"call": "Sort(Key1=Duration, Ascending1=False, Renumber=False, Outline=True)"}


def sort_flat_name_desc(app, project):
    app.Sort(Key1="Name", Ascending1=False, Renumber=False, Outline=False)
    return {"call": "Sort(Key1=Name, Ascending1=False, Renumber=False, Outline=False)"}


def sort_two_keys(app, project):
    app.Sort(Key1="Name", Ascending1=False, Key2="Duration", Ascending2=True,
             Renumber=False, Outline=True)
    return {"call": "Sort(Key1=Name desc, Key2=Duration asc, Renumber=False, Outline=True)"}


def table_cost(app, project):
    app.TableApply("Cost")
    return {"call": "TableApply(Cost)"}


def table_schedule(app, project):
    app.TableApply("Schedule")
    return {"call": "TableApply(Schedule)"}


def outline_level1(app, project):
    app.OutlineShowTasks(1)
    return {"call": "OutlineShowTasks(1)"}


def outline_level2(app, project):
    app.OutlineShowTasks(2)
    return {"call": "OutlineShowTasks(2)"}


def outline_all(app, project):
    app.OutlineShowTasks(65535)  # pjTaskOutlineShowLevelMax
    return {"call": "OutlineShowTasks(65535)"}


# ZoomTimescale is called positionally: the keyword form leaves Project in a
# modal dialog (the 90 s watchdog kills it; build/w35/W35-2-viewstate-run2.log).
def zoom_entire(app, project):
    app.ZoomTimescale(None, True)
    return {"call": "ZoomTimescale(None, True)  # Entire"}


def zoom_reset(app, project):
    app.ZoomTimescale(None, None, None, True)
    return {"call": "ZoomTimescale(None, None, None, True)  # Reset"}


# Duration is a string ("2w", "10d", "1mo"); a number raises 1004.
def zoom_custom(app, project):
    app.ZoomTimescale("2w")
    return {"call": "ZoomTimescale('2w')  # Duration"}


def zoom_custom_narrow(app, project):
    """The 2w zoom again after the Project window is made narrower: if the
    stored scale changes, the value depends on the saving machine's chart pane
    width (the evidence for deferring the zoom write)."""
    notes = {"width_before": app.Width, "state_before": app.WindowState}
    for step, setter in (("state", lambda: setattr(app, "WindowState", -4143)),
                         ("width", lambda: setattr(app, "Width", 900))):
        try:
            setter()
            notes[step] = "ok"
        except Exception as exc:  # noqa: BLE001
            notes[step] = f"<raised: {exc.args[:2]}>"
    notes["width_after"] = app.Width
    app.ZoomTimescale("2w")
    notes["call"] = "WindowState/Width 900; ZoomTimescale('2w')"
    return notes


def zoom_custom_10d(app, project):
    app.ZoomTimescale("10d")
    return {"call": "ZoomTimescale('10d')  # Duration"}


# The outline pairs of a three-level outline: the two-level base cannot tell
# "show level 2" from "show all".
DEEP_PAIRS = ("outline-deep-level1", "outline-deep-level2")


def outline_deep_level1(app, project):
    return outline_level1(app, project)


def outline_deep_level2(app, project):
    return outline_level2(app, project)


EDITS = {
    "sort-two-keys": sort_two_keys,
    "outline-deep-level1": outline_deep_level1,
    "outline-deep-level2": outline_deep_level2,
    "sort-name-desc": sort_name_desc,
    "sort-name-asc": sort_name_asc,
    "sort-duration-desc": sort_duration_desc,
    "sort-flat-name-desc": sort_flat_name_desc,
    "table-cost": table_cost,
    "table-schedule": table_schedule,
    "outline-level1": outline_level1,
    "outline-level2": outline_level2,
    "outline-all": outline_all,
    "zoom-entire": zoom_entire,
    "zoom-custom": zoom_custom,
    "zoom-reset": zoom_reset,
    "zoom-custom-10d": zoom_custom_10d,
    "zoom-custom-narrow": zoom_custom_narrow,
}
PAIRS = ("outline-level1", "zoom-entire", "zoom-custom")


def control_name(pair):
    return f"wave35-viewstate-{pair}-control.mpp"


def edit_name(pair):
    return f"wave35-viewstate-{pair}.mpp"


def null_name(pair):
    return f"wave35-viewstate-{pair}-control-null.mpp"


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
            "summary": prop(task, "Summary"),
            "duration": prop(task, "Duration"),
            "start": prop(task, "Start"),
            "finish": prop(task, "Finish"),
        }
    for key, name in (("view", "ActiveWindow"),):
        out[key] = prop(app, name) if False else None
    try:
        out["view_name"] = val(app.ActiveWindow.ActivePane.View.Name)
        out["table_name"] = val(app.ActiveWindow.ActivePane.View.TableName)
    except Exception as exc:  # noqa: BLE001
        out["view_name"] = f"<unreadable: {exc}>"
    return out


def build(app, deep=False):
    app.FileNew()
    project = app.ActiveProject
    neutralise(app, project, "Synthetic View State Fixture")
    app.Calculation = 1  # pjAutomatic
    project.ProjectStart = dt.datetime(2026, 1, 5, 8, 0)
    names = ("Task A", "Task B", "Task C", "Task D", "Task E")
    durations = (1440, 2400, 960, 4800, 1920)
    if deep:
        names += ("Task F",)
        durations += (1440,)
    tasks = []
    for name, dur in zip(names, durations):
        task = project.Tasks.Add(name)
        task.Manual = False
        task.Duration = dur
        tasks.append(task)
    if deep:
        # A (1) > B (2) > C, D (3); A > E (2); F (1)
        for index, indents in ((1, 1), (2, 2), (3, 2), (4, 1)):
            for _ in range(indents):
                tasks[index].OutlineIndent()
    else:
        tasks[1].OutlineIndent()  # B under A
        tasks[2].OutlineIndent()  # C under A
    return project


def measure(app, pair, entry):
    seed = os.path.join(FIXTURE_DIR, f"wave35-viewstate-{pair}-seed.mpp")
    project = build(app, deep=pair in DEEP_PAIRS)
    entry["pending_facts"] = facts(app, project)
    app.FileSaveAs(seed)
    app.FileClose(0)

    app.FileOpen(seed)
    project = app.ActiveProject
    entry["control_facts"] = facts(app, project)
    app.FileSaveAs(os.path.join(FIXTURE_DIR, control_name(pair)))

    try:
        entry["edit"] = EDITS[pair](app, project)
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
