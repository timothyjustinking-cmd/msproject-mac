#!/usr/bin/env python3
"""Wave 30 (W30-1.1): Project 2021's Insert Summary and Insert Milestone saves.

Measures what Project's own commands do to the neutral synthetic base
(tests/data/synthetic-base.mpp: three top-level 0 % leaf tasks, each with one
unassigned placeholder assignment, no links):

  summary   open the base, save the control with neutral metadata, select the
            SECOND task (ID 2) and press Insert Summary, then save.  The
            command inserts a new summary row above the selection and makes the
            selected task its child.
  milestone open a fresh copy of the base, save the control, select the SECOND
            task (ID 2) and press Insert Milestone, then save.  The command
            inserts a zero-duration milestone row above the selection.

Each pair's two files are saved in one continuous session, so Project's
per-save identity churn is shared and the control-vs-edited diff carries the
command's effect.  Project's no-edit save of each control (the null save) is
produced separately by scripts/oracle_w30_null_saves.py.

All COM facts are recorded by ID/UniqueID only: no task, resource or project
name is read, printed or stored.  The extra file (row of the new task, summary
and milestone flags, outline level) is what the port's writer needs to know.

    python -I scripts/oracle_w30_insert.py

The script refuses to run if any target file already exists.
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

NEUTRAL = r"C:/w30"
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-insert")
OUT = os.path.join(ROOT, "build", "w30")
RESULT = os.path.join(OUT, "W30-1.1-project-insert.json")
WORK = os.path.join(OUT, "insert-work")

BASE_SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")

PAIRS = {
    "summary": ("wave30-insert-summary-control.mpp",
                "wave30-insert-summary.mpp"),
    "milestone": ("wave30-insert-milestone-control.mpp",
                  "wave30-insert-milestone.mpp"),
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
    out = {}
    try:
        count = task.Assignments.Count
    except Exception as exc:  # noqa: BLE001
        return {"unreadable": str(exc)}
    out["count"] = count
    for index in range(1, count + 1):
        assignment = task.Assignments(index)
        out[str(index)] = {
            "uid": prop(assignment, "UniqueID"),
            "resource_uid": prop(assignment, "ResourceUniqueID"),
            "units": prop(assignment, "Units"),
            "work": prop(assignment, "Work"),
            "start": prop(assignment, "Start"),
            "finish": prop(assignment, "Finish"),
        }
    return out


def row(task, row_index):
    return {
        "saved_row": row_index,
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "summary": prop(task, "Summary"),
        "milestone": prop(task, "Milestone"),
        "manual": prop(task, "Manual"),
        "duration": prop(task, "Duration"),
        "percent_complete": prop(task, "PercentComplete"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "outline_level": prop(task, "OutlineLevel"),
        "outline_number": prop(task, "OutlineNumber"),
        "assignments": assignment_facts(task),
    }


def all_facts(project):
    rows = {"project": {
        "id": prop(project.ProjectSummaryTask, "ID"),
        "tasks": project.Tasks.Count,
    }}
    for index in range(1, project.Tasks.Count + 1):
        rows[str(index)] = row(project.Tasks(index), index)
    return rows


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
    facts = all_facts(app.ActiveProject)
    app.FileClose(0)
    return facts


def open_base(app, tag):
    src = working_copy(BASE_SOURCE, tag)
    shutil.copyfile(src, os.path.join(WORK, tag + ".mpp"))
    app.FileOpen(os.path.join(WORK, tag + ".mpp"))
    project = app.ActiveProject
    neutralise(app, project, "Synthetic Insert Fixture")
    return project


def select_second_task(app, project):
    """Select sheet row 2 (the base's all-top-level ID 2, Task B).

    Task.Row does not exist in this object model; the base is three top-level
    rows, so the second sheet row is ID 2.  The post-command facts prove which
    row the new task was inserted above (its outline number)."""
    second = project.Tasks(2)
    app.SelectTaskField(Row=2, Column="Name", RowRelative=False)
    return {"selected_id": second.ID, "selected_unique_id": second.UniqueID,
            "sheet_row": 2}


def main():
    for pair in PAIRS.values():
        for name in pair:
            path = os.path.join(FIXTURE_DIR, name)
            if os.path.exists(path):
                sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    if not os.path.isfile(BASE_SOURCE):
        sys.exit(f"fixture missing: {BASE_SOURCE}")
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)

    facts = {"version": None, "pairs": {}}
    with oracle_session() as app:
        with Guard(900):
            facts["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"

            # ---- Insert Summary: new summary above the selected task ------
            project = open_base(app, "w30-1.1-summary")
            entry = facts["pairs"].setdefault("summary", {})
            entry["control_facts_before_save"] = all_facts(project)
            entry["control"] = save_as(app, PAIRS["summary"][0])
            entry["control_facts"] = all_facts(project)
            entry["selection"] = select_second_task(app, project)
            app.InsertSummaryTask()
            entry["edited_facts_before_save"] = all_facts(project)
            entry["edited"] = save_as(app, PAIRS["summary"][1])
            entry["edited_facts"] = all_facts(project)
            app.FileClose(0)

            # ---- Insert Milestone: new milestone above the selected task --
            project = open_base(app, "w30-1.1-milestone")
            entry = facts["pairs"].setdefault("milestone", {})
            entry["control_facts_before_save"] = all_facts(project)
            entry["control"] = save_as(app, PAIRS["milestone"][0])
            entry["control_facts"] = all_facts(project)
            entry["selection"] = select_second_task(app, project)
            app.InsertMilestoneTask()
            entry["edited_facts_before_save"] = all_facts(project)
            entry["edited"] = save_as(app, PAIRS["milestone"][1])
            entry["edited_facts"] = all_facts(project)
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
        for name in pair:
            path = os.path.join(FIXTURE_DIR, name)
            log(f"fixture {name} ({os.path.getsize(path)} bytes)")
        entry = facts["pairs"][key]
        before = entry["control_facts"]
        after = entry["edited_facts"]
        log(f"{key}: tasks {before['project']['tasks']} -> "
            f"{after['project']['tasks']}; selection "
            f"{entry['selection']}")
        for index in sorted(after, key=lambda k: (k != "project", int(k) if k != "project" else -1)):
            if index == "project":
                continue
            item = after[index]
            log(f"  row {index}: id {item['id']} uid {item['unique_id']} "
                f"summary {item['summary']} milestone {item['milestone']} "
                f"level {item['outline_level']} outline {item['outline_number']} "
                f"duration {item['duration']} assignments "
                f"{item['assignments'].get('count')}")


if __name__ == "__main__":
    main()
