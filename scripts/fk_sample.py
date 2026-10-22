"""W5: sample the values of unnamed Task keys (evidence for the unknowns list).

    python scripts/fk_sample.py 0x0b4004e3 0x0b4004e4 [file]
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fk_lib import FILES, load_task


def fmt(b):
    if b is None:
        return "ABSENT"
    if len(b) <= 8:
        return b.hex(" ")
    try:
        t = b.decode("utf-16le")
        if t.endswith("\x00") and t[:-1].isprintable():
            return f"text {t[:-1]!r}"
    except UnicodeDecodeError:
        pass
    return f"{len(b)}B {b[:16].hex(' ')}"


def main():
    keys = [int(a, 16) for a in sys.argv[1:] if a.startswith("0x")]
    if not keys:
        keys = [0x0B400563, 0x0B400564, 0x0B4004E3, 0x0B4004E4, 0x0B4004E5,
                0x0B4005A1, 0x0B40015E, 0x0B40007C]
    files = dict(FILES)
    for fname in ("project_a", "project_b"):
        t = load_task(files[fname])
        print(f"\n=== {fname}")
        for k in keys:
            es = [e for e in t["entries"] if e["field_id"] == k]
            print(f"  0x{k:08x} n={len(es)}")
            for e in es[:6]:
                print(f"      uid {e['mid']:10d} off {e['offset']:6d}"
                      f"  {fmt(e['block'])}")


if __name__ == "__main__":
    main()
