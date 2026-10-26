#!/usr/bin/env python3
"""Wave 31 (G4): the Task Usage / Resource Usage fixture and its COM facts.

The G4 gate needs a file Project itself saved with BOTH usage views carrying
stored detail lines and Project's own time-phased values to measure against.
synthetic-base.mpp stores no resources and no assignment; this script builds
the fixture through COM on a copy under the neutral root C:/w31/usage-fixture:

  1. open tests/data/synthetic-base.mpp with neutral Author/Manager/Company/
     UserName/Title.  Its three tasks are switched to automatic (the measured
     Resource Sheet fixture needed the same for Project to schedule resource
     work), and its one placeholder resource and three placeholder assignments
     are deleted so the saved file stores real assignment links;
  2. add three Work resources at 100 %: Crew A ($50.00/hr), Crew B
     ($60.00/hr), Crew C ($40.00/hr).  No Cost/Use and the default Prorated
     accrual, so Project phases cost with work;
  3. assign two resources to EVERY task, all at 100 %: task 1 Crew A + Crew B,
     task 2 Crew A + Crew C, task 3 Crew A + Crew C.  Two 100 % resources on a
     task leave its duration and each assignment's work equal to the task's
     own duration (measured for task 1 on the G3 fixture); no 50 % assignment
     is used because Project lengthens such an effort-driven task and that
     would make the daily contour harder to read;
  4. task 1 is set to 100 % complete, tasks 2 and 3 stay at 0 %, so the
     Actual Work detail has a non-empty and an empty row;
  5. apply Task Usage, clear the details, add Work (0) then Actual Work (4);
     apply Resource Usage, clear, add Work (0) then Cost (1); leave both
     timescales at Project's default (weeks / days, formats 13/20, starts
     unset) -- the default is what the file stores and the port renders;
  6. save with Resource Usage applied, reopen, and record the facts the port's
     Qt gate is measured against: applied view/table, the Usage table's own
     columns, every logical row's left-table cells, and daily TimeScaleData
     buckets (unit 4, count 1) for Work, Actual Work and Cost on every task,
     assignment and resource over the project week.  The Type argument is a
     per-object enumeration: Task 0/2/5, Assignment 8/10/26, Resource 13/14/12
     (measured; see TIME_SCALE_TYPES).

Every fact is read back from the REOPENED saved file, not from the live edit
session.  The script only writes under C:/w31 and build/w31; the coordinator
copies the saved file into tests/data and pins its hash by hand after reviewing
the port's reader dump.  Facts go to build/w31/W31-G4-usage.json (not
committed).  The script refuses to overwrite an existing fixture.

    python -I scripts/oracle_w31_usage.py
"""

import datetime as dt
import hashlib
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log  # noqa: E402
from oracle_w3 import Guard, utc, val  # noqa: E402

NEUTRAL = r"C:/w31/usage-fixture"
OUT = os.path.join(ROOT, "build", "w31")
RESULT = os.path.join(OUT, "W31-G4-usage.json")
SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")
BASE = os.path.join(NEUTRAL, "wave31-usage-base.mpp")
FIXTURE = os.path.join(NEUTRAL, "wave31-usage.mpp")

# The project week: Monday 5 January 2026, 08:00.  Four days of step take the
# daily TimeScaleData walk through Friday the 9th inclusive (the API's unit-4
# walk starts each bucket at 00:00 and includes both endpoint days, measured
# in build/w31/g4-probe9.json).
WEEK_START = dt.datetime(2026, 1, 5, 8)
WEEK_DAYS = 4


def prop(obj, name):
    try:
        return val(getattr(obj, name))
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def neutral(app, project):
    app.UserName = "Fixture Builder"
    project.Author = "Fixture Builder"
    project.Manager = "Fixture Builder"
    project.Company = "Fixture Company"
    project.Title = "Synthetic Usage Fixture"


def task_row(task, index):
    return {
        "index": index,
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "name": prop(task, "Name"),
        "manual": prop(task, "Manual"),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "duration": prop(task, "Duration"),
        "work": prop(task, "Work"),
        "actual_work": prop(task, "ActualWork"),
        "remaining_work": prop(task, "RemainingWork"),
        "percent_complete": prop(task, "PercentComplete"),
        "cost": prop(task, "Cost"),
    }


