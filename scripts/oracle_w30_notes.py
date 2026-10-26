#!/usr/bin/env python3
"""Wave 30 (W30-1.5): Project 2021's Task Notes save, the measured add shape.

Measures Application-level Task.Notes on the synthetic base (three top-level
manual leaves, no links, no progress, one placeholder assignment per task):

  * the control is saved;
  * sheet row 2 (ID 2) is selected and its Notes set to one plain ASCII line,
    then the file is saved;
  * both files are reopened for facts.

The edit is the simplest notes write Project offers: a task with NO existing
notes gets its first one.  Saving it appends Project's RichEdit document to
Var2Data (the measured effect), which is why the pair is committed as the
oracle for a later writer.

One continuous session per pair so Project's per-save identities are shared.
Files are saved under the neutral root C:/w30/fixtures-notes with neutral
Author/Manager/Company/UserName/Title.  The COM facts before and after the
edit, and on Project's own reopen, go to
build/w30/W30-1.5-project-notes.json (not committed).  Rows are recorded by
ID only; no task or project name is read, printed or stored.

    python -I scripts/oracle_w30_notes.py

The script refuses to run if either target file already exists.
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
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-notes")
OUT = os.path.join(ROOT, "build", "w30")
RESULT = os.path.join(OUT, "W30-1.5-project-notes.json")
WORK = os.path.join(OUT, "notes-work")

BASE_SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")
CONTROL_NAME = "wave30-notes-control.mpp"
EDITED_NAME = "wave30-notes.mpp"
TARGET_ID = 2
TEXT = "A wave 30 notes fixture line."


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
        "row": row_index,
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "summary": prop(task, "Summary"),
        "manual": prop(task, "Manual"),
        "duration": prop(task, "Duration"),
        "percent_complete": prop(task, "PercentComplete"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "notes": prop(task, "Notes"),
        "has_notes": prop(task, "Notes") != "",
    }


def all_facts(project):
    rows = {"project": {
        "start": prop(project.ProjectSummaryTask, "Start"),
        "finish": prop(project.ProjectSummaryTask, "Finish"),
        "duration": prop(project.ProjectSummaryTask, "Duration"),
    }}
    for index in range(1, project.Tasks.Count + 1):
        rows[str(index)] = row(project.Tasks(index), index)
    return rows


def neutralise(app, project):
    app.UserName = "Fixture Builder"
    project.Author = "Fixture Builder"
    project.Manager = "Fixture Builder"
    project.Company = "Fixture Company"
    project.Title = "Synthetic Notes Fixture"


def main():
    for name in (CONTROL_NAME, EDITED_NAME):
        path = os.path.join(FIXTURE_DIR, name)
        if os.path.exists(path):
            sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    if not os.path.isfile(BASE_SOURCE):
        sys.exit(f"fixture missing: {BASE_SOURCE}")
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)

    facts = {"version": None, "target_id": TARGET_ID, "text": TEXT,
             "control": CONTROL_NAME, "edited": EDITED_NAME,
             "source": os.path.basename(BASE_SOURCE)}
    work = os.path.join(WORK, "notes.mpp")
    shutil.copyfile(working_copy(BASE_SOURCE, "notes"), work)
    with oracle_session() as app:
        with Guard(900):
            facts["version"] = prop(app, "Version")
            app.FileOpen(work)
            project = app.ActiveProject
            neutralise(app, project)
            facts["control_facts_before_save"] = all_facts(project)
            app.FileSaveAs(os.path.join(FIXTURE_DIR, CONTROL_NAME))
            facts["control_facts"] = all_facts(project)

            target = None
            for index in range(1, project.Tasks.Count + 1):
                candidate = project.Tasks(index)
                if candidate.ID == TARGET_ID:
                    target = candidate
                    break
            if target is None:
                sys.exit(f"target ID {TARGET_ID} not found")
            app.SelectTaskField(Row=target.ID, Column="Name",
                                RowRelative=False)
            selection = app.ActiveSelection.Tasks
            facts["selection"] = [selection(index).ID
                                  for index in range(1, selection.Count + 1)]
            target.Notes = TEXT
            facts["edited_facts_before_save"] = all_facts(project)
            app.FileSaveAs(os.path.join(FIXTURE_DIR, EDITED_NAME))
            facts["edited_facts"] = all_facts(project)
            app.FileClose(0)

            for key, name in (("control", CONTROL_NAME),
                              ("edited", EDITED_NAME)):
                app.FileOpen(os.path.join(FIXTURE_DIR, name))
                facts[key + "_reopen"] = all_facts(app.ActiveProject)
                app.FileClose(0)

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for name in (CONTROL_NAME, EDITED_NAME):
        path = os.path.join(FIXTURE_DIR, name)
        log(f"{name}: {os.path.getsize(path)} bytes")
    log(f"selection {facts['selection']}")
    for tag in ("control_facts", "edited_facts"):
        log(tag)
        for index in sorted(facts[tag],
                            key=lambda k: (k != "project",
                                           int(k) if k != "project" else -1)):
            item = facts[tag][index]
            if index == "project":
                log(f"  project start {item['start']} "
                    f"finish {item['finish']} duration {item['duration']}")
                continue
            log(f"  row {index}: id {item['id']} manual {item['manual']} "
                f"start {item['start']} finish {item['finish']} "
                f"notes {item['notes']!r}")


if __name__ == "__main__":
    main()
