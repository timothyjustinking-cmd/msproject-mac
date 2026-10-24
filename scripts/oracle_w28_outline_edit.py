#!/usr/bin/env python3
"""Wave 28 (W28-2.1): Project 2021's OutlineIndent / OutlineOutdent saves.

The brief measures three outline edits on copies of tests/data/synthetic-base.mpp
and commits each before/after pair under tests/data/:

  * indent   -- one leaf (ID 2) under its preceding sibling (ID 1), so ID 1
                becomes a summary;
  * summary  -- ID 2 first made a summary by indenting ID 3 under it, then ID 2
                (with its child) indented under ID 1, so the whole subtree
                moves one level down;
  * outdent  -- the indent pair's shape, then ID 2 outdented back to the top
                level, so ID 1 stops being a summary.

Every file is saved under the neutral root C:/w28/fixtures with neutral
Author/Manager/Company/UserName/Title.  The control and the edited file of a
pair come from ONE continuous session on the same project: Project regenerates
the Fixed2Data row identities on every save, so a pair from two sessions would
differ in bytes no outline edit causes.

The COM facts before and after each edit, and on Project's own reopen of every
committed file, go to build/w28/W28-2.1-project-outline-edit.json (not
committed).  Rows are recorded by ID only; no task or project name is read,
printed or stored.

    python scripts/oracle_w28_outline_edit.py

The script refuses to run if any file of the pairs already exists, so a
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
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w28"
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures")
OUT = os.path.join(ROOT, "build", "w28")
RESULT = os.path.join(OUT, "W28-2.1-project-outline-edit.json")
WORK = os.path.join(OUT, "outline-work")

BASE_SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")

PAIRS = {
    "indent": ("wave28-outline-indent-control.mpp",
               "wave28-outline-indent.mpp"),
    "summary": ("wave28-outline-summary-control.mpp",
                "wave28-outline-summary.mpp"),
    "outdent": ("wave28-outline-outdent-control.mpp",
                "wave28-outline-outdent.mpp"),
}


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
    return {
        "row": row_index,
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "outline_level": prop(task, "OutlineLevel"),
        "outline_number": prop(task, "OutlineNumber"),
        "summary": prop(task, "Summary"),
        "manual": prop(task, "Manual"),
        "duration": prop(task, "Duration"),
        "duration_text": prop(task, "DurationText"),
        "percent_complete": prop(task, "PercentComplete"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "actual_start": prop(task, "ActualStart"),
        "actual_finish": prop(task, "ActualFinish"),
        "free_slack": prop(task, "FreeSlack"),
        "total_slack": prop(task, "TotalSlack"),
    }


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


def save_as(app, name):
    app.FileSaveAs(os.path.join(FIXTURE_DIR, name))
    return name


def reopen_facts(app, path):
    app.FileOpen(path)
    facts = all_facts(app.ActiveProject)
    app.FileClose(0)
    return facts


def main():
    for pair in PAIRS.values():
        for name in pair:
            path = os.path.join(FIXTURE_DIR, name)
            if os.path.exists(path):
                sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    if not os.path.isfile(BASE_SOURCE):
        sys.exit(f"fixture missing: {BASE_SOURCE}")
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)

    facts = {"version": None, "pairs": {}}
    with oracle_session() as app:
        with Guard(600):
            facts["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"

            # ---- indent: one leaf under its preceding sibling -------------
            src = working_copy(BASE_SOURCE, "w28-2.1-indent")
            shutil.copyfile(src, os.path.join(WORK, "indent.mpp"))
            app.FileOpen(os.path.join(WORK, "indent.mpp"))
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Outline Fixture")
            entry = facts["pairs"].setdefault("indent", {})
            entry["control"] = save_as(app, PAIRS["indent"][0])
            entry["control_facts"] = all_facts(project)
            project.Tasks(2).OutlineIndent()
            entry["edited"] = save_as(app, PAIRS["indent"][1])
            entry["edited_facts"] = all_facts(project)
            app.FileClose(0)

            # ---- summary: ID 3 under ID 2, save, then ID 2 under ID 1 ----
            src = working_copy(BASE_SOURCE, "w28-2.1-summary")
            shutil.copyfile(src, os.path.join(WORK, "summary.mpp"))
            app.FileOpen(os.path.join(WORK, "summary.mpp"))
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Outline Fixture")
            project.Tasks(3).OutlineIndent()
            entry = facts["pairs"].setdefault("summary", {})
            entry["control"] = save_as(app, PAIRS["summary"][0])
            entry["control_facts"] = all_facts(project)
            project.Tasks(2).OutlineIndent()
            entry["edited"] = save_as(app, PAIRS["summary"][1])
            entry["edited_facts"] = all_facts(project)
            app.FileClose(0)

            # ---- outdent: the indent shape, then ID 2 back to top level --
            src = working_copy(BASE_SOURCE, "w28-2.1-outdent")
            shutil.copyfile(src, os.path.join(WORK, "outdent.mpp"))
            app.FileOpen(os.path.join(WORK, "outdent.mpp"))
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Outline Fixture")
            project.Tasks(2).OutlineIndent()
            entry = facts["pairs"].setdefault("outdent", {})
            entry["control"] = save_as(app, PAIRS["outdent"][0])
            entry["control_facts"] = all_facts(project)
            project.Tasks(2).OutlineOutdent()
            entry["edited"] = save_as(app, PAIRS["outdent"][1])
            entry["edited_facts"] = all_facts(project)
            app.FileClose(0)

            # Reopen every committed file: what Project itself displays.
            for key, pair in PAIRS.items():
                entry = facts["pairs"][key]
                entry["control_reopen"] = reopen_facts(
                    app, os.path.join(FIXTURE_DIR, pair[0]))
                entry["edited_reopen"] = reopen_facts(
                    app, os.path.join(FIXTURE_DIR, pair[1]))

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for key, pair in PAIRS.items():
        for name in pair:
            path = os.path.join(FIXTURE_DIR, name)
            log(f"fixture {name} ({os.path.getsize(path)} bytes)")
    for key in PAIRS:
        entry = facts["pairs"][key]
        before = entry["control_facts"]["2"]
        after = entry["edited_facts"]["2"]
        log(f"{key}: ID2 {before['outline_number']} lvl {before['outline_level']} "
            f"-> {after['outline_number']} lvl {after['outline_level']}; "
            f"ID1 summary {entry['control_facts']['1']['summary']} -> "
            f"{entry['edited_facts']['1']['summary']}")


if __name__ == "__main__":
    main()
