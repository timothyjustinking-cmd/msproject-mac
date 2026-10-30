#!/usr/bin/env python3
"""Wave 34 (W34-3.1): two more assignment-count pairs.

wave30-assign-add measured the first assignment on a task that had none (a
placeholder row only); wave30-assign-delete measured removing the one real
assignment.  W34-3.1 asks for the task's EXISTING assignment count 0, 1 and 2,
so this script measures the two missing ones on one chain:

  count 1 -> 2   wave34-assign-add-second-control -> wave34-assign-add-second
  count 2 -> 3   wave34-assign-add-third-control  -> wave34-assign-add-third

The base is wave34-resource-add-third.mpp (three work resources, three
unassigned placeholder rows, no real assignment).  In ONE Project session:

  1. open a copy, neutralise, assign resource ID 2 to task ID 1 (count 1),
     save the SECOND control;
  2. assign resource ID 1 to task ID 1 (count 2), save the second edit and
     then the THIRD control (a second save of the same 2-assignment state);
  3. assign resource ID 3 to task ID 1 (count 3), save the third edit.

Each committed control gets its no-edit null save later through
scripts/oracle_w34_null_saves.py.  Facts are IDs and numbers only: no task,
resource or project name is read out or written.

    python -I scripts/oracle_w34_assign_counts.py

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

from oracle_lock import oracle_session, log, working_copy  # noqa: E402
from oracle_privacy import neutralise  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w34"
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-assign")
OUT = os.path.join(ROOT, "build", "w34")
RESULT = os.path.join(OUT, "W34-3.1-assign-counts.json")
WORK = os.path.join(OUT, "assign-count-work")
BASE_SOURCE = os.path.join(ROOT, "tests", "data",
                           "wave34-resource-add-third.mpp")

SECOND_CONTROL = "wave34-assign-add-second-control.mpp"
SECOND_EDIT = "wave34-assign-add-second.mpp"
THIRD_CONTROL = "wave34-assign-add-third-control.mpp"
THIRD_EDIT = "wave34-assign-add-third.mpp"
TITLE = "Synthetic W34 Assign Count Fixture"


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


def assignment_facts(assignment, index):
    return {
        "index": index,
        "task_id": prop(assignment, "TaskID"),
        "task_unique_id": prop(assignment, "TaskUniqueID"),
        "resource_id": prop(assignment, "ResourceID"),
        "resource_unique_id": prop(assignment, "ResourceUniqueID"),
        "units": prop(assignment, "Units"),
        "work": prop(assignment, "Work"),
        "start": prop(assignment, "Start"),
        "finish": prop(assignment, "Finish"),
        "unique_id": prop(assignment, "UniqueID"),
    }


def facts(project):
    out = {
        "tasks": {},
        "resources": {},
        "assignment_count": 0,
    }
    for index in range(1, project.Tasks.Count + 1):
        task = project.Tasks(index)
        assignments = {}
        for a in range(1, task.Assignments.Count + 1):
            assignment = task.Assignments(a)
            assignments[str(a)] = assignment_facts(assignment, a)
        out["tasks"][str(index)] = {
            "id": prop(task, "ID"),
            "unique_id": prop(task, "UniqueID"),
            "work": prop(task, "Work"),
            "duration": prop(task, "Duration"),
            "start": prop(task, "Start"),
            "finish": prop(task, "Finish"),
            "assignments": assignments,
        }
        out["assignment_count"] += task.Assignments.Count
    for index in range(1, project.Resources.Count + 1):
        resource = project.Resources(index)
        out["resources"][str(index)] = {
            "id": prop(resource, "ID"),
            "unique_id": prop(resource, "UniqueID"),
            "work": prop(resource, "Work"),
            "max_units": prop(resource, "MaxUnits"),
            "type": prop(resource, "Type"),
        }
    return out


def main():
    for name in (SECOND_CONTROL, SECOND_EDIT, THIRD_CONTROL, THIRD_EDIT):
        path = os.path.join(FIXTURE_DIR, name)
        if os.path.exists(path):
            sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    if not os.path.isfile(BASE_SOURCE):
        sys.exit(f"fixture missing: {BASE_SOURCE}")
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)

    result = {"version": None, "pairs": {}}
    with oracle_session() as app:
        with Guard(1800):
            result["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"
            source = working_copy(BASE_SOURCE, "w34-3.1-assign-counts")
            copy = os.path.join(WORK, "assign-counts.mpp")
            shutil.copyfile(source, copy)
            app.FileOpen(copy)
            project = app.ActiveProject
            neutralise(app, project, TITLE)
            task = project.Tasks(1)

            entry = result["pairs"].setdefault("assign_add_second", {})
            entry["base_facts"] = facts(project)
            entry["added_first"] = assignment_facts(
                task.Assignments.Add(TaskID=task.ID, ResourceID=2), 1)
            entry["control_facts"] = facts(project)
            app.FileSaveAs(os.path.join(FIXTURE_DIR, SECOND_CONTROL))

            entry["added_second"] = assignment_facts(
                task.Assignments.Add(TaskID=task.ID, ResourceID=1), 2)
            entry["edit_facts"] = facts(project)
            app.FileSaveAs(os.path.join(FIXTURE_DIR, SECOND_EDIT))

            third = result["pairs"].setdefault("assign_add_third", {})
            third["control_facts"] = facts(project)
            app.FileSaveAs(os.path.join(FIXTURE_DIR, THIRD_CONTROL))

            third["added_third"] = assignment_facts(
                task.Assignments.Add(TaskID=task.ID, ResourceID=3), 3)
            third["edit_facts"] = facts(project)
            app.FileSaveAs(os.path.join(FIXTURE_DIR, THIRD_EDIT))
            app.FileClose(0)

            for key, name in (("second_control", SECOND_CONTROL),
                              ("second_edit", SECOND_EDIT),
                              ("third_control", THIRD_CONTROL),
                              ("third_edit", THIRD_EDIT)):
                app.FileOpen(os.path.join(FIXTURE_DIR, name))
                result["pairs"].setdefault("reopen", {})[key] = facts(
                    app.ActiveProject)
                app.FileClose(0)

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for name in (SECOND_CONTROL, SECOND_EDIT, THIRD_CONTROL, THIRD_EDIT):
        path = os.path.join(FIXTURE_DIR, name)
        log(f"fixture {name} ({os.path.getsize(path)} bytes)")
    for key in ("assign_add_second", "assign_add_third"):
        pair = result["pairs"][key]
        log(f"{key}: control assignments {pair['control_facts']['assignment_count']}"
            f" -> edit {pair['edit_facts']['assignment_count']}")


if __name__ == "__main__":
    main()
