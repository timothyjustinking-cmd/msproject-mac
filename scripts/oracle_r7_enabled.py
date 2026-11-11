#!/usr/bin/env python3
"""Round 7, item 4: neutral pairs for the enabled edits whose only evidence is the corpus.

Three enabled port edits are measured against Project only through files CI never
has (the wave-10 probes corpus/probe/w10-*.mpp, which are git-ignored): the
`214/CTable` date-format byte of table 0, the replacement of an existing column of
table 0, and a custom-field formula SET in `114/Props`.  This script repeats the
port's measured COM shape on a synthetic base, with the wave 35 G3 chain: seed,
reopen, CONTROL, ONE edit, EDIT, reopen both for facts, and the control's NULL from
a copy with Application.UserName only.

  pair                   shape (source)                                  base
  table0-column-replace  TableEdit(Name='Entry', TaskTable=True,          synthetic-base.mpp
                         Create=False, OverwriteExisting=True,
                         FieldName='Text1', NewFieldName='Text1',
                         Title='Text1', Width=12, ColumnPosition=0):
                         the replacement call of oracle_w31_tables.py,
                         measured in wave 14 to replace the record at index 1
                         (core/viewstate/viewstate.h set_table_column).
  table0-date-format     TableEdit(Name='Entry', TaskTable=True,          synthetic-base.mpp
                         Create=False, OverwriteExisting=True,
                         DateFormat=5): the date-format byte alone
                         (set_table_date_format, 0x05 for DateFormat 5).
  table0-date-format-w10 the positional call of oracle_w10.prefs():      synthetic-base.mpp
                         TableEdit('Entry', True, False, True, None,
                         'Number1', 'Number1', 'Number1', 10, 0, False,
                         True, 5).  It names a column as well as the
                         date format, so it may replace column 1 too;
                         the JSON records whether it did.  Made because
                         the date-format-only call above may be ignored.
  formula-set-text       CustomFieldSetFormula(Text1 = 0x0B400033,        synthetic-base.mpp
                         '[Name] & "-" & [Duration]'), the formula of
                         oracle_w10.CUSTOM.
  formula-set-number     CustomFieldSetFormula(Number1 = 0x0B400057,      synthetic-base.mpp
                         '[Duration]*2'), likewise.

The port's formula SET is restricted to Text1..30 and Number1..20 (W33-1.3), the two
kinds above.  Formula REPLACE (a control that already holds a formula) and CLEAR are
not made here: CLEAR is disabled in the port for want of a measured flag, and REPLACE
was not in the work order.

Calculation mode: AUTOMATIC, set explicitly after the first open and read back.  The
wave-10 and wave-14 oracles did not set it; the synthetic g3-style pairs are made
under automatic.

After each pair the script lists, with olefile when it is installed, the streams
whose bytes differ between the control and the edit, and between the control and its
null, so the coordinator sees at once whether an edit changed `214/CTable/Var2Data`
(date format and column) or `114/Props` (formula) beyond per-save noise.  A pair
whose edit changed no stream beyond the null's set is flagged "edit_changed_beyond_null".

Files under the neutral root C:/w35/r7-enabled with neutral Author, Manager,
Company, UserName and Title.  Names wave36-r7-<pair>-control.mpp,
wave36-r7-<pair>-control-null.mpp and wave36-r7-<pair>.mpp.  Facts, by ID and field
only (no task name is read), go to build/r7/oracle_r7_enabled.json.  Nothing is
written to tests/data.

    python scripts/oracle_r7_enabled.py --list
    python scripts/oracle_r7_enabled.py [pair ...]

With no pair every pair runs.  The script refuses to run if a target exists.
"""

import argparse
import datetime as dt
import hashlib
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def find_root():
    """The repo root, whether this file sits in scripts/ or in build/r7/scripts/."""
    directory = HERE
    while True:
        if os.path.isfile(os.path.join(directory, "scripts", "oracle_lock.py")):
            return directory
        parent = os.path.dirname(directory)
        if parent == directory:
            sys.exit("cannot find the repo root (scripts/oracle_lock.py)")
        directory = parent


ROOT = find_root()
sys.path.insert(0, os.path.join(ROOT, "scripts"))

from oracle_lock import oracle_session, log, working_copy  # noqa: E402
from oracle_privacy import AUTHOR, neutralise  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w35/r7-enabled"
FIXTURE_DIR = NEUTRAL + "/fixtures"
NULL_DIR = NEUTRAL + "/nulls"
WORK = NEUTRAL + "/work"
OUT = os.path.join(ROOT, "build", "r7")
RESULT = os.path.join(OUT, "oracle_r7_enabled.json")
DATA = os.path.join(ROOT, "tests", "data")
BASE = "synthetic-base.mpp"
TITLE = "Synthetic R7 Enabled Fixture"

