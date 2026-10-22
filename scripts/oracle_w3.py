#!/usr/bin/env python3
"""Coordinator-only scheduling oracle experiments (W3-O1, W3-O4, capability probes).

Runs against a live MS Project through scripts/oracle_lock.py. Every subcommand
starts and closes its own Project instance, so a hang in one case cannot take
the others down with it.

    python scripts/oracle_w3.py links     # can COM create dependencies?
    python scripts/oracle_w3.py o1        # calendar arithmetic / finish instants
    python scripts/oracle_w3.py o4        # eight constraint types (needs links)
    python scripts/oracle_w3.py o4xml     # same, via an MSPDI fixture
    python scripts/oracle_w3.py mspdi     # can Project open MSPDI XML?

Results are written to docs/analysis/oracle/<case>.json (committed evidence)
and printed as a table.
"""

import argparse
import datetime as dt
import json
import os
import subprocess
import sys
import threading
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log, project_pids  # noqa: E402

OUT = os.path.join(ROOT, "docs", "analysis", "oracle")
PROBE = os.path.join(ROOT, "corpus", "probe")

READ_TASK = ["ID", "Name", "Duration", "DurationText", "Work", "Start", "Finish",
             "EarlyStart", "EarlyFinish", "LateStart", "LateFinish",
             "StartSlack", "FinishSlack", "FreeSlack", "TotalSlack", "Critical",
             "ConstraintType", "ConstraintDate", "Deadline", "Type",
             "EffortDriven", "Milestone", "Manual", "Summary", "PercentComplete",
             "Priority", "OutlineLevel", "Summary",
             "Predecessors", "Successors"]

RAWS = [(188743709, "Duration"), (188743716, "Finish"), (188743715, "Start"),
        (188743717, "EarlyStart"), (188743718, "EarlyFinish"),
        (188743719, "LateStart"), (188743720, "LateFinish")]


class Guard:
    """Kill WINPROJ if a COM call blocks, so a modal dialog cannot wedge us."""

    def __init__(self, seconds):
        self.seconds = seconds
        self.timer = None

    def __enter__(self):
        self.timer = threading.Timer(self.seconds, self._fire)
        self.timer.daemon = True
        self.timer.start()
        return self

    def __exit__(self, *exc):
        self.timer.cancel()
        return False

    def _fire(self):
        log(f"!! no progress for {self.seconds}s -- killing WINPROJ (likely a modal dialog)")
        for pid in project_pids():
            subprocess.run(["taskkill", "/PID", str(pid), "/F"],
                           capture_output=True, timeout=20)


def utc(d):
    """pywin32 converts a naive datetime to UTC; an aware UTC datetime passes
    through unchanged, so Project receives the wall-clock value we intended."""
    return d if d.tzinfo else d.replace(tzinfo=dt.timezone.utc)


def val(v):
    if isinstance(v, dt.datetime):
        return v.strftime("%Y-%m-%d %H:%M:%S")
    if isinstance(v, (int, float, bool, str)) or v is None:
        return v
    return repr(v)


def read_task(t):
    row = {}
    for name in READ_TASK:
        try:
            row[name] = val(getattr(t, name))
        except Exception as exc:                        # noqa: BLE001
            row[name] = f"<err {exc.__class__.__name__}: {exc}>"
    raw = {}
    for fid, label in RAWS:
        try:
            v = t.GetField(fid)
            raw[label] = {"value": val(v),
                          "hex": hex(int(v)) if isinstance(v, int) else None,
                          "type": type(v).__name__}
        except Exception as exc:                        # noqa: BLE001
            raw[label] = f"<err {exc.__class__.__name__}: {exc}>"
    row["raw"] = raw
    return row


def rows_of(proj):
    rows, i = [], 1
    while True:
        try:
            t = proj.Tasks(i)
        except Exception:                               # noqa: BLE001
            break
        rows.append(read_task(t))
        i += 1
        if i > 300:
            break
    return rows


def show(rows):
    for r in rows:
        print(f"  ID {r['ID']:>3} {str(r['Name'])[:28]:28} "
              f"dur={str(r['Duration']):>7} ({str(r['DurationText']):>8}) "
              f"start={r['Start']} finish={r['Finish']} "
              f"con={r['ConstraintType']} cdate={r['ConstraintDate']} "
              f"crit={r['Critical']} slack={r['TotalSlack']}")
        for k, v in r["raw"].items():
            if isinstance(v, dict) and v.get("value") is not None:
                print(f"        raw {k:10} {v['value']!r:22} {v['hex'] or ''}")


def save(case, payload):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, case + ".json")
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(payload, fh, indent=2, default=str)
    log(f"wrote {os.path.relpath(path, ROOT)}")


