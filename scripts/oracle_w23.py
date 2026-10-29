#!/usr/bin/env python3
"""Wave-23 oracle experiments (coordinator only; needs MS Project 2021).

    python scripts/oracle_w23.py

Two jobs in one Project session:

1. The W23 fixture: a small client-free plan with a 3-level outline, one FS+lag
   link, one SS link, one FF link and one synthetic resource ("Resource A")
   assigned.  Project saves it to build/w23/; the coordinator copies it to
   tests/data/ only after check_fixtures.py records its hash.
2. The project-summary-row flag: synthetic-base.mpp saved twice with the
   "Project Summary Task" display setting off and on, so the stored view state
   can be diffed to locate the flag.

It also reads Task.Predecessors strings (the W23-7 lag-label oracle) and the
Project version, and writes them to build/w23/oracle-w23.json.  Nothing client
specific is read or written.
"""

import json
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log, project_pids  # noqa: E402
from oracle_privacy import neutralise  # noqa: E402
from oracle_w3 import Guard, utc  # noqa: E402

OUT = os.path.join(ROOT, "build", "w23")
os.makedirs(OUT, exist_ok=True)


def prop(obj, name, default=None):
    try:
        return getattr(obj, name)
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def set_prop(obj, name, value):
    try:
        setattr(obj, name, value)
        return True
    except Exception as exc:  # noqa: BLE001
        log(f"could not set {name}={value!r}: {exc}")
        return False


def probe_timeline(project):
    """Best-effort read of the Timeline object's visibility members."""
    info = {}
    try:
        timeline = project.Timeline
    except Exception as exc:  # noqa: BLE001
        return {"timeline": f"<unreadable: {exc}>"}
    for name in ("Show", "Visible", "IsVisible", "ShowTimeline"):
        try:
            info[name] = getattr(timeline, name)
        except Exception as exc:  # noqa: BLE001
            info[name] = f"<unreadable: {exc}>"
    return info


def read_links(project):
    rows = []
    for task in project.Tasks:
        try:
            if task is None:
                continue
            rows.append({"id": task.ID, "name": task.Name,
                         "pred": task.Predecessors, "succ": task.Successors,
                         "outline": task.OutlineLevel})
        except Exception:  # noqa: BLE001
            continue
    return rows


def build_fixture(app, project, path):
    """A 3-level outline, three link types, one resource."""
    project.ProjectStart = utc(__import__("datetime").datetime(2026, 11, 2, 8, 0))
    project.ProjectFinish = utc(__import__("datetime").datetime(2026, 12, 18, 17, 0))
    names = ["Summary A", "Sub A1", "Leaf A1a", "Leaf A1b", "Sub A2", "Final Task"]
    for name in names:
        task = project.Tasks.Add(name)
        task.Duration = 2 * 8 * 60  # minutes
    tasks = {t.Name: t for t in project.Tasks if t is not None}
    # Indent the hierarchy: Sub A1 and Sub A2 under Summary A, leaves under Sub A1.
    # COM's OutlineIndent() indents one level and takes no argument.
    tasks["Sub A1"].OutlineIndent()
    tasks["Leaf A1a"].OutlineIndent()
    tasks["Leaf A1a"].OutlineIndent()
    tasks["Leaf A1b"].OutlineIndent()
    tasks["Leaf A1b"].OutlineIndent()
    tasks["Sub A2"].OutlineIndent()
    # Three link types through each successor's dependency collection.
    # PjTaskLinkType: 0=FF, 1=FS, 2=SF, 3=SS; Lag is in minutes.
    tasks["Leaf A1b"].TaskDependencies.Add(tasks["Leaf A1a"], 1, 2 * 8 * 60)  # FS + 2 d
    tasks["Sub A2"].TaskDependencies.Add(tasks["Leaf A1a"], 3, 8 * 60)        # SS + 1 d
    tasks["Final Task"].TaskDependencies.Add(tasks["Leaf A1b"], 0, 0)         # FF
    resource = project.Resources.Add("Resource A")
    tasks["Final Task"].Assignments.Add(ResourceID=resource.ID)
    neutralise(app, project, "Synthetic Outline Fixture")
    app.FileSaveAs(path)
    return read_links(project)


def main():
    summary_off = os.path.join(OUT, "summary-off.mpp")
    summary_on = os.path.join(OUT, "summary-on.mpp")
    fixture = os.path.join(OUT, "wave23-outline.mpp")
    base_copy = os.path.join(OUT, "summary-base.mpp")
    shutil.copyfile(os.path.join(ROOT, "tests", "data", "synthetic-base.mpp"), base_copy)

    result = {"version": None, "summary": {}, "links": [], "fixture_links": []}
    with oracle_session() as app:
        result["version"] = prop(app, "Version")
        with Guard(240):
            # --- the project-summary flag pair -------------------------------
            app.FileOpen(base_copy)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Summary Fixture")
            for label, value, target in (("off", False, summary_off),
                                         ("on", True, summary_on)):
                result["summary"][f"display_read_before_{label}"] = prop(
                    project, "DisplayProjectSummaryTask")
                result["summary"][f"set_{label}"] = set_prop(
                    project, "DisplayProjectSummaryTask", value)
                result["summary"][f"project_setting_{label}"] = prop(
                    project, "DisplayProjectSummaryTask")
                result["summary"][f"timeline_{label}"] = probe_timeline(project)
                app.FileSaveAs(target)
            app.FileClose(0)

            # --- the 3-level fixture -----------------------------------------
            app.FileNew()
            project = app.ActiveProject
            result["summary"]["new_project_setting"] = prop(
                project, "DisplayProjectSummaryTask")
            result["summary"]["new_project_timeline"] = probe_timeline(project)
            result["fixture_links"] = build_fixture(app, project, fixture)
            app.FileClose(0)

        result["files"] = {name: os.path.exists(path) for name, path in
                           (("summary_off", summary_off), ("summary_on", summary_on),
                            ("fixture", fixture))}

    with open(os.path.join(OUT, "oracle-w23.json"), "w", encoding="utf-8") as fh:
        json.dump(result, fh, indent=2, default=str)
    print(json.dumps(result, indent=2, default=str))


if __name__ == "__main__":
    main()
