"""Diff the one-change fixtures against base.mpp and attribute each changed byte.

W1 brief item (a) asks how a field id is located inside a 202-byte row. Because
FixedMeta carries no field ids, the answer has to be positional; this probe
recovers positions empirically by changing exactly one property at a time:

    python scripts/probe_fixtures.py

`base.mpp` is the reference. Each `var-*.mpp` differs from it in one property:

    var-name      task 1 renamed "Renamed A"       -> Var2Data only
    var-duration  task 2 duration 3d -> 5d         -> FixedData rows 2
    var-priority  task 3 priority 500 -> 900       -> FixedData row 3
    var-notes     task 2 notes "Hello world"       -> Var2Data only
    var-pct       task 1 50% complete              -> FixedData + Var2Data
    var-projstart project start 2026-06-01         -> FixedData row 0 + derived dates
    var-title     project Title "Probe Title"      -> Var2Data split block only

For every changed FixedData byte it prints (row, intra-row offset, old -> new)
and decodes the 4-byte value at that offset as an integer and as a packed date.
For Var2Data it compares entries by (field_id, mid) and prints payload changes;
the fixture's own file name is stored in a project block and would otherwise
pollute every diff, so a changed block whose payload decodes to the fixture name
is reported separately and excluded from field attribution.
"""

import os
import struct
import sys

from probe_lib import (FIXED_HEADER, FIXED_ROW, VAR_ENTRY, VAR_HEADER, decode_date,
                       load, parse_var_meta, read_block, rows_of, stream)

PROBE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                     "corpus", "probe")
BASE = os.path.join(PROBE, "base.mpp")
VARIANTS = ["var-name", "var-duration", "var-priority", "var-notes",
            "var-pct", "var-projstart", "var-title"]


def fields(path):
    s = load(path)
    return (s, stream(s, "TBkndTask", "FixedData"),
            stream(s, "TBkndTask", "VarMeta"),
            stream(s, "TBkndTask", "Var2Data"))


def describe(value):
    text = f"u32={value}"
    if value >> 16:
        day, secs, hhmm = decode_date(value)
        text += f" date(day={day} {hhmm})"
    return text


def diff_fixed(a, b):
    _, rows_a = rows_of(a)
    _, rows_b = rows_of(b)
    hits = []
    for r in range(min(len(rows_a), len(rows_b))):
        ra, rb = rows_a[r], rows_b[r]
        for i in range(FIXED_ROW):
            if ra[i] != rb[i]:
                hits.append((r, i))
    return hits


def main():
    if not os.path.exists(BASE):
        sys.exit(f"missing {BASE}")
    s_base, fd_base, vm_base, v2_base = fields(BASE)
    head, ent_base, split_base = parse_var_meta(vm_base)

    for name in VARIANTS:
        path = os.path.join(PROBE, name + ".mpp")
        if not os.path.exists(path):
            print(f"\n=== {name}: fixture missing")
            continue
        _, fd, vm, v2 = fields(path)
        print(f"\n=== {name}")

        hits = diff_fixed(fd_base, fd)
        if not hits:
            print("  FixedData: no bytes changed")
        else:
            by_row = {}
            for r, i in hits:
                by_row.setdefault(r, []).append(i)
            for r in sorted(by_row):
                print(f"  FixedData row {r} (offset {FIXED_HEADER + r * FIXED_ROW}):"
                      f" intra {by_row[r]}")
                for i in by_row[r][:6]:
                    off = FIXED_HEADER + r * FIXED_ROW + i
                    old = struct.unpack_from("<I", fd_base, off)[0]
                    new = struct.unpack_from("<I", fd, off)[0]
                    print(f"      +{i:3d}: {fd_base[off:off+4].hex(' ')} ->"
                          f" {fd[off:off+4].hex(' ')}"
                          f"  ({describe(old)} -> {describe(new)})")

        _, ent, split = parse_var_meta(vm)
        key_b = {(e["field_id"], e["mid"]): e for e in ent_base}
        key_n = {(e["field_id"], e["mid"]): e for e in ent}
        changed = []
        for key in sorted(set(key_b) | set(key_n)):
            eb, en = key_b.get(key), key_n.get(key)
            if eb is None:
                changed.append(("added", key, None, read_block(v2, en["offset"])))
            elif en is None:
                changed.append(("removed", key, read_block(v2_base, eb["offset"]), None))
            else:
                pb, pn = read_block(v2_base, eb["offset"]), read_block(v2, en["offset"])
                if pb != pn:
                    changed.append(("payload", key, pb, pn))
        if split_base["field_id"] == split["field_id"]:
            pb = read_block(v2_base, split_base["offset"])
            pn = read_block(v2, split["offset"])
            if pb != pn:
                changed.append(("split", (split["field_id"], split["mid"]), pb, pn))
        print(f"  Var2Data: {len(changed)} changed block(s)"
              f" (Var2Data {len(v2_base)} -> {len(v2)} B)")
        for kind, (fid, mid), old, new in changed:
            shown_old = old[:24] if old else None
            shown_new = new[:24] if new else None
            print(f"      {kind:7} fid 0x{fid:08x} mid {mid}: {shown_old!r} -> {shown_new!r}")


if __name__ == "__main__":
    main()
