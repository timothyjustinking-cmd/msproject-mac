#!/usr/bin/env python3
"""Wave-27 oracle fixtures (coordinator only; needs MS Project 2021).

    python scripts/oracle_w27.py

Builds, through MS Project 2021 on neutral copies under C:/w27:

  * wave27-percent-actuals.mpp -- synthetic-base with Task 1 set to 50 %
    PercentComplete by Project.  This is the committed evidence behind W27-0.1
    (Project's own % edit writes the actuals, the dates, the progress flag and
    the project-row roll-up as well as FixedData +92), and the committed
    project-with-actuals fixture the Move Project prompt gate opens.  The local
    corpus/probe/var-pct.mpp is the same measurement but cannot be committed
    (corpus/ is git-ignored).
  * wave27-priority-control.mpp / wave27-priority-750.mpp -- one session's
    control and edited saves for W27-0.3: Project Information's Priority
    (ActiveProject.ProjectSummaryTask.Priority) unchanged (500) and set to 750.
    The pair isolates the bytes Project writes for the edit; the 750 file is the
    fixture the port's save is compared with.

Every save carries neutral metadata (Application.UserName, Author, Manager,
Company, Title) and lives under C:/w27, so no committed fixture embeds the
operator's name or the company path.  The facts JSON is written under
build/w27/ (not committed); the fixtures are copied to tests/data/ only after a
deliberate review of the stream diff and scripts/check_fixtures.py records their
SHA-256.
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

NEUTRAL = r"C:/w27"
OUT = os.path.join(ROOT, "build", "w27")
FIXTURES = os.path.join(OUT, "fixtures")
os.makedirs(NEUTRAL, exist_ok=True)
os.makedirs(FIXTURES, exist_ok=True)

BASE = os.path.join(NEUTRAL, "base.mpp")
ACTUALS = os.path.join(FIXTURES, "wave27-percent-actuals.mpp")
PRIORITY_CONTROL = os.path.join(FIXTURES, "wave27-priority-control.mpp")
PRIORITY_750 = os.path.join(FIXTURES, "wave27-priority-750.mpp")


def prop(obj, name, default=None):
    try:
        return val(getattr(obj, name))
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def val(v):
    if isinstance(v, dt.datetime):
        return v.strftime("%Y-%m-%d %H:%M:%S")
    if isinstance(v, (int, float, bool, str)) or v is None:
        return v
    return repr(v)


def neutralise(app, project, title):
    """Neutral identity, so no committed fixture carries the operator's name."""
    app.UserName = "Fixture Builder"
    project.Author = "Fixture Builder"
    project.Manager = "Fixture Builder"
    project.Company = "Fixture Company"
    project.Title = title


def task_facts(project):
    rows = []
    for task in project.Tasks:
        if task is None:
            continue
        rows.append(
            {
                "id": prop(task, "ID"),
                "uid": prop(task, "UniqueID"),
                "name": prop(task, "Name"),
                "percent_complete": prop(task, "PercentComplete"),
                "actual_duration": prop(task, "ActualDuration"),
                "remaining_duration": prop(task, "RemainingDuration"),
                "duration": prop(task, "Duration"),
                "actual_start": prop(task, "ActualStart"),
                "actual_finish": prop(task, "ActualFinish"),
                "start": prop(task, "Start"),
                "finish": prop(task, "Finish"),
            }
        )
    return rows


def main():
    shutil.copyfile(os.path.join(ROOT, "tests", "data", "synthetic-base.mpp"), BASE)
    facts = {"version": None, "actuals": {}, "priority": {}}
    with oracle_session() as app:
        with Guard(300):
            facts["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"

            # ---- 1. Project's own 50 % edit (W27-0.1 evidence) -------------
            app.FileOpen(BASE)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Actuals Fixture")
            facts["actuals"]["before"] = task_facts(project)
            facts["actuals"]["project_before"] = {
                "percent_complete": prop(project.ProjectSummaryTask, "PercentComplete"),
                "duration": prop(project.ProjectSummaryTask, "Duration"),
            }
            task = project.Tasks(1)  # ID 1 = "Task A"
            task.PercentComplete = 50
            facts["actuals"]["after_edit"] = task_facts(project)
            facts["actuals"]["project_after_edit"] = {
                "percent_complete": prop(project.ProjectSummaryTask, "PercentComplete"),
                "duration": prop(project.ProjectSummaryTask, "Duration"),
            }
            app.FileSaveAs(ACTUALS)
            app.FileClose(0)
            app.FileOpen(ACTUALS)
            facts["actuals"]["reopen"] = task_facts(app.ActiveProject)
            facts["actuals"]["project_reopen"] = {
                "percent_complete": prop(
                    app.ActiveProject.ProjectSummaryTask, "PercentComplete"),
                "duration": prop(app.ActiveProject.ProjectSummaryTask, "Duration"),
            }
            app.FileClose(0)

            # ---- 2. Project Information Priority, one session's pair ------
            app.FileOpen(BASE)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Priority Fixture")
            app.FileSaveAs(PRIORITY_CONTROL)
            facts["priority"]["control_before_edit"] = prop(
                project.ProjectSummaryTask, "Priority")
            project.ProjectSummaryTask.Priority = 750
            facts["priority"]["control_after_edit"] = prop(
                project.ProjectSummaryTask, "Priority")
            app.FileSaveAs(PRIORITY_750)
            app.FileClose(0)
            for label, path in (("control", PRIORITY_CONTROL), ("edited", PRIORITY_750)):
                app.FileOpen(path)
                facts["priority"][f"reopen_{label}"] = prop(
                    app.ActiveProject.ProjectSummaryTask, "Priority")
                app.FileClose(0)

    with open(os.path.join(OUT, "oracle-facts.json"), "w", encoding="utf-8") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
    log("wrote build/w27/oracle-facts.json")
    for path in (ACTUALS, PRIORITY_CONTROL, PRIORITY_750):
        log(f"fixture {path} ({os.path.getsize(path)} bytes)")


if __name__ == "__main__":
    main()
