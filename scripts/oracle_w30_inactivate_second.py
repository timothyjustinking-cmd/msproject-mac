#!/usr/bin/env python3
"""Wave 30 (W30-1.2): Project's Inactivate save on a file that already has one.

The first pair (scripts/oracle_w30_inactivate.py) measures one inactivation on
the all-active synthetic base.  This second pair starts from that pair's
edited file (tests/data/wave30-inactivate.mpp: ID 2 inactive, the other two
leaves active), saves a fresh control, inactivates ID 1, and saves again.
It answers the questions the first pair cannot:

  * does the project row's active-ID list stay the same shape when one more
    ID drops out ([2,1,3] -> [1,3])?
  * does the 114/Props byte that the first save set to 0x04 stay 0x04?
  * does the already-inactive row's bytes stay put?

The first edited file is COPIED into build/w30/inactivate-work before Project
opens it (a committed fixture is never opened in place).  Facts by ID only.

    python -I scripts/oracle_w30_inactivate_second.py

The script refuses to run if any target file already exists.
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

NEUTRAL = r"C:/w30"
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-inactivate")
OUT = os.path.join(ROOT, "build", "w30")
RESULT = os.path.join(OUT, "W30-1.2-project-inactivate-second.json")
WORK = os.path.join(OUT, "inactivate-work")

SOURCE = os.path.join(ROOT, "tests", "data", "wave30-inactivate.mpp")

CONTROL = "wave30-inactivate-second-control.mpp"
EDITED = "wave30-inactivate-second.mpp"

sys.path.insert(0, HERE)
from oracle_w30_inactivate import all_facts, prop, save_as  # noqa: E402


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
    if not os.path.isfile(SOURCE):
        sys.exit(f"fixture missing: {SOURCE} (run the first measurement first)")
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)

    facts = {"version": None, "control": CONTROL, "edited": EDITED,
             "source": os.path.basename(SOURCE)}
    with oracle_session() as app:
        with Guard(600):
            facts["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"
            work = os.path.join(WORK, "inactivate-second.mpp")
            shutil.copyfile(SOURCE, work)
            app.FileOpen(work)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Inactivate Fixture")
            facts["control_facts_before_save"] = all_facts(project)
            facts["control"] = save_as(app, CONTROL)
            facts["control_facts"] = all_facts(project)

            target = project.Tasks(1)
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
                f"active {item['active']} level {item['outline_level']} "
                f"outline {item['outline_number']} duration {item['duration']}")


if __name__ == "__main__":
    main()
