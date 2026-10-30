#!/usr/bin/env python3
"""Wave 34 (W34-0.3): a SECOND Calculate Project with no model change.

spec/writers/scheduler-save.md leaves one save rule open: the wave-30 pair
measures the first Calculate after a pending duration change, but not whether
a Calculate with nothing to do re-appends the nine var blocks or writes
nothing.  This pair answers it: the base is wave30-calculate.mpp (already
calculated, so no pending change), opened in its stored automatic-calculation
mode on a copy under C:/w34; the control is that session's save, the edit is
the save after Application.CalculateProject() with no model change.

If the engine re-appends, the edit grows VarMeta/Var2Data by nine entries;
if it has nothing to write back, every stream except the per-save noise is
byte-identical.  COM facts before and after prove the model did not change.

    python -I scripts/oracle_w34_calculate.py

The script refuses to run if any target fixture already exists.
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
from oracle_privacy import neutralise  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w34"
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-calculate")
OUT = os.path.join(ROOT, "build", "w34")
RESULT = os.path.join(OUT, "W34-0.3-calculate-second.json")
WORK = os.path.join(OUT, "calculate-work")
BASE_SOURCE = os.path.join(ROOT, "tests", "data", "wave30-calculate.mpp")

CONTROL = "wave34-calculate-second-control.mpp"
EDIT = "wave34-calculate-second.mpp"


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


def facts(project):
    out = {
        "calculation": prop(project, "Calculation"),
        "start": prop(project, "ProjectStart"),
        "finish": prop(project, "ProjectFinish"),
        "duration": prop(project, "Duration"),
        "work": prop(project, "Work"),
        "tasks": {},
    }
    for index in range(1, project.Tasks.Count + 1):
        task = project.Tasks(index)
        out["tasks"][str(index)] = {
            "id": prop(task, "ID"),
            "unique_id": prop(task, "UniqueID"),
            "start": prop(task, "Start"),
            "finish": prop(task, "Finish"),
            "duration": prop(task, "Duration"),
            "work": prop(task, "Work"),
            "percent_complete": prop(task, "PercentComplete"),
        }
    return out


def main():
    for name in (CONTROL, EDIT):
        path = os.path.join(FIXTURE_DIR, name)
        if os.path.exists(path):
            sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    if not os.path.isfile(BASE_SOURCE):
        sys.exit(f"fixture missing: {BASE_SOURCE}")
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)

    result = {"version": None, "pair": {}}
    entry = result["pair"]
    with oracle_session() as app:
        with Guard(900):
            result["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"
            source = working_copy(BASE_SOURCE, "w34-0.3-calculate-second")
            copy = os.path.join(WORK, "calculate-second.mpp")
            shutil.copyfile(source, copy)
            app.FileOpen(copy)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic W34 Calculate Fixture")
            entry["control_facts"] = facts(project)
            app.FileSaveAs(os.path.join(FIXTURE_DIR, CONTROL))
            app.CalculateProject()
            entry["edit_facts"] = facts(project)
            app.FileSaveAs(os.path.join(FIXTURE_DIR, EDIT))
            app.FileClose(0)

            for key, name in (("control", CONTROL), ("edit", EDIT)):
                app.FileOpen(os.path.join(FIXTURE_DIR, name))
                entry[key + "_reopen"] = facts(app.ActiveProject)
                app.FileClose(0)

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for name in (CONTROL, EDIT):
        path = os.path.join(FIXTURE_DIR, name)
        log(f"fixture {name} ({os.path.getsize(path)} bytes)")
    log(f"control {entry['control_facts']}")
    log(f"edit    {entry['edit_facts']}")


if __name__ == "__main__":
    main()
