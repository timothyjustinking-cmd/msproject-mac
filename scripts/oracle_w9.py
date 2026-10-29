#!/usr/bin/env python3
"""Wave-9 coordinator oracle: the fixtures W41 asked for (elapsed lag, calendars).

    python scripts/oracle_w9.py elapsedlag
        corpus/probe/w9-elapsedlag.mpp + docs/analysis/oracle/w9-elapsedlag.json
        Elapsed-duration lags ("2 edays") against working-day lags and an
        elapsed DURATION control, on the Standard calendar.

    python scripts/oracle_w9.py exceptionsapi
        Introspection only: does the Project COM object model expose calendar
        exceptions (needed before five recurring-exception fixtures are worth
        building)?  Prints the members of Application/Calendar that mention
        "xcep" and the type of anything that looks like a collection.

Rules encoded here (see docs/coordination.md): no `/automation` argument,
`CalculateProject` not `Calculate`, `FileClose(0)` (handled by oracle_lock),
aware datetimes for input, `FileSaveAs` with an absolute path.  Client-data
rule: the fixtures are synthetic (T1..T8), and the committed JSON keeps numbers,
dates and ids only.
"""

import datetime as dt
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import log, oracle_session                      # noqa: E402
from oracle_privacy import neutralise  # noqa: E402
from oracle_w3 import (Guard, add_task, calc, new_project,       # noqa: E402
                       rows_of, save, val)

PROBE = os.path.join(ROOT, "corpus", "probe")
FS = 1

# (id, duration text, predecessor id or None, lag) -- the shape W41 asked for.
ELAPSED_PLAN = [
    (1, "3d", None, None),
    (2, "1d", 1, "2 edays"),
    (3, "1d", 1, "3 edays"),
    (4, "1d", 1, "2 days"),
    (5, "5d", None, None),
    (6, "1d", 5, "2 edays"),
    (7, "1d", 5, "2 days"),
    (8, "2 edays", None, None),
]


def elapsedlag():
    path = os.path.join(PROBE, "w9-elapsedlag.mpp")
    with oracle_session() as app:
        with Guard(300):
            proj = new_project(app, dt.datetime(2026, 10, 19, 8))
            tasks = {}
            for tid, duration, _pred, _lag in ELAPSED_PLAN:
                tasks[tid] = add_task(proj, f"T{tid}", duration)
            links = []
            for tid, _duration, pred, lag in ELAPSED_PLAN:
                if pred is None:
                    continue
                tasks[tid].TaskDependencies.Add(tasks[pred], FS, lag)
                links.append({"succ": tid, "pred": pred, "type": FS, "lag": lag})
            calc(app)
            rows = rows_of(proj)
            payload = {
                "case": "elapsed-lag",
                "run_at": dt.datetime.now().isoformat(timespec="seconds"),
                "project_start": val(proj.ProjectStart),
                "project_finish": val(getattr(proj, "ProjectFinish", None)),
                "calendar": val(getattr(proj, "Calendar", None)),
                "plan": [{"id": t, "duration": d, "pred": p, "lag": l}
                         for t, d, p, l in ELAPSED_PLAN],
                "links": links,
                "tasks": rows,
            }
            if os.path.exists(path):
                os.remove(path)
            neutralise(app, proj)
            app.FileSaveAs(Name=path)
            payload["saved"] = os.path.relpath(path, ROOT)
            payload["saved_bytes"] = os.path.getsize(path)
            save("w9-elapsedlag", payload)

    print(f"project {payload['project_start']} -> {payload['project_finish']}")
    for r in payload["tasks"]:
        print(f"  T{r['ID']:<2} dur={r['Duration']:<6} ({r.get('DurationText')}) "
              f"start={r['Start']} finish={r['Finish']} "
              f"late=({r.get('LateStart')}, {r.get('LateFinish')}) "
              f"startslack={r.get('StartSlack')} totalslack={r.get('TotalSlack')} "
              f"freeslack={r.get('FreeSlack')}")
    print(f"saved {payload['saved']} ({payload['saved_bytes']:,} B)")


