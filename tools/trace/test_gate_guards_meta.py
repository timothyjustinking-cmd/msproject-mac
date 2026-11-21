#!/usr/bin/env python3
"""Meta-test of the trace gate's guards (W36-0d.8, R36G0c-4 and -5).

A guard is a clause of tools/trace/trace_gate.py that the gate FAILs a wrong file by: remove it and
the wrong file passes.  A clause no case reaches can be removed and the test stays green, which is how
R36G0c-4 and -5 found the type check of a string slot, the Props14 size-word cross-check, the first
tail entry and the 2048-byte cap.  This script plants each removal in GUARDS, one at a time, in a
temporary copy of the gate, runs tools/trace/test_trace_gate.py against it (`--gate-dir`) and asserts
the test goes red.  A guard whose removal leaves the test green is a case that is missing.

    python test_gate_guards_meta.py [--jobs N] [--skip-baseline] [--list] [ID ...]

It prints one line per guard (`red` with the first failing case, `red (crash)` when the mutant made
the test raise, or `GREEN`), then `guards listed N, red M` (and the baseline).  Exit 0 only when every
listed guard is red, every text to remove was found in the gate exactly as many times as stated, and
the unmodified gate passes (`--skip-baseline` leaves that to a caller that has run
test_trace_gate.py itself).  Exit 1 otherwise; 77 when olefile is absent.

W36-2.4: this is its own ctest and no longer runs at the end of test_trace_gate.py, so
`ctest -R trace_gate_guards` runs the acceptance sweeps only.  W36b-0.6: it is two ctests, one per
shard, `trace_gate_meta_named` (`--only named`) and `trace_gate_meta_derived` (`--only derived`), each
with its own TIMEOUT and both labelled `trace_gate_meta`.  Run both with `ctest -L trace_gate_meta` on
a fresh tree at each group close; the CI jobs are `trace-gate-meta-named` and `trace-gate-meta-derived`.

W36-0e.1 (R36G0d-1): the named list above can only hold the guards someone thought of, so the
candidates are also DERIVED from the gate with `ast`: the condition of an `if` that returns,
continues, breaks or counts a problem is replaced by False (`if-never`), any other by True
(`if-always`); every operand of an
`or` by False (`or-false`) and of an `and` by True (`and-true`); every conditional expression's test
by True or False (`ifexp-true`, `ifexp-false`).  The key of a candidate is `function:kind:condition`.
Each is run as a named guard is.  A candidate that stays GREEN must be on REDUNDANT, as
`key: (cover, reason)`, where the cover is a guard or candidate that is itself red and whose removal
leaves the same files passing; a stale entry (not derived, red itself, or a cover that is not red)
is an error.  A CRASH (the test died with no FAIL line) is an error: a removal is red by a named FAIL.
`--only named` or `--only derived` runs one of the two lists; the end-of-run check of the REDUNDANT
list judges an entry or cover by its result only where it ran, so each shard checks its own.
"""

import ast
import concurrent.futures
import itertools
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
GATE = os.path.join(HERE, "trace_gate.py")
TEST = os.path.join(HERE, "test_trace_gate.py")

try:
    import olefile  # noqa: F401
except ImportError:
    # W36b-0.5 (R36W-5): in CI (MPP_REQUIRE_OLEFILE set) a missing olefile is a failure, not a skip
    if os.environ.get("MPP_REQUIRE_OLEFILE", "") not in ("", "0"):
        print("FAIL: olefile is not installed and MPP_REQUIRE_OLEFILE is set")
        sys.exit(1)
    print("SKIP: olefile is not installed (pip install olefile)")
    sys.exit(77)