def new_project(app, start):
    """start is a datetime; it is sent as an aware UTC value (see utc())."""
    app.FileNew()
    for _ in range(30):
        if app.ActiveProject is not None:
            break
        time.sleep(1)
    proj = app.ActiveProject
    setattr_safe(proj, "ProjectStart", utc(start), "ProjectStart")
    calc(app)
    return proj


def setattr_safe(obj, name, value, label, fallbacks=()):
    for attempt in (value, *fallbacks):
        try:
            setattr(obj, name, attempt)
            return True
        except Exception as exc:                        # noqa: BLE001
            last = exc
    log(f"  ! could not set {label}: {last}")
    return False


def calc(app):
    """Application.Calculate does not exist; CalculateProject does."""
    for name in ("CalculateProject", "CalculateAll"):
        try:
            getattr(app, name)()
            return
        except AttributeError:
            continue
    raise RuntimeError("no calculate method on Application")


def add_task(proj, name, duration, start=None):
    t = proj.Tasks.Add(name)
    setattr_safe(t, "Manual", False, f"{name}.Manual")
    setattr_safe(t, "Duration", duration, f"{name}.Duration")
    if start is not None:
        setattr_safe(t, "Start", utc(start), f"{name}.Start")
    return t


# ---------------------------------------------------------------- capability probes

def cmd_links(args):
    with oracle_session() as app:
        with Guard(120):
            proj = new_project(app, dt.datetime(2026, 10, 19))
            a = add_task(proj, "A", "1d", dt.datetime(2026, 10, 19, 8))
            b = add_task(proj, "B", "1d", dt.datetime(2026, 10, 20, 8))
            calc(app)
            out = {"before": rows_of(proj), "attempts": []}

            def attempt(label, fn):
                try:
                    r = fn()
                    calc(app)
                    out["attempts"].append({"label": label, "ok": True,
                                            "result": val(r)})
                    log(f"  OK   {label} -> {val(r)}")
                except Exception as exc:                # noqa: BLE001
                    out["attempts"].append({"label": label, "ok": False,
                                            "error": f"{exc.__class__.__name__}: {exc}"})
                    log(f"  FAIL {label}: {exc.__class__.__name__}: {exc}")

            attempt("Task.LinkPredecessors(Task1)",
                    lambda: b.LinkPredecessors(a))
            attempt("TaskDependencies.Count(task2)",
                    lambda: b.TaskDependencies.Count)
            attempt("TaskDependencies.Add(Task1)",
                    lambda: b.TaskDependencies.Add(a))
            attempt("TaskDependencies.Add(Task1,1,0)",
                    lambda: b.TaskDependencies.Add(a, 1, 0))
            attempt("Project.TaskDependencies.Add(a,b)",
                    lambda: proj.TaskDependencies.Add(a, b))
            attempt("Application.LinkTasks",
                    lambda: app.LinkTasks(1, 2))
            attempt("Task.Predecessors set '1FS'",
                    lambda: setattr(b, "Predecessors", "1FS"))
            try:
                out["task_members"] = sorted(
                    m for m in dir(b) if "Depend" in m or "Predecess" in m)
                out["project_members"] = sorted(
                    m for m in dir(proj) if "Depend" in m or "Assign" in m)
            except Exception as exc:                    # noqa: BLE001
                out["members_error"] = str(exc)
            calc(app)
            out["after"] = rows_of(proj)
            show(out["after"])
            save("links", out)


# ---------------------------------------------------------------- W3-O1

def cmd_o1(args):
    with oracle_session() as app:
        with Guard(150):
            proj = new_project(app, dt.datetime(2026, 10, 19))
            spec = [("T1", "1d", dt.datetime(2026, 10, 19, 8)),
                    ("T2", "5d", dt.datetime(2026, 10, 20, 8)),
                    ("T3", "4h", dt.datetime(2026, 10, 23, 15)),
                    ("T4", "0d", dt.datetime(2026, 10, 21, 8))]
            for name, dur, start in spec:
                t = add_task(proj, name, dur, start)
                log(f"  added {name} {dur} start {start} -> "
                    f"start={t.Start} finish={t.Finish}")
            calc(app)
            calc(app)
            rows = rows_of(proj)
            show(rows)
            payload = {
                "case": "W3-O1",
                "run_at": dt.datetime.now().isoformat(timespec="seconds"),
                "project_start": val(proj.ProjectStart),
                "hours_per_day": val(getattr(proj, "HoursPerDay", None)),
                "hours_per_week": val(getattr(proj, "HoursPerWeek", None)),
                "days_per_month": val(getattr(proj, "DaysPerMonth", None)),
                "tasks": rows,
            }
            mpp = os.path.join(PROBE, "w3-o1.mpp")
            if os.path.exists(mpp):
                os.remove(mpp)
            try:
                app.FileSaveAs(Name=mpp)
                payload["saved_mpp"] = os.path.relpath(mpp, ROOT)
                payload["saved_bytes"] = os.path.getsize(mpp)
                log(f"  saved {payload['saved_mpp']} ({payload['saved_bytes']:,} B)")
            except Exception as exc:                    # noqa: BLE001
                payload["save_error"] = f"{exc.__class__.__name__}: {exc}"
            save("w3-o1", payload)


