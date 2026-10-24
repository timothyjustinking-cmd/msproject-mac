#!/usr/bin/env python3
"""Wave 28 (W28-5.1): Project 2021's ONE split on a leaf task.

The brief measures Project's Split Task on a leaf and asks whether the bytes it
writes are within the port's writer.  `Application.SplitTask` is interactive
(it waits for a split date and wedges a COM caller), so the split is expressed
through the object model the UI itself updates: `Task.SplitParts(1).Finish`
moves the end of the first working part, then `Task.SplitParts.Add(start, end)`
writes the resumed part.  On an automatic leaf ID 1 (5 days, Mon 2026-01-05
08:00 -> Fri 2026-01-09 17:00) this is one split: part 1 ends Wed 2026-01-07
17:00, part 2 is Fri 2026-01-09 08:00 -> Mon 2026-01-12 17:00, the duration
stays 2400 min and the finish moves to Monday.

The control and the edited file come from ONE continuous session: Project
regenerates Fixed2Data row identities on every save, so a pair from two
sessions would differ in bytes no split causes.  Both are saved under the
neutral root C:/w28/fixtures with neutral Author/Manager/Company/UserName/Title,
then reopened through Project so the stored result is what Project itself
displays.

The COM facts before and after the split, and on Project's own reopen of both
committed files, go to build/w28/W28-5.1-project-split.json (not committed).
Rows are recorded by ID only; no task or project name is read, printed or
stored.

    python scripts/oracle_w28_split.py

The script refuses to run if either file of the pair already exists, so a
half-finished session cannot silently leave a mixed pair.
"""

import datetime as dt
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log, working_copy  # noqa: E402
from oracle_w3 import Guard, utc  # noqa: E402

NEUTRAL = r"C:/w28"
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures")
OUT = os.path.join(ROOT, "build", "w28")
RESULT = os.path.join(OUT, "W28-5.1-project-split.json")
WORK = os.path.join(OUT, "split-work")

BASE_SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")

PAIR = ("wave28-split-control.mpp", "wave28-split.mpp")


def val(v):
    if isinstance(v, dt.datetime):
        return v.strftime("%Y-%m-%d %H:%M:%S")
    if isinstance(v, (int, float, bool, str)) or v is None:
        return v
    return repr(v)


def prop(obj, name):
    try:
        return val(getattr(obj, name))
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def row(task, row_index):
    item = {
        "row": row_index,
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "summary": prop(task, "Summary"),
        "manual": prop(task, "Manual"),
        "duration": prop(task, "Duration"),
        "duration_text": prop(task, "DurationText"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "stop": prop(task, "Stop"),
        "resume": prop(task, "Resume"),
        "actual_start": prop(task, "ActualStart"),
        "actual_finish": prop(task, "ActualFinish"),
        "percent_complete": prop(task, "PercentComplete"),
    }
    try:
        parts = task.SplitParts
        item["split_parts"] = {
            "count": parts.Count,
            "parts": [{"index": parts(i).Index,
                       "start": val(parts(i).Start),
                       "finish": val(parts(i).Finish)}
                      for i in range(1, parts.Count + 1)],
        }
    except Exception as exc:  # noqa: BLE001
        item["split_parts"] = f"<unreadable: {exc}>"
    return item


def all_facts(project):
    rows = {"project": row(project.ProjectSummaryTask, 0)}
    for index in range(1, 4):
        rows[str(index)] = row(project.Tasks(index), index)
    return rows


def neutralise(app, project, title):
    app.UserName = "Fixture Builder"
    project.Author = "Fixture Builder"
    project.Manager = "Fixture Builder"
    project.Company = "Fixture Company"
    project.Title = title


def reopen_facts(app, path):
    app.FileOpen(path)
    facts = all_facts(app.ActiveProject)
    app.FileClose(0)
    return facts


def main():
    for name in PAIR:
        path = os.path.join(FIXTURE_DIR, name)
        if os.path.exists(path):
            sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    if not os.path.isfile(BASE_SOURCE):
        sys.exit(f"fixture missing: {BASE_SOURCE}")
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)

    facts = {"version": None, "pair": {}}
    with oracle_session() as app:
        with Guard(600):
            facts["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"

            src = working_copy(BASE_SOURCE, "w28-5.1-split")
            shutil.copyfile(src, os.path.join(WORK, "split.mpp"))
            app.FileOpen(os.path.join(WORK, "split.mpp"))
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Split Fixture")

            task = project.Tasks(1)
            task.Manual = False
            entry = facts["pair"]
            entry["control"] = PAIR[0]
            app.FileSaveAs(os.path.join(FIXTURE_DIR, PAIR[0]))
            entry["control_facts"] = all_facts(project)

            task.SplitParts(1).Finish = utc(dt.datetime(2026, 1, 7, 17, 0))
            task.SplitParts.Add(utc(dt.datetime(2026, 1, 9, 8, 0)),
                                utc(dt.datetime(2026, 1, 12, 17, 0)))
            entry["edited"] = PAIR[1]
            app.FileSaveAs(os.path.join(FIXTURE_DIR, PAIR[1]))
            entry["edited_facts"] = all_facts(project)
            app.FileClose(0)

            # Reopen every committed file: what Project itself displays.
            entry["control_reopen"] = reopen_facts(
                app, os.path.join(FIXTURE_DIR, PAIR[0]))
            entry["edited_reopen"] = reopen_facts(
                app, os.path.join(FIXTURE_DIR, PAIR[1]))

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for name in PAIR:
        path = os.path.join(FIXTURE_DIR, name)
        log(f"fixture {name} ({os.path.getsize(path)} bytes)")
    before = facts["pair"]["control_facts"]["1"]
    after = facts["pair"]["edited_facts"]["1"]
    log(f"ID1 {before['duration_text']} {before['start']} -> {before['finish']} "
        f"parts {before['split_parts']['count']} -> ID1 {after['duration_text']} "
        f"{after['start']} -> {after['finish']} parts "
        f"{after['split_parts']['count']}")


if __name__ == "__main__":
    main()
