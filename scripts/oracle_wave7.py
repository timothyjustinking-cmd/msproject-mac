#!/usr/bin/env python3
"""Wave-7 coordinator oracle: MSPDI ground truth and the holiday-calendar fixture.

Coordinator-only (COM to Project hangs in a confined shell). Every subcommand
starts and closes its own Project instance through scripts/oracle_lock.py.

    python scripts/oracle_wave7.py export
        corpus/project_a.mpp  ->  corpus/probe/project_a-export.xml (MSPDI)
        docs/analysis/oracle/project_a_export_links.json (uids/counts only)

    python scripts/oracle_wave7.py holiday
        corpus/probe/holiday1.mpp + docs/analysis/oracle/holiday-probe.json

Client-data rule: the XML lives under git-ignored corpus/. The committed JSON
keeps uids, dates, durations and counts only -- never a task name from the
client file. The holiday fixture is synthetic (T1..T3), so its names are safe.

Traps already measured, encoded here:
  * pywin32 converts a naive datetime to UTC; pass aware-UTC datetimes so
    Project receives the wall-clock value we mean (`utc()` in oracle_w3).
  * Application.Calculate does not exist; use CalculateProject.
  * FileSaveAs needs an absolute path.  For MSPDI the documented selector is
    FormatID="MSProject.xml" (PjFileFormat has no XML member -- verified by
    dumping MSPROJ.OLB). Fallbacks are tried and the output magic is checked.
  * FileClose() defaults to Save=2 and blocks on a modal dialog; oracle_lock
    already closes with Save=0.
"""

import argparse
import datetime as dt
import json
import os
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log                      # noqa: E402
from oracle_w3 import (Guard, add_task, calc, new_project,       # noqa: E402
                       read_task, rows_of, save, setattr_safe, utc, val,
                       OUT, PROBE)                               # noqa: E402

PROJECT_A = os.path.join(ROOT, "corpus", "project_a.mpp")
XML = os.path.join(PROBE, "project_a-export.xml")
HOLIDAY = os.path.join(PROBE, "holiday1.mpp")

NS = {"p": "http://schemas.microsoft.com/project"}


def magic(path):
    try:
        with open(path, "rb") as fh:
            head = fh.read(16)
    except OSError:
        return "missing"
    if head[:5].lstrip().startswith(b"<?xml"):
        return "xml"
    if head[:4] == bytes.fromhex("d0cf11e0"):
        return "cfb"
    return "unknown:" + head[:4].hex()


def try_save_as(app, path, attempts, expect="xml"):
    """Attempt FileSaveAs until the file on disk actually has `expect` format."""
    last = "missing"
    for attempt in attempts:
        if os.path.exists(path):
            os.remove(path)
        try:
            app.FileSaveAs(Name=path, **attempt)
        except Exception as exc:                             # noqa: BLE001
            log(f"  FileSaveAs {attempt!r} failed: {exc.__class__.__name__}: {exc}")
            continue
        if not os.path.exists(path):
            log(f"  FileSaveAs {attempt!r} wrote no file")
            last = "missing"
            continue
        kind = magic(path)
        log(f"  FileSaveAs {attempt!r} -> {kind} ({os.path.getsize(path):,} B)")
        if kind == expect:
            return attempt, kind
        last = kind
        os.remove(path)
    return None, last


# ------------------------------------------------------------------ MSPDI export

def task_attr(t, name, default=None):
    return t.findtext("p:" + name, default=default, namespaces=NS)


