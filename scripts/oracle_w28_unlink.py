#!/usr/bin/env python3
"""W28-0.4: Project 2021's reopen of the PORT's Unlink save.

Standing rule 1 asks for the Project-saved file of the same edit.  The port's
Unlink save removes the same two links as Project's own Unlink (the link-table
streams are byte-identical to tests/data/wave27-unlink.mpp), but it does NOT
reschedule: Project's Unlink save moves the dates of the affected rows, and
wave 27 deliberately compared only the link table.  This script opens copies
(never the committed fixtures) under build/oracle-work through the oracle lock
and records what Project shows on open for:

* wave23-outline.mpp          the base the edit starts from
* wave27-unlink-control.mpp   Project's control save before Unlink
* wave27-unlink.mpp           Project's own Unlink save
* app-unlink-port.mpp         the port's Unlink save (built by
                              app_edits_links_tests under build/)

For every task row it records ID, UniqueID, Start, Finish, Duration,
OutlineNumber, the predecessor/successor strings and the TaskDependencies
count, then opens the same files a second time with an explicit Calculate to
show whether Project's first read was already recalculated.  No task or
project name is read, printed or stored; rows are identified by ID only.

    python -I scripts/oracle_w28_unlink.py
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, working_copy  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

DATA = os.path.join(ROOT, "tests", "data")
BASE = os.path.join(DATA, "wave23-outline.mpp")
CONTROL = os.path.join(DATA, "wave27-unlink-control.mpp")
PROJECT_UNLINK = os.path.join(DATA, "wave27-unlink.mpp")
PORT = os.path.join(ROOT, "build", "w27-fresh-app", "test-out", "app-unlink-port.mpp")
OUT = os.path.join(ROOT, "build", "w28")
RESULT = os.path.join(OUT, "W28-0.4-project-reopen.json")

LAST_TASK_INDEX = 6


def val(v):
    if isinstance(v, (int, float, bool, str)) or v is None:
        return v
    return repr(v)


def prop(obj, name):
    try:
        return val(getattr(obj, name))
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def dependency_count(task):
    try:
        return val(task.TaskDependencies.Count)
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def row(task, row_index):
    return {
        "row": row_index,
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "duration": prop(task, "Duration"),
        "outline_number": prop(task, "OutlineNumber"),
        "predecessors": prop(task, "Predecessors"),
        "successors": prop(task, "Successors"),
        "task_dependencies": dependency_count(task),
    }


def read_open(app, path):
    app.FileOpen(path)
    project = app.ActiveProject
    rows = [row(project.ProjectSummaryTask, 0)]
    for index in range(1, LAST_TASK_INDEX + 1):
        rows.append(row(project.Tasks(index), index))
    return rows


def calculate_all(app):
    """Explicit recalc on the reopened copy; name the call that worked."""
    for call in ("CalculateAll", "CalculateProject"):
        try:
            getattr(app, call)()
            return call
        except Exception:  # noqa: BLE001
            continue
    return "<unavailable>"


def measure(app, path, tag):
    """Open, read, close; then reopen, recalculate explicitly, read again."""
    opened = read_open(app, working_copy(path, f"w28-0.4-{tag}-open"))
    app.FileClose(0)
    app.FileOpen(working_copy(path, f"w28-0.4-{tag}-calc"))
    project = app.ActiveProject
    call = calculate_all(app)
    calculated = [row(project.ProjectSummaryTask, 0)]
    for index in range(1, LAST_TASK_INDEX + 1):
        calculated.append(row(project.Tasks(index), index))
    app.FileClose(0)
    return {"on_open": opened, "after_calculate": calculated,
            "calculate_call": call}


def main():
    for path in (BASE, CONTROL, PROJECT_UNLINK, PORT):
        if not os.path.isfile(path):
            sys.exit(f"missing input: {path}")
    os.makedirs(OUT, exist_ok=True)
    with oracle_session() as app:
        with Guard(300):
            payload = {}
            calculation_mode = prop(app, "Calculation")
            for tag, path, label in (
                ("base", BASE, "tests/data/wave23-outline.mpp"),
                ("control", CONTROL, "tests/data/wave27-unlink-control.mpp"),
                ("project-unlink", PROJECT_UNLINK, "tests/data/wave27-unlink.mpp"),
                ("port-unlink", PORT,
                 "build/w27-fresh-app/test-out/app-unlink-port.mpp"),
            ):
                payload[tag] = {
                    "source": label + " (copy under build/oracle-work)",
                    "measurement": measure(app, path, tag),
                }
    out = {
        "measured": "MS Project 2021 reopen of the port's Unlink save",
        "selected_uid": 4,
        "calculation_mode": calculation_mode,
        "files": payload,
    }
    with open(RESULT, "w", encoding="utf-8") as fh:
        json.dump(out, fh, indent=2)
        fh.write("\n")
    for tag, entry in payload.items():
        for phase in ("on_open", "after_calculate"):
            for r in entry["measurement"][phase]:
                print("%-14s %-15s id %2s start %-22s finish %-22s deps %s"
                      % (tag, phase, r["id"], r["start"], r["finish"],
                         r["task_dependencies"]))
    print("wrote", os.path.relpath(RESULT, ROOT))


if __name__ == "__main__":
    main()
