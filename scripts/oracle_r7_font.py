#!/usr/bin/env python3
"""Round 7, item 2: the font pairs for wave 36 G1, on the wave 35 G3 font base.

The wave 35 G3 font pairs (scripts/oracle_w35_g3.py) open a committed synthetic
fixture on a copy, seed, reopen, save the CONTROL, make ONE edit and save the EDIT.
This script does the same for six more pairs, and repeats the g3 chain exactly
(seed save, reopen, control save, edit save, reopen of both for facts, the control's
NULL from a copy with Application.UserName only).

  pair              base (tests/data)       edit
  range2            synthetic-base.mpp      Name cells of rows 1 and 2 selected, then
                                            FontBold(True).  ASSERTS and LOGS
                                            ActiveSelection.Tasks.Count == 2 before
                                            the edit; a count other than 2 refuses
                                            the pair and keeps no file.
  bold-italic       synthetic-base.mpp      Name of row 1: FontBold(True) then
                                            FontItalic(True), one save.
  second-change     wave30-textstyle.mpp    Name of row 1, which already holds a Bold
                                            item in the control: FontItalic(True).
  second-cell       wave30-textstyle.mpp    Name of row 2, in the same view as the
                                            control's one item: FontBold(True).
  bold-session2     synthetic-base.mpp      FontBold(True) on Name of row 1, set in a
                                            SECOND Project session: session 1 saves
                                            the control and quits, session 2 starts
                                            fresh, opens a copy of the control and
                                            makes the edit.
  underline-toolbar synthetic-base.mpp      Name of row 1: Underline through the
                                            toolbar-command path (Application.
                                            FontUnderline), the path the committed
                                            Bold pair uses, NOT Font32Ex.

FontUnderline: late binding read it as a property and the call failed with
"'bool' object is not callable" (wave 35, R35G3-7), a harness error.  Here it is
invoked through the IDispatch interface as a METHOD (GetIDsOfNames, then Invoke with
DISPATCH_METHOD), so win32com cannot treat it as a property.  The argument forms
True, -1 and 1 are tried in that order and the one that worked is recorded.
FontBold and FontItalic use the same late-bound call as the committed Bold pair.

Calculation mode: AUTOMATIC, set explicitly (Application.Calculation = 1) after the
first open of each session and read back.  The wave 35 G3 file-based font pairs set
none in the script, and the trace specs record them as automatic (Mode 1).

Files are written under the neutral root C:/w35/r7-font with neutral Author,
Manager, Company, UserName and Title ("Synthetic R7 Font Fixture").  Names:
wave36-r7-font-<pair>-control.mpp, wave36-r7-font-<pair>-control-null.mpp and
wave36-r7-font-<pair>.mpp.  Facts, by ID and UniqueID only (no name is read), go to
build/r7/oracle_r7_font.json.  Nothing is written to tests/data.

    python scripts/oracle_r7_font.py --list
    python scripts/oracle_r7_font.py [pair ...]

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

NEUTRAL = r"C:/w35/r7-font"
FIXTURE_DIR = NEUTRAL + "/fixtures"
NULL_DIR = NEUTRAL + "/nulls"
WORK = NEUTRAL + "/work"
OUT = os.path.join(ROOT, "build", "r7")
RESULT = os.path.join(OUT, "oracle_r7_font.json")
DATA = os.path.join(ROOT, "tests", "data")
TITLE = "Synthetic R7 Font Fixture"


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
            "outline_level": prop(task, "OutlineLevel"),
            "summary": prop(task, "Summary"),
            "milestone": prop(task, "Milestone"),
            "manual": prop(task, "Manual"),
            "duration": prop(task, "Duration"),
            "start": prop(task, "Start"),
            "finish": prop(task, "Finish"),
            "assignments": prop(task.Assignments, "Count"),
        }
    return out


def set_automatic(app, entry):
    try:
        app.Calculation = 1  # pjAutomatic
    except Exception as exc:  # noqa: BLE001
        entry["calculation_set_error"] = repr(exc)
    entry["calculation"] = prop(app, "Calculation")
    log(f"calculation = {entry['calculation']!r}")


def select_cell(app, row, column="Name"):
    app.SelectTaskField(Row=row, Column=column, RowRelative=False)


def extend_to(app, row, column="Name"):
    app.SelectTaskField(Row=row, Column=column, RowRelative=False, Extend=True)


def selection_tasks(app):
    """ActiveSelection.Tasks.Count, or the reason it could not be read."""
    try:
        return app.ActiveSelection.Tasks.Count
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def invoke_method(app, name, *args):
    """Call an Application method through IDispatch as a METHOD.

    win32com's dynamic dispatch reads FontUnderline as a property; Invoke with
    DISPATCH_METHOD removes the guess.
    """
    import pythoncom
    ids = app._oleobj_.GetIDsOfNames(0, name)
    dispid = ids[0] if isinstance(ids, (tuple, list)) else ids
    return app._oleobj_.Invoke(dispid, 0, pythoncom.DISPATCH_METHOD, True, *args)


# ---- the edits: edit(app, project) -> info ------------------------------------

def edit_range2(app, project):
    select_cell(app, 1)
    extend_to(app, 2)
    count = selection_tasks(app)
    log(f"range2: ActiveSelection.Tasks.Count after Extend = {count!r}")
    info = {"selection_tasks_count_after_extend": count, "assert": "== 2"}
    if count != 2:
        # The wave 35 G3 range pair extended with Extend=True and never checked the
        # count.  The wave 31 fill oracle selected two rows with Width=0, Height=1
        # (Height counts rows in addition to the active cell) and read the count.
        app.SelectTaskField(Row=1, Column="Name", RowRelative=False, Width=0,
                            Height=1)
        count = selection_tasks(app)
        info["selection_method"] = "SelectTaskField(Row=1, Column='Name', Width=0, Height=1)"
        log(f"range2: ActiveSelection.Tasks.Count after Height=1 = {count!r}")
    else:
        info["selection_method"] = "SelectTaskField Row=1 then Row=2 Extend=True"
    info["selection_tasks_count"] = count
    if count != 2:
        info["call"] = "NOT MADE: the selection is not two tasks"
        raise RuntimeError(
            f"ActiveSelection.Tasks.Count is {count!r}, not 2; the range pair "
            "would not measure a two-row range")
    returned = app.FontBold(True)
    info["call"] = "two-row selection (see selection_method); FontBold(True)"
    info["returned"] = val(returned)
    return info


def edit_bold_italic(app, project):
    select_cell(app, 1)
    info = {"selection_tasks_count": selection_tasks(app)}
    info["bold_returned"] = val(app.FontBold(True))
    info["italic_returned"] = val(app.FontItalic(True))
    info["call"] = ("SelectTaskField(Row=1, Column='Name'); FontBold(True); "
                    "FontItalic(True)")
    return info


def edit_second_change(app, project):
    select_cell(app, 1)
    info = {"selection_tasks_count": selection_tasks(app)}
    info["returned"] = val(app.FontItalic(True))
    info["call"] = "SelectTaskField(Row=1, Column='Name'); FontItalic(True)"
    return info


def edit_second_cell(app, project):
    select_cell(app, 2)
    info = {"selection_tasks_count": selection_tasks(app)}
    info["returned"] = val(app.FontBold(True))
    info["call"] = "SelectTaskField(Row=2, Column='Name'); FontBold(True)"
    return info


def edit_bold_row1(app, project):
    select_cell(app, 1)
    info = {"selection_tasks_count": selection_tasks(app)}
    info["returned"] = val(app.FontBold(True))
    info["call"] = "SelectTaskField(Row=1, Column='Name'); FontBold(True)"
    return info


def edit_underline_toolbar(app, project):
    select_cell(app, 1)
    info = {"selection_tasks_count": selection_tasks(app)}
    errors = []
    for argument in (True, -1, 1):
        try:
            info["returned"] = val(invoke_method(app, "FontUnderline", argument))
            info["argument"] = repr(argument)
            break
        except Exception as exc:  # noqa: BLE001
            errors.append(f"{argument!r}: {type(exc).__name__}: {exc}")
    else:
        info["errors"] = errors
        raise RuntimeError("FontUnderline invoked as a method failed for every "
                           "argument form: " + "; ".join(errors))
    info["errors_before_success"] = errors
    info["call"] = ("SelectTaskField(Row=1, Column='Name'); IDispatch.Invoke("
                    "FontUnderline, DISPATCH_METHOD, " + info["argument"] + ")")
    return info


# pair -> (base file in tests/data, edit function, one-line description)
EDITS = {
    "range2": ("synthetic-base.mpp", edit_range2,
               "Bold on the Name cells of rows 1 and 2 (selection count asserted)"),
    "bold-italic": ("synthetic-base.mpp", edit_bold_italic,
                    "Bold then Italic on the Name cell of row 1"),
    "second-change": ("wave30-textstyle.mpp", edit_second_change,
                      "Italic on row 1 Name, which the control already has as Bold"),
    "second-cell": ("wave30-textstyle.mpp", edit_second_cell,
                    "Bold on row 2 Name; the control already holds one Bold item"),
    "bold-session2": ("synthetic-base.mpp", edit_bold_row1,
                      "Bold on row 1 Name in a SECOND Project session"),
    "underline-toolbar": ("synthetic-base.mpp", edit_underline_toolbar,
                          "Underline through Application.FontUnderline"),
}
PAIRS = tuple(EDITS)
SESSION2 = ("bold-session2",)


def kind(pair):
    return f"font-{pair}"


def control_path(pair):
    return f"{FIXTURE_DIR}/wave36-r7-{kind(pair)}-control.mpp"


def edit_path(pair):
    return f"{FIXTURE_DIR}/wave36-r7-{kind(pair)}.mpp"


def null_path(pair):
    return f"{NULL_DIR}/wave36-r7-{kind(pair)}-control-null.mpp"


def targets(pair):
    return (control_path(pair), edit_path(pair), null_path(pair))


def reopen_facts(app, path):
    app.FileOpen(path)
    out = facts(app, app.ActiveProject)
    app.FileClose(0)
    return out


def stage_base(pair):
    """The committed base, copied through working_copy() and then to the neutral
    root, so Project never opens the committed file or a path outside C:/w35."""
    base = EDITS[pair][0]
    staged = working_copy(os.path.join(DATA, base), "r7-font-" + pair)
    copy = f"{WORK}/{pair}-base.mpp"
    shutil.copyfile(staged, copy)
    return copy


def save_null(app, pair, entry):
    """The control's NULL: a copy of the control, UserName only, no edit."""
    copy = f"{WORK}/{pair}-control-for-null.mpp"
    shutil.copyfile(control_path(pair), copy)
    app.FileOpen(copy)
    app.UserName = AUTHOR
    app.FileSaveAs(null_path(pair))
    app.FileClose(0)
    entry["null"] = os.path.basename(null_path(pair))


