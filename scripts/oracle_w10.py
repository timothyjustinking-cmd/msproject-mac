#!/usr/bin/env python3
"""Wave-10 coordinator oracle: the four measurements the board requires.

    python scripts/oracle_w10.py adddelete
        Saves a 5-task project, adds one task, saves, deletes it again, saves,
        then deletes a middle task and saves.  Four files in corpus/probe/ that
        diff stream-by-stream with scripts/mpp_diff.py.  This is W45's design.

    python scripts/oracle_w10.py formulas
        Six custom fields with formulas (Number1 = [Duration]*2,
        Number2 = [Duration]/480, Date1 = [Start]+7, Flag1 = IIF,
        Text1 = concat, Duration1 = [Duration]/2), read back with GetField,
        saved, exported to MSPDI, and the file's VarMeta entries for those field
        ids dumped.  This is W44's ground truth.

    python scripts/oracle_w10.py usage
        Three assignments: flat, back-loaded contour, and one with actual work.
        TimeScaleData daily buckets are recorded, then each state is saved so the
        files can be diffed: are the buckets stored or computed?  W43's question.

    python scripts/oracle_w10.py prefs
        Table date format changed through the one COM setter that exists,
        saved, and 114/Props diffed.  Introspection of the options/timescale
        members that do *not* expose a setter.  W46's persistence question.

    python scripts/oracle_w10.py analyze <file.mpp>
        Offline: VarMeta entries for the custom-field ids, plus a UTF-16/ASCII
        scan for formula text.  Safe on synthetic files; prints no task names.

Rules (docs/coordination.md): no `/automation`, `CalculateProject` not
`Calculate`, `FileClose(0)`, aware datetimes, absolute SaveAs paths, fixtures
only under corpus/probe/.  Committed JSON keeps numbers/dates/ids only.
"""

import datetime as dt
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import log, oracle_session                       # noqa: E402
from oracle_privacy import neutralise_active  # noqa: E402
from oracle_w3 import (Guard, add_task, calc, new_project,        # noqa: E402
                       rows_of, save, utc, val)

PROBE = os.path.join(ROOT, "corpus", "probe")
START = dt.datetime(2026, 10, 19, 8)
FS = 1

# PjField ids of the six custom fields (analysis/field_id_map.tsv, Task).
CUSTOM = [
    ("Number1", 0x0B400057, "[Duration]*2"),
    ("Number2", 0x0B400058, "[Duration]/480"),
    ("Date1", 0x0B400109, "[Start]+7"),
    ("Flag1", 0x0B400048, "IIf([% Complete]>50,1,0)"),
    ("Text1", 0x0B400033, '[Name] & "-" & [Duration]'),
    ("Duration1", 0x0B400067, "[Duration]/2"),
]


def save_as(app, path):
    if os.path.exists(path):
        os.remove(path)
    neutralise_active(app)
    app.FileSaveAs(Name=path)
    return os.path.getsize(path)


def task_brief(proj):
    """(ID, UniqueID, Start, Finish, Duration) -- never a name."""
    out = []
    for i in range(1, 200):
        try:
            t = proj.Tasks(i)
        except Exception:                                    # noqa: BLE001
            break
        out.append({"ID": val(t.ID), "uid": val(t.UniqueID),
                    "start": val(t.Start), "finish": val(t.Finish),
                    "duration": val(t.Duration),
                    "manual": val(t.Manual), "summary": val(t.Summary)})
    return out


def assign_task(t, r):
    """Create one assignment and return it.

    Measured 2026-10-22 on this build: `Task.Assignments.Add(ResourceID=...)`
    alone is rejected ("The argument value is not valid"); the accepted form is
    `Add(TaskID=..., ResourceID=...)`, and its return value is not a usable
    Assignment object, so the collection is re-read afterwards.
    """
    try:
        t.Assignments.Add(TaskID=t.ID, ResourceID=r.ID)
    except Exception:                                        # noqa: BLE001
        t.Assignments.Add(ResourceID=r.ID)
    return t.Assignments(1)


# ---------------------------------------------------------------- add / delete


