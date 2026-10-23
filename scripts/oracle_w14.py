#!/usr/bin/env python3
"""Wave-14 coordinator oracle: the parity gaps that block the next streams.

    tablecol     TableEdit column variants (wave-13 run: replaces, never appends)
    tablecol2    the same with an explicit ColumnPosition (wave-13 run)
    colinsert    Application.ColumnInsert at the selected field
    tableappend  can a column be APPENDED (ColumnPosition 8 / 9)?
    tableadd     add a column to a new table definition, three API variants
    formula      change/add custom-field formulas, then save
    assignfield  assignment-level custom fields (Number1/Text1)
    cost         task Cost1 value (storage was unmeasured)
    manual       manual scheduling: Manual=True with a moved Start

Every measuring case saves a CONTROL file first and the edited file second, so
`scripts/mpp_diff.py control target` isolates the edit from Project's per-save
identity churn.  Rules: no /automation, CalculateProject, FileClose(0), aware
datetimes, absolute SaveAs paths, no client data in committed JSON.

Corrections over the wave-13 sketch, from the typelib and the wave-13 probe run:
  * `Assignment.SetField` does not exist (only Task/Resource have it), so
    assignment custom fields go through the Assignment.Number1 (0x0f40006c) /
    Assignment.Text1 (0x0f400058) properties.
  * `TableEdit` coerces FieldName to a string: a Long raises `The field
    "188743767" does not exist.` (variant D) and with Create=True is ignored
    silently.  A column changes only when FieldName is a field NAME and
    ColumnPosition is supplied; ColumnPosition=0 REPLACED the column at index 1
    (variant I/J), leaving 9 columns and the block length unchanged.

Set W14_BASE to measure against another base file (e.g. corpus/probe/base.mpp
while tests/data/synthetic-base.mpp is held by a sync client).
"""

import datetime as dt
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import log, oracle_session, working_copy     # noqa: E402
from oracle_w3 import (Guard, add_task, calc, new_project,     # noqa: E402
                       save, utc, val)

PROBE = os.path.join(ROOT, "corpus", "probe")
BASE_SOURCE = os.environ.get(
    "W14_BASE", os.path.join(ROOT, "tests", "data", "synthetic-base.mpp"))
# Wave 23 (A-7b): every FileOpen below uses this working COPY.  The committed
# fixture is never opened in place (Project rewrites a file it opens).
BASE = working_copy(BASE_SOURCE, "w14")
START = dt.datetime(2026, 10, 19, 8)

TASK_NUMBER1 = 0x0B400057
TASK_NUMBER2 = 0x0B400058
TASK_TEXT2 = 0x0B400036
TASK_COST1 = 0x0B40006A


def save_as(app, path):
    """Save the active project to an absolute path; return its byte size."""
    if os.path.exists(path):
        os.remove(path)
    app.FileSaveAs(Name=path)
    return os.path.getsize(path)


def rel(path):
    return os.path.relpath(path, ROOT)


def table_fields(proj, name):
    """Enumerate a table's columns through COM: in-session evidence, independent
    of what Project later persists."""
    try:
        tbl = proj.Tables(name)
        count = int(tbl.TableFields.Count)
    except Exception as exc:                            # noqa: BLE001
        return {"error": f"{exc.__class__.__name__}: {exc}"}
    cols = []
    for i in range(1, count + 1):
        try:
            tf = tbl.TableFields(i)
            cols.append({"field": val(tf.Field), "title": val(tf.Title),
                         "width": val(tf.Width)})
        except Exception as exc:                        # noqa: BLE001
            cols.append({"error": f"{exc.__class__.__name__}: {exc}"})
    return {"count": count, "columns": cols}


