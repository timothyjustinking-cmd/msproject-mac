#!/usr/bin/env python3
"""Wave 29 (W29-0.2) probe: is the +174 write a percent-edit rule or save churn?

The wave-28 percent pairs were made on a copy of synthetic-base whose Task ID 1
was switched to Manual=False in the same session as the control save.  Their
edited file stores the row's ActualStart at FixedData +174; every wave-29 pair
(the leaf-under-summary, the work-bearing row and the milestone) leaves +174
untouched.  This probe separates the two explanations:

  a.mpp  neutral copy, ID 1 switched auto, saved
  b.mpp  the SAME session saved again with NO edit between the saves
  c.mpp  PercentComplete = 40, saved

If b.mpp stores +174 = ID 1's Start, the wave-28 pair's +174 is a save/recalc
write, not part of the percent edit; c.mpp then shows what the percent edit
itself changes.

Read-only on the committed fixture (a copy is opened).  The three saves land in
C:/w29/anchor so their row words can be compared (measured: a -> b leaves +174
NA, b -> c writes +174 = Start, so the wave-28 flat auto leaf's +174 is a
percent-edit write, not save churn); ID 1's COM facts go to
build/w29/W29-0.2-anchor-probe.json.

    python -I scripts/oracle_w29_anchor_probe.py
"""

import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log  # noqa: E402
from oracle_privacy import neutralise  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w29/anchor"
RESULT = os.path.join(ROOT, "build", "w29", "W29-0.2-anchor-probe.json")
BASE_SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")
BASE_COPY = os.path.join(NEUTRAL, "base.mpp")
SAVES = [os.path.join(NEUTRAL, name) for name in ("a.mpp", "b.mpp", "c.mpp")]


def prop(obj, name):
    try:
        value = getattr(obj, name)
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"
    return value.strftime("%Y-%m-%d %H:%M:%S") if hasattr(value, "strftime") \
        else value


def task_facts(task):
    return {name: prop(task, name) for name in (
        "ID", "UniqueID", "Manual", "Start", "Finish", "ActualStart",
        "ActualFinish", "PercentComplete", "PercentWorkComplete",
        "ActualDuration", "RemainingDuration", "Duration")}


def main():
    for path in [BASE_COPY] + SAVES:
        if os.path.exists(path):
            sys.exit(f"refusing: {path} already exists (inspect it first)")
    os.makedirs(NEUTRAL, exist_ok=True)
    shutil.copyfile(BASE_SOURCE, BASE_COPY)

    facts = {"version": None, "steps": {}}
    with oracle_session() as app:
        with Guard(300):
            facts["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"
            app.FileOpen(BASE_COPY)
            project = app.ActiveProject
            neutralise(app, project, "Anchor Probe")
            project.Tasks(1).Manual = False
            facts["steps"]["a_before_save"] = task_facts(project.Tasks(1))
            app.FileSaveAs(SAVES[0])
            facts["steps"]["a_after_save"] = task_facts(project.Tasks(1))
            app.FileSaveAs(SAVES[1])
            facts["steps"]["b_after_resave"] = task_facts(project.Tasks(1))
            project.Tasks(1).PercentComplete = 40
            facts["steps"]["c_after_edit"] = task_facts(project.Tasks(1))
            app.FileSaveAs(SAVES[2])
            app.FileClose(0)

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for path in SAVES:
        log(f"saved {path} ({os.path.getsize(path)} bytes)")


if __name__ == "__main__":
    main()