def finish_files(pair, entry):
    entry["files"] = {}
    for label, path in (("control", control_path(pair)), ("edit", edit_path(pair)),
                        ("null", null_path(pair))):
        entry["files"][label] = {"bytes": os.path.getsize(path),
                                 "sha256": sha256(path)}
    entry["edit_differs_from_control"] = (
        entry["files"]["edit"]["sha256"] != entry["files"]["control"]["sha256"])
    log(f"{pair}: edit differs from control: {entry['edit_differs_from_control']}")


def discard(pair):
    for path in targets(pair):
        if os.path.exists(path):
            os.remove(path)


def measure(app, pair, entry):
    """The wave 35 G3 chain for a file base, ending with the control's null."""
    edit_fn = EDITS[pair][1]
    seed = f"{WORK}/{pair}-seed.mpp"
    app.FileOpen(stage_base(pair))
    set_automatic(app, entry)
    # The opened file carries the session's real identity until a save;
    # neutralise it before ANY save.
    neutralise(app, app.ActiveProject, TITLE)
    entry["base"] = EDITS[pair][0]
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
        discard(pair)
        return False
    entry["edit_facts"] = facts(app, project)
    app.FileSaveAs(edit_path(pair))
    app.FileClose(0)
    os.remove(seed)

    entry["control_reopen"] = reopen_facts(app, control_path(pair))
    entry["edit_reopen"] = reopen_facts(app, edit_path(pair))
    save_null(app, pair, entry)
    finish_files(pair, entry)
    return True


