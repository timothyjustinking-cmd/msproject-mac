#!/usr/bin/env python3
"""Wave 30 (G2): the Level All save pair.

The brief measures Level All on a small overallocated fixture.  synthetic-base
is manually scheduled and Project does not level manual rows (measured: the
first attempt produced a byte-identical save), so this script builds the
fixture from a fresh Project project: three automatic, unlinked tasks with the
same start (durations 5 / 3 / 2 days) and one 100 % Work resource assigned to
all three -- PeakUnits 300 %, Overallocated True.

The first save after FileNew is not reopen-stable in this build: its
214/CFilter block is 230 bytes, and the next save after a reopen materialises a
second 230-byte block (measured on the first control).  The script therefore
saves a SEED file, closes it, reopens it and saves that as the control, so the
committed control already carries what Project writes after a reopen; the
control, the edit and the null save then share one filter-block length.  It
runs Application.LevelNow(All=True) and saves the edit in the same session.
All files are saved with neutral metadata under C:/w30/fixtures-g2.

    python -I scripts/oracle_w30_level.py

It refuses if either target fixture already exists.
"""

import datetime as dt
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w30"
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-g2")
OUT = os.path.join(ROOT, "build", "w30")
RESULT = os.path.join(OUT, "W30-2-level.json")

CONTROL = "wave30-level-control.mpp"
EDITED = "wave30-level-all.mpp"
SEED = "wave30-level-seed.mpp"


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


def facts(project):
    out = {"tasks": {}, "resources": {}, "assignments": {}}
    for index in range(1, project.Tasks.Count + 1):
        task = project.Tasks(index)
        out["tasks"][str(index)] = {
            "id": prop(task, "ID"),
            "unique_id": prop(task, "UniqueID"),
            "manual": prop(task, "Manual"),
            "start": prop(task, "Start"),
            "finish": prop(task, "Finish"),
            "duration": prop(task, "Duration"),
            "leveling_delay": prop(task, "LevelingDelay"),
            "overallocated": prop(task, "Overallocated"),
        }
    for index in range(1, project.Resources.Count + 1):
        resource = project.Resources(index)
        out["resources"][str(index)] = {
            "id": prop(resource, "ID"),
            "unique_id": prop(resource, "UniqueID"),
            "type": prop(resource, "Type"),
            "max_units": prop(resource, "MaxUnits"),
            "peak_units": prop(resource, "PeakUnits"),
            "overallocated": prop(resource, "Overallocated"),
        }
    n = 0
    for index in range(1, project.Tasks.Count + 1):
        task = project.Tasks(index)
        for a in range(1, task.Assignments.Count + 1):
            n += 1
            assignment = task.Assignments(a)
            out["assignments"][str(n)] = {
                "task_id": prop(assignment, "TaskID"),
                "resource_id": prop(assignment, "ResourceID"),
                "units": prop(assignment, "Units"),
                "work": prop(assignment, "Work"),
                "start": prop(assignment, "Start"),
                "finish": prop(assignment, "Finish"),
                "leveling_delay": prop(assignment, "LevelingDelay"),
            }
    return out


def reopen(app, name):
    app.FileOpen(os.path.join(FIXTURE_DIR, name))
    result = facts(app.ActiveProject)
    app.FileClose(0)
    return result


def main():
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    for name in (CONTROL, EDITED, SEED):
        path = os.path.join(FIXTURE_DIR, name)
        if os.path.exists(path):
            sys.exit(f"refusing: {path} already exists (inspect the pair first)")

    result = {"version": None, "level_error": None}
    with oracle_session() as app:
        with Guard(1200):
            result["version"] = prop(app, "Version")
            app.FileNew()
            project = app.ActiveProject
            app.UserName = "Fixture Builder"
            project.Author = "Fixture Builder"
            project.Manager = "Fixture Builder"
            project.Company = "Fixture Company"
            project.Title = "Synthetic Leveling Fixture"

            names = ("Task A", "Task B", "Task C")
            durations = (2400, 1440, 960)
            for name, minutes in zip(names, durations):
                task = project.Tasks.Add(name)
                # Project 2021's new-project default creates MANUAL tasks; the
                # resource leveler only moves automatic rows (measured on the
                # first attempt: a manual control saved byte-identically).
                task.Manual = False
                task.Duration = minutes
            resource = project.Resources.Add("Resource 1")
            for task in project.Tasks:
                task.Assignments.Add(TaskID=task.ID, ResourceID=resource.ID)

            result["build_facts"] = facts(project)
            result["overallocated_before"] = prop(resource, "Overallocated")

            # The first save after FileNew is not reopen-stable: Project writes
            # a second 214/CFilter block on the next save after a reopen
            # (measured on the first control: 230 -> 460 bytes).  Make the seed
            # that first save, then save the CONTROL from a reopened seed so the
            # control already carries Project's reopened and re-saved shape.
            seed = os.path.join(FIXTURE_DIR, SEED)
            app.FileSaveAs(seed)
            result["seed"] = SEED
            app.FileClose(0)
            app.FileOpen(seed)
            project = app.ActiveProject
            resource = project.Resources(1)

            result["control_facts"] = facts(project)
            result["overallocated_before_control"] = prop(resource, "Overallocated")
            path = os.path.join(FIXTURE_DIR, CONTROL)
            if os.path.exists(path):
                os.remove(path)
            app.FileSaveAs(path)
            result["control"] = CONTROL

            try:
                app.LevelNow(True)
                result["level_error"] = None
            except Exception as exc:  # noqa: BLE001
                result["level_error"] = repr(exc)
            result["edit_facts_before_save"] = facts(project)
            result["overallocated_after"] = prop(resource, "Overallocated")
            path = os.path.join(FIXTURE_DIR, EDITED)
            if os.path.exists(path):
                os.remove(path)
            app.FileSaveAs(path)
            result["edited"] = EDITED
            result["edited_facts"] = facts(project)
            app.FileClose(0)
            if os.path.exists(seed):
                os.remove(seed)

            result["control_reopen"] = reopen(app, CONTROL)
            result["edited_reopen"] = reopen(app, EDITED)

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for name in (CONTROL, EDITED):
        path = os.path.join(FIXTURE_DIR, name)
        log(f"fixture {name} ({os.path.getsize(path)} bytes)")
    for key in ("control_facts", "edited_facts", "control_reopen", "edited_reopen"):
        log(key + ": " + json.dumps(result[key]["tasks"], sort_keys=True))


if __name__ == "__main__":
    main()