def resource_row(resource, index):
    return {
        "index": index,
        "id": prop(resource, "ID"),
        "unique_id": prop(resource, "UniqueID"),
        "name": prop(resource, "Name"),
        "type": prop(resource, "Type"),
        "work": prop(resource, "Work"),
        "actual_work": prop(resource, "ActualWork"),
        "cost": prop(resource, "Cost"),
        "standard_rate": prop(resource, "StandardRate"),
        "overtime_rate": prop(resource, "OvertimeRate"),
        "cost_per_use": prop(resource, "CostPerUse"),
        "accrue_at": prop(resource, "AccrueAt"),
    }


def assignment_row(assignment, index):
    return {
        "index": index,
        "unique_id": prop(assignment, "UniqueID"),
        "task_id": prop(assignment, "TaskID"),
        "resource_id": prop(assignment, "ResourceID"),
        "units": prop(assignment, "Units"),
        "work": prop(assignment, "Work"),
        "actual_work": prop(assignment, "ActualWork"),
        "remaining_work": prop(assignment, "RemainingWork"),
        "cost": prop(assignment, "Cost"),
        "start": prop(assignment, "Start"),
        "finish": prop(assignment, "Finish"),
    }


def project_facts(project):
    tasks = {
        str(index): task_row(project.Tasks(index), index)
        for index in range(1, project.Tasks.Count + 1)
    }
    resources = {
        str(index): resource_row(project.Resources(index), index)
        for index in range(1, project.Resources.Count + 1)
    }
    assignments = {}
    flat = 0
    for index in range(1, project.Tasks.Count + 1):
        task = project.Tasks(index)
        for a in range(1, task.Assignments.Count + 1):
            flat += 1
            assignments[str(flat)] = assignment_row(task.Assignments(a), flat)
    return {
        "task_count": project.Tasks.Count,
        "resource_count": project.Resources.Count,
        "tasks": tasks,
        "resources": resources,
        "assignments": assignments,
    }


def table_columns(table):
    columns = []
    try:
        fields = table.TableFields
        for index in range(1, fields.Count + 1):
            field = fields(index)
            columns.append({
                "field": prop(field, "Field"),
                "field_id": prop(field, "FieldID"),
                "title": prop(field, "Title"),
                "width": prop(field, "Width"),
            })
    except Exception as exc:  # noqa: BLE001
        columns.append({"error": repr(exc)})
    return columns


def find_table(collection, name):
    """The stored table object with this display name, or None."""
    try:
        for index in range(1, collection.Count + 1):
            table = collection(index)
            if prop(table, "Name") == name:
                return table
    except Exception:  # noqa: BLE001
        return None
    return None


def applied_facts(project):
    return {
        "current_view": prop(project, "CurrentView"),
        "current_table": prop(project, "CurrentTable"),
        "view_count": prop(project.Views, "Count"),
        "task_table_names": [prop(project.TaskTables(i), "Name")
                             for i in range(1, project.TaskTables.Count + 1)],
        "resource_table_names": [prop(project.ResourceTables(i), "Name")
                                 for i in range(1,
                                                   project.ResourceTables.Count + 1)],
    }


def applied_table(project, resource):
    name = prop(project, "CurrentTable")
    collection = project.ResourceTables if resource else project.TaskTables
    return name, find_table(collection, name)


def describe_cell(cell):
    if cell is None:
        return None
    out = {"field": prop(cell, "FieldName"), "text": prop(cell, "Text")}
    try:
        task = cell.Task
        out["task"] = None if task is None else prop(task, "ID")
    except Exception:  # noqa: BLE001
        out["task"] = None
    try:
        assignment = cell.Assignment
        out["assignment"] = None if assignment is None else {
            "unique_id": prop(assignment, "UniqueID"),
            "task_id": prop(assignment, "TaskID"),
            "resource_id": prop(assignment, "ResourceID"),
        }
    except Exception:  # noqa: BLE001
        out["assignment"] = "<none>"
    return out


def grid(app, columns=8, rows=13):
    out = {}
    for row in range(rows):
        for column in range(columns):
            try:
                out[f"{column},{row}"] = describe_cell(app.GetCellInfo(column, row))
            except Exception as exc:  # noqa: BLE001
                out[f"{column},{row}"] = f"<err {exc.__class__.__name__}: {exc}>"
    return out


