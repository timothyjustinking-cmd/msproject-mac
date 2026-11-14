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
the unmodified gate passes (`--skip-baseline` leaves that to the caller, which is test_trace_gate.py
running this after its own pass).  Exit 1 otherwise; 77 when olefile is absent.
"""

import concurrent.futures
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
     [('    if named_absent:\n        out += struct.pack("<II", 0xFFFFFFFF, 0x1E)\n', "")]),
    ("named-absent-count", "R36G0b-2", "the Props14 count word is the raw count",
     [('    out += struct.pack("<I", len(canon))', '    out += struct.pack("<I", len(entries))')]),
    ("tail-ffff-dropped", "R36G0b-2", "the 0xffffffff tail name is dropped whatever its bytes",
     [('(b"" if _text_ok(name) else name)', '(b"" if (_text_ok(name) or eid == 0xFFFFFFFF) else name)')]),
    ("tail-names-waived", "R36G0b-2", "every Props14 tail name is waived",
     [("        if _text_ok(name):\n            noise.update(range(at + 8, stop))",
       "        if True:\n            noise.update(range(at + 8, stop))"),
      ('(b"" if _text_ok(name) else name)', 'b""')]),
    ("tail-entries-dropped", "R36G0b-2", "every Props14 tail entry is dropped",
     [('    canon = ([(0xFFFFFFFF, b"")] if named_absent else []) + entries', "    canon = []"),
      ('    for eid, name in entries:\n        out += struct.pack("<II", eid, 0x1E) + (b"" if _text_ok(name) else name)',
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
     [("    if nul is not None and nul == ctl:", "    if False:")]),
    ("cedl-meta-length", "R36G0b-6", "CEdl VarMeta +20 is zeroed unconditionally",
     [("    if var2 is None or len(data) < 24 or _u32(data, 20) != len(var2):",
       "    if var2 is None or len(data) < 24:")]),
    ("guidstr-shape", "R36G0b-6", "the view GUID string's shape is not tested",
     [("    if not GUIDSTR.fullmatch(text):\n        return None\n", "")]),
    ("guidstr-other", "R36G0b-6", "the view GUID string is masked when only one side holds one",
     [("                if digits and digits == _guidstr_digits(other[at:at + length]):",
       "                if digits:")]),
    ("new-row-other", "R36G0b-4", "a new-row stamp is masked without the other side's row",
     [("                        (row + 4 > len(other) or _u32(other, row) not in known) and \\\n", "")]),
    ("varkey-uid", "R36G0b-4", "an Assn stamp block is masked whether or not the control has it",
     [('if key in rule["varkey"] and off != 0xFFFFFFFF and uid not in known:',
       'if key in rule["varkey"] and off != 0xFFFFFFFF:')]),
    ("si-placeholders", "R36G0b-6", "every SummaryInformation property is replaced by a placeholder",
     [("        elif pid == SI_LASTSAVE and len(raw) == 12 and _u32(raw, 0) == 0x40 and \\\n                _filetime_ok(raw[4:]):",
       "        elif len(raw) >= 8 and pid != 0:")]),
    ("si-lastsave-any", "R36G0b-6", "any 12-byte FILETIME is LastSave",
     [("        elif pid == SI_LASTSAVE and len(raw) == 12 and _u32(raw, 0) == 0x40 and \\\n                _filetime_ok(raw[4:]):",
       "        elif len(raw) == 12 and _u32(raw, 0) == 0x40 and _filetime_ok(raw[4:]):")]),
    ("text-ok-true", "R36G0b-6", "_text_ok is always true",
     [("    if len(payload) % 2 or len(payload) > 2048:\n        return False\n    try:",
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
    ("tail-first-entry", "R36G0c-5", "_tail_entries: the first entry need not start at +4",
     [("    if len(marks) != count or (marks and marks[0] != 4) or not marks and len(tail) != 4:",
       "    if len(marks) != count or not marks and len(tail) != 4:")]),
    ("props14-size-words", "R36G0c-5", "the Props14 blob's two size words need not agree",
     [("    if _u32(data, 4) != end - 4 or end > len(data):", "    if end > len(data):")]),
    ("text-cap", "R36G0c-5", "_text_ok has no 2048-byte cap",
     [("    if len(payload) % 2 or len(payload) > 2048:", "    if len(payload) % 2:")]),
    # --- W36-0d.1: a derived field is told from a raw one -------------------------------------------
    ("derived-compare", "R36G0c-1", "a field derived on one side only is not a problem",
     [("        for field in sorted(d_ref.get(name, set()) ^ d_traced.get(name, set())):",
       "        for field in []:")]),
    ("derived-cedl-words", "R36G0c-1", "the CEdl size words are not named as derived",
     [('            derived.add("+%d" % at)', "            pass")]),
    ("derived-cedl-path", "R36G0c-1", "the CEdl path record is not named as derived",
     [('            derived.add("path")\n        else:\n            out += data[pos:end]',
       "            pass\n        else:\n            out += data[pos:end]")]),
    ("derived-cedl-meta", "R36G0c-1", "CEdl VarMeta +20 is not named as derived",
     [('set(range(20, 24)), {"+20"}', "set(range(20, 24)), set()")]),
    ("derived-view-size", "R36G0c-1", "214/Props 0x00010007 is not named as derived",
     [('            derived.add("rec %#010x" % rid)', "            pass")]),
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
    ("stamp-zero", "R36G0c-6", "_stamp_ok accepts 0",
     [('raw != bytes(4) and raw != b"\\xff" * 4', 'raw != b"\\xff" * 4')]),
    ("stamp-ones", "R36G0c-6", "_stamp_ok accepts all-ones",
     [('raw != bytes(4) and raw != b"\\xff" * 4', "raw != bytes(4)")]),
    ("stamp-new-row", "R36G0c-6", "a new Task row's stamp is masked whatever its value",
     [("                        _stamp_ok(data[row + lo:row + hi]) and _stamp_ok(other[row + lo:row + hi]):",
       "                        True:")]),
    ("stamp-props-word", "R36G0c-6", "a Props save word is masked whatever its value",
     [('            if cls == "word" and length == 4 and _stamp_ok(data[at:at + 4]) and \\\n                    _stamp_ok(other[at:at + 4]):',
       '            if cls == "word" and length == 4:')]),
    ("stamp-varkey", "R36G0c-6", "a new Assn block's stamp is masked whatever its value",
     [("                if _stamp_ok(data[at:off + 4 + hi]) and _stamp_ok(other[at:off + 4 + hi]):",
       "                if True:")]),
    ("filetime-lower", "R36G0c-6", "LastSave may be before 2000",
     [("    return FILETIME_2000 <= value < 0xFFFFFFFFFFFFFFFF", "    return value < 0xFFFFFFFFFFFFFFFF")]),
    ("filetime-upper", "R36G0c-6", "LastSave may be all-ones",
     [("    return FILETIME_2000 <= value < 0xFFFFFFFFFFFFFFFF", "    return FILETIME_2000 <= value")]),
    ("lpstr-text-nul", "R36G0c-6", "a string need not be at least its NUL, ending in a NUL",
     [('    if length < 1 or body[-1:] != b"\\0":', "    if False:")]),
    ("lpstr-text-control", "R36G0c-6", "a string may hold control characters",
     [('    return all(ch >= " " and ch != "\\x7f" for ch in text.rstrip("\\0"))', "    return True")]),
    # --- W36-0d.9: the text test takes no character of a C category ----------------------------------
    ("text-c-category", "R36G0c-8", "_text_ok takes any character from U+0020 but U+007F (C1, private use...)",
     [('    return all(unicodedata.category(ch)[0] != "C" for ch in text)',
       '    return all(ch >= " " and ch != "\\x7f" for ch in text)')]),
    # --- W36-0d.7: the OPEN pins --------------------------------------------------------------------
    ("control-mask-count", "R36G0c-7", "the printed control-mask byte count is one too many",
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


def run_guard(entry, src, tmp):
    gid, finding, what, edits = entry
    mutant, problem = plant(src, edits)
    if problem:
        return gid, "NOFIT", problem
    d = os.path.join(tmp, gid)
    os.makedirs(d)
    with open(os.path.join(d, "trace_gate.py"), "w", encoding="utf-8", newline="\n") as f:
        f.write(mutant)
    r = subprocess.run([sys.executable, TEST, "--gate-dir", d, "--no-meta", "--fail-fast"],
                       capture_output=True, text=True, cwd=HERE)
    fails = [ln for ln in r.stdout.splitlines() if ln.startswith("FAIL")]
    if r.returncode == 0:
        return gid, "GREEN", ""
    if fails:
        return gid, "red", fails[0][5:100]
    return gid, "red (crash)", (r.stderr.strip().splitlines() or ["exit %d" % r.returncode])[-1][:100]


def main(argv):
    jobs, skip_baseline, listing, picks = min(6, os.cpu_count() or 1), False, False, []
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == "--jobs":
            jobs = int(args.pop(0))
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
    guards = [g for g in GUARDS if not picks or g[0] in picks]
    if listing:
        for gid, finding, what, _ in GUARDS:
            print("%-26s %-10s %s" % (gid, finding, what))
        return 0
    with open(GATE, encoding="utf-8") as f:
        src = f.read()
    results = {}
    baseline = None
    with tempfile.TemporaryDirectory(prefix="trace-gate-meta-") as tmp:
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = [pool.submit(run_guard, g, src, tmp) for g in guards]
            if not skip_baseline and not picks:
                base = pool.submit(subprocess.run, [sys.executable, TEST, "--no-meta", "--fail-fast"],
                                   capture_output=True, text=True, cwd=HERE)
            for fut in futures:
                gid, state, detail = fut.result()
                results[gid] = (state, detail)
            if not skip_baseline and not picks:
                baseline = base.result().returncode
    for gid, finding, what, _ in guards:
        state, detail = results[gid]
        print("%-12s %-26s %-10s %s%s" % (state, gid, finding, what, (" -> " + detail) if detail else ""))
    red = [g for g, (s, _) in results.items() if s.startswith("red")]
    crash = [g for g, (s, _) in results.items() if s == "red (crash)"]
    bad = [g for g, (s, _) in results.items() if not s.startswith("red")]
    print("guards listed %d, red %d (%d by a failing case, %d by a crash), not red %d%s" %
          (len(guards), len(red), len(red) - len(crash), len(crash), len(bad),
           (": " + ", ".join(bad)) if bad else ""))
    if baseline is not None:
        print("the unmodified gate: %s" % ("passes" if baseline == 0 else "FAILS (exit %d)" % baseline))
    return 0 if not bad and baseline in (None, 0) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
