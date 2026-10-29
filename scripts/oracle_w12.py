#!/usr/bin/env python3
"""Wave-12 coordinator oracle: recurring calendar exceptions.

Builds one synthetic project whose project calendar carries one exception of
each PjExceptionType, records every COM property, saves the .mpp, and reopens it
read-only.  This is the ground truth for decoding the recurrence parameter
words that `core/cal` does not yet read (weekday mask, every-N, position,
month/day).

    python scripts/oracle_w12.py exceptions
    python scripts/oracle_w12.py apiprobe

Rules: no /automation, CalculateProject, FileClose(0), aware datetimes,
absolute SaveAs paths.  Synthetic fixture only; the JSON keeps numbers/dates.
"""

import datetime as dt
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import log, oracle_session, working_copy      # noqa: E402
from oracle_privacy import neutralise  # noqa: E402
from oracle_w3 import Guard, calc, new_project, save, utc, val   # noqa: E402

PROBE = os.path.join(ROOT, "corpus", "probe")
START = dt.datetime(2026, 10, 19, 8)

PROPS = ("Type", "Start", "Finish", "Occurrences", "Name", "Period",
         "DaysOfWeek", "MonthPosition", "MonthItem", "Month", "MonthDay")


def exception_rows(cal):
    rows = []
    try:
        n = cal.Exceptions.Count
    except Exception as exc:                                  # noqa: BLE001
        return [{"error": f"{exc.__class__.__name__}: {exc}"}]
    for i in range(1, n + 1):
        e = cal.Exceptions(i)
        rec = {}
        for name in PROPS:
            try:
                rec[name] = val(getattr(e, name))
            except Exception as exc:                          # noqa: BLE001
                rec[name] = f"<err {exc.__class__.__name__}: {exc}>"
        rows.append(rec)
    return rows


def exceptions():
    path = os.path.join(PROBE, "w12-exceptions.mpp")
    # (label, kwargs) -- values chosen to separate every enum field.
    specs = [
        ("daily", dict(Type=1, Start=utc(dt.datetime(2026, 11, 2)),
                       Finish=utc(dt.datetime(2026, 11, 30)),
                       Occurrences=0, Name="DailyOff", Period=1)),
        ("weekly", dict(Type=6, Start=utc(dt.datetime(2026, 11, 2)),
                        Finish=utc(dt.datetime(2026, 12, 31)),
                        Occurrences=0, Name="WeeklyOff",
                        Period=1, DaysOfWeek=10)),    # Mon+Wed (Mon=4, Wed=6?)
        ("monthly_day", dict(Type=4, Start=utc(dt.datetime(2026, 11, 15)),
                             Finish=utc(dt.datetime(2027, 3, 15)),
                             Occurrences=0, Name="Monthly15",
                             Period=1, MonthDay=15)),
        ("monthly_pos", dict(Type=5, Start=utc(dt.datetime(2026, 11, 2)),
                             Finish=utc(dt.datetime(2027, 6, 1)),
                             Occurrences=0, Name="FirstMonday",
                             Period=1, MonthPosition=0, MonthItem=4)),
        ("yearly_day", dict(Type=2, Start=utc(dt.datetime(2027, 3, 15)),
                            Finish=utc(dt.datetime(2030, 3, 15)),
                            Occurrences=0, Name="Yearly315",
                            Period=1, Month=3, MonthDay=15)),
        ("yearly_pos", dict(Type=3, Start=utc(dt.datetime(2026, 11, 27)),
                            Finish=utc(dt.datetime(2030, 11, 27)),
                            Occurrences=0, Name="LastFridayNov",
                            Period=1, MonthPosition=4, MonthItem=8, Month=11)),
        ("day_count", dict(Type=7, Start=utc(dt.datetime(2026, 12, 21)),
                           Finish=utc(dt.datetime(2026, 12, 25)),
                           Occurrences=0, Name="FiveDays")),
    ]
    with oracle_session() as app:
        with Guard(420):
            proj = new_project(app, START)
            cal = proj.Calendar
            added = []
            for label, kwargs in specs:
                try:
                    e = cal.Exceptions.Add(**kwargs)
                    added.append({"label": label, "result": val(e.Name)})
                except Exception as exc:                      # noqa: BLE001
                    added.append({"label": label,
                                  "error": f"{exc.__class__.__name__}: {exc}"})
            sizes = {}
            if os.path.exists(path):
                os.remove(path)
            neutralise(app, proj, "Synthetic Calendar Fixture")
            app.FileSaveAs(Name=path)
            sizes["saved"] = os.path.getsize(path)
            before = exception_rows(cal)
            app.FileClose(0)
            app.FileOpen(Name=path, ReadOnly=True)
            reopened = exception_rows(app.ActiveProject.Calendar)
            app.FileClose(0)

    payload = {"case": "exceptions", "added": added, "sizes": sizes,
               "before": before, "reopened": reopened,
               "saved": os.path.relpath(path, ROOT)}
    save("w12-exceptions", payload)
    print(json.dumps(payload, indent=2))


