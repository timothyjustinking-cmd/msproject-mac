#!/usr/bin/env python3
"""Wave 30 (W30-1.3): Project 2021's Respect Links save.

Measures what Project's own Respect Links command writes.  Two linked shapes
are measured, each from a committed fixture copied under C:/w30:

  manual  tests/data/wave23-outline.mpp -- six tasks, ALL manually scheduled,
          three links (uid 3 -> 4 FS+2 d, 3 -> 5 SS+1 d, 4 -> 6 FF), one
          resource; the target ID 4 has a predecessor and a successor.
  auto    tests/data/wave25-auto.mpp -- the same project with every task
          automatically scheduled.

For each shape the session saves the control with neutral metadata, selects
the target's sheet row, calls Application.TaskRespectLinks() -- the ribbon
button's no-argument command, which operates on the selection (the Task
object has no RespectLinks property) -- and saves.  The call is made twice:
the undo list proves each call ran, and the two saves show that the command
writes no task-level byte for a link-satisfied row (it is a one-shot
reschedule action, not a stored flag).

All COM facts are recorded by ID/UniqueID only: no task or resource name is
read, printed or stored.

    python -I scripts/oracle_w30_respect_links.py

The script refuses to run if any target file already exists.
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

NEUTRAL = r"C:/w30"
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-respect")
OUT = os.path.join(ROOT, "build", "w30")
RESULT = os.path.join(OUT, "W30-1.3-project-respect-links.json")
WORK = os.path.join(OUT, "respect-work")

TARGET_ID = 4

SHAPES = (
    ("manual", os.path.join(ROOT, "tests", "data", "wave23-outline.mpp"),
     "wave30-respect-links-manual"),
    ("auto", os.path.join(ROOT, "tests", "data", "wave25-auto.mpp"),
     "wave30-respect-links-auto"),
)


def prop(obj, name):
    try:
        value = getattr(obj, name)
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"
    if hasattr(value, "strftime"):
        return value.strftime("%Y-%m-%d %H:%M:%S")
    if isinstance(value, (int, float, bool, str)) or value is None:
        return value
    return repr(value)


def row(task, row_index):
    return {
        "saved_row": row_index,
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "manual": prop(task, "Manual"),
        "summary": prop(task, "Summary"),
        "milestone": prop(task, "Milestone"),
        "duration": prop(task, "Duration"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "percent_complete": prop(task, "PercentComplete"),
        "outline_level": prop(task, "OutlineLevel"),
        "outline_number": prop(task, "OutlineNumber"),
    }


def all_facts(project):
    rows = {"project": {
        "id": prop(project.ProjectSummaryTask, "ID"),
        "tasks": project.Tasks.Count,
    }}
    for index in range(1, project.Tasks.Count + 1):
        rows[str(index)] = row(project.Tasks(index), index)
    return rows


def selection_facts(app):
    try:
        tasks = app.ActiveSelection.Tasks
        return [tasks(index).ID for index in range(1, tasks.Count + 1)]
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def undo_list(app):
    try:
        count = app.GetUndoListCount()
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"
    items = []
    for index in range(1, count + 1):
        items.append(app.GetUndoListItem(index))
    return items


def save_as(app, name):
    app.FileSaveAs(os.path.join(FIXTURE_DIR, name))
    return name


def reopen_facts(app, path):
    app.FileOpen(path)
    facts = all_facts(app.ActiveProject)
    app.FileClose(0)
    return facts


def select_target(app, project):
    target = None
    for index in range(1, project.Tasks.Count + 1):
        candidate = project.Tasks(index)
        if candidate.ID == TARGET_ID:
            target = candidate
            break
    if target is None:
        sys.exit(f"target ID {TARGET_ID} not found")
    app.SelectTaskField(Row=target.ID, Column="Name", RowRelative=False)
    return target


def measure(app, label, source, base, facts):
    work = os.path.join(WORK, f"respect-links-{label}.mpp")
    shutil.copyfile(source, work)
    app.FileOpen(work)
    project = app.ActiveProject
    neutralise(app, project, f"Synthetic Respect Links Fixture ({label})")
    shape = {
        "control": base + "-control.mpp",
        "after_first": base + "-after-first.mpp",
        "after_second": base + "-after-second.mpp",
        "source": os.path.basename(source),
    }
    facts["shapes"][label] = shape
    shape["control_facts_before_save"] = all_facts(project)
    save_as(app, shape["control"])
    shape["control_facts"] = all_facts(project)

    target = select_target(app, project)
    shape["target"] = {
        "id": prop(target, "ID"),
        "unique_id": prop(target, "UniqueID"),
        "manual_before": prop(target, "Manual"),
    }
    shape["selection_before_first"] = selection_facts(app)
    shape["first_return"] = app.TaskRespectLinks()
    shape["undo_after_first"] = undo_list(app)
    shape["after_first_facts_before_save"] = all_facts(project)
    save_as(app, shape["after_first"])
    shape["after_first_facts"] = all_facts(project)

    select_target(app, project)
    shape["selection_before_second"] = selection_facts(app)
    shape["second_return"] = app.TaskRespectLinks()
    shape["undo_after_second"] = undo_list(app)
    shape["after_second_facts_before_save"] = all_facts(project)
    save_as(app, shape["after_second"])
    shape["after_second_facts"] = all_facts(project)
    app.FileClose(0)

    for key in ("control", "after_first", "after_second"):
        shape[key + "_reopen"] = reopen_facts(
            app, os.path.join(FIXTURE_DIR, shape[key]))


def main():
    wanted = []
    for label, source, base in SHAPES:
        wanted.extend(base + suffix for suffix in
                      ("-control.mpp", "-after-first.mpp",
                       "-after-second.mpp"))
        if not os.path.isfile(source):
            sys.exit(f"fixture missing: {source}")
    for name in wanted:
        path = os.path.join(FIXTURE_DIR, name)
        if os.path.exists(path):
            sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)

    facts = {"version": None, "target_id": TARGET_ID, "shapes": {}}
    with oracle_session() as app:
        with Guard(900):
            facts["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"
            for label, source, base in SHAPES:
                measure(app, label, source, base, facts)

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for label, _source, base in SHAPES:
        shape = facts["shapes"][label]
        for key in ("control", "after_first", "after_second"):
            path = os.path.join(FIXTURE_DIR, shape[key])
            log(f"{label} {key}: {shape[key]} "
                f"({os.path.getsize(path)} bytes)")
        log(f"{label} target {shape['target']} "
            f"selection {shape['selection_before_first']} "
            f"first_return {shape['first_return']!r} "
            f"second_return {shape['second_return']!r}")
        log(f"{label} undo after first {shape['undo_after_first']} "
            f"after second {shape['undo_after_second']}")
        for tag in ("control_facts", "after_first_facts",
                    "after_second_facts"):
            log(f"{label} {tag}")
            for index in sorted(shape[tag],
                                key=lambda k: (k != "project",
                                               int(k) if k != "project" else -1)):
                item = shape[tag][index]
                if index == "project":
                    log(f"  project {item}")
                    continue
                log(f"  row {index}: id {item['id']} uid {item['unique_id']} "
                    f"manual {item['manual']} level {item['outline_level']} "
                    f"duration {item['duration']} start {item['start']} "
                    f"finish {item['finish']}")


if __name__ == "__main__":
    main()
