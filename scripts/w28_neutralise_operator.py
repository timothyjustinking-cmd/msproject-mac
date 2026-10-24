#!/usr/bin/env python3
"""Neutralise the operator's name still stored in synthetic-calendars.mpp.

W28-0.1's path pass removed the save path, but the file's SummaryInformation
properties (Author and Last Saved By) and one Var2Data value still carried the
operator's name, and the pre-push guard refuses any new blob containing it.
synthetic-calendars.mpp is the engine suite's calendar base whose recorded
oracle is pinned to its bytes, so -- exactly as for the paths -- each name is
replaced IN PLACE with a neutral string of the same character count, leaving
every length prefix, every other byte and the whole scheduling payload
unchanged.  The name itself is NOT written in this file (standing rule 6); it
is passed on the command line, and the run is logged under build/w28/.

    python -I scripts/w28_neutralise_operator.py "<operator name>" ["W28 User"]
"""

import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "tests" / "data" / "synthetic-calendars.mpp"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def main(argv):
    if len(argv) not in (2, 3):
        sys.exit(__doc__)
    operator = argv[1]
    neutral = argv[2] if len(argv) == 3 else "W28 User"
    if len(neutral) != len(operator):
        sys.exit("neutral string must be exactly as long as the name")
    data = bytearray(FIXTURE.read_bytes())
    before = sha256(data)
    hits = []
    for encoding in ("ascii", "utf-16le"):
        needle = operator.encode(encoding)
        replacement = neutral.encode(encoding)
        start = 0
        while True:
            at = data.find(needle, start)
            if at < 0:
                break
            data[at:at + len(needle)] = replacement
            hits.append((encoding, at))
            start = at + len(needle)
    if len(hits) != 3:
        sys.exit(f"expected 3 occurrences, found {len(hits)}: {hits}")
    after = sha256(data)
    if len(data) != FIXTURE.stat().st_size:
        sys.exit("file size changed")
    FIXTURE.write_bytes(bytes(data))
    print(f"{FIXTURE.relative_to(ROOT)}: replaced {len(hits)} name string(s)")
    print(f"  {before}\n  -> {after}")
    print(f"  {len(data)} bytes, size unchanged")


if __name__ == "__main__":
    main(sys.argv)
