#!/usr/bin/env python3
"""Regenerate tests/data/project_a_preds_golden.sha256 with ONE HASH PER ROW.

Wave 24 (W24-8/V).  The old file stored a single SHA-256 over all 83 rows, so a
failure could not name the row that differed.  This reads Project's own
Task.Predecessors / Task.Successors through COM from a COPY of the client file
(never the committed copy: Project rewrites files it opens) and writes, per row:

    <ID>\\t<SHA256 of Predecessors>\\t<SHA256 of Successors>

Only hashes are stored.  The strings carry client schedule data and are never
quoted.

    python scripts/oracle_preds_golden.py
"""

import hashlib
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

SOURCE = os.path.join(ROOT, "corpus", "project_a.mpp")
TARGET = os.path.join(ROOT, "tests", "data", "project_a_preds_golden.sha256")
WORK = os.path.join(ROOT, "build", "w24", "preds-project_a.mpp")


def digest(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest().upper()


def main():
    if not os.path.exists(SOURCE):
        print("SKIP: corpus/project_a.mpp is not present (coordinator machine only)")
        return 0
    os.makedirs(os.path.dirname(WORK), exist_ok=True)
    shutil.copyfile(SOURCE, WORK)
    source_sha = hashlib.sha256(open(SOURCE, "rb").read()).hexdigest()
    rows = []
    with oracle_session() as app:
        with Guard(240):
            app.FileOpen(WORK)
            project = app.ActiveProject
            for task in project.Tasks:
                if task is None:
                    continue
                rows.append((int(task.ID), str(task.Predecessors or ""),
                             str(task.Successors or "")))
            app.FileClose(0)
    rows.sort(key=lambda row: row[0])
    lines = [
        "# Task.Predecessors / Task.Successors strings for corpus/project_a.mpp,",
        "# read through COM from a COPY of the file (build/w24/preds-project_a.mpp)",
        "# on 2026-10-23 with MS Project Professional 2021, 16.0.20430.20182.",
        "#",
        "# Wave 24 (W24-8): ONE HASH PER ROW, so a failure names the row:",
        "#     <ID>\\t<SHA256(Predecessors)>\\t<SHA256(Successors)>",
        "# The strings are hashed, never stored.",
        "#",
        f"# Source file SHA-256: {source_sha}",
        f"# Rows: {len(rows)}",
    ]
    for task_id, predecessors, successors in rows:
        lines.append(f"{task_id}\t{digest(predecessors)}\t{digest(successors)}")
    with open(TARGET, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"wrote {TARGET}: {len(rows)} rows, source sha256 {source_sha}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