def measure_session2_part1(app, pair, entry):
    """Session 1: the seed and the control, then the session ends."""
    seed = f"{WORK}/{pair}-seed.mpp"
    app.FileOpen(stage_base(pair))
    set_automatic(app, entry)
    neutralise(app, app.ActiveProject, TITLE)
    entry["base"] = EDITS[pair][0]
    entry["pending_facts"] = facts(app, app.ActiveProject)
    app.FileSaveAs(seed)
    app.FileClose(0)

    app.FileOpen(seed)
    entry["control_facts"] = facts(app, app.ActiveProject)
    app.FileSaveAs(control_path(pair))
    app.FileClose(0)
    os.remove(seed)
    entry["session1"] = "seed and control saved, Project quit"


def measure_session2_part2(app, pair, entry):
    """Session 2, a fresh Project process: open a copy of the control, edit, save."""
    edit_fn = EDITS[pair][1]
    copy = f"{WORK}/{pair}-session2-edit.mpp"
    shutil.copyfile(control_path(pair), copy)
    app.FileOpen(copy)
    set_automatic(app, entry)
    # The control is already neutral: UserName only, as the null save does, so a
    # revision bump does not separate the edit from the null.
    app.UserName = AUTHOR
    project = app.ActiveProject
    entry["session2_open_facts"] = facts(app, project)
    try:
        entry["edit"] = edit_fn(app, project)
    except Exception as exc:  # noqa: BLE001
        entry["edit"] = f"<raised: {type(exc).__name__}: {exc}>"
        app.FileClose(0)
        discard(pair)
        return False
    entry["edit_facts"] = facts(app, project)
    app.FileSaveAs(edit_path(pair))
    app.FileClose(0)

    entry["control_reopen"] = reopen_facts(app, control_path(pair))
    entry["edit_reopen"] = reopen_facts(app, edit_path(pair))
    save_null(app, pair, entry)
    finish_files(pair, entry)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--list", action="store_true",
                    help="print the pairs and do nothing else")
    ap.add_argument("pairs", nargs="*")
    args = ap.parse_args()

    if args.list:
        print(f"neutral root {NEUTRAL}; calculation: automatic (set explicitly)")
        for pair, (base, _, description) in EDITS.items():
            sessions = "2 sessions" if pair in SESSION2 else "1 session"
            print(f"{pair}: base tests/data/{base}; {description}; {sessions}")
            print(f"    -> {os.path.basename(control_path(pair))}, "
                  f"{os.path.basename(null_path(pair))}, "
                  f"{os.path.basename(edit_path(pair))}")
        return

    pairs = tuple(args.pairs) or PAIRS
    for pair in pairs:
        if pair not in EDITS:
            sys.exit(f"unknown pair {pair}")
        if not os.path.isfile(os.path.join(DATA, EDITS[pair][0])):
            sys.exit(f"base missing: tests/data/{EDITS[pair][0]}")
        for path in targets(pair):
            if os.path.exists(path):
                sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    for directory in (FIXTURE_DIR, NULL_DIR, WORK, OUT):
        os.makedirs(directory, exist_ok=True)

    result = {"version": None, "pairs": {}}
    if os.path.exists(RESULT):
        with open(RESULT, encoding="utf-8") as handle:
            result = json.load(handle)

    single = [pair for pair in pairs if pair not in SESSION2]
    second = [pair for pair in pairs if pair in SESSION2]

    if single:
        with oracle_session() as app:
            with Guard(1800):
                result["version"] = prop(app, "Version")
                app.UserName = AUTHOR
                for pair in single:
                    entry = result["pairs"].setdefault(pair, {})
                    ok = measure(app, pair, entry)
                    log(f"{pair}: {'ok' if ok else 'FAILED'} {entry.get('edit')}")

    for pair in second:
        entry = result["pairs"].setdefault(pair, {})
        with oracle_session() as app:               # session 1
            with Guard(900):
                result["version"] = prop(app, "Version")
                app.UserName = AUTHOR
                measure_session2_part1(app, pair, entry)
        # Project has quit and its process is gone before the next session starts.
        with oracle_session() as app:               # session 2, a new process
            with Guard(900):
                app.UserName = AUTHOR
                ok = measure_session2_part2(app, pair, entry)
                log(f"{pair}: {'ok' if ok else 'FAILED'} {entry.get('edit')}")

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")


if __name__ == "__main__":
    main()
