#!/usr/bin/env python3
"""Wave 28 (W28-1.1): Project 2021's PercentComplete saves, one pair per edit.

Sets Task ID 1's PercentComplete through Project 2021 COM on a synthetic-base
copy and saves a before/after pair for each edit the brief names:

  * auto-scheduled leaf 0 -> 40 %
  * auto-scheduled leaf 40 -> 100 %
  * auto-scheduled leaf 100 -> 0 %
  * the same leaf made Manual=True at 40 %

Every save lives under the neutral root C:/w28/fixtures and carries neutral
Author/Manager/Company/UserName/Title.  The pairs are committed under
tests/data/ as the standing-rule-1 oracle for the wave-28 actuals writer; the
facts (COM values before and after each edit, and on reopen) are written to
build/w28/W28-1.1-project-actuals.json (not committed).

    python scripts/oracle_w28_actuals.py

The script refuses to run if any file in the pairs already exists, so a
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

from oracle_lock import oracle_session, log  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w28"
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures")
OUT = os.path.join(ROOT, "build", "w28")
RESULT = os.path.join(OUT, "W28-1.1-project-actuals.json")
WORK = os.path.join(OUT, "actuals-work")

BASE_SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")
AUTO_BASE = os.path.join(WORK, "auto-base.mpp")
MANUAL_BASE = os.path.join(WORK, "manual-base.mpp")

# synthetic-base's three tasks are Manual=True (measured: the W27 fixture's
# 50 % save left PercentWorkComplete 0 and only the ActualStart the manual row
# already stored).  The auto pairs therefore start from a copy whose Task ID 1
# was switched to Manual=False FIRST; the manual pair uses the fixture's own
# manual row unchanged.
AUTO_PAIRS = [
    ("auto0_40", "wave28-percent-auto0-control.mpp", "wave28-percent-auto40.mpp", 40),
    ("auto40_100", "wave28-percent-auto40-control.mpp", "wave28-percent-auto100.mpp", 100),
    ("auto100_0", "wave28-percent-auto100-control.mpp", "wave28-percent-auto0.mpp", 0),
]
MANUAL_PAIRS = [
    ("manual0_40", "wave28-percent-manual0-control.mpp", "wave28-percent-manual40.mpp", 40),
]
PAIRS = [(key, control, edited, True, percent) for key, control, edited, percent in AUTO_PAIRS] + [
    (key, control, edited, False, percent) for key, control, edited, percent in MANUAL_PAIRS
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


def project_facts(project):
    return task_facts(project.ProjectSummaryTask)


def all_facts(project):
    rows = {"project": project_facts(project)}
    for task in project.Tasks:
        if task is None:
            continue
        rows[str(prop(task, "ID"))] = task_facts(task)
    return rows


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


def main():
    for _, control, edited, _, _ in PAIRS:
        for name in (control, edited):
            path = os.path.join(FIXTURE_DIR, name)
            if os.path.exists(path):
                sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    shutil.copyfile(BASE_SOURCE, AUTO_BASE)
    shutil.copyfile(BASE_SOURCE, MANUAL_BASE)

    facts = {"version": None, "pairs": {}}
    with oracle_session() as app:
        with Guard(600):
            facts["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"

            # ---- auto-scheduled pairs: one continuous session -------------
            app.FileOpen(AUTO_BASE)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Percent Fixture")
            project.Tasks(1).Manual = False
            facts["auto_initial"] = all_facts(project)
            for key, control, edited, percent in AUTO_PAIRS:
                control_path = os.path.join(FIXTURE_DIR, control)
                edited_path = os.path.join(FIXTURE_DIR, edited)
                app.FileSaveAs(control_path)
                entry = facts.setdefault(key, {})
                entry["control_path"] = control
                entry["edited_path"] = edited
                entry["scheduled"] = "auto"
                entry["target_percent"] = percent
                entry["control"] = all_facts(project)
                project.Tasks(1).PercentComplete = percent
                entry["after_edit"] = all_facts(project)
                app.FileSaveAs(edited_path)
            app.FileClose(0)

            # ---- manual pair: the fixture's own manual row, fresh copy ----
            app.FileOpen(MANUAL_BASE)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Percent Fixture")
            facts["manual_initial"] = all_facts(project)
            for key, control, edited, percent in MANUAL_PAIRS:
                control_path = os.path.join(FIXTURE_DIR, control)
                edited_path = os.path.join(FIXTURE_DIR, edited)
                app.FileSaveAs(control_path)
                entry = facts.setdefault(key, {})
                entry["control_path"] = control
                entry["edited_path"] = edited
                entry["scheduled"] = "manual"
                entry["target_percent"] = percent
                entry["control"] = all_facts(project)
                project.Tasks(1).PercentComplete = percent
                entry["after_edit"] = all_facts(project)
                app.FileSaveAs(edited_path)
            app.FileClose(0)

            # Reopen every committed file: what Project itself displays.
            for key, control, edited, _, _ in PAIRS:
                entry = facts[key]
                entry["control_reopen"] = reopen_facts(
                    app, os.path.join(FIXTURE_DIR, control))
                entry["edited_reopen"] = reopen_facts(
                    app, os.path.join(FIXTURE_DIR, edited))

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for key, control, edited, _, _ in PAIRS:
        for name in (control, edited):
            path = os.path.join(FIXTURE_DIR, name)
            log(f"fixture {name} ({os.path.getsize(path)} bytes)")
    for label, key in (("auto ID1", "auto_initial"), ("manual ID1", "manual_initial")):
        row = facts.get(key, {}).get("1", {})
        log(f"{label}: pct={row.get('percent_complete')} manual={row.get('manual')} "
            f"start={row.get('start')} finish={row.get('finish')}")


if __name__ == "__main__":
    main()
