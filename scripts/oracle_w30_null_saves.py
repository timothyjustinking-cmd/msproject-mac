#!/usr/bin/env python3
"""Wave 30: a NULL save of every new save pair's control.

The W29-1.0b discipline (see tests/save_pair_noise.h): each committed pair's
control gets a Project 2021 no-edit save, committed as
tests/data/<control-stem>-null.mpp.  A byte that differs between the control
and its null save is per-save noise (identity GUIDs, the stored path/session
words, timestamps, view/undo state); every other byte of Project's edit save
is the edit's effect, which the port must reproduce.

For each control the script:
  1. copies the committed fixture to build/w30/null-work (never opens the
     fixture in place -- Project rewrites files it opens);
  2. opens the copy, sets UserName neutral, and FileSaveAs the null path under
     the neutral root C:/w30/null;
  3. closes the file, copies the save out and records its size and sha256 plus
     Project's row facts.

Controls whose -null fixture already exists are skipped, so this script can be
extended as the wave adds pairs.  It refuses to run when every control is
already done.

    python -I scripts/oracle_w30_null_saves.py
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
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w30"
NULL_DIR = NEUTRAL + "/null"
WORK = os.path.join(ROOT, "build", "w30", "null-work")
SNAPSHOTS = os.path.join(ROOT, "build", "w30", "null-saves")
FIXTURE_DIR = os.path.join(ROOT, "tests", "data")
RESULT = os.path.join(ROOT, "build", "w30", "W30-null-saves.json")

CONTROLS = [
    "wave30-task-mode-auto-control.mpp",
    "wave30-task-mode-manual-control.mpp",
    "wave30-insert-summary-control.mpp",
    "wave30-insert-milestone-control.mpp",
    "wave30-inactivate-control.mpp",
    "wave30-inactivate-second-control.mpp",
    "wave30-respect-links-auto-control.mpp",
    "wave30-respect-links-manual-control.mpp",
    "wave30-move-back-control.mpp",
    "wave30-move-forward-control.mpp",
    "wave30-notes-control.mpp",
    "wave30-timeline-control.mpp",
    # W30-1.9: the Font Bold pair (Application.FontBold on one Name cell).
    "wave30-textstyle-control.mpp",
    # W30-2.0: the Resource tab's first pair (Add Resources, a work row).
    "wave30-resource-add-control.mpp",
]


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def null_fixture_name(control):
    stem, ext = os.path.splitext(control)
    return stem + "-null" + ext


def main():
    pending = []
    for control in CONTROLS:
        path = os.path.join(FIXTURE_DIR, control)
        if not os.path.exists(path):
            sys.exit(f"refusing: {path} is not a committed fixture")
        if os.path.exists(os.path.join(FIXTURE_DIR, null_fixture_name(control))):
            log(f"skip {control}: its null save is already committed")
            continue
        pending.append(control)
    if not pending:
        sys.exit("every control already has a null save; nothing to do")
    os.makedirs(NULL_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    os.makedirs(SNAPSHOTS, exist_ok=True)

    facts = {"version": None, "null_saves": {}}
    previous = {}
    if os.path.exists(RESULT):
        with open(RESULT, encoding="utf-8") as handle:
            previous = json.load(handle).get("null_saves", {})
    facts["null_saves"] = previous

    with oracle_session() as app:
        with Guard(1200):
            facts["version"] = str(app.Version)
            app.UserName = "Fixture Builder"
            for control in pending:
                source = os.path.join(FIXTURE_DIR, control)
                copy = os.path.join(WORK, control)
                shutil.copyfile(source, copy)
                target = os.path.join(NULL_DIR, null_fixture_name(control))
                if os.path.exists(target):
                    os.remove(target)
                app.FileOpen(copy)
                project = app.ActiveProject
                before = {
                    "author": str(project.Author),
                    "manager": str(project.Manager),
                    "company": str(project.Company),
                    "title": str(project.Title),
                    "tasks": project.Tasks.Count,
                }
                app.FileSaveAs(target)
                after = {
                    "author": str(project.Author),
                    "manager": str(project.Manager),
                    "company": str(project.Company),
                    "title": str(project.Title),
                    "tasks": project.Tasks.Count,
                }
                app.FileClose(0)
                # Copy only after the close: Project pre-allocates the open
                # file and truncates on close (W29-1.0b).
                snapshot = os.path.join(
                    SNAPSHOTS, null_fixture_name(control)[:-4] + "-save1.mpp")
                shutil.copyfile(target, snapshot)
                first = os.path.join(
                    FIXTURE_DIR, null_fixture_name(control))
                shutil.copyfile(snapshot, first)
                entry = {
                    "control": control,
                    "null_fixture": null_fixture_name(control),
                    "null_save_path": target,
                    "metadata_before": before,
                    "metadata_after": after,
                    "sha256": sha256(snapshot),
                    "bytes": os.path.getsize(snapshot),
                }
                facts["null_saves"][control] = entry
                log(f"{control}: null save {entry['bytes']} bytes, "
                    f"sha256 {entry['sha256'][:16]}...")

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")


if __name__ == "__main__":
    main()