def tablecol():
    """Positional TableEdit variants; A/B/C return True but change nothing except
    the table flags, D (numeric FieldName) raises."""
    variants = [
        ("A-create-false-string", dict(
            name="Entry", task_table=True, create=False, overwrite=False,
            new_name="", field="Number1", new_field="Number1")),
        ("B-overwrite-string", dict(
            name="Entry", task_table=True, create=False, overwrite=True,
            new_name="", field="Number1", new_field="Number1")),
        ("C-newtable-field", dict(
            name="Entry", task_table=True, create=True, overwrite=True,
            new_name="W14 Entry C", field="Number1", new_field="Number1")),
        ("D-numeric-overwrite", dict(
            name="Entry", task_table=True, create=False, overwrite=True,
            new_name="", field=188743767, new_field="Number1")),
    ]
    results = []
    with oracle_session() as app:
        with Guard(300):
            for label, v in variants:
                app.FileOpen(Name=BASE, ReadOnly=False)
                out = os.path.join(PROBE, f"w14-tablecol-{label[0]}.mpp")
                rec = {"variant": label, "args": {k: str(x) for k, x in v.items()}}
                try:
                    rec["ok"] = val(app.TableEdit(
                        v["name"], v["task_table"], v["create"], v["overwrite"],
                        v["new_name"], v["field"], v["new_field"],
                        "Number1", 12, 0, False, True))
                    app.TableApply(v["new_name"] or v["name"])
                    calc(app)
                    rec["bytes"] = save_as(app, out)
                    rec["saved"] = rel(out)
                except Exception as exc:                      # noqa: BLE001
                    rec["error"] = f"{exc.__class__.__name__}: {exc}"
                finally:
                    try:
                        app.FileClose(0)
                    except Exception:                         # noqa: BLE001
                        pass
                results.append(rec)
                log(f"{label}: ok={rec.get('ok')} err={rec.get('error')}")
    payload = {"case": "table-column-probe", "base": rel(BASE),
               "variants": results}
    save("w14-tablecol", payload)
    print(json.dumps(payload, indent=2))


def tablecol2():
    """Keyword TableEdit probes with an explicit ColumnPosition."""
    variants = [
        ("I-pos0-number", dict(Name="Entry", TaskTable=True, Create=False,
                               OverwriteExisting=True, FieldName="Number1",
                               NewFieldName="Number1", Title="Number1",
                               Width=12, ColumnPosition=0)),
        ("J-pos0-text", dict(Name="Entry", TaskTable=True, Create=False,
                             OverwriteExisting=True, FieldName="Text1",
                             NewFieldName="Text1", Title="Text1",
                             Width=12, ColumnPosition=0)),
        ("K-nopos-text", dict(Name="Entry", TaskTable=True, Create=False,
                              OverwriteExisting=True, FieldName="Text1",
                              NewFieldName="Text1", Title="Text1",
                              Width=12)),
    ]
    results = []
    with oracle_session() as app:
        with Guard(300):
            for label, kwargs in variants:
                app.FileOpen(Name=BASE, ReadOnly=False)
                out = os.path.join(PROBE, f"w14-tablecol2-{label[0]}.mpp")
                rec = {"variant": label,
                       "args": {k: str(v) for k, v in kwargs.items()}}
                try:
                    rec["ok"] = val(app.TableEdit(**kwargs))
                    app.TableApply(kwargs.get("NewName") or "Entry")
                    calc(app)
                    rec["bytes"] = save_as(app, out)
                    rec["saved"] = rel(out)
                except Exception as exc:                      # noqa: BLE001
                    rec["error"] = f"{exc.__class__.__name__}: {exc}"
                finally:
                    try:
                        app.FileClose(0)
                    except Exception:                         # noqa: BLE001
                        pass
                results.append(rec)
                log(f"{label}: ok={rec.get('ok')} err={rec.get('error')}")
    payload = {"case": "table-column-probe-2", "base": rel(BASE),
               "variants": results}
    save("w14-tablecol2", payload)
    print(json.dumps(payload, indent=2))


def colinsert():
    """Insert a column through Application.ColumnInsert at the selection."""
    out = os.path.join(PROBE, "w14-colinsert.mpp")
    with oracle_session() as app:
        with Guard(300):
            app.FileOpen(Name=BASE, ReadOnly=False)
            app.TableApply("Entry")
            app.SelectTaskField(1, "Name")
            app.ColumnInsert()
            calc(app)
            size = save_as(app, out)
            app.FileClose(0)
    payload = {"case": "column-insert", "base": rel(BASE), "bytes": size,
               "saved": rel(out)}
    save("w14-colinsert", payload)
    print(json.dumps(payload, indent=2))


def tableappend():
    """Can TableEdit APPEND?  Same call as variant I, ColumnPosition 8 and 9,
    which is where Entry's last column (index 8) sits."""
    results = []
    with oracle_session() as app:
        with Guard(400):
            for pos in (8, 9):
                label = f"append-pos{pos}"
                out = os.path.join(PROBE, f"w14-tableappend-{pos}.mpp")
                rec = {"variant": label, "position": pos}
                try:
                    app.FileOpen(Name=BASE, ReadOnly=False)
                    proj = app.ActiveProject
                    rec["ok"] = val(app.TableEdit(
                        "Entry", True, False, True, "", "Number1", "Number1",
                        "Number1", 12, 0, False, True, None, None, pos))
                    app.TableApply("Entry")
                    rec["fields"] = table_fields(proj, "Entry")
                    calc(app)
                    rec["bytes"] = save_as(app, out)
                    rec["saved"] = rel(out)
                except Exception as exc:                      # noqa: BLE001
                    rec["error"] = f"{exc.__class__.__name__}: {exc}"
                finally:
                    try:
                        app.FileClose(0)
                    except Exception:                         # noqa: BLE001
                        pass
                results.append(rec)
                log(f"{label}: ok={rec.get('ok')} n="
                    f"{rec.get('fields', {}).get('count')} err={rec.get('error')}")
    payload = {"case": "table-append-probe", "base": rel(BASE),
               "variants": results}
    save("w14-tableappend", payload)
    print(json.dumps(payload, indent=2))


