#!/usr/bin/env python3
"""Wave 29 (W29-0.2): Project 2021's percent saves on a real schedule.

Wave 28's percent pairs were top-level leaves with no resources, no summary and
no milestone.  The wave-28 review (R28-2) requires four more pairs, each its own
Project save pair on a schedule that carries the shape:

  leaf       ID 3 (a leaf two levels under a summary) 0 -> 40 %
  work       ID 6 (assigned to the file's one resource, work-bearing) 0 -> 40 %
  milestone  ID 7 (added by this script, Milestone=True) 0 -> 100 %
  mid        ID 4 at 25 % -> 60 % (an edit between two recorded values)

Base: tests/data/wave25-auto.mpp, a Project-2021 synthetic fixture (all tasks
automatic, one resource assigned to ID 6, summaries + leaves).  A copy under
build/w29/percent-work is opened -- never the committed fixture -- and the
milestone row is added there; the shaped base is saved under C:/w29/work.  Every
pair then opens a FRESH copy of that base, saves the control, makes the edit and
saves the edited file, so the pair is one Project session with only the named
edit between the two saves.  The 25 % state for the mid pair is Project's own
save (set before that pair's control).

All eight fixtures are written under the neutral root C:/w29/fixtures with
neutral Author/Manager/Company/UserName/Title.  Facts (before, after, reopen by
ID; summary rows and assignment work included) go to
build/w29/W29-0.2-project-percent.json; the fixtures are committed under
tests/data/wave29-* in a fixtures-only commit.

    python -I scripts/oracle_w29_percent.py

Refuses to run if any fixture already exists, so a half-finished session cannot
leave a mixed pair.
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
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w29"
FIXTURE_DIR = NEUTRAL + "/fixtures"
WORK = os.path.join(ROOT, "build", "w29", "percent-work")
RESULT = os.path.join(ROOT, "build", "w29", "W29-0.2-project-percent.json")
BASE_SOURCE = os.path.join(ROOT, "tests", "data", "wave25-auto.mpp")
BASE_COPY = os.path.join(WORK, "wave25-auto.mpp")
SHAPED_BASE = NEUTRAL + "/work/base.mpp"

# key, control fixture, edited fixture, task ID, target percent
PAIRS = [
    ("leaf", "wave29-percent-leaf-control.mpp", "wave29-percent-leaf40.mpp", 3, 40),
    ("work", "wave29-percent-work-control.mpp", "wave29-percent-work40.mpp", 6, 40),
    ("milestone", "wave29-percent-milestone-control.mpp",
     "wave29-percent-milestone100.mpp", 7, 100),
    ("mid", "wave29-percent-mid25-control.mpp", "wave29-percent-mid60.mpp", 4, 60),
]
MID_FROM = 25  # Project records this state before the mid pair's control save

# The shaped schedule this script requires before it measures anything.
EXPECTED_SHAPE = {
    1: {"outline_level": 1, "summary": True, "manual": False},
    2: {"outline_level": 2, "summary": True, "manual": False},
    3: {"outline_level": 3, "summary": False, "manual": False},
    4: {"outline_level": 3, "summary": False, "manual": False},
    5: {"outline_level": 2, "summary": False, "manual": False},
    6: {"outline_level": 1, "summary": False, "manual": False},
    7: {"outline_level": 1, "summary": False, "manual": False},
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


def assignment_facts(task):
    rows = []
    try:
        assignments = task.Assignments
        count = assignments.Count
    except Exception as exc:  # noqa: BLE001
        return {"count": f"<unreadable: {exc}>", "rows": []}
    for index in range(1, count + 1):
        assignment = assignments(index)
        rows.append({
            "index": index,
            "resource_uid": prop(assignment, "ResourceUniqueID"),
            "units": prop(assignment, "Units"),
            "work": prop(assignment, "Work"),
            "actual_work": prop(assignment, "ActualWork"),
            "remaining_work": prop(assignment, "RemainingWork"),
            "start": prop(assignment, "Start"),
            "finish": prop(assignment, "Finish"),
        })
    return {"count": count, "rows": rows}


def task_facts(task):
    return {
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "outline_level": prop(task, "OutlineLevel"),
        "manual": prop(task, "Manual"),
        "summary": prop(task, "Summary"),
        "milestone": prop(task, "Milestone"),
        "duration": prop(task, "Duration"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "percent_complete": prop(task, "PercentComplete"),
        "percent_work_complete": prop(task, "PercentWorkComplete"),
        "actual_duration": prop(task, "ActualDuration"),
        "remaining_duration": prop(task, "RemainingDuration"),
        "actual_start": prop(task, "ActualStart"),
        "actual_finish": prop(task, "ActualFinish"),
        "work": prop(task, "Work"),
        "actual_work": prop(task, "ActualWork"),
        "remaining_work": prop(task, "RemainingWork"),
        "assignments": assignment_facts(task),
    }


def all_facts(project):
    rows = {"project": task_facts(project.ProjectSummaryTask)}
    for task in project.Tasks:
        if task is None:
            continue
        rows[str(prop(task, "ID"))] = task_facts(task)
    return {
        "new_tasks_created_as_manual": prop(
            project, "NewTasksCreatedAsManual"),
        "rows": rows,
    }


def neutralise(app, project, title):
    app.UserName = "Fixture Builder"
    project.Author = "Fixture Builder"
    project.Manager = "Fixture Builder"
    project.Company = "Fixture Company"
    project.Title = title


def reopen_facts(app, path):
    app.FileOpen(path)
    facts = all_facts(app.ActiveProject)
    app.FileClose(0)
    return facts


def check_shape(facts, where):
    rows = facts["rows"]
    for task_id, want in EXPECTED_SHAPE.items():
        row = rows.get(str(task_id))
        if row is None:
            sys.exit(f"{where}: task ID {task_id} is missing")
        for field, value in want.items():
            if row[field] != value:
                sys.exit(f"{where}: task ID {task_id} {field} is "
                         f"{row[field]!r}, expected {value!r}")


def main():
    for _, control, edited, _, _ in PAIRS:
        for name in (control, edited):
            path = os.path.join(FIXTURE_DIR, name)
            if os.path.exists(path):
                sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    os.makedirs(NEUTRAL + "/work", exist_ok=True)
    shutil.copyfile(BASE_SOURCE, BASE_COPY)

    facts = {"version": None, "pairs": {}, "base": {}}
    with oracle_session() as app:
        with Guard(900):
            facts["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"

            # ---- shape the base: add the milestone row --------------------
            app.FileOpen(BASE_COPY)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Percent Schedule")
            facts["base"]["before_add"] = all_facts(project)
            added = project.Tasks.Add("Milestone")
            project.NewTasksCreatedAsManual = False
            added.Manual = False
            added.Milestone = True
            if prop(added, "Duration") != 0:
                added.Duration = 0
            facts["base"]["added"] = task_facts(added)
            facts["base"]["after_add"] = all_facts(project)
            check_shape(facts["base"]["after_add"], "shaped base")
            app.FileSaveAs(SHAPED_BASE)
            app.FileClose(0)
            log(f"shaped base saved: {len(facts['base']['after_add']['rows']) - 1} "
                f"task rows, milestone ID {facts['base']['added']['id']}")

            # ---- one fresh copy of the shaped base per pair ---------------
            for key, control, edited, task_id, target in PAIRS:
                work = os.path.join(WORK, f"{key}.mpp")
                shutil.copyfile(SHAPED_BASE, work)
                app.FileOpen(work)
                project = app.ActiveProject
                neutralise(app, project, "Synthetic Percent Schedule")
                if key == "mid":
                    project.Tasks(task_id).PercentComplete = MID_FROM
                entry = facts["pairs"].setdefault(key, {})
                entry["control_path"] = control
                entry["edited_path"] = edited
                entry["task_id"] = task_id
                entry["target_percent"] = target
                entry["control"] = all_facts(project)
                app.FileSaveAs(os.path.join(FIXTURE_DIR, control))
                project.Tasks(task_id).PercentComplete = target
                entry["after_edit"] = all_facts(project)
                app.FileSaveAs(os.path.join(FIXTURE_DIR, edited))
                app.FileClose(0)
                row = entry["after_edit"]["rows"][str(task_id)]
                log(f"{key}: ID {task_id} {entry['control']['rows'][str(task_id)]['percent_complete']}"
                    f" -> {row['percent_complete']} % "
                    f"(summary ID 1 {entry['control']['rows']['1']['percent_complete']}"
                    f" -> {entry['after_edit']['rows']['1']['percent_complete']})")

            # ---- Project's reopen of every fixture ------------------------
            for key, control, edited, _, _ in PAIRS:
                entry = facts["pairs"][key]
                entry["control_reopen"] = reopen_facts(
                    app, os.path.join(FIXTURE_DIR, control))
                entry["edited_reopen"] = reopen_facts(
                    app, os.path.join(FIXTURE_DIR, edited))

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for _, control, edited, _, _ in PAIRS:
        for name in (control, edited):
            path = os.path.join(FIXTURE_DIR, name)
            log(f"fixture {name} ({os.path.getsize(path)} bytes)")


if __name__ == "__main__":
    main()
