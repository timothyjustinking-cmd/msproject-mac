#!/usr/bin/env python3
"""Print the stored date pairs of FixedMeta-covered .mpp task rows.

WHY THIS EXISTS.  `mspdi_cmp` compares the reader's Start/Finish with Project's
MSPDI export.  The reader returns the *calculated* pair at row +96/+100; Project
also stores an *effective* pair at +104/+108.  When the two pairs differ, a
Start/Finish "mismatch" is explained by which pair each side reads -- it is not
an MSPDI import bug.  This script prints the rows where the pairs differ, so a
mismatch can be attributed from bytes instead of guessed at.

Read-only, and it prints uids, durations and dates only -- never a task name
(the corpus is client data, docs/coordination.md).

Uses scripts/probe_lib.py (the project's read-only OLE/record probe helpers).

usage:
    python tools/mspdi_cmp/date_pairs.py <file.mpp>
    python tools/mspdi_cmp/date_pairs.py <file.mpp> --all

exit: 0 on success, 2 on bad usage.
"""

from __future__ import annotations

import datetime
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import probe_lib  # noqa: E402  (path set above)


def decode_date(raw: int) -> str:
    if raw in (0, 0xFFFFFFFF):
        return "(none)"
    day, _seconds, clock = probe_lib.decode_date(raw)
    return (datetime.date(1983, 12, 31) + datetime.timedelta(days=day)).isoformat() + " " + clock


def covered_rows(rows, entries) -> set[int]:
    """Task FixedMeta entries cover a row; the first three describe the header."""
    covered: set[int] = set()
    for entry in entries[3:]:
        offset = entry["offset"]
        if (
            probe_lib.FIXED_HEADER <= offset
            < probe_lib.FIXED_HEADER + probe_lib.FIXED_ROW * len(rows)
            and (offset - probe_lib.FIXED_HEADER) % probe_lib.FIXED_ROW == 0
        ):
            covered.add((offset - probe_lib.FIXED_HEADER) // probe_lib.FIXED_ROW)
    return covered


def main(argv: list[str]) -> int:
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        print(__doc__.strip())
        return 2 if len(argv) < 2 else 0
    path = argv[1]
    show_all = "--all" in argv[2:]

    streams = probe_lib.load(path)
    fixed = probe_lib.stream(streams, "TBkndTask", "FixedData")
    meta = probe_lib.stream(streams, "TBkndTask", "FixedMeta")
    _, rows = probe_lib.rows_of(fixed)
    _, entries = probe_lib.parse_fixed_meta(meta)
    covered = covered_rows(rows, entries)

    print(f"{path}: {len(rows)} FixedData rows, {len(covered)} FixedMeta-covered rows")
    print(
        "row  uid       dur_ticks  +96 start              +100 finish             "
        "+104 effective start  +108 effective finish  same"
    )
    covered_count = 0
    differ = 0
    for index, row in enumerate(rows):
        if index not in covered:
            continue
        uid = probe_lib.u32(row, 4)
        if uid == 0:  # project summary row
            continue
        covered_count += 1
        stored = (probe_lib.u32(row, 96), probe_lib.u32(row, 100))
        effective = (probe_lib.u32(row, 104), probe_lib.u32(row, 108))
        if stored != effective:
            differ += 1
        if stored == effective and not show_all:
            continue
        print(
            f"{index:3d}  {uid:8d}  {probe_lib.u32(row, 84):9d}  "
            f"{decode_date(stored[0]):20s}  {decode_date(stored[1]):20s}  "
            f"{decode_date(effective[0]):20s}  {decode_date(effective[1]):20s}  "
            f"{'yes' if stored == effective else 'NO'}"
        )
    print(
        f"summary: {covered_count} FixedMeta-covered task rows (deleted slots included), "
        f"{differ} with +96/+100 != +104/+108; only those can produce a "
        "reader-vs-export Start/Finish difference when the export writes the "
        "effective pair"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
