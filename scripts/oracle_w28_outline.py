#!/usr/bin/env python3
"""W28-0.2: Project 2021's OutlineNumber for the corpus capture's first rows.

The wave-27 test pinned OutlineNumber values that cannot occur in Project
(ID 1 = "2", ID 12 = "1" while the rows display in ID order at levels implying
1, 1.1, ... 2).  This measurement opens a COPY of corpus/project_a.mpp under
build/oracle-work through the oracle lock, reads Project's own
OutlineNumber/OutlineLevel for the project row and IDs 1..12, and writes the
numbers to build/w28/W28-0.2-project-outline.json.  It also measures
tests/data/wave24-order.mpp, whose stored row order differs from its ID order,
so the committed qt_task_order test can pin the same rule in CI (the corpus
test only runs where corpus/ exists).  No task or project name is read,
printed or stored; rows are identified by ID only.

    python -I scripts/oracle_w28_outline.py
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, working_copy  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

CORPUS = os.path.join(ROOT, "corpus", "project_a.mpp")
ORDER_FIXTURE = os.path.join(ROOT, "tests", "data", "wave24-order.mpp")
OUT = os.path.join(ROOT, "build", "w28")
RESULT = os.path.join(OUT, "W28-0.2-project-outline.json")


def val(v):
    if isinstance(v, (int, float, bool, str)) or v is None:
        return v
    return repr(v)


def prop(obj, name):
    try:
        return val(getattr(obj, name))
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def row(task, row_index):
    return {
        "row": row_index,
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "outline_number": prop(task, "OutlineNumber"),
        "outline_level": prop(task, "OutlineLevel"),
        "summary": prop(task, "Summary"),
    }


def measure(app, path, last_index):
    app.FileOpen(path)
    project = app.ActiveProject
    rows = [row(project.ProjectSummaryTask, 0)]
    for index in range(1, last_index + 1):
        rows.append(row(project.Tasks(index), index))
    app.FileClose(0)
    return rows


def main():
    if not os.path.isfile(CORPUS):
        sys.exit(f"corpus file missing: {CORPUS}")
    if not os.path.isfile(ORDER_FIXTURE):
        sys.exit(f"fixture missing: {ORDER_FIXTURE}")
    os.makedirs(OUT, exist_ok=True)
    with oracle_session() as app:
        with Guard(300):
            corpus_rows = measure(app, working_copy(CORPUS, "w28-0.2-outline"), 12)
            order_rows = measure(app, working_copy(ORDER_FIXTURE, "w28-0.2-order"), 5)
    payload = {
        "measured": "MS Project 2021 OutlineNumber for the captured rows",
        "corpus": {
            "source": "corpus/project_a.mpp (copy under build/oracle-work)",
            "rows": corpus_rows,
        },
        "wave24_order": {
            "source": "tests/data/wave24-order.mpp (copy under build/oracle-work)",
            "rows": order_rows,
        },
    }
    with open(RESULT, "w", encoding="utf-8") as fh:
        json.dump(payload, fh, indent=2)
        fh.write("\n")
    for label, rows in (("corpus", corpus_rows), ("wave24-order", order_rows)):
        for r in rows:
            print("%-12s row %2d id %2s level %s summary %s outline %r"
                  % (label, r["row"], r["id"], r["outline_level"], r["summary"],
                     r["outline_number"]))
    print("wrote", os.path.relpath(RESULT, ROOT))


if __name__ == "__main__":
    main()
