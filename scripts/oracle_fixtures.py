#!/usr/bin/env python3
"""Build the assignment and link fixtures the reader tests need (coordinator only).

`corpus/probe/` is git-ignored; these two files are what makes the Rsc/Cons/Assn
row layouts checkable. Re-run them if the fixtures are lost.

    python scripts/oracle_fixtures.py assign   # corpus/probe/assign1.mpp
    python scripts/oracle_fixtures.py link     # corpus/probe/link1.mpp

The COM facts these encode:

* an assignment is created with `Task.Assignments.Add(ResourceID=<id>)`.
  `Add(resource_object)` and `Add()` raise "The argument value is not valid".
* a link is created with `Task.TaskDependencies.Add(pred, type, lag)` or
  `Task.LinkPredecessors(pred, type, lag)`; `Project.TaskDependencies` does not
  exist. Types are 0=FF, 1=FS, 2=SF, 3=SS; lag is a duration ("1d", "-1d", "2h").
"""

import argparse
import datetime as dt
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log  # noqa: E402
from oracle_w3 import (Guard, add_task, calc, new_project, rows_of, save,  # noqa: E402
                       val, PROBE)


def save_fixture(app, name):
    path = os.path.join(PROBE, name)
    if os.path.exists(path):
        os.remove(path)
    app.FileSaveAs(Name=path)          # must be absolute: Project resolves relative to its own cwd
    log(f"saved {path} ({os.path.getsize(path):,} B)")
    return os.path.basename(path), os.path.getsize(path)


def cmd_assign(args):
    with oracle_session() as app:
        with Guard(120):
            proj = new_project(app, dt.datetime(2026, 10, 19))
            t1 = add_task(proj, "T1", "3d", dt.datetime(2026, 10, 19, 8))
            t2 = add_task(proj, "T2", "2d", dt.datetime(2026, 10, 22, 8))
            r1 = proj.Resources.Add("Crew")
            r2 = proj.Resources.Add("Crane")
            calc(app)
            assignments = [t1.Assignments.Add(ResourceID=r1.ID),
                           t1.Assignments.Add(ResourceID=r2.ID),
                           t2.Assignments.Add(ResourceID=r1.ID)]
            calc(app)
            out = {"resource_ids": [val(r1.ID), val(r2.ID)],
                   "task_ids": [val(t1.ID), val(t2.ID)],
                   "assignments": [{"task": val(a.TaskID), "resource": val(a.ResourceID),
                                    "units": val(a.Units), "work": val(a.Work)}
                                   for a in assignments]}
            out["saved"], out["bytes"] = save_fixture(app, "assign1.mpp")
            save("assign-probe", out)


def cmd_link(args):
    with oracle_session() as app:
        with Guard(120):
            proj = new_project(app, dt.datetime(2026, 10, 19))
            tasks = [add_task(proj, f"T{i}", "1d",
                              dt.datetime(2026, 10, 19, 8) if i == 1 else None)
                     for i in range(1, 5)]
            calc(app)
            links = [(tasks[1], tasks[0], 1, 0),        # T2 FS    <- T1
                     (tasks[2], tasks[0], 3, "1d"),     # T3 SS+1d <- T1
                     (tasks[3], tasks[1], 0, "-1d"),    # T4 FF-1d <- T2
                     (tasks[3], tasks[2], 2, "2h")]     # T4 SF+2h <- T3
            for succ, pred, link_type, lag in links:
                succ.TaskDependencies.Add(pred, link_type, lag)
            calc(app)
            out = {"tasks": rows_of(proj),
                   "links": [{"succ": val(s.ID), "pred": val(p.ID), "type": t,
                              "lag": str(l)} for s, p, t, l in links]}
            out["saved"], out["bytes"] = save_fixture(app, "link1.mpp")
            save("link-probe", out)



def cmd_outline(args):
    """A project with a summary, two children and a gap: outline fields + rollup."""
    with oracle_session() as app:
        with Guard(120):
            proj = new_project(app, dt.datetime(2026, 10, 19))
            add_task(proj, "S1", "2d")
            add_task(proj, "C1", "2d", dt.datetime(2026, 10, 19, 8))
            add_task(proj, "C2", "1d", dt.datetime(2026, 10, 26, 8))
            calc(app)
            proj.Tasks(2).OutlineIndent()
            proj.Tasks(3).OutlineIndent()
            calc(app)
            out = {"tasks": rows_of(proj)}
            out["saved"], out["bytes"] = save_fixture(app, "outline1.mpp")
            save("outline-probe", out)


CASES = {"assign": cmd_assign, "link": cmd_link, "outline": cmd_outline}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("case", choices=sorted(CASES))
    args = ap.parse_args()
    CASES[args.case](args)


if __name__ == "__main__":
    main()
