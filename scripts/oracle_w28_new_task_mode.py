#!/usr/bin/env python3
"""Wave 28 (W28-3.1): Project 2021's new-task scheduling-mode saves.

File > Options > Schedule > "New tasks created: Auto Scheduled / Manually
Scheduled" is stored per project in `114/Props` record 0x024013c8 (the
QNEWTASKSAREMANUAL property, index 64 of the derived name table; decoded in
wave 22).  The type library exposes it as `Project.NewTasksCreatedAsManual`.

The brief measures the save Project makes when the option changes, in BOTH
directions, on copies of tests/data/synthetic-base.mpp (whose stored value is
1 = Manually Scheduled):

  * manual -> auto: control saved with the option True, edited saved after
    setting it False;
  * auto -> manual: control saved with the option False, edited saved after
    setting it True.

Each pair comes from ONE continuous session: Project regenerates the Fixed2Data
row identities on every save, so a control and an edited file from two sessions
would differ in bytes the option does not cause.  Every file is written under
the neutral root C:/w28/fixtures with neutral Author/Manager/Company/UserName/
Title.

The COM facts (the property before and after each save, each task's Manual flag
and the project row's facts, and Project's own reopen of every committed file)
go to build/w28/W28-3.1-project-new-task-mode.json (not committed).  Rows are
recorded by ID only; no task or project name is read, printed or stored.

An INSERT probe (not a committed fixture) checks what Project's own InsertTask
command creates under each mode: the new row's Manual flag and the saved bytes
are recorded in the same JSON.  The port's Insert Task uses the project's
stored mode; this is the measurement behind that choice.

    python scripts/oracle_w28_new_task_mode.py

The script refuses to run if any file of the pairs already exists, so a
half-finished session cannot silently leave a mixed pair.
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
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w28"
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures")
OUT = os.path.join(ROOT, "build", "w28")
RESULT = os.path.join(OUT, "W28-3.1-project-new-task-mode.json")
WORK = os.path.join(OUT, "newtask-work")

BASE_SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")

PAIRS = {
    # key: (control name, edited name, control value, edited value)
    "manual_to_auto": ("wave28-newtask-manual-control.mpp",
                       "wave28-newtask-auto.mpp", True, False),
    "auto_to_manual": ("wave28-newtask-auto-control.mpp",
                       "wave28-newtask-manual.mpp", False, True),
}
INSERT_FILES = {
    "manual": os.path.join(WORK, "insert-manual.mpp"),
    "auto": os.path.join(WORK, "insert-auto.mpp"),
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


def task_facts(task):
    return {
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "manual": prop(task, "Manual"),
        "summary": prop(task, "Summary"),
        "duration": prop(task, "Duration"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "percent_complete": prop(task, "PercentComplete"),
    }


def all_facts(project):
    rows = {"project": task_facts(project.ProjectSummaryTask)}
    for task in project.Tasks:
        if task is None:
            continue
        rows[str(prop(task, "ID"))] = task_facts(task)
    return rows


def mode_facts(app, project):
    return {
        "new_tasks_created_as_manual": prop(project, "NewTasksCreatedAsManual"),
        "tasks": all_facts(project),
    }


def neutralise(app, project, title):
    app.UserName = "Fixture Builder"
    project.Author = "Fixture Builder"
    project.Manager = "Fixture Builder"
    project.Company = "Fixture Company"
    project.Title = title


def save_as(app, name):
    app.FileSaveAs(os.path.join(FIXTURE_DIR, name))
    return name


def reopen_facts(app, path):
    app.FileOpen(path)
    facts = mode_facts(app, app.ActiveProject)
    app.FileClose(0)
    return facts


def set_mode(app, project, manual):
    project.NewTasksCreatedAsManual = bool(manual)
    got = prop(project, "NewTasksCreatedAsManual")
    if isinstance(got, str) or bool(got) != bool(manual):
        raise SystemExit(
            f"Project did not store NewTasksCreatedAsManual={manual} "
            f"(reads back {got!r})")
    log(f"option NewTasksCreatedAsManual = {got!r}")


def main():
    for pair in PAIRS.values():
        for name in (pair[0], pair[1]):
            path = os.path.join(FIXTURE_DIR, name)
            if os.path.exists(path):
                sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    for path in INSERT_FILES.values():
        if os.path.exists(path):
            sys.exit(f"refusing: {path} already exists (inspect the probe first)")
    if not os.path.isfile(BASE_SOURCE):
        sys.exit(f"fixture missing: {BASE_SOURCE}")
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)

    facts = {"version": None, "base_stored_mode": None, "pairs": {},
             "insert_probe": {}}
    with oracle_session() as app:
        with Guard(600):
            facts["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"

            # The base file's stored value, read before any save by Project.
            src = working_copy(BASE_SOURCE, "w28-3.1-mode-probe")
            app.FileOpen(src)
            facts["base_stored_mode"] = prop(
                app.ActiveProject, "NewTasksCreatedAsManual")
            app.FileClose(0)
            log(f"base stored NewTasksCreatedAsManual = "
                f"{facts['base_stored_mode']!r}")

            # ---- one continuous session per pair ------------------------
            for key, (control_name, edited_name, control_value,
                      edited_value) in PAIRS.items():
                src = working_copy(BASE_SOURCE, f"w28-3.1-{key}")
                work = os.path.join(WORK, f"{key}.mpp")
                shutil.copyfile(src, work)
                app.FileOpen(work)
                project = app.ActiveProject
                neutralise(app, project, "Synthetic New Task Mode Fixture")
                entry = facts["pairs"].setdefault(key, {})
                set_mode(app, project, control_value)
                entry["control"] = save_as(app, control_name)
                entry["control_facts"] = mode_facts(app, project)
                set_mode(app, project, edited_value)
                entry["edited"] = save_as(app, edited_name)
                entry["edited_facts"] = mode_facts(app, project)
                app.FileClose(0)

            # ---- Insert probe: Project's own InsertTask under each mode -
            # Not a committed fixture: the writer's add shape is measured in
            # W45; here we only need the new row's scheduling mode.
            for key, manual in (("manual", True), ("auto", False)):
                src = working_copy(BASE_SOURCE, f"w28-3.1-insert-{key}")
                work = os.path.join(WORK, f"insert-{key}.mpp")
                shutil.copyfile(src, work)
                app.FileOpen(work)
                project = app.ActiveProject
                neutralise(app, project, "Synthetic New Task Mode Fixture")
                set_mode(app, project, manual)
                before = all_facts(project)
                app.InsertTask()
                after = all_facts(project)
                app.FileSaveAs(INSERT_FILES[key])
                entry = {
                    "mode": manual,
                    "before_facts": before,
                    "after_facts": after,
                    "path": os.path.relpath(INSERT_FILES[key], ROOT),
                }
                facts["insert_probe"][key] = entry
                # Project's InsertTask inserts BEFORE the active row and
                # renumbers, so the new row is NOT a new ID: the row that was
                # last gains a new ID while the inserted row takes ID 1 and a
                # new unique id.  Identify it by the unique id (wave-28 review:
                # an ID-keyed set difference printed the old row that moved).
                new_uids = sorted({row["unique_id"] for row in after.values()} -
                                  {row["unique_id"] for row in before.values()})
                if new_uids:
                    new_row = next(row for row in after.values()
                                   if row["unique_id"] == new_uids[0])
                    log(f"InsertTask under "
                        f"{'manual' if manual else 'auto'} mode: new unique id "
                        f"{new_uids[0]} at ID {new_row['id']} "
                        f"Manual={new_row['manual']} "
                        f"Summary={new_row['summary']}")
                else:
                    log(f"InsertTask under "
                        f"{'manual' if manual else 'auto'} mode: "
                        f"no new row (facts unchanged)")
                app.FileClose(0)

            # Reopen every committed file: what Project itself displays.
            for key, pair in PAIRS.items():
                entry = facts["pairs"][key]
                entry["control_reopen"] = reopen_facts(
                    app, os.path.join(FIXTURE_DIR, pair[0]))
                entry["edited_reopen"] = reopen_facts(
                    app, os.path.join(FIXTURE_DIR, pair[1]))

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for key, pair in PAIRS.items():
        for name in (pair[0], pair[1]):
            path = os.path.join(FIXTURE_DIR, name)
            log(f"fixture {name} ({os.path.getsize(path)} bytes)")
    for key in PAIRS:
        entry = facts["pairs"][key]
        log(f"{key}: control property "
            f"{entry['control_facts']['new_tasks_created_as_manual']!r} -> "
            f"edited property "
            f"{entry['edited_facts']['new_tasks_created_as_manual']!r}; "
            f"reopen control "
            f"{entry['control_reopen']['new_tasks_created_as_manual']!r} / "
            f"edited {entry['edited_reopen']['new_tasks_created_as_manual']!r}")


if __name__ == "__main__":
    main()
