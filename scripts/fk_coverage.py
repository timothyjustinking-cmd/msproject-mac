"""W5: coverage report for data/field_keys.tsv.

    python scripts/fk_coverage.py

Counts, for each corpus file, how many Task VarMeta entries / distinct keys /
addressed Var2Data bytes are covered by a name in data/field_keys.tsv.
`absent` entries (offset 0xFFFFFFFF, no block) are reported separately because
they carry no value bytes.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fk_lib import FILES, key_stats, load_task

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABLE = os.path.join(ROOT, "data", "field_keys.tsv")


def main():
    named = {}
    with open(TABLE, encoding="utf-8") as f:
        f.readline()
        for line in f:
            c = line.rstrip("\n").split("\t")
            named[int(c[1], 16)] = c[2]
    for name, path in FILES:
        if name not in ("base", "project_a", "project_b") or not os.path.exists(path):
            continue
        t = load_task(path)
        st = key_stats(t)
        absent = sum(d["shapes"].get("nodata", 0) for d in st.values())
        tot_entries = len(t["entries"])
        tot_bytes = sum(d["bytes"] for d in st.values())
        n_keys = sum(1 for k in st if named.get(k))
        n_ent = sum(d["n"] for k, d in st.items() if named.get(k))
        n_by = sum(d["bytes"] for k, d in st.items() if named.get(k))
        # bytes that are actually addressed (block present)
        addr = sum(d["bytes"] for d in st.values())
        print(f"{name}: rows {len(t['rows'])} entries {tot_entries} "
              f"(absent {absent}) keys {len(st)} Var2Data {len(t['v2'])} B")
        print(f"   named keys {n_keys}/{len(st)}"
              f"  entries {n_ent}/{tot_entries} = {100*n_ent/tot_entries:.1f}%"
              f"  bytes {n_by}/{addr} = {100*n_by/addr:.1f}%"
              f"  (addressed {addr}/{len(t['v2'])})")
        unnamed = sorted((k for k in st if not named.get(k)),
                         key=lambda k: -st[k]["n"])
        print(f"   unnamed entries: {sum(st[k]['n'] for k in unnamed)}"
              f" across {len(unnamed)} keys")
        for k in unnamed[:12]:
            print(f"      0x{k:08x} n={st[k]['n']:3d} bytes={st[k]['bytes']:5d}"
                  f" {dict(st[k]['shapes'])}")


if __name__ == "__main__":
    main()
