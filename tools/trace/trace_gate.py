#!/usr/bin/env python3
"""Trace gate (docs/handoff/trace-runbook.md section 5): does a traced save equal the committed
pair's file, outside the null-save noise?  Runs INSIDE THE TRACE VM, where the saved file stays.

    python trace_gate.py TRACED REFERENCE CONTROL CONTROL_NULL

TRACED       the traced save (C:\\trace\\work\\<target>.mpp)
REFERENCE    the committed file the trace must reproduce (the pair's edited file, or the
             committed null save for the null target)
CONTROL, CONTROL_NULL
             the committed control and its committed null save; the bytes where these two
             differ (same length only) are the control mask.

A byte of the traced save may differ from the reference only where
  1. the control mask covers it, or
  2. NOISE below names it (an exact window), and the stream's ceiling on such bytes holds.
Any other differing byte, a size change, or a stream in one file only, is a problem.

The noise allow-list is the whole of the waiver: there is no by-name whole-stream skip.  The
four known sources, each with a narrow window taken from the committed control/null pairs
(99 of them) and the trace's null run (trace-null.md):
  * the UuidCreateSequential GUID counter bytes (Assn Fixed2Data +0 and +16);
  * the creation date-time stamps (Assn Var2Data key 0x0f40027a, Task FixedData +128);
  * the uninitialised top byte of the font word (payload +43 of the 0x2240000c items);
  * the per-save and per-file Props identity records (114/Props ids below, the 214/Props
    view GUID string).

Prints one summary line, then one line per problem stream (no names, no paths, only stream
paths and byte counts), then the SHA-256 of everything printed before it, so a copy read off
the screen can be checked.  Exit 0 means PASS (no problems), 1 FAIL.
"""

import hashlib
import struct
import sys

try:
    import olefile
except ImportError:  # the importer (the ctest) reports SKIP; the command line needs it
    olefile = None


def streams(path):
    ole = olefile.OleFileIO(path)
    try:
        return {"/".join("".join(ch if " " < ch < "\x7f" else "?" for ch in part) for part in s):
                ole.openstream(s).read() for s in ole.listdir(streams=True, storages=False)}
    finally:
        ole.close()


# ---------------------------------------------------------------------------------------------
# Fixed-stride streams: (stream-name suffix) -> (first record offset, stride).  Masks and noise
# windows are per record, so a stream that grew by a row keeps every older row's mask.
STRIDE = {
    "TBkndTask/FixedData": (48, 202),
    "TBkndTask/Fixed2Data": (0, 64),
    "TBkndAssn/FixedData": (0, 110),
    "TBkndAssn/Fixed2Data": (0, 48),
    "TBkndRsc/Fixed2Data": (0, 40),
    "TBkndCal/Fixed2Data": (0, 16),
    "TBkndCons/Fixed2Data": (0, 16),
}

# Noise windows.  Every rule is {"windows": [...], "ceiling": N} where the ceiling is the most
# bytes the null run showed outside the control mask (trace-null.md, the 14 problem lines), and
# a stride stream's ceiling is that number per `rows` records, scaled up for a longer stream.
#   record windows  (lo, hi) in-record offsets of a stride stream
#   byte windows    (lo, hi) absolute stream offsets
#   props windows   {record id: (lo, hi) payload offsets} for a Props stream
#   tail windows    (lo, hi) offsets from the first byte after the Props record area
NOISE = {
    # UuidCreateSequential GUIDs (trace-insert-milestone-r4.md 1b-1) and the per-record stamps.
    "TBkndAssn/Fixed2Data": {"record": [(0, 32), (32, 42)], "ceiling": 40, "rows": 2},
    "TBkndTask/Fixed2Data": {"record": [(0, 10), (24, 34), (42, 50)], "ceiling": 50, "rows": 6},
    "TBkndRsc/Fixed2Data": {"record": [(0, 10), (24, 34)], "ceiling": 20, "rows": 1},
    "TBkndCal/Fixed2Data": {"record": [(0, 10)], "ceiling": 10, "rows": 18},
    "TBkndCons/Fixed2Data": {"record": [(0, 16)], "ceiling": 30, "rows": 3},
    # Creation date-time stamp of a new row (trace-insert-milestone-r4.md 1b-2; insert-summary
    # "+984" = record +128 of the 202-byte rows that start at 48).
    "TBkndTask/FixedData": {"record": [(128, 132)], "ceiling": 3, "rows": 1},
    # Per-save and per-file Props records (trace-props-framing.md section 1).
    "114/Props": {"props": {0x02400007: (0, 4), 0x024013b1: (0, 4), 0x02400029: (0, 16),
                            0x024013c2: (0, 16), 0x02401394: (0, 16)}, "ceiling": 24},
    # The view GUID string record and one tail byte pair, as seen in the null saves.
    "214/Props": {"props": {0x03400001: (0, 80)}, "tail": [(1500, 1502)], "ceiling": 18},
    # View-stream save stamps (null run: CEdl 10 + 1, CFilter 20, CGrouping 20, CTable 10,
    # CV_iew 30 + 4, all at the offsets the committed control/null pairs show).
    "214/CEdl/FixedData": {"byte": [(144, 154)], "ceiling": 10},
    "214/CEdl/VarMeta": {"byte": [(20, 22)], "ceiling": 1},
    "214/CFilter/FixedData": {"byte": [(214, 224), (444, 454)], "ceiling": 20},
    "214/CGrouping/FixedData": {"byte": [(214, 224), (444, 454)], "ceiling": 20},
    "214/CTable/FixedData": {"byte": [(214, 224)], "ceiling": 10},
    "214/CV_iew/FixedData": {"byte": [(122, 132), (260, 270), (398, 408)], "ceiling": 30},
    # A saved date word whose offset moves with the view; the null run showed 4 bytes.
    "214/CV_iew/Var2Data": {"byte": [(0, 1 << 30)], "ceiling": 4},
    # The assignment creation stamp is a var block located through VarMeta.
    "TBkndAssn/Var2Data": {"varkey": {0x0F40027A: (0, 4)}, "ceiling": 3},
}

