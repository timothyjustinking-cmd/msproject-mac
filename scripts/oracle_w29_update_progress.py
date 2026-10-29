#!/usr/bin/env python3
"""Wave 29 (W29-0.3): Mark on Track and Update Project on a row WITH progress.

The wave-28 update pairs all start from 0 % rows.  This script measures what
Project 2021 does when the row already records progress:

  prepared base: synthetic-base.mpp copy, Task ID 1 switched automatic and set
                 to PercentComplete = 60 (Project stores ActualStart Mon
                 2026-01-05 08:00; the 5-day row is 1440/2400 ticks)

  mark_after     Mark on Track, project status date 2026-01-09 (after the
                 recorded 60 %)
  mark_before    Mark on Track, project status date 2026-01-07 (before it)
  update_after   UpdateProject(All=True,  2026-01-09, pj0to100Percent)
  update_before  UpdateProject(All=True,  2026-01-07, pj0to100Percent)
  update_selected UpdateProject(All=False, 2026-01-09, ...) with task IDs 1 and
                 2 selected (two of the three rows)

Mark on Track has no type-library member; as in wave 28 the ribbon button is
invoked through UI Automation (AutomationId UpdateAsScheduled) while the
session is held under scripts/oracle_lock.py.  Every save and fact goes
through COM.

Each pair saves its control before the command and the edited file after it,
in the same Project session; every file is reopened for the displayed facts.
The JSON goes to build/w29/W29-0.3-project-progress-update.json (not
committed).  The script refuses to run if any target file already exists.

    python scripts/oracle_w29_update_progress.py
"""

import datetime as dt
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log  # noqa: E402
from oracle_privacy import neutralise  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w29"
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-update")
OUT = os.path.join(ROOT, "build", "w29")
RESULT = os.path.join(OUT, "W29-0.3-project-progress-update.json")
WORK = os.path.join(OUT, "update-progress-work")
INVOKE = os.path.join(WORK, "uiainvoke.ps1")
INVOKE_SOURCE = os.path.join(ROOT, "build", "w28", "uiainvoke.ps1")

BASE_SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")
PREPARED = os.path.join(WORK, "prepared-60.mpp")

AFTER = dt.datetime(2026, 1, 9, 0, 0, 0, tzinfo=dt.timezone.utc)
BEFORE = dt.datetime(2026, 1, 7, 0, 0, 0, tzinfo=dt.timezone.utc)

PAIRS = [
    ("mark_after", "wave29-mark-60-after-control.mpp",
     "wave29-mark-60-after.mpp", "mark", AFTER),
    ("mark_before", "wave29-mark-60-before-control.mpp",
     "wave29-mark-60-before.mpp", "mark", BEFORE),
    ("update_after", "wave29-update-60-after-control.mpp",
     "wave29-update-60-after.mpp", "update_all", AFTER),
    ("update_before", "wave29-update-60-before-control.mpp",
     "wave29-update-60-before.mpp", "update_all", BEFORE),
    ("update_selected", "wave29-update-60-selected-control.mpp",
     "wave29-update-60-selected.mpp", "update_selected", AFTER),
]


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
        "duration": prop(task, "Duration"),
        "duration_text": prop(task, "DurationText"),
        "percent_complete": prop(task, "PercentComplete"),
        "percent_work_complete": prop(task, "PercentWorkComplete"),
        "actual_duration": prop(task, "ActualDuration"),
        "remaining_duration": prop(task, "RemainingDuration"),
        "actual_start": prop(task, "ActualStart"),
        "actual_finish": prop(task, "ActualFinish"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "work": prop(task, "Work"),
        "actual_work": prop(task, "ActualWork"),
        "remaining_work": prop(task, "RemainingWork"),
    }


def all_facts(project):
    rows = {"project": task_facts(project.ProjectSummaryTask),
            "status_date": prop(project, "StatusDate")}
    for task in project.Tasks:
        if task is None:
            continue
        rows[str(prop(task, "ID"))] = task_facts(task)
    return rows


def reopen_facts(app, path):
    app.FileOpen(path)
    facts = all_facts(app.ActiveProject)
    app.FileClose(0)
    return facts