def parse_export(path):
    root = ET.parse(path).getroot()
    tasks = root.findall(".//p:Task", NS)
    links, constraints, deadlines, actuals = [], [], [], []
    milestones = 0
    for t in tasks:
        uid = task_attr(t, "UID")
        ct = task_attr(t, "ConstraintType")
        cd = task_attr(t, "ConstraintDate")
        if ct not in (None, "0"):
            constraints.append({"uid": int(uid), "type": int(ct), "date": cd})
        dl = task_attr(t, "Deadline")
        if dl:
            deadlines.append({"uid": int(uid), "deadline": dl})
        if task_attr(t, "ActualStart") or task_attr(t, "ActualFinish"):
            actuals.append({"uid": int(uid),
                            "actual_start": task_attr(t, "ActualStart"),
                            "actual_finish": task_attr(t, "ActualFinish"),
                            "percent": task_attr(t, "PercentComplete")})
        if task_attr(t, "Milestone") == "1":
            milestones += 1
        for pl in t.findall("p:PredecessorLink", NS):
            links.append({
                "succ_uid": int(uid),
                "pred_uid": int(pl.findtext("p:PredecessorUID", namespaces=NS)),
                "type": int(pl.findtext("p:Type", default="1", namespaces=NS)),
                "lag": pl.findtext("p:LinkLag", default="0", namespaces=NS),
                "lag_format": pl.findtext("p:LagFormat", default="", namespaces=NS),
            })
    payload = {
        "source": "corpus/project_a.mpp",
        "export": "corpus/probe/project_a-export.xml",
        "bytes": os.path.getsize(path),
        "tasks": len(tasks),
        "milestones": milestones,
        "links": len(links),
        "link_rows": links,
        "constraints": constraints,
        "deadlines": deadlines,
        "actual_rows": actuals,
    }
    return payload


def cmd_export(args):
    with oracle_session() as app:
        with Guard(300):
            if not os.path.exists(PROJECT_A):
                raise SystemExit(f"missing {PROJECT_A}")
            log(f"opening {os.path.relpath(PROJECT_A, ROOT)}")
            app.FileOpen(Name=PROJECT_A, ReadOnly=True)
            proj = app.ActiveProject
            calc(app)
            log(f"  project start {val(proj.ProjectStart)}")
            used, kind = try_save_as(
                app, XML,
                [{"FormatID": "MSProject.xml"},
                 {"Format": 6},
                 {"Format": 17}])
            if kind != "xml":
                raise SystemExit(f"MSPDI export did not produce XML (last {kind})")
            payload = parse_export(XML)
            payload["format_attempt"] = used
            save("project_a_export_links", payload)
            # counts to the log; the XML itself stays in git-ignored corpus/
            log(f"  {payload['tasks']} tasks, {payload['links']} predecessor links, "
                f"{len(payload['constraints'])} constraints, "
                f"{len(payload['deadlines'])} deadlines, "
                f"{len(payload['actual_rows'])} actual rows")


# ------------------------------------------------------- holiday calendar fixture

def find_calendar(app, name):
    """Application.BaseCalendars is a method returning the Calendars collection."""
    cals = app.BaseCalendars()
    for i in range(1, cals.Count + 1):
        cal = cals(i)
        if val(cal.Name) == name:
            return cal
    raise LookupError(f"base calendar {name!r} not found")


def describe_calendar(app, name):
    cal = find_calendar(app, name)
    week = []
    for i in range(1, 8):                     # PjWeekday: 1=Sunday .. 7=Saturday
        try:
            d = cal.WeekDays(i)
            shifts = []
            for s in (1, 2, 3):
                sh = getattr(d, f"Shift{s}")
                try:
                    shifts.append([val(sh.Start), val(sh.Finish)])
                except Exception:            # noqa: BLE001
                    shifts.append(None)
            week.append({"index": i, "working": val(d.Working), "shifts": shifts})
        except Exception as exc:             # noqa: BLE001
            week.append({"index": i, "error": f"{exc.__class__.__name__}: {exc}"})
    exceptions = []
    try:
        for i in range(1, cal.Exceptions.Count + 1):
            e = cal.Exceptions(i)
            exceptions.append({"name": val(e.Name), "start": val(e.Start),
                               "finish": val(e.Finish), "type": val(e.Type),
                               "occurrences": val(e.Occurrences)})
    except Exception as exc:                 # noqa: BLE001
        exceptions.append({"error": f"{exc.__class__.__name__}: {exc}"})
    return {"name": val(cal.Name), "week_days": week, "exceptions": exceptions}


