#!/usr/bin/env python3
"""Trace gate (docs/handoff/trace-runbook.md section 5): does a traced save equal the committed
pair's file, outside the null-save noise?  Runs INSIDE THE TRACE VM, where the saved file stays.

    python trace_gate.py TRACED REFERENCE CONTROL CONTROL_NULL

TRACED       the traced save (C:\\trace\\work\\<target>.mpp)
REFERENCE    the committed file the trace must reproduce (the pair's edited file, or the
             committed null save for the null target)
CONTROL, CONTROL_NULL
             the committed control and its committed null save.  An absent, unreadable or
             control-identical CONTROL_NULL is a FAIL named "no null", never a pass (W36-0.3).
             The bytes where these two differ are the control mask.  It is built from an equal-length control and null
             only, and is refused (line "mask-refused") when the reference's length differs
             from the control's, except in a fixed-stride stream, which is masked per record.

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
# Fixed-stride streams: (exact stream name) -> (first record offset, stride).  Masks and noise
# windows are per record, so a stream that grew by a row keeps every older row's mask.
STRIDE = {
    "???114/TBkndTask/FixedData": (48, 202),
    "???114/TBkndTask/Fixed2Data": (0, 64),
    "???114/TBkndAssn/FixedData": (0, 110),
    "???114/TBkndAssn/Fixed2Data": (0, 48),
    "???114/TBkndRsc/Fixed2Data": (0, 40),
    "???114/TBkndCal/Fixed2Data": (0, 16),
    "???114/TBkndCons/Fixed2Data": (0, 16),
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
    "???114/TBkndAssn/Fixed2Data": {"record": [(0, 32), (32, 42)], "ceiling": 40, "rows": 2},
    "???114/TBkndTask/Fixed2Data": {"record": [(0, 10), (24, 34), (42, 50)], "ceiling": 50, "rows": 6},
    "???114/TBkndRsc/Fixed2Data": {"record": [(0, 10), (24, 34)], "ceiling": 20, "rows": 1},
    "???114/TBkndCal/Fixed2Data": {"record": [(0, 10)], "ceiling": 10, "rows": 18},
    "???114/TBkndCons/Fixed2Data": {"record": [(0, 16)], "ceiling": 30, "rows": 3},
    # Creation date-time stamp of a new row (trace-insert-milestone-r4.md 1b-2; insert-summary
    # "+984" = record +128 of the 202-byte rows that start at 48).
    "???114/TBkndTask/FixedData": {"record": [(128, 132)], "ceiling": 3, "rows": 1},
    # Per-save and per-file Props records (trace-props-framing.md section 1).
    "???114/Props": {"props": {0x02400007: (0, 4), 0x024013b1: (0, 4), 0x02400029: (0, 16),
                            0x024013c2: (0, 16), 0x02401394: (0, 16)}, "ceiling": 24},
    # The view GUID string record, as seen in the null saves.  (The typed record 0x00010007, the
    # length of 214/CEdl/Var2Data, is canonicalised with that stream's other size words.)
    "???214/Props": {"props": {0x03400001: (0, 80)}, "ceiling": 17},
    # View-stream save stamps (null run: CEdl 10 + 1, CFilter 20, CGrouping 20, CTable 10,
    # CV_iew 30 + 4, all at the offsets the committed control/null pairs show).
    "???214/CEdl/FixedData": {"byte": [(144, 154)], "ceiling": 10},
    "???214/CFilter/FixedData": {"byte": [(214, 224), (444, 454)], "ceiling": 20},
    "???214/CGrouping/FixedData": {"byte": [(214, 224), (444, 454)], "ceiling": 20},
    "???214/CTable/FixedData": {"byte": [(214, 224)], "ceiling": 10},
    "???214/CV_iew/FixedData": {"byte": [(122, 132), (260, 270), (398, 408)], "ceiling": 30},
    # The assignment creation stamp is a var block located through VarMeta.
    "???114/TBkndAssn/Var2Data": {"varkey": {0x0F40027A: (0, 4)}, "ceiling": 3},
}

# Streams that carry a file path or a user name, whose length the machine moves (null run:
# CEdl/Var2Data 602 -> 500, Props14 530 -> 464, SummaryInformation 292 -> 284).  They are not
# skipped and not given a size tolerance: each is parsed, and the decoded noise fields (the path
# and user-name strings, the size words derived from their length, the per-save SummaryInformation
# values) are replaced by fixed placeholders in a canonical form that is compared byte for byte
# (`canonical`, below).  A field is only replaced when it decodes exactly; one that does not stays
# raw, so a difference in it shows.
CEDL_VAR2 = "???214/CEdl/Var2Data"
CEDL_META = "???214/CEdl/VarMeta"
VIEW_PROPS = "???214/Props"
PROPS14 = "Props14"
SUMMARY = "?SummaryInformation"
CVIEW_VAR2 = "???214/CV_iew/Var2Data"
# Var2Data streams whose noise is located through their VarMeta: name -> VarMeta name.  Every
# stream name in this file is matched whole, never by a suffix: "?DocumentSummaryInformation" is
# not "?SummaryInformation" and is compared in full.
VAR_META = {
    CVIEW_VAR2: "???214/CV_iew/VarMeta",
    "???114/TBkndAssn/Var2Data": "???114/TBkndAssn/VarMeta",
}

CEDL_PATH_ID = 0x26400031        # UTF-16 file path ending "!" and NUL (the last CEdl record)
PROPS14_PATH_ID = 0x35400008     # UTF-16 file path
PROPS14_NAME_ENTRIES = (0xFFFFFFFF, 0x3540000B)   # tail entries, each a UTF-16 user name
VIEW_CEDL_SIZE_ID = 0x00010007   # 214/Props typed u32: the length of 214/CEdl/Var2Data
SI_AUTHOR, SI_LASTAUTHOR, SI_REVNUMBER, SI_LASTSAVE = 4, 8, 9, 13

# The font word of a view's font item (id 0x2240000c, kind 1, 44 bytes a row) is a bag record in a
# CV_iew/Var2Data bag block; its top byte, payload +43 of each row, is allocator garbage
# (trace-font-item-r6.md).  The blocks are the VarMeta entries of key BAG_KEY; each is a 4-byte
# length, then a Props-format blob: a 16-byte header, then records [len u32][id u32][kind u32]
# [payload], each padded to an even length.
FONT_ITEM_ID = 0x2240000C
FONT_ROW = 44
FONT_GARBAGE = 43
BAG_KEY = 0x20400006


def _stride(name):
    for key, (base, stride) in STRIDE.items():
        if name == key:
            return base, stride
    return None


def _noise_rule(name):
    for key, rule in NOISE.items():
        if name == key:
            return rule
    return None


def _u32(data, at):
    return struct.unpack_from("<I", data, at)[0]


def _text_ok(payload):
    """A decoded path or user name: UTF-16LE of printable characters, at most one trailing NUL."""
    if len(payload) % 2 or len(payload) > 2048:
        return False
    try:
        text = payload.decode("utf-16le")
    except UnicodeDecodeError:
        return False
    if text.endswith("\0"):
        text = text[:-1]
    return all(ch >= " " and ch != "\x7f" for ch in text)


def frame_records(data, base):
    """[(offset, length, id, kind)] of the Props-format records ([len][id][kind][payload], padded
    to an even length) that chain exactly from `base` to the end of `data`, else None."""
    pos = base
    out = []
    while pos < len(data):
        if pos + 12 > len(data):
            return None
        length, rid, kind = struct.unpack_from("<III", data, pos)
        if pos + 12 + length > len(data):
            return None
        out.append((pos, length, rid, kind))
        pos += 12 + length + (length & 1)
    return out if pos == len(data) else None


def _canon_cedl_var2(data, streams):
    """214/CEdl/Var2Data: the path record (length word, payload, pad) and the three size words
    derived from the stream length (+0 = len-4, +4 and +8 = len-8) are noise."""
    recs = frame_records(data, 20) if len(data) >= 20 else None
    if recs is None:
        return data, set()
    out = bytearray(data[:20])
    noise = set()
    for at, want in ((0, len(data) - 4), (4, len(data) - 8), (8, len(data) - 8)):
        if _u32(data, at) == want:
            out[at:at + 4] = bytes(4)
            noise.update(range(at, at + 4))
    for pos, length, rid, kind in recs:
        end = pos + 12 + length + (length & 1)
        payload = data[pos + 12:pos + 12 + length]
        if rid == CEDL_PATH_ID and _text_ok(payload) and payload.endswith(b"!\0\0\0"):
            out += struct.pack("<III", 0, rid, kind)
            noise.update(range(pos, pos + 4))
            noise.update(range(pos + 12, end))
        else:
            out += data[pos:end]
    return bytes(out), noise


def _canon_cedl_meta(data, streams):
    """214/CEdl/VarMeta +20 is the length of 214/CEdl/Var2Data: noise when it says so."""
    var2 = streams.get(CEDL_VAR2)
    if var2 is None or len(data) < 24 or _u32(data, 20) != len(var2):
        return data, set()
    return data[:20] + bytes(4) + data[24:], set(range(20, 24))


def _canon_view_props(data, streams):
    """214/Props: the typed record 0x00010007 is the length of 214/CEdl/Var2Data (a size word of
    the path-bearing stream); noise only when it equals that length."""
    var2 = streams.get(CEDL_VAR2)
    recs = frame_records(data, 16) if len(data) >= 16 and var2 is not None else None
    if recs is None:
        return data, set()
    out = bytearray(data)
    noise = set()
    for pos, length, rid, kind in recs:
        if rid == VIEW_CEDL_SIZE_ID and length == 4 and _u32(data, pos + 12) == len(var2):
            out[pos + 12:pos + 16] = bytes(4)
            noise.update(range(pos + 12, pos + 16))
    return bytes(out), noise


def _tail_entries(tail):
    """The Props14 tail [count][entries (id, type 0x1e, UTF-16 name)] as [(id, name bytes)], or
    None.  A name has no length or terminator; it runs to the next entry's `id 1e 00 00 00`."""
    if len(tail) < 4:
        return None
    count = _u32(tail, 0)
    marks = []
    for at in range(4, len(tail) - 7, 2):
        if _u32(tail, at) in PROPS14_NAME_ENTRIES and _u32(tail, at + 4) == 0x1E:
            marks.append(at)
    if len(marks) != count or (marks and marks[0] != 4) or not marks and len(tail) != 4:
        return None
    ends = marks[1:] + [len(tail)]
    return [(_u32(tail, at), tail[at + 8:end]) for at, end in zip(marks, ends)]