def adddelete():
    base = os.path.join(PROBE, "w10-adddel-base.mpp")
    added = os.path.join(PROBE, "w10-adddel-add.mpp")
    back = os.path.join(PROBE, "w10-adddel-back.mpp")
    deleted = os.path.join(PROBE, "w10-adddel-del.mpp")
    sizes, stages = {}, {}
    with oracle_session() as app:
        with Guard(420):
            proj = new_project(app, START)
            tasks = [add_task(proj, f"T{i}", "2d") for i in range(1, 6)]
            tasks[1].TaskDependencies.Add(tasks[0], FS, "0")
            res = proj.Resources.Add("R1")
            res.Type = 0
            res.MaxUnits = 1.0
            calc(app)                     # Assignments.Add needs a live calc
            assign_task(tasks[1], res)
            calc(app)
            sizes["base"] = save_as(app, base)
            stages["base"] = task_brief(proj)

            t6 = add_task(proj, "T6", "2d")
            calc(app)
            sizes["add"] = save_as(app, added)
            stages["add"] = task_brief(proj)

            t6.Delete()
            calc(app)
            sizes["back"] = save_as(app, back)
            stages["back"] = task_brief(proj)

            for i in range(1, proj.Tasks.Count + 1):
                if proj.Tasks(i).Name == "T4":
                    proj.Tasks(i).Delete()
                    break
            calc(app)
            sizes["del"] = save_as(app, deleted)
            stages["del"] = task_brief(proj)

            app.FileClose(0)
            reopen = {}
            for label, path in (("base", base), ("add", added),
                                ("back", back), ("del", deleted)):
                entry = {"bytes": os.path.getsize(path)}
                try:
                    app.FileOpen(Name=path, ReadOnly=True)
                    entry["tasks"] = task_brief(app.ActiveProject)
                except Exception as exc:                     # noqa: BLE001
                    entry["error"] = f"{exc.__class__.__name__}: {exc}"
                finally:
                    try:
                        app.FileClose(0)
                    except Exception:                        # noqa: BLE001
                        pass
                reopen[label] = entry
                log(f"reopened {label}: {len(entry.get('tasks', []))} rows")

    payload = {"case": "add-delete",
               "run_at": dt.datetime.now().isoformat(timespec="seconds"),
               "sizes": sizes, "stages": stages, "reopen": reopen,
               "fixtures": {k: os.path.relpath(p, ROOT) for k, p in
                            (("base", base), ("add", added),
                             ("back", back), ("del", deleted))}}
    save("w10-adddelete", payload)
    print("sizes:", sizes)
    for label, rows in stages.items():
        print(f"  {label}: uids={[r['uid'] for r in rows]}")
    print("reopen counts:",
          {k: len(v.get("tasks", [])) for k, v in reopen.items()})
    print("next: python scripts/mpp_diff.py "
          "corpus/probe/w10-adddel-base.mpp corpus/probe/w10-adddel-add.mpp")


# ------------------------------------------------------------------- formulas


def read_custom(t):
    out = {}
    for name, fid, _formula in CUSTOM:
        rec = {}
        try:
            rec["getfield"] = val(t.GetField(fid))
            rec["getfield_type"] = type(t.GetField(fid)).__name__
        except Exception as exc:                             # noqa: BLE001
            rec["getfield"] = f"<err {exc.__class__.__name__}: {exc}>"
        try:
            rec["attr"] = val(getattr(t, name))
        except Exception as exc:                             # noqa: BLE001
            rec["attr"] = f"<err {exc.__class__.__name__}: {exc}>"
        out[name] = rec
    return out


