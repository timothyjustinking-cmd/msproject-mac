#!/usr/bin/env python3
"""Open .mpp files read-only in MS Project and dump the scheduling fields.

Coordinator-only. Every run starts and closes its own Project instance through
scripts/oracle_lock.py, opens each file ReadOnly (Project recalculates on open --
that recalculation *is* the measurement), prints one line per task and writes
docs/analysis/oracle/task-fields-<stem>.json.

Why this exists: the writer streams keep asking the same question -- "did the
date I wrote survive an open, and did Project keep or drop my constraint?" --
and the answer needs fields (`Manual`, `ConstraintType`, `ConstraintDate`,
`Type`) that the narrower wave-specific scripts do not print.

Client-data rule: only numbers, dates and booleans are recorded -- never a task
name -- so the JSON is safe to commit for a client file as well as a synthetic
fixture. `--no-save` skips the JSON when you only want the printout.

    python scripts/oracle_task_fields.py corpus/probe/w37-snet-save.mpp
    python scripts/oracle_task_fields.py --no-save corpus/project_a.mpp
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import log, oracle_session                     # noqa: E402
from oracle_w3 import Guard, rows_of, save, val                 # noqa: E402

# Fields worth printing on the console; the JSON keeps the whole read_task row.
SHOW = ("ID", "Manual", "ConstraintType", "ConstraintDate", "Duration",
        "Start", "Finish", "EarlyStart", "EarlyFinish", "LateStart",
        "LateFinish", "TotalSlack", "Milestone", "Priority", "PercentComplete")


def line_for(row):
    parts = []
    for name in SHOW:
        if name not in row:
            continue
        value = row[name]
        if isinstance(value, str) and len(value) > 19:
            value = value[:19]
        parts.append(f"{name}={value}")
    return "  " + "  ".join(parts)


# Text-valued keys are dropped from the saved JSON so the tool is safe on a
# client file too: names, notes and the predecessor/successor summaries are the
# only fields that can carry client text.
TEXT_KEYS = ("Name", "Notes", "Predecessors", "Successors")


def numbers_only(row):
    return {k: v for k, v in row.items() if k not in TEXT_KEYS}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--no-save", action="store_true",
                    help="print only; write no JSON")
    args = ap.parse_args()

    runs = []
    with oracle_session() as app:
        with Guard(300):
            for given in args.files:
                path = os.path.abspath(given)
                entry = {"file": os.path.basename(path),
                         "bytes": os.path.getsize(path)
                         if os.path.exists(path) else None}
                if not os.path.exists(path):
                    entry["error"] = "missing"
                    print(f"{path}: missing")
                    runs.append(entry)
                    continue
                try:
                    app.FileOpen(Name=path, ReadOnly=True)
                    proj = app.ActiveProject
                    entry["project_start"] = val(proj.ProjectStart)
                    entry["project_finish"] = val(
                        getattr(proj, "ProjectFinish", None))
                    entry["tasks"] = rows_of(proj)
                except Exception as exc:                     # noqa: BLE001
                    entry["error"] = f"{exc.__class__.__name__}: {exc}"
                finally:
                    # FileClose() defaults to Save=2 and blocks on a modal.
                    try:
                        app.FileClose(0)
                    except Exception:                        # noqa: BLE001
                        pass
                runs.append(entry)
                log(f"opened {entry['file']}: "
                    f"{len(entry.get('tasks', []))} rows"
                    + (f" error={entry['error']}" if "error" in entry else ""))
                print(f"{entry['file']}  project "
                      f"{entry.get('project_start')} -> "
                      f"{entry.get('project_finish')}"
                      + (f"  ERROR {entry['error']}" if "error" in entry else ""))
                for row in entry.get("tasks", []):
                    print(line_for(row))

    payload = {"case": "task-fields",
               "files": [{k: v for k, v in run.items() if k != "tasks"}
                         for run in runs],
               "runs": [dict(run, tasks=[numbers_only(t)
                                         for t in run.get("tasks", [])])
                        if "tasks" in run else run for run in runs]}
    if not args.no_save:
        stem = os.path.splitext(os.path.basename(runs[0]["file"]))[0] \
            if len(runs) == 1 else "multi"
        save(f"task-fields-{stem}", payload)
    else:
        print(json.dumps(payload["files"], indent=2))


if __name__ == "__main__":
    main()
