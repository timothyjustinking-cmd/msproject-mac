#!/usr/bin/env python3
"""Dump the 202-byte task rows of a .mpp file, straight from FixedData.

Independent of the C++ reader: this is the coordinator's byte-level oracle check.
Offsets are spec/mpp-records.md §2 (row byte = 48 + 202*row + off).

    python scripts/mpp_rows.py corpus/probe/w3-o1.mpp

Prints, per row: row id, UniqueID, Priority, Duration (ticks and minutes),
PercentComplete, Start/Finish packed words decoded.
"""

import argparse
import os
import struct
import sys
from datetime import datetime, timedelta

import olefile

STRIDE = 202
HEADER = 48
EPOCH = datetime(1983, 12, 31)


def find_stream(ole, suffix):
    for path in ole.listdir(streams=True, storages=False):
        joined = "/".join(p.strip() for p in path)
        if joined.endswith(suffix):
            return ole.openstream(path).read()
    return None


def decode_date32(v):
    if v == 0 or v == 0xFFFFFFFF:
        return None
    day, tick = v >> 16, v & 0xFFFF
    try:
        return (EPOCH + timedelta(days=day)).replace() + timedelta(seconds=tick * 6)
    except OverflowError:
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    args = ap.parse_args()

    if not olefile.isOleFile(args.path):
        sys.exit(f"not an OLE2 file: {args.path}")
    ole = olefile.OleFileIO(args.path)
    try:
        fixed = find_stream(ole, "TBkndTask/FixedData")
        meta = find_stream(ole, "TBkndTask/FixedMeta")
    finally:
        ole.close()
    if fixed is None:
        sys.exit("no TBkndTask/FixedData stream")

    n = (len(fixed) - HEADER) // STRIDE
    print(f"{os.path.basename(args.path)}: FixedData {len(fixed)} B = "
          f"{HEADER} + {n} x {STRIDE}")
    if meta:
        print(f"  FixedMeta {len(meta)} B, header count word = "
              f"{struct.unpack_from('<I', meta, 8)[0]}, "
              f"size word = {struct.unpack_from('<I', meta, 12)[0]}")
    print(f"\n{'row':>3} {'id':>4} {'uid':>6} {'prio':>5} {'durTicks':>9} "
          f"{'durMin':>7} {'pct':>4}  start                 finish")
    for r in range(n):
        base = HEADER + r * STRIDE
        row = fixed[base:base + STRIDE]
        row_id, uid = struct.unpack_from("<II", row, 0)
        prio = struct.unpack_from("<H", row, 78)[0]
        dur = struct.unpack_from("<I", row, 84)[0]
        pct = struct.unpack_from("<H", row, 92)[0]
        start = struct.unpack_from("<I", row, 96)[0]
        finish = struct.unpack_from("<I", row, 100)[0]
        print(f"{r:>3} {row_id:>4} {uid:>6} {prio:>5} {dur:>9} {dur/10:>7.0f} "
              f"{pct:>4}  {str(decode_date32(start)):20}  {decode_date32(finish)}"
              f"   [{start:08x} {finish:08x}]")


if __name__ == "__main__":
    main()