# The Type argument of TimeScaleData is a DIFFERENT enumeration per object
# (Microsoft's PjTaskTimescaledData / PjResourceTimescaledData /
# PjAssignmentTimescaledData).  Measured here: an Assignment with 8/10/26
# returns work/actual work/cost buckets, while a Task with 8 returns its empty
# Fixed Cost and a Resource with 26 returns Peak Units (3/2/1 for a crew on
# three, two and one task).  The values below are the enum members the gate
# needs, named by their Microsoft numbers.
TIME_SCALE_TYPES = {
    "tasks": {"work": 0, "actual_work": 2, "cost": 5},          # PjTaskTimescaledData
    "assignments": {"work": 8, "actual_work": 10, "cost": 26},  # PjAssignmentTimescaledData
    "resources": {"work": 13, "actual_work": 14, "cost": 12},   # PjResourceTimescaledData
}


def time_scale_data(subject, dtype):
    """Daily buckets for one subject over the project week.

    Work and Actual Work are minutes; Cost is the currency amount (the file
    stores AUD and Project displays its "$" option).  Unit 4 = days, count 1.
    A weekend or zero bucket is Project's empty string, kept as "" rather than
    coerced to 0.
    """
    try:
        values = subject.TimeScaleData(utc(WEEK_START),
                                       utc(WEEK_START + dt.timedelta(days=WEEK_DAYS)),
                                       dtype, 4, 1)
        buckets = []
        for index in range(1, values.Count + 1):
            bucket = values.Item(index)
            buckets.append({"start": val(bucket.StartDate),
                            "value": val(bucket.Value)})
        return {"count": values.Count, "buckets": buckets}
    except Exception as exc:  # noqa: BLE001
        return {"error": f"{exc.__class__.__name__}: {exc}"}


def time_series(project):
    series = {"tasks": {}, "assignments": {}, "resources": {}}
    series["types"] = TIME_SCALE_TYPES

    def one(subject, kind):
        types = TIME_SCALE_TYPES[kind]
        return {"work": time_scale_data(subject, types["work"]),
                "actual_work": time_scale_data(subject, types["actual_work"]),
                "cost": time_scale_data(subject, types["cost"])}

    for index in range(1, project.Tasks.Count + 1):
        task = project.Tasks(index)
        key = str(task.ID)
        series["tasks"][key] = one(task, "tasks")
        for a in range(1, task.Assignments.Count + 1):
            assignment = task.Assignments(a)
            series["assignments"][str(assignment.UniqueID)] = one(
                assignment, "assignments")
    for index in range(1, project.Resources.Count + 1):
        resource = project.Resources(index)
        series["resources"][str(resource.ID)] = one(resource, "resources")
    return series


def view_facts(app, project, resource):
    """The applied usage view's own table and visible left-table cells."""
    name, table = applied_table(project, resource)
    columns = table_columns(table) if table is not None else []
    return {
        "current_view": prop(project, "CurrentView"),
        "current_table": name,
        "table_found": table is not None,
        "table_columns": columns,
        "grid": grid(app),
        "time_scale_data": time_series(project),
    }


