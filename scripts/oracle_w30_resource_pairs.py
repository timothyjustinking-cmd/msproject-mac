#!/usr/bin/env python3
"""Wave 30 (G2): the Resource-tab save pairs.

Measures Project 2021's own saves for the group's write commands, on copies of
the committed synthetic fixtures under the neutral root C:/w30:

  resource_add    synthetic-base -> Resources.Add("R2"):  a Work resource row.
  assign_add      resource_add's save -> Tasks(1).Assignments.Add(R2).
  assign_delete   assign_add's save -> the R2 assignment's Delete().
  rename          resource_add's save -> R2.Name changed (one field).
  notes           resource_add's save -> R2.Notes set to one plain ASCII line.

Level All is measured separately by scripts/oracle_w30_level.py on a fresh
AUTOMATIC project: Project's leveler does not move the synthetic base's manual
rows (measured -- the first attempt saved byte-identically), so this script
does not carry a level pair.

Each pair is two files saved in ONE Project session (control, then the edit) so
Project's per-save identities are shared; a later no-edit save of each control
(the -null file) supplies the noise mask (tests/save_pair_noise.h).  Every file
is saved with neutral Author/Manager/Company/UserName/Title.  Rows are recorded
by ID only.

    python -I scripts/oracle_w30_resource_pairs.py

The script refuses to run if any target fixture already exists.
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
from oracle_privacy import neutralise  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w30"
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-g2")
OUT = os.path.join(ROOT, "build", "w30")
RESULT = os.path.join(OUT, "W30-2-resource-pairs.json")
WORK = os.path.join(OUT, "g2-work")

BASE_SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")

PAIRS = {
    "resource_add": ("wave30-resource-add-control.mpp", "wave30-resource-add.mpp"),
    "assign_add": ("wave30-assign-add-control.mpp", "wave30-assign-add.mpp"),
    "assign_delete": ("wave30-assign-delete-control.mpp", "wave30-assign-delete.mpp"),
    "rename": ("wave30-resource-rename-control.mpp", "wave30-resource-rename.mpp"),
    "notes": ("wave30-resource-notes-control.mpp", "wave30-resource-notes.mpp"),
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


def resource_facts(resource, index):
    return {
        "index": index,
        "id": prop(resource, "ID"),
        "unique_id": prop(resource, "UniqueID"),
        "type": prop(resource, "Type"),
        "max_units": prop(resource, "MaxUnits"),
        "start": prop(resource, "Start"),
        "finish": prop(resource, "Finish"),
        "work": prop(resource, "Work"),
        "overallocated": prop(resource, "Overallocated"),
        "peak_units": prop(resource, "PeakUnits"),
        "notes_len": len(str(prop(resource, "Notes"))),
    }


def assignment_facts(assignment, index):
    return {
        "index": index,
        "task_id": prop(assignment, "TaskID"),
        "task_unique_id": prop(assignment, "TaskUniqueID"),
        "resource_id": prop(assignment, "ResourceID"),
        "resource_unique_id": prop(assignment, "ResourceUniqueID"),
        "units": prop(assignment, "Units"),
        "work": prop(assignment, "Work"),
        "start": prop(assignment, "Start"),
        "finish": prop(assignment, "Finish"),
        "leveling_delay": prop(assignment, "LevelingDelay"),
    }


def task_facts(task, index):
    return {
        "index": index,
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "summary": prop(task, "Summary"),
        "manual": prop(task, "Manual"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "duration": prop(task, "Duration"),
        "leveling_delay": prop(task, "LevelingDelay"),
        "overallocated": prop(task, "Overallocated"),
    }


def all_facts(project):
    facts = {
        "resource_count": project.Resources.Count,
        "task_count": project.Tasks.Count,
        "assignment_count": project.Resources.Count and sum(
            project.Tasks(i).Assignments.Count for i in range(1, project.Tasks.Count + 1)),
        "overallocated_resources": prop(project, "OverallocatedResources"),
        "resources": {},
        "tasks": {},
        "assignments": {},
    }
    for index in range(1, project.Resources.Count + 1):
        facts["resources"][str(index)] = resource_facts(project.Resources(index), index)
    for index in range(1, project.Tasks.Count + 1):
        facts["tasks"][str(index)] = task_facts(project.Tasks(index), index)
    flat = 0
    for index in range(1, project.Tasks.Count + 1):
        task = project.Tasks(index)
        for a in range(1, task.Assignments.Count + 1):
            flat += 1
            facts["assignments"][str(flat)] = assignment_facts(
                task.Assignments(a), flat)
    return facts


def open_copy(app, source, tag, title):
    os.makedirs(WORK, exist_ok=True)
    copy = os.path.join(WORK, tag + ".mpp")
    shutil.copyfile(source, copy)
    app.FileOpen(copy)
    project = app.ActiveProject
    neutralise(app, project, title)
    return project


def save(app, name):
    path = os.path.join(FIXTURE_DIR, name)
    if os.path.exists(path):
        os.remove(path)
    app.FileSaveAs(path)
    return name


def reopen_facts(app, name):
    app.FileOpen(os.path.join(FIXTURE_DIR, name))
    facts = all_facts(app.ActiveProject)
    app.FileClose(0)
    return facts


def main():
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    for pair in PAIRS.values():
        for name in pair:
            path = os.path.join(FIXTURE_DIR, name)
            if os.path.exists(path):
                sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    if not os.path.isfile(BASE_SOURCE):
        sys.exit(f"fixture missing: {BASE_SOURCE}")

    facts = {"version": None, "pairs": {}}
    with oracle_session() as app:
        with Guard(1800):
            facts["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"

            # ---- resource_add --------------------------------------------
            project = open_copy(app, BASE_SOURCE, "g2-resource", "Synthetic G2 Fixture")
            entry = facts["pairs"].setdefault("resource_add", {})
            entry["control_facts"] = all_facts(project)
            entry["control"] = save(app, PAIRS["resource_add"][0])
            resource = project.Resources.Add("R2")
            entry["edit_facts_before_save"] = all_facts(project)
            entry["added_resource"] = resource_facts(resource, project.Resources.Count)
            entry["edited"] = save(app, PAIRS["resource_add"][1])
            entry["edited_facts"] = all_facts(project)
            app.FileClose(0)

            resource_add_path = os.path.join(FIXTURE_DIR, PAIRS["resource_add"][1])

            # ---- assign_add ---------------------------------------------
            project = open_copy(app, resource_add_path, "g2-assign",
                                "Synthetic G2 Fixture")
            entry = facts["pairs"].setdefault("assign_add", {})
            entry["control_facts"] = all_facts(project)
            entry["control"] = save(app, PAIRS["assign_add"][0])
            task = project.Tasks(1)
            assignment = task.Assignments.Add(TaskID=task.ID, ResourceID=2)
            entry["added_assignment"] = assignment_facts(assignment, 1)
            entry["edit_facts_before_save"] = all_facts(project)
            entry["edited"] = save(app, PAIRS["assign_add"][1])
            entry["edited_facts"] = all_facts(project)
            app.FileClose(0)

            assign_add_path = os.path.join(FIXTURE_DIR, PAIRS["assign_add"][1])

            # ---- assign_delete ------------------------------------------
            project = open_copy(app, assign_add_path, "g2-unassign",
                                "Synthetic G2 Fixture")
            entry = facts["pairs"].setdefault("assign_delete", {})
            entry["control_facts"] = all_facts(project)
            entry["control"] = save(app, PAIRS["assign_delete"][0])
            task = project.Tasks(1)
            deleted = None
            for a in range(1, task.Assignments.Count + 1):
                candidate = task.Assignments(a)
                if prop(candidate, "ResourceUniqueID") == 2:
                    deleted = assignment_facts(candidate, a)
                    candidate.Delete()
                    break
            entry["deleted_assignment"] = deleted
            entry["edit_facts_before_save"] = all_facts(project)
            entry["edited"] = save(app, PAIRS["assign_delete"][1])
            entry["edited_facts"] = all_facts(project)
            app.FileClose(0)

            # ---- rename -------------------------------------------------
            project = open_copy(app, resource_add_path, "g2-rename",
                                "Synthetic G2 Fixture")
            entry = facts["pairs"].setdefault("rename", {})
            entry["control_facts"] = all_facts(project)
            entry["control"] = save(app, PAIRS["rename"][0])
            project.Resources(2).Name = "R2b"
            entry["edit_facts_before_save"] = all_facts(project)
            entry["edited"] = save(app, PAIRS["rename"][1])
            entry["edited_facts"] = all_facts(project)
            app.FileClose(0)

            # ---- notes --------------------------------------------------
            project = open_copy(app, resource_add_path, "g2-notes",
                                "Synthetic G2 Fixture")
            entry = facts["pairs"].setdefault("notes", {})
            entry["control_facts"] = all_facts(project)
            entry["control"] = save(app, PAIRS["notes"][0])
            project.Resources(2).Notes = "Resource note line."
            entry["edit_facts_before_save"] = all_facts(project)
            entry["edited"] = save(app, PAIRS["notes"][1])
            entry["edited_facts"] = all_facts(project)
            app.FileClose(0)

            # Reopen every committed file: what Project itself displays.
            for key, pair in PAIRS.items():
                entry = facts["pairs"][key]
                entry["control_reopen"] = reopen_facts(app, pair[0])
                entry["edited_reopen"] = reopen_facts(app, pair[1])

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for key, pair in PAIRS.items():
        for name in pair:
            path = os.path.join(NEUTRAL, "fixtures-g2", name)
            log(f"fixture {name} ({os.path.getsize(path)} bytes)")


if __name__ == "__main__":
    main()