def tableadd():
    """Add a column to a new table definition, three ways, with a control save."""
    variants = []
    with oracle_session() as app:
        with Guard(600):
            for tag, mode in (("str", "tableedit-string"),
                              ("ex", "tableedit-ex"),
                              ("tf", "tablefields-add")):
                name = f"W14 {tag}"
                ctl = os.path.join(PROBE, f"w14-tableadd-{tag}-control.mpp")
                tgt = os.path.join(PROBE, f"w14-tableadd-{tag}.mpp")
                entry = {"tag": tag, "mode": mode, "table": name}
                try:
                    app.FileOpen(Name=BASE, ReadOnly=False)
                    proj = app.ActiveProject
                    entry["control_bytes"] = save_as(app, ctl)
                    entry["tables_before"] = int(proj.Tables.Count)
                    if mode == "tableedit-string":
                        # Name, TaskTable, Create, OverwriteExisting, NewName,
                        # FieldName, NewFieldName, Title, Width, Align,
                        # ShowInMenu, LockFirstColumn (dispatch 402 order).
                        entry["edit_returns"] = val(app.TableEdit(
                            "Entry", True, True, True, name, "Number1", None,
                            "Number1", 12, 0, True, True))
                    elif mode == "tableedit-ex":
                        entry["edit_returns"] = val(app.TableEditEx(
                            "Entry", True, True, True, name, "Number1", None,
                            "Number1", 12, 0, True, True))
                    else:
                        entry["edit_returns"] = val(app.TableEdit(
                            "Entry", True, True, True, name))
                        app.TableApply(name)
                        tbl = proj.Tables(name)
                        entry["add_returns"] = val(
                            tbl.TableFields.Add(TASK_NUMBER1, 0, 12, "Number1"))
                    app.TableApply(name)
                    entry["tables_after"] = int(proj.Tables.Count)
                    entry["fields"] = table_fields(proj, name)
                    calc(app)
                    entry["bytes"] = save_as(app, tgt)
                    entry["saved"] = rel(tgt)
                    entry["control"] = rel(ctl)
                except Exception as exc:                # noqa: BLE001
                    entry["error"] = f"{exc.__class__.__name__}: {exc}"
                    log(f"  ! {tag}: {entry['error']}")
                    try:
                        app.FileClose(0)
                    except Exception:                   # noqa: BLE001
                        pass
                else:
                    app.FileClose(0)
                variants.append(entry)
                log(f"  {tag}: fields={entry.get('fields', {}).get('count')} "
                    f"bytes={entry.get('bytes')}")
    payload = {"case": "table-add", "base": rel(BASE), "variants": variants}
    save("w14-tableadd", payload)
    print(json.dumps(payload, indent=2))


def formula():
    ids = {"Number2": TASK_NUMBER2, "Text2": TASK_TEXT2}
    want = {"Number2": "[Duration]/240", "Text2": "[Name]"}
    ctl = os.path.join(PROBE, "w14-formula-control.mpp")
    tgt = os.path.join(PROBE, "w14-formula.mpp")
    with oracle_session() as app:
        with Guard(300):
            app.FileOpen(Name=BASE, ReadOnly=False)
            head = {"case": "formula-write", "base": rel(BASE),
                    "control": rel(ctl), "saved": rel(tgt), "formulas": want,
                    "field_ids": {k: hex(v) for k, v in ids.items()}}
            head["control_bytes"] = save_as(app, ctl)
            head["before"] = {}
            for label, fid in ids.items():
                try:
                    head["before"][label] = val(app.CustomFieldGetFormula(fid))
                except Exception as exc:                # noqa: BLE001
                    head["before"][label] = f"<err {exc.__class__.__name__}: {exc}>"
            head["set_returns"] = {}
            for label, fid in ids.items():
                try:
                    head["set_returns"][label] = val(
                        app.CustomFieldSetFormula(fid, want[label]))
                except Exception as exc:                # noqa: BLE001
                    head["set_returns"][label] = f"<err {exc.__class__.__name__}: {exc}>"
            head["after"] = {}
            for label, fid in ids.items():
                try:
                    head["after"][label] = val(app.CustomFieldGetFormula(fid))
                except Exception as exc:                # noqa: BLE001
                    head["after"][label] = f"<err {exc.__class__.__name__}: {exc}>"
            calc(app)
            head["bytes"] = save_as(app, tgt)
            app.FileClose(0)
    save("w14-formula", head)
    print(json.dumps(head, indent=2))


