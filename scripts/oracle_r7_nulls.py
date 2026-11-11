#!/usr/bin/env python3
"""Round 7, item 1: control-nulls for the ten pairs that have none.

A control-null is a Project no-edit save of the pair's CONTROL.  Bytes that differ
between the control and its null are per-save noise; the trace gate uses the null
to mask them (tools/trace/capture/specs6.ps1 names it "<target>-control-null").

The ten pairs, the file each null is a save of, and the oracle that made the pair:

  pair                      control (tests/data)             source oracle
  wave27-percent-actuals    synthetic-base.mpp               oracle_w28_privacy.py
  wave27-priority-750       wave27-priority-control.mpp      oracle_w28_privacy.py
  wave27-unlink             wave27-unlink-control.mpp        oracle_w28_privacy.py
  wave28-newtask-auto       wave28-newtask-manual-control    oracle_w28_new_task_mode.py
  wave28-newtask-manual     wave28-newtask-auto-control      oracle_w28_new_task_mode.py
  wave23-flags              wave23-outline.mpp               oracle_w28_privacy.py
  wave28-split              wave28-split-control.mpp         oracle_w28_split.py
  wave31-tables             synthetic-base.mpp               oracle_w31_tables.py
  wave31-resource-sheet     synthetic-base.mpp               oracle_w31_resource_sheet.py
  wave31-usage              synthetic-base.mpp               oracle_w31_usage.py

The committed wave27-* and wave23-flags files were rebuilt by oracle_w28_privacy.py
(not oracle_w27*.py or oracle_w23.py), so that script is their source.  Four pairs
have no committed "<pair>-control.mpp": their control is the base file the edit
chain opened (synthetic-base.mpp or wave23-outline.mpp), as the trace specs state
(specs6-g5-w27w28.ps1 and specs6-g5-w31.ps1, key Ctl).  The two wave28-newtask
pairs are crossed: the control of wave28-newtask-auto is
wave28-newtask-manual-control.mpp, and the other way round.

The null procedure is the wave-34 one (oracle_w34_null_saves.py, and the W29-1.0b
discipline): copy the committed control through oracle_lock.working_copy() so the
committed file is never handed to FileOpen, open the copy from the neutral root,
set Application.UserName only (no neutralise(): the control is already neutral and
a null must not change Author, Manager, Company or Title), FileSaveAs the null,
close, and only then read its size and hash.  The null is saved ONCE per pair and
never replayed from the chain: the chain's edit is not part of a null.

Output: C:/w35/r7-nulls/<pair>-control-null.mpp.  The coordinator copies each one
to tests/data/ after scanning its strings (UTF-8 and UTF-16).  Nothing is written
to tests/data.  Facts, by file and counts only (no task or project name is read),
go to build/r7/oracle_r7_nulls.json.

Calculation mode: none of the source oracles sets Application.Calculation, so by
default this script does not either ("leave"): the session's own mode applies, as
it did for the sources, and the value Project reports is recorded per pair.
--calc auto or --calc manual sets it before the opens (0 manual, 1 automatic).

    python scripts/oracle_r7_nulls.py --list
    python scripts/oracle_r7_nulls.py [--calc leave|auto|manual] [pair ...]

With no pair every pair runs.  The script refuses to run if a target exists.
"""

import argparse
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
from oracle_privacy import AUTHOR, COMPANY  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w35/r7-nulls"
WORK = NEUTRAL + "/work"
OUT = os.path.join(ROOT, "build", "r7")
RESULT = os.path.join(OUT, "oracle_r7_nulls.json")
DATA = os.path.join(ROOT, "tests", "data")