TEXT1 = 0x0B400033    # 188743731
NUMBER1 = 0x0B400057  # 188743767


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


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def facts(app, project):
    out = {"tasks": {}}
    for index in range(1, project.Tasks.Count + 1):
        task = project.Tasks(index)
        out["tasks"][str(index)] = {
            "id": prop(task, "ID"),
            "unique_id": prop(task, "UniqueID"),
            "manual": prop(task, "Manual"),
            "duration": prop(task, "Duration"),
            "start": prop(task, "Start"),
            "finish": prop(task, "Finish"),
        }
    out["task_tables"] = table_facts(project)
    return out


def table_facts(project):
    """The stored task tables and their columns: field, title, width only."""
    tables = []
    collection = getattr(project, "TaskTables", None)
    count = prop(collection, "Count") if collection is not None else None
    if not isinstance(count, int):
        return {"count": count, "tables": tables}
    for index in range(1, count + 1):
        table = collection(index)
        columns = []
        try:
            fields = table.TableFields
            for column in range(1, fields.Count + 1):
                field = fields(column)
                columns.append({"field": prop(field, "Field"),
                                "title": prop(field, "Title"),
                                "width": prop(field, "Width")})
        except Exception as exc:  # noqa: BLE001
            columns.append({"error": repr(exc)})
        tables.append({"index": index, "name": prop(table, "Name"),
                       "columns": columns})
    return {"count": count, "tables": tables}


def stream_hashes(path):
    """name -> sha256 of every stream, or None when olefile is unavailable."""
    try:
        import olefile
    except ImportError:
        return None
    hashes = {}
    ole = olefile.OleFileIO(path)
    try:
        for entry in ole.listdir(streams=True, storages=False):
            data = ole.openstream(entry).read()
            hashes["/".join(entry)] = hashlib.sha256(data).hexdigest()
    finally:
        ole.close()
    return hashes


def differing(a, b):
    if a is None or b is None:
        return None
    names = sorted(set(a) | set(b))
    return [name for name in names if a.get(name) != b.get(name)]


# ---- the edits: edit(app, project) -> info ------------------------------------

def edit_column_replace(app, project):
    returned = app.TableEdit(Name="Entry", TaskTable=True, Create=False,
                             OverwriteExisting=True, FieldName="Text1",
                             NewFieldName="Text1", Title="Text1", Width=12,
                             ColumnPosition=0)
    return {"call": "TableEdit(Name='Entry', TaskTable=True, Create=False, "
                    "OverwriteExisting=True, FieldName='Text1', "
                    "NewFieldName='Text1', Title='Text1', Width=12, "
                    "ColumnPosition=0)",
            "returned": val(returned)}


def edit_date_format(app, project):
    returned = app.TableEdit(Name="Entry", TaskTable=True, Create=False,
                             OverwriteExisting=True, DateFormat=5)
    return {"call": "TableEdit(Name='Entry', TaskTable=True, Create=False, "
                    "OverwriteExisting=True, DateFormat=5)",
            "returned": val(returned)}


def edit_date_format_w10(app, project):
    returned = app.TableEdit("Entry", True, False, True, None, "Number1",
                             "Number1", "Number1", 10, 0, False, True, 5)
    return {"call": "TableEdit('Entry', True, False, True, None, 'Number1', "
                    "'Number1', 'Number1', 10, 0, False, True, 5)",
            "returned": val(returned)}


def edit_formula(field_id, label, formula):
    def edit(app, project):
        returned = app.CustomFieldSetFormula(field_id, formula)
        info = {"call": f"CustomFieldSetFormula({label} = {field_id:#x}, {formula!r})",
                "returned": val(returned)}
        try:
            info["formula_read_back"] = val(app.CustomFieldGetFormula(field_id))
        except Exception as exc:  # noqa: BLE001
            info["formula_read_back"] = f"<unreadable: {exc}>"
        return info
    return edit


# pair -> (edit function, one-line description, the stream that should change)
EDITS = {
    "table0-column-replace": (edit_column_replace,
                              "replace Entry's column at ColumnPosition 0 with Text1",
                              "214/CTable/Var2Data"),
    "table0-date-format": (edit_date_format,
                           "table 0 date format 5, no column named",
                           "214/CTable/Var2Data"),
    "table0-date-format-w10": (edit_date_format_w10,
                               "table 0 date format 5, the wave-10 positional call",
                               "214/CTable/Var2Data"),
    "formula-set-text": (edit_formula(TEXT1, "Text1", '[Name] & "-" & [Duration]'),
                         "Text1 formula SET", "114/Props"),
    "formula-set-number": (edit_formula(NUMBER1, "Number1", "[Duration]*2"),
                           "Number1 formula SET", "114/Props"),
}
PAIRS = tuple(EDITS)


def control_path(pair):
    return f"{FIXTURE_DIR}/wave36-r7-{pair}-control.mpp"


def edit_path(pair):
    return f"{FIXTURE_DIR}/wave36-r7-{pair}.mpp"


