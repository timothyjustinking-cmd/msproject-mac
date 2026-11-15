"""Read the spans of the gate's masked stamps from saved copies (W36-2.0).

    python span_probe.py <dir> [<dir> ...]

Every .mpp under each directory (not recursive) is opened with the gate's own `streams` and the stamp
fields the gate masks are decoded with the gate's own framing: the LastSave FILETIME of
?SummaryInformation, the 114/Props save words (and the baseline-slot words that hold a save word), the
Task FixedData row stamp, the Assn and Rsc creation stamps located through VarMeta, and the view stamp
of 214/CV_iew/Var2Data.  Prints the count read and the minimum and maximum of each class, in the form
the gate's constants take; it prints no file name.  Nothing is written.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace_gate as g   # noqa: E402


def summary_lastsave(data):
    if len(data) < 56:
        return None
    sec = g._u32(data, 44)
    if sec + 8 > len(data):
        return None
    count = g._u32(data, sec + 4)
    table = sec + 8
    for i in range(min(count, (len(data) - table) // 8)):
        pid, off = struct.unpack_from("<II", data, table + 8 * i)
        if pid == g.SI_LASTSAVE and sec + off + 12 <= len(data) and g._u32(data, sec + off) == 0x40:
            return struct.unpack_from("<Q", data, sec + off + 4)[0]
    return None


def read_file(path, acc):
    st = g.streams(path)
    ls = summary_lastsave(st.get(g.SUMMARY, b""))
    if ls is not None:
        acc["lastsave"].append(ls)
    props = st.get("???114/Props", b"")
    for pos, length, rid, kind in g.frame_records(props, 16) or []:
        if length == 4 and rid in g.SAVE_WORD_IDS:
            acc["props save word"].append(g._u32(props, pos + 12))
        elif length == 4 and rid in (0x02401395, 0x02401396, 0x02401397, 0x0240139F):
            word = g._u32(props, pos + 12)
            if word != 0xFFFFFFFF:
                acc["props slot word"].append(word)
    fixed = st.get("???114/TBkndTask/FixedData", b"")
    base, size = g.STRIDE["???114/TBkndTask/FixedData"]
    for r in range(max(0, (len(fixed) - base) // size)):
        acc["task row stamp"].append(g._u32(fixed, base + r * size + 128))
    for name, key, label in (("???114/TBkndAssn/Var2Data", 0x0F40027A, "assn stamp"),
                             ("???114/TBkndRsc/Var2Data", 0x0C4002D6, "rsc stamp")):
        var = st.get(name, b"")
        for uid, off, k in g._meta_blocks(st.get(g.VAR_META[name])):
            if k == key and off != 0xFFFFFFFF and off + 8 <= len(var):
                acc[label].append(g._u32(var, off + 4))
    var = st.get(g.CVIEW_VAR2, b"")
    for uid, off, key in g._meta_blocks(st.get(g.VAR_META[g.CVIEW_VAR2])):
        if key != g.BAG_KEY or off == 0xFFFFFFFF or off + 4 > len(var):
            continue
        end = off + 4 + g._u32(var, off)
        recs = g.frame_records(var[:end], off + 20) if end <= len(var) else None
        for pos, length, rid, kind in recs or []:
            if rid == g.CVIEW_STAMP_ID and kind == 1 and length == 20 and g._u32(var, pos + 12) == 0:
                acc["view stamp"].append(g._u32(var, pos + 16))


def main(argv):
    classes = ("lastsave", "props save word", "props slot word", "task row stamp", "assn stamp",
               "rsc stamp", "view stamp")
    acc = {c: [] for c in classes}
    files = unreadable = 0
    for d in argv[1:]:
        for name in sorted(os.listdir(d)):
            if not name.lower().endswith(".mpp"):
                continue
            try:
                read_file(os.path.join(d, name), acc)
                files += 1
            except Exception:
                unreadable += 1
    print("files read %d, unreadable %d" % (files, unreadable))
    stamps = []
    for c in classes:
        v = acc[c]
        if not v:
            print("%-16s none" % c)
            continue
        zero = sum(1 for x in v if x in (0, 0xFFFFFFFF))
        w = 16 if c == "lastsave" else 8
        print("%-16s n=%-6d min=%#0*x max=%#0*x  (0 or all-ones: %d)" % (c, len(v), w + 2, min(v), w + 2,
                                                                         max(v), zero))
        if c != "lastsave" and c != "view stamp":
            stamps += [x for x in v if x not in (0, 0xFFFFFFFF)]
    if stamps:
        print("STAMP_SPAN (task, assn, rsc, props words) n=%d (%#010x, %#010x)" % (len(stamps), min(stamps),
                                                                                    max(stamps)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
