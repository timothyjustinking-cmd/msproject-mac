#!/usr/bin/env python3
"""Wave 31 (G3): read-only Project facts the Resource Sheet renderer needs.

The committed fixture (tests/data/wave31-resource-sheet.mpp) was saved by the
G3 build script.  This probe reopens a COPY of it and records the facts the
model and the Qt gate must follow that the earlier facts file did not capture:

  * the application's currency display options (CurrencySymbol,
    SymbolPlacement, CurrencyDigits, ProjectCurrency) -- Project prints the
    resource rates and Cost/Use through these, and they are OPTIONS (the file
    stores only the currency CODE, 0x024013bb = "AUD", not the symbol);
  * every resource TableField's AlignData/AlignTitle, so the port's column
    alignment is Project's, not a guess;
  * the Table's RowHeight setting (the PjRowHeight enum the fixture stores);
  * the resource rows' full value set, including the rate strings Project
    formats.

The probe saves nothing and opens a working copy, so the committed fixture is
never handed to FileOpen.  Output: build/w31/W31-G3-probe.json (not committed).

    python -I scripts/oracle_w31_resource_sheet_probe.py
"""

import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import log, oracle_session  # noqa: E402
from oracle_w3 import val  # noqa: E402

FIXTURE = os.path.join(ROOT, "tests", "data", "wave31-resource-sheet.mpp")
OUT = os.path.join(ROOT, "build", "w31")
RESULT = os.path.join(OUT, "W31-G3-probe.json")
WORK = os.path.join(ROOT, "build", "oracle-work", "wave31-resource-sheet-probe.mpp")


def prop(obj, name):
    try:
        return val(getattr(obj, name))
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def main():
    os.makedirs(OUT, exist_ok=True)
    shutil.copyfile(FIXTURE, WORK)
    result = {}
    with oracle_session() as app:
        app.FileOpen(WORK)
        project = app.ActiveProject

        result["currency_options"] = {
            "CurrencySymbol": prop(app.OptionsView, "CurrencySymbol"),
            "SymbolPlacement": prop(app.OptionsView, "SymbolPlacement"),
            "CurrencyDigits": prop(app.OptionsView, "CurrencyDigits"),
            "ProjectCurrency": prop(app.OptionsView, "ProjectCurrency"),
        }
        result["project_currency"] = {
            "CurrencyCode": prop(project, "CurrencyCode"),
            "CurrencySymbol": prop(project, "CurrencySymbol"),
        }

        tables = {}
        for table in project.ResourceTables:
            records = {
                "name": prop(table, "Name"),
                "row_height": prop(table, "RowHeight"),
                "adjust_header_row_height": prop(table, "AdjustHeaderRowHeight"),
                "fields": [],
            }
            for field in table.TableFields:
                records["fields"].append(
                    {
                        "index": prop(field, "Index"),
                        "field": prop(field, "Field"),
                        "title": prop(field, "Title"),
                        "width": prop(field, "Width"),
                        "align_data": prop(field, "AlignData"),
                        "align_title": prop(field, "AlignTitle"),
                        "auto_wrap": prop(field, "AutoWrap"),
                    }
                )
            tables[records["name"]] = records
        result["resource_tables"] = tables

        resources = {}
        for resource in project.Resources:
            if resource.ID == -1:
                continue
            resources[str(resource.ID)] = {
                key: prop(resource, key)
                for key in (
                    "ID", "UniqueID", "Name", "Initials", "Type",
                    "MaterialLabel", "Group", "MaxUnits", "StandardRate",
                    "OvertimeRate", "CostPerUse", "AccrueAt", "BaseCalendar",
                    "Code", "Work", "PeakUnits", "Overallocated",
                )
            }
        result["resources"] = resources

        # Cell texts are in W31-G3-resource-sheet.json (reopened_cells); this
        # probe only adds the options/alignment facts above.
        app.FileCloseEx(0)
    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=1)
    log("wrote " + RESULT)


if __name__ == "__main__":
    main()
