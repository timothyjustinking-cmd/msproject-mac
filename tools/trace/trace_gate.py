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
             The bytes where these two differ are the control mask.  It excuses nothing (W36-0c.5):
             a byte it alone would have excused is printed as `control-mask <stream> <n>` and is a
             problem.  It is built from an equal-length control and null only, and a fixed-stride
             stream is masked row by row by the row's identity (its uid word), never by its
             position: a row inserted before another does not move a mask, and a row either side
             lacks has no mask (W36-0b.8).  It tells a byte no field names from a byte that
             is not under the mask either; both FAIL.

A byte of the traced save may differ from the reference only where NOISE below names it: a decoded
field, never a region, and no limit on how many rows carry one; or the stream is parsed and the
field is replaced in the canonical form (`canonical`).  Any other differing byte, a size change,
or a stream in one file only, is a problem.  A field nothing decodes is left unmasked: the pair
FAILs and is OPEN in the test (tools/trace/test_trace_gate.py), recorded as not decoded.

The named fields are the whole of the waiver: there is no by-name whole-stream skip and, since
W36-0c.5, no mask.  A field is waived only as a value a save writes, on both sides (a version-1
GUID, a 32-bit stamp and a LastSave FILETIME each inside the span the committed saves show, the two
Props save words equal, text with no C-category character, a size word that equals what its stream
implies), and a derived field that is present on one side only
FAILs; the one thing this does not stop is noise that is itself a valid value (a name of 33 bytes of
0xff is valid Windows-1252) and the per-save fields nobody decoded, which are OPEN in the test.
Each field is decoded from the committed control/null pairs and the trace's
null run, and cited by row of spec/writers/evidence/trace-gate-fields-g0b.md.  A field the
committed files do not show to be noise is not one, and a pair whose edit or control differs in
a field nobody decoded FAILs there (OPEN in the test, with the stream and offsets):
  * version-1 GUIDs (the Fixed2Data rows, the view stamps, the Props save GUIDs);
  * the creation date-times of a new Task row or assignment, and the Props save words;
  * the uninitialised top byte of the font word (payload +43 of the 0x2240000c items);
  * the path, user-name and size fields of the path-bearing streams, and the SummaryInformation
    values, replaced by placeholders in the canonical form.

