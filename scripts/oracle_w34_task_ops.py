#!/usr/bin/env python3
"""Wave 34 (W34-0.5): the four corpus-only save paths, measured on synthetic-base.

Insert Task, Delete Task, link ADD and custom-field SET/CLEAR are today diffed
against Project only through corpus/ files CI never has.  This script measures
each on tests/data/synthetic-base.mpp (three top-level 0 % manual leaves, one
unassigned placeholder assignment per task, empty link table, no custom-field
values) through scripts/oracle_lock.py, so the pair and its null save can be
committed:

  insert-task  Project's append shape -- Tasks.Add of one task -- which is what
               the port's Insert Task writes when it anchors after the last
               saved row (add_task_row appends at the row high-water mark).
  delete-task  Tasks(2).Delete(): the tombstone and the assignment cascade.
  link-add     Tasks(2).TaskDependencies.Add(Tasks(1), 1, 0): one FS + 0 link
               into a 0 % successor with no prior incoming link.
  field-set    Tasks(2).Text1 = "Synthetic text one." (VarMeta key 0x0b400033).
  field-clear  a seed session stores the value; the edit session REOPENS the
               seed, saves the control from disk, then sets Text1 = "" and
               saves -- the measured CLEAR shape (entry kept, empty payload).

Each pair's control and edit are saved in one continuous session, so Project's
per-save identity churn is shared; the no-edit save of each control (the null
save) is produced separately by scripts/oracle_w34_null_saves.py.

All COM facts are recorded by ID/UniqueID and length only.  The only strings
written are synthetic: the inserted task's name and the Text1 value.

    python -I scripts/oracle_w34_task_ops.py

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
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-task-ops")
OUT = os.path.join(ROOT, "build", "w34")
RESULT = os.path.join(OUT, "W34-0.5-task-ops.json")
WORK = os.path.join(OUT, "task-ops-work")
BASE_SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")

INSERT_NAME = "W34 Inserted Task"
FIELD_BEFORE = "Synthetic text one."
TEXT1_FIELD_ID = 0x0B400033

PAIRS = {
    "insert-task": {
        "control": "wave34-insert-task-control.mpp",
        "edit": "wave34-insert-task.mpp",
        "seed": False,
        "note": "Tasks.Add appends one task (the port's Insert Task append shape)",
    },
    "delete-task": {
        "control": "wave34-delete-task-control.mpp",
        "edit": "wave34-delete-task.mpp",
        "seed": False,
        "note": "Tasks(2).Delete(): tombstone plus the assignment cascade",
    },
    "link-add": {
        "control": "wave34-link-add-control.mpp",
        "edit": "wave34-link-add.mpp",
        "seed": False,
        "note": "one FS + 0 link 1->2 into a 0 % successor with no prior link",
    },
    "field-set": {
        "control": "wave34-field-set-control.mpp",
        "edit": "wave34-field-set.mpp",
        "seed": False,
        "note": "Text1 set on task 2 from no value",
    },
    "field-clear": {
        "control": "wave34-field-clear-control.mpp",
        "edit": "wave34-field-clear.mpp",
        "seed": True,
        "note": "Text1 cleared on task 2; the control stores the seeded value",
    },
}


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


def text_len(value):
    try:
        return len(value)
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def task_dependencies(task):
    rows = []
    try:
        collection = task.TaskDependencies
        count = collection.Count
    except Exception as exc:  # noqa: BLE001
        return {"unreadable": str(exc)}
    for index in range(1, count + 1):
        dep = collection(index)
        rows.append({
            "from_uid": prop(dep.From, "UniqueID"),
            "to_uid": prop(dep.To, "UniqueID"),
            "type": prop(dep, "Type"),
            "lag": prop(dep, "Lag"),
        })
    return rows


def row(task):
    assignments = {"count": prop(task.Assignments, "Count")}
    try:
        for index in range(1, task.Assignments.Count + 1):
            assignment = task.Assignments(index)
            assignments[str(index)] = {
                "uid": prop(assignment, "UniqueID"),
                "resource_uid": prop(assignment, "ResourceUniqueID"),
                "units": prop(assignment, "Units"),
            }
    except Exception as exc:  # noqa: BLE001
        assignments["unreadable"] = str(exc)
    return {
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "name_len": text_len(prop(task, "Name")),
        "manual": prop(task, "Manual"),
        "summary": prop(task, "Summary"),
        "milestone": prop(task, "Milestone"),
        "duration": prop(task, "Duration"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "percent_complete": prop(task, "PercentComplete"),
        "outline_level": prop(task, "OutlineLevel"),
        "text1_len": text_len(prop(task, "Text1")),
        "dependencies": task_dependencies(task),
        "assignments": assignments,
    }


def facts(project):
    out = {"project": {"tasks": project.Tasks.Count}}
    for index in range(1, project.Tasks.Count + 1):
        out[str(index)] = row(project.Tasks(index))
    return out


def save_as(app, name):
    path = os.path.join(FIXTURE_DIR, name)
    app.FileSaveAs(path)
    return name


def open_copy(app, tag):
    source = working_copy(BASE_SOURCE, tag)
    os.makedirs(WORK, exist_ok=True)
    copy = os.path.join(WORK, tag + ".mpp")
    shutil.copyfile(source, copy)
    app.FileOpen(copy)
    project = app.ActiveProject
    neutralise(app, project, "Synthetic W34 Task Ops Fixture")
    return project


def apply_insert(app, project):
    before = project.Tasks.Count
    project.Tasks.Add(INSERT_NAME)
    if project.Tasks.Count != before + 1:
        raise SystemExit("refusing: Tasks.Add did not append a task row")


def apply_delete(app, project):
    before = project.Tasks.Count
    project.Tasks.UniqueID(2).Delete()
    if project.Tasks.Count != before - 1:
        raise SystemExit("refusing: the delete did not remove one task row")


def apply_link(app, project):
    successor = project.Tasks.UniqueID(2)
    predecessor = project.Tasks.UniqueID(1)
    successor.TaskDependencies.Add(predecessor, 1, 0)  # FS, 0 lag
    rows = task_dependencies(successor)
    if not any(dep["from_uid"] == 1 and dep["to_uid"] == 2 and dep["type"] == 1
               for dep in rows
               if isinstance(dep, dict)):
        raise SystemExit(
            f"refusing: the link did not appear on task 2 ({rows!r})")


def apply_field(project, value):
    project.Tasks.UniqueID(2).Text1 = value
    got = prop(project.Tasks.UniqueID(2), "Text1")
    if got != value:
        raise SystemExit(
            f"refusing: Text1 did not take the edit ({got!r})")


def measure(app, result, key, spec):
    tag = "w34-0.5-" + key
    entry = result["pairs"].setdefault(key, {})
    entry["note"] = spec["note"]

    project = open_copy(app, tag)
    if spec["seed"]:
        apply_field(project, FIELD_BEFORE)
        seed_name = "seed-" + key + ".mpp"
        seed_path = os.path.join(FIXTURE_DIR, seed_name)
        if os.path.exists(seed_path):
            os.remove(seed_path)
        app.FileSaveAs(seed_path)
        entry["seed_fields"] = facts(project)
        app.FileClose(0)

        # Reopen the seed so the control's value is loaded from disk, exactly
        # as an editor session sees an existing custom-field value.
        reopened = os.path.join(WORK, tag + "-seed.mpp")
        shutil.copyfile(seed_path, reopened)
        app.FileOpen(reopened)
        project = app.ActiveProject
        got = prop(project.Tasks.UniqueID(2), "Text1")
        if got != FIELD_BEFORE:
            raise SystemExit(
                f"refusing: the seeded Text1 did not survive the reopen ({got!r})")
        os.remove(seed_path)

    entry["control_before_save"] = facts(project)
    entry["control"] = save_as(app, spec["control"])

    if key == "insert-task":
        apply_insert(app, project)
    elif key == "delete-task":
        apply_delete(app, project)
    elif key == "link-add":
        apply_link(app, project)
    elif key == "field-set":
        apply_field(project, FIELD_BEFORE)
    elif key == "field-clear":
        apply_field(project, "")
    else:
        raise SystemExit(f"unknown pair {key}")

    entry["edit_facts_before_save"] = facts(project)
    entry["edit"] = save_as(app, spec["edit"])
    entry["edit_facts"] = facts(project)
    app.FileClose(0)


def reopen_facts(app, name):
    app.FileOpen(os.path.join(FIXTURE_DIR, name))
    project = app.ActiveProject
    result = facts(project)
    app.FileClose(0)
    return result


def main():
    for spec in PAIRS.values():
        for name in (spec["control"], spec["edit"]):
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
            for key, spec in PAIRS.items():
                measure(app, result, key, spec)

            # Reopen every committed file: what Project itself displays.
            for key, spec in PAIRS.items():
                entry = result["pairs"][key]
                entry["control_reopen"] = reopen_facts(app, spec["control"])
                entry["edit_reopen"] = reopen_facts(app, spec["edit"])

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for key, spec in PAIRS.items():
        entry = result["pairs"][key]
        for name in (spec["control"], spec["edit"]):
            path = os.path.join(FIXTURE_DIR, name)
            log(f"fixture {name} ({os.path.getsize(path)} bytes)")
        before = entry["control_before_save"]["project"]["tasks"]
        after = entry["edit_facts"]["project"]["tasks"]
        log(f"{key}: tasks {before} -> {after}; edit facts "
            f"{entry['edit_facts_before_save']}")
        log(f"  reopen edit {entry['edit_reopen']['project']}")


if __name__ == "__main__":
    main()
