#!/usr/bin/env python3
"""Wave 30 (W30-1.4): Project 2021's Move Task save, both commonest options.

Measures Application.TaskMove on the synthetic base (three top-level manual
leaves, no links, no progress, one placeholder assignment per task):

  * forward  the control is saved, sheet row 2 (ID 2) is selected and
             TaskMove(MoveForward=True, IsWorkingDuration=True, MoveDays=1)
             runs, then the file is saved.
  * back     the same shape, TaskMove(MoveForward=False, IsWorkingDuration=True,
             MoveDays=1).

One working day and the working-duration flag are the dialog defaults, the
two commonest options.  Both pairs are saved under the neutral root
C:/w30/fixtures-move with neutral Author/Manager/Company/UserName/Title, in
one continuous session per pair so Project's per-save identities are shared.

The COM facts before and after each edit, and on Project's own reopen of every
committed file, go to build/w30/W30-1.4-project-move.json (not committed).
Rows are recorded by ID only; no task or project name is read, printed or
stored.

    python -I scripts/oracle_w30_move.py

The script refuses to run if any target file already exists.
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

NEUTRAL = r"C:/w30"
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-move")
OUT = os.path.join(ROOT, "build", "w30")
RESULT = os.path.join(OUT, "W30-1.4-project-move.json")
WORK = os.path.join(OUT, "move-work")

BASE_SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")
TARGET_ID = 2

PAIRS = (
    ("forward", True, "wave30-move-forward-control.mpp",
     "wave30-move-forward.mpp"),
    ("back", False, "wave30-move-back-control.mpp",
     "wave30-move-back.mpp"),
)


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
        "summary": prop(task, "Summary"),
        "manual": prop(task, "Manual"),
        "duration": prop(task, "Duration"),
        "duration_text": prop(task, "DurationText"),
        "percent_complete": prop(task, "PercentComplete"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "constraint_type": prop(task, "ConstraintType"),
        "constraint_date": prop(task, "ConstraintDate"),
        "free_slack": prop(task, "FreeSlack"),
        "total_slack": prop(task, "TotalSlack"),
    }


def all_facts(project):
    rows = {"project": row(project.ProjectSummaryTask, 0)}
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
    return [app.GetUndoListItem(index) for index in range(1, count + 1)]


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


def measure(app, label, forward, control_name, edited_name, facts):
    work = os.path.join(WORK, f"move-{label}.mpp")
    src = working_copy(BASE_SOURCE, "move-" + label)
    shutil.copyfile(src, work)
    app.FileOpen(work)
    project = app.ActiveProject
    neutralise(app, project, f"Synthetic Move Fixture ({label})")
    entry = {
        "control": control_name,
        "edited": edited_name,
        "source": os.path.basename(BASE_SOURCE),
        "target_id": TARGET_ID,
        "move_forward": forward,
        "is_working_duration": True,
        "move_days": 1,
    }
    facts["pairs"][label] = entry
    entry["control_facts_before_save"] = all_facts(project)
    save_as(app, control_name)
    entry["control_facts"] = all_facts(project)

    select_target(app, project)
    entry["selection"] = selection_facts(app)
    entry["return"] = app.TaskMove(MoveForward=forward,
                                   IsWorkingDuration=True, MoveDays=1)
    entry["undo"] = undo_list(app)
    entry["edited_facts_before_save"] = all_facts(project)
    save_as(app, edited_name)
    entry["edited_facts"] = all_facts(project)
    app.FileClose(0)

    for key, name in (("control", control_name), ("edited", edited_name)):
        entry[key + "_reopen"] = reopen_facts(
            app, os.path.join(FIXTURE_DIR, name))


def main():
    for _label, _forward, control_name, edited_name in PAIRS:
        for name in (control_name, edited_name):
            path = os.path.join(FIXTURE_DIR, name)
            if os.path.exists(path):
                sys.exit(f"refusing: {path} already exists "
                         "(inspect the pair first)")
    if not os.path.isfile(BASE_SOURCE):
        sys.exit(f"fixture missing: {BASE_SOURCE}")
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)

    facts = {"version": None, "target_id": TARGET_ID, "pairs": {}}
    with oracle_session() as app:
        with Guard(900):
            facts["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"
            for label, forward, control_name, edited_name in PAIRS:
                measure(app, label, forward, control_name, edited_name, facts)

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for label, _forward, control_name, edited_name in PAIRS:
        entry = facts["pairs"][label]
        for name in (control_name, edited_name):
            path = os.path.join(FIXTURE_DIR, name)
            log(f"{label} {name}: {os.path.getsize(path)} bytes")
        log(f"{label} selection {entry['selection']} "
            f"return {entry['return']!r} undo {entry['undo']}")
        for tag in ("control_facts", "edited_facts"):
            log(f"{label} {tag}")
            for index in sorted(entry[tag],
                                key=lambda k: (k != "project",
                                               int(k) if k != "project" else -1)):
                item = entry[tag][index]
                if index == "project":
                    log(f"  project start {item['start']} "
                        f"finish {item['finish']} "
                        f"duration {item['duration']}")
                    continue
                log(f"  row {index}: id {item['id']} manual {item['manual']} "
                    f"duration {item['duration']} start {item['start']} "
                    f"finish {item['finish']} constraint "
                    f"{item['constraint_type']} {item['constraint_date']}")


if __name__ == "__main__":
    main()