def null_path(pair):
    return f"{NULL_DIR}/wave36-r7-{pair}-control-null.mpp"


def targets(pair):
    return (control_path(pair), edit_path(pair), null_path(pair))


def set_automatic(app, entry):
    try:
        app.Calculation = 1  # pjAutomatic
    except Exception as exc:  # noqa: BLE001
        entry["calculation_set_error"] = repr(exc)
    entry["calculation"] = prop(app, "Calculation")
    log(f"calculation = {entry['calculation']!r}")


def reopen_facts(app, path):
    app.FileOpen(path)
    out = facts(app, app.ActiveProject)
    app.FileClose(0)
    return out


def measure(app, pair, entry):
    edit_fn = EDITS[pair][0]
    seed = f"{WORK}/{pair}-seed.mpp"
    staged = working_copy(os.path.join(DATA, BASE), "r7-enabled-" + pair)
    base_copy = f"{WORK}/{pair}-base.mpp"
    shutil.copyfile(staged, base_copy)
    app.FileOpen(base_copy)
    set_automatic(app, entry)
    neutralise(app, app.ActiveProject, TITLE)
    entry["base"] = BASE
    entry["pending_facts"] = facts(app, app.ActiveProject)
    app.FileSaveAs(seed)
    app.FileClose(0)

    app.FileOpen(seed)
    project = app.ActiveProject
    entry["control_facts"] = facts(app, project)
    app.FileSaveAs(control_path(pair))
    try:
        entry["edit"] = edit_fn(app, project)
    except Exception as exc:  # noqa: BLE001
        entry["edit"] = f"<raised: {type(exc).__name__}: {exc}>"
        app.FileClose(0)
        os.remove(seed)
        os.remove(control_path(pair))
        return False
    entry["edit_facts"] = facts(app, project)
    app.FileSaveAs(edit_path(pair))
    app.FileClose(0)
    os.remove(seed)

    entry["control_reopen"] = reopen_facts(app, control_path(pair))
    entry["edit_reopen"] = reopen_facts(app, edit_path(pair))

    copy = f"{WORK}/{pair}-control-for-null.mpp"
    shutil.copyfile(control_path(pair), copy)
    app.FileOpen(copy)
    app.UserName = AUTHOR
    app.FileSaveAs(null_path(pair))
    app.FileClose(0)

    entry["files"] = {label: {"bytes": os.path.getsize(path),
                              "sha256": sha256(path)}
                      for label, path in (("control", control_path(pair)),
                                          ("edit", edit_path(pair)),
                                          ("null", null_path(pair)))}
    control_streams = stream_hashes(control_path(pair))
    edit_changed = differing(control_streams, stream_hashes(edit_path(pair)))
    null_changed = differing(control_streams, stream_hashes(null_path(pair)))
    entry["streams_edit_vs_control"] = edit_changed
    entry["streams_null_vs_control"] = null_changed
    expected = EDITS[pair][2]
    if edit_changed is None:
        entry["edit_changed_beyond_null"] = "unchecked: olefile is not installed"
    else:
        entry["edit_changed_beyond_null"] = bool(set(edit_changed) - set(null_changed))
        entry["expected_stream_changed"] = expected in edit_changed
    log(f"{pair}: streams differing from control: edit {edit_changed}, "
        f"null {null_changed}")
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--list", action="store_true",
                    help="print the pairs and do nothing else")
    ap.add_argument("pairs", nargs="*")
    args = ap.parse_args()

    if args.list:
        print(f"neutral root {NEUTRAL}; base tests/data/{BASE}; "
              "calculation: automatic (set explicitly)")
        for pair, (_, description, stream) in EDITS.items():
            print(f"{pair}: {description}; expected stream {stream}")
            print(f"    -> {os.path.basename(control_path(pair))}, "
                  f"{os.path.basename(null_path(pair))}, "
                  f"{os.path.basename(edit_path(pair))}")
        return

    pairs = tuple(args.pairs) or PAIRS
    if not os.path.isfile(os.path.join(DATA, BASE)):
        sys.exit(f"base missing: tests/data/{BASE}")
    for pair in pairs:
        if pair not in EDITS:
            sys.exit(f"unknown pair {pair}")
        for path in targets(pair):
            if os.path.exists(path):
                sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    for directory in (FIXTURE_DIR, NULL_DIR, WORK, OUT):
        os.makedirs(directory, exist_ok=True)

    result = {"version": None, "pairs": {}}
    if os.path.exists(RESULT):
        with open(RESULT, encoding="utf-8") as handle:
            result = json.load(handle)
    with oracle_session() as app:
        with Guard(1800):
            result["version"] = prop(app, "Version")
            app.UserName = AUTHOR
            for pair in pairs:
                entry = result["pairs"].setdefault(pair, {})
                ok = measure(app, pair, entry)
                log(f"{pair}: {'ok' if ok else 'FAILED'} {entry.get('edit')}")

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")


if __name__ == "__main__":
    main()
