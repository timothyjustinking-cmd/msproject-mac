#!/usr/bin/env python3
"""Recalculate a project in MS Project and save the result (coordinator only).

This is the M2 oracle: it produces the file the port's own recalculation must
reproduce, task for task.

    python scripts/oracle_recalc.py <input.mpp> <output.mpp>

The output lands under `corpus/probe/` by convention (git-ignored, like all
`.mpp`). Opening a client file is read-only; Save As writes a *copy*.
"""

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log  # noqa: E402
from oracle_privacy import neutralise  # noqa: E402
from oracle_w3 import Guard, calc, rows_of, save, val  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("source")
    ap.add_argument("output")
    args = ap.parse_args()

    out = {"source": os.path.basename(args.source), "output": os.path.basename(args.output)}
    with oracle_session() as app:
        with Guard(240):
            app.FileOpen(os.path.abspath(args.source))
            proj = app.ActiveProject
            if proj is None:
                out["opened"] = False
                save("recalc", out)
                raise SystemExit("Project did not open the file")
            out["opened"] = True
            out["name"] = val(getattr(proj, "Name", None))
            out["project_start"] = val(getattr(proj, "ProjectStart", None))
            # Deliberately NOT storing task rows: the source may be a client
            # programme, and this JSON is committed while the .mpp is not.
            before = rows_of(proj)
            calc(app)
            after = rows_of(proj)
            out["task_count"] = len(after)
            out["changed_task_count"] = sum(
                1 for a, b in zip(before, after)
                if (a.get("Start"), a.get("Finish")) != (b.get("Start"), b.get("Finish")))
            target = os.path.abspath(args.output)
            if os.path.exists(target):
                os.remove(target)
            neutralise(app, proj, "Synthetic Recalc Fixture")
            app.FileSaveAs(Name=target)
            out["bytes"] = os.path.getsize(target)
            log(f"saved {target} ({out['bytes']:,} B, {len(out['tasks_after'])} tasks)")
    save("recalc", out)


if __name__ == "__main__":
    main()
