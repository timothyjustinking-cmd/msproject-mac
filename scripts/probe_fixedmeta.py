"""Probe the FixedMeta / Fixed2Meta layout of a table.

Question the probe answers (W1 brief, item (a)): is FixedMeta a slot table
carrying field ids, or purely positional bookkeeping?

    python scripts/probe_fixedmeta.py corpus/probe/base.mpp corpus/project_a.mpp

For every file it prints:
  * the 16-byte header and the derived entry count,
  * the size identity  FixedMeta == 16 + 47 * (rows + 3),
  * the first three entries (FixedData header descriptors, offsets 0/16/32),
  * the row entries with their FixedData offsets and small counters,
  * a byte scan of FixedMeta for any 0x0b40 field-id pattern (expected: none).

Evidence is collected in spec/mpp-records.md.
"""

import sys

from probe_lib import (FIXED_HEADER, FIXED_ROW, META_ENTRY, META_HEADER,
                       family_entries, load, parse_fixed_meta, rows_of, stream,
                       u32)


def probe(path):
    streams = load(path)
    fixed = stream(streams, "TBkndTask", "FixedData")
    meta = stream(streams, "TBkndTask", "FixedMeta")
    header, rows = rows_of(fixed)
    mh, entries = parse_fixed_meta(meta)
    head, body = family_entries(entries)

    print(f"\n=== {path}")
    print(f"FixedData {len(fixed):,} B -> {len(rows)} rows of {FIXED_ROW} B"
          f" + {FIXED_HEADER} B header")
    print(f"FixedMeta {len(meta):,} B = {META_HEADER} + {META_ENTRY} * {len(entries)}"
          f"   header says {mh['entry_count']} entries"
          f"   header FixedData size {mh['fixed_data_size']}")
    print(f"size identity 16 + 47*(rows+3): {16 + 47 * (len(rows) + 3)}"
          f"  (actual {len(meta)})")
    print("FixedData 48-byte header:", header.hex(" "))

    print("first 3 entries (header descriptors):")
    for e in head:
        print(f"  [{e['index']}] off={e['offset']:5d} size_field={e['count']:3d}"
              f" blob={e['blob'].hex(' ')}")
    print("row entries (first 5, last 2):")
    show = body[:5] + body[-2:] if len(body) > 7 else body
    for e in show:
        print(f"  [{e['index']:2d}] row={e['row']!s:>4} off={e['offset']:6d}"
              f" count={e['count']:3d} blob={e['blob'].hex(' ')}")
    offs = [e["offset"] for e in body]
    dupes = sorted({o for o in offs if offs.count(o) > 1})
    print(f"row entries: {len(body)}; duplicate offsets: {dupes};"
          f" non-consecutive rows: "
          f"{[(e['row'], e['offset']) for e in body if e['row'] is not None and e['offset'] != 48 + 202 * e['row']]}")

    hits = meta.count(bytes([0x40, 0x0B]))
    print(f"0x0b40 field-id byte pairs inside FixedMeta: {hits} (expected 0)")
    return {"path": path, "rows": len(rows), "entries": len(entries),
            "header": mh, "entry_count": mh["entry_count"],
            "size_ok": len(meta) == 16 + 47 * (len(rows) + 3),
            "field_id_hits": hits}


def main(argv):
    if not argv:
        sys.exit(__doc__)
    for path in argv:
        probe(path)


if __name__ == "__main__":
    main(sys.argv[1:])