def _canon_props14(data, streams):
    """Props14: a blob [size][size][0x10000][count] of Props-format records (size = blob end - 4,
    derived), then the tail.  Noise: the file path record, the two blob size words, the user-name
    strings of the tail, and the tail entry id 0xffffffff, which the committed controls and edits
    carry and 98 of the 99 committed null saves do not (a per-save entry, not decoded)."""
    if len(data) < 16 or _u32(data, 8) != 0x10000:
        return data, set()
    end = _u32(data, 0) + 4
    if _u32(data, 4) != end - 4 or end > len(data):
        return data, set()
    recs = frame_records(data[:end], 16)
    entries = _tail_entries(data[end:])
    if recs is None or entries is None:
        return data, set()
    out = bytearray(data[:16])
    out[0:8] = bytes(8)
    noise = set(range(0, 8))
    for pos, length, rid, kind in recs:
        stop = pos + 12 + length + (length & 1)
        payload = data[pos + 12:pos + 12 + length]
        if rid == PROPS14_PATH_ID and _text_ok(payload):
            out += struct.pack("<III", 0, rid, kind)
            noise.update(range(pos, pos + 4))
            noise.update(range(pos + 12, stop))
        else:
            out += data[pos:stop]
    kept = [e for e in entries if e[0] != 0xFFFFFFFF]
    out += struct.pack("<I", len(kept))
    at = end + 4
    for eid, name in entries:
        stop = at + 8 + len(name)
        if eid == 0xFFFFFFFF:
            noise.update(range(at, stop))
        elif _text_ok(name):
            noise.update(range(at + 8, stop))
        at = stop
    noise.update(range(end, end + 4))   # the entry count follows the dropped entry
    for eid, name in kept:
        out += struct.pack("<II", eid, 0x1E) + (b"" if _text_ok(name) else name)
    return bytes(out), noise


