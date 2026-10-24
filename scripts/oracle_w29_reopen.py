#!/usr/bin/env python3
"""W29-0.1: Project 2021's reopen of the PORT's wave-29 saves.

Standing rule 1 says a save path is evidence only when Project opens the port's
file and displays what Project's own save of the same edit displays.  The
wave-28 review's R28-1 found no such reopen for the actuals, outline or
new-task-mode writers, so this script measures them through COM on copies under
build/oracle-work (the committed fixtures and the port's build outputs are
never opened in place):

  actuals       app-actuals-{auto0-40,manual0-40,mark-auto,mark-manual,update}
                against tests/data/wave28-{percent-auto40,percent-manual40,
                mark-auto,mark-manual,update}.mpp;
  outline       app-outline-{indent,summary,outdent} against
                tests/data/wave28-outline-{indent,summary,outdent}.mpp;
  new-task mode app-newtask-{manual_to_auto,auto_to_manual} against
                tests/data/wave28-newtask-{auto,manual}.mpp.

Every row is recorded by ID only (no task, resource or project name): Manual,
Summary, OutlineNumber, Duration, Start, Finish, PercentComplete,
PercentWorkComplete, Actual/Remaining Duration, Actual dates, Work, ActualWork,
RemainingWork, and the assignment-level Work/ActualWork/RemainingWork/Units.
Each pair's port reopen is compared FIELD BY FIELD with the Project reopen of
Project's own save; the differing fields are listed.  The port-only saves
(actuals-mixed and the two insert saves) are reopened too and checked against
the values the port wrote.

Facts go to build/w29/W29-0.1-project-reopen.json (not committed; the record
quotes its path, sha256 and numbers).

    python -I scripts/oracle_w29_reopen.py [<port-save-dir>]

The default port-save directory is build/w28-fresh-app/test-out (the app test
binaries' APP_TEST_OUT_DIR).
"""

import datetime as dt
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log, working_copy  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

DATA = os.path.join(ROOT, "tests", "data")
OUT = os.path.join(ROOT, "build", "w29")
RESULT = os.path.join(OUT, "W29-0.1-project-reopen.json")
DEFAULT_PORT_DIR = os.path.join(ROOT, "build", "w28-fresh-app", "test-out")

# tag, port save (in the port dir), Project's own save of the same edit
PAIRS = [
    ("actuals-auto0-40", "app-actuals-auto0-40.mpp",
     "wave28-percent-auto40.mpp"),
    ("actuals-manual0-40", "app-actuals-manual0-40.mpp",
     "wave28-percent-manual40.mpp"),
    ("actuals-mark-auto", "app-actuals-mark-auto.mpp", "wave28-mark-auto.mpp"),
    ("actuals-mark-manual", "app-actuals-mark-manual.mpp",
     "wave28-mark-manual.mpp"),
    ("actuals-update", "app-actuals-update.mpp", "wave28-update.mpp"),
    ("outline-indent", "app-outline-indent.mpp", "wave28-outline-indent.mpp"),
    ("outline-summary", "app-outline-summary.mpp", "wave28-outline-summary.mpp"),
    ("outline-outdent", "app-outline-outdent.mpp", "wave28-outline-outdent.mpp"),
    ("newtask-manual-to-auto", "app-newtask-manual_to_auto.mpp",
     "wave28-newtask-auto.mpp"),
    ("newtask-auto-to-manual", "app-newtask-auto_to_manual.mpp",
     "wave28-newtask-manual.mpp"),
]


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


def assignment_facts(task):
    """Assignment-level facts by ordinal; no resource name or id is read."""
    try:
        assignments = task.Assignments
        count = assignments.Count
    except Exception as exc:  # noqa: BLE001
        return {"count": f"<unreadable: {exc}>", "rows": []}
    rows = []
    for index in range(1, count + 1):
        assignment = assignments(index)
        rows.append({
            "index": index,
            "work": prop(assignment, "Work"),
            "actual_work": prop(assignment, "ActualWork"),
            "remaining_work": prop(assignment, "RemainingWork"),
            "start": prop(assignment, "Start"),
            "finish": prop(assignment, "Finish"),
            "units": prop(assignment, "Units"),
        })
    return {"count": count, "rows": rows}


