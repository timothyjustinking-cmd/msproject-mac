#!/usr/bin/env python3
"""Wave 34 (W34-0.3): the note RTF rules the wave-30 pair did not settle.

wave30-notes measured ONE new plain single-line ASCII task note.  The decode
note (spec/writers/note-richtext.md) lists what is still open: escaping of
`\\`, `{` and `}`; non-ASCII text; a multi-line note; replacing an existing
note; clearing one; and two note changes in one save.  This script measures
each as its own pair on tests/data/synthetic-base.mpp (three top-level 0 %
manual leaves, the project calendar, one placeholder assignment per task, one
work resource; no notes in the base).

  escape     fresh base, control with no note, edit sets task 2's Notes to
             `Back\\slash and {braces} end}`.
  nonascii   the same shape with `Café naïve résumé.`
  multiline  the same shape with a CRLF between two lines.
  replace    seed session sets task 2's note, then a SECOND session saves the
             control (the note is loaded from disk), replaces the text and
             saves the edit.
  clear      the same seed-and-reopen shape; the edit sets the note to "".
  two        seed session sets notes on tasks 2 and 3; the edit clears task
             2's note and replaces task 3's in one save.

The seed-and-reopen shape is the wave-30 calculate pattern: the control and
the edit are saved in one session from a file that already stores the note,
so the replace/clear paths see a loaded note, and the two saves share the
session's per-save identity churn.

All edits are plain COM property sets through scripts/oracle_lock.py on copies
under C:/w34 (neutralise() before the first save).  Only synthetic text is
written; facts are recorded by task ID/UniqueID and note length.

    python -I scripts/oracle_w34_notes.py

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

NEUTRAL = r"C:/w34"
FIXTURE_DIR = os.path.join(NEUTRAL, "fixtures-notes")
OUT = os.path.join(ROOT, "build", "w34")
RESULT = os.path.join(OUT, "W34-0.3-notes.json")
WORK = os.path.join(OUT, "notes-work")
BASE_SOURCE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")

ESCAPE_TEXT = "Back\\slash and {braces} end}"
NONASCII_TEXT = "Caf\u00e9 na\u00efve r\u00e9sum\u00e9."
MULTILINE_TEXT = "First line of the note.\r\nSecond line."
REPLACE_BEFORE = "First note text on task two."
REPLACE_AFTER = "Second note text, longer than the first."
CLEAR_BEFORE = "Note to be cleared."
TWO_BEFORE = {2: "Note two to clear.", 3: "Note three before."}
TWO_AFTER = {2: "", 3: "Note three after the edit."}

PAIRS = {
    "escape": {
        "control": "wave34-note-escape-control.mpp",
        "edit": "wave34-note-escape.mpp",
        "seed": [],
        "changes": [(2, ESCAPE_TEXT)],
        "note": "escaping of backslash and braces",
    },
    "nonascii": {
        "control": "wave34-note-nonascii-control.mpp",
        "edit": "wave34-note-nonascii.mpp",
        "seed": [],
        "changes": [(2, NONASCII_TEXT)],
        "note": "non-ASCII text under ansicpg1252",
    },
    "multiline": {
        "control": "wave34-note-multiline-control.mpp",
        "edit": "wave34-note-multiline.mpp",
        "seed": [],
        "changes": [(2, MULTILINE_TEXT)],
        "note": "a two-line note",
    },
    "replace": {
        "control": "wave34-note-replace-control.mpp",
        "edit": "wave34-note-replace.mpp",
        "seed": [(2, REPLACE_BEFORE)],
        "changes": [(2, REPLACE_AFTER)],
        "note": "replace an existing note",
    },
    "clear": {
        "control": "wave34-note-clear-control.mpp",
        "edit": "wave34-note-clear.mpp",
        "seed": [(2, CLEAR_BEFORE)],
        "changes": [(2, "")],
        "note": "clear an existing note",
    },
    "two": {
        "control": "wave34-note-two-control.mpp",
        "edit": "wave34-note-two.mpp",
        "seed": sorted(TWO_BEFORE.items()),
        "changes": sorted(TWO_AFTER.items()),
        "note": "clear one note and replace another in one save",
    },
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


def notes_length(project, index):
    try:
        return len(project.Tasks(index).Notes)
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def row(project, index):
    task = project.Tasks(index)
    return {
        "id": prop(task, "ID"),
        "unique_id": prop(task, "UniqueID"),
        "notes_len": notes_length(project, index),
        "start": prop(task, "Start"),
        "finish": prop(task, "Finish"),
        "duration": prop(task, "Duration"),
        "percent_complete": prop(task, "PercentComplete"),
        "manual": prop(task, "Manual"),
    }


def facts(project):
    out = {"project": {"tasks": project.Tasks.Count}}
    for index in range(1, project.Tasks.Count + 1):
        out[str(index)] = row(project, index)
    return out


def notes_texts(project):
    return {str(index): prop(project.Tasks(index), "Notes")
            for index in range(1, project.Tasks.Count + 1)}


def save_as(app, name):
    path = os.path.join(FIXTURE_DIR, name)
    app.FileSaveAs(path)
    return name


def open_copy(app, tag):
    source = working_copy(BASE_SOURCE, tag)
    os.makedirs(WORK, exist_ok=True)
    copy = os.path.join(WORK, tag + ".mpp")
    shutil.copyfile(source, copy)
    app.FileOpen(copy)
    project = app.ActiveProject
    neutralise(app, project, "Synthetic W34 Notes Fixture")
    return project


def set_notes(project, changes):
    for index, text in changes:
        project.Tasks(index).Notes = text


def reopen_facts(app, name):
    app.FileOpen(os.path.join(FIXTURE_DIR, name))
    project = app.ActiveProject
    result = {"facts": facts(project), "notes": notes_texts(project)}
    app.FileClose(0)
    return result


def measure(app, result, key, spec):
    tag = "w34-0.3-" + key
    entry = result["pairs"].setdefault(key, {})
    entry["note"] = spec["note"]
    entry["texts_seeded"] = [[i, t] for i, t in spec["seed"]]
    entry["texts_set"] = [[i, t] for i, t in spec["changes"]]

    project = open_copy(app, tag)
    if spec["seed"]:
        set_notes(project, spec["seed"])
        seed_name = "seed-" + key + ".mpp"
        seed_path = os.path.join(FIXTURE_DIR, seed_name)
        if os.path.exists(seed_path):
            os.remove(seed_path)
        app.FileSaveAs(seed_path)
        entry["seed"] = seed_name
        entry["seed_notes"] = notes_texts(project)
        app.FileClose(0)

        # Reopen the seed so the control's note is loaded from disk, exactly
        # as an editor session sees an existing note.
        reopened = os.path.join(WORK, tag + "-seed.mpp")
        shutil.copyfile(seed_path, reopened)
        app.FileOpen(reopened)
        project = app.ActiveProject
        entry["reopened_notes"] = notes_texts(project)
        for index, text in spec["seed"]:
            if notes_texts(project).get(str(index)) != text:
                raise SystemExit(
                    f"refusing: the seed note on task {index} did not survive "
                    f"the reopen ({notes_texts(project)!r})")

    entry["control_before_save"] = facts(project)
    entry["control_notes"] = notes_texts(project)
    entry["control"] = save_as(app, spec["control"])
    set_notes(project, spec["changes"])
    entry["edit_facts_before_save"] = facts(project)
    entry["edit_notes"] = notes_texts(project)
    entry["edit"] = save_as(app, spec["edit"])
    entry["edit_facts"] = facts(project)
    app.FileClose(0)


def main():
    for spec in PAIRS.values():
        for name in (spec["control"], spec["edit"]):
            path = os.path.join(FIXTURE_DIR, name)
            if os.path.exists(path):
                sys.exit(f"refusing: {path} already exists (inspect the pair first)")
    if not os.path.isfile(BASE_SOURCE):
        sys.exit(f"fixture missing: {BASE_SOURCE}")
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)

    result = {"version": None, "pairs": {}}
    with oracle_session() as app:
        with Guard(1800):
            result["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"
            for key, spec in PAIRS.items():
                measure(app, result, key, spec)

            # Reopen every committed file: what Project itself displays.
            for key, spec in PAIRS.items():
                entry = result["pairs"][key]
                entry["control_reopen"] = reopen_facts(app, spec["control"])
                entry["edit_reopen"] = reopen_facts(app, spec["edit"])

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")
    for key, spec in PAIRS.items():
        entry = result["pairs"][key]
        for name in (spec["control"], spec["edit"]):
            path = os.path.join(FIXTURE_DIR, name)
            log(f"fixture {name} ({os.path.getsize(path)} bytes)")
        log(f"{key}: control notes {entry['control_notes']} -> edit notes "
            f"{entry['edit_notes']}")
        log(f"  reopen control {entry['control_reopen']['facts']}")
        log(f"  reopen edit    {entry['edit_reopen']['facts']}")


if __name__ == "__main__":
    main()
