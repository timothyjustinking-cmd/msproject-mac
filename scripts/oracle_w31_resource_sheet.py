#!/usr/bin/env python3
"""Wave 31 (G3): the Resource Sheet fixture and its rendering facts.

The Resource Sheet gate needs a file Project itself saved while the Resource
Sheet was the APPLIED view, so the file stores the view, its applied resource
table and that table's column widths -- plus resources Project computes as
overallocated.  synthetic-base.mpp stores no resource view, table or resource,
so this script builds one through COM on a copy under the neutral root
C:/w31/resource-sheet:

  1. open tests/data/synthetic-base.mpp with neutral Author/Manager/Company/
     UserName/Title.  That file carries one resource row and three placeholder
     assignments whose resource link is uid 0; both are cleared so the saved
     fixture stores real assignment links.  Its three tasks are manually
     scheduled, and Project computes PeakUnits 0.0 / Overallocated False for
     manual tasks even at 300% (measured), so they are switched to automatic;
  2. add three Work resources (Crew A/B/C) and assignments: Crew A carries all
     three tasks at 100% (peak 300% -> Overallocated), Crew B one task (100% ->
     not), Crew C two tasks at 50% each (peak 100% -> not);
  3. give the rows text and rate values so the gate exercises every stored
     column whose value the reader can decode: Group and Code on all three
     rows, Crew B's Standard/Overtime rate and Cost/Use, and AccrueAt
     Start/End on Crew B/C (the default Prorated stays on Crew A).  The rate
     storage is the measured one (build/w31/g3-rate-probe*.json):
     StandardRate/OvertimeRate are currency-per-hour doubles at row +16/+24,
     CostPerUse is hundredths at +72;
  4. apply the Resource Sheet view and the resource Entry table, so both are
     stored and applied in the saved file;
  5. save, then reopen and record the facts the port's Qt gate is measured
     against: the applied view/table name, the resource tables and every
     TableField (Field / Title / Width), the resource rows' values (Name, Type,
     Initials, MaxUnits, Work, PeakUnits, Overallocated, ...) and the
     assignments' Units/Work.

Every fact is read back from the REOPENED saved file, not from the live edit
session.  The script only writes under C:/w31 and build/w31; the coordinator
copies the saved file into tests/data and pins its hash by hand after reviewing
the port's reader dump.  Facts go to build/w31/W31-G3-resource-sheet.json (not
committed).  The script refuses to overwrite an existing fixture.

    python -I scripts/oracle_w31_resource_sheet.py
"""

import hashlib
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log  # noqa: E402
from oracle_w3 import Guard, val  # noqa: E402

NEUTRAL = r"C:/w31/resource-sheet"
OUT = os.path.join(ROOT, "build", "w31")
RESULT = os.path.join(OUT, "W31-G3-resource-sheet.json")
SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")
BASE = os.path.join(NEUTRAL, "wave31-resource-sheet-base.mpp")
FIXTURE = os.path.join(NEUTRAL, "wave31-resource-sheet.mpp")


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
    project.Title = "Synthetic Resource Sheet Fixture"


def table_columns(table):
    columns = []
    try:
        fields = table.TableFields
        for index in range(1, fields.Count + 1):
            field = fields(index)
            columns.append({
                "field": prop(field, "Field"),
                "title": prop(field, "Title"),
                "width": prop(field, "Width"),
            })
    except Exception as exc:  # noqa: BLE001
        columns.append({"error": repr(exc)})
    return columns


def table_facts(project, collection_name):
    """Task or resource tables: name, type and every stored column."""
    tables = []
    collection = getattr(project, collection_name, None)
    count = prop(collection, "Count") if collection is not None else None
    if not isinstance(count, int):
        return {"collection": collection_name, "count": count, "tables": tables,
                "error": "collection unavailable"}
    for index in range(1, count + 1):
        table = collection(index)
        tables.append({
            "index": index,
            "name": prop(table, "Name"),
            "type": prop(table, "TableType"),
            "row_height": prop(table, "RowHeight"),
            "columns": table_columns(table),
        })
    return {"collection": collection_name, "count": count, "tables": tables}


def all_tables(project):
    return {
        "resource_tables": table_facts(project, "ResourceTables"),
        "task_tables": table_facts(project, "TaskTables"),
    }


