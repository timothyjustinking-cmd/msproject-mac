#!/usr/bin/env python3
"""Wave 29 (W29-0.4): OutlineIndent / OutlineOutdent on AUTOMATIC rows.

The wave-28 outline pairs are all manually scheduled rows.  This script
measures the automatic-row shapes the app still refuses:

  * indent_auto          two top-level automatic leaves; ID 2 indented under
                         its preceding sibling ID 1, which becomes a summary;
  * summary_auto         all rows automatic; ID 3 first indented under ID 2
                         (ID 2 becomes a summary), then ID 2 with its child
                         indented under ID 1 (the summary-making indent);
  * outdent_auto         the indent_auto shape, then ID 2 outdented back to
                         the top level;
  * outdent_manual_parent ID 2 is automatic and its parent ID 1 is manual
                         (the base's scheduling mode), then ID 2 is outdented.

Every file is saved under the neutral root C:/w29/fixtures-outline with
neutral Author/Manager/Company/UserName/Title.  Each pair's control and
edited file come from ONE continuous session on the same project (Project
regenerates the Fixed2Data row identities on every save).

The COM facts before and after each edit, and on Project's own reopen of every
committed file, go to build/w29/W29-0.4-project-outline-auto.json (not
committed).  Rows are recorded by ID only; no task or project name is read,
printed or stored.

    python scripts/oracle_w29_outline_auto.py

The script refuses to run if any file of the pairs already exists.
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

NEUTRAL = r"C:/w29"
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-outline")
OUT = os.path.join(ROOT, "build", "w29")
RESULT = os.path.join(OUT, "W29-0.4-project-outline-auto.json")
WORK = os.path.join(OUT, "outline-work")

BASE_SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")

PAIRS = {
    "indent_auto": ("wave29-outline-auto-indent-control.mpp",
                    "wave29-outline-auto-indent.mpp"),
    "summary_auto": ("wave29-outline-auto-summary-control.mpp",
                     "wave29-outline-auto-summary.mpp"),
    "outdent_auto": ("wave29-outline-auto-outdent-control.mpp",
                     "wave29-outline-auto-outdent.mpp"),
    "outdent_manual_parent": ("wave29-outline-manual-parent-control.mpp",
                              "wave29-outline-manual-parent.mpp"),
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


def set_manual(project, manual_by_id):
    for task_id, manual in manual_by_id.items():
        project.Tasks(task_id).Manual = manual


def open_base(app, tag):
    src = working_copy(BASE_SOURCE, tag)
    shutil.copyfile(src, os.path.join(WORK, tag + ".mpp"))
    app.FileOpen(os.path.join(WORK, tag + ".mpp"))
    project = app.ActiveProject
    neutralise(app, project, "Synthetic Outline Fixture")
    return project


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

            # ---- indent_auto: every row automatic --------------------------
            project = open_base(app, "w29-0.4-indent")
            set_manual(project, {1: False, 2: False, 3: False})
            entry = facts["pairs"].setdefault("indent_auto", {})
            entry["control"] = save_as(app, PAIRS["indent_auto"][0])
            entry["control_facts"] = all_facts(project)
            project.Tasks(2).OutlineIndent()
            entry["edited"] = save_as(app, PAIRS["indent_auto"][1])
            entry["edited_facts"] = all_facts(project)
            app.FileClose(0)

            # ---- summary_auto: ID 3 under ID 2, then ID 2 under ID 1 ------
            project = open_base(app, "w29-0.4-summary")
            set_manual(project, {1: False, 2: False, 3: False})
            project.Tasks(3).OutlineIndent()
            entry = facts["pairs"].setdefault("summary_auto", {})
            entry["control"] = save_as(app, PAIRS["summary_auto"][0])
            entry["control_facts"] = all_facts(project)
            project.Tasks(2).OutlineIndent()
            entry["edited"] = save_as(app, PAIRS["summary_auto"][1])
            entry["edited_facts"] = all_facts(project)
            app.FileClose(0)

            # ---- outdent_auto: the indent shape, then ID 2 back out -------
            project = open_base(app, "w29-0.4-outdent")
            set_manual(project, {1: False, 2: False, 3: False})
            project.Tasks(2).OutlineIndent()
            entry = facts["pairs"].setdefault("outdent_auto", {})
            entry["control"] = save_as(app, PAIRS["outdent_auto"][0])
            entry["control_facts"] = all_facts(project)
            project.Tasks(2).OutlineOutdent()
            entry["edited"] = save_as(app, PAIRS["outdent_auto"][1])
            entry["edited_facts"] = all_facts(project)
            app.FileClose(0)

            # ---- outdent_manual_parent: ID 1 manual, ID 2 automatic -------
            project = open_base(app, "w29-0.4-manual-parent")
            set_manual(project, {1: True, 2: False})
            project.Tasks(2).OutlineIndent()
            entry = facts["pairs"].setdefault("outdent_manual_parent", {})
            entry["control"] = save_as(app, PAIRS["outdent_manual_parent"][0])
            entry["control_facts"] = all_facts(project)
            project.Tasks(2).OutlineOutdent()
            entry["edited"] = save_as(app, PAIRS["outdent_manual_parent"][1])
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
            f"manual {before['manual']} -> {after['outline_number']} lvl "
            f"{after['outline_level']} manual {after['manual']}; "
            f"ID1 summary {entry['control_facts']['1']['summary']} manual "
            f"{entry['control_facts']['1']['manual']} -> "
            f"{entry['edited_facts']['1']['summary']} manual "
            f"{entry['edited_facts']['1']['manual']}")


if __name__ == "__main__":
    main()
