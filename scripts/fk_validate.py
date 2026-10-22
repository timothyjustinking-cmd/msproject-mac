"""W5: validate the corrected VarMeta framing on every corpus file.

    python scripts/fk_validate.py

Checks, per file:
  * VarMeta length == 24 + 12 * n;
  * the header's entry count vs. the number of entries actually present;
  * every `uid` in an entry is the UniqueID of some FixedData row, or
    0x80000000 | UniqueID (the second value family);
  * entries with offset 0xFFFFFFFF (no block) and Var2Data bytes not reached by
    any entry offset.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe_lib as L
from fk_lib import FILES, load_task


def main():
    print("file     rows  hdr_n  real_n  mids_ok  absent  addressed  stream  unreached")
    for name, path in FILES:
        if not os.path.exists(path):
            continue
        t = load_task(path)
        uids = {L.u32(r, 4) for r in t["rows"]}
        ok = 0
        bad = []
        for e in t["entries"]:
            m = e["mid"]
            if m in uids or (m & 0x80000000) and (m & 0x7FFFFFFF) in uids:
                ok += 1
            else:
                bad.append(m)
        absent = sum(1 for e in t["entries"] if e["offset"] == 0xFFFFFFFF)
        addressed = sum(len(e["block"] or b"") for e in t["entries"])
        print(f"{name:8s} {len(t['rows']):5d} {t['header']['header_count']:6d}"
              f" {len(t['entries']):7d} {ok:8d} {absent:7d}"
              f" {addressed:10d} {len(t['v2']):7d}"
              f" {len(t['v2'])-addressed:9d}"
              + (f"  BAD mids {bad[:3]}" if bad else ""))


if __name__ == "__main__":
    main()
