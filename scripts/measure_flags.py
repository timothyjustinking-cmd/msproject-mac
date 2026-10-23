#!/usr/bin/env python3
"""Measure the MPP14 Task Flag1..Flag20 FixedMeta bits, one at a time.

Coordinator-only (needs MS Project through scripts/oracle_lock.py).  Opens a COPY
of tests/data/wave23-outline.mpp, saves a control with every flag False on ID 3,
then one save per flag with ONLY that flag True, and reports the single
FixedMeta byte each flag moves.  The same run writes the aggregate all-twenty
fixture and leaves every single-bit save under C:/w25, so a committed control is
a copy of the measured save rather than a hand edit.

Wave 24 wrote the first version under build/w24 (not committed, so the fixture
provenance could not be reproduced); wave 25 W25-9 commits it and adds the
Flag11..Flag17 single-bit controls for the +33 group.

    python scripts/measure_flags.py

Expected map (measured 2026-10-23/24):

    Flag1..7   byte +24  bits 0x02 0x04 0x08 0x10 0x20 0x40 0x80
    Flag8..10  byte +25  bits 0x01 0x02 0x04
    Flag11..17 byte +33  bits 0x02 0x04 0x08 0x10 0x20 0x40 0x80
    Flag18..20 byte +34  bits 0x01 0x02 0x04
"""

import json
import os
import shutil
import sys

import olefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from oracle_lock import oracle_session  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w25"
OUT = os.path.join(ROOT, "build", "w25")
os.makedirs(NEUTRAL, exist_ok=True)
os.makedirs(OUT, exist_ok=True)


def fixed_meta(path):
    ole = olefile.OleFileIO(path)
    entry = [e for e in ole.listdir()
             if e[-1] == "FixedMeta" and "TBkndTask" in "/".join(e)][0]
    data = ole.openstream(entry).read()
    ole.close()
    return data


def diffs(a, b):
    if len(a) != len(b):
        return None
    return [(i, a[i], b[i]) for i in range(len(a)) if a[i] != b[i]]


def main():
    base = os.path.join(NEUTRAL, "flags-base.mpp")
    shutil.copyfile(os.path.join(ROOT, "tests", "data", "wave23-outline.mpp"),
                    base)
    control = os.path.join(NEUTRAL, "flags-control.mpp")
    all_on = os.path.join(NEUTRAL, "flags20.mpp")
    facts = {"level_base": None, "flags": {}}
    with oracle_session() as app:
        with Guard(420):
            app.UserName = "Fixture Builder"
            app.FileOpen(base)
            project = app.ActiveProject
            project.Author = project.Manager = "Fixture Builder"
            project.Company = "Fixture Company"
            project.Title = "Synthetic Flags Fixture"
            task = {t.ID: t for t in project.Tasks if t is not None}[3]
            for index in range(1, 21):
                setattr(task, f"Flag{index}", False)
            app.FileSaveAs(control)
            for index in range(1, 21):
                setattr(task, f"Flag{index}", True)
                target = os.path.join(NEUTRAL, f"flag-{index:02d}.mpp")
                app.FileSaveAs(target)
                setattr(task, f"Flag{index}", False)
            for index in range(1, 21):
                setattr(task, f"Flag{index}", True)
            app.FileSaveAs(all_on)
            app.FileClose(0)
    control_data = fixed_meta(control)
    for index in range(1, 21):
        path = os.path.join(NEUTRAL, f"flag-{index:02d}.mpp")
        facts["flags"][index] = diffs(control_data, fixed_meta(path))
    facts["entry_base_hint_flag10"] = facts["flags"][10][0][0] - 25
    facts["entry_base_hint_flag1"] = facts["flags"][1][0][0] - 24
    with open(os.path.join(OUT, "flags-map.json"), "w", encoding="utf-8",
              newline="\n") as fh:
        json.dump(facts, fh, indent=2)
        fh.write("\n")
    print(json.dumps(facts, indent=2))


if __name__ == "__main__":
    main()
