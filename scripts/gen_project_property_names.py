#!/usr/bin/env python3
"""Derive analysis/project_property_names.tsv from the local decompiled source.

Wave 23 (W23-13 / audit A-6, Q9): the property-name table lives in the
git-ignored `decompiled/part_0000.c` (FUN_1401b8a70), which cannot be committed
(it is Microsoft's binary, decompiled).  What CAN be committed is the derived
index -> name table, exactly like the other analysis/*.tsv files.

The function assigns `local_3e8 = L"NAME"; local_3e0 = L"NAME"; ...` in
descending slot order.  Element i sits at slot 0x3e8 - 8*i (QNEWTASKSAREMANUAL
is local_1e8 -> i = 64, which is the published record id 0x024013c8).

    python scripts/gen_project_property_names.py            # rewrite the TSV
    python scripts/gen_project_property_names.py --check    # verify it matches
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "decompiled", "part_0000.c")
TSV = os.path.join(ROOT, "analysis", "project_property_names.tsv")
FUNCTION = "FUN_1401b8a70"

ASSIGN = re.compile(r'local_([0-9a-f]+)\s*=\s*L"([^"]*)"')


def entries():
    text = open(SOURCE, encoding="utf-8", errors="replace").read()
    start = text.find("// ==== " + FUNCTION + " @")
    if start < 0:
        raise SystemExit(f"{FUNCTION} not found in {SOURCE}")
    end = text.find("// ====", start + 10)
    body = text[start:end if end > 0 else len(text)]
    rows = []
    for match in ASSIGN.finditer(body):
        slot = int(match.group(1), 16)
        name = match.group(2)
        index = (0x3E8 - slot) // 8
        rows.append((index, name))
    rows.sort()
    return rows


def render(rows):
    lines = [
        "# Project's project-property name table, derived from the local",
        "# decompiled winproj binary (FUN_1401b8a70); see",
        "# scripts/gen_project_property_names.py.  The decompiled source is",
        "# git-ignored and local-only; this index -> name TSV is the committed",
        "# evidence.  Record id = 0x02401388 + index (index 64 =",
        "# QNEWTASKSAREMANUAL = 0x024013c8).",
        "index\tname",
    ]
    lines += [f"{index}\t{name}" for index, name in rows]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    text = render(entries())
    if args.check:
        if not os.path.exists(TSV):
            print(f"MISSING {os.path.relpath(TSV, ROOT)}")
            return 1
        current = open(TSV, encoding="utf-8").read()
        if current != text:
            print(f"STALE {os.path.relpath(TSV, ROOT)}: regenerate it")
            return 1
        print(f"OK: {os.path.relpath(TSV, ROOT)} matches {FUNCTION}")
        return 0
    os.makedirs(os.path.dirname(TSV), exist_ok=True)
    open(TSV, "w", encoding="utf-8", newline="\n").write(text)
    rows = entries()
    print(f"wrote {os.path.relpath(TSV, ROOT)} ({len(rows)} names, "
          f"index {rows[0][0]}..{rows[-1][0]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
