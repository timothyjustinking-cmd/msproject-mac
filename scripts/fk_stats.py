"""W5: statistics over the corrected Task VarMeta parse.

    python scripts/fk_stats.py

Prints, per corpus file, the distinct field keys and how many VarMeta entries /
Var2Data bytes they account for, plus whether `analysis/field_catalog_task.tsv`
or `analysis/field_id_map.tsv` has a name for them.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fk_lib import FILES, key_stats, load_task, merged_names, shape


def main():
    names = merged_names()
    for name, path in FILES:
        if not os.path.exists(path):
            print(f"missing {path}")
            continue
        t = load_task(path)
        st = key_stats(t)
        named = [k for k in st if k in names]
        byts = sum(v["bytes"] for v in st.values())
        print(f"\n=== {name}: rows {len(t['rows'])}, entries {len(t['entries'])},"
              f" distinct keys {len(st)}, Var2Data {len(t['v2'])} B")
        print(f"  named keys {len(named)}/{len(st)}; named entries"
              f" {sum(st[k]['n'] for k in named)}/{len(t['entries'])};"
              f" named bytes {sum(st[k]['bytes'] for k in named)}/{byts}")
        unknown = sorted((k for k in st if k not in names),
                         key=lambda k: -st[k]["n"])
        print(f"  unnamed keys ({len(unknown)}): "
              + " ".join(f"0x{k:08x}({st[k]['n']})" for k in unknown[:25]))
        print("  top keys:")
        for k in sorted(st, key=lambda k: -st[k]["n"])[:12]:
            shapes = ",".join(f"{s}x{c}" for s, c in st[k]["shapes"].most_common())
            print(f"    0x{k:08x} {str(names.get(k, '?'))[:28]:28s}"
                  f" n={st[k]['n']:4d} {shapes}")


if __name__ == "__main__":
    main()