def cmd_holiday(args):
    """A custom base calendar: short Friday + one non-working Wednesday.

    T1 2d from Mon 19 Oct 2026 08:00            -> Tue 20 Oct 17:00
    T2 5d FS after T1 (Wed 21 is a holiday)     -> Thu 22 Oct 08:00 .. Thu 29 Oct 12:00
    T3 1d explicitly on Fri 23 Oct 2026 08:00   -> Fri 23 Oct 12:00 (4 h Friday)
    """
    with oracle_session() as app:
        with Guard(180):
            proj = new_project(app, dt.datetime(2026, 10, 19))
            app.BaseCalendarCreate(Name="SiteCal", FromName="Standard")
            # short Friday: 08:00-12:00 (4 h)
            app.BaseCalendarEditDays(Name="SiteCal", WeekDay=6, Working=True,
                                     From1="08:00", To1="12:00")
            # one non-working exception: Wednesday 21 October 2026
            app.BaseCalendarEditDays(Name="SiteCal",
                                     StartDate=utc(dt.datetime(2026, 10, 21)),
                                     EndDate=utc(dt.datetime(2026, 10, 21)),
                                     Working=False)
            setattr_safe(proj, "Calendar", "SiteCal", "project calendar")
            t1 = add_task(proj, "T1", "2d", dt.datetime(2026, 10, 19, 8))
            t2 = add_task(proj, "T2", "5d")
            t3 = add_task(proj, "T3", "1d", dt.datetime(2026, 10, 23, 8))
            for t in (t1, t2, t3):
                setattr_safe(t, "Calendar", "SiteCal", f"{t.Name}.Calendar")
            calc(app)
            t2.TaskDependencies.Add(t1, 1, 0)      # FS
            calc(app)
            rows = rows_of(proj)
            try:
                calendar_info = describe_calendar(app, "SiteCal")
            except Exception as exc:             # noqa: BLE001
                calendar_info = {"error": f"{exc.__class__.__name__}: {exc}"}
            payload = {
                "case": "holiday-calendar",
                "run_at": dt.datetime.now().isoformat(timespec="seconds"),
                "project_start": val(proj.ProjectStart),
                "project_calendar": val(getattr(proj, "Calendar", None)),
                "hours_per_day": val(getattr(proj, "HoursPerDay", None)),
                "calendar": calendar_info,
                "tasks": rows,
            }
            show_cols = ("ID", "Duration", "Start", "Finish", "Critical",
                         "TotalSlack", "ConstraintType")
            for r in rows:
                log("  " + "  ".join(f"{k}={r.get(k)}" for k in show_cols))
            if os.path.exists(HOLIDAY):
                os.remove(HOLIDAY)
            app.FileSaveAs(Name=HOLIDAY)
            payload["saved"] = "corpus/probe/holiday1.mpp"
            payload["saved_bytes"] = os.path.getsize(HOLIDAY)
            log(f"  saved {payload['saved']} ({payload['saved_bytes']:,} B)")
            save("holiday-probe", payload)


def _save_mpp(app, path):
    if os.path.exists(path):
        os.remove(path)
    app.FileSaveAs(Name=path)
    log(f"  saved {os.path.relpath(path, ROOT)} ({os.path.getsize(path):,} B)")
    return os.path.getsize(path)


def _export_xml(app, path):
    used, kind = try_save_as(app, path, [{"FormatID": "MSProject.xml"},
                                         {"Format": 6},
                                         {"Format": 17}])
    if kind != "xml":
        raise RuntimeError(f"MSPDI export failed (last {kind})")
    log(f"  exported {os.path.relpath(path, ROOT)} "
        f"({os.path.getsize(path):,} B) via {used!r}")
    return os.path.getsize(path)


def _link_specs(*pairs):
    return [{"succ": val(s.ID), "pred": val(p.ID), "type": t, "lag": str(l)}
            for s, p, t, l in pairs]


