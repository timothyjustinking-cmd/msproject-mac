#!/usr/bin/env python3
"""Wave 30 (W30-1.2): Project 2021's Inactivate save.

Measures what Project's own Inactivate writes on the neutral synthetic base
(tests/data/synthetic-base.mpp: three top-level 0 % leaf tasks, each with one
unassigned placeholder assignment, no links):

  open a copy of the base, save the control with neutral metadata, set the
  SECOND task (ID 2) to Active = False through COM -- the property the Task
  tab's Inactivate button toggles -- record the facts, save, and reopen both
  files.  The control is saved first in the SAME session, so the
  control-vs-edited diff carries the command's effect.

A second direction is deliberately NOT measured: reactivating an inactive
task.  The port's button will only inactivate (see the record).

All COM facts are recorded by ID/UniqueID only: no task, resource or project
name is read, printed or stored.

    python -I scripts/oracle_w30_inactivate.py

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
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-inactivate")
OUT = os.path.join(ROOT, "build", "w30")
RESULT = os.path.join(OUT, "W30-1.2-project-inactivate.json")
WORK = os.path.join(OUT, "inactivate-work")

BASE_SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")

CONTROL = "wave30-inactivate-control.mpp"
EDITED = "wave30-inactivate.mpp"


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


def row(task, row_index):
    return {
        "saved_row": row_index,
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "active": prop(task, "Active"),
        "summary": prop(task, "Summary"),
        "milestone": prop(task, "Milestone"),
        "manual": prop(task, "Manual"),
        "duration": prop(task, "Duration"),
        "percent_complete": prop(task, "PercentComplete"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "outline_level": prop(task, "OutlineLevel"),
        "outline_number": prop(task, "OutlineNumber"),
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


def main():
    for name in (CONTROL, EDITED):
        path = os.path.join(FIXTURE_DIR, name)
        if os.path.exists(path):
            sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    if not os.path.isfile(BASE_SOURCE):
        sys.exit(f"fixture missing: {BASE_SOURCE}")
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)

    facts = {"version": None, "control": CONTROL, "edited": EDITED}
    with oracle_session() as app:
        with Guard(600):
            facts["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"
            src = working_copy(BASE_SOURCE, "w30-1.2-inactivate")
            shutil.copyfile(src, os.path.join(WORK, "inactivate.mpp"))
            app.FileOpen(os.path.join(WORK, "inactivate.mpp"))
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Inactivate Fixture")
            facts["control_facts_before_save"] = all_facts(project)
            facts["control"] = save_as(app, CONTROL)
            facts["control_facts"] = all_facts(project)

            target = project.Tasks(2)
            facts["target"] = {
                "id": prop(target, "ID"),
                "unique_id": prop(target, "UniqueID"),
                "active_before": prop(target, "Active"),
            }
            target.Active = False
            facts["edited_facts_before_save"] = all_facts(project)
            facts["edited"] = save_as(app, EDITED)
            facts["edited_facts"] = all_facts(project)
            app.FileClose(0)

            # Reopen every committed file: what Project itself displays.
            facts["control_reopen"] = reopen_facts(
                app, os.path.join(FIXTURE_DIR, CONTROL))
            facts["edited_reopen"] = reopen_facts(
                app, os.path.join(FIXTURE_DIR, EDITED))

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for name in (CONTROL, EDITED):
        path = os.path.join(FIXTURE_DIR, name)
        log(f"fixture {name} ({os.path.getsize(path)} bytes)")
    log(f"target {facts['target']}")
    for tag in ("control_facts", "edited_facts", "edited_reopen"):
        log(tag)
        for index in sorted(facts[tag], key=lambda k: (k != "project",
                                                       int(k) if k != "project" else -1)):
            item = facts[tag][index]
            if index == "project":
                log(f"  project {item}")
                continue
            log(f"  row {index}: id {item['id']} uid {item['unique_id']} "
                f"active {item['active']} summary {item['summary']} "
                f"milestone {item['milestone']} level {item['outline_level']} "
                f"outline {item['outline_number']} duration {item['duration']}")


if __name__ == "__main__":
    main()