Prints one summary line, then one line per problem stream (no names, no paths, only stream
paths and byte counts), then the SHA-256 of everything printed before it, so a copy read off
the screen can be checked.  Exit 0 means PASS (no problems), 1 FAIL.  The summary line counts
the streams, those compared byte by byte (`checked`) and those that could not be (`not
checked`: in one file only, or of another size; each is also a problem line, and with no usable
null every stream is not checked).
"""

import hashlib
import re
import struct
import sys
import unicodedata
import uuid

try:
    import olefile
except ImportError:  # the importer (the ctest) reports SKIP; the command line needs it
    olefile = None


class Streams(dict):
    """One file's streams, stream name -> bytes.  The name every rule of this file uses is the
    printable form of the directory name (a character outside " " < c < "\x7f" is "?"), which is
    lossy, so the raw directory entries are kept beside it (W36-0d.2 / R36G0c-2):
      raw         {name: the raw entry name}, to compare the two sides' raw names
      raw_names   every raw entry name, so the head line counts directory entries
      collisions  the names that two or more entries render alike; the later entries are kept
                  under `name#<n>` so that none is dropped or overwritten"""

    def __init__(self):
        super().__init__()
        self.raw = {}
        self.raw_names = []
        self.collisions = []


def streams(path):
    ole = olefile.OleFileIO(path)
    try:
        out = Streams()
        for entry in ole.listdir(streams=True, storages=False):
            raw = "/".join(entry)
            name = "/".join("".join(ch if " " < ch < "\x7f" else "?" for ch in part)
                            for part in entry)
            out.raw_names.append(raw)
            if name in out:
                if name not in out.collisions:
                    out.collisions.append(name)
                name = "%s#%d" % (name, out.raw_names.index(raw))
            out[name] = ole.openstream(entry).read()
            out.raw[name] = raw
        return out
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
    # FIELDS:12  Assn Fixed2Data GUIDs at +0 and +16 (+32..+37 and +41 are per-save, not decoded)
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
CEDL_SLOTS_ID = 0x26400024       # the task uids in view order, then 0xffffffff fill (capacity)
CEDL_ROWS_ID = 0x2640003D        # view row positions, from the slot table; in one save only at times
CEDL_SLOTS_MAX = 1024            # bytes: the capacity bound; the committed saves reach 144
# The view's date words (4-byte payloads: tenths of a minute from 1 January 1984) and the 8-byte view
# word are session fields (W36-0f.4.1; DOSSIER "CEdl view date word 0x2640002a", "0x2640002b",
# "0x2640002d" and "CEdl view word rec 0x26400026"): a host control and its null differ in them in 7 of
# 223 pairs, and 143 to 144 of the 231 recordings differ from their references in them.  Each is masked
# only as a value a save writes.  A date lies in one span, 220968000 to 226137600 (5 January to 30
# December 2026): the smallest and largest of the 1872 date words of the 624 committed saves, which
# the VM's saves (2d: 221015733 to 225969900) also lie in.  The view word's upper u32 lies in the span
# of its 624 committed values and the VM's (24183812 to 53413760); its lower u32 is 0 in every save and
# stays compared.  A word of another length or outside its span is data and is compared.
CEDL_DATE_IDS = (0x2640002A, 0x2640002B, 0x2640002D)
CEDL_DATE_SPAN = (220968000, 226137600)
CEDL_VIEW_ID = 0x26400026
CEDL_VIEW_SPAN = (24183812, 53413760)
PROPS14_PATH_ID = 0x35400008     # UTF-16 file path
PROPS14_NAME_ENTRIES = (0xFFFFFFFF, 0x3540000B)   # tail entries, each a UTF-16 user name
PROPS14_SAVED_ID = 0x3540000A    # u16 0 or 1, session: how the host file was saved (DOSSIER)
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
# The view stamp (rec 0x22400015, 20 bytes: u32 0, the stamp, 12 constant bytes) and the three date
# words of a view-state record (rec 0x22400008, from +1176, +1182, +1186) are session fields (W36-0f.4.2;
# DOSSIER "CV_iew view stamp word (rec 0x22400015 +4..7)" and "CV_iew view date words (rec 0x22400008
# +1176..1190)"): a host control and its null differ in the stamp in 21 of 202 pairs and in a date word
# in 17 to 21, always one date against another.  A stamp lies in the span of the 627 committed ones (the
# 0x3d1412c0 to 0x3d2812c0 of the saves, widened to the committed top of STAMP_SPAN, 0x3d282db4); a date lies in the CEdl date
# span (the 630 non-default words run 220968000 to 225989400).  A date word of 14400, 14400 and
# 0x340a75ff is the view's unset value, equal in the other 1245 records, and is compared, as is the
# u16 at +1190 (0 or 0x8080).  The u16 words at +1172 and +1180 are not these fields.
CVIEW_STAMP_ID = 0x22400015
CVIEW_STAMP_SPAN = (0x3D1412C0, 0x3D282DB4)
CVIEW_STATE_ID = 0x22400008
CVIEW_STATE_MIN = 1194
CVIEW_DATE_AT = (1176, 1182, 1186)
CVIEW_DATE_SPAN = (220968000, 226137600)


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
    """A decoded path or user name: UTF-16LE text with no character of a Unicode C category
    (controls, C1 included; format; surrogate; private use; unassigned), at most one trailing NUL."""
    if len(payload) > 2048:
        return False
    try:   # an odd length does not decode, so it needs no test of its own (W36-0e.1)
        text = payload.decode("utf-16le")
    except UnicodeDecodeError:
        return False
    if text.endswith("\0"):
        text = text[:-1]
    return all(unicodedata.category(ch)[0] != "C" for ch in text)


def _saved_flag_ok(length, payload):
    """Props14 record 0x3540000a: a u16 of 0 or 1, as every one of the 624 committed saves and the
    VM saves hold (DOSSIER "Props14 rec 0x3540000a"); another length or value is data (W36-0f.1)."""
    return length == 2 and struct.unpack("<H", payload)[0] in (0, 1)


def _name_ok(payload):
    """A Props14 tail user name: text, and not an empty one.  The canonical form replaces a name by
    an empty byte string, so an emptied name must not equal its own placeholder (W36-0e.3 /
    R36G0d-3, as `payload and` does for the path records)."""
    return bool(payload) and _text_ok(payload)


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
    derived from the stream length (+0 = len-4, +4 and +8 = len-8) are noise.  Each replaced field
    is also named in the third result, so a raw word of 0 (or an emptied path record), which the
    placeholder would equal, is told from a derived one (W36-0d.1)."""
    recs = frame_records(data, 20) if len(data) >= 20 else None
    if recs is None:
        return data, set(), set()
    out = bytearray(data[:20])
    noise = set()
    derived = set()
    for at, want in ((0, len(data) - 4), (4, len(data) - 8), (8, len(data) - 8)):
        if _u32(data, at) == want:
            out[at:at + 4] = bytes(4)
            noise.update(range(at, at + 4))
            derived.add("+%d" % at)
    slots = None
    for pos, length, rid, kind in recs:
        if rid == CEDL_SLOTS_ID:
            slots = _slot_uids(data[pos + 12:pos + 12 + length])
    kept = 0
    for pos, length, rid, kind in recs:
        end = pos + 12 + length + (length & 1)
        payload = data[pos + 12:pos + 12 + length]
        if rid == CEDL_PATH_ID and _text_ok(payload) and payload.endswith(b"!\0\0\0"):
            out += struct.pack("<III", 0, rid, kind)
            noise.update(range(pos, pos + 4))
            noise.update(range(pos + 12, end))
            derived.add("path")
        elif rid == CEDL_SLOTS_ID and _slot_uids(payload) is not None:
            uids = _slot_uids(payload)
            out += struct.pack("<III", 4 * len(uids), rid, kind) + payload[:4 * len(uids)]
            noise.update(range(pos, pos + 4))
            noise.update(range(pos + 12 + 4 * len(uids), end))
            derived.add("slots")
        elif rid == CEDL_ROWS_ID and slots is not None and _rows_ok(payload, len(slots)):
            noise.update(range(pos, end))
            continue
        elif rid in CEDL_DATE_IDS and length == 4 and                 CEDL_DATE_SPAN[0] <= _u32(payload, 0) <= CEDL_DATE_SPAN[1]:
            out += struct.pack("<III", length, rid, kind) + bytes(4)
            noise.update(range(pos + 12, pos + 16))
            derived.add("date %#010x" % rid)
        elif rid == CEDL_VIEW_ID and length == 8 and _u32(payload, 0) == 0 and                 CEDL_VIEW_SPAN[0] <= _u32(payload, 4) <= CEDL_VIEW_SPAN[1]:
            out += struct.pack("<III", length, rid, kind) + bytes(8)
            noise.update(range(pos + 16, pos + 20))
            derived.add("view %#010x" % rid)
        else:
            out += data[pos:end]
        kept += 1
    if _u32(data, 16) == len(recs):
        out[16:20] = struct.pack("<I", kept)
        noise.update(range(16, 20))
        derived.add("count")
    return bytes(out), noise, derived