# ---------------------------------------------------------------- W3-O4 (COM links)

CONSTRAINTS = {0: "ASAP", 1: "ALAP", 2: "MSO", 3: "MFO",
               4: "SNET", 5: "SNLT", 6: "FNET", 7: "FNLT"}


def build_o4(proj, via_xml=False):
    t1 = add_task(proj, "T1", "1d", dt.datetime(2026, 10, 19, 8))
    t2 = add_task(proj, "T2", "1d", None)
    return t1, t2


def link(t1, t2):
    for label, fn in (
            ("Task.LinkPredecessors(pred)  [Link default = 1]",
             lambda: t2.LinkPredecessors(t1)),
            ("Task.LinkPredecessors(pred, 1, 0)",
             lambda: t2.LinkPredecessors(t1, 1, 0)),
            ("Task.LinkPredecessors('1', 1, 0)",
             lambda: t2.LinkPredecessors(str(t1.ID), 1, 0)),
            ("Task.TaskDependencies.Add(pred, 1, 0)",
             lambda: t2.TaskDependencies.Add(t1, 1, 0)),
    ):
        try:
            fn()
            log(f"  link via {label}")
            return label
        except Exception as exc:                        # noqa: BLE001
            log(f"  link failed via {label}: {exc.__class__.__name__}: {exc}")
    return None




def assignment_rows(t):
    out = []
    try:
        n = t.Assignments.Count
    except Exception:                                    # noqa: BLE001
        return out
    for i in range(1, n + 1):
        a = t.Assignments(i)
        row = {}
        for name in ("TaskID", "ResourceID", "ResourceName", "Units", "Work",
                     "RemainingWork", "ActualWork"):
            try:
                row[name] = val(getattr(a, name))
            except Exception as exc:                     # noqa: BLE001
                row[name] = f"<err {exc.__class__.__name__}>"
        out.append(row)
    return out


def task_triangle(t):
    row = {}
    for name in ("Duration", "DurationText", "Work", "Units", "Type",
                 "EffortDriven", "Start", "Finish", "PercentComplete"):
        try:
            row[name] = val(getattr(t, name))
        except Exception as exc:                         # noqa: BLE001
            row[name] = f"<err {exc.__class__.__name__}>"
    row["assignments"] = assignment_rows(t)
    return row


def cmd_o2(args):
    """W3-O2: the duration/work/units triangle, all nine cells."""
    out = {"case": "W3-O2", "runs": [],
           "run_at": dt.datetime.now().isoformat(timespec="seconds")}
    for task_type, type_name in ((0, "fixed units"), (1, "fixed duration"),
                                 (2, "fixed work")):
        with oracle_session() as app:
            with Guard(150):
                proj = new_project(app, dt.datetime(2026, 10, 19))
                t1 = add_task(proj, "T1", "3d", dt.datetime(2026, 10, 19, 8))
                r1 = proj.Resources.Add("R1")
                setattr_safe(r1, "MaxUnits", 1.0, "R1.MaxUnits")
                t1.Assignments.Add(ResourceID=r1.ID)
                setattr_safe(t1, "Type", task_type, f"T1.Type={task_type}")
                calc(app)
                steps = [("initial 3d", None, None)]
                out["runs"].append({"type": task_type, "type_name": type_name,
                                    "step": "initial", "task": task_triangle(t1)})
                log(f"  {type_name:14} initial      " + summary_line(task_triangle(t1)))
                for step, attr, value in (("Work=4h", "Work", "4h"),
                                          ("Units=50%", "Units", 0.5),
                                          ("Units=200%", "Units", 2.0),
                                          ("Duration=5d", "Duration", "5d")):
                    setattr_safe(t1, attr, value, f"T1.{attr}={value}")
                    calc(app)
                    tri = task_triangle(t1)
                    out["runs"].append({"type": task_type, "type_name": type_name,
                                        "step": step, "task": tri})
                    log(f"  {type_name:14} {step:11} " + summary_line(tri))
                app.FileClose(0)

    # two resources, effort-driven on and off
    for driven in (False, True):
        with oracle_session() as app:
            with Guard(150):
                proj = new_project(app, dt.datetime(2026, 10, 19))
                t1 = add_task(proj, "T1", "3d", dt.datetime(2026, 10, 19, 8))
                r1 = proj.Resources.Add("R1")
                r2 = proj.Resources.Add("R2")
                t1.Assignments.Add(ResourceID=r1.ID)
                t1.Assignments.Add(ResourceID=r2.ID)
                setattr_safe(t1, "EffortDriven", driven, f"T1.EffortDriven={driven}")
                calc(app)
                tri0 = task_triangle(t1)
                out["runs"].append({"effort_driven": driven, "step": "two resources",
                                    "task": tri0})
                log(f"  effort={driven!s:5} two-res      " + summary_line(tri0))
                setattr_safe(t1, "Work", "4h", "T1.Work=4h")
                calc(app)
                tri1 = task_triangle(t1)
                out["runs"].append({"effort_driven": driven, "step": "Work=4h",
                                    "task": tri1})
                log(f"  effort={driven!s:5} Work=4h      " + summary_line(tri1))
                app.FileClose(0)
    save("w3-o2", out)