def resource_row(resource, index):
    return {
        "index": index,
        "id": prop(resource, "ID"),
        "unique_id": prop(resource, "UniqueID"),
        "name": prop(resource, "Name"),
        "initials": prop(resource, "Initials"),
        "type": prop(resource, "Type"),
        "material_label": prop(resource, "MaterialLabel"),
        "group": prop(resource, "Group"),
        "max_units": prop(resource, "MaxUnits"),
        "work": prop(resource, "Work"),
        "peak_units": prop(resource, "PeakUnits"),
        "overallocated": prop(resource, "Overallocated"),
        "standard_rate": prop(resource, "StandardRate"),
        "overtime_rate": prop(resource, "OvertimeRate"),
        "cost_per_use": prop(resource, "CostPerUse"),
        "accrue_at": prop(resource, "AccrueAt"),
        "base_calendar": prop(resource, "BaseCalendar"),
        "code": prop(resource, "Code"),
    }


def assignment_row(assignment, index):
    return {
        "index": index,
        "task_id": prop(assignment, "TaskID"),
        "resource_id": prop(assignment, "ResourceID"),
        "units": prop(assignment, "Units"),
        "work": prop(assignment, "Work"),
        "start": prop(assignment, "Start"),
        "finish": prop(assignment, "Finish"),
    }


def resource_facts(project):
    resources = {}
    for index in range(1, project.Resources.Count + 1):
        resources[str(index)] = resource_row(project.Resources(index), index)
    assignments = {}
    flat = 0
    for index in range(1, project.Tasks.Count + 1):
        task = project.Tasks(index)
        for a in range(1, task.Assignments.Count + 1):
            flat += 1
            assignments[str(flat)] = assignment_row(task.Assignments(a), flat)
    return {
        "resource_count": project.Resources.Count,
        "overallocated_resources": prop(project, "OverallocatedResources"),
        "resources": resources,
        "assignments": assignments,
    }


def applied_facts(project):
    views = []
    count = prop(project.Views, "Count")
    if isinstance(count, int):
        for index in range(1, count + 1):
            views.append(prop(project.Views(index), "Name"))
    return {
        "current_table": prop(project, "CurrentTable"),
        "current_view": prop(project, "CurrentView"),
        "view_count": count,
        "views": views,
    }


def shown_cell(app, row, column):
    """Project's displayed text for one Resource Sheet cell, when selectable."""
    try:
        app.SelectResourceField(Row=row, Column=column, RowRelative=False)
        return {"row": row, "column": column,
                "text": prop(app.ActiveCell, "Text"),
                "field": prop(app.ActiveCell, "Field")}
    except Exception as exc:  # noqa: BLE001
        return {"row": row, "column": column, "error": repr(exc)}