# (id, the finding it answers or the item that added it, what is removed, [(old text, new text[, count])])
GUARDS = [
    # --- the review's table of planted removals (G0c review, "Mutations") ----------------------------
    ("guid-zero", "R36G0b-4", "_guid_ok accepts an all-zero GUID",
     [("    return _is_v1_guid(raw)\n", "    return _is_v1_guid(raw) or raw == bytes(16)\n")]),
    ("guid-record-other", "R36G0b-4", "a GUID record is masked without the other side's GUID",
     [("if _guid_ok(data[row + lo:row + lo + 16]) and _guid_ok(other[row + lo:row + lo + 16]):",
       "if _guid_ok(data[row + lo:row + lo + 16]):")]),
    ("guid-absolute-other", "R36G0b-4", "an absolute GUID is masked without the other side's GUID",
     [("        if _guid_ok(data[at:at + 16]) and _guid_ok(other[at:at + 16]):\n            offs.update",
       "        if _guid_ok(data[at:at + 16]):\n            offs.update")]),
    ("guid-props-other", "R36G0b-4", "a Props GUID record is masked without the other side's GUID",
     [(r'''            elif cls == "guid" and length == 16 and _guid_ok(data[at:at + 16]) and \
                    _guid_ok(other[at:at + 16]):''',
       '            elif cls == "guid" and length == 16 and _guid_ok(data[at:at + 16]):')]),
    ("slot-length", "R36G0b-3", "_lpstr_slot_ok: the slot length is not checked",
     [("    return len(raw) == 8 + ((length + 3) & ~3) and not any(raw[8 + length:])",
       "    return not any(raw[8 + length:])")]),
    ("slot-padding", "R36G0b-3", "_lpstr_slot_ok: the padding is not checked",
     [("    return len(raw) == 8 + ((length + 3) & ~3) and not any(raw[8 + length:])",
       "    return len(raw) == 8 + ((length + 3) & ~3)")]),
    ("slot-type", "R36G0c-4", "_lpstr_slot_ok: the type word is not checked",
     [("    if len(raw) < 8 or _u32(raw, 0) != 0x1E:\n        return False",
       "    if len(raw) < 8:\n        return False")]),
    ("slot-call-site", "R36G0b-3", "an Author, LastAuthor or RevNumber slot is any slot of 8 bytes",
     [(r'''and _lpstr_slot_ok(raw) and \
                _lpstr_text_ok(raw):''', "and len(raw) >= 8:")]),
    ("named-absent-header", "R36G0b-2", "named_absent: the 0xffffffff header is not written",
     [('    if named_absent:\n        out += struct.pack("<II", 0xFFFFFFFF, 0)\n', "")]),
    ("named-absent-count", "R36G0b-2", "the Props14 count word is the raw count",
     [('    out += struct.pack("<I", len(canon))', '    out += struct.pack("<I", len(entries))')]),
    ("tail-ffff-dropped", "R36G0b-2", "the 0xffffffff tail name is dropped whatever its bytes",
     [('out += struct.pack("<II", eid, 0) if _name_ok(name) else',
       'out += struct.pack("<II", eid, 0) if (_name_ok(name) or eid == 0xFFFFFFFF) else')]),
    ("tail-names-waived", "R36G0b-2", "every Props14 tail name is waived",
     [("        if _name_ok(name):\n            noise.update(range(at + 4, stop))",
       "        if True:\n            noise.update(range(at + 4, stop))"),
      ('out += struct.pack("<II", eid, 0) if _name_ok(name) else struct.pack("<II", eid, len(name)) + name',
       'out += struct.pack("<II", eid, 0)')]),
    ("tail-entries-dropped", "R36G0b-2", "every Props14 tail entry is dropped",
     [('    canon = ([(0xFFFFFFFF, b"")] if named_absent else []) + entries', "    canon = []"),
      ('    for eid, name in entries:\n        out += struct.pack("<II", eid, 0) if _name_ok(name) else struct.pack("<II", eid, len(name)) + name',
       "    for eid, name in []:\n        pass")]),
    ("control-mask-no-problem", "R36G0b-1", "a control-mask line is not a problem",
     [('            lines.append("control-mask %s %d" % (name, len(masked)))\n            problems += 1',
       '            lines.append("control-mask %s %d" % (name, len(masked)))')]),
    ("control-mask-excuses", "R36G0b-1", "the control mask excuses a byte again",
     [("        masked = [i for i in outside if i in mask]\n        free = [i for i in outside if i not in mask]",
       "        masked = []\n        free = [i for i in outside if i not in mask]")]),
    ("bytes-differ-no-problem", "R36G0b-1", "a bytes-differ line is not a problem",
     [('            lines.append("bytes-differ %s %d" % (name, len(free)))\n            problems += 1',
       '            lines.append("bytes-differ %s %d" % (name, len(free)))')]),
    ("not-checked-uncounted", "R36G0b-10", "`not checked` is never counted",
     [("            unchecked += 1\n", "            unchecked += 0\n", 4)]),
    ("no-null-count", "R36G0b-10", "with no null `not checked` is 0",
     [('        return ["no null"], 1, 0, len(names)', '        return ["no null"], 1, 0, 0')]),
    ("nothing-checked", "W36-0.4", "a gate that checked nothing passes",
     [("    if checked == 0 and problems == 0:", "    if False:")]),
    ("control-as-null", "W36-0.3", "the control passed as its own null is accepted",
     [("    if nul == ctl:", "    if False:")]),
    ("cedl-meta-length", "R36G0b-6", "CEdl VarMeta +20 is zeroed unconditionally",
     [("    if var2 is None or len(data) < 24 or _u32(data, 20) != len(var2):",
       "    if var2 is None or len(data) < 24:")]),
    ("guidstr-shape", "R36G0b-6", "the view GUID string's shape is not tested",
     [("    if not GUIDSTR.fullmatch(text):\n        return None\n", "")]),
    ("guidstr-other", "R36G0b-6", "the view GUID string is masked when only one side holds one",
     [("                if digits == _guidstr_digits(other[at:at + length]):",
       "                if digits:")]),
    ("si-placeholders", "R36G0b-6", "every SummaryInformation property is replaced by a placeholder",
     [("        elif pid == SI_LASTSAVE and len(raw) == 12 and _u32(raw, 0) == 0x40 and \\\n                _filetime_ok(raw[4:]):",
       "        elif len(raw) >= 8 and pid != 0:")]),
    ("si-lastsave-any", "R36G0b-6", "any 12-byte FILETIME is LastSave",
     [("        elif pid == SI_LASTSAVE and len(raw) == 12 and _u32(raw, 0) == 0x40 and \\\n                _filetime_ok(raw[4:]):",
       "        elif len(raw) == 12 and _u32(raw, 0) == 0x40 and _filetime_ok(raw[4:]):")]),
    ("text-ok-true", "R36G0b-6", "_text_ok is always true",
     [("    if len(payload) > 2048:\n        return False\n    try:",
       "    return True\n    try:")]),
    ("cedl-path-waived", "R36G0b-6", "the CEdl path record is waived whatever its bytes",
     [('        if rid == CEDL_PATH_ID and _text_ok(payload) and payload.endswith(b"!\\0\\0\\0"):',
       "        if rid == CEDL_PATH_ID:")]),
    ("cedl-size-words", "R36G0b-6", "the CEdl size words are zeroed unconditionally",
     [("        if _u32(data, at) == want:\n            out[at:at + 4] = bytes(4)",
       "        if True:\n            out[at:at + 4] = bytes(4)")]),
    ("view-props-size", "R36G0b-6", "214/Props record 0x00010007 is zeroed unconditionally",
     [("        if rid == VIEW_CEDL_SIZE_ID and length == 4 and _u32(data, pos + 12) == len(var2):",
       "        if rid == VIEW_CEDL_SIZE_ID and length == 4:")]),
    ("props14-path-waived", "R36G0b-6", "the Props14 path record is waived whatever its bytes",
     [("        if rid == PROPS14_PATH_ID and payload and _text_ok(payload):",
       "        if rid == PROPS14_PATH_ID:")]),
    ("font-byte-any-record", "R36G0b-6", "the font byte is taken from any record of 44-byte rows",
     [("            if rid == FONT_ITEM_ID and kind == 1 and length % FONT_ROW == 0:",
       "            if length % FONT_ROW == 0:")]),
    # --- R36G0c-4 and -5: the guards the review found no case for --------------------------------------
    # (W36-0f.1: `_tail_entries` reads by the length words from +4, so "the first entry need not
    # start at +4" is no clause any more; the case "a tail whose first entry is not at +4" stays and
    # is now answered by tail-entry-id and tail-chain-end.)
    # --- W36-0e.1: the four guards the G0d review removed with the test still green ------------------------
    ("tail-count", "R36G0d-1", "_tail_entries: the count word need not equal the entries found",
     [("    return entries if len(entries) == count else None", "    return entries")]),
    # --- W36-0f.1: the Props14 tail entry is [id][byte length][name] ---------------------------------------
    ("tail-chain-end", "W36-0f.1", "_tail_entries: a length word may run past the end of the tail",
     [("        if eid not in PROPS14_NAME_ENTRIES or at + 8 + size > len(tail):",
       "        if eid not in PROPS14_NAME_ENTRIES:")]),
    ("tail-entry-id", "W36-0f.1", "_tail_entries: an entry may have any id",
     [("        if eid not in PROPS14_NAME_ENTRIES or at + 8 + size > len(tail):",
       "        if at + 8 + size > len(tail):")]),
    ("saved-flag-value", "W36-0f.1", "the Props14 record 0x3540000a is masked whatever u16 it holds",
     [('    return length == 2 and struct.unpack("<H", payload)[0] in (0, 1)', "    return length == 2")]),
    ("saved-flag-length", "W36-0f.1", "the Props14 record 0x3540000a is masked whatever its length",
     [('    return length == 2 and struct.unpack("<H", payload)[0] in (0, 1)',
       '    return struct.unpack("<H", payload[:2])[0] in (0, 1)')]),
    ("slots-bound", "W36-0f.2", "the CEdl slot table is a table at any capacity",
     [("    if len(payload) % 4 or len(payload) > CEDL_SLOTS_MAX:", "    if len(payload) % 4:")]),
    ("slots-whole-words", "W36-0f.2", "the CEdl slot table is a table at any length",
     [("    if len(payload) % 4 or len(payload) > CEDL_SLOTS_MAX:", "    if len(payload) > CEDL_SLOTS_MAX:")]),
    ("slots-uids-ignored", "W36-0f.2", "the CEdl slot table's uids are taken as fill",
     [("    k = len(words)\n    while k and words[k - 1] == 0xFFFFFFFF:", "    k = 0\n    while k and words[k - 1] == 0xFFFFFFFF:")]),
    ("slots-length-word", "W36-0f.2", "the CEdl slot table keeps its raw length word",
     [('            out += struct.pack("<III", 4 * len(uids), rid, kind) + payload[:4 * len(uids)]',
       '            out += struct.pack("<III", len(payload), rid, kind) + payload[:4 * len(uids)]')]),
    ("slots-fill-kept", "W36-0f.2", "the CEdl slot table keeps its fill",
     [('            out += struct.pack("<III", 4 * len(uids), rid, kind) + payload[:4 * len(uids)]',
       '            out += struct.pack("<III", 4 * len(uids), rid, kind) + payload')]),
    ("rows-shape", "W36-0f.2", "the CEdl row record is dropped whatever it holds",
     [("    return payload == want", "    return True")]),
    ("rows-count", "W36-0f.2", "the CEdl row record's n is its own and need not be the slot table's uid count",
     [("_rows_ok(payload, len(slots))", "_rows_ok(payload, _u32(payload, 0))")]),
    ("rows-needs-table", "W36-0f.2", "the CEdl row record is dropped with no slot table to check it against",
     [("elif rid == CEDL_ROWS_ID and kind == CEDL_ROWS_KIND and slots is not None and \\\n                _rows_ok(payload, len(slots)):",
       "elif rid == CEDL_ROWS_ID and kind == CEDL_ROWS_KIND and \\\n                _rows_ok(payload, len(slots)):")]),
    ("rows-kind", "R36G0f-3", "the CEdl row record is dropped whatever its kind word",
     [("elif rid == CEDL_ROWS_ID and kind == CEDL_ROWS_KIND and slots is not None and",
       "elif rid == CEDL_ROWS_ID and slots is not None and")]),
    ("rows-kind-value", "R36G0f-3", "the CEdl row record's kind word is not the one the committed saves carry",
     [("CEDL_ROWS_KIND = 1 ", "CEDL_ROWS_KIND = 0 ")]),
    ("cedl-count-any", "W36-0f.2", "the CEdl record count word is replaced whatever it says",
     [("    if _u32(data, 16) == len(recs):\n        out[16:20] = struct.pack(\"<I\", kept)",
       "    if True:\n        out[16:20] = struct.pack(\"<I\", kept)")]),
    ("cedl-count-raw", "W36-0f.2", "the CEdl record count word keeps the number of records, not the number kept",
     [('        out[16:20] = struct.pack("<I", kept)', '        out[16:20] = struct.pack("<I", len(recs))')]),
    ("cedl-date-lo", "W36-0f.4.1", "a CEdl date word is masked below the committed span",
     [("CEDL_DATE_SPAN[0] <= _u32(payload, 0) <= CEDL_DATE_SPAN[1]:", "_u32(payload, 0) <= CEDL_DATE_SPAN[1]:")]),
    ("cedl-date-hi", "W36-0f.4.1", "a CEdl date word is masked above the committed span",
     [("CEDL_DATE_SPAN[0] <= _u32(payload, 0) <= CEDL_DATE_SPAN[1]:", "CEDL_DATE_SPAN[0] <= _u32(payload, 0):")]),
    ("cedl-date-length", "W36-0f.4.1", "a CEdl date word is masked at any payload length",
     [("elif rid in CEDL_DATE_IDS and length == 4 and", "elif rid in CEDL_DATE_IDS and")]),
    ("cedl-date-ids", "W36-0f.4.1", "another CEdl record is masked as a date word",
     [("elif rid in CEDL_DATE_IDS and length == 4 and", "elif rid in CEDL_DATE_IDS + (0x26400028,) and length == 4 and")]),
    ("cedl-view-low", "W36-0f.4.1", "the view word is masked whatever its lower u32",
     [("elif rid == CEDL_VIEW_ID and length == 8 and _u32(payload, 0) == 0 and", "elif rid == CEDL_VIEW_ID and length == 8 and")]),
    ("cedl-view-lo", "W36-0f.4.1", "the view word is masked below the committed span",
     [("CEDL_VIEW_SPAN[0] <= _u32(payload, 4) <= CEDL_VIEW_SPAN[1]:", "_u32(payload, 4) <= CEDL_VIEW_SPAN[1]:")]),
    ("cedl-view-hi", "W36-0f.4.1", "the view word is masked above the committed span",
     [("CEDL_VIEW_SPAN[0] <= _u32(payload, 4) <= CEDL_VIEW_SPAN[1]:", "CEDL_VIEW_SPAN[0] <= _u32(payload, 4):")]),
    ("cedl-view-length", "W36-0f.4.1", "the view word is masked at any payload length",
     [("elif rid == CEDL_VIEW_ID and length == 8 and", "elif rid == CEDL_VIEW_ID and")]),
    ("cview-canon-absent", "W36-0f.4.2", "the CV_iew stream is not canonicalised",
     [("    CVIEW_VAR2: _canon_cview_var2,\n", "")]),
    ("cview-key", "W36-0f.4.2", "a CV_iew block of any key is read as a bag block",
     [("        if key != BAG_KEY or off == 0xFFFFFFFF or off + 4 > len(data):\n            continue\n        end = off + 4 + _u32(data, off)\n",
       "        if off == 0xFFFFFFFF or off + 4 > len(data):\n            continue\n        end = off + 4 + _u32(data, off)\n")]),
    ("cview-end-bound", "W36-0f.4.2", "a bag block whose length word runs past the stream is read to the stream's end",
     [("yield uid, (frame_records(data[:end], off + 20) if end <= len(data) else None)",
       "yield uid, frame_records(data[:end], off + 20)")]),
    ("cview-chain", "W36-0f.4.2", "a bag block that does not chain exactly is read up to its last whole record",
     [("yield uid, (frame_records(data[:end], off + 20) if end <= len(data) else None)",
       "yield uid, ((frame_records(data[:end], off + 20) or frame_records(data[:end - 5], off + 20)) if end <= len(data) else None)")]),
    ("cview-stamp-lo", "W36-0f.4.2", "the view stamp is masked below the committed span",
     [("CVIEW_STAMP_SPAN[0] <= _u32(data, at + 4) <= CVIEW_STAMP_SPAN[1]:", "_u32(data, at + 4) <= CVIEW_STAMP_SPAN[1]:")]),
    ("cview-stamp-hi", "W36-0f.4.2", "the view stamp is masked above the committed span",
     [("CVIEW_STAMP_SPAN[0] <= _u32(data, at + 4) <= CVIEW_STAMP_SPAN[1]:", "CVIEW_STAMP_SPAN[0] <= _u32(data, at + 4):")]),
    ("cview-stamp-length", "W36-0f.4.2", "the view stamp record is masked at any length",
     [("and kind == 1 and length == 20 and _u32(data, at) == 0 and", "and kind == 1 and _u32(data, at) == 0 and")]),
    ("cview-stamp-kind", "W36-0f.4.2", "the view stamp record is masked at any kind",
     [("if rid == CVIEW_STAMP_ID and kind == 1 and", "if rid == CVIEW_STAMP_ID and")]),
    ("cview-stamp-lead", "W36-0f.4.2", "the view stamp is masked whatever the word before it",
     [("length == 20 and _u32(data, at) == 0 and \\\n", "length == 20 and \\\n")]),
    ("cview-stamp-id", "W36-0f.4.2", "another CV_iew record is masked as the view stamp record",
     [("if rid == CVIEW_STAMP_ID and", "if rid in (CVIEW_STAMP_ID, 0x22400016) and")]),
    ("cview-state-id", "W36-0f.4.2", "another CV_iew record is masked as the view-state record",
     [("return rid == CVIEW_STATE_ID and kind == 1", "return rid in (CVIEW_STATE_ID, 0x22400009) and kind == 1")]),
    ("cview-state-kind", "W36-0f.4.2", "the view-state record is masked at any kind",
     [("return rid == CVIEW_STATE_ID and kind == 1 and", "return rid == CVIEW_STATE_ID and")]),
    ("cview-state-min", "W36-0f.4.2", "the view-state record is masked at 1190 bytes and over",
     [("CVIEW_STATE_MIN = 1194", "CVIEW_STATE_MIN = 1190")]),
    ("cview-date-lo", "W36-0f.4.2", "a view date word is masked below the committed span",
     [("if (CVIEW_DATE_SPAN[0] <= _u32(data, at + lo) <= CVIEW_DATE_SPAN[1]\n", "if (_u32(data, at + lo) <= CVIEW_DATE_SPAN[1]\n")]),
    ("cview-date-hi", "W36-0f.4.2", "a view date word is masked above the committed span",
     [("if (CVIEW_DATE_SPAN[0] <= _u32(data, at + lo) <= CVIEW_DATE_SPAN[1]\n", "if (CVIEW_DATE_SPAN[0] <= _u32(data, at + lo)\n")]),
    ("cview-date-pair", "W36b-0.2", "a view date word is masked in every pair, not only where the control and null differ in it",
     [("and _pair_date_differs(pair, uid, states - 1, length, which)):", "and True):")]),
    ("cview-date-pair-index", "W36b-0.2", "the pair is read at the first view-state record of the block whatever the record's place",
     [("_pair_date_differs(pair, uid, states - 1, length, which)", "_pair_date_differs(pair, uid, 0, length, which)")]),
    ("cview-date-pair-word", "W36b-0.2", "the pair's first date word excuses every date word of the record",
     [("_pair_date_differs(pair, uid, states - 1, length, which)", "_pair_date_differs(pair, uid, states - 1, length, 0)")]),
    ("cview-date-pair-length", "W36b-0.2", "the pair's record may be of another length than the record masked",
     [("    if ctl[index][0] != length or nul[index][0] != length:\n        return False\n", "")]),
    ("cview-date-pair-equal", "W36b-0.2", "a date word the control and null hold equal is masked as a difference",
     [("    return a != b and all(", "    return all(")]),
    ("cview-date-pair-span", "W36b-0.2", "a date word the control or null holds outside the span is masked as a difference",
     [("    return a != b and all(CVIEW_DATE_SPAN[0] <= w <= CVIEW_DATE_SPAN[1] for w in (a, b))", "    return a != b")]),
    ("cview-date-pair-none", "W36b-0.2", "a gate called with no pair masks every in-span view date word",
     [("    if pair is None:\n        return False\n    ctl, nul = (_state_dates(side, uid) for side in pair)", "    if pair is None:\n        return True\n    ctl, nul = (_state_dates(side, uid) for side in pair)")]),
    ("cview-date-pair-uid", "W36b-0.2", "the pair's view-state records are read from every block, not the block masked",
     [("        if u != uid:\n            continue\n", "        if False:\n            continue\n")]),
    ("cview-date-pair-passed", "W36b-0.2", "compare passes no pair to canonical_all",
     [("    (traced_c, _, d_traced), (ref_c, _, d_ref) = (canonical_all(traced, ref, pair),\n                                                  canonical_all(ref, traced, pair))",
       "    (traced_c, _, d_traced), (ref_c, _, d_ref) = (canonical_all(traced, ref), canonical_all(ref, traced))")]),
    ("cview-date-at", "W36-0f.4.2", "a fourth word of the view-state record is masked as a date",
     [("CVIEW_DATE_AT = (1176, 1182, 1186)", "CVIEW_DATE_AT = (1100, 1176, 1182, 1186)")]),
    ("cview-stamp-kept", "W36-0f.4.2", "the view stamp keeps its raw bytes in the canonical form",
     [("out[at + 4:at + 8] = bytes(4)", "out[at + 4:at + 8] = data[at + 4:at + 8]")]),
    ("cview-date-kept", "W36-0f.4.2", "a view date word keeps its raw bytes in the canonical form",
     [("out[at + lo:at + lo + 4] = bytes(4)", "out[at + lo:at + lo + 4] = data[at + lo:at + lo + 4]")]),
    ("cview-stamp-unnamed", "W36-0f.4.2", "the view stamp is not named as a derived field",
     [('derived.add("view stamp uid %d" % uid)', "pass")]),
    ("cview-date-unnamed", "W36-0f.4.2", "a view date word is not named as a derived field",
     [('derived.add("view date uid %d +%d" % (uid, lo))', "pass")]),
    ("cview-stamp-uid", "W36-0f.4.2", "the view stamp's derived name does not say which block holds it",
     [('derived.add("view stamp uid %d" % uid)', 'derived.add("view stamp")')]),
    ("cview-date-uid", "W36-0f.4.2", "a view date word's derived name does not say which block holds it",
     [('derived.add("view date uid %d +%d" % (uid, lo))', 'derived.add("view date +%d" % lo)')]),
    ("note-lang-bound", "W36-0f.3", "a note language id is masked at any value",
     [("    if m and int(m.group(1)) <= 0xFFFF:", "    if m:")]),
    ("note-lang-lead", "W36-0f.3", "a note language id may start with a zero",
     [(r'_RTF_LANG = re.compile(rb"\\deflang([1-9][0-9]{0,4})(?![0-9])")',
       r'_RTF_LANG = re.compile(rb"\\deflang([0-9]{1,5})(?![0-9])")')]),
    ("note-build-lead", "W36-0f.3", "a note generator build may start with a zero",
     [(r'Riched20 16\.0\.([1-9][0-9]{0,4})\}")', r'Riched20 16\.0\.([0-9]{1,5})\}")')]),
    ("note-build-width", "W36-0f.3", "a note generator build may have six digits",
     [(r'Riched20 16\.0\.([1-9][0-9]{0,4})\}")', r'Riched20 16\.0\.([1-9][0-9]{0,5})\}")')]),
    ("note-build-version", "W36-0f.3", "the note generator's version is masked whatever its major and minor",
     [(r'Riched20 16\.0\.([1-9]', r'Riched20 [0-9]+\.[0-9]+\.([1-9]')]),
    ("note-head-only", "W36-0f.3", "the machine numbers are read in the whole note block, not its header",
     [(r'    head = block[:block.index(b"\\pard")]', "    head = block")]),
    ("note-rtf-start", "W36-0f.3", "the note block need not be RTF",
     [(r'    if not block.startswith(b"{\\rtf1") or b"\\pard" not in block:',
       r'    if b"\\pard" not in block:')]),
    ("note-paragraph", "W36-0f.3", "the note block need not hold a paragraph to end its header",
     [(r'    if not block.startswith(b"{\\rtf1") or b"\\pard" not in block:',
       r'    if not block.startswith(b"{\\rtf1"):')]),
    ("note-key", "W36-0f.3", "every Var2Data block is read as a note, whatever its key",
     [("            if key != note_key or off == 0xFFFFFFFF", "            if off == 0xFFFFFFFF")]),
    ("saved-flag-absent-count", "W36-0f.1","anabsent record 0x3540000a is added whatever the record count word says",
     [("    if all(rid != PROPS14_SAVED_ID for pos, length, rid, kind in recs) and _u32(data, 12) == len(recs):",
       "    if all(rid != PROPS14_SAVED_ID for pos, length, rid, kind in recs):")]),
    ("saved-flag-absent-present", "W36-0f.1", "a placeholder 0x3540000a is added to a blob that has the record",
     [("    if all(rid != PROPS14_SAVED_ID for pos, length, rid, kind in recs) and _u32(data, 12) == len(recs):",
       "    if _u32(data, 12) == len(recs):")]),
    ("si-section-size", "R36G0d-1", "_canon_summary: the section size word is not checked",
     [("    if sec + 8 > len(data) or _u32(data, sec) != len(data) - sec:", "    if sec + 8 > len(data):")]),
    ("si-first-value", "R36G0d-1", "_canon_summary: the first value need not follow the offset table",
     [("    if count == 0 or ents[order[0]][1] != 8 + 8 * count:", "    if count == 0:")]),
    ("si-lastsave-type", "R36G0d-1", "_canon_summary: the LastSave type word is not checked",
     [("        elif pid == SI_LASTSAVE and len(raw) == 12 and _u32(raw, 0) == 0x40 and \\\n                _filetime_ok(raw[4:]):",
       "        elif pid == SI_LASTSAVE and len(raw) == 12 and _filetime_ok(raw[4:]):")]),
    ("props14-size-words", "R36G0c-5", "the Props14 blob's two size words need not agree",
     [("    if _u32(data, 4) != end - 4 or end > len(data):", "    if end > len(data):")]),
    ("text-cap", "R36G0c-5", "_text_ok has no 2048-byte cap",
     [("    if len(payload) > 2048:", "    if False:")]),
    # --- W36-0d.1: a derived field is told from a raw one -------------------------------------------
    ("derived-compare", "R36G0c-1", "a field derived on one side only is not a problem",
     [("        for field in sorted(d_ref.get(name, set()) ^ d_traced.get(name, set())):",
       "        for field in []:")]),
    ("derived-cedl-words", "R36G0c-1", "the CEdl size words are not named as derived",
     [('            derived.add("+%d" % at)', "            pass")]),
    ("derived-cedl-path", "R36G0c-1", "the CEdl path record is not named as derived",
     [('            derived.add("path")\n        elif rid == CEDL_SLOTS_ID',
       "            pass\n        elif rid == CEDL_SLOTS_ID")]),
    ("derived-cedl-meta", "R36G0c-1", "CEdl VarMeta +20 is not named as derived",
     [('set(range(20, 24)), {"+20"}', "set(range(20, 24)), set()")]),
    ("derived-view-size", "R36G0c-1", "214/Props 0x00010007 is not named as derived",
     [('            noise.update(range(pos + 12, pos + 16))\n            derived.add("rec %#010x" % rid)\n'
       '    return bytes(out), noise, derived',
       '            noise.update(range(pos + 12, pos + 16))\n            pass\n    return bytes(out), noise, derived')]),
    ("derived-summary", "R36G0c-6", "the SummaryInformation slots are not named as derived",
     [('            derived.add("pid %d" % pid)\n', "            pass\n", 2)]),
    ("props14-empty-path", "R36G0c-1", "an empty Props14 path record is replaced by its placeholder",
     [("        if rid == PROPS14_PATH_ID and payload and _text_ok(payload):",
       "        if rid == PROPS14_PATH_ID and _text_ok(payload):")]),
    # --- W36-0d.2: streams are keyed by their raw name ----------------------------------------------
    ("stream-collision", "R36G0c-2", "a collision of printable names is not a problem",
     [('    for key in sorted(set(getattr(traced, "collisions", ())) | set(getattr(ref, "collisions", ()))):',
       "    for key in []:")]),
    ("stream-raw-name", "R36G0c-2", "a stream whose raw name differs is the same stream",
     [("        if name in raw_t and name in raw_r and raw_t[name] != raw_r[name]:", "        if False:")]),
    ("stream-count", "R36G0c-2", "the head line counts printable names, not entries",
     [("    entries = set(traced.raw_names) | set(ref.raw_names)", "    entries = set(traced) | set(ref)")]),
    # --- W36-0d.3: the view GUID string is a version-1 GUID -----------------------------------------
    ("guidstr-v1", "R36G0c-3", "the view GUID string is masked whatever GUID it holds",
     [("    if raw is None or not _guid_ok(raw):\n        return []", "    if raw is None:\n        return []")]),
    ("guid-version", "R36G0c-3", "_is_v1_guid: the version is not checked",
     [("raw[6] >> 4 == 1 and raw[8] >> 6 == 2 and\n", "raw[8] >> 6 == 2 and\n")]),
    ("guid-variant", "R36G0c-3", "_is_v1_guid: the variant is not checked",
     [("raw[6] >> 4 == 1 and raw[8] >> 6 == 2 and\n", "raw[6] >> 4 == 1 and\n")]),
    ("guid-timestamp", "R36G0c-3", "_is_v1_guid: the timestamp may be 0",
     [("(any(raw[:6]) or raw[6] & 15 != 0 or raw[7] != 0))", "True)")]),
    # --- W36-0d.6: a masked field holds a value a save writes ----------------------------------------
    ("stamp-span-vm", "W36-2.0", "the stamp span stops at the top of the committed saves, under the stamps of the 14 and 15 November saves",
     [("STAMP_SPAN = (0x3D141482, 0x3D2C15EA)", "STAMP_SPAN = (0x3D141482, 0x3D282DB4)")]),
    ("pslot-id-1395", "W36-0f.4.4", "baseline slot 0x02401395 is not read as a slot",
     [('0x02401395: "slot"', '0x02401395: "none"')]),
    ("pslot-id-1396", "W36-0f.4.4", "baseline slot 0x02401396 is not read as a slot",
     [('0x02401396: "slot"', '0x02401396: "none"')]),
    ("pslot-id-1397", "W36-0f.4.4", "baseline slot 0x02401397 is not read as a slot",
     [('0x02401397: "slot"', '0x02401397: "none"')]),
    ("pslot-id-139f", "W36-0f.4.4", "baseline slot 0x0240139f is not read as a slot",
     [('0x0240139F: "slot"', '0x0240139F: "none"')]),
    ("props-guid-13bd", "W36-0f.4.4", "the third Props GUID 0x024013bd is not read as a GUID",
     [('0x024013C2: "guid", 0x024013BD: "guid",', '0x024013C2: "guid", 0x024013BD: "none",')]),
    ("pslot-length", "W36-0f.4.4", "a slot record of any length is masked",
     [('elif cls == "slot" and length == 4 and _slot_word_ok(data, at)', 'elif cls == "slot" and _slot_word_ok(data, at)')]),
    ("pslot-traced-side", "W36-0f.4.4", "a slot is masked whatever the traced side holds",
     [('elif cls == "slot" and length == 4 and _slot_word_ok(data, at) and \\\n                    _slot_word_ok(other, at):',
       'elif cls == "slot" and length == 4 and _slot_word_ok(other, at):')]),
    ("pslot-other-side", "W36-0f.4.4", "a slot is masked whatever the other side holds",
     [('elif cls == "slot" and length == 4 and _slot_word_ok(data, at) and \\\n                    _slot_word_ok(other, at):',
       'elif cls == "slot" and length == 4 and _slot_word_ok(data, at):')]),
    ("pslot-both-words", "W36-0f.4.4", "a slot equal to one save word only is masked",
     [("set(words.values()) == {word}", "word in words.values()")]),
    ("pslot-two-words", "W36-0f.4.4", "a slot is masked when the file has no two save words",
     [("return len(words) == 2 and set(words.values()) == {word}", "return set(words.values()) <= {word}")]),
    ("pslot-kept", "W36-0f.4.4", "a slot is not replaced in the noise window",
     [('_slot_word_ok(other, at):\n                offs.update(range(at, at + 4))', "_slot_word_ok(other, at):\n                pass")]),
    ("rows-ctable", "W36-0f.4.5", "the GUID of a table row after the first is not read",
     [('"guid": (214,), "guid_rows": (214, 230)}', '"guid": (214,)}')]),
    ("rows-cview", "W36-0f.4.5", "the GUID of a view row after the third is not read",
     [('"guid": (122, 260, 398), "guid_rows": (122, 138)}', '"guid": (122, 260, 398)}')]),
    ("rows-whole", "W36-0f.4.5", "the row GUIDs of a stream that is not whole rows are read",
     [("        if len(data) % size == 0:   # whole rows", "        if True:   # whole rows")]),
    ("rows-traced-v1", "W36-0f.4.5", "a row GUID is masked whatever the traced side holds",
     [("            for at in range(first, len(data) - 15, size):\n                if _guid_ok(data[at:at + 16]) and _guid_ok(other[at:at + 16]):",
       "            for at in range(first, len(data) - 15, size):\n                if _guid_ok(other[at:at + 16]):")]),
    ("rows-other-v1", "W36-0f.4.5", "a row GUID is masked whatever the other side holds",
     [("            for at in range(first, len(data) - 15, size):\n                if _guid_ok(data[at:at + 16]) and _guid_ok(other[at:at + 16]):",
       "            for at in range(first, len(data) - 15, size):\n                if _guid_ok(data[at:at + 16]):")]),
    ("assn-guid3", "W36-0f.4.6", "the third GUID of an assignment's Fixed2Data row is not read",
     [('"???114/TBkndAssn/Fixed2Data": {"guid_record": (0, 16, 32)}',
       '"???114/TBkndAssn/Fixed2Data": {"guid_record": (0, 16)}')]),
    ("assn-guid3-off", "W36-0f.4.6", "the third GUID of an assignment's row is read one byte late",
     [('"???114/TBkndAssn/Fixed2Data": {"guid_record": (0, 16, 32)}',
       '"???114/TBkndAssn/Fixed2Data": {"guid_record": (0, 16, 33)}')]),
    ("assn-guid2-off", "W36-0f.4.6", "the second GUID of an assignment's row is read one byte early",
     [('"???114/TBkndAssn/Fixed2Data": {"guid_record": (0, 16, 32)}',
       '"???114/TBkndAssn/Fixed2Data": {"guid_record": (0, 15, 32)}')]),
    ("stamp-lower", "R36G0d-2", "_stamp_ok has no lower bound (0, 1)",
     [('STAMP_SPAN[0] <= struct.unpack("<I", raw)[0] <= STAMP_SPAN[1]',
       'struct.unpack("<I", raw)[0] <= STAMP_SPAN[1]')]),
    ("stamp-upper", "R36G0d-2", "_stamp_ok has no upper bound (0x7fffffff, all-ones)",
     [('STAMP_SPAN[0] <= struct.unpack("<I", raw)[0] <= STAMP_SPAN[1]',
       'STAMP_SPAN[0] <= struct.unpack("<I", raw)[0]')]),
    ("save-words-equal", "R36G0d-2", "the two 114/Props save words need not be equal",
     [("        words_one_value = _save_words_equal(data) and _save_words_equal(other)",
       "        words_one_value = True")]),
    ("stamp-new-row", "R36G0c-6", "a Task row's stamp is masked whatever its value",
     [("                        _stamp_ok(data[row + lo:row + hi]) and _stamp_ok(other[row + lo:row + hi]):",
       "                        True:")]),
    ("stamp-row-other", "W36-0f.4.7", "a Task row's stamp is masked whatever the other side holds",
     [("                        _stamp_ok(data[row + lo:row + hi]) and _stamp_ok(other[row + lo:row + hi]):",
       "                        _stamp_ok(data[row + lo:row + hi]):")]),
    ("stamp-row-traced", "W36-0f.4.7", "a Task row's stamp is masked whatever this side holds",
     [("                        _stamp_ok(data[row + lo:row + hi]) and _stamp_ok(other[row + lo:row + hi]):",
       "                        _stamp_ok(other[row + lo:row + hi]):")]),
    ("stamp-row-known", "R36G0f-2", "the stamp of a row the control has is masked",
     [("                if _u32(data, row) not in known and \\\n", "                if True and \\\n")]),
    ("stamp-row-known-other", "R36G0f-2", "a stamp is masked though the other side's row is one the control has",
     [("                        (row + 4 > len(other) or _u32(other, row) not in known) and \\\n", "")]),
    ("stamp-row-known-ctl", "R36G0f-2", "the control's Task rows are not read: every row is new",
     [("            known = _task_uids(ctl.get(name))\n", "            known = set()\n")]),
    ("varkey-known", "R36G0f-2", "the stamp of an assignment or resource block the control has is masked",
     [('            if key in rule["varkey"] and off != 0xFFFFFFFF and uid not in known:',
       '            if key in rule["varkey"] and off != 0xFFFFFFFF:')]),
    ("varkey-known-ctl", "R36G0f-2", "the control's VarMeta is not read: every block is new",
     [('        known = {uid for uid, _, _ in _meta_blocks(ctl.get(VAR_META[name]))}\n', "        known = set()\n")]),
    ("rsc-stamp-rule", "W36-0f.4.7", "a resource's creation stamp block is not read",
     [('"???114/TBkndRsc/Var2Data": {"varkey": {0x0C4002D6: (0, 4)}, "stale"', '"???114/TBkndRsc/Var2Data": {"stale"')]),
    ("rsc-stamp-meta", "W36-0f.4.7", "a resource's Var2Data is read without its VarMeta",
     [('    "???114/TBkndRsc/Var2Data": "???114/TBkndRsc/VarMeta",\n', "")]),
    ("rsc-stamp-key", "W36-0f.4.7", "a resource's stamp block is read under the assignment's key",
     [('{"varkey": {0x0C4002D6: (0, 4)}, "stale"', '{"varkey": {0x0F40027A: (0, 4)}, "stale"')]),
    ("task-stamp-rule", "W36-0f.4.7", "a Task row's creation stamp is not read",
     [('{"row_stamp": (128, 132)}', '{}')]),
    ("task-stamp-off", "W36-0f.4.7", "a Task row's creation stamp is read one byte late",
     [('{"row_stamp": (128, 132)}', '{"row_stamp": (129, 133)}')]),
    # --- W36-0f.4.8: the stale memory of a baseline sub-record --------------------------------------
    ("stale-task-rule", "W36-0f.4.8", "a Task's baseline block is not read for stale words",
     [('    "???114/TBkndTask/Var2Data": {"stale": {0x0B4000AE: (True, 58, 60)}},\n', "")]),
    ("stale-task-meta", "W36-0f.4.8", "a Task's Var2Data is read without its VarMeta",
     [('    "???114/TBkndTask/Var2Data": "???114/TBkndTask/VarMeta",\n', "")]),
    ("stale-task-key", "W36-0f.4.8", "the Task stale block is read under another key",
     [("{0x0B4000AE: (True, 58, 60)}", "{0x0B40015E: (True, 58, 60)}")]),
    ("stale-task-hi", "W36-0f.4.8", "the Task heap bytes reach one byte further",
     [("{0x0B4000AE: (True, 58, 60)}", "{0x0B4000AE: (True, 58, 61)}")]),
    ("stale-task-lo", "W36-0f.4.8", "the Task heap bytes start one byte early",
     [("{0x0B4000AE: (True, 58, 60)}", "{0x0B4000AE: (True, 57, 60)}")]),
    ("stale-task-addr", "W36-0f.4.8", "the Task stale words need no image-base high half",
     [("{0x0B4000AE: (True, 58, 60)}", "{0x0B4000AE: (False, 58, 60)}")]),
    ("stale-assn-rule", "W36-0f.4.8", "an assignment's baseline blocks are not read for stale words",
     [(',\n                                  "stale": {0x0F400035: (True, 58, 62), 0x0F400124: (True, 58, 62),\n'
       '                                            0x0F40012D: (True, 58, 62), 0x0F400175: (True, 58, 62)}}',
       "}")]),
    ("stale-assn-key-35", "W36-0f.4.8", "the assignment block of key 0xf400035 is not read",
     [("0x0F400035: (True, 58, 62), ", "")]),
    ("stale-assn-key-124", "W36-0f.4.8", "the assignment block of key 0xf400124 is not read",
     [("0x0F400124: (True, 58, 62),", "")]),
    ("stale-assn-key-12d", "W36-0f.4.8", "the assignment block of key 0xf40012d is not read",
     [("0x0F40012D: (True, 58, 62), ", "")]),
    ("stale-assn-key-175", "W36-0f.4.8", "the assignment block of key 0xf400175 is not read",
     [("0x0F400175: (True, 58, 62)}", "}")]),
    ("stale-assn-hi", "W36-0f.4.8", "the assignment heap bytes reach one byte further",
     [("0x0F400035: (True, 58, 62)", "0x0F400035: (True, 58, 63)")]),
    ("stale-assn-addr", "W36-0f.4.8", "the assignment stale words need no image-base high half",
     [("0x0F400035: (True, 58, 62)", "0x0F400035: (False, 58, 62)")]),
    ("stale-rsc-rule", "W36-0f.4.8", "a resource's baseline block is not read for stale words",
     [(', "stale": {0x0C400045: (False, 0, 0)}}', "}")]),
    ("stale-rsc-key", "W36-0f.4.8", "the resource stale block is read under another key",
     [("{0x0C400045: (False, 0, 0)}", "{0x0C40015F: (False, 0, 0)}")]),
    ("stale-rsc-addr", "W36-0f.4.8", "the resource stale words need an image-base high half",
     [("{0x0C400045: (False, 0, 0)}", "{0x0C400045: (True, 0, 0)}")]),
    ("stale-rsc-hi", "W36-0f.4.8", "the resource block masks bytes +0 to +4 as heap bytes",
     [("{0x0C400045: (False, 0, 0)}", "{0x0C400045: (False, 0, 4)}")]),
    ("stale-high-half", "W36-0f.4.8", "the image base's high half is any value",
     [("STALE_BASE_HIGH = (0x7FF6, 0x7FF7)", "STALE_BASE_HIGH = tuple(range(65536))")]),
    ("stale-high-7ff7", "W36-0f.4.8", "the image base's high half 0x7ff7 is not taken",
     [("STALE_BASE_HIGH = (0x7FF6, 0x7FF7)", "STALE_BASE_HIGH = (0x7FF6,)")]),
    ("stale-high-7ff6", "W36-0f.4.8", "the image base's high half 0x7ff6 is not taken",
     [("STALE_BASE_HIGH = (0x7FF6, 0x7FF7)", "STALE_BASE_HIGH = (0x7FF7,)")]),
    ("stale-pair-equal", "W36-0f.4.8", "the +18 and +38 words need not be one value",
     [(" and block[18:22] == block[38:42] and \\\n", " and \\\n")]),
    ("stale-length-76", "W36-0f.4.8", "the block need not hold a length word of 76",
     [(" and _u32(blob, off) == 76 and ", " and ")]),
    ("stale-length-span", "W36-0f.4.8", "the block need not hold 76 bytes after its length word",
     [("    return len(block) == 76 and ", "    return ")]),
    ("stale-other", "W36-0f.4.8", "the stale words are masked without the other side's block",
     [(" and \\\n                    _stale_ok(other, off, spec[0]):", ":")]),
    ("stale-this", "W36-0f.4.8", "the stale words are masked without this side's block",
     [("off != 0xFFFFFFFF and _stale_ok(data, off, spec[0]) and \\\n                    _stale_ok(other",
       "off != 0xFFFFFFFF and \\\n                    _stale_ok(other")]),
    ("stale-off-18", "W36-0f.4.8", "the +18 word is masked one byte late",
     [("offs.update(range(at + 18, at + 22))", "offs.update(range(at + 19, at + 23))")]),
    ("stale-off-38", "W36-0f.4.8", "the +38 word is masked one byte late",
     [("offs.update(range(at + 38, at + 42))", "offs.update(range(at + 39, at + 43))")]),
    # --- W36-0f.4b: the Gantt chart pane width -------------------------------------------------------
    ("pane-width-rule", "W36-0f.4b", "the view-state record's pane width is not read",
     [("                if length == CVIEW_GANTT_LEN and CVIEW_WIDTH_SPAN[0] <= width <= CVIEW_WIDTH_SPAN[1]:",
       "                if False:")]),
    ("pane-width-record", "W36-0f.4b", "the pane width is read in a view-state record of any size",
     [("                if length == CVIEW_GANTT_LEN and CVIEW_WIDTH_SPAN[0]", "                if CVIEW_WIDTH_SPAN[0]")]),
    ("pane-width-lo", "W36-0f.4b", "a pane width has no lower bound (0 is one)",
     [("CVIEW_WIDTH_SPAN = (128, 4096)", "CVIEW_WIDTH_SPAN = (0, 4096)")]),
    ("pane-width-lo-one", "W36-0f.4b", "a pane width may be the default 100 or any width above 99",
     [("CVIEW_WIDTH_SPAN = (128, 4096)", "CVIEW_WIDTH_SPAN = (100, 4096)")]),
    ("pane-width-hi", "W36-0f.4b", "a pane width has no upper bound (0xffff is one)",
     [("CVIEW_WIDTH_SPAN = (128, 4096)", "CVIEW_WIDTH_SPAN = (128, 65535)")]),
    ("pane-width-hi-over", "W36-0f.4b", "a pane width may be one over the span's end, 4097",
     [("CVIEW_WIDTH_SPAN = (128, 4096)", "CVIEW_WIDTH_SPAN = (128, 4097)")]),
    ("pane-width-at", "W36-0f.4b", "the pane width is read one byte late",
     [("CVIEW_WIDTH_AT = 1172", "CVIEW_WIDTH_AT = 1173")]),
    ("pane-unit-at", "W36-0f.4b", "the pane unit word is read two bytes late",
     [("CVIEW_UNIT_AT = 1180", "CVIEW_UNIT_AT = 1182")]),
    ("pane-unit-zero", "W36-C.1", "a pane unit of 0 is masked",
     [("    if w1 == w2 or not (1 <= u1 <= w1 and 1 <= u2 <= w2):",
       "    if w1 == w2 or not (u1 <= w1 and u2 <= w2):")]),
    ("pane-unit-over", "W36-C.1", "a pane unit above its width is masked",
     [("    if w1 == w2 or not (1 <= u1 <= w1 and 1 <= u2 <= w2):",
       "    if w1 == w2 or not (1 <= u1 and 1 <= u2):")]),
    ("pane-unit-same-width", "W36-C.1", "a pane unit is masked beside an unchanged width",
     [("    if w1 == w2 or not (1 <= u1", "    if not (1 <= u1")]),
    ("pane-unit-ratio", "W36-C.1", "a pane unit is masked whatever its ratio to the width",
     [("    return abs(u1 * w2 - u2 * w1) <= max(w1, w2)",
       "    return True")]),
    ("pane-unit-ratio-wide", "W36-C.1", "the ratio's tolerance is four widths, not one",
     [("abs(u1 * w2 - u2 * w1) <= max(w1, w2)", "abs(u1 * w2 - u2 * w1) <= 4 * max(w1, w2)")]),
    ("pane-unit-other", "W36-C.1", "a pane unit is masked without the other side's record",
     [("                    if other_data is not None and at + CVIEW_UNIT_AT + 2 <= len(other_data) and \\\n"
       "                            _unit_follows_width",
       "                    if True or \\\n                            _unit_follows_width")]),
    ("stamp-props-word", "R36G0c-6", "a Props save word is masked whatever its value",
     [('            if cls == "word" and length == 4 and words_one_value and _stamp_ok(data[at:at + 4]) and \\\n                    _stamp_ok(other[at:at + 4]):',
       '            if cls == "word" and length == 4 and words_one_value:')]),
    ("stamp-varkey", "R36G0c-6", "a new Assn block's stamp is masked whatever its value",
     [("                if _stamp_ok(data[at:off + 4 + hi]) and _stamp_ok(other[at:off + 4 + hi]):",
       "                if True:")]),
    ("filetime-lower", "R36G0d-2", "LastSave has no lower bound (2001, 0)",
     [("    return len(raw) == 8 and LASTSAVE_SPAN[0] <= struct.unpack(\"<Q\", raw)[0] <= LASTSAVE_SPAN[1]",
       "    return len(raw) == 8 and struct.unpack(\"<Q\", raw)[0] <= LASTSAVE_SPAN[1]")]),
    ("filetime-upper", "R36G0d-2", "LastSave has no upper bound (2600, all-ones)",
     [("    return len(raw) == 8 and LASTSAVE_SPAN[0] <= struct.unpack(\"<Q\", raw)[0] <= LASTSAVE_SPAN[1]",
       "    return len(raw) == 8 and LASTSAVE_SPAN[0] <= struct.unpack(\"<Q\", raw)[0]")]),
    ("tail-name-empty", "R36G0d-3", "an empty Props14 tail name is a name (equals its placeholder)",
     [("    return bool(payload) and _text_ok(payload)", "    return _text_ok(payload)")]),
    ("lpstr-text-nul", "R36G0c-6", "a string need not be at least its NUL, ending in a NUL",
     [('    if length < 1 or body[-1:] != b"\\0":', "    if False:")]),
    ("lpstr-text-control", "R36G0c-6", "a string may hold control characters",
     [('    return all(ch >= " " and ch != "\\x7f" for ch in text.rstrip("\\0"))', "    return True")]),
    # --- W36-0d.9: the text test takes no character of a C category ----------------------------------
    ("text-c-category", "R36G0c-8", "_text_ok takes any character from U+0020 but U+007F (C1, private use...)",
     [('    return all(unicodedata.category(ch)[0] != "C" for ch in text)',
       '    return all(ch >= " " and ch != "\\x7f" for ch in text)')]),
    # --- W36-0d.7: the OPEN pins --------------------------------------------------------------------
    ("control-mask-count", "R36G0c-7", "a pinned number, not a guard removal: the printed control-mask byte count is one too many",
     [('            lines.append("control-mask %s %d" % (name, len(masked)))',
       '            lines.append("control-mask %s %d" % (name, len(masked) + 1))')]),
]