def _slot_uids(payload):
    """The uids of the CEdl slot table (record 0x26400024) as a tuple, or None when it is not
    one.  The table is the task uids in view order and then 0xffffffff words up to an allocation
    capacity: 64 bytes in 280 committed files, 128 in 97 files with the same three uids, 72 in the
    VM's saves (W36-0f.2, DOSSIER "CEdl slot-table capacity").  Only that trailing fill is
    session state; the capacity is bounded at CEDL_SLOTS_MAX bytes (the committed saves reach 144)."""
    if len(payload) % 4 or len(payload) > CEDL_SLOTS_MAX:
        return None
    words = struct.unpack("<%dI" % (len(payload) // 4), payload)
    k = len(words)
    while k and words[k - 1] == 0xFFFFFFFF:
        k -= 1
    return words[:k]


def _rows_ok(payload, n):
    """True when `payload` is the view-row-position record 0x2640003d in the form it has in 459
    of the committed saves, [n][0][22][44]..[22 n][22][u16 0], with n the number of slot-table uids.
    The record is then derived from the slot table, which is compared, so its presence is the
    session field (it is in one save only of 2 of the 223 control/null pairs, DOSSIER "CEdl record
    0x2640003d").  Any other content, or another n, is a difference that stays."""
    want = struct.pack("<II", n, 0) + b"".join(struct.pack("<I", 22 * k) for k in range(1, n + 1)) \
        + struct.pack("<I", 22) + bytes(2)
    return payload == want


def _canon_cedl_meta(data, streams):
    """214/CEdl/VarMeta +20 is the length of 214/CEdl/Var2Data: noise when it says so."""
    var2 = streams.get(CEDL_VAR2)
    if var2 is None or len(data) < 24 or _u32(data, 20) != len(var2):
        return data, set(), set()
    return data[:20] + bytes(4) + data[24:], set(range(20, 24)), {"+20"}


def _canon_view_props(data, streams):
    """214/Props: the typed record 0x00010007 is the length of 214/CEdl/Var2Data (a size word of
    the path-bearing stream); noise only when it equals that length."""
    var2 = streams.get(CEDL_VAR2)
    recs = frame_records(data, 16) if len(data) >= 16 and var2 is not None else None
    if recs is None:
        return data, set(), set()
    out = bytearray(data)
    noise = set()
    derived = set()
    for pos, length, rid, kind in recs:
        if rid == VIEW_CEDL_SIZE_ID and length == 4 and _u32(data, pos + 12) == len(var2):
            out[pos + 12:pos + 16] = bytes(4)
            noise.update(range(pos + 12, pos + 16))
            derived.add("rec %#010x" % rid)
    return bytes(out), noise, derived


# The rich text of a note (Task, Assignment and Resource): a Var2Data block [length][RTF text and NUL]
# located through the VarMeta entry (uid, offset, key) with the key below.  The text is data; two
# numbers of the header are the machine's and not the document's: `\deflang<n>`, the language id
# (1033 in the host saves, 2057 in the VM's) and the build of `{\*\generator Riched20 16.0.<build>}`
# (20430 and 20527 among the committed saves) (W36-0f.3, DOSSIER "rich-text lang id and generator
# build").  Each is replaced by zeros of its own width, so a different width is a size difference.
NOTE_KEYS = {
    "???114/TBkndTask/Var2Data": ("???114/TBkndTask/VarMeta", 0x0B40000F),
    "???114/TBkndAssn/Var2Data": ("???114/TBkndAssn/VarMeta", 0x0F400047),
    "???114/TBkndRsc/Var2Data": ("???114/TBkndRsc/VarMeta", 0x0C400014),
}
_RTF_LANG = re.compile(rb"\\deflang([1-9][0-9]{0,4})(?![0-9])")
_RTF_BUILD = re.compile(rb"\{\\\*\\generator Riched20 16\.0\.([1-9][0-9]{0,4})\}")


def _note_fields(block):
    """[(field, lo, hi)] of the machine numbers in the header of an RTF block: the text before the
    first `\\pard`, so never the note's own text.  The language id is a decimal of 1 to 65535, the
    build one of 1 to 5 digits."""
    if not block.startswith(b"{\\rtf1") or b"\\pard" not in block:
        return []
    head = block[:block.index(b"\\pard")]
    out = []
    m = _RTF_LANG.search(head)
    if m and int(m.group(1)) <= 0xFFFF:
        out.append(("lang", m.start(1), m.end(1)))
    m = _RTF_BUILD.search(head)
    if m:
        out.append(("build", m.start(1), m.end(1)))
    return out


def _canon_note_var2(name):
    meta_name, note_key = NOTE_KEYS[name]

    def canon(data, streams):
        meta = streams.get(meta_name)
        if meta is None or len(meta) < 24:
            return data, set(), set()
        out = bytearray(data)
        noise = set()
        derived = set()
        done = set()
        for k in range(_u32(meta, 8)):
            at = 24 + 12 * k
            if at + 12 > len(meta):
                break
            uid, off, key = struct.unpack_from("<III", meta, at)
            if key != note_key or off == 0xFFFFFFFF or off in done or off + 4 > len(data):
                continue
            done.add(off)
            end = off + 4 + _u32(data, off)
            if end > len(data):
                continue
            for field, lo, hi in _note_fields(data[off + 4:end]):
                out[off + 4 + lo:off + 4 + hi] = b"0" * (hi - lo)
                noise.update(range(off + 4 + lo, off + 4 + hi))
                derived.add("note %d %s" % (uid, field))
        return bytes(out), noise, derived
    return canon


def _tail_entries(tail):
    """The Props14 tail [count][entries] as [(id, name bytes)], or None.  An entry is [id][byte
    length][UTF-16 name]: the second word is the name's length, 30 for the 15-character name of the
    host files, 10 for the VM's, 16 for an 8-character one (W36-0f.1, DOSSIER "size-differs
    Props14").  The entries chain by their length words exactly to the end of the tail, each id is
    one of the two names, and the count word is their number."""
    if len(tail) < 4:
        return None
    count = _u32(tail, 0)
    entries = []
    at = 4
    while at < len(tail):
        if at + 8 > len(tail):
            return None
        eid, size = struct.unpack_from("<II", tail, at)
        if eid not in PROPS14_NAME_ENTRIES or at + 8 + size > len(tail):
            return None
        entries.append((eid, tail[at + 8:at + 8 + size]))
        at += 8 + size
    return entries if len(entries) == count else None


def _canon_props14(data, streams):
    """Props14: a blob [size][size][0x10000][count] of Props-format records (size = blob end - 4,
    derived), then the tail.  Noise: the file path record, the two blob size words, and the
    user-name text of each tail entry.  The 0xffffffff entry, which the committed controls and
    edits carry and the committed null saves do not, is a named case below."""
    if len(data) < 16 or _u32(data, 8) != 0x10000:
        return data, set(), set()
    end = _u32(data, 0) + 4
    if _u32(data, 4) != end - 4 or end > len(data):
        return data, set(), set()
    recs = frame_records(data[:end], 16)
    entries = _tail_entries(data[end:])
    if recs is None or entries is None:
        return data, set(), set()
    out = bytearray(data[:16])
    out[0:8] = bytes(8)
    noise = set(range(0, 8))
    derived = set()
    for pos, length, rid, kind in recs:
        stop = pos + 12 + length + (length & 1)
        payload = data[pos + 12:pos + 12 + length]
        if rid == PROPS14_PATH_ID and payload and _text_ok(payload):
            out += struct.pack("<III", 0, rid, kind)
            noise.update(range(pos, pos + 4))
            noise.update(range(pos + 12, stop))
            derived.add("path")
        elif rid == PROPS14_SAVED_ID and _saved_flag_ok(length, payload):
            out += struct.pack("<III", length, rid, kind) + bytes(2)
            noise.update(range(pos + 12, pos + 14))
            derived.add("rec %#010x" % rid)
        else:
            out += data[pos:stop]
    # The record 0x3540000a is a session field and may be absent on one side (DOSSIER "Props14
    # rec 0x3540000a": the committed control and null differ in 9 of 223 pairs, and the one file
    # with no such record holds 20 records); an absent one is a placeholder record, and the record
    # count word, which counts the records, says 21 for it (W36-0f.1).
    if all(rid != PROPS14_SAVED_ID for pos, length, rid, kind in recs) and _u32(data, 12) == len(recs):
        out[12:16] = struct.pack("<I", len(recs) + 1)
        out += struct.pack("<III", 2, PROPS14_SAVED_ID, 2) + bytes(2)
        derived.add("rec %#010x" % PROPS14_SAVED_ID)
    # The two committed shapes are a lone 0x3540000b entry (98 of 99 committed null saves) and
    # 0xffffffff then 0x3540000b (the controls and edits).  Both entry ids are handled alike: the
    # 8-byte header stays in the canonical form and only a well-formed text name is replaced.  The
    # 0xffffffff entry may be absent on one side only as the named case: the tail is exactly one
    # 0x3540000b entry with a text payload; its canonical form then carries the 0xffffffff header
    # and the derived count (W36-0c.4 / R36G0b-2).  Junk in the entry, a second one, or any other
    # shape is compared as it is.
    named_absent = len(entries) == 1 and entries[0][0] == 0x3540000B and _name_ok(entries[0][1])
    canon = ([(0xFFFFFFFF, b"")] if named_absent else []) + entries
    out += struct.pack("<I", len(canon))
    if named_absent:
        noise.update(range(end, end + 4))   # the count word is the derived one
        derived.add("tail name %#010x" % 0xFFFFFFFF)   # the absent entry's name is the placeholder
    # An entry is [id][byte length][name]; a text name and its length word are one machine field
    # (W36-0f.1), replaced together by a length of 0 and no name.  The chain by length words, which
    # _tail_entries checked, is what a wrong length word fails.
    at = end + 4
    for eid, name in entries:
        stop = at + 8 + len(name)
        if _name_ok(name):
            noise.update(range(at + 4, stop))
            derived.add("tail name %#010x" % eid)
        at = stop
    if named_absent:
        out += struct.pack("<II", 0xFFFFFFFF, 0)
    for eid, name in entries:
        out += struct.pack("<II", eid, 0) if _name_ok(name) else struct.pack("<II", eid, len(name)) + name
    return bytes(out), noise, derived


# The range of each masked value is the span the committed saves show (W36-0e.2 / R36G0d-2), each a
# row of "Value bounds" in spec/writers/evidence/trace-gate-fields-g0b.md.  A value outside its span
# is data, not noise.  The stamp grows with time, so a capture made after the last committed save
# is outside its span until the span is widened with that evidence row (the re-gate will show it).
# 5835 stamps of 624 files (Task +128, Assn block, Props words) run 0x3d141482 to 0x3d282db4; the top is
# the VM's: its three latest saves wrote 0x3d283502, 0x3d283516 and 0x3d28352a in the Props baseline-slot
# words (DOSSIER, "Props stamp word 0x2401396", "0x240139f" and "0x2401397"), saved after the newest
# committed file, so a save word of those saves is one too (W36-0f.4.3).
STAMP_SPAN = (0x3D141482, 0x3D28352A)
LASTSAVE_SPAN = (0x01DD62019F6A0E00, 0x01DD72130AAC6C00)   # 624 LastSave FILETIMEs, 22 Oct to 11 Nov 2026


def _stamp_ok(raw):
    """A 32-bit creation stamp or save word as saved: inside the committed span (a stamp of 1, of
    0x7fffffff, of 0 or all-ones is not one a save writes; W36-0e.2)."""
    return len(raw) == 4 and STAMP_SPAN[0] <= struct.unpack("<I", raw)[0] <= STAMP_SPAN[1]


def _filetime_ok(raw):
    """A LastSave FILETIME: inside the committed span (a year 2001 or 2600 is not one a save
    writes; W36-0e.2)."""
    return len(raw) == 8 and LASTSAVE_SPAN[0] <= struct.unpack("<Q", raw)[0] <= LASTSAVE_SPAN[1]


def _lpstr_text_ok(raw):
    """The string of a VT_LPSTR slot: at least its NUL, ending in a NUL, a Windows-1252 text with
    no control character before the trailing NULs (W36-0d.6)."""
    length = _u32(raw, 4)
    body = raw[8:8 + length]
    if length < 1 or body[-1:] != b"\0":
        return False
    try:
        text = body.decode("cp1252")
    except UnicodeDecodeError:
        return False
    return all(ch >= " " and ch != "\x7f" for ch in text.rstrip("\0"))


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
        return data, set(), set()
    sec = _u32(data, 44)
    if sec + 8 > len(data) or _u32(data, sec) != len(data) - sec:
        return data, set(), set()
    count = _u32(data, sec + 4)
    table = sec + 8
    if table + 8 * count > len(data):
        return data, set(), set()
    ents = [(_u32(data, table + 8 * i), _u32(data, table + 8 * i + 4)) for i in range(count)]
    order = sorted(range(count), key=lambda i: ents[i][1])
    if count == 0 or ents[order[0]][1] != 8 + 8 * count:
        return data, set(), set()
    values = {}
    for k, i in enumerate(order):
        lo = sec + ents[i][1]
        hi = sec + ents[order[k + 1]][1] if k + 1 < count else len(data)
        if hi < lo:
            return data, set(), set()
        values[i] = (lo, hi)
    noise = set()
    derived = set()
    canon = {}
    for i, (pid, _) in enumerate(ents):
        lo, hi = values[i]
        raw = data[lo:hi]
        if pid in (SI_AUTHOR, SI_LASTAUTHOR, SI_REVNUMBER) and _lpstr_slot_ok(raw) and \
                _lpstr_text_ok(raw):
            # a VT_LPSTR: type, length (with its NUL), bytes, zero padding to a 4-byte boundary;
            # only the length word and the string are noise (W36-0c.3), and only a text (W36-0d.6)
            raw = struct.pack("<II", 0x1E, 0)
            noise.update(range(lo + 4, lo + 8 + _u32(data, lo + 4)))
            derived.add("pid %d" % pid)
        elif pid == SI_LASTSAVE and len(raw) == 12 and _u32(raw, 0) == 0x40 and \
                _filetime_ok(raw[4:]):
            raw = struct.pack("<I", 0x40) + bytes(8)
            noise.update(range(lo + 4, hi))
            derived.add("pid %d" % pid)
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
    return bytes(out), noise, derived


def _canon_cview_var2(data, streams):
    """CV_iew/Var2Data: the view stamp of a view-stamp record and the three date words of a view-state
    record are session fields, each replaced in place (the length does not change, so the font-row
    windows still find their offsets) and each named in the third result with its block's uid, only in
    a bag block whose records chain exactly to the block's end and only as a value a save writes
    (W36-0f.4.2)."""
    out = bytearray(data)
    noise = set()
    derived = set()
    meta = streams.get(VAR_META[CVIEW_VAR2])
    for uid, off, key in _meta_blocks(meta):
        if key != BAG_KEY or off == 0xFFFFFFFF or off + 4 > len(data):
            continue
        end = off + 4 + _u32(data, off)
        recs = frame_records(data[:end], off + 20) if end <= len(data) else None
        for pos, length, rid, kind in recs or []:
            at = pos + 12
            if rid == CVIEW_STAMP_ID and kind == 1 and length == 20 and _u32(data, at) == 0 and \
                    CVIEW_STAMP_SPAN[0] <= _u32(data, at + 4) <= CVIEW_STAMP_SPAN[1]:
                out[at + 4:at + 8] = bytes(4)
                noise.update(range(at + 4, at + 8))
                derived.add("view stamp uid %d" % uid)
            elif rid == CVIEW_STATE_ID and kind == 1 and length >= CVIEW_STATE_MIN:
                for lo in CVIEW_DATE_AT:
                    if CVIEW_DATE_SPAN[0] <= _u32(data, at + lo) <= CVIEW_DATE_SPAN[1]:
                        out[at + lo:at + lo + 4] = bytes(4)
                        noise.update(range(at + lo, at + lo + 4))
                        derived.add("view date uid %d +%d" % (uid, lo))
    return bytes(out), noise, derived


CANON = {
    CVIEW_VAR2: _canon_cview_var2,
    CEDL_VAR2: _canon_cedl_var2,
    CEDL_META: _canon_cedl_meta,
    VIEW_PROPS: _canon_view_props,
    PROPS14: _canon_props14,
    SUMMARY: _canon_summary,
}
for _name in NOTE_KEYS:
    CANON[_name] = _canon_note_var2(_name)


def canonical_all(streams):
    """-> (canonical streams, noise offsets, derived fields), each a dict by stream name.  The
    CANON streams have their decoded noise fields replaced by fixed placeholders; `noise` holds the
    offsets of the raw bytes that were replaced and `derived` the name of each replaced field.  A
    placeholder can equal a raw field (a size word of 0, an emptied path record), so two canonical
    forms are equal only when the same fields were replaced on both sides (W36-0d.1)."""
    out = {}
    noise = {}
    derived = {}
    for name, data in streams.items():
        if name in CANON:
            out[name], noise[name], derived[name] = CANON[name](data, streams)
        else:
            out[name], noise[name], derived[name] = data, set(), set()
    return out, noise, derived


def canonical(streams, with_noise=False):
    """One file's streams with the decoded noise fields of the CANON streams replaced by fixed
    placeholders.  `with_noise` also returns, per stream, the offsets of the raw bytes that were
    replaced (the named noise fields)."""
    out, noise, _ = canonical_all(streams)
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
    """A version-1 (time-based) GUID in RFC byte order: byte 6 >> 4 == 1, byte 8 >> 6 == 2, and a
    timestamp that is not 0 (time_low, time_mid and the low 12 bits of time_hi; W36-0d.3)."""
    return (len(raw) == 16 and raw[6] >> 4 == 1 and raw[8] >> 6 == 2 and
            (any(raw[:6]) or raw[6] & 15 != 0 or raw[7] != 0))


def _guid_ok(raw):   # W36-0c.2: a GUID field is noise only as a version-1 GUID, never as zeros
    return _is_v1_guid(raw)


GUIDSTR = re.compile(r"\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-"
                     r"[0-9A-Fa-f]{12}\}")


def _guidstr_bytes(payload):
    """The 16 bytes, in the file's own order, of the UTF-16 GUID string that starts at payload +0
    (the string is the little-endian rendering of those bytes), or None when the payload does not
    hold one (W36-0d.3 / R36G0c-3)."""
    if len(payload) < 76:
        return None
    try:
        text = payload[:76].decode("utf-16le")
    except UnicodeDecodeError:
        return None
    if not GUIDSTR.fullmatch(text):
        return None
    try:
        return uuid.UUID(text).bytes_le
    except ValueError:   # not reached after the shape test; a damaged string never raises (W36-0e.1)
        return None


def _guidstr_digits(payload):
    """The payload offsets of the low bytes of the 32 hex digits of a UTF-16 GUID string that
    starts at payload +0, or [] when the payload does not hold a string that is a version-1 GUID
    with a timestamp that is not 0 (all-zero digits, or a changed version digit, is data)."""
    raw = _guidstr_bytes(payload)
    if raw is None or not _guid_ok(raw):
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


SAVE_WORD_IDS = (0x02400007, 0x024013B1)   # the two 114/Props save words


def _save_words_equal(blob):
    """The two 114/Props save words of one file are one value: they are equal in all 624 committed
    files (spec/writers/evidence/trace-gate-fields-g0b.md, Value bounds), so a file whose two words
    differ has a word that no save wrote, and neither is noise (W36-0e.2 / R36G0d-2)."""
    words = {rid: blob[pos + 12:pos + 16] for pos, length, rid, kind in frame_records(blob, 16) or []
             if rid in SAVE_WORD_IDS and length == 4}
    return len(set(words.values())) <= 1


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
                        (row + 4 > len(other) or _u32(other, row) not in known) and \
                        _stamp_ok(data[row + lo:row + hi]) and _stamp_ok(other[row + lo:row + hi]):
                    offs.update(range(row + lo, row + hi))
    for at in rule.get("guid", ()):
        if _guid_ok(data[at:at + 16]) and _guid_ok(other[at:at + 16]):
            offs.update(range(at, at + 16))
    if "props" in rule:
        words_one_value = _save_words_equal(data) and _save_words_equal(other)
        for pos, length, rid, kind in frame_records(data, 16) or []:
            cls = rule["props"].get(rid)
            at = pos + 12
            if cls == "word" and length == 4 and words_one_value and _stamp_ok(data[at:at + 4]) and \
                    _stamp_ok(other[at:at + 4]):
                offs.update(range(at, at + 4))
            elif cls == "guid" and length == 16 and _guid_ok(data[at:at + 16]) and \
                    _guid_ok(other[at:at + 16]):
                offs.update(range(at, at + 16))
            elif cls == "guidstr":
                digits = _guidstr_digits(data[at:at + length])
                if digits == _guidstr_digits(other[at:at + length]):   # [] == [] masks nothing
                    offs.update(at + d for d in digits)
    if "varkey" in rule:
        known = {uid for uid, _, _ in _meta_blocks(ctl.get(VAR_META[name]))}
        for uid, off, key in _meta_blocks(meta):
            if key in rule["varkey"] and off != 0xFFFFFFFF and uid not in known:
                lo, hi = rule["varkey"][key]
                at = off + 4 + lo
                if _stamp_ok(data[at:off + 4 + hi]) and _stamp_ok(other[at:off + 4 + hi]):
                    offs.update(range(at, min(off + 4 + hi, len(data))))
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
    `nul` is None when there is no usable control null, which is a FAIL named "no null".
    `checked` counts the streams compared byte by byte; `not_checked` those that could not be
    (a stream in one file only, or of another size), each also a problem line, and every stream
    when there is no null (W36-0c.8 / R36G0b-10)."""
    lines = []
    problems = 0
    checked = 0
    unchecked = 0
    names = sorted(set(traced) | set(ref))
    if nul is None:
        return ["no null"], 1, 0, len(names)
    # two directory entries that render the same name are a collision, whichever side holds them
    # (W36-0d.2); a stream whose raw name differs between the two sides is not the same stream
    for key in sorted(set(getattr(traced, "collisions", ())) | set(getattr(ref, "collisions", ()))):
        lines.append("stream name collision %s" % key)
        problems += 1
    raw_t, raw_r = getattr(traced, "raw", {}), getattr(ref, "raw", {})
    (traced, _, d_traced), (ref, _, d_ref) = canonical_all(traced), canonical_all(ref)
    ctl, nul = canonical(ctl), canonical(nul)
    for name in names:
        if name not in ref:
            lines.append("only-in-traced %s %d" % (name, len(traced[name])))
            problems += 1
            unchecked += 1
            continue
        if name not in traced:
            lines.append("only-in-reference %s %d" % (name, len(ref[name])))
            problems += 1
            unchecked += 1
            continue
        if name in raw_t and name in raw_r and raw_t[name] != raw_r[name]:
            lines.append("stream name differs %s" % name)
            problems += 1
            unchecked += 1
            continue
        a, b = ref[name], traced[name]
        if len(a) != len(b):
            lines.append("size-differs %s %d->%d" % (name, len(a), len(b)))
            problems += 1
            unchecked += 1
            continue
        checked += 1
        # a field replaced by its placeholder on one side only is not the placeholder's value:
        # a raw 0 is not a derived 0 (W36-0d.1 / R36G0c-1)
        for field in sorted(d_ref.get(name, set()) ^ d_traced.get(name, set())):
            lines.append("derived %s %s" % (name, field))
            problems += 1
        c, n = ctl.get(name), nul.get(name)
        ids = (row_ids(name, c, ctl), row_ids(name, a, ref)) if _stride(name) else None
        mask = control_mask(name, len(a), c, n, ids) or set()
        meta_a = meta_b = None
        if name in VAR_META:
            meta_a, meta_b = ref.get(VAR_META[name]), traced.get(VAR_META[name])
        win = noise_windows(name, a, meta_a, ctl, b) | noise_windows(name, b, meta_b, ctl, a)
        outside = [i for i in range(len(a)) if a[i] != b[i] and i not in win]
        # The control mask excuses nothing (W36-0c.5).  A byte it alone would have excused is a
        # difference no field names; it is counted apart, so the line says which kind it is.
        masked = [i for i in outside if i in mask]
        free = [i for i in outside if i not in mask]
        if free:
            lines.append("bytes-differ %s %d" % (name, len(free)))
            problems += 1
        if masked:
            lines.append("control-mask %s %d" % (name, len(masked)))
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
    if nul == ctl:
        nul = None   # the control passed as its own null: no noise was measured
    lines, problems, checked, unchecked = compare(traced, ref, ctl, nul)
    # the count is of raw directory entries, not of printable names (W36-0d.2)
    entries = set(traced.raw_names) | set(ref.raw_names)
    head = "%d streams, %d checked, %d not checked, %d problems" % (len(entries), checked,
                                                                    unchecked, problems)
    text = "\n".join([head] + lines) + "\n"
    sys.stdout.write(text)
    print("sha256 " + hashlib.sha256(text.encode("ascii")).hexdigest())
    return 0 if problems == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
