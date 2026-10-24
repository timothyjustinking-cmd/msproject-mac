#!/usr/bin/env python3
"""Wave 29 (W29-1.0b): a NULL save of every actuals and outline save pair's control.

The wave-28 review (R28-1, residual in the brief's W29-1.0b) showed that a
whole-stream expected failure cannot tell Project's per-save churn (timestamps,
opaque ids, the stored path, recalculation on open) from a change the edit
caused.  This script builds the missing baseline: Project 2021 opens a copy of
each pair's CONTROL file and saves it with NO edit, twice.

For each control fixture the script:
  1. copies the committed fixture to build/w29/null-work (never opens the
     fixture in place -- Project rewrites files it opens);
  2. opens the copy, sets Project's UserName neutral (the file's own neutral
     Author/Manager/Company/Title are kept), and FileSaveAs the null path under
     the neutral root C:/w29/null;
  3. copies the saved file out as -save1.mpp, calls FileSave() again and copies
     the second save out as -save2.mpp (same stored path, so the two snapshots
     differ only in what Project rewrites on every save);
  4. closes the file and records the sizes and sha256 of both snapshots plus
     Project's row facts for the reopen check.

The first snapshot of each control is committed as
tests/data/<control-stem>-null.mpp; the second stays under build/w29 (its
per-stream instability is reported by build/w29/w29_1_0b_analysis.py and
recorded in build/w29/W29-1.0b-null-saves.json and
build/w29/W29-1.0b-noise-effects.log).

    python -I scripts/oracle_w29_null_saves.py

Refuses to run if any fixture already exists, so a half-finished session cannot
leave a mixed set.
"""

import hashlib
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log, working_copy  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w29"
NULL_DIR = NEUTRAL + "/null"
WORK = os.path.join(ROOT, "build", "w29", "null-work")
SNAPSHOTS = os.path.join(ROOT, "build", "w29", "null-saves")
FIXTURE_DIR = os.path.join(ROOT, "tests", "data")
RESULT = os.path.join(ROOT, "build", "w29", "W29-1.0b-null-saves.json")

# Every control fixture the actuals and outline save-pair tests read.
CONTROLS = [
    "wave28-percent-auto0-control.mpp",
    "wave28-percent-auto40-control.mpp",
    "wave28-percent-auto100-control.mpp",
    "wave28-percent-manual0-control.mpp",
    "wave28-mark-auto-control.mpp",
    "wave28-mark-manual-control.mpp",
    "wave28-update-control.mpp",
    "wave28-outline-indent-control.mpp",
    "wave28-outline-summary-control.mpp",
    "wave28-outline-outdent-control.mpp",
    "wave29-percent-leaf-control.mpp",
    "wave29-percent-work-control.mpp",
    "wave29-percent-milestone-control.mpp",
    "wave29-percent-mid25-control.mpp",
    "wave29-mark-60-after-control.mpp",
    "wave29-mark-60-before-control.mpp",
    "wave29-update-60-after-control.mpp",
    "wave29-update-60-before-control.mpp",
    "wave29-update-60-selected-control.mpp",
    "wave29-outline-auto-indent-control.mpp",
    "wave29-outline-auto-summary-control.mpp",
    "wave29-outline-auto-outdent-control.mpp",
    "wave29-outline-manual-parent-control.mpp",
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
    # A typo in the table would otherwise be discovered half-way through a
    # COM session; check every control first.
    for control in CONTROLS:
        path = os.path.join(FIXTURE_DIR, control)
        if not os.path.exists(path):
            sys.exit(f"refusing: {path} is not a committed fixture")
        fixture = os.path.join(FIXTURE_DIR, null_fixture_name(control))
        if os.path.exists(fixture):
            sys.exit(f"refusing: {fixture} already exists (inspect it first)")
    os.makedirs(NULL_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    os.makedirs(SNAPSHOTS, exist_ok=True)

    facts = {"version": None, "null_saves": {}}
    with oracle_session() as app:
        with Guard(1800):
            facts["version"] = str(app.Version)
            app.UserName = "Fixture Builder"
            for control in CONTROLS:
                source = os.path.join(FIXTURE_DIR, control)
                copy = os.path.join(WORK, control)
                shutil.copyfile(source, copy)
                target = os.path.join(NULL_DIR, null_fixture_name(control))
                if os.path.exists(target):
                    os.remove(target)
                app.FileOpen(copy)
                project = app.ActiveProject
                author = str(project.Author)
                manager = str(project.Manager)
                company = str(project.Company)
                title = str(project.Title)
                before = {
                    "author": author, "manager": manager,
                    "company": company, "title": title,
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
                # The file must be CLOSED before it is copied: while Project
                # holds it open the on-disk length is the pre-allocated one
                # (344064 for a 239104-byte file), and copying then caught the
                # tail the close truncates.
                first = os.path.join(
                    SNAPSHOTS, null_fixture_name(control)[:-4] + "-save1.mpp")
                shutil.copyfile(target, first)
                # The second save: reopen the null save and save it again with
                # no edit, so the two snapshots differ only by per-save churn.
                app.FileOpen(target)
                app.UserName = "Fixture Builder"
                app.FileSave()
                app.FileClose(0)
                second = os.path.join(
                    SNAPSHOTS, null_fixture_name(control)[:-4] + "-save2.mpp")
                shutil.copyfile(target, second)
                entry = {
                    "control": control,
                    "null_fixture": null_fixture_name(control),
                    "null_save_path": target,
                    "metadata_before": before,
                    "metadata_after": after,
                    "save1_sha256": sha256(first),
                    "save2_sha256": sha256(second),
                    "save1_bytes": os.path.getsize(first),
                    "save2_bytes": os.path.getsize(second),
                }
                facts["null_saves"][control] = entry
                log(f"{control}: saved {entry['save1_bytes']} bytes, "
                    f"second save {entry['save2_bytes']} bytes, "
                    f"same={entry['save1_sha256'] == entry['save2_sha256']}")

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")

    # The committed fixture is the FIRST null save.  Copy it in only after the
    # whole session succeeded, so a failure cannot leave a partial set.
    for control in CONTROLS:
        first = os.path.join(
            SNAPSHOTS, null_fixture_name(control)[:-4] + "-save1.mpp")
        shutil.copyfile(first, os.path.join(FIXTURE_DIR, null_fixture_name(control)))
        log(f"fixture {null_fixture_name(control)} "
            f"({os.path.getsize(first)} bytes)")


if __name__ == "__main__":
    main()
