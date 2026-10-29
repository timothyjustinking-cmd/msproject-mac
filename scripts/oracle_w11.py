#!/usr/bin/env python3
"""Wave-11 coordinator oracle: linked exact-date pin, and task delete cascade.

    python scripts/oracle_w11.py linkeddate
        An auto FS pair whose successor carries a real SNET pin EARLIER than the
        predecessor's finish.  Measures whether Project reopens the successor at
        the pin or at max(pred finish, pin) -- the inference in save.h §10.8.

    python scripts/oracle_w11.py linkdelete
        Three linked tasks, one assignment on the middle task; delete the middle
        task, save, and diff the link/assignment tables against the pre-save
        copy.  Tells the writer what a delete cascade must do.

Both write synthetic fixtures under corpus/probe/ and a numbers-only JSON under
docs/analysis/oracle/.  Rules: no /automation, CalculateProject, FileClose(0),
aware datetimes, absolute SaveAs paths.
"""

import datetime as dt
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import log, oracle_session, working_copy     # noqa: E402
from oracle_privacy import neutralise_active  # noqa: E402
from oracle_w3 import (Guard, add_task, calc, new_project,     # noqa: E402
                       rows_of, save, utc, val)

PROBE = os.path.join(ROOT, "corpus", "probe")
START = dt.datetime(2026, 10, 19, 8)
FS = 1


def save_as(app, path):
    if os.path.exists(path):
        os.remove(path)
    neutralise_active(app)
    app.FileSaveAs(Name=path)
    return os.path.getsize(path)


def brief(proj):
    rows = []
    for i in range(1, 200):
        try:
            t = proj.Tasks(i)
        except Exception:                                    # noqa: BLE001
            break
        rows.append({"ID": val(t.ID), "uid": val(t.UniqueID),
                     "start": val(t.Start), "finish": val(t.Finish),
                     "duration": val(t.Duration),
                     "constraint": val(t.ConstraintType),
                     "constraint_date": val(t.ConstraintDate),
                     "manual": val(t.Manual)})
    return rows


# ---------------------------------------------------------------- linked pin


def linkeddate():
    path = os.path.join(PROBE, "w11-snet-link.mpp")
    with oracle_session() as app:
        with Guard(300):
            proj = new_project(app, START)
            t1 = add_task(proj, "P1", "5d")
            t2 = add_task(proj, "S1", "1d")
            t2.TaskDependencies.Add(t1, FS, "0")
            calc(app)
            before = brief(proj)
            # Pin the successor BEFORE the predecessor's finish (T1 ends Fri
            # 10-23 17:00; the pin is Wed 10-21 08:00).
            t2.ConstraintType = 4          # SNET
            t2.ConstraintDate = utc(dt.datetime(2026, 10, 21, 8))
            calc(app)
            after_calc = brief(proj)
            size = save_as(app, path)

            # Reopen from disk (the real question).
            app.FileClose(0)
            app.FileOpen(Name=path, ReadOnly=True)
            reopened = brief(app.ActiveProject)
            app.FileClose(0)

    payload = {"case": "snet-link", "saved": os.path.relpath(path, ROOT),
               "bytes": size, "before": before, "after_calc": after_calc,
               "reopened": reopened}
    save("w11-snet-link", payload)
    print(json.dumps(payload, indent=2))


# ------------------------------------------------------------- delete cascade


def linkdelete():
    pre = os.path.join(PROBE, "w11-linkdelete-pre.mpp")
    post = os.path.join(PROBE, "w11-linkdelete-post.mpp")
    with oracle_session() as app:
        with Guard(300):
            proj = new_project(app, START)
            tasks = [add_task(proj, f"T{i}", "2d") for i in (1, 2, 3)]
            tasks[1].TaskDependencies.Add(tasks[0], FS, "0")
            tasks[2].TaskDependencies.Add(tasks[1], FS, "0")
            r = proj.Resources.Add("R1")
            r.Type = 0
            calc(app)
            tasks[1].Assignments.Add(TaskID=tasks[1].ID, ResourceID=r.ID)
            calc(app)
            pre_size = save_as(app, pre)
            pre_rows = brief(proj)
            # Delete the middle task (uid 2): links 1->2 and 2->3, assignment 2/R1.
            for i in range(1, proj.Tasks.Count + 1):
                if proj.Tasks(i).UniqueID == 2:
                    proj.Tasks(i).Delete()
                    break
            calc(app)
            post_size = save_as(app, post)
            post_rows = brief(proj)
            app.FileClose(0)
            app.FileOpen(Name=pre, ReadOnly=True)
            pre_reopen = brief(app.ActiveProject)
            app.FileClose(0)
            app.FileOpen(Name=post, ReadOnly=True)
            post_reopen = brief(app.ActiveProject)
            app.FileClose(0)

    payload = {"case": "link-delete", "sizes": {"pre": pre_size,
                                                 "post": post_size},
               "pre": pre_reopen, "post": post_reopen,
               "pre_rows": pre_rows, "post_rows": post_rows,
               "fixtures": {"pre": os.path.relpath(pre, ROOT),
                            "post": os.path.relpath(post, ROOT)}}
    save("w11-linkdelete", payload)
    print(json.dumps(payload, indent=2))