def cmd_milestone(args):
    """0-duration tasks: FS+0, FS+50d, explicit 17:00 start, SS+0, after a 1d task.

    Measured question (W23a): PROJECT_A stores three milestones with start == finish
    == 17:00, while core/sched aligns every start-side bound forward to 08:00.
    This fixture isolates that: T3 replicates PROJECT_A 10742's FS+50d placement.
    """
    path = os.path.join(PROBE, "milestone1.mpp")
    with oracle_session() as app:
        with Guard(180):
            proj = new_project(app, dt.datetime(2026, 10, 19))
            t1 = add_task(proj, "T1", "1d", dt.datetime(2026, 10, 19, 8))
            t2 = add_task(proj, "T2", "0d")                       # FS+0 after T1
            t3 = add_task(proj, "T3", "0d")                       # FS+50d after T1
            t4 = add_task(proj, "T4", "0d", dt.datetime(2026, 10, 21, 17))
            t5 = add_task(proj, "T5", "0d")                       # SS+0 after T1
            t6 = add_task(proj, "T6", "1d", dt.datetime(2026, 10, 23, 8))
            t7 = add_task(proj, "T7", "0d")                       # FS+0 after T6
            calc(app)
            pairs = [(t2, t1, 1, 0), (t3, t1, 1, "50d"),
                     (t5, t1, 3, 0), (t7, t6, 1, 0)]
            for succ, pred, lt, lag in pairs:
                succ.TaskDependencies.Add(pred, lt, lag)
            calc(app)
            rows = rows_of(proj)
            payload = {"case": "milestone-duration-zero",
                       "run_at": dt.datetime.now().isoformat(timespec="seconds"),
                       "project_start": val(proj.ProjectStart),
                       "links": _link_specs(*pairs),
                       "tasks": rows}
            for r in rows:
                log(f"  ID={r['ID']} dur={r['Duration']} start={r['Start']} "
                    f"finish={r['Finish']} milestone={r['Milestone']} "
                    f"con={r['ConstraintType']} cdate={r['ConstraintDate']}")
            payload["saved_bytes"] = _save_mpp(app, path)
            payload["saved"] = "corpus/probe/milestone1.mpp"
            save("milestone-probe", payload)


LAGFF = [("T2", "-1d"), ("T3", "-3d"), ("T4", "-6d"),
         ("T5", 0), ("T6", "-1ed")]


def cmd_lagff(args):
    """FF with negative working-day/elapsed-day lags on a FIVE-day predecessor.

    W23b: FF-1d on the existing 1-day w3-o3 pair behaves as if the lag were
    dropped.  On a 5-day predecessor "dropped" and "applied" are distinguishable
    (Fri 23 17:00 vs Thu 22 / Tue 20 / Thu 15 17:00), and -1ed separates the
    working-time and elapsed-time readings.
    """
    path = os.path.join(PROBE, "lagff1.mpp")
    xml = os.path.join(PROBE, "lagff1.xml")
    with oracle_session() as app:
        with Guard(180):
            proj = new_project(app, dt.datetime(2026, 10, 19))
            t1 = add_task(proj, "T1", "5d", dt.datetime(2026, 10, 19, 8))
            ts = [add_task(proj, name, "1d") for name, _ in LAGFF]
            calc(app)
            pairs = [(t, t1, 0, lag) for t, (_, lag) in zip(ts, LAGFF)]
            for succ, pred, lt, lag in pairs:
                succ.TaskDependencies.Add(pred, lt, lag)
            calc(app)
            rows = rows_of(proj)
            payload = {"case": "ff-negative-lag",
                       "run_at": dt.datetime.now().isoformat(timespec="seconds"),
                       "project_start": val(proj.ProjectStart),
                       "links": _link_specs(*pairs),
                       "tasks": rows}
            for r in rows:
                log(f"  ID={r['ID']} dur={r['Duration']} start={r['Start']} "
                    f"finish={r['Finish']} slack={r['TotalSlack']}")
            payload["saved_bytes"] = _save_mpp(app, path)
            payload["saved"] = "corpus/probe/lagff1.mpp"
            payload["xml_bytes"] = _export_xml(app, xml)
            payload["xml"] = "corpus/probe/lagff1.xml"
            save("lagff-probe", payload)


