"""Worked example: read Task.Name for the first three tasks of the PROJECT_A file.

W1 brief item (f). Run:

    python scripts/probe_names.py corpus/project_a.mpp

It locates each task's row in FixedData, reads the row's UniqueID at +4, finds
the VarMeta entries whose `mid` equals that UniqueID, picks the entry keyed by
field id 0x0b400564 (Task.Name, established by var-name.mpp), and prints the
Var2Data block offset, its u32 byte-length prefix, and the raw UTF-16LE bytes.

Expected output (build 16.0.19725, file saved 2026-07-22):

    row 1  uid 8141  field 0x0b400564  Var2Data[1456]
           prefix 28 00 00 00  -> 40 bytes, "client-string-10dbe851 "
    row 2  uid 8142  field 0x0b400564  Var2Data[1660]
           prefix 46 00 00 00  -> 70 bytes, "client-string-25d8f08b"
    row 3  uid 8166  field 0x0b400564  Var2Data[1866]
           prefix 1a 00 00 00  -> 26 bytes, "client-string-9c4bcb75"
"""

import sys

from probe_lib import (FIXED_HEADER, FIXED_ROW, decode_text, load, parse_var_meta,
                       read_block, rows_of, stream, u32, var_groups)

NAME_FIELD = 0x0B400564


def probe(path, count=3):
    streams = load(path)
    fixed = stream(streams, "TBkndTask", "FixedData")
    var_meta = stream(streams, "TBkndTask", "VarMeta")
    var2 = stream(streams, "TBkndTask", "Var2Data")
    header, entries, split = parse_var_meta(var_meta)
    _, groups = var_groups(entries)
    _, rows = rows_of(fixed)

    print(f"file: {path}")
    print(f"FixedData {len(fixed):,} B, {len(rows)} rows; "
          f"VarMeta {len(var_meta):,} B, {len(entries)} entries; "
          f"Var2Data {len(var2):,} B\n")
    for index in range(1, min(count + 1, len(rows))):
        row = rows[index]
        uid = u32(row, 4)
        names = [e for e in groups.get(uid, []) if e["field_id"] == NAME_FIELD]
        print(f"row {index}: FixedData[{FIXED_HEADER + index * FIXED_ROW}:"
              f"{FIXED_HEADER + index * FIXED_ROW + 8}] ="
              f" id {u32(row, 0)} uid {uid}")
        if not names:
            print(f"  no 0x{NAME_FIELD:08x} entry for mid={uid}")
            continue
        e = names[0]
        payload = read_block(var2, e["offset"])
        raw = var2[e["offset"]:e["offset"] + 4 + len(payload)]
        print(f"  VarMeta entry {e['index']}: field_id 0x{e['field_id']:08x}"
              f" mid {e['mid']} offset {e['offset']}")
        print(f"  Var2Data[{e['offset']}:{e['offset'] + 4 + len(payload)}] ="
              f" {raw.hex(' ')}")
        print(f"  length prefix {raw[:4].hex(' ')} = {len(payload)} bytes;"
              f" name = {decode_text(payload)!r}")


def main(argv):
    if not argv:
        sys.exit(__doc__)
    probe(argv[0], int(argv[1]) if len(argv) > 1 else 3)


if __name__ == "__main__":
    main(sys.argv[1:])