# Streams that carry a file path or a user name, whose length the machine moves (null run:
# CEdl/Var2Data 602 -> 500, Props14 530 -> 464, SummaryInformation 292 -> 284).  The size may move
# by at most `delta`; then the stream is not compared ("size-noise", not counted as checked).
# At equal size at most `same` bytes may differ.
PATHSTREAM = {
    "214/CEdl/Var2Data": {"delta": 128, "same": 64},
    "Props14": {"delta": 96, "same": 64},
    "SummaryInformation": {"delta": 16, "same": 40},
}

# The payload +43 byte of every 0x2240000c font item in 214/Props is allocator garbage
# (trace-font-item-r6.md); items are 12 header bytes + 44 per row.
FONT_ITEM_ID = 0x2240000C


def _stride(name):
    for key, (base, stride) in STRIDE.items():
        if name.endswith(key):
            return base, stride
    return None


def _noise_rule(name):
    for key, rule in NOISE.items():
        if name.endswith(key):
            return rule
    return None


def _path_rule(name):
    for key, rule in PATHSTREAM.items():
        if name.endswith(key):
            return rule
    return None


def props_records(data):
    """The (offset, length, id) of each framed record and the first byte after the record area:
    a 16-byte header, then u32 length, u32 id (byte 2 == 0x40), u32 kind, payload (props_edit.cpp)."""
    pos = 16
    out = []
    while pos + 12 <= len(data):
        length, rid = struct.unpack_from("<II", data, pos)
        if (rid & 0x00FF0000) != 0x00400000 or length > len(data) - pos - 12:
            break
        out.append((pos, length, rid))
        pos += 12 + length
    return out, pos


