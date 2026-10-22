"""W5: compare the repo's own field-name tables with mpxj's MPP task ids.

`data/mpxj_task_field_ids.tsv` is an extraction of
`MPPTaskField.FIELD_ARRAY` from
https://raw.githubusercontent.com/joniles/mpxj/master/src/main/java/org/mpxj/common/MPPTaskField.java
(id -> enum constant).  It is used only to grade confidence in the names that
come from `analysis/field_catalog_task.tsv` / `analysis/field_id_map.tsv`.

    python scripts/fk_compare.py
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fk_lib import FILES, key_stats, load_task, merged_names

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MPXJ = os.path.join(ROOT, "data", "mpxj_task_field_ids.tsv")


def norm(s):
    return re.sub(r"[^a-z0-9]", "", s.lower())


def main():
    mpxj = {}
    with open(MPXJ, encoding="utf-8") as f:
        f.readline()              # id / task_field header
        for line in f:
            i, n = line.rstrip("\n").split("\t")
            mpxj[int(i)] = n
    names = merged_names()
    keys = set()
    for _, path in FILES:
        if os.path.exists(path):
            keys |= set(key_stats(load_task(path)))
    print(f"mpxj ids: {len(mpxj)}; distinct keys in corpus/probe: {len(keys)}")
    agree = disagree = only_cat = only_mpxj = neither = 0
    for k in sorted(keys):
        low = k & 0xFFFF
        c, m = names.get(k), mpxj.get(low)
        if c and m:
            if norm(c) == norm(m):
                agree += 1
            else:
                disagree += 1
                print(f"  DISAGREE 0x{k:08x}: catalog={c} mpxj={m}")
        elif c:
            only_cat += 1
        elif m:
            only_mpxj += 1
            print(f"  MPXJ-ONLY 0x{k:08x}: {m}")
        else:
            neither += 1
    print(f"agree {agree}, disagree {disagree}, catalog-only {only_cat},"
          f" mpxj-only {only_mpxj}, neither {neither}")


if __name__ == "__main__":
    main()
