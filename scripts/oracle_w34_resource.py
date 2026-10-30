#!/usr/bin/env python3
"""Wave 34 (W34-0.3): a SECOND resource calendar row (add a third resource).

wave30-resource-add measured the first `Resources.Add` on the synthetic base:
one TBkndRsc row plus ONE resource-calendar row in TBkndCal, whose 48-byte
Fixed2 slot carries a save-generated identity.  G3.2 needs to know whether the
second added resource gets the same identity family or a new one, so this pair
opens wave30-resource-add.mpp (the base's own resource plus R2), saves the
control, adds a third work resource and saves the edit.

The measured question is in the TBkndCal streams: the new row's 12-byte
FixedData, its FixedMeta and Fixed2Meta entries, and above all the 48-byte
Fixed2 slot identity relative to the first added calendar's.  COM facts show
the resource and calendar counts before and after.

    python -I scripts/oracle_w34_resource.py

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
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-resource")
OUT = os.path.join(ROOT, "build", "w34")
RESULT = os.path.join(OUT, "W34-0.3-resource-third.json")
WORK = os.path.join(OUT, "resource-work")
BASE_SOURCE = os.path.join(ROOT, "tests", "data", "wave30-resource-add.mpp")

CONTROL = "wave34-resource-add-third-control.mpp"
EDIT = "wave34-resource-add-third.mpp"
NEW_NAME = "R3"


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
        "resources": project.Resources.Count,
        "calendars": prop(project, "Calendars"),
        "base_calendars": prop(project, "BaseCalendars"),
        "rows": {},
    }
    for index in range(1, project.Resources.Count + 1):
        resource = project.Resources(index)
        out["rows"][str(index)] = {
            "id": prop(resource, "ID"),
            "unique_id": prop(resource, "UniqueID"),
            "type": prop(resource, "Type"),
            "max_units": prop(resource, "MaxUnits"),
            "standard_rate": prop(resource, "StandardRate"),
        }
    try:
        out["calendar_count"] = project.Calendars.Count
        out["base_calendar_count"] = project.BaseCalendars.Count
    except Exception as exc:  # noqa: BLE001
        out["calendar_count"] = f"<unreadable: {exc}>"
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
            source = working_copy(BASE_SOURCE, "w34-0.3-resource-third")
            copy = os.path.join(WORK, "resource-third.mpp")
            shutil.copyfile(source, copy)
            app.FileOpen(copy)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic W34 Resource Fixture")
            entry["control_facts"] = facts(project)
            app.FileSaveAs(os.path.join(FIXTURE_DIR, CONTROL))
            project.Resources.Add(NEW_NAME)
            entry["edit_facts"] = facts(project)
            if entry["edit_facts"]["resources"] != entry["control_facts"]["resources"] + 1:
                raise SystemExit("refusing: Resources.Add did not add one row")
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