def formulas():
    mpp = os.path.join(PROBE, "w10-formulas.mpp")
    xml = os.path.join(PROBE, "w10-formulas.xml")
    with oracle_session() as app:
        with Guard(420):
            proj = new_project(app, START)
            tasks = [add_task(proj, "T1", "2d"),
                     add_task(proj, "T2", "5d"),
                     add_task(proj, "T3", "1d")]
            calc(app)
            tasks[0].PercentComplete = 25
            tasks[1].PercentComplete = 75
            calc(app)

            setlog, getformula = {}, {}
            for name, fid, formula in CUSTOM:
                accepted = None
                for attempt in (formula, formula.replace(",", ";")):
                    try:
                        app.CustomFieldSetFormula(fid, attempt)
                        accepted = attempt
                        break
                    except Exception as exc:                 # noqa: BLE001
                        last = f"{exc.__class__.__name__}: {exc}"
                setlog[name] = {"wanted": formula, "accepted": accepted,
                                **({"error": last} if accepted is None else {})}
                try:
                    getformula[name] = val(app.CustomFieldGetFormula(fid))
                except Exception as exc:                     # noqa: BLE001
                    getformula[name] = f"<err {exc.__class__.__name__}: {exc}>"

            calc(app)
            rows = []
            for t in tasks:
                rows.append({"ID": val(t.ID), "uid": val(t.UniqueID),
                             "duration": val(t.Duration),
                             "start": val(t.Start), "finish": val(t.Finish),
                             "percent": val(t.PercentComplete),
                             "work": val(t.Work),
                             "custom": read_custom(t)})

            sizes = {"mpp": save_as(app, mpp)}
            if os.path.exists(xml):
                os.remove(xml)
            neutralise_active(app)
            app.FileSaveAs(Name=xml, FormatID="MSProject.xml")
            sizes["xml"] = os.path.getsize(xml)

    payload = {"case": "formulas", "run_at": dt.datetime.now().isoformat(
                   timespec="seconds"),
               "set": setlog, "get_formula": getformula, "tasks": rows,
               "sizes": sizes,
               "fields": [{"name": n, "field_id": f"0x{fid:08x}",
                           "formula": fm} for n, fid, fm in CUSTOM]}
    save("w10-formulas", payload)
    print(json.dumps({"set": setlog, "get": getformula}, indent=2))
    for row in rows:
        print(f"  T{row['ID']} dur={row['duration']} pct={row['percent']} "
              f"start={row['start']}")
        for name, rec in row["custom"].items():
            print(f"    {name}: getfield={rec.get('getfield')!r} "
                  f"attr={rec.get('attr')!r}")
    print(f"saved {sizes['mpp']:,} B + xml {sizes['xml']:,} B")


# ---------------------------------------------------------------------- usage


def timescale(a, start, days, dtype):
    """Daily TimeScaleData buckets for one assignment (work or actual work)."""
    try:
        vals = a.TimeScaleData(utc(start),
                               utc(start + dt.timedelta(days=days)),
                               dtype, 4, days)
        out = []
        for i in range(1, vals.Count + 1):
            v = vals.Item(i)
            out.append({"start": val(v.StartDate), "end": val(v.EndDate),
                        "value": val(v.Value)})
        return out
    except Exception as exc:                                 # noqa: BLE001
        return f"<err {exc.__class__.__name__}: {exc}>"


def assign_brief(a):
    rec = {}
    for name in ("ID", "TaskID", "ResourceID", "TaskUniqueID",
                 "ResourceUniqueID", "Units", "Work", "ActualWork",
                 "RemainingWork", "Start", "Finish", "WorkContour"):
        try:
            rec[name] = val(getattr(a, name))
        except Exception as exc:                             # noqa: BLE001
            rec[name] = f"<err {exc.__class__.__name__}: {exc}>"
    return rec


def usage():
    paths = {k: os.path.join(PROBE, f"w10-usage-{k}.mpp")
             for k in ("flat", "contour", "actual")}
    sizes = {}
    with oracle_session() as app:
        with Guard(420):
            proj = new_project(app, START)
            tasks = [add_task(proj, f"T{i}", "3d") for i in (1, 2, 3)]
            res = []
            for i in (1, 2, 3):
                r = proj.Resources.Add(f"R{i}")
                r.Type = 0
                res.append(r)
            calc(app)                     # Assignments.Add needs a live calc
            a1 = assign_task(tasks[0], res[0])
            calc(app)
            a1_work = timescale(a1, START, 3, 8)
            a1_actual = timescale(a1, START, 3, 10)
            sizes["flat"] = save_as(app, paths["flat"])

            a2 = assign_task(tasks[1], res[1])
            try:
                a2.WorkContour = 1
            except Exception as exc:                         # noqa: BLE001
                log(f"WorkContour failed: {exc.__class__.__name__}: {exc}")
            calc(app)
            a2_work = timescale(a2, START, 3, 8)
            sizes["contour"] = save_as(app, paths["contour"])

            a3 = assign_task(tasks[2], res[2])
            actual_note = None
            try:
                a3.ActualStart = utc(START)
                a3.ActualWork = "8h"
            except Exception as exc:                         # noqa: BLE001
                actual_note = f"{exc.__class__.__name__}: {exc}"
                log(f"actual work failed: {actual_note}")
            calc(app)
            a3_work = timescale(a3, START, 3, 8)
            a3_actual = timescale(a3, START, 3, 10)
            sizes["actual"] = save_as(app, paths["actual"])

    payload = {"case": "usage",
               "run_at": dt.datetime.now().isoformat(timespec="seconds"),
               "sizes": sizes,
               "flat": {"assignment": assign_brief(a1),
                        "work": a1_work, "actual": a1_actual},
               "contour": {"assignment": assign_brief(a2),
                           "work": a2_work},
               "actual": {"assignment": assign_brief(a3),
                          "work": a3_work, "actual": a3_actual,
                          "note": actual_note},
               "fixtures": {k: os.path.relpath(p, ROOT)
                            for k, p in paths.items()}}
    save("w10-usage", payload)
    for key in ("flat", "contour", "actual"):
        block = payload[key]
        print(f"  {key}: {block['assignment']}")
        print(f"    work={block['work']}")
        if "actual" in block:
            print(f"    actual={block['actual']}")
    print("sizes:", sizes)