def plant(src, edits):
    """The gate text with each (old, new[, count]) applied, or the message that it does not fit."""
    for edit in edits:
        old, new = edit[0], edit[1]
        want = edit[2] if len(edit) > 2 else 1
        if src.count(old) != want:
            return None, "the text to remove occurs %d times, not %d: %r" % (src.count(old), want, old[:70])
        src = src.replace(old, new)
    return src, None


# --- W36-0e.1 (R36G0d-1): the candidate guards are derived from the gate, not written by hand --------
# Every condition of trace_gate.py that decides a refusal, a problem line or a waiver is a candidate,
# found by `ast`:
#   if-never    an `if` whose body returns, continues, breaks or counts a problem: the test becomes False
#               (the refusal never fires)
#   or-false    each operand of an `or` inside such a test becomes False (that one refusal is gone)
#   if-always   any other `if`: the test becomes True (the branch is taken whatever the bytes)
#   and-true    each operand of an `and` outside such a test becomes True (that one requirement is gone)
#   ifexp-true / ifexp-false   each conditional expression, both ways
# Module-level code is not a candidate (the `__main__` guard).  GUARDS above are the named, compound
# removals (several edits at once); the derived candidates are every single condition.
# A candidate may be left off ONLY by REDUNDANT below: key -> (covering guard, reason).  The covering
# guard is a GUARDS id or another derived key, and its own removal must be red, so the refusal the
# listed condition makes is still made, by that one.  An entry for a key that is not derived, whose
# removal is red (it is not needed), or whose cover is not red, is stale and fails the run.  Keys are
# `function:kind:condition`, with `#n` for the n-th occurrence of the same text.
# Each reason below is a short proof that, with the condition gone, every input is still refused or
# taken as it was, because the cover does it.
REDUNDANT = {
    "frame_records:if-never:pos + 12 + length > len(data)": (
        "frame_records:ifexp-true:pos == len(data)",
        "a record that overruns the data leaves pos past len(data), which the final test refuses"),
    "_canon_cedl_var2:ifexp-true:len(data) >= 20": (
        "frame_records:ifexp-true:pos == len(data)",
        "frame_records(data, 20) of fewer than 20 bytes starts past the end and is refused by the final test"),
    "_canon_view_props:and-true:len(data) >= 16": (
        "frame_records:ifexp-true:pos == len(data)",
        "frame_records(data, 16) of fewer than 16 bytes starts past the end and is refused by the final test"),
    "_canon_props14:or-false:end > len(data)": (
        "_tail_entries:if-never:len(tail) < 4",
        "a blob that ends past the data leaves an empty tail, which _tail_entries refuses"),
    "_lpstr_text_ok:or-false:length < 1": (
        "_lpstr_text_ok:or-false:body[-1:] != b'\\x00'",
        "a length of 0 leaves an empty body, which does not end in a NUL"),
    "_canon_summary:and-true:len(raw) == 12": (
        "_filetime_ok:and-true:len(raw) == 8",
        "_filetime_ok(raw[4:]) takes only eight bytes, so a slot of another length than 12 is refused there"),
    "font_item_garbage:or-false:off == 4294967295": (
        "font_item_garbage:or-false:off + 4 > len(data)",
        "an offset of 0xffffffff is past the data, which the next test refuses"),
    "_guidstr_bytes:if-never:len(payload) < 76": (
        "_guidstr_bytes:if-never:not GUIDSTR.fullmatch(text)",
        "fewer than 76 bytes decode to fewer than 38 characters, which the shape test refuses"),
    "noise_windows:and-true:off != 4294967295": (
        "_stamp_ok:and-true:len(raw) == 4",
        "at an offset of 0xffffffff the slice is empty, and _stamp_ok of an empty slice is False"),
    "row_ids:or-false:len(data) < base": (
        "row_ids:or-false:(len(data) - base) % size",
        "base is less than size, so a stream shorter than base leaves a remainder, which the next test refuses"),
    "row_ids:or-false:len(fd) < fbase": (
        "row_ids:or-false:(len(fd) - fbase) % fsize",
        "fbase is less than fsize, so a stream shorter than fbase leaves a remainder, which the next test refuses"),
    "canon:or-false:off == 4294967295": (
        "canon:or-false:off + 4 > len(data)",
        "an offset of 0xffffffff plus 4 is past the data, which the next test refuses"),
    "_bag_chains:or-false:off == 4294967295": (
        "_bag_chains:or-false:off + 4 > len(data)",
        "an offset of 0xffffffff plus 4 is past the data, which the next test refuses"),
    "noise_windows:and-true:off != 4294967295#2": (
        "_stale_ok:and-true:len(block) == 76",
        "at an offset of 0xffffffff the block slice is empty, and _stale_ok of an empty block is False"),
    "_slot_word_ok:and-true:length == 4": (
        "noise_windows:and-true:length == 4",
        "a save word record of another length than 4 is not masked as a word, so where its two sides differ "
        "it fails there whatever the slot; where they are equal, a slot that copies it is equal too"),
    "compare:ifexp-true:_stride(name)": (
        "control_mask:if-never:stride is not None and ids is not None and (ids[0] is not None) and "
        "(ids[1] is not None)",
        "row_ids of a stream that is not a stride stream is None, so ids is (None, None) and the stride "
        "branch of control_mask is not taken, as with no ids"),
}