# pair -> (control file in tests/data, source oracle)
NULLS = {
    "wave27-percent-actuals": ("synthetic-base.mpp", "oracle_w28_privacy.py"),
    "wave27-priority-750": ("wave27-priority-control.mpp", "oracle_w28_privacy.py"),
    "wave27-unlink": ("wave27-unlink-control.mpp", "oracle_w28_privacy.py"),
    "wave28-newtask-auto": ("wave28-newtask-manual-control.mpp",
                            "oracle_w28_new_task_mode.py"),
    "wave28-newtask-manual": ("wave28-newtask-auto-control.mpp",
                              "oracle_w28_new_task_mode.py"),
    "wave23-flags": ("wave23-outline.mpp", "oracle_w28_privacy.py"),
    "wave28-split": ("wave28-split-control.mpp", "oracle_w28_split.py"),
    "wave31-tables": ("synthetic-base.mpp", "oracle_w31_tables.py"),
    "wave31-resource-sheet": ("synthetic-base.mpp", "oracle_w31_resource_sheet.py"),
    "wave31-usage": ("synthetic-base.mpp", "oracle_w31_usage.py"),
}
PAIRS = tuple(NULLS)


def null_name(pair):
    return f"{pair}-control-null.mpp"


def target_path(pair):
    return os.path.join(NEUTRAL, null_name(pair))


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


def counts(project):
    """Counts and neutrality flags only: no name is read."""
    return {
        "tasks": prop(project.Tasks, "Count"),
        "resources": prop(project.Resources, "Count"),
        "author_neutral": prop(project, "Author") == AUTHOR,
        "manager_neutral": prop(project, "Manager") == AUTHOR,
        "company_neutral": prop(project, "Company") == COMPANY,
    }


def make_null(app, pair, entry):
    control, source = NULLS[pair]
    entry["control"] = control
    entry["source_oracle"] = source
    committed = os.path.join(DATA, control)
    # working_copy() copies under build/oracle-work; the open is from the neutral
    # root, so the path Project may embed is neutral.
    staged = working_copy(committed, "r7-null-" + pair)
    copy = os.path.join(WORK, f"{pair}-{control}")
    shutil.copyfile(staged, copy)
    target = target_path(pair)

    app.FileOpen(copy)
    project = app.ActiveProject
    entry["before"] = counts(project)
    app.FileSaveAs(target)
    entry["after"] = counts(project)
    app.FileClose(0)
    # Read size and hash only after the close: Project pre-allocates the open
    # file and truncates on close (W29-1.0b).
    entry["null_file"] = null_name(pair)
    entry["bytes"] = os.path.getsize(target)
    entry["sha256"] = sha256(target)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--list", action="store_true",
                    help="print the pairs and do nothing else")
    ap.add_argument("--calc", choices=("leave", "auto", "manual"), default="leave")
    ap.add_argument("pairs", nargs="*")
    args = ap.parse_args()

    if args.list:
        print(f"neutral root {NEUTRAL}; calculation: {args.calc} "
              "(default leave: the sources set none)")
        for pair, (control, source) in NULLS.items():
            print(f"{pair}: null of tests/data/{control} -> {null_name(pair)}"
                  f" (source {source})")
        return

    pairs = tuple(args.pairs) or PAIRS
    for pair in pairs:
        if pair not in NULLS:
            sys.exit(f"unknown pair {pair}")
        if not os.path.isfile(os.path.join(DATA, NULLS[pair][0])):
            sys.exit(f"control missing: tests/data/{NULLS[pair][0]}")
        if os.path.exists(target_path(pair)):
            sys.exit(f"refusing: {target_path(pair)} already exists "
                     "(inspect the pair first)")
    for directory in (NEUTRAL, WORK, OUT):
        os.makedirs(directory, exist_ok=True)

    result = {"version": None, "calc_arg": args.calc, "pairs": {}}
    with oracle_session() as app:
        with Guard(1800):
            result["version"] = prop(app, "Version")
            app.UserName = AUTHOR
            if args.calc != "leave":
                app.Calculation = 1 if args.calc == "auto" else 0
            result["calculation"] = prop(app, "Calculation")
            for pair in pairs:
                entry = result["pairs"].setdefault(pair, {})
                ok = make_null(app, pair, entry)
                log(f"{pair}: {'ok' if ok else 'FAILED'} {entry.get('bytes')} bytes")

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {os.path.relpath(RESULT, ROOT)}")


if __name__ == "__main__":
    main()