def assignfield():
    ctl = os.path.join(PROBE, "w14-assignfield-control.mpp")
    tgt = os.path.join(PROBE, "w14-assignfield.mpp")
    with oracle_session() as app:
        with Guard(300):
            proj = new_project(app, START)
            t = add_task(proj, "T1", "3d")
            r = proj.Resources.Add("R1")
            r.Type = 0
            calc(app)
            t.Assignments.Add(TaskID=t.ID, ResourceID=r.ID)
            calc(app)
            a = t.Assignments(1)
            head = {"case": "assignment-fields", "control": rel(ctl),
                    "saved": rel(tgt),
                    "values": {"Number1 0x0f40006c": 7.5,
                               "Text1 0x0f400058": "assign-text"}}
            head["control_bytes"] = save_as(app, ctl)
            head["before"] = {"Number1": val(a.Number1), "Text1": val(a.Text1)}
            a.Number1 = 7.5
            a.Text1 = "assign-text"
            calc(app)
            head["after"] = {"Number1": val(a.Number1), "Text1": val(a.Text1)}
            head["bytes"] = save_as(app, tgt)
            app.FileClose(0)
    save("w14-assignfield", head)
    print(json.dumps(head, indent=2))


def cost():
    ctl = os.path.join(PROBE, "w14-cost-control.mpp")
    tgt = os.path.join(PROBE, "w14-cost.mpp")
    with oracle_session() as app:
        with Guard(300):
            app.FileOpen(Name=BASE, ReadOnly=False)
            t = app.ActiveProject.Tasks(1)
            head = {"case": "cost-field", "base": rel(BASE),
                    "control": rel(ctl), "saved": rel(tgt),
                    "cost1": 123.45, "field": "Task.Cost1 0x0b40006a"}
            head["control_bytes"] = save_as(app, ctl)
            head["before"] = {"Cost1": val(t.Cost1)}
            head["set_returns"] = val(t.SetField(TASK_COST1, 123.45))
            calc(app)
            head["after"] = {"Cost1": val(t.Cost1),
                             "GetField": val(t.GetField(TASK_COST1))}
            head["bytes"] = save_as(app, tgt)
            app.FileClose(0)
    save("w14-cost", head)
    print(json.dumps(head, indent=2))


def manual():
    ctl = os.path.join(PROBE, "w14-manual-control.mpp")
    tgt = os.path.join(PROBE, "w14-manual.mpp")
    with oracle_session() as app:
        with Guard(300):
            app.FileOpen(Name=BASE, ReadOnly=False)
            t = app.ActiveProject.Tasks(1)
            head = {"case": "manual-start", "base": rel(BASE),
                    "control": rel(ctl), "saved": rel(tgt),
                    "task": "uid 1", "start": "2026-01-07 09:00"}
            head["control_bytes"] = save_as(app, ctl)
            head["before"] = {"Manual": val(t.Manual), "Start": val(t.Start),
                              "ConstraintType": val(t.ConstraintType),
                              "ConstraintDate": val(t.ConstraintDate),
                              "Type": val(t.Type)}
            t.Manual = True
            t.Start = utc(dt.datetime(2026, 1, 7, 9))
            calc(app)
            head["after"] = {"Manual": val(t.Manual), "Start": val(t.Start),
                             "ConstraintType": val(t.ConstraintType),
                             "ConstraintDate": val(t.ConstraintDate),
                             "Type": val(t.Type)}
            head["bytes"] = save_as(app, tgt)
            app.FileClose(0)
    save("w14-manual", head)
    print(json.dumps(head, indent=2))


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    cases = {"tablecol": tablecol, "tablecol2": tablecol2,
             "colinsert": colinsert, "tableappend": tableappend,
             "tableadd": tableadd, "formula": formula,
             "assignfield": assignfield, "cost": cost, "manual": manual}
    if sys.argv[1] not in cases:
        raise SystemExit(f"unknown case {sys.argv[1]!r}")
    cases[sys.argv[1]]()


if __name__ == "__main__":
    main()