_COUNTER = itertools.count()


def _refusing(body):
    for n in body:
        if isinstance(n, (ast.Return, ast.Continue, ast.Break)):
            return True
        if isinstance(n, ast.AugAssign) and isinstance(n.target, ast.Name) and \
                n.target.id in ("problems", "unchecked"):
            return True
    return False


def derive(src):
    """-> [(key, kind, function, line, (start byte, end byte), replacement text, condition text)]"""
    tree = ast.parse(src)
    raw = src.encode("utf-8")
    starts, pos = [0], 0
    for ln in raw.split(b"\n")[:-1]:
        pos += len(ln) + 1
        starts.append(pos)
    found = []

    def add(kind, fn, node, repl):
        span = (starts[node.lineno - 1] + node.col_offset, starts[node.end_lineno - 1] + node.end_col_offset)
        found.append((kind, fn, node.lineno, span, repl, ast.unparse(node)))

    def visit(node, fn, refuse):
        for field, value in ast.iter_fields(node):
            for k in (value if isinstance(value, list) else [value]):
                if not isinstance(k, ast.AST):
                    continue
                f = k.name if isinstance(k, ast.FunctionDef) else fn
                r = refuse
                if isinstance(node, ast.If) and field == "test":
                    r = _refusing(node.body)
                elif isinstance(node, ast.If):
                    r = False
                if f is not None:
                    if isinstance(k, ast.If):
                        add("if-never", f, k.test, "False") if _refusing(k.body) else \
                            add("if-always", f, k.test, "True")
                    elif isinstance(k, ast.IfExp):
                        add("ifexp-true", f, k.test, "True")
                        add("ifexp-false", f, k.test, "False")
                    elif isinstance(k, ast.BoolOp) and r and isinstance(k.op, ast.Or):
                        for v in k.values:
                            add("or-false", f, v, "False")
                    elif isinstance(k, ast.BoolOp) and not r and isinstance(k.op, ast.And):
                        for v in k.values:
                            add("and-true", f, v, "True")
                visit(k, f, r)

    visit(tree, None, False)
    seen, out = {}, []
    for kind, fn, line, span, repl, text in found:
        base = "%s:%s:%s" % (fn, kind, text)
        seen[base] = seen.get(base, 0) + 1
        key = base if seen[base] == 1 else "%s#%d" % (base, seen[base])
        if raw[span[0]:span[1]].decode("utf-8") != repl:
            out.append((key, kind, fn, line, span, repl, text))
    return out