def invoke_mark_on_track(tag):
    time.sleep(1.0)
    logfile = os.path.join(WORK, f"uia-{tag}.log")
    if os.path.exists(logfile):
        os.remove(logfile)
    run = subprocess.run(
        ["pwsh", "-NoProfile", "-File", INVOKE,
         "-AutomationId", "UpdateAsScheduled", "-Name", "Mark on Track",
         "-Out", logfile],
        capture_output=True, text=True, timeout=120)
    body = (open(logfile, encoding="utf-8").read().strip()
            if os.path.exists(logfile) else "")
    log(f"mark on track [{tag}]: rc={run.returncode} {body}")
    if run.returncode != 0:
        raise SystemExit(
            f"Mark on Track invocation failed for {tag}: {body or run.stderr}")
    time.sleep(1.0)


def select_rows(app, rows):
    """Select the given sheet rows; the second and later calls ADD to the
    selection (SelectTaskField's measured type-library Add argument)."""
    for index, row in enumerate(rows):
        app.SelectTaskField(Row=row, Column="Name", RowRelative=False,
                            Add=(index > 0))


def main():
    for _, control, edited, _, _ in PAIRS:
        for name in (control, edited):
            path = os.path.join(FIXTURE_DIR, name)
            if os.path.exists(path):
                sys.exit(f"refusing: {path} already exists (inspect it first)")
    for path in (PREPARED, INVOKE):
        if os.path.exists(path):
            sys.exit(f"refusing: {path} already exists (inspect it first)")
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    shutil.copyfile(INVOKE_SOURCE, INVOKE)

    facts = {"version": None, "after_date_utc": val(AFTER),
             "before_date_utc": val(BEFORE), "pairs": {}}
    with oracle_session() as app:
        with Guard(900):
            facts["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"

            # ---- the 60 % prepared base -----------------------------------
            source = os.path.join(WORK, "prepared-source.mpp")
            shutil.copyfile(BASE_SOURCE, source)
            app.FileOpen(source)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Progress Update Fixture")
            project.Tasks(1).Manual = False
            project.Tasks(1).PercentComplete = 60
            facts["prepared"] = all_facts(project)
            app.FileSaveAs(PREPARED)
            app.FileClose(0)
            log(f"prepared 60 % base: {os.path.getsize(PREPARED)} bytes")

            for key, control, edited, command, date in PAIRS:
                conf = os.path.join(WORK, key + "-base.mpp")
                shutil.copyfile(PREPARED, conf)
                app.FileOpen(conf)
                project = app.ActiveProject
                neutralise(app, project, "Synthetic Progress Update Fixture")
                entry = facts["pairs"].setdefault(key, {})
                entry["control_path"] = control
                entry["edited_path"] = edited
                entry["date_utc"] = val(date)
                if command == "mark":
                    entry["command"] = "Mark on Track"
                    project.StatusDate = date
                    app.SelectTaskField(Row=1, Column="Name",
                                        RowRelative=False)
                    entry["selection"] = [1]
                elif command == "update_all":
                    entry["command"] = "UpdateProject(All=True)"
                    entry["selection"] = []
                else:
                    entry["command"] = "UpdateProject(All=False)"
                    select_rows(app, [1, 2])
                    entry["selection"] = [1, 2]
                control_path = os.path.join(FIXTURE_DIR, control)
                edited_path = os.path.join(FIXTURE_DIR, edited)
                app.FileSaveAs(control_path)
                entry["control"] = all_facts(project)
                if command == "mark":
                    invoke_mark_on_track(key)
                elif command == "update_all":
                    app.UpdateProject(True, date, 1)
                else:
                    app.UpdateProject(False, date, 1)
                entry["after_edit"] = all_facts(project)
                app.FileSaveAs(edited_path)
                app.FileClose(0)

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
    for key, control, edited, _, _ in PAIRS:
        entry = facts["pairs"][key]
        for name in (control, edited):
            path = os.path.join(FIXTURE_DIR, name)
            log(f"fixture {name} ({os.path.getsize(path)} bytes)")
        log(key + ": " + ", ".join(
            f"ID {tid} {entry['after_edit'][tid]['percent_complete']} %"
            for tid in sorted((k for k in entry["after_edit"] if k.isdigit()),
                              key=int)))


if __name__ == "__main__":
    main()
