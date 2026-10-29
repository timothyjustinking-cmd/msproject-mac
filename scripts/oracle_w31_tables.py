#!/usr/bin/env python3
"""Wave 31 (G1, W31-1.3): a Project-saved fixture with TWO stored task tables.

The Tables command must switch between the tables a file stores, and the list
must come from the file -- so CI needs a file with more than one task table.
Every committed fixture before this one stores a single `&Entry`, and Project's
`TableEdit(Create=True, NewName=...)` is the measured way to add one (wave 14:
create makes a COPY of the named table).  This script:

  1. copies synthetic-base.mpp under the neutral root C:/w31/tables;
  2. opens it in Project 2021 with neutral Author/Manager/Company/UserName/
     Title;
  3. creates the task table "Review" as a copy of Entry, then replaces its
     column at ColumnPosition 0 (the record after ID) with Text1 -- the
     replacement TableEdit call wave 14 measured;
  4. applies Entry again, so the file's APPLIED table is still Entry and the
     second table is genuinely an alternative the command can switch to;
  5. saves, reopens and records both tables' fields through COM, so the
     fixture's shape is measured rather than inferred.

The script only writes under C:/w31 and build/w31; the coordinator copies the
saved file into tests/data and updates the hash/README by hand after reviewing
the port's reader dump.  Facts go to build/w31/W31-1.3-tables-fixture.json (not
committed).  The script refuses to overwrite an existing fixture.

    python -I scripts/oracle_w31_tables.py
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
from oracle_privacy import neutralise  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w31/tables"
OUT = os.path.join(ROOT, "build", "w31")
RESULT = os.path.join(OUT, "W31-1.3-tables-fixture.json")
SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")
BASE = os.path.join(NEUTRAL, "wave31-tables-base.mpp")
FIXTURE = os.path.join(NEUTRAL, "wave31-tables.mpp")


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def val(v):
    if isinstance(v, (int, float, bool, str)) or v is None:
        return v
    return repr(v)


def prop(obj, name):
    try:
        return val(getattr(obj, name))
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def table_facts(project):
    """The stored task tables and their columns, by COM name/index only."""
    tables = []
    count = prop(project.TaskTables, "Count")
    if not isinstance(count, int):
        return {"count": count, "tables": tables}
    for index in range(1, count + 1):
        table = project.TaskTables(index)
        columns = []
        try:
            fields = table.TableFields
            for column_index in range(1, fields.Count + 1):
                field = fields(column_index)
                columns.append({
                    "field": prop(field, "Field"),
                    "title": prop(field, "Title"),
                    "width": prop(field, "Width"),
                })
        except Exception as exc:  # noqa: BLE001
            columns.append({"error": repr(exc)})
        tables.append({"index": index, "name": prop(table, "Name"),
                       "columns": columns})
    return {"count": count, "tables": tables}


def main():
    os.makedirs(NEUTRAL, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    if os.path.exists(FIXTURE):
        sys.exit(f"refusing: {FIXTURE} already exists (inspect it first)")
    shutil.copyfile(SOURCE, BASE)

    result = {"version": None, "source": "tests/data/synthetic-base.mpp",
              "neutral_root": NEUTRAL}
    with oracle_session() as app:
        with Guard(600):
            result["version"] = str(app.Version)
            app.FileOpen(BASE)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Tables Fixture")
            result["before"] = table_facts(project)

            # Create "Review" as a copy of Entry.  Wave 14 measured that the
            # Create call copies the named table and ignores FieldName.
            result["create_returns"] = val(app.TableEdit(
                Name="Entry", TaskTable=True, Create=True,
                OverwriteExisting=True, NewName="Review",
                FieldName="Number1", NewFieldName="Number1",
                Title="Number1", Width=12, ColumnPosition=0))
            log("created Review: " + str(result["create_returns"]))

            # Replace the copy's column at ColumnPosition 0 (Entry's second
            # column) with Text1, so the two tables are visibly different.
            result["replace_returns"] = val(app.TableEdit(
                Name="Review", TaskTable=True, Create=False,
                OverwriteExisting=True, FieldName="Text1",
                NewFieldName="Text1", Title="Text1", Width=12,
                ColumnPosition=0))
            log("edited Review: " + str(result["replace_returns"]))

            # Leave the file APPLYING Entry: the stored alternative is Review.
            result["apply_entry_returns"] = val(app.TableApply("Entry"))
            result["after_edits"] = table_facts(project)
            result["applied_table"] = prop(project, "CurrentTable")
            app.FileSaveAs(FIXTURE)
            app.FileClose(0)

            # Reopen the saved file and measure what actually persisted.
            app.FileOpen(FIXTURE)
            reopened = app.ActiveProject
            result["reopened"] = table_facts(reopened)
            result["reopened_applied_table"] = prop(reopened, "CurrentTable")
            app.FileClose(0)

    result["fixture"] = {
        "path": FIXTURE,
        "bytes": os.path.getsize(FIXTURE),
        "sha256": sha256(FIXTURE),
    }
    names = [t["name"] for t in result["reopened"]["tables"]]
    if len(names) < 2:
        raise SystemExit(f"refusing: the saved file stores one table: {names}")
    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
        handle.write("\n")
    log("reopened tables: " + json.dumps(names))
    print("wrote", os.path.relpath(RESULT, ROOT))
    print("fixture", FIXTURE, result["fixture"]["sha256"])


if __name__ == "__main__":
    main()