def font_item_garbage(data):
    """Offsets of payload +43 in every 44-byte font item payload of a view-bag stream."""
    out = []
    pos = 0
    while True:
        at = data.find(struct.pack("<I", FONT_ITEM_ID), pos)
        if at < 0 or at < 4:
            break
        length = struct.unpack_from("<I", data, at - 4)[0]
        if length and length % 44 == 0 and at + 8 + length <= len(data) and \
                struct.unpack_from("<H", data, at + 4)[0] == 1:
            for row in range(length // 44):
                out.append(at + 8 + row * 44 + 43)
        pos = at + 4
    return out


def noise_windows(name, data, meta=None):
    """The noise-window offsets of one stream's bytes -> (set of offsets, per-stream ceiling
    scale, record count).  `meta` is the companion VarMeta bytes for a Var2Data stream."""
    rule = _noise_rule(name)
    offs = set()
    nrec = 0
    if name.endswith("214/Props"):
        offs.update(font_item_garbage(data))
    if rule is None:
        return offs, nrec
    stride = _stride(name)
    if "record" in rule and stride:
        base, size = stride
        nrec = max(0, (len(data) - base) // size)
        for r in range(nrec):
            for lo, hi in rule["record"]:
                offs.update(range(base + r * size + lo, min(base + r * size + hi, len(data))))
    if "byte" in rule:
        for lo, hi in rule["byte"]:
            offs.update(range(lo, min(hi, len(data))))
    if "props" in rule:
        recs, tail = props_records(data)
        for pos, length, rid in recs:
            if rid in rule["props"]:
                lo, hi = rule["props"][rid]
                offs.update(range(pos + 12 + lo, pos + 12 + min(hi, length)))
        for lo, hi in rule.get("tail", []):
            offs.update(range(tail + lo, min(tail + hi, len(data))))
    if "varkey" in rule and meta is not None and len(meta) >= 24:
        count = struct.unpack_from("<I", meta, 8)[0]
        for k in range(count):
            at = 24 + 12 * k
            if at + 12 > len(meta):
                break
            _, off, key = struct.unpack_from("<III", meta, at)
            if key in rule["varkey"] and off != 0xFFFFFFFF:
                lo, hi = rule["varkey"][key]
                offs.update(range(off + 4 + lo, min(off + 4 + hi, len(data))))
    return offs, nrec


def ceiling_for(name, nrec):
    rule = _noise_rule(name)
    if rule is None:
        return 0
    base = rule["ceiling"]
    rows = rule.get("rows")
    if rows and nrec > rows:
        return -(-base * nrec // rows)
    return base


def control_mask(name, ref_len, ctl, nul):
    """The offsets where the control and its null save differ (equal-length control and null
    only), applied to the reference by index."""
    if ctl is None or nul is None or len(ctl) != len(nul):
        return None
    return {i for i in range(len(ctl)) if ctl[i] != nul[i]}


def compare(traced, ref, ctl, nul):
    """-> (lines, problems, checked, not_checked).  The four dicts are stream name -> bytes."""
    lines = []
    problems = 0
    checked = 0
    unchecked = 0
    names = sorted(set(traced) | set(ref))
    for name in names:
        if name not in ref:
            lines.append("only-in-traced %s %d" % (name, len(traced[name])))
            problems += 1
            continue
        if name not in traced:
            lines.append("only-in-reference %s %d" % (name, len(ref[name])))
            problems += 1
            continue
        a, b = ref[name], traced[name]
        path = _path_rule(name)
        if path is not None and len(a) != len(b):
            if abs(len(a) - len(b)) > path["delta"]:
                lines.append("size-differs %s %d->%d" % (name, len(a), len(b)))
                problems += 1
            else:
                lines.append("size-noise %s %d->%d" % (name, len(a), len(b)))
                unchecked += 1
            continue
        if len(a) != len(b):
            lines.append("size-differs %s %d->%d" % (name, len(a), len(b)))
            problems += 1
            continue
        checked += 1
        c, n = ctl.get(name), nul.get(name)
        mask = control_mask(name, len(a), c, n) or set()
        meta = None
        if name.endswith("Var2Data"):
            meta_name = name[:-len("Var2Data")] + "VarMeta"
            meta = (traced.get(meta_name), ref.get(meta_name))
        win = set()
        nrec = 0
        if meta is None:
            win, nrec = noise_windows(name, a)
            wb, _ = noise_windows(name, b)
            win |= wb
        else:
            for m in meta:
                w, nr = noise_windows(name, a, m)
                win |= w
                nrec = max(nrec, nr)
        diff = [i for i in range(len(a)) if a[i] != b[i] and i not in mask]
        if path is not None:
            ceiling = path["same"]
            outside = []
            counted = diff
        else:
            outside = [i for i in diff if i not in win]
            counted = [i for i in diff if i in win]
            ceiling = ceiling_for(name, nrec)
            if name.endswith("214/Props"):
                ceiling += len(font_item_garbage(a))
        if outside:
            lines.append("bytes-differ %s %d" % (name, len(outside)))
            problems += 1
        elif len(counted) > ceiling:
            lines.append("over-ceiling %s %d>%d" % (name, len(counted), ceiling))
            problems += 1
    return lines, problems, checked, unchecked


def main(argv):
    if len(argv) != 5:
        print(__doc__)
        return 2
    if olefile is None:
        print("trace_gate: install olefile (pip install olefile)")
        return 2
    traced, ref, ctl, nul = (streams(p) for p in argv[1:])
    lines, problems, checked, unchecked = compare(traced, ref, ctl, nul)
    names = set(traced) | set(ref)
    head = "%d streams, %d checked, %d problems" % (len(names), checked, problems)
    text = "\n".join([head] + lines) + "\n"
    sys.stdout.write(text)
    print("sha256 " + hashlib.sha256(text.encode("ascii")).hexdigest())
    return 0 if problems == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