def splice(src, span, repl):
    raw = src.encode("utf-8")
    return (raw[:span[0]] + repl.encode("utf-8") + raw[span[1]:]).decode("utf-8")


def run_mutant(gid, mutant, tmp):
    d = os.path.join(tmp, "m%04d" % next(_COUNTER))
    os.makedirs(d)
    with open(os.path.join(d, "trace_gate.py"), "w", encoding="utf-8", newline="\n") as f:
        f.write(mutant)
    r = subprocess.run([sys.executable, TEST, "--gate-dir", d, "--fail-fast"],
                       capture_output=True, text=True, cwd=HERE)
    fails = [ln for ln in r.stdout.splitlines() if ln.startswith("FAIL")]
    if r.returncode == 0:
        return "GREEN", ""
    if fails and "gate raised" in fails[0]:
        return "red (raise)", fails[0][5:100]    # a case asserts the gate does not raise, and it does
    if fails:
        return "red", fails[0][5:100]
    return "CRASH", (r.stderr.strip().splitlines() or ["exit %d" % r.returncode])[-1][:100]


def run_guard(entry, src, tmp):
    gid, finding, what, edits = entry
    mutant, problem = plant(src, edits)
    if problem:
        return gid, "NOFIT", problem
    state, detail = run_mutant(gid, mutant, tmp)
    return gid, state, detail