# ---------------------------------------------------------------------- prefs


def prefs():
    base = os.path.join(PROBE, "w10-prefs-base.mpp")
    col = os.path.join(PROBE, "w10-prefs-col.mpp")
    dated = os.path.join(PROBE, "w10-prefs-date.mpp")
    with oracle_session() as app:
        with Guard(420):
            proj = new_project(app, START)
            add_task(proj, "T1", "2d")
            add_task(proj, "T2", "3d")
            calc(app)
            sizes = {"base": save_as(app, base)}

            # dir() on a pywin32 COM object raises "Not implemented" on this
            # build, so probe a static list of candidate members instead.
            members = {}
            for name in ("Tables", "Views", "OptionsView", "OptionsGeneral",
                         "OptionsSchedule", "OptionsPreferences",
                         "OptionsCalculation", "Timescale", "DateFormat",
                         "DurationFormat", "TableApply", "TableEdit",
                         "ViewApply", "ViewsSingle"):
                try:
                    members[name] = type(getattr(app, name)).__name__
                except Exception as exc:                     # noqa: BLE001
                    members[name] = f"<err {exc.__class__.__name__}: {exc}>"

            attempts = []
            try:
                app.TableApply("Entry")
                attempts.append({"TableApply(Entry)": "ok"})
            except Exception as exc:                         # noqa: BLE001
                attempts.append({"TableApply(Entry)":
                                 f"{exc.__class__.__name__}: {exc}"})

            # 1. add a column to the Entry table (structure change).
            try:
                ok = app.TableEdit("Entry", True, False, False, None,
                                   "Number1", "Number1", "Number1",
                                   10, 0, False, True)
                attempts.append({"add Number1": f"ok ({ok!r})"})
            except Exception as exc:                         # noqa: BLE001
                attempts.append({"add Number1":
                                 f"{exc.__class__.__name__}: {exc}"})
            calc(app)
            sizes["col"] = save_as(app, col)

            # 2. change that table's date format.  The TableEdit DateFormat
            # argument is the only exposed setter (Table.DateFormat needs a
            # Table object this build will not hand out).
            try:
                ok = app.TableEdit("Entry", True, False, True, None,
                                   "Number1", "Number1", "Number1",
                                   10, 0, False, True, 5)
                attempts.append({"date format 5": f"ok ({ok!r})"})
            except Exception as exc:                         # noqa: BLE001
                attempts.append({"date format 5":
                                 f"{exc.__class__.__name__}: {exc}"})
            calc(app)
            sizes["date"] = save_as(app, dated)

    payload = {"case": "prefs",
               "run_at": dt.datetime.now().isoformat(timespec="seconds"),
               "sizes": sizes,
               "members": members,
               "attempts": attempts,
               "fixtures": {"base": os.path.relpath(base, ROOT),
                            "col": os.path.relpath(col, ROOT),
                            "date": os.path.relpath(dated, ROOT)}}
    save("w10-prefs", payload)
    print("members:", json.dumps(members, indent=2))
    print("attempts:", json.dumps(attempts, indent=2))
    print("sizes:", sizes)


# ---------------------------------------------------------------- field values


