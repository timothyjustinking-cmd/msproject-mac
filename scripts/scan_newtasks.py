#!/usr/bin/env python3
"""Scan every .mpp on disk for the QNEWTASKSAREMANUAL project property.

Wave 23 (W23-13 / audit A-6): this is the E7 scan, committed so the evidence is
reproducible from a clean clone.  Record id 0x024013c8 comes from the derived
name table in analysis/project_property_names.tsv (index 64 of the property
table; see scripts/gen_project_property_names.py).

    python scripts/scan_newtasks.py             # one line per file (local)
    python scripts/scan_newtasks.py --summary   # COUNTS ONLY, no file names

`--summary` is what the committed docs/analysis/newtasks-scan-summary.md
records: file names under corpus/ are client data and never leave the machine.
Requires olefile (a local tool dependency, not a test dependency).
"""

import argparse
import glob
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RECORD = 0x024013C8


def norm(joined):
    return "/".join(part.strip() for part in joined.split("/"))


def find_record(data):
    off = 16
    while off + 12 <= len(data):
        length, rid, kind = struct.unpack_from("<III", data, off)
        if not ((rid >> 16) & 0xFF == 0x40):
            break
        if off + 12 + length > len(data):
            break
        if rid == RECORD:
            return kind, length, data[off + 12:off + 12 + length]
        off += 12 + length
    return None


def scan():
    import olefile

    files = sorted(glob.glob(os.path.join(ROOT, "corpus", "**", "*.mpp"),
                             recursive=True) +
                   glob.glob(os.path.join(ROOT, "tests", "data", "*.mpp")))
    rows = []
    for path in files:
        rel = os.path.relpath(path, ROOT)
        try:
            ole = olefile.OleFileIO(path)
        except Exception as exc:  # noqa: BLE001
            rows.append((rel, "ERROR", str(exc)))
            continue
        streams = {norm("/".join(e)): e for e in
                   ole.listdir(streams=True, storages=False)}
        version = "MPP14" if "Props14" in streams else (
            "MPP12" if "Props12" in streams else "?")
        props_name = "114/Props" if "114/Props" in streams else (
            "112/Props" if "112/Props" in streams else None)
        if props_name is None:
            rows.append((rel, version, "no props stream"))
            continue
        data = ole.openstream(streams[props_name]).read()
        rec = find_record(data)
        if rec is None:
            rows.append((rel, version, "ABSENT"))
            continue
        kind, length, payload = rec
        value = struct.unpack_from("<H", payload)[0] if len(payload) >= 2 else None
        rows.append((rel, version, f"kind={kind} len={length} value={value!r}"))
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--summary", action="store_true",
                        help="counts only; no file names (safe to commit)")
    args = parser.parse_args()
    rows = scan()
    if not args.summary:
        for rel, version, detail in rows:
            print(f"{rel}\t{version}\t{detail}")
        return 0
    counts = {"manual": 0, "auto": 0, "absent": 0, "no_props": 0, "error": 0,
              "bad_kind": 0}
    for _, version, detail in rows:
        if detail.startswith("ERROR"):
            counts["error"] += 1
        elif detail == "no props stream":
            counts["no_props"] += 1
        elif detail == "ABSENT":
            counts["absent"] += 1
        elif "value=1" in detail:
            counts["manual"] += 1
        elif "value=0" in detail:
            counts["auto"] += 1
        else:
            counts["bad_kind"] += 1
    print(f"files scanned: {len(rows)}")
    for key in ("manual", "auto", "absent", "no_props", "bad_kind", "error"):
        print(f"{key}: {counts[key]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