def run_candidate(cand, src, tmp):
    key, kind, fn, line, span, repl, text = cand
    state, detail = run_mutant(key, splice(src, span, repl), tmp)
    return key, state, detail


def main(argv):
    jobs, skip_baseline, listing, picks, only = min(6, os.cpu_count() or 1), False, False, [], None
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == "--jobs":
            jobs = int(args.pop(0))
        elif a == "--only":
            only = args.pop(0)      # "named" or "derived"
        elif a == "--skip-baseline":
            skip_baseline = True
        elif a == "--list":
            listing = True
        elif a.startswith("-"):
            print(__doc__)
            return 2
        else:
            picks.append(a)
    ids = [g[0] for g in GUARDS]
    if len(set(ids)) != len(ids):
        print("duplicate guard id")
        return 1
    with open(GATE, encoding="utf-8") as f:
        src = f.read()
    cands = derive(src)
    keys = [c[0] for c in cands]
    if len(set(keys)) != len(keys):
        print("duplicate candidate key")
        return 1
    guards = [g for g in GUARDS if (not picks or g[0] in picks) and only != "derived"]
    derived = [c for c in cands if (not picks or c[0] in picks) and only != "named"]
    if listing:
        for gid, finding, what, _ in GUARDS:
            print("%-26s %-10s %s" % (gid, finding, what))
        for key, kind, fn, line, span, repl, text in cands:
            print("%-8s %s%s" % ("derived", key, "   [redundant: %s]" % REDUNDANT[key][0] if key in REDUNDANT else ""))
        return 0
    results = {}
    baseline = None
    with tempfile.TemporaryDirectory(prefix="trace-gate-meta-") as tmp:
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = [pool.submit(run_guard, g, src, tmp) for g in guards]
            futures += [pool.submit(run_candidate, c, src, tmp) for c in derived]
            if not skip_baseline and not picks:
                base = pool.submit(subprocess.run, [sys.executable, TEST, "--fail-fast"],
                                   capture_output=True, text=True, cwd=HERE)
            for fut in futures:
                gid, state, detail = fut.result()
                results[gid] = (state, detail)
            if not skip_baseline and not picks:
                baseline = base.result().returncode
    named = {g[0]: g for g in GUARDS}
    for gid, finding, what, _ in guards:
        state, detail = results[gid]
        print("%-12s %-26s %-10s %s%s" % (state, gid, finding, what, (" -> " + detail) if detail else ""))
    for key, kind, fn, line, span, repl, text in derived:
        state, detail = results[key]
        note = ("   [redundant: %s]" % REDUNDANT[key][0]) if key in REDUNDANT else ""
        print("%-12s derived %s (line %d)%s%s" % (state, key, line, note, (" -> " + detail) if detail else ""))
    # named guards: every one red; derived candidates: red, or on REDUNDANT and not red
    bad = [g for g, _, _, _ in guards if not results[g][0].startswith("red")]
    d_red = [c[0] for c in derived if results[c[0]][0].startswith("red")]
    d_raise = [k for k in d_red if results[k][0] == "red (raise)"]
    d_redundant = [c[0] for c in derived if c[0] in REDUNDANT and not results[c[0]][0].startswith("red")]
    d_green = [c[0] for c in derived if results[c[0]][0] == "GREEN" and c[0] not in REDUNDANT]
    d_crash = [c[0] for c in derived if results[c[0]][0] == "CRASH"]
    n_crash = [g for g, _, _, _ in guards if results[g][0] == "CRASH"]
    d_nofit = [c[0] for c in derived if results[c[0]][0] == "NOFIT"]
    # a stale entry: not a derived key, no longer green (it is red: the entry is not needed), or a
    # cover that is not a guard, is the entry itself, or is not red when removed.  With `--only` (W36b-0.6)
    # only the entries and covers that ran are judged on their result; the rest are judged in the other shard.
    stale = []
    if not picks:
        for key, (cover, why) in REDUNDANT.items():
            if key not in keys:
                stale.append("%s (not a derived candidate)" % key)
            elif key in results and results[key][0].startswith("red"):
                stale.append("%s (red when removed: take it off the list)" % key)
            elif cover == key or (cover not in named and cover not in keys):
                stale.append("%s (the cover %r is not another guard)" % (key, cover))
            elif cover in results and not results[cover][0].startswith("red"):
                stale.append("%s (the cover %r is not red when removed: %s)" % (key, cover, results[cover][0]))
    print("named guards listed %d, red %d, not red %d%s" %
          (len(guards), len(guards) - len(bad), len(bad), (": " + ", ".join(bad)) if bad else ""))
    print("candidates derived %d (run %d): removed red %d (%d of them by a case that asserts the gate does "
          "not raise), listed redundant %d, green %d, crashed %d" %
          (len(cands), len(derived), len(d_red), len(d_raise), len(d_redundant), len(d_green),
           len(d_crash) + len(n_crash)))
    for what, items in (("GREEN, not on the redundant list", d_green), ("CRASH (the test died; a case is "
                        "missing)", d_crash + n_crash), ("NOFIT", d_nofit), ("stale REDUNDANT entry", stale)):
        for item in items:
            print("  %s: %s" % (what, item))
    if baseline is not None:
        print("the unmodified gate: %s" % ("passes" if baseline == 0 else "FAILS (exit %d)" % baseline))
    ok = not bad and not d_green and not d_crash and not n_crash and not d_nofit and not stale
    return 0 if ok and baseline in (None, 0) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
