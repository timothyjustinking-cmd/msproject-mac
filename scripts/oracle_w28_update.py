#!/usr/bin/env python3
"""Wave 28 (W28-1.3): Project 2021's Mark on Track and Update Project saves.

The two commands compute the same thing -- a task's percent complete through a
date -- and Project stores the result through the percent-actuals save the
W28-1.2 writer already reproduces.  This script makes Project 2021 produce the
measured pairs on a synthetic-base copy under the neutral root C:/w28:

  * Mark on Track on an automatic row (0 -> the status date's percent)
  * Mark on Track on the fixture's manual row (the manual charge rule)
  * Update Project, "0% - 100% complete", through the same date, entire project

The task facts (COM values before, after and on reopen) are written to
build/w28/W28-1.3-project-update.json (not committed).  Every save carries
neutral Author/Manager/Company/UserName/Title.

Update Project also stores its update date as the project status date
(114/Props key 0x02400045): the update control stores NA and the edited file
stores 2026-01-07.  A separate probe with the status date already set to
2026-01-06 and the same update date read back 2026-01-07, so the command always
moves it.  Mark on Track does not change an existing status date -- the mark
pairs save it before the command and the edited files still hold it.

HOW MARK ON TRACK IS TRIGGERED.  Project's type library exposes
UpdateProject(All, UpdateDate, action) but has no Mark on Track member, and
CommandBars.ExecuteMso only accepts Office idMso names -- Project's own ribbon
controls use custom ids, so GetEnabledMso/ExecuteMso reject every candidate
(tested: "Bold" works, "MarkOnTrack"/"ProjectMarkOnTrack" raise E_INVALIDARG).
The button is therefore invoked on Project's own ribbon through UI Automation
(AutomationId UpdateAsScheduled, measured by build/w28/uiadump.ps1) while the
session is held under scripts/oracle_lock.py; every save and every fact still
goes through COM.  The command is Project's, not the port's.

The script refuses to run if any target file already exists, so a half-finished
session cannot silently leave a mixed pair.

    python scripts/oracle_w28_update.py
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

NEUTRAL = r"C:/w28"
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures")
OUT = os.path.join(ROOT, "build", "w28")
RESULT = os.path.join(OUT, "W28-1.3-project-update.json")
WORK = os.path.join(OUT, "update-work")
INVOKE = os.path.join(OUT, "uiainvoke.ps1")

BASE_SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")

# 2026-01-07 00:00 UTC.  Aware datetimes make the stored packed date
# timezone-independent (measured: raw 0x3bf40000, day 0x3bf4 = 2026-01-07,
# time 0).  The fixture's rows give one working row per scheduling mode, a
# second row past its finish, and a row already finished:
#   ID 1 auto:   Mon 2026-01-05 08:00, 5 d (2400 min) -> Mon+Tue worked
#                = 960/2400 = 40 %
#   ID 2 manual: Sun 2026-01-04 13:00, 3 d (1440 min) -> the 240 min start-day
#                tail + Mon+Tue 960 = 1200/1440 = 83 % (truncated)
#   ID 3 manual: Sun 2026-01-04 13:00, 2 d (960 min) -> finish Tue 12:00,
#                before the through date = 100 %
STATUS = dt.datetime(2026, 1, 7, 0, 0, 0, tzinfo=dt.timezone.utc)

PAIRS = [
    ("mark_auto", "wave28-mark-auto-control.mpp", "wave28-mark-auto.mpp"),
    ("mark_manual", "wave28-mark-manual-control.mpp", "wave28-mark-manual.mpp"),
    ("update_0to100", "wave28-update-control.mpp", "wave28-update.mpp"),
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
    time.sleep(1.0)          # let the save settle before UIA scans
    logfile = os.path.join(WORK, f"uia-{tag}.log")
    if os.path.exists(logfile):
        os.remove(logfile)
    run = subprocess.run(
        ["pwsh", "-NoProfile", "-File", INVOKE,
         "-AutomationId", "UpdateAsScheduled", "-Name", "Mark on Track",
         "-Out", logfile],
        capture_output=True, text=True, timeout=120)
    body = open(logfile, encoding="utf-8").read().strip() if os.path.exists(logfile) else ""
    log(f"mark on track [{tag}]: rc={run.returncode} {body}")
    if run.returncode != 0:
        raise SystemExit(f"Mark on Track invocation failed for {tag}: {body or run.stderr}")
    time.sleep(1.0)


def main():
    for _, control, edited in PAIRS:
        for name in (control, edited):
            path = os.path.join(FIXTURE_DIR, name)
            if os.path.exists(path):
                sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)

    facts = {"version": None, "status_date_utc": val(STATUS), "pairs": {}}
    with oracle_session() as app:
        with Guard(900):
            facts["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"

            # ---- Mark on Track, automatic row ------------------------------
            source = os.path.join(WORK, "mark-auto-base.mpp")
            shutil.copyfile(BASE_SOURCE, source)
            app.FileOpen(source)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Mark Fixture")
            project.Tasks(1).Manual = False
            project.StatusDate = STATUS
            app.SelectTaskField(Row=1, Column="Name", RowRelative=False)
            control_path = os.path.join(FIXTURE_DIR, PAIRS[0][1])
            edited_path = os.path.join(FIXTURE_DIR, PAIRS[0][2])
            app.FileSaveAs(control_path)
            entry = facts["pairs"].setdefault("mark_auto", {})
            entry["control_path"] = PAIRS[0][1]
            entry["edited_path"] = PAIRS[0][2]
            entry["command"] = "Mark on Track"
            entry["selection_rows"] = [1]
            entry["control"] = all_facts(project)
            invoke_mark_on_track("mark-auto")
            entry["after_edit"] = all_facts(project)
            app.FileSaveAs(edited_path)
            app.FileClose(0)

            # ---- Mark on Track, manual row --------------------------------
            source = os.path.join(WORK, "mark-manual-base.mpp")
            shutil.copyfile(BASE_SOURCE, source)
            app.FileOpen(source)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Mark Fixture")
            project.StatusDate = STATUS
            app.SelectTaskField(Row=2, Column="Name", RowRelative=False)
            control_path = os.path.join(FIXTURE_DIR, PAIRS[1][1])
            edited_path = os.path.join(FIXTURE_DIR, PAIRS[1][2])
            app.FileSaveAs(control_path)
            entry = facts["pairs"].setdefault("mark_manual", {})
            entry["control_path"] = PAIRS[1][1]
            entry["edited_path"] = PAIRS[1][2]
            entry["command"] = "Mark on Track"
            entry["selection_rows"] = [2]
            entry["control"] = all_facts(project)
            invoke_mark_on_track("mark-manual")
            entry["after_edit"] = all_facts(project)
            app.FileSaveAs(edited_path)
            app.FileClose(0)

            # ---- Update Project, 0 % - 100 %, entire project ---------------
            source = os.path.join(WORK, "update-base.mpp")
            shutil.copyfile(BASE_SOURCE, source)
            app.FileOpen(source)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Update Fixture")
            control_path = os.path.join(FIXTURE_DIR, PAIRS[2][1])
            edited_path = os.path.join(FIXTURE_DIR, PAIRS[2][2])
            app.FileSaveAs(control_path)
            entry = facts["pairs"].setdefault("update_0to100", {})
            entry["control_path"] = PAIRS[2][1]
            entry["edited_path"] = PAIRS[2][2]
            entry["command"] = "UpdateProject(All=True, action=pj0to100Percent)"
            entry["update_date"] = val(STATUS)
            entry["control"] = all_facts(project)
            app.UpdateProject(True, STATUS, 1)
            entry["after_edit"] = all_facts(project)
            app.FileSaveAs(edited_path)
            app.FileClose(0)

            # Reopen every committed file: what Project itself displays.
            for key, control, edited in PAIRS:
                entry = facts["pairs"][key]
                entry["control_reopen"] = reopen_facts(
                    app, os.path.join(FIXTURE_DIR, control))
                entry["edited_reopen"] = reopen_facts(
                    app, os.path.join(FIXTURE_DIR, edited))

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for key, control, edited in PAIRS:
        entry = facts["pairs"][key]
        for name in (control, edited):
            path = os.path.join(FIXTURE_DIR, name)
            log(f"fixture {name} ({os.path.getsize(path)} bytes)")
        log(f"{key}: " + ", ".join(
            f"ID {tid} {entry['after_edit'][tid]['percent_complete']} %"
            for tid in sorted((k for k in entry["after_edit"] if k.isdigit()), key=int)))


if __name__ == "__main__":
    main()
