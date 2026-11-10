"""mapdiff.py <family> : map the pair's changed stream bytes (control -> set, public fixtures) to setter calls (spec/writers/evidence/trace-fc4-<target>.txt, hash-checked VM text).
Fixed streams: stream offset = base + stride*uid + descriptor offset (Task FixedData 48/202; Task Fixed2Data 192/64; Assn Fixed2Data 0/48).
Each run is shown with the pair's before/after bytes and the setter calls whose uid and offset fit and whose first dword equals the new bytes."""
import re
import struct
import sys

import olefile

fam = sys.argv[1]
tgt = {"baseline": "baseline", "insert-summary": "insert-summary", "insert-milestone": "insert-milestone"}[fam]
D = "tests/data/"  # run from the repo root
ctl = "wave30-%s-control" % fam
setn = "wave30-%s" % ("baseline-set" if fam == "baseline" else fam)


def S(f, suffix):
    o = olefile.OleFileIO(D + f + ".mpp")
    for s in o.listdir(streams=True, storages=False):
        n = "/".join(s)
        if n.endswith(suffix):
            return o.openstream(s).read()


calls = []
for ln in open("spec/writers/evidence/trace-fc4-%s.txt" % tgt):
    m = re.match(r"C (\S+)t(\w+) row (\d+) fld (\d+) d (\S+) v (\S+)", ln.strip())
    if not m:
        continue
    pos, tbl, row, fld, d, v = m.groups()
    parts = d.split(".")
    calls.append(dict(pos=pos, tbl=tbl, row=int(row), fld=int(fld), off=int(parts[1], 16), key=int(parts[3], 16), sz=parts[4], fl=parts[5], v=v))

geom = {"114/TBkndTask/FixedData": ("b4", 48, 202, 3), "114/TBkndTask/Fixed2Data": ("b4", 192, 64, 3), "114/TBkndAssn/Fixed2Data": ("f4", 0, 48, 0),
        "114/TBkndAssn/FixedData": ("f4", 0, 110, 0)}


def runs(a, b):
    r = []
    i = 0
    n = min(len(a), len(b))
    while i < n:
        if a[i] != b[i]:
            j = i
            while j < n and (a[j] != b[j] or (j + 1 < n and a[j + 1] != b[j + 1] and j - i < 64)):
                j += 1
            r.append((i, j))
            i = j
        else:
            i += 1
    return r


for suffix in geom:
    a = S(ctl, suffix)
    b = S(setn, suffix)
    if a is None or b is None:
        continue
    pre, base, stride, row0 = geom[suffix]
    print("== %s ctl %d set %d" % (suffix, len(a), len(b)))
    if len(a) != len(b):
        print("   size change; only the common prefix is compared")
    for (i, j) in runs(a, b):
        rel = None
        if base is not None and i >= base:
            uid = (i - base) // stride
            rel = (i - base) % stride
        s = "   +%d len %d  %s -> %s" % (i, j - i, a[i:j].hex(" ")[:48], b[i:j].hex(" ")[:48])
        cand = []
        if rel is not None:
            for c in calls:
                if not ("%x" % c["key"]).startswith(pre):
                    continue
                if c["row"] - row0 != uid:
                    continue
                if c["off"] <= rel < c["off"] + 8:
                    try:
                        vb = struct.pack("<I", int(c["v"], 16))
                    except Exception:
                        continue
                    at = i - (rel - c["off"])
                    if b[at:at + 4] == vb or b[at:at + 2] == vb[:2]:
                        cand.append("%s key %08x off 0x%x fld %d v %s" % (c["pos"], c["key"], c["off"], c["fld"], c["v"]))
        print(s + ("   uid %d rel %d" % (uid, rel) if rel is not None else "") + ("  <- " + "; ".join(cand) if cand else ""))
    if len(b) > len(a) and base is not None:
        uid = (len(a) - base) // stride
        rows = []
        for c in calls:
            if not ("%x" % c["key"]).startswith(pre) or c["row"] - row0 != uid:
                continue
            at = base + stride * uid + c["off"]
            try:
                vb = struct.pack("<I", int(c["v"], 16))
            except Exception:
                continue
            if at + 4 <= len(b) and c["off"] < stride and b[at:at + 4] == vb and any(b[at:at + 4]):
                rows.append("      +%d  %s key %08x off 0x%x v %s" % (at, c["pos"], c["key"], c["off"], c["v"]))
        print("   new row uid %d: bytes %d..%d; setter calls whose first dword equals the saved bytes (non-zero): %d" % (uid, base + stride * uid, base + stride * (uid + 1), len(rows)))
        for r in rows:
            print(r)
