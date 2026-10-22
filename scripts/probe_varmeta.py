"""Probe VarMeta / Var2Data: entry keying, block framing, and the split entry.

Answers W1 brief items (b) and (e):

    python scripts/probe_varmeta.py corpus/probe/base.mpp corpus/project_a.mpp

For each file it prints the 32-byte header, the size identity, the entry count,
the 4-byte trailing field id, and the `mid` groups. It then checks the central
claim against FixedData:

    mid == the row's UniqueID == u32 at FixedData[48 + 202*row + 4]

and decodes a tree of Var2Data blocks (u32 byte length + payload) for the
project row and the first task row.
"""

import sys

from probe_lib import (FIXED_HEADER, FIXED_ROW, VAR_ENTRY, VAR_HEADER, load,
                       parse_var_meta, read_block, rows_of, stream, u32,
                       var_groups)


def probe(path):
    streams = load(path)
    var_meta = stream(streams, "TBkndTask", "VarMeta")
    var2 = stream(streams, "TBkndTask", "Var2Data")
    fixed = stream(streams, "TBkndTask", "FixedData")
    header, entries, split = parse_var_meta(var_meta)
    _, rows = rows_of(fixed)

    print(f"\n=== {path}")
    print(f"VarMeta {len(var_meta):,} B  entries {len(entries)}"
          f"  header count+1 {header['entry_count_plus_one']}"
          f"  header Var2Data size {header['var2_size']:,}"
          f"  actual {len(var2):,}")
    print(f"size identity 32 + 12*{len(entries)} + 4: "
          f"{VAR_HEADER + VAR_ENTRY * len(entries) + 4} (actual {len(var_meta)})")
    print(f"split entry: field_id 0x{split['field_id']:08x}"
          f" mid {split['mid']} offset {split['offset']}"
          f" -> block {read_block(var2, split['offset'])[:32]!r}")

    order, groups = var_groups(entries)
    print(f"mid groups ({len(order)}): "
          f"{[(m, len(groups[m])) for m in order[:8]]}"
          f"{' ...' if len(order) > 8 else ''}")
    high = [m for m in order if m & 0x80000000]
    print(f"high-bit mids (0x80000000|uid): {len(high)}"
          f" first {[hex(m) for m in high[:5]]}")

    uid_by_row = [u32(row, 4) for row in rows]
    missing = [r for r, uid in enumerate(uid_by_row) if uid not in groups]
    extra = [m for m in order if m not in uid_by_row and
             (m & 0x7FFFFFFF) not in uid_by_row]
    print(f"rows {len(rows)}; rows with no mid group: {missing}")
    print(f"mid groups that are neither a UID nor UID|0x80000000: {extra}")
    mismatched = [r for r, uid in enumerate(uid_by_row)
                  if uid in groups and rows[r][:4] != rows[r][:4]]
    print(f"UIDs at FixedData row+4: {uid_by_row[:6]}"
          f"{' ...' if len(uid_by_row) > 6 else ''}")

    for label, row_index in (("project row", 0), ("task row 1", 1)):
        if row_index >= len(rows):
            continue
        uid = uid_by_row[row_index]
        print(f"{label}: row {row_index} uid {uid}"
              f" ({len(groups.get(uid, []))} var entries; first 10)")
        for e in groups.get(uid, [])[:10]:
            payload = read_block(var2, e["offset"])
            print(f"    fid 0x{e['field_id']:08x} off {e['offset']:5d}"
                  f" len {len(payload):3d} {payload[:40]!r}")


def main(argv):
    if not argv:
        sys.exit(__doc__)
    for path in argv:
        probe(path)


if __name__ == "__main__":
    main(sys.argv[1:])