def main():
    os.makedirs(NEUTRAL, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    if os.path.exists(FIXTURE):
        sys.exit(f"refusing: {FIXTURE} already exists (inspect it first)")
    if not os.path.isfile(SOURCE):
        sys.exit(f"fixture missing: {SOURCE}")
    shutil.copyfile(SOURCE, BASE)

    result = {"version": None, "source": "tests/data/synthetic-base.mpp",
              "neutral_root": NEUTRAL,
              "week": {"start": val(WEEK_START), "days": WEEK_DAYS}}
    with oracle_session() as app:
        with Guard(900):
            result["version"] = str(app.Version)
            app.FileOpen(BASE)
            project = app.ActiveProject
            neutral(app, project)
            result["base"] = project_facts(project)

            for task_id in (1, 2, 3):
                project.Tasks(task_id).Manual = False
            for task_id in (1, 2, 3):
                task = project.Tasks(task_id)
                while task.Assignments.Count:
                    task.Assignments(1).Delete()
            while project.Resources.Count:
                project.Resources(1).Delete()
            result["cleared"] = project_facts(project)

            # Three Work resources, neutral names, per-hour standard rates.
            crew_a = project.Resources.Add("Crew A")
            crew_b = project.Resources.Add("Crew B")
            crew_c = project.Resources.Add("Crew C")
            crew_a.StandardRate = "$50.00/hr"
            crew_b.StandardRate = "$60.00/hr"
            crew_c.StandardRate = "$40.00/hr"
            result["resource_ids"] = {
                "a": prop(crew_a, "ID"),
                "b": prop(crew_b, "ID"),
                "c": prop(crew_c, "ID"),
            }

            # Two 100 % resources on every task.  With no assignment below
            # 100 % Project keeps the task's own duration (measured on the G3
            # fixture for task 1), so every assignment spans its whole task.
            pairs = ((1, crew_a), (1, crew_b),
                     (2, crew_a), (2, crew_c),
                     (3, crew_a), (3, crew_c))
            for task_id, resource in pairs:
                task = project.Tasks(task_id)
                task.Assignments.Add(TaskID=task.ID,
                                     ResourceID=prop(resource, "ID"))
            result["calculate_returns"] = val(app.CalculateProject())

            # Task 1 is finished, so the Actual Work detail has real buckets;
            # tasks 2 and 3 have none.  Both percentages are exact in the port
            # (0 and 100); partial progress is not measured by this fixture.
            project.Tasks(1).PercentComplete = 100
            result["calculate_after_complete"] = val(app.CalculateProject())
            result["after_assign"] = project_facts(project)

            # Stored detail lines: Task Usage = Work + Actual Work,
            # Resource Usage = Work + Cost.  These are the ids measured in
            # build/w31/g4-probe*.json (0 = Work, 4 = Actual Work, 1 = Cost).
            result["task_usage_apply"] = val(app.ViewApply("Task Usage"))
            result["task_usage_remove_all"] = val(app.DetailStylesRemoveAll())
            result["task_usage_add_work"] = val(app.DetailStylesAdd(0))
            result["task_usage_add_actual"] = val(app.DetailStylesAdd(4))
            result["resource_usage_apply"] = val(app.ViewApply("Resource Usage"))
            result["resource_usage_remove_all"] = val(app.DetailStylesRemoveAll())
            result["resource_usage_add_work"] = val(app.DetailStylesAdd(0))
            result["resource_usage_add_cost"] = val(app.DetailStylesAdd(1))
            result["applied_before_save"] = applied_facts(project)

            app.FileSaveAs(FIXTURE)
            app.FileClose(0)

            # Reopen the SAVED file: these are the facts the Qt gate uses.
            app.FileOpen(FIXTURE)
            reopened = app.ActiveProject
            result["reopened_applied"] = applied_facts(reopened)
            result["reopened"] = project_facts(reopened)
            result["task_usage_view_apply"] = val(app.ViewApply("Task Usage"))
            result["task_usage"] = view_facts(app, reopened, resource=False)
            result["resource_usage_view_apply"] = val(
                app.ViewApply("Resource Usage"))
            result["resource_usage"] = view_facts(app, reopened, resource=True)
            result["reopened_applied_after"] = applied_facts(reopened)
            app.FileClose(0)

    result["fixture"] = {
        "path": FIXTURE,
        "bytes": os.path.getsize(FIXTURE),
        "sha256": sha256(FIXTURE),
    }
    reopened = result["reopened"]
    if reopened["task_count"] != 3 or reopened["resource_count"] != 3:
        raise SystemExit("refusing: the saved file does not carry 3 tasks and "
                         f"3 resources: {json.dumps(reopened)}")
    if len(reopened["assignments"]) != 6:
        raise SystemExit("refusing: the saved file does not carry 6 assignments")
    if result["reopened"]["tasks"]["1"]["percent_complete"] != 100:
        raise SystemExit("refusing: task 1 is not 100 % complete in the save")
    for key in ("task_usage", "resource_usage"):
        if "error" in result[key]["time_scale_data"]["tasks"]["1"]["work"]:
            raise SystemExit(f"refusing: TimeScaleData failed on {key}")
    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
        handle.write("\n")
    log("reopened applied: " + json.dumps(result["reopened_applied"]))
    log("task usage work task 1: "
        + json.dumps(result["task_usage"]["time_scale_data"]["tasks"]["1"]["work"]))
    log("resource usage cost Crew A: "
        + json.dumps(result["resource_usage"]["time_scale_data"]
                     ["resources"]["1"]["cost"]))
    print("wrote", os.path.relpath(RESULT, ROOT))
    print("fixture", FIXTURE, result["fixture"]["sha256"])


if __name__ == "__main__":
    main()
