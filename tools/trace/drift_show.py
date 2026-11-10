import sys
import olefile


def fd(p):
    o = olefile.OleFileIO(p)
    for s in o.listdir(streams=True, storages=False):
        n = "/".join(s)
        if n.endswith("114/TBkndTask/FixedData"):
            return o.openstream(s).read()


a = fd(sys.argv[1])
b = fd(sys.argv[2])
print("len", len(a), len(b), "bytes differing from input:", sum(1 for i in range(min(len(a), len(b))) if a[i] != b[i]))
print("runs at", [(o, a[o:o + 6].hex(" ")) for o in (346, 548, 638, 750, 840)])
print("input   ", [(o, b[o:o + 6].hex(" ")) for o in (346, 548, 638, 750, 840)])