def task_facts(task):
    return {
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "manual": prop(task, "Manual"),
        "summary": prop(task, "Summary"),
        "outline_number": prop(task, "OutlineNumber"),
        "duration": prop(task, "Duration"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "percent_complete": prop(task, "PercentComplete"),
        "percent_work_complete": prop(task, "PercentWorkComplete"),
        "actual_duration": prop(task, "ActualDuration"),
        "remaining_duration": prop(task, "RemainingDuration"),
        "actual_start": prop(task, "ActualStart"),
        "actual_finish": prop(task, "ActualFinish"),
        "work": prop(task, "Work"),
        "actual_work": prop(task, "ActualWork"),
        "remaining_work": prop(task, "RemainingWork"),
        "assignments": assignment_facts(task),
    }


def project_facts(project):
    rows = {"project": task_facts(project.ProjectSummaryTask)}
    for task in project.Tasks:
        if task is None:
            continue
        rows[str(prop(task, "ID"))] = task_facts(task)
    return {
        "new_tasks_created_as_manual": prop(
            project, "NewTasksCreatedAsManual"),
        "rows": rows,
    }


def reopen_facts(app, path, tag):
    app.FileOpen(working_copy(path, tag))
    facts = project_facts(app.ActiveProject)
    app.FileClose(0)
    return facts


def compare_facts(port, project):
    """Field-by-field diff; `port` is the port's reopen, `project` Project's."""
    differences = []
    if port["new_tasks_created_as_manual"] != project["new_tasks_created_as_manual"]:
        differences.append({
            "row": "project",
            "field": "new_tasks_created_as_manual",
            "port": port["new_tasks_created_as_manual"],
            "project": project["new_tasks_created_as_manual"],
        })
    porta = port["rows"]
    projectb = project["rows"]
    for key in sorted(set(porta) | set(projectb),
                      key=lambda k: (k == "project", k if k == "project" else int(k))):
        ra = porta.get(key)
        rb = projectb.get(key)
        if ra is None or rb is None:
            differences.append({
                "row": key, "field": "<row present>",
                "port": ra is not None, "project": rb is not None,
            })
            continue
        for field in sorted(set(ra) | set(rb)):
            if ra.get(field) != rb.get(field):
                differences.append({
                    "row": key, "field": field,
                    "port": ra.get(field), "project": rb.get(field),
                })
    return differences


def row_number(rows, task_id):
    return rows.get(str(task_id))


def run_checks(facts, checks):
    """checks: list of (label, actual, expected); all values JSON-friendly."""
    results = []
    for label, actual, expected in checks:
        results.append({
            "check": label,
            "pass": actual == expected,
            "actual": actual,
            "expected": expected,
        })
    return results


def main():
    port_dir = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PORT_DIR
    inputs = [os.path.join(DATA, project) for _, _, project in PAIRS]
    inputs += [os.path.join(port_dir, port) for _, port, _ in PAIRS]
    inputs += [
        os.path.join(port_dir, name)
        for name in ("app-actuals-mixed.mpp", "app-newtask-insert-manual.mpp",
                     "app-newtask-insert-auto.mpp")
    ]
    for path in inputs:
        if not os.path.isfile(path):
            sys.exit(f"missing input: {path}")
    os.makedirs(OUT, exist_ok=True)

    payload = {"pairs": {}, "port_only": {}}
    with oracle_session() as app:
        with Guard(900):
            version = prop(app, "Version")
            calculation = prop(app, "Calculation")
            for tag, port_name, project_name in PAIRS:
                log(f"reopening {tag} (port and Project)")
                port = reopen_facts(
                    app, os.path.join(port_dir, port_name), f"w29-{tag}-port")
                project = reopen_facts(
                    app, os.path.join(DATA, project_name),
                    f"w29-{tag}-project")
                differences = compare_facts(port, project)
                payload["pairs"][tag] = {
                    "port_save": os.path.relpath(
                        os.path.join(port_dir, port_name), ROOT),
                    "project_save": os.path.relpath(
                        os.path.join(DATA, project_name), ROOT),
                    "port_reopen": port,
                    "project_reopen": project,
                    "differing_fields": len(differences),
                    "differences": differences[:60],
                }
                log(f"{tag}: {len(differences)} differing field(s)")

            mixed = reopen_facts(
                app, os.path.join(port_dir, "app-actuals-mixed.mpp"),
                "w29-actuals-mixed")
            payload["port_only"]["actuals-mixed"] = {
                "port_save": os.path.relpath(
                    os.path.join(port_dir, "app-actuals-mixed.mpp"), ROOT),
                "reopen": mixed,
                "checks": run_checks(mixed, [
                    ("ID 1 percent complete",
                     (row_number(mixed["rows"], 1) or {}).get("percent_complete"),
                     40),
                    ("ID 2 percent complete",
                     (row_number(mixed["rows"], 2) or {}).get("percent_complete"),
                     83),
                    ("ID 3 percent complete",
                     (row_number(mixed["rows"], 3) or {}).get("percent_complete"),
                     100),
                ]),
            }

            for tag, name, expected_manual in (
                ("newtask-insert-manual", "app-newtask-insert-manual.mpp", True),
                ("newtask-insert-auto", "app-newtask-insert-auto.mpp", False),
            ):
                facts = reopen_facts(app, os.path.join(port_dir, name),
                                     f"w29-{tag}")
                ids = [int(key) for key in facts["rows"] if key != "project"]
                highest = str(max(ids)) if ids else None
                inserted = facts["rows"].get(highest, {})
                payload["port_only"][tag] = {
                    "port_save": os.path.relpath(
                        os.path.join(port_dir, name), ROOT),
                    "reopen": facts,
                    "checks": run_checks(facts, [
                        ("the inserted row is the highest ID", highest is not None,
                         True),
                        ("the inserted row's Manual flag",
                         inserted.get("manual"), expected_manual),
                        ("the project stores no new-task-mode change",
                         facts["new_tasks_created_as_manual"], False),
                    ]),
                }
                log(f"{tag}: inserted ID {highest} manual="
                    f"{inserted.get('manual')}")

    out = {
        "measured": "MS Project reopen of the port's wave-29 actuals, outline "
                    "and new-task-mode saves",
        "version": version,
        "calculation_mode": calculation,
        "port_dir": os.path.relpath(port_dir, ROOT),
        "notes": [
            "Each pair compares the port's reopen with Project's reopen of "
            "Project's own save of the same edit, field by field, over the "
            "project summary row and every task row.",
            "The wave-28 fixtures' TBkndAssn rows are resource-less (resource "
            "word 0xFFFF0001), so Project exposes no Assignment objects for "
            "them and the assignment-level facts are empty on both sides.  The "
            "save-pair tests compare the TBkndAssn streams field by field, "
            "including the tombstone and the re-created row.",
        ],
        "pairs_compared": len(payload["pairs"]),
        "pairs_with_differences": sum(
            1 for entry in payload["pairs"].values()
            if entry["differing_fields"]),
        "files": payload,
    }
    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(out, handle, indent=2)
        handle.write("\n")
    for tag, entry in payload["pairs"].items():
        log(f"{tag}: {entry['differing_fields']} differing field(s)")
    for tag, entry in payload["port_only"].items():
        ok = all(check["pass"] for check in entry["checks"])
        log(f"{tag}: {'checks pass' if ok else 'CHECK FAILED'}")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")


if __name__ == "__main__":
    main()