def summary_line(tri):
    return (f"dur={tri['Duration']} ({tri['DurationText']}) work={tri['Work']} "
            f"units={tri['Units']} start={str(tri['Start'])[:16]} "
            f"finish={str(tri['Finish'])[:16]} "
            f"a={[(x.get('ResourceID'), x.get('Units'), x.get('Work')) for x in tri['assignments']]}")



def cmd_o2b(args):
    """W3-O2b: the assignment Units axis (the part Task.Units does not have)."""
    out = {"case": "W3-O2b", "runs": [],
           "run_at": dt.datetime.now().isoformat(timespec="seconds")}
    for task_type, type_name in ((0, "fixed units"), (1, "fixed duration"),
                                 (2, "fixed work")):
        with oracle_session() as app:
            with Guard(150):
                proj = new_project(app, dt.datetime(2026, 10, 19))
                t1 = add_task(proj, "T1", "3d", dt.datetime(2026, 10, 19, 8))
                r1 = proj.Resources.Add("R1")
                a = t1.Assignments.Add(ResourceID=r1.ID)
                setattr_safe(t1, "Type", task_type, f"T1.Type={task_type}")
                calc(app)
                out["runs"].append({"type": task_type, "type_name": type_name,
                                    "step": "initial", "task": task_triangle(t1),
                                    "assignment": assignment_rows(t1)})
                log(f"  {type_name:14} initial   " + summary_line(task_triangle(t1)))
                for label, units in (("Units=50%", 0.5), ("Units=200%", 2.0),
                                     ("Units=50% again", 0.5)):
                    ok = setattr_safe(a, "Units", units, f"assignment.Units={units}")
                    calc(app)
                    tri = task_triangle(t1)
                    out["runs"].append({"type": task_type, "type_name": type_name,
                                        "step": label, "set_ok": ok,
                                        "task": tri, "assignment": assignment_rows(t1)})
                    log(f"  {type_name:14} {label:15} " + summary_line(tri))
                app.FileClose(0)
    save("w3-o2b", out)



def cmd_o6(args):
    """W3-O6: summary rollup with a gap between children, and outdenting."""
    out = {"case": "W3-O6", "runs": [],
           "run_at": dt.datetime.now().isoformat(timespec="seconds")}
    with oracle_session() as app:
        with Guard(180):
            proj = new_project(app, dt.datetime(2026, 10, 19))
            s1 = add_task(proj, "S1", "2d")
            c1 = add_task(proj, "C1", "2d", dt.datetime(2026, 10, 19, 8))
            c2 = add_task(proj, "C2", "1d", dt.datetime(2026, 10, 26, 8))
            calc(app)
            out["before_outline"] = rows_of(proj)
            try:
                proj.Tasks(2).OutlineIndent()
                proj.Tasks(3).OutlineIndent()
                calc(app)
                rows = rows_of(proj)
                out["runs"].append({"step": "two children under S1", "tasks": rows})
                show_rows_brief(rows, "outlined")
            except Exception as exc:                    # noqa: BLE001
                out["outline_error"] = f"{exc.__class__.__name__}: {exc}"
                log(f"  ! outline failed: {exc}")
            for i in (2, 3):
                try:
                    proj.Tasks(i).OutlineOutdent()
                except Exception as exc:                # noqa: BLE001
                    log(f"  ! outdent {i} failed: {exc}")
            calc(app)
            out["runs"].append({"step": "children outdented", "tasks": rows_of(proj)})
            show_rows_brief(out["runs"][-1]["tasks"], "outdented")
    save("w3-o6", out)