def apiprobe():
    with oracle_session() as app:
        with Guard(240):
            proj = new_project(app, START)
            cal = proj.Calendar
            log(f"calendar members: "
                f"{[n for n in dir(cal) if not n.startswith('_')]}")
            log(f"exceptions members: "
                f"{[n for n in dir(cal.Exceptions) if not n.startswith('_')]}")
            e = cal.Exceptions.Add(Type=1,
                                   Start=utc(dt.datetime(2026, 11, 2)),
                                   Finish=utc(dt.datetime(2026, 11, 30)),
                                   Occurrences=0, Name="ProbeDaily")
            log(f"added: {e.Name} type={val(e.Type)} start={val(e.Start)} "
                f"finish={val(e.Finish)} occ={val(e.Occurrences)}")
            log(f"count={cal.Exceptions.Count}")


def linkadd():
    """Project's own add-link save on the same synthetic base (W52's target)."""
    original = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")
    src = working_copy(original, "w12-linkadd")  # wave 23: never in place
    out = os.path.join(PROBE, "w12-linkadd.mpp")
    with oracle_session() as app:
        with Guard(300):
            app.FileOpen(Name=src, ReadOnly=False)
            proj = app.ActiveProject
            t1 = proj.Tasks(1)
            t2 = proj.Tasks(2)
            t3 = proj.Tasks(3)
            t2.TaskDependencies.Add(t1, 1, "0")     # FS
            t3.TaskDependencies.Add(t1, 3, "1d")    # SS+1d
            calc(app)
            if os.path.exists(out):
                os.remove(out)
            neutralise(app, proj, "Synthetic Link Fixture")
            app.FileSaveAs(Name=out)
            size = os.path.getsize(out)
            app.FileClose(0)
    payload = {"case": "link-add", "source": os.path.relpath(original, ROOT),
               "saved": os.path.relpath(out, ROOT), "bytes": size,
               "links": [{"pred": 1, "succ": 2, "type": 1, "lag": "0"},
                         {"pred": 1, "succ": 3, "type": 3, "lag": "1d"}]}
    save("w12-linkadd", payload)
    print(json.dumps(payload, indent=2))


def assignadd():
    """Project's own add-resource + assignment save on the same synthetic base."""
    src = working_copy(os.path.join(ROOT, "tests", "data", "synthetic-base.mpp"),
                       "w12-assignadd")  # wave 23: never in place
    out = os.path.join(PROBE, "w12-assignadd.mpp")
    with oracle_session() as app:
        with Guard(300):
            app.FileOpen(Name=src, ReadOnly=False)
            proj = app.ActiveProject
            r = proj.Resources.Add("R1")
            r.Type = 0
            r.MaxUnits = 1.0
            calc(app)
            t = proj.Tasks(1)
            t.Assignments.Add(TaskID=t.ID, ResourceID=r.ID)
            calc(app)
            if os.path.exists(out):
                os.remove(out)
            neutralise(app, proj, "Synthetic Assignment Fixture")
            app.FileSaveAs(Name=out)
            size = os.path.getsize(out)
            app.FileClose(0)
    payload = {"case": "assign-add", "source": os.path.relpath(src, ROOT),
               "saved": os.path.relpath(out, ROOT), "bytes": size,
               "resource": "R1 Work 100%", "assignment": "T1/R1 100%"}
    save("w12-assignadd", payload)
    print(json.dumps(payload, indent=2))


def monthpos():
    """A monthly-positional exception whose start day is NOT the target weekday:
    third Monday, range starts Sunday 2026-11-01.  Settles whether Project stores
    MonthItem for type 5."""
    path = os.path.join(PROBE, "w12-exceptions-monthpos.mpp")
    with oracle_session() as app:
        with Guard(300):
            proj = new_project(app, START)
            cal = proj.Calendar
            rec = {}
            try:
                e = cal.Exceptions.Add(
                    Type=5, Start=utc(dt.datetime(2026, 11, 1)),
                    Finish=utc(dt.datetime(2027, 6, 30)), Occurrences=0,
                    Name="ThirdMonday", Period=1, MonthPosition=2,
                    MonthItem=4)
                rec["added"] = val(e.Name)
            except Exception as exc:                          # noqa: BLE001
                rec["error"] = f"{exc.__class__.__name__}: {exc}"
            if os.path.exists(path):
                os.remove(path)
            neutralise(app, proj, "Synthetic Calendar Fixture")
            app.FileSaveAs(Name=path)
            rec["bytes"] = os.path.getsize(path)
            rec["before"] = exception_rows(cal)
            app.FileClose(0)
            app.FileOpen(Name=path, ReadOnly=True)
            rec["reopened"] = exception_rows(app.ActiveProject.Calendar)
            app.FileClose(0)
    payload = {"case": "monthpos", "saved": os.path.relpath(path, ROOT),
               **rec}
    save("w12-monthpos", payload)
    print(json.dumps(payload, indent=2))


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    if sys.argv[1] == "exceptions":
        exceptions()
    elif sys.argv[1] == "apiprobe":
        apiprobe()
    elif sys.argv[1] == "linkadd":
        linkadd()
    elif sys.argv[1] == "assignadd":
        assignadd()
    elif sys.argv[1] == "monthpos":
        monthpos()
    else:
        raise SystemExit(f"unknown case {sys.argv[1]!r}")


if __name__ == "__main__":
    main()
