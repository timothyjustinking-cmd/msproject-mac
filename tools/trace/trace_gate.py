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
             from the control's, except in a fixed-stride stream, which is masked row by row
             by the row's identity (its uid word), never by its position: a row inserted before
             another does not move a mask, and a row either side lacks has no mask (W36-0b.8).

A byte of the traced save may differ from the reference only where
  1. the control mask covers it, or
  2. NOISE below names it: a decoded field, never a region, and no limit on how many rows carry
     one; or the stream is parsed and the field is replaced in the canonical form (`canonical`).
Any other differing byte, a size change, or a stream in one file only, is a problem.

The named fields are the whole of the waiver: there is no by-name whole-stream skip.  Each is
decoded from the committed control/null pairs (99 of them) and the trace's null run, and cited by
row of spec/writers/evidence/trace-gate-fields-g0b.md:
  * version-1 GUIDs (the Fixed2Data rows, the view stamps, the Props save GUIDs);
  * the creation date-times of a new Task row or assignment, and the Props save words;
  * the uninitialised top byte of the font word (payload +43 of the 0x2240000c items);
  * the path, user-name and size fields of the path-bearing streams, and the SummaryInformation
    values, replaced by placeholders in the canonical form.

Prints one summary line, then one line per problem stream (no names, no paths, only stream
paths and byte counts), then the SHA-256 of everything printed before it, so a copy read off
the screen can be checked.  Exit 0 means PASS (no problems), 1 FAIL.
"""

import hashlib
import re
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

# Row identity of the stride streams (W36-0b.8): (FixedData stream the identity is read from, or
# None when the stream's own row +0 is the uid; that stream's first row offset and row size;
# header rows; rows per item; uid offset in the item).  Decoded from all 332 committed files:
# Task Fixed2Data = 3 header rows + a row per Task FixedData row; Rsc likewise (row size 172);
# Cal FixedData = 4 header items of 16 bytes + 12-byte items (uid at +8), 3 Fixed2Data rows per
# item after 12 header rows; Cons FixedData = 20-byte items, 3 Fixed2Data rows per item.
IDENT = {
    "???114/TBkndTask/FixedData": (None, 0, 0, 0, 1, 0),
    "???114/TBkndTask/Fixed2Data": ("???114/TBkndTask/FixedData", 48, 202, 3, 1, 0),
    "???114/TBkndAssn/FixedData": (None, 0, 0, 0, 1, 0),
    "???114/TBkndAssn/Fixed2Data": ("???114/TBkndAssn/FixedData", 0, 110, 0, 1, 0),
    "???114/TBkndRsc/Fixed2Data": ("???114/TBkndRsc/FixedData", 48, 172, 3, 1, 0),
    "???114/TBkndCal/Fixed2Data": ("???114/TBkndCal/FixedData", 64, 12, 12, 3, 8),
    "???114/TBkndCons/Fixed2Data": ("???114/TBkndCons/FixedData", 0, 20, 0, 3, 0),
}

# Noise fields.  Each rule names fields; none names a region.  Field classes:
#   guid_record  in-record offsets of 16-byte GUIDs of a stride stream
#   guid         absolute offsets of 16-byte GUIDs
#   props        {record id: class} payload fields of a Props stream: "word" (4 bytes), "guid" (16
#                bytes), "guidstr" (the 32 hex digits of a UTF-16 GUID string)
#   new_row      (lo, hi) in-record bytes of a Task FixedData row whose uid is not in the control
#   varkey       {key: (lo, hi)} bytes of the Var2Data block of that key, for a block whose uid
#                is not in the control's VarMeta
# A GUID is masked only when both sides hold a version-1 GUID (all zeros against a real GUID FAILs: W36-0c.2).  Every row cites its
# line of spec/writers/evidence/trace-gate-fields-g0b.md (FIELDS) for the decoded field.
# There is no ceiling on how many bytes of the named fields may differ: a field is noise or it
# is not, and every row of every stream is judged by the same rule (W36-0b.5).
FIELDS = "spec/writers/evidence/trace-gate-fields-g0b.md"
NOISE = {
    # FIELDS:12  Assn Fixed2Data GUIDs at +0 and +16 (+32 is data)
    "???114/TBkndAssn/Fixed2Data": {"guid_record": (0, 16)},
    # FIELDS:13  Task Fixed2Data GUIDs at +0 and +24
    "???114/TBkndTask/Fixed2Data": {"guid_record": (0, 24)},
    # FIELDS:14  Rsc Fixed2Data GUIDs at +0 and +24
    "???114/TBkndRsc/Fixed2Data": {"guid_record": (0, 24)},
    # FIELDS:15  Cal Fixed2Data GUID at +0
    "???114/TBkndCal/Fixed2Data": {"guid_record": (0,)},
    # FIELDS:16  Cons Fixed2Data GUID at +0
    "???114/TBkndCons/Fixed2Data": {"guid_record": (0,)},
    # FIELDS:22  creation stamp of a new Task row (record +128, 4 bytes)
    "???114/TBkndTask/FixedData": {"new_row": (128, 132)},
    # FIELDS:18-19  the per-save words and GUIDs; 0x02401394 is per-file identity, not noise
    # (FIELDS:20), so it is not listed
    "???114/Props": {"props": {0x02400007: "word", 0x024013B1: "word", 0x02400029: "guid",
                               0x024013C2: "guid"}},
    # FIELDS:21  the view GUID string.  (The typed record 0x00010007, the length of
    # 214/CEdl/Var2Data, is canonicalised with that stream's other size words.)
    "???214/Props": {"props": {0x03400001: "guidstr"}},
    # FIELDS:17  view stamps: version-1 GUIDs saved with the view
    "???214/CEdl/FixedData": {"guid": (144,)},
    "???214/CFilter/FixedData": {"guid": (214, 444)},
    "???214/CGrouping/FixedData": {"guid": (214, 444)},
    "???214/CTable/FixedData": {"guid": (214,)},
    "???214/CV_iew/FixedData": {"guid": (122, 260, 398)},
    # FIELDS:23  the assignment creation stamp is a var block located through VarMeta
    "???114/TBkndAssn/Var2Data": {"varkey": {0x0F40027A: (0, 4)}},
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
    derived), then the tail.  Noise: the file path record, the two blob size words, and the
    user-name text of each tail entry.  The 0xffffffff entry, which the committed controls and
    edits carry and the committed null saves do not, is a named case below."""
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
    # The two committed shapes are a lone 0x3540000b entry (98 of 99 committed null saves) and
    # 0xffffffff then 0x3540000b (the controls and edits).  Both entry ids are handled alike: the
    # 8-byte header stays in the canonical form and only a well-formed text name is replaced.  The
    # 0xffffffff entry may be absent on one side only as the named case: the tail is exactly one
    # 0x3540000b entry with a text payload; its canonical form then carries the 0xffffffff header
    # and the derived count (W36-0c.4 / R36G0b-2).  Junk in the entry, a second one, or any other
    # shape is compared as it is.
    named_absent = len(entries) == 1 and entries[0][0] == 0x3540000B and _text_ok(entries[0][1])
    canon = ([(0xFFFFFFFF, b"")] if named_absent else []) + entries
    out += struct.pack("<I", len(canon))
    if named_absent:
        noise.update(range(end, end + 4))   # the count word is the derived one
    at = end + 4
    for eid, name in entries:
        stop = at + 8 + len(name)
        if _text_ok(name):
            noise.update(range(at + 8, stop))
        at = stop
    if named_absent:
        out += struct.pack("<II", 0xFFFFFFFF, 0x1E)
    for eid, name in entries:
        out += struct.pack("<II", eid, 0x1E) + (b"" if _text_ok(name) else name)
    return bytes(out), noise


