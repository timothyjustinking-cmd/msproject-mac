#!/usr/bin/env python3
"""Wave 30 (W30-1.6): Project 2021's Add to Timeline save, measured.

Measures Application.TaskOnTimeline -- the COM method behind Task > Properties >
Add to Timeline -- on the synthetic base (three top-level manual leaves, no
links, no progress, one placeholder assignment per task):

  * the control is saved;
  * TaskOnTimeline(TaskID=2) is called on sheet row 2 (ID 2), then the file is
    saved;
  * both files are reopened for facts.

On this Project build (16.0.20430) the ribbon's TaskAddToTimeline toggle is
disabled and inert -- for every row, for Select All, and on a fresh
Project-created project -- and TaskOnTimeline returns True without adding a
bar.  The edit save therefore carries NO timeline state: under the
save-pair mask its only unmasked byte is Project's universal Props14 edit
marker 0x3540000a at normalised offset 402 (payload 0x0000 -> 0x0001), and the
pair totals 137 noise bytes with zero effects everywhere else (see
build/w30/W30-1.6-timeline-decision.log).  The pair is still committed as the
negative baseline and oracle for a later writer.

One continuous session per pair so Project's per-save identities are shared.
Files are saved under the neutral root C:/w30/fixtures-timeline with neutral
Author/Manager/Company/UserName/Title.  The COM facts before and after the
edit, and on Project's own reopen, go to
build/w30/W30-1.6-project-timeline.json (not committed).  Rows are recorded by
ID only; no task or project name is read, printed or stored.

    python -I scripts/oracle_w30_timeline.py

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
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-timeline")
OUT = os.path.join(ROOT, "build", "w30")
RESULT = os.path.join(OUT, "W30-1.6-project-timeline.json")
WORK = os.path.join(OUT, "timeline-work")

BASE_SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")
CONTROL_NAME = "wave30-timeline-control.mpp"
EDITED_NAME = "wave30-timeline.mpp"
TARGET_ID = 2


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
        "outline_level": prop(task, "OutlineLevel"),
        "duration": prop(task, "Duration"),
        "percent_complete": prop(task, "PercentComplete"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
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


def timeline_probe(project):
    """Best-effort read of the Timeline object (the object model is small)."""
    info = {}
    try:
        timeline = project.Timeline
    except Exception as exc:  # noqa: BLE001
        return {"timeline": f"<unreadable: {exc}>"}
    info["type"] = str(type(timeline))
    for name in ("Show", "Visible", "IsVisible", "ShowTimeline", "Bars", "Tasks",
                 "Name"):
        try:
            value = getattr(timeline, name)
            info[name] = val(value)
        except Exception as exc:  # noqa: BLE001
            info[name] = f"<unreadable: {exc}>"
    return info


def neutralise(app, project):
    app.UserName = "Fixture Builder"
    project.Author = "Fixture Builder"
    project.Manager = "Fixture Builder"
    project.Company = "Fixture Company"
    project.Title = "Synthetic Timeline Fixture"


def main():
    for name in (CONTROL_NAME, EDITED_NAME):
        path = os.path.join(FIXTURE_DIR, name)
        if os.path.exists(path):
            sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    if not os.path.isfile(BASE_SOURCE):
        sys.exit(f"fixture missing: {BASE_SOURCE}")
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)

    facts = {"version": None, "target_id": TARGET_ID,
             "control": CONTROL_NAME, "edited": EDITED_NAME,
             "source": os.path.basename(BASE_SOURCE)}
    work = os.path.join(WORK, "timeline.mpp")
    shutil.copyfile(working_copy(BASE_SOURCE, "timeline"), work)
    with oracle_session() as app:
        with Guard(900):
            facts["version"] = prop(app, "Version")
            app.FileOpen(work)
            project = app.ActiveProject
            neutralise(app, project)
            facts["timeline_before"] = timeline_probe(project)
            facts["control_facts_before_save"] = all_facts(project)
            app.FileSaveAs(os.path.join(FIXTURE_DIR, CONTROL_NAME))
            facts["control_facts"] = all_facts(project)

            added = app.TaskOnTimeline(TaskID=TARGET_ID, Remove=False)
            facts["task_on_timeline_return"] = val(added)
            facts["timeline_after"] = timeline_probe(project)
            facts["edited_facts_before_save"] = all_facts(project)
            app.FileSaveAs(os.path.join(FIXTURE_DIR, EDITED_NAME))
            facts["edited_facts"] = all_facts(project)
            app.FileClose(0)

            for key, name in (("control", CONTROL_NAME),
                              ("edited", EDITED_NAME)):
                app.FileOpen(os.path.join(FIXTURE_DIR, name))
                facts[key + "_reopen"] = all_facts(app.ActiveProject)
                facts[key + "_reopen_timeline"] = timeline_probe(app.ActiveProject)
                app.FileClose(0)

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for name in (CONTROL_NAME, EDITED_NAME):
        path = os.path.join(FIXTURE_DIR, name)
        log(f"{name}: {os.path.getsize(path)} bytes")
    log(f"TaskOnTimeline returned {facts['task_on_timeline_return']}")
    log(f"timeline before {facts['timeline_before']}")
    log(f"timeline after  {facts['timeline_after']}")
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
                f"duration {item['duration']} percent "
                f"{item['percent_complete']}")


if __name__ == "__main__":
    main()