def show_rows_brief(rows, label):
    for r in rows:
        log(f"  {label:10} id={r.get('ID')} lvl={r.get('OutlineLevel')} "
            f"sum={r.get('Summary')} dur={r.get('Duration')} work={r.get('Work')} "
            f"start={str(r.get('Start'))[:16]} finish={str(r.get('Finish'))[:16]} "
            f"slack={r.get('TotalSlack')} crit={r.get('Critical')}")


def cmd_o3(args):
    """Link types and the lag anchor (W3-O3). One Project, FileNew per type."""
    names = {0: "FF", 1: "FS", 2: "SF", 3: "SS"}
    lags = [("0", "0", 0), ("+1d", "1d", 480), ("-1d", "-1d", -480),
            ("+2h", "2h", 120)]
    out = {"case": "W3-O3", "runs": [],
           "run_at": dt.datetime.now().isoformat(timespec="seconds")}
    with oracle_session() as app:
        with Guard(300):
            for link_type in (1, 3, 0, 2):
                proj = new_project(app, dt.datetime(2026, 10, 19))
                t1 = add_task(proj, "T1", "1d", dt.datetime(2026, 10, 19, 8))
                t2 = add_task(proj, "T2", "1d")
                calc(app)
                out.setdefault("baseline", {})[names[link_type]] = rows_of(proj)
                for label, as_text, as_minutes in lags:
                    err = None
                    for lag in (as_text, as_minutes):
                        try:
                            t2.TaskDependencies.Add(t1, link_type, lag)
                            break
                        except Exception as exc:        # noqa: BLE001
                            err = f"{exc.__class__.__name__}: {exc}"
                            lag = None
                    if lag is None:
                        out["runs"].append({"type": link_type, "name": names[link_type],
                                            "lag": label, "error": err})
                        log(f"  {names[link_type]} lag {label}: FAILED {err}")
                        continue
                    calc(app)
                    rows = rows_of(proj)
                    out["runs"].append({"type": link_type, "name": names[link_type],
                                        "lag": label, "lag_value": lag, "tasks": rows})
                    r1, r2 = rows[0], rows[1]
                    log(f"  {names[link_type]} lag {label:4} "
                        f"T1 {r1['Start'][:16]}->{r1['Finish'][:16]}  "
                        f"T2 start={r2['Start'][:16]} finish={r2['Finish'][:16]}")
                    try:
                        t2.UnlinkPredecessors(t1)
                    except Exception as exc:            # noqa: BLE001
                        log(f"  ! unlink failed: {exc}")
                app.FileClose(0)
    save("w3-o3", out)



def cmd_o3b(args):
    """Re-test the two anomalous lag rows in fresh projects (no unlink/re-add)."""
    names = {0: "FF", 1: "FS", 2: "SF", 3: "SS"}
    cases = [(0, "-1d"), (2, "1d"), (0, "0"), (2, "0")]
    out = {"case": "W3-O3b", "runs": [],
           "run_at": dt.datetime.now().isoformat(timespec="seconds")}
    with oracle_session() as app:
        with Guard(240):
            for link_type, lag in cases:
                proj = new_project(app, dt.datetime(2026, 10, 19))
                t1 = add_task(proj, "T1", "1d", dt.datetime(2026, 10, 19, 8))
                t2 = add_task(proj, "T2", "1d")
                try:
                    t2.TaskDependencies.Add(t1, link_type, lag)
                except Exception as exc:                # noqa: BLE001
                    log(f"  {names[link_type]} {lag}: add failed {exc}")
                    out["runs"].append({"type": link_type, "lag": lag,
                                        "error": str(exc)})
                    continue
                calc(app)
                rows = rows_of(proj)
                count = 0
                try:
                    count = t2.TaskDependencies.Count
                except Exception:                       # noqa: BLE001
                    pass
                out["runs"].append({"type": link_type, "name": names[link_type],
                                    "lag": lag, "dependency_count": count,
                                    "tasks": rows})
                r1, r2 = rows[0], rows[1]
                log(f"  {names[link_type]} {lag:4} deps={count} "
                    f"T1 {r1['Start'][:16]}->{r1['Finish'][:16]}  "
                    f"T2 start={r2['Start'][:16]} finish={r2['Finish'][:16]}")
                app.FileClose(0)
    save("w3-o3b", out)