def _lpstr_slot_ok(raw):
    """A VT_LPSTR value slot exactly: type 0x1e, the length word, the string, and zero padding to
    a 4-byte boundary, so the slot is 8 + the length rounded up to 4 bytes (W36-0c.3 / R36G0b-3).
    Bytes after the string, nonzero padding and padding of another length are not a string slot."""
    if len(raw) < 8 or _u32(raw, 0) != 0x1E:
        return False
    length = _u32(raw, 4)
    return len(raw) == 8 + ((length + 3) & ~3) and not any(raw[8 + length:])


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
        if pid in (SI_AUTHOR, SI_LASTAUTHOR, SI_REVNUMBER) and _lpstr_slot_ok(raw):
            # a VT_LPSTR: type, length (with its NUL), bytes, zero padding to a 4-byte boundary;
            # only the length word and the string are noise (W36-0c.3)
            raw = struct.pack("<II", 0x1E, 0)
            noise.update(range(lo + 4, lo + 8 + _u32(data, lo + 4)))
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


def _is_v1_guid(raw):
    """A version-1 (time-based) GUID in RFC byte order: byte 6 >> 4 == 1, byte 8 >> 6 == 2."""
    return len(raw) == 16 and raw[6] >> 4 == 1 and raw[8] >> 6 == 2