def cmd_open(args):
    """Open each .mpp ReadOnly and dump what Project's model holds.

    Project recalculates on open; that is the measurement.  No explicit
    CalculateProject here, so the result is "what Project made of the file".
    The files are W26's single-variable writer fixtures (synthetic base.mpp).
    """
    files = args.files
    if not files:
        raise SystemExit("open needs at least one .mpp path")
    runs = []
    with oracle_session() as app:
        with Guard(300):
            for given in files:
                path = os.path.abspath(given)
                entry = {"file": os.path.abspath(path),
                         "bytes": os.path.getsize(path) if os.path.exists(path) else None}
                if not os.path.exists(path):
                    entry["error"] = "missing"
                    runs.append(entry)
                    continue
                try:
                    app.FileOpen(Name=path, ReadOnly=True)
                    proj = app.ActiveProject
                    entry["project_start"] = val(proj.ProjectStart)
                    entry["project_finish"] = val(getattr(proj, "ProjectFinish", None))
                    entry["tasks"] = rows_of(proj)
                    try:
                        entry["project_summary"] = read_task(proj.ProjectSummaryTask)
                    except Exception as exc:         # noqa: BLE001
                        entry["project_summary_error"] = (
                            f"{exc.__class__.__name__}: {exc}")
                except Exception as exc:             # noqa: BLE001
                    entry["error"] = f"{exc.__class__.__name__}: {exc}"
                finally:
                    try:
                        app.FileClose(0)
                    except Exception:                # noqa: BLE001
                        pass
                runs.append(entry)
                log(f"opened {os.path.basename(path)}: {len(entry.get('tasks', []))} rows"
                    + (f" error={entry['error']}" if "error" in entry else ""))
    save("w26-open-" + dt.datetime.now().strftime("%H%M%S"),
         {"case": "open-writer-variants", "runs": runs})
    for run in runs:
        print(os.path.basename(run["file"]))
        summary = run.get("project_summary")
        if summary:
            print(f"  PROJECT start={summary['Start']} finish={summary['Finish']} "
                  f"dur={summary['Duration']} ({summary['DurationText']}) "
                  f"early=({summary['EarlyStart']}, {summary['EarlyFinish']}) "
                  f"late=({summary['LateStart']}, {summary['LateFinish']}) "
                  f"slack={summary['TotalSlack']}")
        for t in run.get("tasks", []):
            print(f"  ID={t['ID']} dur={t['Duration']} ({t['DurationText']}) "
                  f"start={t['Start']} finish={t['Finish']} "
                  f"early=({t['EarlyStart']}, {t['EarlyFinish']}) "
                  f"late=({t['LateStart']}, {t['LateFinish']}) "
                  f"slack={t['TotalSlack']} pct={t['PercentComplete']} "
                  f"prio={t['Priority']} milestone={t['Milestone']}")


def cmd_edit_manual_start(args):
    """Edit a manual task's Start in Project, save, and record what moved.

    The W26 fixtures show Project ignores +96/+100 on load for a manual task and
    reads Fixed2Data +54 (finish), but the +46/+50 start copies did not move the
    reported Start.  This experiment asks Project itself where a manual start is
    written: open base.mpp, set Task.Start, CalculateProject, Save As, then diff
    the streams (the caller does the byte diff).
    """
    src = os.path.join(PROBE, "base.mpp")
    dst = os.path.join(PROBE, "manual-start-edited.mpp")
    with oracle_session() as app:
        with Guard(180):
            app.FileOpen(Name=src)
            proj = app.ActiveProject
            task = proj.Tasks(1)
            before = read_task(task)
            log(f"  before: manual={before.get('Manual')} start={before.get('Start')} "
                f"finish={before.get('Finish')} dur={before.get('Duration')}")
            try:
                task.Manual = True
            except Exception as exc:                 # noqa: BLE001
                log(f"  Manual=True failed: {exc.__class__.__name__}: {exc}")
            task.Start = utc(dt.datetime(2026, 1, 12, 8))
            calc(app)
            after = read_task(task)
            log(f"  after:  manual={after.get('Manual')} start={after.get('Start')} "
                f"finish={after.get('Finish')} dur={after.get('Duration')}")
            if os.path.exists(dst):
                os.remove(dst)
            app.FileSaveAs(Name=dst)
            payload = {"case": "manual-start-edit",
                       "source": "corpus/probe/base.mpp",
                       "saved": "corpus/probe/manual-start-edited.mpp",
                       "saved_bytes": os.path.getsize(dst),
                       "before": before, "after": after}
            log(f"  saved {payload['saved']} ({payload['saved_bytes']:,} B)")
            save("manual-start-edit", payload)


CASES = {"export": cmd_export, "holiday": cmd_holiday,
         "milestone": cmd_milestone, "lagff": cmd_lagff,
         "open": cmd_open, "edit-manual-start": cmd_edit_manual_start}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("case", choices=sorted(CASES))
    ap.add_argument("files", nargs="*", help="for 'open': .mpp paths")
    args = ap.parse_args()
    CASES[args.case](args)


if __name__ == "__main__":
    main()
