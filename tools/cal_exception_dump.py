#!/usr/bin/env python3
"""Dump the 92-byte dated-exception records of 114/TBkndCal (read-only).

Diagnostic tool for the W53 recurring-exception work.  It prints, for every
calendar data block, the raw 92-byte exception record (hex) plus the fields
core/cal already decodes (start/end day, intervals, occurrences, type) and the
candidate recurrence-parameter words the stream is investigating.

    python tools/cal_exception_dump.py corpus/probe/w12-exceptions.mpp
    python tools/cal_exception_dump.py corpus/probe/base.mpp --diff

Exception NAMES are never printed (client-data rule); only their byte length.
"""

import argparse
import struct
import sys

import olefile

WEEK_BYTES = 420
FIXED_BYTES = 92


def u16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def load(path):
    ole = olefile.OleFileIO(path)
    try:
        out = {}
        for parts in ole.listdir(streams=True):
            out[tuple(p.strip() for p in parts)] = ole.openstream(parts).read()
        return out
    finally:
        ole.close()


def var_blocks(streams):
    """Return [(calendar_uid, block bytes)] for key low-word 8 (calendar data)."""
    var_meta = streams.get(("114", "TBkndCal", "VarMeta"))
    var2 = streams.get(("114", "TBkndCal", "Var2Data"))
    if var_meta is None or var2 is None:
        return []
    n = (len(var_meta) - 24) // 12
    out = []
    for i in range(n):
        mid, offset, key = struct.unpack_from("<3I", var_meta, 24 + i * 12)
        if key & 0xFFFF != 8:
            continue
        length = u32(var2, offset)
        out.append((mid, var2[offset + 4:offset + 4 + length]))
    return out


def fixed_rows(streams):
    """Return [calendar_uid] in FixedData row order."""
    fixed = streams.get(("114", "TBkndCal", "FixedData"))
    if fixed is None:
        return []
    return [u32(fixed, 64 + i * 12 + 8) for i in range((len(fixed) - 64) // 12)]


def exceptions(block):
    if len(block) < WEEK_BYTES:
        return []
    count = u16(block, WEEK_BYTES)
    out = []
    off = WEEK_BYTES + 4
    for _ in range(count):
        if off + FIXED_BYTES > len(block):
            break
        rec = block[off:off + FIXED_BYTES]
        name_len = u32(rec, 88)
        out.append({"offset": off, "raw": rec, "name_len": name_len})
        off += FIXED_BYTES + (name_len + 3) // 4 * 4
    return out


def parameters(rec):
    """The recurrence-parameter words, decoded the way core/cal/cal.cpp does.

    Offsets and per-type meanings are measured on corpus/probe/w12-exceptions.mpp
    and documented in core/cal/cal.h (`Recurrence parameter words`).  This is the
    independent second opinion on the C++ decoder.
    """
    rtype = u16(rec, 72)
    w76 = u16(rec, 76)
    w78 = u16(rec, 78)
    low = w76 & 0xFF
    high = (w76 >> 8) & 0xFF
    if rtype == 2:
        return f"month {low + 1} month_day {high}"
    if rtype == 3:
        return f"month {low + 1} position {high} month_item {w78 & 0xFF}"
    if rtype == 4:
        return f"month_day {low} period {w78 & 0xFF}"
    if rtype == 5:
        return f"position {low} month_item {high} period {w78}"
    if rtype == 6:
        return f"days_of_week 0x{low:02x} period {w78}"
    if rtype == 7:
        return f"period {w76}"
    return "no parameters (plain range)"


def describe(uid, ex):
    rec = ex["raw"]
    occ = u16(rec, 4)
    periods = u16(rec, 14)
    rtype = u16(rec, 72)
    return (f"cal {uid:>4} off {ex['offset']:>4} type {rtype} "
            f"start {u16(rec, 0):>6} end {u16(rec, 2):>6} occ {occ:>3} "
            f"periods {periods} name_bytes {ex['name_len']} "
            f"| {parameters(rec)}")


def words(rec):
    """Every u16/u32 word of the record, for eyeballing one field across files."""
    return {
        "u16": [u16(rec, o) for o in range(0, FIXED_BYTES, 2)],
        "u32": [u32(rec, o) for o in range(0, FIXED_BYTES, 4)],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--hex", action="store_true", help="hexdump each record")
    ap.add_argument("--words", action="store_true",
                    help="print every u16 word index/value")
    ap.add_argument("--diff", default=None,
                    help="another .mpp to compare word-by-word against")
    args = ap.parse_args()

    base = load(args.path)
    rows = fixed_rows(base)
    blocks = dict(var_blocks(base))
    print(f"file {args.path}: {len(rows)} calendar rows, "
          f"{len(blocks)} data blocks")
    have = []
    for uid in rows:
        block = blocks.get(uid)
        if block is None:
            continue
        exs = exceptions(block)
        print(f"calendar uid {uid}: {len(exs)} exceptions")
        for ex in exs:
            print("  " + describe(uid, ex))
            if args.hex:
                rec = ex["raw"]
                for o in range(0, FIXED_BYTES, 16):
                    print(f"    +{o:02d} "
                          + " ".join(f"{b:02x}" for b in rec[o:o + 16]))
            if args.words:
                w = words(ex["raw"])["u16"]
                for i in range(0, len(w), 8):
                    chunk = " ".join(f"{i + j:>2}:{v:>6}"
                                     for j, v in enumerate(w[i:i + 8]))
                    print(f"    {chunk}")
            have.append((uid, ex["raw"]))

    if args.diff:
        other = load(args.diff)
        orows = fixed_rows(other)
        oblocks = dict(var_blocks(other))
        ref = {}
        for uid in orows:
            block = oblocks.get(uid)
            if block is None:
                continue
            for ex in exceptions(block):
                ref[u16(ex["raw"], 72)] = ex["raw"]
        print(f"\ndiff against {args.diff}")
        for uid, rec in have:
            rtype = u16(rec, 72)
            if rtype not in ref:
                print(f"  type {rtype}: no counterpart in the reference file")
                continue
            a = words(rec)["u16"]
            b = words(ref[rtype])["u16"]
            diffs = [f"+{i * 2}:{b[i]}->{a[i]}"
                     for i in range(len(a)) if a[i] != b[i]]
            print(f"  cal {uid} type {rtype}: " + (", ".join(diffs) or "identical"))


if __name__ == "__main__":
    sys.exit(main())
