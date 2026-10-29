#!/usr/bin/env python3
"""Wave-24 oracle fixtures (coordinator only; needs MS Project 2021).

    python scripts/oracle_w24.py

Builds, through MS Project 2021 on neutral copies under C:/w24, the fixture
pairs wave 24 needs and the measurements behind the reader decodes:

  1. summary off/on -- the project-summary row setting (214/Props 0x03400009),
     measured by the reviewer as 0/1 and re-measured here on a fresh pair.
  2. timeline shown/hidden -- the Timeline strip (214/Props 0x03400008,
     "Time&line" / empty).  A pair from ONE session; wave 24 reopens each in a
     FRESH session and captures the window locally (see docs/handoff/
     wave24-complete.md).
  3. stored order != ID order -- four tasks, then a task inserted above row 1
     and saved; Project appends the new FixedData row but displays it as ID 2.
     Used by qt_task_order to prove the ID sort is actually exercised.

Every save carries neutral metadata (Application.UserName, Author, Manager,
Company, Title) and lives under C:/w24, so the committed copies embed neither
the operator's name nor the company path (brief R14).  The outputs are copied to
tests/data/ only after scripts/check_fixtures.py records their SHA-256.
"""

import json
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log  # noqa: E402
from oracle_privacy import neutralise  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w24"
OUT = os.path.join(ROOT, "build", "w24")
os.makedirs(NEUTRAL, exist_ok=True)
os.makedirs(OUT, exist_ok=True)

SUMMARY_OFF = os.path.join(NEUTRAL, "wave24-summary-off.mpp")
SUMMARY_ON = os.path.join(NEUTRAL, "wave24-summary-on.mpp")
TIMELINE_SHOWN = os.path.join(NEUTRAL, "wave24-timeline-shown.mpp")
TIMELINE_HIDDEN = os.path.join(NEUTRAL, "wave24-timeline-hidden.mpp")
ORDER_FIXTURE = os.path.join(NEUTRAL, "wave24-order.mpp")


def prop(obj, name, default=None):
    try:
        return getattr(obj, name)
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def main():
    base = os.path.join(NEUTRAL, "base.mpp")
    shutil.copyfile(os.path.join(ROOT, "tests", "data", "synthetic-base.mpp"), base)
    facts = {"version": None, "summary": {}, "timeline": {}, "order": {}}
    with oracle_session() as app:
        with Guard(300):
            facts["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"

            # ---- 1. project-summary row off/on -----------------------------
            app.FileOpen(base)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Summary Fixture")
            facts["summary"]["open_setting"] = project.DisplayProjectSummaryTask
            project.DisplayProjectSummaryTask = False
            app.FileSaveAs(SUMMARY_OFF)
            facts["summary"]["off_setting_at_save"] = project.DisplayProjectSummaryTask
            project.DisplayProjectSummaryTask = True
            app.FileSaveAs(SUMMARY_ON)
            facts["summary"]["on_setting_at_save"] = project.DisplayProjectSummaryTask
            app.FileClose(0)
            for label, path in (("off", SUMMARY_OFF), ("on", SUMMARY_ON)):
                app.FileOpen(path)
                facts["summary"][f"reopen_{label}"] = app.ActiveProject.DisplayProjectSummaryTask
                app.FileClose(0)
            facts["summary"]["new_project_default"] = None  # filled below

            # ---- 2. Timeline shown/hidden (one session, two saves) ---------
            app.FileOpen(base)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Timeline Fixture")
            app.FileSaveAs(TIMELINE_SHOWN)
            app.TimelineViewToggle()
            time.sleep(1)
            app.FileSaveAs(TIMELINE_HIDDEN)
            app.FileClose(0)

            # ---- 3. stored order differs from ID order ---------------------
            app.FileNew()
            project = app.ActiveProject
            facts["summary"]["new_project_default"] = project.DisplayProjectSummaryTask
            neutralise(app, project, "Synthetic Order Fixture")
            for name in ("Alpha", "Bravo", "Charlie", "Delta"):
                project.Tasks.Add(name)
            # A mixed scheduling state so the row-menu label can be tested
            # against the FILE's task (Alpha auto, the rest manual -- FileNew's
            # default).  Task.Manual is Project's own "Manually Scheduled" cell.
            project.Tasks(1).Manual = False
            order_base = os.path.join(NEUTRAL, "order-base.mpp")
            app.FileSaveAs(order_base)
            app.FileClose(0)

            app.FileOpen(order_base)
            project = app.ActiveProject
            app.SelectRow(1)
            app.InsertTask()
            time.sleep(0.5)
            inserted = None
            for task in project.Tasks:
                if task is not None and task.Name == "<New Task>":
                    inserted = task
                    break
            if inserted is None:
                raise RuntimeError("InsertTask did not create a '<New Task>' row")
            inserted.Name = "Inserted Row"
            # A distinctive duration: the row is stored LAST but displays as
            # ID 2, so the Insert Task placeholder must copy THIS row (what the
            # writer clones), not the highest-ID task (Delta).
            inserted.Duration = 4 * 8 * 60
            facts["order"]["display"] = [
                [task.ID, task.Name] for task in project.Tasks if task is not None
            ]
            facts["order"]["manual"] = [
                [task.ID, task.Name, task.Manual]
                for task in project.Tasks if task is not None
            ]
            # Show the project row in this fixture too, so one file exercises
            # both "stored order != ID order" (W24-5) and every RecordSet row
            # vs plan index call site (W24-1).
            project.DisplayProjectSummaryTask = True
            app.FileSaveAs(ORDER_FIXTURE)
            app.FileClose(0)

    facts["files"] = {
        name: os.path.exists(path)
        for name, path in (("summary_off", SUMMARY_OFF), ("summary_on", SUMMARY_ON),
                           ("timeline_shown", TIMELINE_SHOWN),
                           ("timeline_hidden", TIMELINE_HIDDEN),
                           ("order", ORDER_FIXTURE))
    }
    with open(os.path.join(OUT, "oracle-w24.json"), "w", encoding="utf-8") as fh:
        json.dump(facts, fh, indent=2, default=str)
    print(json.dumps(facts, indent=2, default=str))


if __name__ == "__main__":
    main()
