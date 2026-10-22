"""W5: one-change fixture diff on the corrected Task VarMeta parse.

    python scripts/fk_diff.py            # every var-* fixture + probe-v1/v2
    python scripts/fk_diff.py var-notes  # one

For each fixture it compares the set of (uid, field key) pairs and each pair's
Var2Data payload against `base.mpp`.  A key whose payload changes for exactly
the row that the fixture changed is direct proof of that key's meaning.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fk_lib import FILES, load_task, merged_names

ORDER = ["var-name", "var-notes", "var-pct", "var-duration", "var-priority",
         "var-projstart", "var-title", "var-rsc2", "var-baseline",
         "probe-v1", "probe-v2"]


def keymap(task):
    return {(e["mid"], e["field_id"]): e["block"] for e in task["entries"]}


def show(payload):
    if payload is None:
        return "-"
    if payload[:2] == b"{\\":
        return f"RTF {len(payload)}B {payload[:24]!r}"
    try:
        t = payload.decode("utf-16le")
        if t.endswith("\x00") and (t[:-1] == "" or t[:-1].isprintable()):
            return f"text({len(payload)}B) {t[:-1]!r}"
    except UnicodeDecodeError:
        pass
    if len(payload) <= 8:
        return f"{len(payload)}B {payload.hex(' ')}"
    return f"{len(payload)}B {payload[:12].hex(' ')}..."


def main():
    only = sys.argv[1:] or ORDER
    files = dict(FILES)
    names = merged_names()
    base = load_task(files["base"])
    bb = keymap(base)
    for name in only:
        if name not in files:
            print(f"unknown fixture {name}")
            continue
        t = load_task(files[name])
        nb = keymap(t)
        print(f"\n=== {name} (entries {len(base['entries'])} -> {len(t['entries'])},"
              f" Var2Data {len(base['v2'])} -> {len(t['v2'])} B)")
        for key in sorted(set(bb) | set(nb)):
            a, b = bb.get(key), nb.get(key)
            if a is None:
                print(f"  ADDED   uid {key[0]:10d} key 0x{key[1]:08x}"
                      f" {names.get(key[1], '?')}: {show(b)}")
            elif b is None:
                print(f"  REMOVED uid {key[0]:10d} key 0x{key[1]:08x}"
                      f" {names.get(key[1], '?')}: {show(a)}")
            elif a != b:
                print(f"  CHANGED uid {key[0]:10d} key 0x{key[1]:08x}"
                      f" {names.get(key[1], '?')}: {show(a)} -> {show(b)}")


if __name__ == "__main__":
    main()