def main():
    os.makedirs(NEUTRAL, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    if os.path.exists(FIXTURE):
        sys.exit(f"refusing: {FIXTURE} already exists (inspect it first)")
    if not os.path.isfile(SOURCE):
        sys.exit(f"fixture missing: {SOURCE}")
    shutil.copyfile(SOURCE, BASE)

    result = {"version": None, "source": "tests/data/synthetic-base.mpp",
              "neutral_root": NEUTRAL}
    with oracle_session() as app:
        with Guard(900):
            result["version"] = str(app.Version)
            app.FileOpen(BASE)
            project = app.ActiveProject
            neutral(app, project)
            result["base_resources"] = resource_facts(project)

            # Automatic tasks are the shape Project computes PeakUnits and
            # Overallocated for; a manual task stays at 0.0/False even when its
            # resource carries three concurrent 100% assignments (measured on
            # the first attempt at this fixture).
            for task_id in (1, 2, 3):
                project.Tasks(task_id).Manual = False
            # Clear the base's placeholder assignments and its one resource,
            # so the saved fixture stores real assignment links only.
            for task_id in (1, 2, 3):
                task = project.Tasks(task_id)
                while task.Assignments.Count:
                    task.Assignments(1).Delete()
            while project.Resources.Count:
                project.Resources(1).Delete()
            result["cleared"] = resource_facts(project)

            # Three Work resources.  Names are neutral by the standing rules.
            crew_a = project.Resources.Add("Crew A")
            crew_b = project.Resources.Add("Crew B")
            crew_c = project.Resources.Add("Crew C")
            result["resource_ids"] = {
                "a": prop(crew_a, "ID"), "b": prop(crew_b, "ID"),
                "c": prop(crew_c, "ID"),
            }
            # Text columns and the rate/accrual values the stored table shows.
            # Neutral words only; the two rate strings are Project's own
            # display form for the measured per-hour/dollar default.
            crew_a.Group, crew_a.Code = "North", "CR-A"
            crew_b.Group, crew_b.Code = "South", "CR-B"
            crew_c.Group, crew_c.Code = "North", "CR-C"
            crew_b.StandardRate = "$50.00/hr"
            crew_b.OvertimeRate = "$75.00/hr"
            crew_b.CostPerUse = "$10.00"
            crew_b.AccrueAt = 1  # Start
            crew_c.AccrueAt = 2  # End
            result["resources_after_add"] = resource_facts(project)

            # Crew A on all three tasks -> 300% peak.
            # Crew B on task 1 -> 100% peak.
            # Crew C on tasks 2 and 3 at 50% each -> 100% peak.
            for task_id in (1, 2, 3):
                task = project.Tasks(task_id)
                task.Assignments.Add(TaskID=task.ID,
                                     ResourceID=prop(crew_a, "ID"))
            task = project.Tasks(1)
            task.Assignments.Add(TaskID=task.ID, ResourceID=prop(crew_b, "ID"))
            for task_id in (2, 3):
                task = project.Tasks(task_id)
                assignment = task.Assignments.Add(
                    TaskID=task.ID, ResourceID=prop(crew_c, "ID"))
                assignment.Units = 0.5
            result["calculate_returns"] = val(app.CalculateProject())
            result["resources_after_assign"] = resource_facts(project)
            result["tasks_after_assign"] = [
                {"id": prop(project.Tasks(i), "ID"),
                 "uid": prop(project.Tasks(i), "UniqueID"),
                 "manual": prop(project.Tasks(i), "Manual"),
                 "start": prop(project.Tasks(i), "Start"),
                 "finish": prop(project.Tasks(i), "Finish"),
                 "duration": prop(project.Tasks(i), "Duration")}
                for i in range(1, project.Tasks.Count + 1)
            ]

            # The saved file must store the Resource Sheet as the applied view
            # and the resource Entry table as its applied table.
            result["view_apply_returns"] = val(app.ViewApply("Resource Sheet"))
            result["table_apply_returns"] = val(app.TableApply("Entry"))
            result["applied_before_save"] = applied_facts(project)
            result["tables_before_save"] = all_tables(project)

            # What Project itself prints in the resource rows' cells (every
            # stored column's value, in the rows the gate compares).
            columns = ("ID", "Name", "Type", "MaterialLabel", "Initials",
                       "Group", "MaxUnits", "StandardRate", "OvertimeRate",
                       "CostPerUse", "AccrueAt", "BaseCalendar", "Code", "Work",
                       "PeakUnits", "Overallocated")
            result["cells_before_save"] = [
                shown_cell(app, row, column)
                for row in (1, 2, 3)
                for column in columns
            ]

            app.FileSaveAs(FIXTURE)
            app.FileClose(0)

            # Reopen the SAVED file: these are the facts the Qt gate uses.
            app.FileOpen(FIXTURE)
            reopened = app.ActiveProject
            result["reopened_applied"] = applied_facts(reopened)
            result["reopened_tables"] = all_tables(reopened)
            result["reopened_resources"] = resource_facts(reopened)
            result["reopened_cells"] = [
                shown_cell(app, row, column)
                for row in (1, 2, 3)
                for column in columns
            ]
            app.FileClose(0)

    result["fixture"] = {
        "path": FIXTURE,
        "bytes": os.path.getsize(FIXTURE),
        "sha256": sha256(FIXTURE),
    }
    resources = result["reopened_resources"]["resources"]
    overallocated = [row["name"] for row in resources.values()
                     if row["overallocated"] in (True, -1, "True", "True ")]
    if len(resources) != 3 or not overallocated:
        raise SystemExit(
            "refusing: the saved file does not carry three resources with an "
            f"overallocated row: {json.dumps(resources)}")
    tables = result["reopened_tables"]["resource_tables"]
    if not tables["tables"]:
        raise SystemExit("refusing: the saved file stores no resource table")
    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
        handle.write("\n")
    log("reopened resource tables: "
        + json.dumps([t["name"] for t in result["reopened_tables"]
                      ["resource_tables"]["tables"]]))
    log("overallocated: " + json.dumps(overallocated))
    print("wrote", os.path.relpath(RESULT, ROOT))
    print("fixture", FIXTURE, result["fixture"]["sha256"])


if __name__ == "__main__":
    main()