def fieldvalues():
    """Store a value directly in each custom field (no formula) and save.

    Project does not compute formula results into GetField in a headless COM
    session (measured: CustomFieldSetFormula returns True, a literal `1+1`
    still reads 0, and the saved file has no custom-field VarMeta entries).
    Direct values *are* stored, so this fixture is the storage-key ground truth:
    encode -> save -> dump the VarMeta block (the `analyze` case).
    """
    path = os.path.join(PROBE, "w10-fieldvalues.mpp")
    with oracle_session() as app:
        with Guard(300):
            proj = new_project(app, START)
            tasks = [add_task(proj, "T1", "2d"), add_task(proj, "T2", "5d")]
            calc(app)
            attempts = {}
            for name, fid, value in [
                    ("Number1", 0x0B400057, 42.5),
                    ("Number2", 0x0B400058, 3.25),
                    ("Date1", 0x0B400109, utc(dt.datetime(2026, 10, 26, 8))),
                    ("Flag1", 0x0B400048, "Yes"),
                    ("Text1", 0x0B400033, "alpha"),
                    ("Duration1", 0x0B400067, "1d")]:
                try:
                    tasks[0].SetField(fid, value)
                    attempts[name] = "SetField ok"
                except Exception as exc:                     # noqa: BLE001
                    attempts[name] = f"SetField {exc.__class__.__name__}: {exc}"
                    try:
                        setattr(tasks[0], name, value)
                        attempts[name] += " / attr ok"
                    except Exception as exc2:                # noqa: BLE001
                        attempts[name] += (f" / attr {exc2.__class__.__name__}:"
                                           f" {exc2}")
            calc(app)
            read = []
            for t in tasks:
                read.append({
                    "uid": val(t.UniqueID),
                    "getfield": {name: val(t.GetField(fid)) for name, fid in
                                 (("Number1", 0x0B400057),
                                  ("Number2", 0x0B400058),
                                  ("Date1", 0x0B400109),
                                  ("Flag1", 0x0B400048),
                                  ("Text1", 0x0B400033),
                                  ("Duration1", 0x0B400067))},
                    "attr": {name: val(getattr(t, name)) for name in
                             ("Number1", "Number2", "Date1", "Flag1",
                              "Text1", "Duration1")}})
            sizes = {"mpp": save_as(app, path)}

    payload = {"case": "field-values", "attempts": attempts, "read": read,
               "sizes": sizes,
               "fixture": os.path.relpath(path, ROOT),
               "fields": [{"name": n, "field_id": f"0x{f:08x}", "value": v}
                          for n, f, v in [
                              ("Number1", 0x0B400057, 42.5),
                              ("Number2", 0x0B400058, 3.25),
                              ("Date1", 0x0B400109, "2026-10-26T08:00"),
                              ("Flag1", 0x0B400048, "Yes"),
                              ("Text1", 0x0B400033, "alpha"),
                              ("Duration1", 0x0B400067, "1d")]]}
    save("w10-fieldvalues", payload)
    print(json.dumps({"attempts": attempts, "read": read}, indent=2))
    print(f"saved {sizes['mpp']:,} B")


# ------------------------------------------------------------------- analysis