def _canon_summary(data, streams):
    """?SummaryInformation (a property set, one section at the offset in the header): the
    Author, LastAuthor and RevNumber strings and the LastSave FILETIME are noise; the offsets and
    the section size follow from the values and are rebuilt.  The section must lay its values
    out back to back, else the stream stays raw."""
    if len(data) < 56:
        return data, set()
    sec = _u32(data, 44)
    if sec + 8 > len(data) or _u32(data, sec) != len(data) - sec:
        return data, set()
    count = _u32(data, sec + 4)
    table = sec + 8
    if table + 8 * count > len(data):
        return data, set()
    ents = [(_u32(data, table + 8 * i), _u32(data, table + 8 * i + 4)) for i in range(count)]
    order = sorted(range(count), key=lambda i: ents[i][1])
    if count == 0 or ents[order[0]][1] != 8 + 8 * count:
        return data, set()
    values = {}
    for k, i in enumerate(order):
        lo = sec + ents[i][1]
        hi = sec + ents[order[k + 1]][1] if k + 1 < count else len(data)
        if hi < lo:
            return data, set()
        values[i] = (lo, hi)
    noise = set()
    canon = {}
    for i, (pid, _) in enumerate(ents):
        lo, hi = values[i]
        raw = data[lo:hi]
        if pid in (SI_AUTHOR, SI_LASTAUTHOR, SI_REVNUMBER) and len(raw) >= 8 and \
                _u32(raw, 0) == 0x1E and 8 + _u32(raw, 4) <= len(raw):
            # a VT_LPSTR: type, length (with its NUL), bytes, padding to a 4-byte boundary
            raw = struct.pack("<II", 0x1E, 0)
            noise.update(range(lo + 4, hi))
        elif pid == SI_LASTSAVE and len(raw) == 12 and _u32(raw, 0) == 0x40:
            raw = struct.pack("<I", 0x40) + bytes(8)
            noise.update(range(lo + 4, hi))
        canon[i] = raw
    out = bytearray(data[:sec])
    offs = {}
    cur = 8 + 8 * count
    for i in order:
        offs[i] = cur
        cur += len(canon[i])
    out += struct.pack("<II", cur, count)
    for i, (pid, _) in enumerate(ents):
        out += struct.pack("<II", pid, offs[i])
    for i in order:
        out += canon[i]
    noise.update(range(sec, sec + 4))
    for i in range(count):
        noise.update(range(table + 8 * i + 4, table + 8 * i + 8))
    return bytes(out), noise