def fieldbase():
    """Project's own six custom-field values on the SAME synthetic base our
    writer uses, so the FixedMeta/Fixed2Meta/Props deltas are directly
    comparable byte for byte."""
    original = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")
    src = working_copy(original, "w11-fields-base")  # wave 23: never in place
    out = os.path.join(PROBE, "w11-fields-base.mpp")
    with oracle_session() as app:
        with Guard(300):
            app.FileOpen(Name=src, ReadOnly=False)
            proj = app.ActiveProject
            t = proj.Tasks(1)          # uid 1
            t.SetField(0x0B400033, "W48 alpha")
            t.SetField(0x0B400057, 42.5)
            t.SetField(0x0B400058, 3.25)
            t.SetField(0x0B400067, "1d")
            t.Date1 = utc(dt.datetime(2026, 10, 26, 8))
            t.SetField(0x0B400048, "Yes")
            calc(app)
            size = save_as(app, out)
            app.FileClose(0)
            app.FileOpen(Name=out, ReadOnly=True)
            rows = brief(app.ActiveProject)
            app.FileClose(0)
    payload = {"case": "field-base", "source": os.path.relpath(original, ROOT),
               "saved": os.path.relpath(out, ROOT), "bytes": size,
               "rows": rows}
    save("w11-fields-base", payload)
    print(json.dumps(payload, indent=2))


def fieldone():
    """One and two custom fields on the same base, to derive the presence rule."""
    src = working_copy(os.path.join(ROOT, "tests", "data", "synthetic-base.mpp"),
                       "w11-fields-one-two")  # wave 23: never in place
    one = os.path.join(PROBE, "w11-fields-number1.mpp")
    two = os.path.join(PROBE, "w11-fields-number1-text1.mpp")
    with oracle_session() as app:
        with Guard(300):
            app.FileOpen(Name=src, ReadOnly=False)
            proj = app.ActiveProject
            t = proj.Tasks(1)
            t.SetField(0x0B400057, 42.5)
            calc(app)
            sizes = {"one": save_as(app, one)}
            t.SetField(0x0B400033, "abc")
            calc(app)
            sizes["two"] = save_as(app, two)
            app.FileClose(0)
    payload = {"case": "field-one-two", "sizes": sizes,
               "fixtures": {"one": os.path.relpath(one, ROOT),
                            "two": os.path.relpath(two, ROOT)}}
    save("w11-fields-one-two", payload)
    print(json.dumps(payload, indent=2))


def fieldprobe():
    """One custom field at a time on the same base: build the presence bitmap."""
    src = working_copy(os.path.join(ROOT, "tests", "data", "synthetic-base.mpp"),
                       "w11-fieldprobe")  # wave 23: never in place
    specs = [
        ("number2", 0x0B400058, 3.25),
        ("duration1", 0x0B400067, "1d"),
        ("date1", 0x0B400109, "DATE"),
        ("flag1", 0x0B400048, "Yes"),
    ]
    out = {}
    with oracle_session() as app:
        with Guard(420):
            for label, fid, value in specs:
                path = os.path.join(PROBE, f"w11-field-{label}.mpp")
                app.FileOpen(Name=src, ReadOnly=False)
                proj = app.ActiveProject
                t = proj.Tasks(1)
                if value == "DATE":
                    t.Date1 = utc(dt.datetime(2026, 10, 26, 8))
                else:
                    t.SetField(fid, value)
                calc(app)
                out[label] = {"bytes": save_as(app, path)}
                app.FileClose(0)
    payload = {"case": "field-probes", "runs": out,
               "fixtures": {k: os.path.relpath(
                   os.path.join(PROBE, f"w11-field-{k}.mpp"), ROOT)
                   for k in out}}
    save("w11-field-probes", payload)
    print(json.dumps(payload, indent=2))


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    if sys.argv[1] == "linkeddate":
        linkeddate()
    elif sys.argv[1] == "linkdelete":
        linkdelete()
    elif sys.argv[1] == "fieldbase":
        fieldbase()
    elif sys.argv[1] == "fieldone":
        fieldone()
    elif sys.argv[1] == "fieldprobe":
        fieldprobe()
    else:
        raise SystemExit(f"unknown case {sys.argv[1]!r}")


if __name__ == "__main__":
    main()
