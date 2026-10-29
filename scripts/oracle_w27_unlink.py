#!/usr/bin/env python3
"""Wave-27 W27-3.3 oracle: Project's Task > Unlink Tasks, measured.

    python scripts/oracle_w27_unlink.py

Opens a neutral copy of tests/data/wave23-outline.mpp (a synthetic fixture,
not client data) under C:/w27, saves an unedited control from the same
session, then selects the one task with BOTH a predecessor and a successor
(UniqueID 4 in that fixture: 3 -> 4 FS+2d, 4 -> 6 FF) and calls Project's
Application.UnlinkTasks.  It records ActiveProject.TaskDependencies before,
after and after reopen, and saves the edited file as the fixture the port's
own Unlink save is compared with.

The fixture pair carries neutral metadata (UserName/Author/Manager/Company/
Title) and lives under C:/w27.  Facts go to build/w27/unlink-facts.json; the
two saves are copied to tests/data/ only after a deliberate review of the
stream diff (see build/w27/diff-unlink-*.txt).

usage: python scripts/oracle_w27_unlink.py
"""

import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log  # noqa: E402
from oracle_privacy import neutralise  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w27"
OUT = os.path.join(ROOT, "build", "w27")
FIXTURES = os.path.join(OUT, "fixtures")
os.makedirs(NEUTRAL, exist_ok=True)
os.makedirs(FIXTURES, exist_ok=True)

BASE = os.path.join(NEUTRAL, "wave27-unlink-base.mpp")
CONTROL = os.path.join(FIXTURES, "wave27-unlink-control.mpp")
UNLINKED = os.path.join(FIXTURES, "wave27-unlink.mpp")

SELECTED_UID = 4  # the fixture's task with a predecessor (3) and a successor (6)


def prop(obj, name, default=None):
    try:
        return getattr(obj, name)
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def task_dependencies(task):
    """One task's TaskDependencies collection as dicts."""
    rows = []
    collection = task.TaskDependencies
    for index in range(1, collection.Count + 1):
        dep = collection(index)
        rows.append(
            {
                "from_uid": prop(dep.From, "UniqueID"),
                "from_id": prop(dep.From, "ID"),
                "to_uid": prop(dep.To, "UniqueID"),
                "to_id": prop(dep.To, "ID"),
                "type": prop(dep, "Type"),
                "lag": prop(dep, "Lag"),
            }
        )
    return rows


def dependencies(project):
    """Every project dependency once, keyed (FromUniqueID, ToUniqueID).

    Task.TaskDependencies is the per-task collection (the project object has no
    TaskDependencies property in this typelib), so the project-wide set is the
    union over every task, de-duplicated by the dependency's two ends.
    """
    seen = {}
    for task in project.Tasks:
        for dep in task_dependencies(task):
            seen[(dep["from_uid"], dep["to_uid"])] = dep
    return [seen[key] for key in sorted(seen)]


def selected_dependencies(project, uid):
    """The SELECTED task's own TaskDependencies count and rows."""
    task = project.Tasks.UniqueID(uid)
    return {"count": task.TaskDependencies.Count, "rows": task_dependencies(task)}


def main():
    shutil.copyfile(os.path.join(ROOT, "tests", "data", "wave23-outline.mpp"), BASE)
    facts = {"version": None, "selected_uid": SELECTED_UID, "unlink": {}}
    with oracle_session() as app:
        with Guard(300):
            facts["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"

            app.FileOpen(BASE)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Unlink Fixture")
            facts["unlink"]["before"] = dependencies(project)
            facts["unlink"]["before_count"] = len(facts["unlink"]["before"])
            facts["unlink"]["selected_before"] = selected_dependencies(
                project, SELECTED_UID)
            facts["unlink"]["task_before"] = {
                "uid": prop(project.Tasks.UniqueID(SELECTED_UID), "UniqueID"),
                "id": prop(project.Tasks.UniqueID(SELECTED_UID), "ID"),
                "predecessors": prop(project.Tasks.UniqueID(SELECTED_UID), "Predecessors"),
                "successors": prop(project.Tasks.UniqueID(SELECTED_UID), "Successors"),
            }
            # Same-session control: the unedited save, so the pair isolates the
            # bytes UnlinkTasks changes from Project's per-save bookkeeping.
            app.FileSaveAs(CONTROL)

            # Task > Unlink Tasks acts on the SELECTION.  EditGoTo(ID) selects
            # the task by its stored ID (which is also its UniqueID here).
            app.EditGoTo(SELECTED_UID)
            app.UnlinkTasks()
            facts["unlink"]["after"] = dependencies(project)
            facts["unlink"]["after_count"] = len(facts["unlink"]["after"])
            facts["unlink"]["selected_after"] = selected_dependencies(
                project, SELECTED_UID)
            facts["unlink"]["task_after"] = {
                "predecessors": prop(project.Tasks.UniqueID(SELECTED_UID), "Predecessors"),
                "successors": prop(project.Tasks.UniqueID(SELECTED_UID), "Successors"),
            }
            app.FileSaveAs(UNLINKED)
            app.FileClose(0)

            app.FileOpen(UNLINKED)
            project = app.ActiveProject
            facts["unlink"]["reopen"] = dependencies(project)
            facts["unlink"]["reopen_count"] = len(facts["unlink"]["reopen"])
            facts["unlink"]["selected_reopen"] = selected_dependencies(
                project, SELECTED_UID)
            facts["unlink"]["task_reopen"] = {
                "predecessors": prop(project.Tasks.UniqueID(SELECTED_UID), "Predecessors"),
                "successors": prop(project.Tasks.UniqueID(SELECTED_UID), "Successors"),
            }
            app.FileClose(0)

    with open(os.path.join(OUT, "unlink-facts.json"), "w", encoding="utf-8") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
    log("wrote build/w27/unlink-facts.json")
    for path in (CONTROL, UNLINKED):
        log(f"fixture {path} ({os.path.getsize(path)} bytes)")


if __name__ == "__main__":
    main()