CANON = {
    CEDL_VAR2: _canon_cedl_var2,
    CEDL_META: _canon_cedl_meta,
    VIEW_PROPS: _canon_view_props,
    PROPS14: _canon_props14,
    SUMMARY: _canon_summary,
}


def canonical(streams, with_noise=False):
    """One file's streams with the decoded noise fields of the CANON streams replaced by fixed
    placeholders.  `with_noise` also returns, per stream, the offsets of the raw bytes that were
    replaced (the named noise fields)."""
    out = {}
    noise = {}
    for name, data in streams.items():
        if name in CANON:
            out[name], noise[name] = CANON[name](data, streams)
        else:
            out[name], noise[name] = data, set()
    return (out, noise) if with_noise else out


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


def font_item_garbage(data, meta):
    """Offsets of payload +43 of every row of every font item in the bag blocks of a
    CV_iew/Var2Data stream, found by parsing the blocks its VarMeta locates, never by a byte
    search."""
    out = []
    if meta is None or len(meta) < 24:
        return out
    count = struct.unpack_from("<I", meta, 8)[0]
    for k in range(count):
        at = 24 + 12 * k
        if at + 12 > len(meta):
            break
        _, off, key = struct.unpack_from("<III", meta, at)
        if key != BAG_KEY or off == 0xFFFFFFFF or off + 4 > len(data):
            continue
        end = min(off + 4 + struct.unpack_from("<I", data, off)[0], len(data))
        pos = off + 4 + 16
        while pos + 12 <= end:
            length, rid, kind = struct.unpack_from("<III", data, pos)
            if pos + 12 + length > end:
                break
            if rid == FONT_ITEM_ID and kind == 1 and length % FONT_ROW == 0:
                out.extend(pos + 12 + row * FONT_ROW + FONT_GARBAGE
                           for row in range(length // FONT_ROW))
            pos += 12 + length + (length & 1)
    return out


def noise_windows(name, data, meta=None):
    """The noise-window offsets of one stream's bytes -> (set of offsets, per-stream ceiling
    scale, record count).  `meta` is the companion VarMeta bytes for a Var2Data stream."""
    rule = _noise_rule(name)
    offs = set()
    nrec = 0
    if name == CVIEW_VAR2:
        offs.update(font_item_garbage(data, meta))
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
    """The offsets of the REFERENCE where the control and its null save differ, or None when the
    mask is refused.  A mask is built only from equal-length control and null.  When the
    reference is the same length as the control the offsets map one to one.  When it is not,
    only a fixed-stride stream is masked, record by record (a row the control does not have
    is not masked); any other stream gets no mask (W36-0.2 / A-4)."""
    if ctl is None or nul is None or len(ctl) != len(nul):
        return None
    if len(ctl) == ref_len:
        return {i for i in range(ref_len) if ctl[i] != nul[i]}
    stride = _stride(name)
    if stride is None:
        return None
    base, size = stride
    mask = set()
    for i in range(min(base, len(ctl), ref_len)):
        if ctl[i] != nul[i]:
            mask.add(i)
    nrec = (len(ctl) - base) // size
    nref = (ref_len - base) // size
    for r in range(min(nrec, nref)):
        for k in range(size):
            j = base + r * size + k
            if ctl[j] != nul[j]:
                mask.add(j)
    return mask


def compare(traced, ref, ctl, nul):
    """-> (lines, problems, checked, not_checked).  The four dicts are stream name -> bytes;
    `nul` is None when there is no usable control null, which is a FAIL named "no null"."""
    lines = []
    problems = 0
    checked = 0
    unchecked = 0
    names = sorted(set(traced) | set(ref))
    if nul is None:
        return ["no null"], 1, 0, len(names)
    traced, ref, ctl, nul = (canonical(s) for s in (traced, ref, ctl, nul))
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
        if len(a) != len(b):
            lines.append("size-differs %s %d->%d" % (name, len(a), len(b)))
            problems += 1
            continue
        checked += 1
        c, n = ctl.get(name), nul.get(name)
        mask = control_mask(name, len(a), c, n)
        if mask is None and c is not None and n is not None and len(c) == len(n) and len(c) != len(a):
            lines.append("mask-refused %s control %d reference %d" % (name, len(c), len(a)))
        mask = mask or set()
        meta_a = meta_b = None
        if name in VAR_META:
            meta_a, meta_b = ref.get(VAR_META[name]), traced.get(VAR_META[name])
        win, nrec = noise_windows(name, a, meta_a)
        wb, nrb = noise_windows(name, b, meta_b)
        win |= wb
        nrec = max(nrec, nrb)
        diff = [i for i in range(len(a)) if a[i] != b[i] and i not in mask]
        outside = [i for i in diff if i not in win]
        counted = [i for i in diff if i in win]
        ceiling = ceiling_for(name, nrec)
        if name == CVIEW_VAR2:
            ceiling += len(font_item_garbage(a, meta_a))
        if outside:
            lines.append("bytes-differ %s %d" % (name, len(outside)))
            problems += 1
        elif len(counted) > ceiling:
            lines.append("over-ceiling %s %d>%d" % (name, len(counted), ceiling))
            problems += 1
    if checked == 0 and problems == 0:
        lines.append("nothing checked")   # a gate that compared no stream is not a pass (W36-0.4)
        problems += 1
    return lines, problems, checked, unchecked


def main(argv):
    if len(argv) != 5:
        print(__doc__)
        return 2
    if olefile is None:
        print("trace_gate: install olefile (pip install olefile)")
        return 2
    traced, ref, ctl = (streams(p) for p in argv[1:4])
    nul = None
    try:
        nul = streams(argv[4])
    except (OSError, ValueError, AttributeError):
        nul = None   # an absent or unreadable control null
    if nul is not None and nul == ctl:
        nul = None   # the control passed as its own null: no noise was measured
    lines, problems, checked, unchecked = compare(traced, ref, ctl, nul)
    names = set(traced) | set(ref)
    head = "%d streams, %d checked, %d not checked, %d problems" % (len(names), checked, unchecked,
                                                                    problems)
    text = "\n".join([head] + lines) + "\n"
    sys.stdout.write(text)
    print("sha256 " + hashlib.sha256(text.encode("ascii")).hexdigest())
    return 0 if problems == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