def exceptionsapi():
    with oracle_session() as app:
        with Guard(240):
            new_project(app, dt.datetime(2026, 10, 12, 8))
            proj = app.ActiveProject
            for label, obj in (("Application", app), ("Project", proj)):
                names = [n for n in dir(obj) if "xcep" in n.lower()]
                log(f"{label} members mentioning 'xcep': {names}")
            try:
                bcs = app.BaseCalendars
            except Exception as exc:                         # noqa: BLE001
                log(f"BaseCalendars failed: {exc.__class__.__name__}: {exc}")
                return
            log(f"BaseCalendars type: {type(bcs)!r}")
            log(f"BaseCalendars members: "
                f"{[n for n in dir(bcs) if not n.startswith('_')]}")
            try:
                log(f"BaseCalendars.Count: {bcs.Count}")
            except Exception as exc:                         # noqa: BLE001
                log(f"Count failed: {exc.__class__.__name__}: {exc}")
            for name in ("Standard",):
                try:
                    cal = bcs(name)
                except Exception as exc:                     # noqa: BLE001
                    log(f"BaseCalendars({name!r}) failed: "
                        f"{exc.__class__.__name__}: {exc}")
                    continue
                log(f"calendar {name}: {[n for n in dir(cal) if not n.startswith('_')]}")
            for attr in ("Calendar", "ProjectCalendar"):
                try:
                    cal = getattr(proj, attr)
                    log(f"Project.{attr}: {[n for n in dir(cal) if not n.startswith('_')]}")
                except Exception as exc:                     # noqa: BLE001
                    log(f"Project.{attr} failed: {exc.__class__.__name__}: {exc}")


def resourcetypes():
    """W40's ask: a Work (50%), a Material and a Cost resource, saved + exported.

    Resource Type is not located in the 172-byte resource row (every fixture on
    disk is Work), and MaxUnits' +8 double scale is only tested at 100%.  This
    fixture puts all three types in one file at a non-default MaxUnits.
    """
    mpp = os.path.join(PROBE, "w9-resourcetypes.mpp")
    xml = os.path.join(PROBE, "w9-resourcetypes.xml")
    with oracle_session() as app:
        with Guard(300):
            proj = new_project(app, dt.datetime(2026, 10, 19, 8))
            tasks = [add_task(proj, f"T{i}", "2d" if i == 1 else "1d")
                     for i in (1, 2, 3)]
            specs = [("R1", 0, 0.5, None),      # work, 50%
                     ("R2", 1, 1.0, "metre"),       # material
                     ("R3", 2, 1.0, None)]      # cost
            resources = []
            for name, rtype, units, label in specs:
                r = proj.Resources.Add(name)
                r.Type = rtype
                # MaxUnits only applies to a Work resource: Project rejects the
                # assignment on a Material/Cost resource (measured, this script).
                if rtype == 0:
                    r.MaxUnits = units
                if label is not None:
                    r.MaterialLabel = label
                resources.append(r)
            for task, res in zip(tasks, resources):
                task.Assignments.Add(ResourceID=res.ID)
            calc(app)
            rows = []
            for r in resources:
                rows.append({"id": val(r.ID), "type": val(r.Type),
                             "max_units": val(r.MaxUnits),
                             "material_label": val(getattr(r, "MaterialLabel", None))})
            payload = {"case": "resource-types",
                       "run_at": dt.datetime.now().isoformat(timespec="seconds"),
                       "resources": rows,
                       "tasks": [{k: v for k, v in row.items()
                                  if k not in ("Name", "Notes", "Predecessors",
                                               "Successors")}
                                 for row in rows_of(proj)]}
            if os.path.exists(mpp):
                os.remove(mpp)
            neutralise(app, proj)
            app.FileSaveAs(Name=mpp)
            payload["saved"] = os.path.relpath(mpp, ROOT)
            payload["saved_bytes"] = os.path.getsize(mpp)
            if os.path.exists(xml):
                os.remove(xml)
            app.FileSaveAs(Name=xml, FormatID="MSProject.xml")
            payload["export"] = os.path.relpath(xml, ROOT)
            payload["export_bytes"] = os.path.getsize(xml)
            save("w9-resourcetypes", payload)

    print("resources (COM):", payload["resources"])
    import re
    text = open(xml, encoding="utf-8", errors="replace").read()
    for uid, rtype, units in re.findall(
            r"<Resource>.*?<UID>(\d+)</UID>.*?<Type>(\d+)</Type>.*?<MaxUnits>([\d.]+)</MaxUnits>",
            text, re.S):
        print(f"  export UID {uid}: Type={rtype} MaxUnits={units}")
    print(f"saved {payload['saved']} ({payload['saved_bytes']:,} B), "
          f"export {payload['export']} ({payload['export_bytes']:,} B)")


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    case = sys.argv[1]
    if case == "elapsedlag":
        elapsedlag()
    elif case == "exceptionsapi":
        exceptionsapi()
    elif case == "resourcetypes":
        resourcetypes()
    else:
        raise SystemExit(f"unknown case {case!r}")


if __name__ == "__main__":
    main()