def cmd_o4(args):
    with oracle_session() as app:
        with Guard(180):
            out = {"case": "W3-O4", "runs": [],
                   "run_at": dt.datetime.now().isoformat(timespec="seconds")}
            proj = new_project(app, dt.datetime(2026, 10, 19))
            t1, t2 = build_o4(proj)
            out["link_method"] = link(t1, t2)
            if not out["link_method"]:
                save("w3-o4", out)
                return
            calc(app)
            out["baseline"] = rows_of(proj)
            show(out["baseline"])

            for dur, tag in (("1d", "feasible"), ("10d", "infeasible")):
                setattr_safe(t1, "Duration", dur, "T1.Duration")
                for c in range(8):
                    setattr_safe(t2, "ConstraintType", c, f"T2.ConstraintType={c}")
                    setattr_safe(t2, "ConstraintDate",
                                 utc(dt.datetime(2026, 10, 26, 8)),
                                 "T2.ConstraintDate")
                    calc(app)
                    rows = rows_of(proj)
                    out["runs"].append({"t1_duration": dur, "phase": tag,
                                        "constraint": c,
                                        "constraint_name": CONSTRAINTS[c],
                                        "tasks": rows})
                    r2 = rows[1] if len(rows) > 1 else {}
                    log(f"  {tag:10} {CONSTRAINTS[c]:5} T2 start={r2.get('Start')} "
                        f"finish={r2.get('Finish')} ES={r2.get('EarlyStart')} "
                        f"LS={r2.get('LateStart')} slack={r2.get('TotalSlack')} "
                        f"crit={r2.get('Critical')}")

            # deadlines + critical slack limit, feasible baseline
            setattr_safe(t1, "Duration", "1d", "T1.Duration")
            setattr_safe(t2, "ConstraintType", 0, "T2.ConstraintType=0")
            for deadline in (dt.datetime(2026, 10, 27, 17),
                             dt.datetime(2026, 10, 20, 17)):
                setattr_safe(t2, "Deadline", utc(deadline), "T2.Deadline")
                calc(app)
                rows = rows_of(proj)
                out.setdefault("deadlines", []).append(
                    {"deadline": val(deadline), "tasks": rows})
                r2 = rows[1] if len(rows) > 1 else {}
                log(f"  deadline {deadline:%Y-%m-%d %H:%M} T2 finish={r2.get('Finish')} "
                    f"slack={r2.get('TotalSlack')} crit={r2.get('Critical')}")

            for limit in (0, 2):
                try:
                    app.OptionsSchedule(CriticalSlack=limit)
                except Exception as exc:                # noqa: BLE001
                    log(f"  ! CriticalSlack={limit}: {exc}")
                calc(app)
                out.setdefault("critical_slack_limit", []).append(
                    {"limit": limit, "tasks": rows_of(proj)})
            mpp = os.path.join(PROBE, "w3-o4-last.mpp")
            if os.path.exists(mpp):
                os.remove(mpp)
            try:
                app.FileSaveAs(Name=mpp)
                out["saved_mpp"] = os.path.relpath(mpp, ROOT)
                log(f"  saved {out['saved_mpp']} ({os.path.getsize(mpp):,} B)")
            except Exception as exc:                    # noqa: BLE001
                out["save_error"] = f"{exc.__class__.__name__}: {exc}"
            save("w3-o4", out)


# ---------------------------------------------------------------- MSPDI fixtures

CALENDAR = """  <Calendars>
    <Calendar>
      <UID>3</UID>
      <Name>Standard</Name>
      <IsBaseCalendar>1</IsBaseCalendar>
      <WeekDays>
{weekdays}
      </WeekDays>
    </Calendar>
  </Calendars>
"""

WORKDAYS = (2, 3, 4, 5, 6)      # MSPDI DayType: 1=Sunday .. 7=Saturday


def weekdays_xml():
    out = []
    for day in range(1, 8):
        if day in WORKDAYS:
            out.append(f"""        <WeekDay><DayType>{day}</DayType><DayWorking>1</DayWorking>
          <WorkingTimes>
            <WorkingTime><FromTime>08:00:00</FromTime><ToTime>12:00:00</ToTime></WorkingTime>
            <WorkingTime><FromTime>13:00:00</FromTime><ToTime>17:00:00</ToTime></WorkingTime>
          </WorkingTimes>
        </WeekDay>""")
        else:
            out.append(f"        <WeekDay><DayType>{day}</DayType>"
                       f"<DayWorking>0</DayWorking></WeekDay>")
    return "\n".join(out)


def task_xml(uid, name, duration, start, finish, extra=""):
    return f"""    <Task>
      <UID>{uid}</UID>
      <ID>{uid}</ID>
      <Name>{name}</Name>
      <Type>1</Type>
      <Duration>{duration}</Duration>
      <Start>{start}</Start>
      <Finish>{finish}</Finish>
      <Milestone>0</Milestone>
      <Summary>0</Summary>{extra}
    </Task>"""