def analyze(path):
    from mpp_diff import load

    streams = load(path)
    print(f"== {os.path.basename(path)}: {len(streams)} streams")
    for key in sorted(streams):
        print(f"  {len(streams[key]):>9,}  {key}")

    meta = streams.get("114/TBkndTask/VarMeta")
    var2 = streams.get("114/TBkndTask/Var2Data")
    if meta and var2:
        # 24-byte header + (uid, offset, key) triples; the stream length is
        # authoritative (core/mppwrite/table.cpp; probe_lib's 32-byte reading
        # is the pre-W21 one).
        n = (len(meta) - 24) // 12
        header = struct.unpack_from("<6I", meta, 0)
        entries = [dict(zip(("mid", "offset", "field_id"),
                            struct.unpack_from("<3I", meta, 24 + i * 12)))
                   for i in range(n)]
        print(f"VarMeta header {header}; entries {n}")
        fids = {fid: name for name, fid, _ in CUSTOM}
        for e in entries:
            if e["field_id"] not in fids:
                continue
            off = e["offset"]
            length = struct.unpack_from("<I", var2, off)[0]
            payload = var2[off + 4:off + 4 + length]
            guesses = {"hex": payload.hex(" ")[:64], "len": len(payload)}
            if len(payload) == 4:
                guesses["u32"] = struct.unpack_from("<I", payload)[0]
                guesses["i32"] = struct.unpack_from("<i", payload)[0]
                d = struct.unpack_from("<I", payload)[0]
                day, sec = d >> 16, (d & 0xFFFF) * 6
                if 15000 < day < 17000:
                    epoch = dt.date(1983, 12, 31) + dt.timedelta(days=day)
                    guesses["date"] = (f"{epoch.isoformat()} "
                                       f"{sec // 3600:02d}:{sec % 3600 // 60:02d}")
            elif len(payload) == 8:
                guesses["double"] = struct.unpack_from("<d", payload)[0]
            else:
                try:
                    text = payload.decode("utf-16le").rstrip("\x00")
                    if text.isprintable():
                        guesses["text"] = text
                except Exception:                            # noqa: BLE001
                    pass
            print(f"  {fids[e['field_id']]:9} uid={e['mid']} "
                  f"off={e['offset']} {guesses}")

    needles = ["[Duration]", "[Start]", "IIf", "CustomField", "Duration1",
               "Number1"]
    for key in sorted(streams):
        data = streams[key]
        for needle in needles:
            for enc, pat in (("u16", needle.encode("utf-16le")),
                             ("ascii", needle.encode("ascii", "ignore"))):
                start = 0
                hits = 0
                while hits < 3:
                    i = data.find(pat, start)
                    if i < 0:
                        break
                    print(f"  string {needle!r} ({enc}) in {key} at +{i}")
                    hits += 1
                    start = i + 1

    xml = os.path.splitext(path)[0] + ".xml"
    if os.path.exists(xml):
        import xml.etree.ElementTree as ET
        with open(xml, "rb") as fh:
            head = fh.read(8)
        if not head.startswith(b"<?xml"):
            print(f"== {os.path.basename(xml)} is not XML (head {head!r}); "
                  f"FileSaveAs fell back to a binary .mpp")
        else:
            root = ET.parse(xml).getroot()
            print(f"== MSPDI extended attributes ({os.path.basename(xml)})")
            for task in root.iter("Task"):
                uid = task.findtext("UID")
                for ea in task.findall("ExtendedAttribute"):
                    fid = ea.findtext("FieldID")
                    if fid in {str(f) for _, f, _ in CUSTOM}:
                        print(f"  uid {uid} field {fid}: "
                              f"value={ea.findtext('Value')!r} "
                              f"duration={ea.findtext('Duration')!r} "
                              f"date={ea.findtext('Date')!r}")


def flagdiff():
    """Two files differing only in one task's Flag1, to locate flag storage.

    Flag1 was not in VarMeta when set through SetField; the fixed tables are the
    remaining candidate.  Save one task with Flag1=False, then True, and diff.
    """
    off = os.path.join(PROBE, "w10-flag-off.mpp")
    on = os.path.join(PROBE, "w10-flag-on.mpp")
    with oracle_session() as app:
        with Guard(300):
            proj = new_project(app, START)
            t = add_task(proj, "T1", "2d")
            calc(app)
            t.Flag1 = False
            calc(app)
            sizes = {"off": save_as(app, off)}
            t.Flag1 = True
            calc(app)
            sizes["on"] = save_as(app, on)
            payload = {"case": "flag-diff", "sizes": sizes,
                       "getfield": val(t.GetField(0x0B400048)),
                       "attr": val(t.Flag1),
                       "fixtures": {"off": os.path.relpath(off, ROOT),
                                    "on": os.path.relpath(on, ROOT)}}
            save("w10-flagdiff", payload)
    print(json.dumps(payload, indent=2))


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    case = sys.argv[1]
    if case == "adddelete":
        adddelete()
    elif case == "formulas":
        formulas()
    elif case == "usage":
        usage()
    elif case == "prefs":
        prefs()
    elif case == "fieldvalues":
        fieldvalues()
    elif case == "flagdiff":
        flagdiff()
    elif case == "analyze":
        analyze(os.path.abspath(sys.argv[2]))
    else:
        raise SystemExit(f"unknown case {case!r}")


if __name__ == "__main__":
    main()
