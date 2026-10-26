#!/usr/bin/env python3
"""Wave 30 (W30-1.0): the scheduling-mode (Manual) save.

Measures Project 2021's own saves of the task-mode flag on the synthetic base
(all three rows manual):

  * auto_to_manual  ID 1 switched to automatic (Project recalculates it), the
                    control saved, then ID 1 switched back to manual and the
                    edited file saved.  The ONLY edit between the pair is the
                    manual flag.
  * manual_to_auto  the untouched all-manual file saved as the control, then
                    ID 1 switched to automatic and saved.  This is the shape
                    the port refuses if Project reschedules the row.

Both files are saved under the neutral root C:/w30/fixtures-mode with neutral
Author/Manager/Company/UserName/Title, in one continuous session per pair so
Project's per-save identities are shared.

The COM facts before and after each edit, and on Project's own reopen of every
committed file, go to build/w30/W30-1.0-project-task-mode.json (not
committed).  Rows are recorded by ID only; no task or project name is read,
printed or stored.

    python -I scripts/oracle_w30_task_mode.py

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
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-mode")
OUT = os.path.join(ROOT, "build", "w30")
RESULT = os.path.join(OUT, "W30-1.0-project-task-mode.json")
WORK = os.path.join(OUT, "mode-work")

BASE_SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")

PAIRS = {
    "auto_to_manual": ("wave30-task-mode-auto-control.mpp",
                       "wave30-task-mode-manual.mpp"),
    "manual_to_auto": ("wave30-task-mode-manual-control.mpp",
                       "wave30-task-mode-auto.mpp"),
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


def row(task, row_index):
    return {
        "row": row_index,
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "summary": prop(task, "Summary"),
        "manual": prop(task, "Manual"),
        "duration": prop(task, "Duration"),
        "duration_text": prop(task, "DurationText"),
        "percent_complete": prop(task, "PercentComplete"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "constraint_type": prop(task, "ConstraintType"),
        "constraint_date": prop(task, "ConstraintDate"),
        "free_slack": prop(task, "FreeSlack"),
        "total_slack": prop(task, "TotalSlack"),
    }


def all_facts(project):
    rows = {"project": row(project.ProjectSummaryTask, 0)}
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
    neutralise(app, project, "Synthetic Task Mode Fixture")
    return project


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

            # ---- auto_to_manual: ID 1 automatic in the control ------------
            project = open_base(app, "w30-1.0-auto")
            project.Tasks(1).Manual = False
            entry = facts["pairs"].setdefault("auto_to_manual", {})
            entry["control_facts_before_save"] = all_facts(project)
            entry["control"] = save_as(app, PAIRS["auto_to_manual"][0])
            entry["control_facts"] = all_facts(project)
            project.Tasks(1).Manual = True
            entry["edited_facts_before_save"] = all_facts(project)
            entry["edited"] = save_as(app, PAIRS["auto_to_manual"][1])
            entry["edited_facts"] = all_facts(project)
            app.FileClose(0)

            # ---- manual_to_auto: every row manual in the control ----------
            project = open_base(app, "w30-1.0-manual")
            entry = facts["pairs"].setdefault("manual_to_auto", {})
            entry["control_facts_before_save"] = all_facts(project)
            entry["control"] = save_as(app, PAIRS["manual_to_auto"][0])
            entry["control_facts"] = all_facts(project)
            project.Tasks(1).Manual = False
            entry["edited_facts_before_save"] = all_facts(project)
            entry["edited"] = save_as(app, PAIRS["manual_to_auto"][1])
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
    for key in PAIRS:
        entry = facts["pairs"][key]
        before = entry["control_facts"]["1"]
        after = entry["edited_facts"]["1"]
        log(f"{key}: ID1 manual {before['manual']} -> {after['manual']}; "
            f"start {before['start']} -> {after['start']}; "
            f"finish {before['finish']} -> {after['finish']}; "
            f"duration {before['duration']} -> {after['duration']}")


if __name__ == "__main__":
    main()