def write_mspdi(path, tasks, links=(), pre=""):
    body = "\n".join(tasks)
    link_xml = ""
    for succ, pred, typ, lag in links:
        link_xml += f"""      <PredecessorLink>
        <PredecessorUID>{pred}</PredecessorUID>
        <Type>{typ}</Type>
        <LinkLag>{lag}</LinkLag>
        <LagFormat>7</LagFormat>
      </PredecessorLink>
"""
    doc = f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Project xmlns="http://schemas.microsoft.com/project">
  <Name>W3 fixture</Name>
  <Title>W3 fixture</Title>
  <StartDate>2026-10-19T08:00:00</StartDate>
  <ScheduleFromStart>1</ScheduleFromStart>
  <MinutesPerDay>480</MinutesPerDay>
  <MinutesPerWeek>2400</MinutesPerWeek>
  <DaysPerMonth>20</DaysPerMonth>
  <CalendarUID>3</CalendarUID>
{CALENDAR.format(weekdays=weekdays_xml())}
  <Tasks>
{body}
  </Tasks>
</Project>
"""
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(doc)
    return path


def cmd_mspdi(args):
    path = os.path.join(PROBE, "mspdi-min.mpp.xml")
    write_mspdi(path, [task_xml(1, "A", "PT8H0M0S", "2026-10-19T08:00:00",
                                "2026-10-19T17:00:00")])
    with oracle_session() as app:
        with Guard(150):
            info = {"file": os.path.relpath(path, ROOT), "attempts": []}
            for label, fn in (
                    ("FileOpen(path)", lambda: app.FileOpen(path)),
                    ("FileOpenEx(path)", lambda: app.FileOpenEx(path)),
                    ("FileOpen(Name=path,FormatID=MSProject.XML)",
                     lambda: app.FileOpen(Name=path, FormatID="MSProject.XML")),
            ):
                try:
                    fn()
                    proj = app.ActiveProject
                    info["attempts"].append({"label": label, "ok": True,
                                             "name": val(getattr(proj, "Name", None))})
                    log(f"  OK   {label}")
                    break
                except Exception as exc:                # noqa: BLE001
                    info["attempts"].append({"label": label, "ok": False,
                                             "error": f"{exc.__class__.__name__}: {exc}"})
                    log(f"  FAIL {label}: {exc.__class__.__name__}: {exc}")
            try:
                proj = app.ActiveProject
                info["project_start"] = val(getattr(proj, "ProjectStart", None))
                info["hours_per_day"] = val(getattr(proj, "HoursPerDay", None))
                info["tasks"] = rows_of(proj)
                show(info["tasks"])
            except Exception as exc:                    # noqa: BLE001
                info["read_error"] = f"{exc.__class__.__name__}: {exc}"
            save("mspdi", info)


def cmd_o4xml(args):
    """Constraints without COM links: build MSPDI, open, read, then Save As .mpp."""
    xml = os.path.join(PROBE, "w3-o4.xml")
    tasks = [task_xml(1, "T1", "PT8H0M0S", "2026-10-19T08:00:00",
                      "2026-10-19T17:00:00",
                      "<ConstraintType>4</ConstraintType>"
                      "<ConstraintDate>2026-10-19T08:00:00</ConstraintDate>"),
             task_xml(2, "T2", "PT8H0M0S", "2026-10-20T08:00:00",
                      "2026-10-20T17:00:00")]
    # splice the predecessor into T2
    tasks[1] = tasks[1].replace(
        "      <Milestone>0</Milestone>",
        "      <PredecessorLink>\n        <PredecessorUID>1</PredecessorUID>"
        "\n        <Type>1</Type>\n        <LinkLag>0</LinkLag>\n"
        "        <LagFormat>7</LagFormat>\n      </PredecessorLink>"
        "\n      <Milestone>0</Milestone>")
    write_mspdi(xml, tasks)
    with oracle_session() as app:
        with Guard(180):
            out = {"case": "W3-O4xml", "file": os.path.relpath(xml, ROOT),
                   "runs": [], "run_at": dt.datetime.now().isoformat(timespec="seconds")}
            app.FileOpen(xml)
            proj = app.ActiveProject
            calc(app)
            out["baseline"] = rows_of(proj)
            show(out["baseline"])
            for c in range(8):
                t2 = proj.Tasks(2)
                setattr_safe(t2, "ConstraintType", c, f"T2.ConstraintType={c}")
                setattr_safe(t2, "ConstraintDate",
                             utc(dt.datetime(2026, 10, 26, 8)),
                             "T2.ConstraintDate")
                calc(app)
                rows = rows_of(proj)
                out["runs"].append({"constraint": c,
                                    "constraint_name": CONSTRAINTS[c],
                                    "tasks": rows})
                r2 = rows[1] if len(rows) > 1 else {}
                log(f"  {CONSTRAINTS[c]:5} T2 start={r2.get('Start')} "
                    f"finish={r2.get('Finish')} ES={r2.get('EarlyStart')} "
                    f"LS={r2.get('LateStart')} slack={r2.get('TotalSlack')} "
                    f"crit={r2.get('Critical')}")
            mpp = os.path.join(PROBE, "w3-o4-last.mpp")
            try:
                app.FileSaveAs(Name=mpp)
                out["saved_mpp"] = os.path.relpath(mpp, ROOT)
                log(f"  saved {out['saved_mpp']} ({os.path.getsize(mpp):,} B)")
            except Exception as exc:                    # noqa: BLE001
                out["save_error"] = f"{exc.__class__.__name__}: {exc}"
            save("w3-o4xml", out)



def cmd_o4b(args):
    """Deadlines with negative slack, the ASAP+ConstraintDate side effect, and
    what CriticalSlack actually compares against."""
    with oracle_session() as app:
        with Guard(300):
            out = {"case": "W3-O4b",
                   "run_at": dt.datetime.now().isoformat(timespec="seconds")}

            # A. deadline strictly earlier than the computed finish
            proj = new_project(app, dt.datetime(2026, 10, 19))
            t1 = add_task(proj, "T1", "1d", dt.datetime(2026, 10, 19, 8))
            t2 = add_task(proj, "T2", "1d")
            out["link_method"] = link(t1, t2)
            calc(app)
            out["deadline_baseline"] = rows_of(proj)
            for dl in ("2026-10-20 08:00", "2026-10-19 17:00",
                       "2026-10-21 17:00"):
                setattr_safe(t2, "Deadline",
                             utc(dt.datetime.strptime(dl, "%Y-%m-%d %H:%M")),
                             "T2.Deadline")
                calc(app)
                rows = rows_of(proj)
                out.setdefault("deadlines", []).append(
                    {"deadline": dl, "tasks": rows})
                r2 = rows[1]
                log(f"  deadline {dl} T2 start={r2['Start']} finish={r2['Finish']} "
                    f"slack={r2['TotalSlack']} free={r2['FreeSlack']} "
                    f"crit={r2['Critical']}")

            # B. does assigning ConstraintDate to an ASAP task change its type?
            proj = new_project(app, dt.datetime(2026, 10, 19))
            t = add_task(proj, "S1", "1d", dt.datetime(2026, 10, 19, 8))
            calc(app)
            before = read_task(t)
            setattr_safe(t, "ConstraintType", 0, "S1.ConstraintType=0")
            calc(app)
            typed = read_task(t)
            setattr_safe(t, "ConstraintDate",
                         utc(dt.datetime(2026, 10, 26, 8)), "S1.ConstraintDate")
            calc(app)
            after = read_task(t)
            out["asap_constraintdate_probe"] = {
                "before": before, "after_type_set": typed, "after_date_set": after}
            log(f"  ASAP probe: type {before['ConstraintType']} -> "
                f"{typed['ConstraintType']} -> {after['ConstraintType']}  "
                f"start {before['Start']} -> {typed['Start']} -> {after['Start']}")

            # C. CriticalSlack limit against a task with exactly 1 day of slack
            proj = new_project(app, dt.datetime(2026, 10, 19))
            add_task(proj, "A", "1d", dt.datetime(2026, 10, 19, 8))
            add_task(proj, "B", "1d", dt.datetime(2026, 10, 20, 8))
            calc(app)
            out["slack_baseline"] = rows_of(proj)
            for limit in (0, 1, 2):
                try:
                    app.OptionsSchedule(CriticalSlack=limit)
                except Exception as exc:                # noqa: BLE001
                    log(f"  ! CriticalSlack={limit}: {exc}")
                calc(app)
                rows = rows_of(proj)
                out.setdefault("critical_slack_limit", []).append(
                    {"limit": limit, "tasks": rows})
                log(f"  CriticalSlack={limit}: A slack={rows[0]['TotalSlack']} "
                    f"crit={rows[0]['Critical']}  "
                    f"B slack={rows[1]['TotalSlack']} crit={rows[1]['Critical']}")

            save("w3-o4b", out)


CASES = {"links": cmd_links, "o1": cmd_o1, "o2": cmd_o2, "o2b": cmd_o2b,
         "o3": cmd_o3, "o3b": cmd_o3b, "o6": cmd_o6,
         "o4": cmd_o4, "o4b": cmd_o4b,
         "mspdi": cmd_mspdi, "o4xml": cmd_o4xml}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("case", choices=sorted(CASES))
    args = ap.parse_args()
    try:
        CASES[args.case](args)
    except SystemExit:
        raise
    except Exception:                                   # noqa: BLE001
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