def _guid_ok(raw):   # W36-0c.2: a GUID field is noise only as a version-1 GUID, never as zeros
    return _is_v1_guid(raw)


GUIDSTR = re.compile(r"\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-"
                     r"[0-9A-Fa-f]{12}\}")


def _guidstr_digits(payload):
    """The payload offsets of the low bytes of the 32 hex digits of a UTF-16 GUID string that
    starts at payload +0, or [] when the payload does not hold one."""
    if len(payload) < 76:
        return []
    try:
        text = payload[:76].decode("utf-16le")
    except UnicodeDecodeError:
        return []
    if not GUIDSTR.fullmatch(text):
        return []
    return [2 * i for i in range(38) if i not in (0, 9, 14, 19, 24, 37)]


def _task_uids(data):
    """The uid word (row +0) of every row of a Task FixedData stream."""
    base, size = STRIDE["???114/TBkndTask/FixedData"]
    if data is None:
        return set()
    return {_u32(data, base + r * size) for r in range((len(data) - base) // size)}


def _meta_blocks(meta):
    """[(uid, offset, key)] of a VarMeta stream."""
    if meta is None or len(meta) < 24:
        return []
    count = min(struct.unpack_from("<I", meta, 8)[0], (len(meta) - 24) // 12)   # a flipped count
    return [struct.unpack_from("<III", meta, 24 + 12 * k) for k in range(count)]   # must not loop 2^32


def noise_windows(name, data, meta=None, ctl=None, other=None):
    """The set of offsets of the named noise fields of one stream's bytes.  `meta` is the
    companion VarMeta of a Var2Data stream, `ctl` the control's streams (a field that only a new
    row carries is masked only for a row the control lacks) and `other` the other side's bytes
    of the same stream (a GUID is masked only when both sides hold a version-1 GUID)."""
    rule = _noise_rule(name)
    offs = set()
    if name == CVIEW_VAR2:
        offs.update(font_item_garbage(data, meta))
    if rule is None:
        return offs
    ctl = ctl or {}
    other = data if other is None else other
    stride = _stride(name)
    if stride:
        base, size = stride
        nrec = max(0, (len(data) - base) // size)
        for r in range(nrec):
            row = base + r * size
            for lo in rule.get("guid_record", ()):
                if _guid_ok(data[row + lo:row + lo + 16]) and _guid_ok(other[row + lo:row + lo + 16]):
                    offs.update(range(row + lo, row + lo + 16))
        if "new_row" in rule:
            known = _task_uids(ctl.get(name))
            lo, hi = rule["new_row"]
            for r in range(nrec):
                row = base + r * size
                if _u32(data, row) not in known and \
                        (row + 4 > len(other) or _u32(other, row) not in known):
                    offs.update(range(row + lo, row + hi))
    for at in rule.get("guid", ()):
        if _guid_ok(data[at:at + 16]) and _guid_ok(other[at:at + 16]):
            offs.update(range(at, at + 16))
    if "props" in rule:
        for pos, length, rid, kind in frame_records(data, 16) or []:
            cls = rule["props"].get(rid)
            at = pos + 12
            if cls == "word" and length == 4:
                offs.update(range(at, at + 4))
            elif cls == "guid" and length == 16 and _guid_ok(data[at:at + 16]) and \
                    _guid_ok(other[at:at + 16]):
                offs.update(range(at, at + 16))
            elif cls == "guidstr":
                digits = _guidstr_digits(data[at:at + length])
                if digits and digits == _guidstr_digits(other[at:at + length]):
                    offs.update(at + d for d in digits)
    if "varkey" in rule:
        known = {uid for uid, _, _ in _meta_blocks(ctl.get(VAR_META[name]))}
        for uid, off, key in _meta_blocks(meta):
            if key in rule["varkey"] and off != 0xFFFFFFFF and uid not in known:
                lo, hi = rule["varkey"][key]
                offs.update(range(off + 4 + lo, min(off + 4 + hi, len(data))))
    return offs


def row_ids(name, data, streams):
    """[identity of each row] of a fixed-stride stream, or None when the rows cannot be named
    exactly.  An identity is the uid word of the row (FixedData) or of the FixedData item the
    row belongs to (Fixed2Data, which has header rows and, for calendars and constraints, three
    rows per item); a header row is named by its place.  A stream whose row count does not fit
    its companion FixedData is not decoded (W36-0b.8 / R36G0-9)."""
    spec = IDENT.get(name)
    if spec is None or data is None:
        return None
    base, size = STRIDE[name]
    if len(data) < base or (len(data) - base) % size:
        return None
    nrows = (len(data) - base) // size
    src, fbase, fsize, hdr, per, uo = spec
    if src is None:
        return [("uid", _u32(data, base + r * size + uo)) for r in range(nrows)]
    fd = (streams or {}).get(src)
    if fd is None or len(fd) < fbase or (len(fd) - fbase) % fsize:
        return None
    if nrows != hdr + per * ((len(fd) - fbase) // fsize):
        return None
    return [("hdr", r) if r < hdr else
            ("uid", _u32(fd, fbase + ((r - hdr) // per) * fsize + uo), (r - hdr) % per)
            for r in range(nrows)]


def _mask_by_id(stride, ref_len, ctl, nul, ctl_ids, ref_ids):
    """The control/null differences, moved to the reference row that carries the same identity.
    A row that either side lacks, or names twice, is not masked."""
    base, size = stride
    mask = {i for i in range(min(base, len(ctl), ref_len)) if ctl[i] != nul[i]}
    where = {}
    for r, ident in enumerate(ref_ids):
        where.setdefault(ident, []).append(r)
    ctl_count = {}
    for ident in ctl_ids:
        ctl_count[ident] = ctl_count.get(ident, 0) + 1
    for r, ident in enumerate(ctl_ids):
        rows = where.get(ident, ())
        if len(rows) != 1 or ctl_count[ident] != 1:
            continue
        for k in range(size):
            if ctl[base + r * size + k] != nul[base + r * size + k]:
                mask.add(base + rows[0] * size + k)
    return mask


def control_mask(name, ref_len, ctl, nul, ids=None):
    """The offsets of the REFERENCE where the control and its null save differ, or None when the
    mask is refused.  A mask is built only from equal-length control and null.  A fixed-stride
    stream whose rows are named on both sides (`ids` = (control row ids, reference row ids), see
    `row_ids`) is masked row by row by identity, so an inserted row does not move a mask onto
    another row (W36-0b.8).  Any other stream is masked one to one when the reference is the
    control's length, and gets no mask when it is not (W36-0.2 / A-4)."""
    if ctl is None or nul is None or len(ctl) != len(nul):
        return None
    stride = _stride(name)
    if stride is not None and ids is not None and ids[0] is not None and ids[1] is not None:
        return _mask_by_id(stride, ref_len, ctl, nul, ids[0], ids[1])
    if len(ctl) == ref_len:
        return {i for i in range(ref_len) if ctl[i] != nul[i]}
    return None


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
        ids = (row_ids(name, c, ctl), row_ids(name, a, ref)) if _stride(name) else None
        mask = control_mask(name, len(a), c, n, ids)
        if mask is None and c is not None and n is not None and len(c) == len(n) and len(c) != len(a):
            lines.append("mask-refused %s control %d reference %d" % (name, len(c), len(a)))
        mask = mask or set()
        meta_a = meta_b = None
        if name in VAR_META:
            meta_a, meta_b = ref.get(VAR_META[name]), traced.get(VAR_META[name])
        win = noise_windows(name, a, meta_a, ctl, b) | noise_windows(name, b, meta_b, ctl, a)
        diff = [i for i in range(len(a)) if a[i] != b[i] and i not in mask]
        outside = [i for i in diff if i not in win]
        if outside:
            lines.append("bytes-differ %s %d" % (name, len(outside)))
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
