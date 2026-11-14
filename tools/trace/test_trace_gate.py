#!/usr/bin/env python3
"""Guards for tools/trace/trace_gate.py (wave 36 G0, G0b and G0c).

Runs the gate on committed pairs (tests/data) and asserts it FAILS where it must.  The first four
are G0's; G0b and G0c added the later checks listed after them:

  W36-0.1  no by-name whole-stream waiver: a flipped byte in a stream the old gate skipped whole
           (214/CEdl/Var2Data, Props14) is no longer hidden;
  W36-0.2  no wrong-length mask: a control/null difference is not applied to a reference of
           another length, and a fixed-stride stream is masked record by record;
  W36-0.3  an absent control null, or the control passed as its own null, is a FAIL "no null";
  W36-0.4  one flipped byte in EVERY stream of a committed pair (at an offset no named field
           covers) is a FAIL, with `checked > 0` and an equal stream set; the unflipped pair is
           a PASS.
  W36-0b   every offset outside the named fields is flipped in three pairs and fails; planted
           windows are found; stride masks follow a row's identity; the two sweeps run over the
           committed pairs;
  W36-0c   the sweeps run over the committed file set, with the OPEN list (pairs whose undecoded
           field makes the positive sweep fail) pinned to what each stream and offset shows; the
           control mask excuses nothing (0c.5); a GUID is noise only as a version-1 GUID (0c.2),
           a SummaryInformation string slot only as exactly its string (0c.3), the Props14 tail
           entries and count word as named (0c.4); the noise of the five parsed streams and the
           font rows is written in the test, not read from the gate (0c.6); the head line's
           `not checked` is a real count (0c.8).

    python test_trace_gate.py [--gate PATH | --gate-dir DIR] [--cli-only]

--gate runs the command-line cases against another copy of the gate (a red run against an older
version); --gate-dir runs every case, in-process ones too, against DIR/trace_gate.py (a gate
with one guard removed); --cli-only skips the cases that import the gate's functions.  Exit 0
all pass, 1 a case failed, 77 olefile is not installed (ctest SKIP).
"""

import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "..", "..", "tests", "data")

try:
    import olefile
except ImportError:
    print("SKIP: olefile is not installed (pip install olefile)")
    sys.exit(77)

gate_path = os.path.join(HERE, "trace_gate.py")
cli_only = False
gate_dir = HERE
args = sys.argv[1:]
while args:
    a = args.pop(0)
    if a == "--gate":
        gate_path = args.pop(0)
        cli_only = True
    elif a == "--gate-dir":
        # another copy of the gate for the command line AND the in-process cases (a mutant)
        gate_dir = os.path.abspath(args.pop(0))
        gate_path = os.path.join(gate_dir, "trace_gate.py")
    elif a == "--cli-only":
        cli_only = True
    else:
        print(__doc__)
        sys.exit(2)

failures = []


def check(label, ok, detail=""):
    print(("PASS " if ok else "FAIL ") + label + ((" -- " + detail) if detail and not ok else ""))
    if not ok:
        failures.append(label)


def pair(stem):
    return [os.path.join(DATA, stem + s) for s in (".mpp", "-control.mpp", "-control-null.mpp")]


# The control of a committed control-null save.  Eight nulls were made from a control that is not
# `<stem>-control.mpp` (scripts/oracle_r7_nulls.py NULLS; the capture specs' `Ctl` in
# tools/trace/capture/specs6-g5-*.ps1 name the same files); every other null's control is
# `<stem>-control.mpp`.  A null whose control is not in tests/data is a FAIL line, never a crash.
CONTROL_OF = {
    "wave23-flags": "wave23-outline",
    "wave27-percent-actuals": "synthetic-base",
    "wave27-priority-750": "wave27-priority-control",
    "wave28-newtask-auto": "wave28-newtask-manual-control",
    "wave28-newtask-manual": "wave28-newtask-auto-control",
    "wave31-resource-sheet": "synthetic-base",
    "wave31-tables": "synthetic-base",
    "wave31-usage": "synthetic-base",
}


def control_path(stem):
    path = os.path.join(DATA, CONTROL_OF.get(stem, stem + "-control") + ".mpp")
    return path if os.path.exists(path) else None


def stream_path(ole, name):
    for s in ole.listdir(streams=True, storages=False):
        n = "/".join("".join(c if " " < c < "\x7f" else "?" for c in part) for part in s)
        if n == name:
            return s
    raise KeyError(name)


def flipped_copy(src, dst, name, offset):
    shutil.copyfile(src, dst)
    ole = olefile.OleFileIO(dst, write_mode=True)
    try:
        path = stream_path(ole, name)
        data = bytearray(ole.openstream(path).read())
        data[offset] ^= 0xFF
        ole.write_stream(path, bytes(data))
    finally:
        ole.close()


def run_cli(traced, ref, ctl, nul):
    p = subprocess.run([sys.executable, gate_path, traced, ref, ctl, nul],
                       capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def _u(blob):
    return struct.unpack_from("<I", blob, 0)[0]


def head_counts(out):
    m = re.match(r"(\d+) streams, (\d+) checked, (?:(\d+) not checked, )?(\d+) problems",
                 out.splitlines()[0])
    return int(m.group(1)), int(m.group(2)), int(m.group(4))   # streams, checked, problems


tmp = tempfile.mkdtemp(prefix="trace-gate-test-")
try:
    ref, ctl, nul = pair("wave30-calculate")

    # --- the unflipped pair passes, with something checked and the same stream set --------------
    rc, out = run_cli(ref, ref, ctl, nul)
    streams_n, checked, problems = head_counts(out)
    check("W36-0.4 identical pair: exit 0", rc == 0, out[:200])
    check("W36-0.4 identical pair: checked > 0", checked > 0, out[:200])
    check("W36-0.4 identical pair: no only-in line",
          "only-in" not in out and problems == 0, out[:200])

    # --- W36-0.1: a flip in a stream the old gate skipped whole is caught ----------------------
    for name, offset in (("???214/CEdl/Var2Data", 20), ("Props14", 20)):
        bad = os.path.join(tmp, "flip.mpp")
        # flip a block of 200 bytes: no field of a path stream is that wide
        shutil.copyfile(ref, bad)
        ole = olefile.OleFileIO(bad, write_mode=True)
        path = stream_path(ole, name)
        data = bytearray(ole.openstream(path).read())
        for k in range(offset, offset + 200):
            data[k] ^= 0xFF
        ole.write_stream(path, bytes(data))
        ole.close()
        rc, out = run_cli(bad, ref, ctl, nul)
        check("W36-0.1 %s: 200 flipped bytes fail (the old gate skipped the stream whole)" % name,
              rc == 1 and name in out, out[:300])

    # --- W36-0b.1 (R36G0-1): the view stream is not under a whole-stream window -----------------
    # TRACED = the control's own null save, REFERENCE = the edit: a view-state edit must FAIL, and
    # the failing stream is the view bag, not a skipped one.
    for stem in ("wave35-viewstate-sort-name-desc", "wave35-g3-font-bold-off"):
        e_ref, e_ctl, e_nul = pair(stem)
        rc, out = run_cli(e_nul, e_ref, e_ctl, e_nul)
        check("W36-0b.1 %s: control-null vs the edit fails on the view bag" % stem,
              rc == 1 and "bytes-differ ???214/CV_iew/Var2Data" in out, out[:300])

    # --- W36-0b.3 (R36G0-3): stream names match whole; DocumentSummaryInformation in full -------
    # The old gate matched by suffix, so "SummaryInformation" also took the document summary
    # (a separate stream) and gave it a size tolerance.  One flipped byte in it must now fail.
    dsi = "?DocumentSummaryInformation"
    for offset in (0, 60):
        bad = os.path.join(tmp, "flip-dsi.mpp")
        flipped_copy(ref, bad, dsi, offset)
        rc, out = run_cli(bad, ref, ctl, nul)
        check("W36-0b.3 one flipped byte at +%d of %s fails and names it" % (offset, dsi),
              rc == 1 and ("bytes-differ " + dsi) in out, out[:300])

    # --- W36-0.2: the control/null difference is not applied to a reference of another length ----
    ref2, ctl2, nul2 = pair("wave34-assign-add-second")
    bad = os.path.join(tmp, "flip2.mpp")
    flipped_copy(ref2, bad, "???114/TBkndAssn/FixedMeta", 4)   # offset 4 differs in control/null
    rc, out = run_cli(bad, ref2, ctl2, nul2)
    check("W36-0.2 flip at a control/null offset of a longer reference fails", rc == 1 and
          "TBkndAssn/FixedMeta" in out, out[:300])

    # --- W36-0.3: no null is a FAIL named "no null" ---------------------------------------------
    rc, out = run_cli(ref, ref, ctl, os.path.join(tmp, "does-not-exist.mpp"))
    check("W36-0.3 absent control null: exit 1 and 'no null'", rc == 1 and "no null" in out, out[:300])
    rc, out = run_cli(ref, ref, ctl, ctl)
    check("W36-0.3 control passed as its own null: exit 1 and 'no null'",
          rc == 1 and "no null" in out, out[:300])

    # --- W36-0.4: one flipped byte in every stream, at an offset nothing covers ----------------
    if not cli_only:
        sys.path.insert(0, gate_dir)
        import trace_gate as g

        def load(path):
            return g.streams(path)

        R, C, N = load(ref), load(ctl), load(nul)

        def is_v1(raw):
            return len(raw) == 16 and raw[6] >> 4 == 1 and raw[8] >> 6 == 2

        def records(data, base):
            pos, out = base, []
            while pos + 12 <= len(data):
                length, rid = struct.unpack_from("<II", data, pos)
                out.append((pos + 12, length, rid))
                pos += 12 + length + (length & 1)
            return out

        def blocks(meta):
            if not meta or len(meta) < 24:
                return []
            return [struct.unpack_from("<III", meta, 24 + 12 * k)
                    for k in range(struct.unpack_from("<I", meta, 8)[0])]

        def task_uids(blob):
            return {struct.unpack_from("<I", blob, 48 + 202 * r)[0]
                    for r in range(max(0, (len(blob or b"") - 48) // 202))}

        # --- W36-0c.6 (R36G0b-6): the noise of the parsed streams and the font rows, written here -----
        # From the rows of trace-gate-fields-g0b.md and the test's own parse of the bag and of the
        # property records; nothing below calls the gate's canonical forms or font parser.
        def text_ok(raw):
            """Printable UTF-16LE with at most one trailing NUL: a path or a user name."""
            if len(raw) % 2 or len(raw) > 2048:
                return False
            try:
                s = raw.decode("utf-16le")
            except UnicodeDecodeError:
                return False
            s = s[:-1] if s.endswith("\0") else s
            return all(c >= " " and c != "\x7f" for c in s)

        CANON_STREAMS = ("???214/CEdl/Var2Data", "???214/CEdl/VarMeta", "???214/Props", "Props14",
                         "?SummaryInformation")

        def padded(at, length):
            return range(at, at + length + (length & 1))

        def spec_canon(name, data, streams_):
            """The offsets replaced in the canonical form of `data`, by the evidence note."""
            out = set()
            if name == "???214/CEdl/Var2Data":
                for at, want in ((0, len(data) - 4), (4, len(data) - 8), (8, len(data) - 8)):
                    if struct.unpack_from("<I", data, at)[0] == want:
                        out.update(range(at, at + 4))
                for at, length, rid in records(data, 20):
                    if rid == 0x26400031 and text_ok(data[at:at + length]) and \
                            data[at:at + length].endswith(b"!\0\0\0"):
                        out.update(range(at - 12, at - 8))
                        out.update(padded(at, length))
            elif name == "???214/CEdl/VarMeta":
                var2 = streams_.get("???214/CEdl/Var2Data")
                if var2 is not None and struct.unpack_from("<I", data, 20)[0] == len(var2):
                    out.update(range(20, 24))
            elif name == "???214/Props":
                var2 = streams_.get("???214/CEdl/Var2Data")
                for at, length, rid in records(data, 16):
                    if rid == 0x00010007 and length == 4 and var2 is not None and \
                            struct.unpack_from("<I", data, at)[0] == len(var2):
                        out.update(range(at, at + 4))
            elif name == "Props14":
                end = _u(data) + 4
                out.update(range(0, 8))
                for at, length, rid in records(data[:end], 16):
                    if rid == 0x35400008 and text_ok(data[at:at + length]):
                        out.update(range(at - 12, at - 8))
                        out.update(padded(at, length))
                tail = data[end:]
                marks = [i for i in range(4, len(tail) - 7, 2)
                         if struct.unpack_from("<I", tail, i)[0] in (0xFFFFFFFF, 0x3540000B) and
                         struct.unpack_from("<I", tail, i + 4)[0] == 0x1E]
                names = [(m, e) for m, e in zip(marks, marks[1:] + [len(tail)])]
                if len(names) == 1 and struct.unpack_from("<I", tail, names[0][0])[0] == 0x3540000B \
                        and text_ok(tail[names[0][0] + 8:names[0][1]]):
                    out.update(range(end, end + 4))   # the count word, the named absent case
                for m, e in names:
                    if text_ok(tail[m + 8:e]):
                        out.update(range(end + m + 8, end + e))
            elif name == "?SummaryInformation":
                sec = _u(data[44:])
                count = struct.unpack_from("<I", data, sec + 4)[0]
                ents = [struct.unpack_from("<II", data, sec + 8 + 8 * i) for i in range(count)]
                out.update(range(sec, sec + 4))
                for i in range(count):
                    out.update(range(sec + 8 + 8 * i + 4, sec + 8 + 8 * i + 8))
                order = sorted(ents, key=lambda e: e[1])
                ends = [e[1] for e in order[1:]] + [len(data) - sec]
                for (pid, off), stop in zip(order, ends):
                    lo, hi = sec + off, sec + stop
                    slen = struct.unpack_from("<I", data, lo + 4)[0] if hi - lo >= 8 else 0
                    if pid in (4, 8, 9) and hi - lo >= 8 and _u(data[lo:]) == 0x1E and \
                            hi - lo == 8 + ((slen + 3) & ~3) and not any(data[lo + 8 + slen:hi]):
                        out.update(range(lo + 4, lo + 8 + slen))
                    elif pid == 13 and hi - lo == 12 and _u(data[lo:]) == 0x40:
                        out.update(range(lo + 4, hi))
            return out

        def bag_records(data, meta):
            """[(payload offset, length, id, kind)] of the records of every bag block (VarMeta key
            0x20400006): a 4-byte length, a 16-byte header, then [len][id][kind][payload] padded."""
            out = []
            for uid, off, key in blocks(meta):
                if key != 0x20400006 or off == 0xFFFFFFFF or off + 4 > len(data):
                    continue
                stop = min(off + 4 + _u(data[off:]), len(data))
                pos = off + 20
                while pos + 12 <= stop:
                    length, rid, kind = struct.unpack_from("<III", data, pos)
                    if pos + 12 + length > stop:
                        break
                    out.append((pos + 12, length, rid, kind))
                    pos += 12 + length + (length & 1)
            return out

        def bag_font_bytes(data, meta):
            """Payload +43 of each 44-byte row of every font item (0x2240000c, kind 1)."""
            return [at + 44 * row + 43 for at, length, rid, kind in bag_records(data, meta)
                    if rid == 0x2240000C and kind == 1 and length % 44 == 0
                    for row in range(length // 44)]

        # the CLI path once, on a stream class the gate checks plainly
        bad = os.path.join(tmp, "flip3.mpp")
        flipped_copy(ref, bad, "???114/TBkndTask/FixedData", 60)
        rc, out = run_cli(bad, ref, ctl, nul)
        check("W36-0.4 CLI: one flipped byte in Task FixedData fails", rc == 1, out[:300])

        lines, problems, checked, unchecked = g.compare(R, R, C, N)
        check("W36-0.4 compare: identical pair has no problem and checked > 0",
              problems == 0 and checked > 0, "%r" % (lines,))
        check("W36-0.4 compare: equal stream set (no only-in line)",
              not any(ln.startswith("only-in") for ln in lines))

        # W36-0b.1: only the font word's top byte (payload +43) of the font item is noise
        f_ref, f_ctl, f_nul = pair("wave35-g3-font-bold-off")
        FR, FC, FN = load(f_ref), load(f_ctl), load(f_nul)
        view, view_meta = "???214/CV_iew/Var2Data", "???214/CV_iew/VarMeta"
        found = bag_font_bytes(FR[view], FR[view_meta])
        check("W36-0b.1 the bold-off edit has one font item row, found by parsing the bag",
              len(found) == 1, "%r" % (found,))
        for delta, want in ((0, 0), (-3, 1)):   # +43 passes, +40 fails
            mutated = dict(FR)
            b = bytearray(FR[view])
            b[found[0] + delta] ^= 0xFF
            mutated[view] = bytes(b)
            lines, problems, checked, unchecked = g.compare(mutated, FR, FC, FN)
            check("W36-0b.1 font item payload +%d flipped -> %d problem(s)" % (43 + delta, want),
                  problems == want and (want == 0 or any(view in ln for ln in lines)),
                  "%r" % (lines,))

        # W36-0b.3: a name that merely ends like a rule's key gets no rule, no stride, no parser
        for key in list(g.NOISE) + list(g.STRIDE) + list(g.CANON) + list(g.VAR_META):
            for look in ("x" + key, "zzz/" + key, key[1:]):
                check("W36-0b.3 %r is not matched as %r" % (look, key),
                      g._noise_rule(look) is None and g._stride(look) is None and
                      look not in g.CANON and look not in g.VAR_META)
        check("W36-0b.3 the document summary is not a canonical stream",
              "?DocumentSummaryInformation" not in g.CANON and
              "?DocumentSummaryInformation" in R)

        # --- the acceptance sweep over every committed pair (W36-0b.2) --------------------------
        # Positive: the control's own null save, traced against the control, is a PASS.  Negative:
        # the null save traced against the pair's edit is a FAIL when the edit differs from the null
        # save outside SummaryInformation.  Both run on every control-null in tests/data (W36-0c.1).
        #
        # OPEN: the null saves whose records differ from their control's in a field nothing decodes
        # (the gate reports the stream; the field and the reason are read from the gate's output and
        # the records, as of 14 November 2026).  An OPEN pair is reported as OPEN, never as a PASS;
        # the list may only grow with a named field, and a pair that passes again must leave it.
        CEDL = "???214/CEdl/Var2Data"
        R24 = {CEDL: "record 0x26400024 (the sort table) is 64 bytes in one save and 128 in the "
                     "other; what doubles it is not decoded"}
        R3D = {CEDL: "record 0x2640003d (26 bytes) is in one save only; what adds it is not decoded"}
        LATER_BUILD = {   # the null was saved by a later Project build than its older control
            CEDL: "the path record 0x26400031 is absent from the control and present in the null",
            "???114/TBkndAssn/Var2Data": "one block of key 0x0f400299 (20 bytes) per assignment, in "
                                         "the null only; not decoded",
            "???114/TBkndAssn/VarMeta": "the three VarMeta entries of those blocks (36 bytes)",
            "???214/CFilter/FixedData": "a second filter row (230 bytes) in the null only; not decoded",
            "???214/CFilter/FixedMeta": "the meta items of that second filter row",
            "???214/CFilter/Var2Data": "the var block of that second filter row (116 bytes)",
            "???214/CFilter/VarMeta": "its VarMeta entry, uid 2, key 0x20400006",
            "Props14": "the canonical forms are 428 and 408 bytes; the record or tail entry that "
                       "differs is not decoded",
        }
        SIZE_OPEN = {
            "wave27-priority-750": R24, "wave27-unlink": R24, "wave28-newtask-auto": R24,
            "wave28-newtask-manual": R24, "wave28-split": R24,
            "wave35-g3-assign-remove-port-saved": R24,
            "wave30-inactivate": R3D, "wave36-r7-font-bold-session2": R3D,
            "wave23-flags": LATER_BUILD, "wave27-percent-actuals": LATER_BUILD,
            "wave31-resource-sheet": LATER_BUILD, "wave31-tables": LATER_BUILD,
            "wave31-usage": LATER_BUILD,
        }
        # W36-0c.5 (R36G0b-1): the control mask excuses nothing.  These are the streams in which the
        # control and its null differ at bytes no named field covers, so the gate prints
        # `control-mask <stream> <n>` and the pair is OPEN.  Each field is recorded as NOT DECODED
        # (offsets as of 14 November 2026, from the control/null pairs); none is masked.
        MASKED = {
            "114/Props": "typed-property records between +63430 and +63502 (0x01000004, 0x00800004, "
                         "0x024013c8, 0x024013bd among them); what they hold is not decoded",
            "114/TBkndAssn/Fixed2Data": "row +32..+37 and +41: data by trace-gate-fields-g0b.md "
                                        "row 12, never masked",
            "114/TBkndAssn/Fixed2Meta": "meta items at +8, +20, +68..+78, +121..+131, +174; not decoded",
            "114/TBkndAssn/FixedMeta": "meta items at +4, +8, +16..+20, +50..+54, +86..+97; not decoded",
            "114/TBkndRsc/FixedMeta": "meta items at +176 and +213; not decoded",
            "114/TBkndTask/Fixed2Data": "row +42 and +46..+48: a flags byte and the Calculate save "
                                        "stamp, data by trace-gate-fields-g0b.md row 13, never masked",
            "114/TBkndTask/FixedMeta": "the meta item at +315; not decoded",
            "214/CEdl/Var2Data": "12 payload bytes of record 0x26400024 (the sort table); not decoded",
            "214/CTable/FixedData": "the byte at +447; not decoded",
            "214/CV_iew/Var2Data": "4-byte save words of the view bag (bag block uid 2 +42682; block "
                                   "uid 3 +2270..+2281 and their kin); not decoded",
            "214/Props": "the words at +10358 and +10406 of the canonical form; not decoded",
            "Props14": "the canonical byte at +394 (or the record that holds it); not decoded",
        }
        MASKED_IN = {
            "wave23-flags": (
                "114/Props", "114/TBkndAssn/Fixed2Data", "114/TBkndAssn/Fixed2Meta",
                "114/TBkndAssn/FixedMeta", "114/TBkndTask/Fixed2Data", "214/CV_iew/Var2Data",
                "214/Props",
            ),
            "wave27-percent-actuals": (
                "114/Props", "114/TBkndAssn/Fixed2Meta", "114/TBkndAssn/FixedMeta",
                "114/TBkndRsc/FixedMeta", "114/TBkndTask/Fixed2Data", "114/TBkndTask/FixedMeta",
                "214/CV_iew/Var2Data", "214/Props",
            ),
            "wave27-priority-750": ("214/CV_iew/Var2Data",),
            "wave27-unlink": ("114/TBkndAssn/Fixed2Data", "214/CV_iew/Var2Data"),
            "wave28-mark-auto": ("214/CV_iew/Var2Data",),
            "wave28-mark-manual": ("214/CV_iew/Var2Data",),
            "wave28-newtask-auto": ("214/CV_iew/Var2Data",),
            "wave28-newtask-manual": ("214/CV_iew/Var2Data",),
            "wave28-outline-indent": ("214/CV_iew/Var2Data",),
            "wave28-outline-outdent": (
                "114/Props", "114/TBkndAssn/Fixed2Meta", "114/TBkndAssn/FixedMeta",
                "214/CV_iew/Var2Data",
            ),
            "wave28-outline-summary": (
                "114/Props", "114/TBkndAssn/Fixed2Meta", "114/TBkndAssn/FixedMeta",
                "214/CV_iew/Var2Data",
            ),
            "wave28-percent-auto0": ("214/CV_iew/Var2Data",),
            "wave28-percent-auto100": ("214/CV_iew/Var2Data", "Props14"),
            "wave28-percent-auto40": ("214/CV_iew/Var2Data", "Props14"),
            "wave28-percent-manual0": ("214/CV_iew/Var2Data",),
            "wave28-split": ("214/CV_iew/Var2Data",),
            "wave28-update": ("214/CV_iew/Var2Data",),
            "wave29-outline-auto-indent": ("214/CV_iew/Var2Data",),
            "wave29-outline-auto-outdent": (
                "114/Props", "114/TBkndAssn/Fixed2Meta", "114/TBkndAssn/FixedMeta",
                "214/CV_iew/Var2Data",
            ),
            "wave29-outline-auto-summary": (
                "114/Props", "114/TBkndAssn/Fixed2Meta", "114/TBkndAssn/FixedMeta",
                "214/CV_iew/Var2Data",
            ),
            "wave29-outline-manual-parent": (
                "114/Props", "114/TBkndAssn/Fixed2Meta", "114/TBkndAssn/FixedMeta",
                "214/CV_iew/Var2Data",
            ),
            "wave29-percent-leaf": ("114/TBkndAssn/Fixed2Data",),
            "wave29-percent-mid25": ("114/TBkndAssn/Fixed2Data", "214/CV_iew/Var2Data"),
            "wave29-percent-milestone": ("114/TBkndAssn/Fixed2Data",),
            "wave29-percent-work": ("114/TBkndAssn/Fixed2Data",),
            "wave30-assign-add": ("114/TBkndRsc/FixedMeta",),
            "wave30-assign-delete": ("114/TBkndAssn/Fixed2Data", "114/TBkndRsc/FixedMeta"),
            "wave30-baseline": ("114/TBkndTask/Fixed2Data",),
            "wave30-inactivate-second": ("214/CEdl/Var2Data",),
            "wave30-level": ("114/TBkndAssn/Fixed2Data", "114/TBkndRsc/FixedMeta"),
            "wave30-resource-add": ("114/TBkndRsc/FixedMeta",),
            "wave30-resource-notes": ("114/TBkndRsc/FixedMeta",),
            "wave30-resource-rename": ("114/TBkndRsc/FixedMeta",),
            "wave30-respect-links-auto": ("114/TBkndAssn/Fixed2Data",),
            "wave30-respect-links-manual": ("114/TBkndAssn/Fixed2Data",),
            "wave30-task-mode-auto": ("214/CV_iew/Var2Data",),
            "wave30-wbs": ("114/TBkndTask/Fixed2Data",),
            "wave31-resource-sheet": (
                "114/Props", "114/TBkndAssn/Fixed2Meta", "114/TBkndAssn/FixedMeta",
                "114/TBkndRsc/FixedMeta", "114/TBkndTask/Fixed2Data", "114/TBkndTask/FixedMeta",
                "214/CV_iew/Var2Data", "214/Props",
            ),
            "wave31-tables": (
                "114/Props", "114/TBkndAssn/Fixed2Meta", "114/TBkndAssn/FixedMeta",
                "114/TBkndRsc/FixedMeta", "114/TBkndTask/Fixed2Data", "114/TBkndTask/FixedMeta",
                "214/CV_iew/Var2Data", "214/Props",
            ),
            "wave31-usage": (
                "114/Props", "114/TBkndAssn/Fixed2Meta", "114/TBkndAssn/FixedMeta",
                "114/TBkndRsc/FixedMeta", "114/TBkndTask/Fixed2Data", "114/TBkndTask/FixedMeta",
                "214/CV_iew/Var2Data", "214/Props",
            ),
            "wave34-assign-add-second": (
                "114/Props", "114/TBkndAssn/Fixed2Data", "114/TBkndAssn/Fixed2Meta",
                "114/TBkndAssn/FixedMeta",
            ),
            "wave34-assign-add-third": (
                "114/Props", "114/TBkndAssn/Fixed2Data", "114/TBkndAssn/Fixed2Meta",
                "114/TBkndAssn/FixedMeta", "Props14",
            ),
            "wave35-g3-assign-remove-port-saved": (
                "114/Props", "114/TBkndAssn/Fixed2Data", "114/TBkndAssn/Fixed2Meta",
                "114/TBkndAssn/FixedMeta", "114/TBkndRsc/FixedMeta", "214/CV_iew/Var2Data",
                "Props14",
            ),
            "wave35-viewstate-zoom-custom-narrow": ("214/CEdl/Var2Data",),
            "wave36-r7-calc-assign-1": ("114/TBkndAssn/Fixed2Data",),
            "wave36-r7-calc-assign-2": ("114/TBkndAssn/Fixed2Data",),
            "wave36-r7-dsi-total-work": ("114/TBkndAssn/Fixed2Data",),
            "wave36-r7-note-assignment": ("114/TBkndAssn/Fixed2Data",),
            "wave36-r7-table1-column-replace": ("214/CTable/FixedData",),
            "wave36-r7-table1-date-format": ("214/CTable/FixedData",),
            "wave36-r7-w37-b-assign-one": ("114/TBkndAssn/Fixed2Data",),
            "wave36-r7-w37-b-assign-two": ("114/TBkndAssn/Fixed2Data",),
            "wave36-r7-w37-b-rebaseline-changed": ("114/Props",),
            "wave36-r7-w37-b-rebaseline-same": ("114/Props",),
        }
        def stream_key(short):   # the gate's name of a stream: the storage prefix is three "?"
            return short if short == "Props14" else "???" + short

        OPEN = {}
        for stem in sorted(set(SIZE_OPEN) | set(MASKED_IN)):
            OPEN[stem] = dict(SIZE_OPEN.get(stem, {}))
            for sname in MASKED_IN.get(stem, ()):
                OPEN[stem][stream_key(sname)] = MASKED[sname]
        # two edits equal their control outside the noise (a no-op percent edit).  Before W36-0c.5
        # they PASSed the negative case because the control mask excused the view-bag bytes in which
        # their null differs from the control; now they FAIL it like every other edit, and the test
        # still shows that they equal their control
        NOOP_EDITS = {"wave28-percent-auto100": "no task, assignment or view byte differs from its "
                                                "control; only the path strings and their sizes",
                      "wave28-percent-auto40": "no task, assignment or view byte differs from its "
                                               "control; only the path strings and their sizes"}
        stems = sorted(f[:-len("-control-null.mpp")] for f in os.listdir(DATA)
                       if f.endswith("-control-null.mpp"))
        pos_n = neg_n = 0
        pos_failed, pos_kind, neg_passed, no_control = {}, {}, set(), []
        for stem in stems:
            c_path = control_path(stem)
            n_path = os.path.join(DATA, stem + "-control-null.mpp")
            e_path = os.path.join(DATA, stem + ".mpp")
            if c_path is None:
                no_control.append(stem)
                continue
            SC, SN = load(c_path), load(n_path)
            lines, problems, checked, unchecked = g.compare(SN, SC, SC, SN)
            pos_n += 1
            if problems:
                pos_failed[stem] = sorted({ln.split()[1] for ln in lines if len(ln.split()) > 1})
                pos_kind[stem] = {(ln.split()[0], ln.split()[1]) for ln in lines
                                  if len(ln.split()) > 1}
            if not os.path.exists(e_path):
                continue
            SE = load(e_path)
            if all(SE.get(n) == SN.get(n) for n in set(SE) | set(SN) if n != "?SummaryInformation"):
                continue
            neg_n += 1
            lines, problems, checked, unchecked = g.compare(SN, SE, SC, SN)
            if not problems:
                neg_passed.add(stem)
        print("W36-0c.1 positive: %d control-nulls, %d PASS, %d OPEN; negative: %d edits, %d FAIL, "
              "%d PASS" % (pos_n, pos_n - len(pos_failed), len(pos_failed), neg_n,
                                   neg_n - len(neg_passed), len(neg_passed)))
        check("W36-0c.1 every control-null has its control in tests/data",
              not no_control, "%r" % (no_control,))
        check("W36-0c.1 every control-null of tests/data was swept (%d)" % len(stems),
              pos_n == len(stems) and pos_n > 200)
        unexpected = {s: v for s, v in pos_failed.items() if s not in OPEN}
        check("W36-0c.1 positive sweep: every control-null PASSes except the OPEN list",
              not unexpected, "%r" % (unexpected,))
        stale = sorted(s for s in OPEN if s not in pos_failed)
        check("W36-0c.1 every OPEN pair still fails (a pair that passes leaves the list)",
              not stale, "%r" % (stale,))
        beyond = {s: sorted(set(v) - set(OPEN[s])) for s, v in pos_failed.items()
                  if s in OPEN and set(v) - set(OPEN[s])}
        check("W36-0c.1 an OPEN pair fails only in the streams its entry names",
              not beyond, "%r" % (beyond,))
        for s in sorted(OPEN):
            print("OPEN %s: %s" % (s, "; ".join("%s: %s" % (n, OPEN[s][n]) for n in
                                                  sorted(pos_failed.get(s, [])))))
        # W36-0c.5: the control mask minus the named fields is empty for every committed pair
        # except the OPEN ones, and for those it is exactly the streams MASKED_IN names.  In the
        # positive sweep every byte that differs lies under the control mask, so a stream that
        # fails by `bytes-differ` would be a byte the mask does not cover: none exists.
        masked_seen = {s: sorted(n for k, n in v if k == "control-mask")
                       for s, v in pos_kind.items()}
        masked_seen = {s: v for s, v in masked_seen.items() if v}
        check("W36-0c.5 the control mask minus the named fields is exactly the MASKED_IN streams "
              "(%d pairs)" % len(MASKED_IN),
              masked_seen == {s: sorted(stream_key(n) for n in v) for s, v in MASKED_IN.items()},
              "%r" % (sorted(set(masked_seen) ^ set(MASKED_IN)),))
        check("W36-0c.5 no positive pair fails by bytes-differ (the control mask is every byte)",
              not [s for s, v in pos_kind.items() if any(k == "bytes-differ" for k, n in v)],
              "%r" % ([s for s, v in pos_kind.items() if any(k == "bytes-differ" for k, n in v)],))
        check("W36-0c.5 every MASKED stream has a recorded reason",
              all(n in MASKED for v in MASKED_IN.values() for n in v))
        check("W36-0c.5 the negative sweep: no edit PASSes (the no-op edits fail on their null)",
              neg_passed == set(), "%r" % (sorted(neg_passed),))
        for stem in sorted(NOOP_EDITS):
            c_path = control_path(stem)
            e_path = os.path.join(DATA, stem + ".mpp")
            n_path = os.path.join(DATA, stem + "-control-null.mpp")
            lines, problems, checked, unchecked = g.compare(load(e_path), load(c_path),
                                                            load(c_path), load(n_path))
            check("W36-0b.2 %s: the no-op edit equals its control outside the noise (%s)" %
                  (stem, NOOP_EDITS[stem]), problems == 0, "%r" % (lines,))

        # --- W36-0c.5 (R36G0b-1): the review's seven inverted-byte files FAIL -----------------------
        # TRACED = the pair's edit with exactly the bytes at which its control and null differ, and
        # no field names, inverted; REFERENCE = the edit; the pair's own control and null.  Before
        # W36-0c.5 each was `0 problems`.  The byte counts are the review's (R36G0b-1 table), so a
        # field that is later named, and leaves the count short, shows here as a difference.
        INVERTED = (   # (pair stem, the pair's edit file, {stream: bytes inverted})
            ("wave30-baseline", "wave30-baseline-set", {"???114/TBkndTask/Fixed2Data": 12}),
            ("wave30-wbs", "wave30-wbs-code", {"???114/TBkndTask/Fixed2Data": 12}),
            ("wave29-percent-leaf", "wave29-percent-leaf40", {"???114/TBkndAssn/Fixed2Data": 5}),
            ("wave28-outline-summary", "wave28-outline-summary",
             {"???114/Props": 2, "???114/TBkndAssn/FixedMeta": 5, "???114/TBkndAssn/Fixed2Meta": 3}),
            ("wave30-resource-rename", "wave30-resource-rename",
             {"???114/TBkndRsc/FixedMeta": 2}),
            ("wave36-r7-table1-column-replace", "wave36-r7-table1-column-replace",
             {"???214/CTable/FixedData": 1}),
            ("wave28-mark-auto", "wave28-mark-auto", {"???214/CV_iew/Var2Data": 6}),
        )
        for stem, edit, expect in INVERTED:
            e_path, c_path = os.path.join(DATA, edit + ".mpp"), control_path(stem)
            n_path = os.path.join(DATA, stem + "-control-null.mpp")
            IE, IC, IN = load(e_path), load(c_path), load(n_path)
            bad = os.path.join(tmp, "inverted-%s.mpp" % stem)
            shutil.copyfile(e_path, bad)
            ole = olefile.OleFileIO(bad, write_mode=True)
            found = {}
            try:
                for sname in expect:
                    ids = (g.row_ids(sname, IC[sname], IC), g.row_ids(sname, IE[sname], IE)) \
                        if g._stride(sname) else None
                    mask = g.control_mask(sname, len(IE[sname]), IC[sname], IN[sname], ids) or set()
                    meta = IE.get(g.VAR_META[sname]) if sname in g.VAR_META else None
                    win = g.noise_windows(sname, IE[sname], meta, IC, IE[sname])
                    found[sname] = sorted(mask - win)
                    data = bytearray(IE[sname])
                    for k in found[sname]:
                        data[k] ^= 0xFF
                    ole.write_stream(stream_path(ole, sname), bytes(data))
            finally:
                ole.close()
            check("W36-0c.5 %s: the unnamed control/null bytes are the review's %r" %
                  (stem, tuple(expect.values())),
                  {s: len(v) for s, v in found.items()} == expect, "%r" % (found,))
            rc, out = run_cli(bad, e_path, c_path, n_path)
            check("W36-0c.5 %s: the inverted file FAILs, one control-mask line per stream" % stem,
                  rc == 1 and all(("control-mask %s %d" % (s, n)) in out for s, n in expect.items()),
                  out[:400])

        # --- W36-0b.7 (R36G0-8): a flip at EVERY offset outside the named noise fields fails -----------
        # The named fields are written out here from the evidence note (FIELDS rows 12 to 24), not
        # read back from the gate, so a window that grows past its field, or covers a row, a record
        # or a stream, is a difference between the two.  Then every offset outside the named fields
        # of every stream of three pairs is flipped (all at once per stream; the gate counts each
        # one, so "bytes-differ N" must be exactly the number flipped).
        ROW_SIZE = {"???114/TBkndAssn/Fixed2Data": 48, "???114/TBkndTask/Fixed2Data": 64,
                    "???114/TBkndRsc/Fixed2Data": 40, "???114/TBkndCal/Fixed2Data": 16,
                    "???114/TBkndCons/Fixed2Data": 16}
        ROW_GUIDS = {"???114/TBkndAssn/Fixed2Data": (0, 16), "???114/TBkndTask/Fixed2Data": (0, 24),
                     "???114/TBkndRsc/Fixed2Data": (0, 24), "???114/TBkndCal/Fixed2Data": (0,),
                     "???114/TBkndCons/Fixed2Data": (0,)}
        ABS_GUIDS = {"???214/CEdl/FixedData": (144,), "???214/CFilter/FixedData": (214, 444),
                     "???214/CGrouping/FixedData": (214, 444), "???214/CTable/FixedData": (214,),
                     "???214/CV_iew/FixedData": (122, 260, 398)}
        PROPS_WORDS, PROPS_GUIDS = (0x02400007, 0x024013B1), (0x02400029, 0x024013C2)

        def spec_windows(name, data, meta, ctl):
            """The offsets of the named noise fields of one stream, by the evidence note."""
            out = set()
            for row in range(len(data) // ROW_SIZE[name] if name in ROW_SIZE else 0):
                for lo in ROW_GUIDS[name]:
                    at = row * ROW_SIZE[name] + lo
                    if is_v1(data[at:at + 16]):
                        out.update(range(at, at + 16))
            for at in ABS_GUIDS.get(name, ()):
                if is_v1(data[at:at + 16]):
                    out.update(range(at, at + 16))
            if name == "???114/Props":
                for at, length, rid in records(data, 16):
                    if rid in PROPS_WORDS and length == 4:
                        out.update(range(at, at + 4))
                    if rid in PROPS_GUIDS and is_v1(data[at:at + length]):
                        out.update(range(at, at + 16))
            if name == "???214/Props":
                for at, length, rid in records(data, 16):
                    if rid == 0x03400001 and length >= 76:
                        out.update(at + 2 * i for i in range(38) if i not in (0, 9, 14, 19, 24, 37))
            if name == "???114/TBkndTask/FixedData":
                known = task_uids(ctl.get(name))
                for row in range(max(0, (len(data) - 48) // 202)):
                    at = 48 + 202 * row
                    if struct.unpack_from("<I", data, at)[0] not in known:
                        out.update(range(at + 128, at + 132))
            if name == "???114/TBkndAssn/Var2Data":
                known = {uid for uid, _, _ in blocks(ctl.get("???114/TBkndAssn/VarMeta"))}
                for uid, off, key in blocks(meta):
                    if key == 0x0F40027A and off != 0xFFFFFFFF and uid not in known:
                        out.update(range(off + 4, off + 8))
            if name == "???214/CV_iew/Var2Data":
                out.update(bag_font_bytes(data, meta))
            return out

        def window_audit(stem, only=None):
            """Problems found in the gate's windows of one pair, by name; the empty list is clean."""
            rp, cp, np_ = pair(stem)
            RS, CS = load(rp), load(cp)
            gate_canon = g.canonical(RS, with_noise=True)[1]
            canon_noise = {n_: spec_canon(n_, RS[n_], RS) for n_ in CANON_STREAMS if n_ in RS}
            found = []
            for n_ in CANON_STREAMS:
                if n_ in RS and gate_canon.get(n_, set()) != canon_noise[n_]:
                    got_, want_ = gate_canon.get(n_, set()), canon_noise[n_]
                    found.append("%s: the canonical form replaces %d bytes, the evidence note "
                                 "%d (%d extra, %d missing)" % (n_, len(got_), len(want_),
                                                                len(got_ - want_),
                                                                len(want_ - got_)))
            flipped_streams = 0
            for name in sorted(RS):
                data = RS[name]
                if not data or (only and name not in only):
                    continue
                meta = RS.get(g.VAR_META.get(name))
                got = g.noise_windows(name, data, meta, CS, data)
                want = spec_windows(name, data, meta, CS)
                if got != want:
                    found.append("%s: the windows are %d bytes, the named fields %d (%d extra, %d "
                                 "missing)" % (name, len(got), len(want), len(got - want),
                                               len(want - got)))
                    continue
                skip = want | canon_noise.get(name, set())
                flip = [i for i in range(len(data)) if i not in skip]
                if not flip and ROW_SIZE.get(name) != 16:
                    # only a 16-byte row that is one GUID (Cal, Cons Fixed2Data) is all field
                    found.append("%s: the noise covers the whole stream" % name)
                    continue
                if flip:
                    mutated = dict(RS)
                    blob = bytearray(data)
                    for i in flip:
                        blob[i] ^= 0xFF
                    mutated[name] = bytes(blob)
                    lines, problems, checked, unchecked = g.compare(mutated, RS, CS, dict(CS))
                    mine = [ln for ln in lines if name in ln]
                    if name in CANON_STREAMS:
                        # a flip that breaks the framing leaves the stream raw: the gate reports a
                        # size change, or at least the flipped bytes (the placeholders add a few)
                        ok = any(ln.startswith("size-differs") or (
                            ln.startswith("bytes-differ") and int(ln.split()[-1]) >= len(flip))
                            for ln in mine)
                    else:
                        ok = ("bytes-differ %s %d" % (name, len(flip))) in lines
                    if not ok:
                        found.append("%s: %d offsets outside the named fields were flipped, the "
                                     "gate says %r" % (name, len(flip), mine or "PASS"))
                # a GUID field is noise only as a version-1 GUID: break byte 6 of each
                starts = [row * ROW_SIZE[name] + lo for row in range(len(data) // ROW_SIZE[name])
                          for lo in ROW_GUIDS[name]] if name in ROW_SIZE else []
                starts += list(ABS_GUIDS.get(name, ()))
                starts = [s for s in starts if is_v1(data[s:s + 16])]
                if starts:
                    mutated = dict(RS)
                    blob = bytearray(data)
                    for s in starts:
                        blob[s + 6] ^= 0xFF
                    mutated[name] = bytes(blob)
                    lines, problems, checked, unchecked = g.compare(mutated, RS, CS, dict(CS))
                    if ("bytes-differ %s %d" % (name, len(starts))) not in lines:
                        found.append("%s: byte 6 of %d GUIDs changed, the gate says %r" %
                                     (name, len(starts), [ln for ln in lines if name in ln] or "PASS"))
                flipped_streams += 1
            return found, flipped_streams

        for stem in ("wave30-calculate", "wave34-assign-add-second",
                     "wave35-g3-insert-milestone-first"):
            found, flipped_streams = window_audit(stem)
            check("W36-0b.7 %s: every offset outside the named fields fails, in %d streams" %
                  (stem, flipped_streams), not found and flipped_streams > 50, "%r" % (found,))

        # the audit sees a window that is more than a named field: planted into the gate, each of
        # these is a window the old one-byte flip test could not see (it flipped the first free byte)
        def planted(name, extra):
            real = g.noise_windows

            def plant(n, data, meta=None, ctl=None, other=None):
                out = real(n, data, meta, ctl, other)
                return out | extra(data) if n == name else out
            return plant

        stem7 = "wave34-assign-add-second"
        for label, name, extra in (
                ("the whole stream but its first byte", "???114/TBkndTask/Var2Data",
                 lambda d: set(range(1, len(d)))),
                ("the whole stream", "???114/TBkndTask/FixedData", lambda d: set(range(len(d)))),
                ("every row but 4 bytes", "???114/TBkndAssn/Fixed2Data",
                 lambda d: {i for i in range(len(d)) if i % 48 not in (7, 8, 9, 10)}),
                ("one whole record", "???114/Props", lambda d: set(range(16, 16 + 12 + 128))),
                ("one byte of a stream that has no noise", "???114/TBkndRsc/FixedData",
                 lambda d: {5}),
                ("everything after byte 100", "???214/CTable/Var2Data",
                 lambda d: set(range(100, len(d))))):
            real = g.noise_windows
            g.noise_windows = planted(name, extra)
            try:
                found, _ = window_audit(stem7, only={name})
            finally:
                g.noise_windows = real
            check("W36-0b.7 a planted window over %s (%s) is found" % (name, label),
                  found != [], "%r" % (found,))

        # a stream missing from the traced save, and an extra one, are problems
        gone = dict(R)
        del gone["???114/TBkndTask/FixedData"]
        lines, problems, checked, unchecked = g.compare(gone, R, C, N)
        check("W36-0.4 a missing stream fails", problems == 1 and
              lines[0].startswith("only-in-reference"), "%r" % (lines,))
        extra = dict(R)
        extra["zzz/extra"] = b"x"
        lines, problems, checked, unchecked = g.compare(extra, R, C, N)
        check("W36-0.4 an extra stream fails", problems == 1 and
              any(ln.startswith("only-in-traced") for ln in lines), "%r" % (lines,))

        # --- W36-0c.8 (R36G0b-10): the head line's "not checked" is a real count ------------------
        # the streams that could not be compared byte by byte: in one file only, or another size
        lines, problems, checked, unchecked = g.compare(gone, R, C, N)
        check("W36-0c.8 a stream missing from the traced save is 1 not checked, the rest checked",
              unchecked == 1 and checked == len(R) - 1, "%r" % ((unchecked, checked, len(R)),))
        lines, problems, checked, unchecked = g.compare(extra, R, C, N)
        check("W36-0c.8 an extra stream is 1 not checked", unchecked == 1 and checked == len(R),
              "%r" % ((unchecked, checked),))
        longer = dict(R)
        longer["?SummaryInformation"] = R["?SummaryInformation"] + b"\0\0\0\0"
        lines, problems, checked, unchecked = g.compare(longer, R, C, N)
        check("W36-0c.8 a stream of another size is 1 not checked", unchecked == 1 and
              checked == len(R) - 1 and any(ln.startswith("size-differs") for ln in lines),
              "%r" % ((unchecked, checked, lines),))
        lines, problems, checked, unchecked = g.compare(R, R, C, N)
        check("W36-0c.8 a pair compared in full is 0 not checked", unchecked == 0 and
              checked == len(R), "%r" % ((unchecked, checked),))
        lines, problems, checked, unchecked = g.compare(R, R, C, None)
        check("W36-0c.8 no null: every stream is not checked", unchecked == len(R) and checked == 0,
              "%r" % ((unchecked, checked),))
        rc, out = run_cli(ref, ref, ctl, nul)
        check("W36-0c.8 the head line of a clean pair says 0 not checked",
              ", 0 not checked, 0 problems" in out.splitlines()[0], out[:200])

        # --- W36-0b.4 (R36G0-4): every mask names a field -------------------------------------------
        # The control mask is taken away (the control equals the reference, its null is a copy), so
        # only the named noise fields excuse a difference.  A fresh version-1 GUID, or a new save
        # word, in a named field is noise; one data byte next to it, a GUID that is not version 1,
        # a per-file identity record, and a stamp of a row the control already has are not.
        def v1(n):
            return bytes([n & 255, 0x11, 0x22, 0x33, 0x44, 0x55, 0x11, 0xF1, 0xB9, 0x5B, 0x00, 0x15,
                          0x5D, 1, 2, (n >> 8) & 255])

        def v4(n):
            return bytes([n & 255, 0x11, 0x22, 0x33, 0x44, 0x55, 0x41, 0xF1, 0xB9, 0x5B, 0x00, 0x15,
                          0x5D, 1, 2, (n >> 8) & 255])

        def put(streams_, name_, edits):
            out = dict(streams_)
            blob = bytearray(out[name_])
            for at, raw in edits:
                blob[at:at + len(raw)] = raw
            out[name_] = bytes(blob)
            return out

        def verdict(mutated, ref_, ctl_=None, name_=None):
            ctl_ = ref_ if ctl_ is None else ctl_
            lines_, problems_, checked_, unchecked_ = g.compare(mutated, ref_, ctl_, dict(ctl_))
            return problems_, lines_

        a_ref, a_ctl, a_nul = pair("wave34-assign-add-second")
        AR, AC = load(a_ref), load(a_ctl)
        guid_fields = 0
        for sname, rule in sorted(g.NOISE.items()):
            if sname not in AR:
                continue
            offsets = []
            if "guid_record" in rule:
                base, size = g.STRIDE[sname]
                offsets = [base + r * size + lo for r in range((len(AR[sname]) - base) // size)
                           for lo in rule["guid_record"]]
            offsets += [at for at in rule.get("guid", ())]
            offsets = [at for at in offsets if g._is_v1_guid(AR[sname][at:at + 16])]
            if not offsets:
                continue
            guid_fields += len(offsets)
            for which in (offsets[0], offsets[-1]):
                problems, lines = verdict(put(AR, sname, [(which, v1(40))]), AR)
                check("W36-0b.4 %s: a fresh version-1 GUID at +%d is noise" % (sname, which),
                      problems == 0, "%r" % (lines,))
            bad = put(AR, sname, [(offsets[0], v4(7))])
            problems, lines = verdict(bad, AR)
            check("W36-0b.4 %s: a GUID that is not version 1 fails" % sname,
                  problems >= 1 and any(sname in ln for ln in lines), "%r" % (lines,))
            for at in (offsets[0] + 16, offsets[0] - 1):
                if 0 <= at < len(AR[sname]) and not any(o <= at < o + 16 for o in offsets):
                    edit = put(AR, sname, [(at, bytes([AR[sname][at] ^ 1]))])
                    problems, lines = verdict(edit, AR)
                    check("W36-0b.4 %s: a data byte at +%d next to a GUID fails" % (sname, at),
                          problems >= 1 and any(sname in ln for ln in lines), "%r" % (lines,))
        check("W36-0b.4 the GUID classes found fields to test (%d)" % guid_fields, guid_fields > 20)

        # --- W36-0c.2 (R36G0b-4): a GUID field is noise only when BOTH sides are version-1 GUIDs ----
        # All zeros against a real GUID is a save that never wrote the GUID, so it FAILs, whichever
        # side holds the zeros.  (In the 196 committed control/null pairs no GUID is zero on one
        # side only: 5,426 are zero on both sides, which is equality, not noise.)
        zeroed = 0
        for sname, rule in sorted(g.NOISE.items()):
            if sname not in AR:
                continue
            offsets = []
            if "guid_record" in rule:
                base, size = g.STRIDE[sname]
                offsets = [base + r * size + lo for r in range((len(AR[sname]) - base) // size)
                           for lo in rule["guid_record"]]
            offsets += list(rule.get("guid", ()))
            for rid, cls in rule.get("props", {}).items():
                if cls == "guid":
                    offsets += [pos + 12 for pos, length, r_, kind in g.frame_records(AR[sname], 16)
                                if r_ == rid]
            offsets = [at for at in offsets if g._is_v1_guid(AR[sname][at:at + 16])]
            if not offsets:
                continue
            zeroed += len(offsets)
            z = put(AR, sname, [(at, bytes(16)) for at in offsets])
            for which, (tr, rf) in (("traced", (z, AR)), ("reference", (AR, z))):
                problems, lines = verdict(tr, rf)
                check("W36-0c.2 %s: every GUID zero in the %s fails (%d GUIDs)" %
                      (sname, which, len(offsets)),
                      problems >= 1 and any(sname in ln for ln in lines), "%r" % (lines,))
            one = put(AR, sname, [(offsets[0], bytes(16))])
            problems, lines = verdict(one, AR)
            check("W36-0c.2 %s: one GUID zero at +%d fails" % (sname, offsets[0]),
                  problems >= 1 and any(sname in ln for ln in lines), "%r" % (lines,))
        check("W36-0c.2 the zeroed GUIDs were found (%d)" % zeroed, zeroed > 20)
        # both sides zero is equality: it passes, nothing to mask
        both = put(AR, "???114/TBkndTask/Fixed2Data", [(0, bytes(16))])
        problems, lines = verdict(both, both)
        check("W36-0c.2 the same zeros on both sides pass", problems == 0, "%r" % (lines,))

        # the Assn Fixed2Data +32 window and the Task Fixed2Data +42/+46 bytes are data
        for sname, offs in (("???114/TBkndAssn/Fixed2Data", (32, 40, 42)),
                            ("???114/TBkndTask/Fixed2Data", (42, 46, 49))):
            for at in offs:
                row = put(AR, sname, [(at, bytes([AR[sname][at] ^ 1]))])
                problems, lines = verdict(row, AR)
                check("W36-0b.4 %s row 0 +%d is data: a flipped byte fails" % (sname, at),
                      problems >= 1 and any(sname in ln for ln in lines), "%r" % (lines,))

        # 114/Props: the save words and GUIDs are noise; the per-file identity is not
        props_n = "???114/Props"
        recs = {rid: (pos + 12, length) for pos, length, rid, kind in
                g.frame_records(AR[props_n], 16)}
        for rid, cls in sorted(g.NOISE[props_n]["props"].items()):
            at, length = recs[rid]
            new = bytes([AR[props_n][at] ^ 0x55]) * 4 if cls == "word" else v1(99)
            problems, lines = verdict(put(AR, props_n, [(at, new)]), AR)
            check("W36-0b.4 114/Props record %#x (%s) changed is noise" % (rid, cls),
                  problems == 0, "%r" % (lines,))
        for rid in (0x02401394, 0x02400003):
            at, length = recs[rid]
            problems, lines = verdict(put(AR, props_n, [(at, bytes([AR[props_n][at] ^ 1]))]), AR)
            check("W36-0b.4 114/Props record %#x is not noise: a flipped byte fails" % rid,
                  problems >= 1 and props_n in "".join(lines), "%r" % (lines,))
        # the view GUID string: the 32 hex digits are noise, the braces and high bytes are data
        vp = "???214/Props"
        at, length = {rid: (pos + 12, length) for pos, length, rid, kind in
                      g.frame_records(AR[vp], 16)}[0x03400001]
        digit = at + 2 * 1
        problems, lines = verdict(put(AR, vp, [(digit, b"7" if AR[vp][digit:digit + 1] != b"7"
                                                else b"8")]), AR)
        check("W36-0b.4 214/Props view GUID string: one hex digit changed is noise",
              problems == 0, "%r" % (lines,))
        for where, off in (("the opening brace", at), ("a digit's high byte", at + 2 * 1 + 1),
                           ("the first dash", at + 2 * 9), ("the byte after the string", at + 76)):
            problems, lines = verdict(put(AR, vp, [(off, bytes([AR[vp][off] ^ 1]))]), AR)
            check("W36-0b.4 214/Props view GUID string: %s is data, a flipped byte fails" % where,
                  problems >= 1 and vp in "".join(lines), "%r" % (lines,))

        # creation stamps: only a row or block the control does not have
        task_n = "???114/TBkndTask/FixedData"
        base, size = g.STRIDE[task_n]
        rows = (len(AR[task_n]) - base) // size
        last = base + (rows - 1) * size
        short = dict(AR)
        short[task_n] = AR[task_n][:last]                      # a control without the last row
        problems, lines = verdict(put(AR, task_n, [(last + 128, b"\x01\x02\x03\x04")]), AR, short)
        check("W36-0b.4 Task FixedData: the creation stamp of a row the control lacks is noise",
              problems == 0, "%r" % (lines,))
        problems, lines = verdict(put(AR, task_n, [(last + 132, b"\x01")]), AR, short)
        check("W36-0b.4 Task FixedData: the byte after the stamp is data and fails",
              problems >= 1, "%r" % (lines,))
        problems, lines = verdict(put(AR, task_n, [(base + size + 128, b"\x01\x02\x03\x04")]),
                                  AR, short)
        check("W36-0b.4 Task FixedData: the stamp of a row the control has fails",
              problems >= 1 and task_n in "".join(lines), "%r" % (lines,))
        var_n, meta_n2 = "???114/TBkndAssn/Var2Data", "???114/TBkndAssn/VarMeta"
        known = {uid for uid, _, _ in g._meta_blocks(AC[meta_n2])}
        stamps = [(uid, off) for uid, off, key in g._meta_blocks(AR[meta_n2])
                  if key == 0x0F40027A]
        fresh_s = [off + 4 for uid, off in stamps if uid not in known]
        old_s = [off + 4 for uid, off in stamps if uid in known]
        check("W36-0b.4 the Assn Var2Data case has a new block and an old one",
              fresh_s and old_s, "%r %r" % (fresh_s, old_s))
        problems, lines = verdict(put(AR, var_n, [(fresh_s[0], b"\xaa\xbb\xcc\xdd")]), AR, AC)
        check("W36-0b.4 Assn Var2Data: the stamp of an assignment the control lacks is noise",
              problems == 0, "%r" % (lines,))
        problems, lines = verdict(put(AR, var_n, [(old_s[0], b"\xaa\xbb\xcc\xdd")]), AR, AC)
        check("W36-0b.4 Assn Var2Data: the stamp of an assignment the control has fails",
              problems >= 1 and var_n in "".join(lines), "%r" % (lines,))

        # --- W36-0b.5 (R36G0-5): there is no ceiling on how many noise fields change -------------
        # A traced save in which EVERY named noise field of EVERY row has changed (every GUID is a
        # fresh version-1 GUID, every save word, view string, creation stamp and font byte differs)
        # is still a PASS; the old per-stream ceilings failed it as "over-ceiling".  One data byte
        # added to that same save FAILs, so nothing else was waived.
        W = {}                     # stream -> {offset: new bytes}

        def want(sname, at, raw):
            W.setdefault(sname, {})[at] = raw

        serial = [100]

        def fresh_guid():
            serial[0] += 1
            return v1(serial[0])

        for sname, rule in sorted(g.NOISE.items()):
            if sname not in AR:
                continue
            data = AR[sname]
            if "guid_record" in rule:
                base, size = g.STRIDE[sname]
                for r in range((len(data) - base) // size):
                    for lo in rule["guid_record"]:
                        at = base + r * size + lo
                        if g._is_v1_guid(data[at:at + 16]):
                            want(sname, at, fresh_guid())
            for at in rule.get("guid", ()):
                if g._is_v1_guid(data[at:at + 16]):
                    want(sname, at, fresh_guid())
            for pos, length, rid, kind in (g.frame_records(data, 16) or []) if "props" in rule else []:
                cls, at = rule["props"].get(rid), pos + 12
                if cls == "word":
                    want(sname, at, bytes([data[at] ^ 0x55]) + data[at + 1:at + 4])
                elif cls == "guid" and g._is_v1_guid(data[at:at + 16]):
                    want(sname, at, fresh_guid())
                elif cls == "guidstr":
                    for d in g._guidstr_digits(data[at:at + length]):
                        want(sname, at + d, b"7" if data[at + d:at + d + 1] != b"7" else b"8")
            if "new_row" in rule:
                base, size = g.STRIDE[sname]
                known_uids = g._task_uids(AC.get(sname))
                lo, hi = rule["new_row"]
                for r in range((len(data) - base) // size):
                    row = base + r * size
                    if g._u32(data, row) not in known_uids:
                        want(sname, row + lo, bytes(b ^ 0x33 for b in data[row + lo:row + hi]))
        for uid, off, key in g._meta_blocks(AR[meta_n2]):
            if key == 0x0F40027A and off != 0xFFFFFFFF and uid not in known:
                want(var_n, off + 4, bytes(b ^ 0x33 for b in AR[var_n][off + 4:off + 8]))
        cview = "???214/CV_iew/Var2Data"
        for at in bag_font_bytes(AR[cview], AR["???214/CV_iew/VarMeta"]):
            want(cview, at, bytes([AR[cview][at] ^ 0xFF]))
        every = dict(AR)
        n_fields = 0
        for sname, edits in W.items():
            blob = bytearray(every[sname])
            for at, raw in edits.items():
                blob[at:at + len(raw)] = raw
            every[sname] = bytes(blob)
            n_fields += len(edits)
        problems, lines = verdict(every, AR, AC)
        check("W36-0b.5 every noise field of every row changed (%d fields in %d streams) is a PASS"
              % (n_fields, len(W)), problems == 0 and n_fields > 50,"%r" % (lines,))
        data_byte = g.STRIDE[task_n][0] + 60              # a data byte of the first Task row
        plus = put(every, task_n, [(data_byte, bytes([every[task_n][data_byte] ^ 1]))])
        problems, lines = verdict(plus, AR, AC)
        check("W36-0b.5 the same save with one data byte changed fails",
              problems >= 1 and task_n in "".join(lines), "%r" % (lines,))
        check("W36-0b.5 the gate has no ceiling left",
              not hasattr(g, "ceiling_for") and not any("ceiling" in r for r in g.NOISE.values()))


        # a gate that checked nothing is not a pass
        lines, problems, checked, unchecked = g.compare({}, {}, C, N)
        check("W36-0.4 nothing checked: a problem 'nothing checked'",
              checked == 0 and problems == 1 and "nothing checked" in lines, "%r" % (lines,))

        # W36-0b.2: a path-bearing stream is compared field by field.  A longer path, with the
        # size words that follow from it, is noise; a stray appended byte, a size word that does
        # not follow from the length, or a changed data record is not.
        name, meta_n, props_n = "???214/CEdl/Var2Data", "???214/CEdl/VarMeta", "???214/Props"
        d = R[name]
        path = [r for r in g.frame_records(d, 20) if r[2] == g.CEDL_PATH_ID][0]
        new_payload = d[path[0] + 12:path[0] + 12 + path[1] - 4] + "x:\\longer\\".encode("utf-16le") + \
            b"!\0\0\0"
        grown = bytearray(d[:path[0]])
        grown += struct.pack("<III", len(new_payload), path[2], path[3]) + new_payload
        grown += d[path[0] + 12 + path[1] + (path[1] & 1):]
        struct.pack_into("<III", grown, 0, len(grown) - 4, len(grown) - 8, len(grown) - 8)
        mutated = dict(R)
        mutated[name] = bytes(grown)
        mm = bytearray(R[meta_n])
        struct.pack_into("<I", mm, 20, len(grown))
        mutated[meta_n] = bytes(mm)
        pp = bytearray(R[props_n])
        for pos, length, rid, kind in g.frame_records(R[props_n], 16):
            if rid == g.VIEW_CEDL_SIZE_ID:
                struct.pack_into("<I", pp, pos + 12, len(grown))
        mutated[props_n] = bytes(pp)
        lines, problems, checked, unchecked = g.compare(mutated, R, C, N)
        check("W36-0b.2 CEdl/Var2Data: a longer path with its derived size words is noise (%d->%d)"
              % (len(d), len(grown)), problems == 0, "%r" % (lines,))
        stray = dict(mutated)
        stray[name] = bytes(grown) + b"\0"
        lines, problems, checked, unchecked = g.compare(stray, R, C, N)
        check("W36-0b.2 CEdl/Var2Data: a stray appended byte fails", problems >= 1, "%r" % (lines,))
        bad_meta = dict(mutated)
        mm2 = bytearray(mm)
        mm2[20] ^= 1
        bad_meta[meta_n] = bytes(mm2)
        lines, problems, checked, unchecked = g.compare(bad_meta, R, C, N)
        check("W36-0b.2 CEdl/VarMeta: a size word that is not the stream length fails",
              any(meta_n in ln for ln in lines), "%r" % (lines,))
        data_rec = [r for r in g.frame_records(d, 20) if r[2] == 0x26400024][0]
        for where, nm, offset in (("CEdl sort table", name, data_rec[0] + 12),
                                  ("Props14 record 0x3540000a", "Props14",
                                   [r for r in g.frame_records(R["Props14"][:_u(R["Props14"]) + 4], 16)
                                    if r[2] == 0x3540000A][0][0] + 12)):
            mutated = dict(R)
            b = bytearray(R[nm])
            b[offset] ^= 0x01
            mutated[nm] = bytes(b)
            lines, problems, checked, unchecked = g.compare(mutated, R, C, N)
            check("W36-0b.2 %s: one flipped data byte fails" % where,
                  any(nm in ln for ln in lines), "%r" % (lines,))

        # --- W36-0c.3 (R36G0b-3): a SummaryInformation string slot is exactly its string --------------
        # A slot of Author, LastAuthor or RevNumber is accepted as noise only when its length is
        # 8 + the string length rounded up to 4 and its padding is zero.  Bytes after the string,
        # nonzero padding and padding of another length FAIL, whatever offsets and section size say.
        SI = "?SummaryInformation"

        def si_slots(blob):
            """(section offset, table pids in table order, [(pid, value slot)] in value order)."""
            sec = _u(blob[44:])
            count = struct.unpack_from("<I", blob, sec + 4)[0]
            ents = [struct.unpack_from("<II", blob, sec + 8 + 8 * i) for i in range(count)]
            order = sorted(ents, key=lambda e: e[1])
            ends = [e[1] for e in order[1:]] + [len(blob) - sec]
            return (sec, [pid for pid, off in ents],
                    [(pid, blob[sec + off:sec + end]) for (pid, off), end in zip(order, ends)])

        def si_build(blob, change):
            """The stream with the slots `change` {pid: new slot bytes} replaced, the offsets and
            the section size rebuilt to be consistent."""
            sec, table_pids, slots = si_slots(blob)
            slots = [(pid, change.get(pid, raw)) for pid, raw in slots]
            offs, cur = {}, 8 + 8 * len(slots)
            for pid, raw in slots:
                offs[pid] = cur
                cur += len(raw)
            table = b"".join(struct.pack("<II", pid, offs[pid]) for pid in table_pids)
            return blob[:sec] + struct.pack("<II", cur, len(slots)) + table + \
                b"".join(raw for _, raw in slots)

        def lpstr(text, pad=None):
            """A VT_LPSTR slot: type, length with its NUL, the bytes, zero padding to 4."""
            raw = text.encode("ascii") + b"\0"
            slot = struct.pack("<II", 0x1E, len(raw)) + raw
            return slot + (bytes((-len(slot)) % 4) if pad is None else pad)

        si_sec, si_table, si_have = si_slots(R[SI])
        check("W36-0c.3 the reference has Author, LastAuthor and RevNumber strings",
              all(p in si_table for p in (4, 8, 9)), "%r" % (si_table,))
        check("W36-0c.3 si_build with nothing changed is the stream itself",
              si_build(R[SI], {}) == R[SI])
        for pid, label in ((4, "Author"), (8, "LastAuthor"), (9, "RevNumber")):
            for text in ("a", "a longer value than the one saved, 33"):
                mutated = dict(R)
                mutated[SI] = si_build(R[SI], {pid: lpstr(text)})
                lines, problems, checked, unchecked = g.compare(mutated, R, C, N)
                check("W36-0c.3 %s replaced by a well-formed %d-byte string is noise" %
                      (label, len(text)), problems == 0, "%r" % (lines,))
            for what, slot in (
                    ("240 zero bytes after the string", lpstr("abc") + bytes(240)),
                    ("240 junk bytes after the string", lpstr("abc") + b"\xa5" * 240),
                    ("one nonzero padding byte", lpstr("abcd", b"\xff\0\0")),
                    ("zero padding of another length", lpstr("abcd", bytes(7)))):
                mutated = dict(R)
                mutated[SI] = si_build(R[SI], {pid: slot})
                lines, problems, checked, unchecked = g.compare(mutated, R, C, N)
                check("W36-0c.3 %s: %s fails" % (label, what),
                      problems >= 1 and any(SI in ln for ln in lines), "%r" % (lines,))

        # --- W36-0c.4 (R36G0b-2): the Props14 tail entry 0xffffffff is a decoded entry ----------------
        # The tail is [count][entry]...; an entry is [id][0x1e][UTF-16 name, no length].  The two
        # committed shapes are one entry (0x3540000b, a user name) and two (0xffffffff then
        # 0x3540000b, each a name).  The entry 0xffffffff is handled like 0x3540000b: its 8-byte
        # header is compared, only a well-formed text name is noise, and it may be absent on ONE
        # side only in the named case of a single remaining 0x3540000b entry with a text payload.
        # Junk in it, and a second one, FAIL.
        P14 = "Props14"

        def p14_split(blob):
            """(blob part before the tail, [(entry id, name bytes)]) by the test's own parse."""
            head = _u(blob) + 4
            tail = blob[head:]
            marks = [i for i in range(4, len(tail) - 7, 2)
                     if struct.unpack_from("<II", tail, i)[1] == 0x1E and
                     struct.unpack_from("<I", tail, i)[0] in (0xFFFFFFFF, 0x3540000B)]
            ends = marks[1:] + [len(tail)]
            return blob[:head], [(struct.unpack_from("<I", tail, m)[0], tail[m + 8:e])
                                 for m, e in zip(marks, ends)]

        def p14_join(head, entries, count=None):
            return head + struct.pack("<I", len(entries) if count is None else count) + \
                b"".join(struct.pack("<II", eid, 0x1E) + name for eid, name in entries)

        p14_head, p14_entries = p14_split(R[P14])
        check("W36-0c.4 the reference tail is the two entries 0xffffffff and 0x3540000b",
              [e[0] for e in p14_entries] == [0xFFFFFFFF, 0x3540000B] and
              p14_join(p14_head, p14_entries) == R[P14], "%r" % ([e[0] for e in p14_entries],))
        text_name = "z:\\other\\name".encode("utf-16le")
        junk = bytes([1, 0, 2, 0]) * 100        # control characters: not a text name

        def p14_verdict(label, entries, count=None, want_fail=True):
            mutated = dict(R)
            mutated[P14] = p14_join(p14_head, entries, count)
            lines, problems, checked, unchecked = g.compare(mutated, R, C, N)
            check("W36-0c.4 %s %s" % (label, "fails" if want_fail else "is noise"),
                  (problems >= 1 and any(P14 in ln for ln in lines)) if want_fail else problems == 0,
                  "%r" % (lines,))
            return lines

        first, second = p14_entries
        p14_verdict("a different well-formed text name in each entry",
                    [(first[0], text_name), (second[0], text_name * 2)], want_fail=False)
        p14_verdict("400 junk bytes in place of the 0xffffffff name", [(first[0], junk), second])
        p14_verdict("400 junk bytes in place of the 0x3540000b name", [first, (second[0], junk)])
        p14_verdict("a second 0xffffffff entry of 400 non-text bytes (count 3)",
                    [first, second, (0xFFFFFFFF, junk)])
        p14_verdict("a second 0xffffffff entry with a text name (count 3)",
                    [first, (0xFFFFFFFF, text_name), second])
        p14_verdict("a second 0xffffffff entry after the first, count unchanged",
                    [first, (0xFFFFFFFF, junk), second], count=2)
        p14_verdict("the 0xffffffff entry's 400 junk bytes with the count word 1",
                    [(first[0], junk), second], count=1)
        # absent on one side only: the named case
        p14_verdict("the 0xffffffff entry absent (one 0x3540000b entry, count 1)", [second],
                    want_fail=False)
        mutated = dict(R)
        absent = dict(R)
        absent[P14] = p14_join(p14_head, [second])
        lines, problems, checked, unchecked = g.compare(R, absent, C, N)
        check("W36-0c.4 the 0xffffffff entry present in the traced save only is noise",
              problems == 0, "%r" % (lines,))
        for label, entries in (("absent with a junk 0x3540000b name", [(second[0], junk)]),
                               ("alone with a junk name", [(first[0], junk)]),
                               ("absent, the remaining entry is 0xffffffff", [first]),
                               ("both absent", [])):
            p14_verdict("the 0xffffffff entry %s" % label, entries)

        # --- W36-0c.6 (R36G0b-6): the guards the review removed, one wrong file each --------------------
        # Each case is a file the real gate FAILs and a gate with the named guard removed PASSes
        # (the review's table, R36G0b-6).  Every guard was removed in a copy of the gate and the
        # case seen red (build/w36/mutants.py; logs build/w36/*-W36-0c.6-mutant-*.log).
        def swapped(streams_, name_, blob):
            out = dict(streams_)
            out[name_] = blob
            return out

        def fails(label, mutated, ref_, stream):
            problems_, lines_ = verdict(mutated, ref_)
            check("W36-0c.6 %s fails" % label,
                  problems_ >= 1 and any(stream in ln for ln in lines_), "%r" % (lines_,))

        def passes(label, mutated, ref_):
            problems_, lines_ = verdict(mutated, ref_)
            check("W36-0c.6 %s is noise" % label, problems_ == 0, "%r" % (lines_,))

        # SummaryInformation: Title, CreateTime and the rest are data; LastSave and the three
        # user-name strings are noise (trace-gate-fields-g0b.md, the SummaryInformation rows)
        si_have = dict(si_slots(R[SI])[2])
        check("W36-0c.6 the reference holds Title, CreateTime and LastSave slots",
              all(p in si_have for p in (2, 12, 13)) and len(si_have[12]) == 12 and
              len(si_have[13]) == 12, "%r" % (sorted(si_have),))
        title = bytearray(si_have[2])
        title[8] ^= 1
        fails("SummaryInformation Title changed by one byte",
              swapped(R, SI, si_build(R[SI], {2: bytes(title)})), R, SI)
        fails("SummaryInformation Title replaced by a longer string",
              swapped(R, SI, si_build(R[SI], {2: lpstr("another title altogether")})), R, SI)
        created = bytearray(si_have[12])
        created[5] ^= 1
        fails("SummaryInformation CreateTime changed",
              swapped(R, SI, si_build(R[SI], {12: bytes(created)})), R, SI)
        saved = bytearray(si_have[13])
        saved[5] ^= 1
        passes("SummaryInformation LastSave changed",
               swapped(R, SI, si_build(R[SI], {13: bytes(saved)})), R)
        for pid, label, at in ((18, "the application name", -1), (1, "the code page", 4),
                               (19, "property 19", -1)):
            slot = bytearray(si_have[pid])
            slot[at] ^= 1
            fails("SummaryInformation %s changed" % label,
                  swapped(R, SI, si_build(R[SI], {pid: bytes(slot)})), R, SI)

        # CEdl path record: printable text ending "!" and NUL; anything else is compared as it is
        cedl, view_props = "???214/CEdl/Var2Data", "???214/Props"
        cd6 = R[cedl]
        p_at, p_len = [(pos, length) for pos, length, rid in records(cd6, 20)
                       if rid == 0x26400031][0]
        ctl_chars = bytes([1, 0, 2, 0]) * 10
        fails("CEdl path record: 20 control characters in the path",
              put(R, cedl, [(p_at, ctl_chars)]), R, cedl)
        fails("CEdl path record: printable text that does not end with the exclamation mark",
              put(R, cedl, [(p_at + p_len - 4, b"?\0\0\0")]), R, cedl)
        for at in (0, 4, 8):
            fails("CEdl size word +%d off by one" % at,
                  put(R, cedl, [(at, struct.pack("<I", struct.unpack_from("<I", cd6, at)[0] + 1))]),
                  R, cedl)
        # 214/Props record 0x00010007 is the length of the CEdl stream: another value is data
        vp_at = [pos for pos, length, rid in records(R[view_props], 16) if rid == 0x00010007][0]
        check("W36-0c.6 214/Props record 0x00010007 holds the CEdl stream length",
              struct.unpack_from("<I", R[view_props], vp_at)[0] == len(cd6))
        fails("214/Props record 0x00010007 that is not the CEdl length",
              put(R, view_props, [(vp_at, struct.pack("<I", len(cd6) + 2))]), R, view_props)

        # Props14 path record 0x35400008: control characters
        p14_path = [pos for pos, length, rid in records(R[P14][:_u(R[P14]) + 4], 16)
                    if rid == 0x35400008][0]
        fails("Props14 path record: 20 control characters in the path",
              put(R, P14, [(p14_path, ctl_chars)]), R, P14)

        # 114/Props save GUIDs: a version-4 GUID is not noise, wherever the record is
        props114 = "???114/Props"
        v4_cases = 0
        for rid in sorted(g.NOISE[props114]["props"]):
            hits = [(pos, length) for pos, length, r_ in records(R[props114], 16) if r_ == rid]
            if hits and hits[0][1] == 16:
                v4_cases += 1
                fails("114/Props record %#x holding a version-4 GUID" % rid,
                      put(R, props114, [(hits[0][0], v4(5))]), R, props114)
        check("W36-0c.6 114/Props holds the two save GUID records (%d)" % v4_cases, v4_cases == 2)

        # the view GUID string is noise only when BOTH sides hold a GUID string
        vg_at = [pos for pos, length, rid in records(R[view_props], 16) if rid == 0x03400001][0]
        z = b"Z" if R[view_props][vg_at + 2:vg_at + 3] != b"Z" else b"Y"
        fails("214/Props view GUID string: a hex digit that is not hex, in the traced save",
              put(R, view_props, [(vg_at + 2, z)]), R, view_props)
        fails("214/Props view GUID string: a hex digit that is not hex, in the reference",
              R, put(R, view_props, [(vg_at + 2, z)]), view_props)
        fails("214/Props view GUID string: the traced save holds text, the reference a GUID",
              put(R, view_props, [(vg_at, "x".encode("utf-16le") * 38)]), R, view_props)

        # the font byte is payload +43 of a 44-byte row of the font item 0x2240000c, kind 1, only
        fview, fmeta = "???214/CV_iew/Var2Data", "???214/CV_iew/VarMeta"
        font_recs = [(at, length, rid, kind) for at, length, rid, kind in
                     bag_records(FR[fview], FR[fmeta]) if rid == 0x2240000C]
        check("W36-0c.6 the bold-off edit's bag holds one font item of 44-byte rows",
              len(font_recs) == 1 and font_recs[0][1] % 44 == 0 and font_recs[0][3] == 1,
              "%r" % (font_recs,))
        f_at = font_recs[0][0]
        for label, hdr_at, other in (("another record id", f_at - 8, struct.pack("<I", 0x2240000D)),
                                     ("kind 2", f_at - 4, struct.pack("<I", 2))):
            ref_f = put(FR, fview, [(hdr_at, other)])
            edit_f = put(ref_f, fview, [(f_at + 43, bytes([ref_f[fview][f_at + 43] ^ 0xFF]))])
            problems_, lines_ = verdict(edit_f, ref_f, FC)
            check("W36-0c.6 a 44-byte-row record of %s is no font item: its +43 byte fails" % label,
                  problems_ >= 1 and any(fview in ln for ln in lines_), "%r" % (lines_,))
        # W36-0.2: a mask is built only from equal-length control and null, and per record
        # for a fixed-stride stream
        stride_name = "???114/TBkndTask/Fixed2Data"
        ctl_s = bytearray(128)
        nul_s = bytearray(128)
        nul_s[64 + 60] = 1                       # row 1, +60: noise in the control
        ref_len = 192                            # the reference has a third row
        mask = g.control_mask(stride_name, ref_len, bytes(ctl_s), bytes(nul_s),
                              (["a", "b"], ["a", "b", "c"]))
        check("W36-0.2 stride stream: the control's noise is masked in its own record",
              mask is not None and (64 + 60) in mask)
        check("W36-0.2 stride stream: the new row carries no control mask",
              mask is not None and not any(i >= 128 for i in mask))
        other = "???114/TBkndTask/Var2Data"
        check("W36-0.2 non-stride stream of another length: mask refused",
              g.control_mask(other, 200, bytes(ctl_s), bytes(nul_s)) is None)
        check("W36-0.2 control and null of different lengths: mask refused",
              g.control_mask(stride_name, 128, bytes(ctl_s), bytes(ctl_s) + b"\0") is None)
        check("W36-0.2 same-length reference: offsets map one to one",
              g.control_mask(other, 128, bytes(ctl_s), bytes(nul_s)) == {124})

        # --- W36-0b.8 (R36G0-9): a stride row is masked by its identity, not its position ----------
        # unit: a row inserted before the control's second row moves it from row 1 to row 2
        ctl_u = bytes(128)
        nul_u = bytearray(128)
        nul_u[64 + 60] = 1                       # noise in the control's row "b"
        ids_u = (["a", "b"], ["a", "new", "b"])
        mask = g.control_mask(stride_name, 192, ctl_u, bytes(nul_u), ids_u)
        check("W36-0b.8 unit: the noise of row b follows it to row 2 and not row 1",
              mask == {128 + 60}, "%r" % (mask,))
        check("W36-0b.8 unit: no row identities and another length: refused",
              g.control_mask(stride_name, 192, ctl_u, bytes(nul_u)) is None)
        check("W36-0b.8 unit: undecoded reference rows: refused",
              g.control_mask(stride_name, 192, ctl_u, bytes(nul_u), (["a", "b"], None)) is None)
        check("W36-0b.8 unit: a row the reference lacks has no mask",
              g.control_mask(stride_name, 128, ctl_u, bytes(nul_u), (["a", "b"], ["a", "c"])) == set())
        check("W36-0b.8 unit: a row named twice has no mask",
              g.control_mask(stride_name, 128, ctl_u, bytes(nul_u), (["a", "b"], ["b", "b"])) == set())
        check("W36-0b.8 unit: same length, rows swapped: the mask follows the row",
              g.control_mask(stride_name, 128, ctl_u, bytes(nul_u), (["a", "b"], ["b", "a"])) == {60})

        # the committed middle-insert pair: row identities of the control and of the edit
        m_ref, m_ctl, m_nul = pair("wave35-g3-insert-milestone-first")
        MR, MC, MN = load(m_ref), load(m_ctl), load(m_nul)
        t2 = "???114/TBkndTask/Fixed2Data"
        cids, rids = g.row_ids(t2, MC[t2], MC), g.row_ids(t2, MR[t2], MR)
        check("W36-0b.8 Task Fixed2Data rows are named on both sides (3 header rows + a row per uid)",
              cids is not None and rids is not None and len(cids) == 10 and len(rids) == 11 and
              sum(i[0] == "hdr" for i in cids) == 3 and len(set(rids)) == 11, "%r %r" % (cids, rids))
        moved = [(r, rids.index(i)) for r, i in enumerate(cids) if i in rids and rids.index(i) != r]
        new_rows = [r for r, i in enumerate(rids) if i not in cids]
        check("W36-0b.8 the insert moves control rows and adds one new row", len(moved) > 0 and
              len(new_rows) == 1, "%r %r" % (moved, new_rows))
        win = g.noise_windows(t2, MR[t2], None, MC, MR[t2])
        free = [k for k in range(64) if all(r * 64 + k not in win for r in range(11))]
        check("W36-0b.8 a row byte outside every noise window exists", len(free) > 0)
        if moved and new_rows and free:
            r0, s0 = moved[0]
            k0 = free[0]
            N2 = dict(MN)
            nb = bytearray(MN[t2])
            nb[r0 * 64 + k0] ^= 0x5A             # the control's noise, in row r0 of the control
            N2[t2] = bytes(nb)

            def traced_with(row):
                out = dict(MR)
                tb = bytearray(MR[t2])
                tb[row * 64 + k0] ^= 0x5A
                out[t2] = bytes(tb)
                return out

            # W36-0c.5: the mask excuses nothing, so the control's noise seen in the same row of the
            # edit FAILs, and the line says it lies under the mask (it follows its row by identity);
            # the same byte at the control's POSITION in another row, or in the inserted row, is not
            # under any mask and FAILs as a plain difference.
            lines, problems, checked, unchecked = g.compare(traced_with(s0), MR, MC, N2)
            check("W36-0b.8 noise in the control's row, seen in the same row of the edit, is "
                  "reported as control-mask and fails",
                  problems == 1 and lines == ["control-mask %s 1" % t2], "%r" % (lines,))
            lines, problems, checked, unchecked = g.compare(traced_with(r0), MR, MC, N2)
            check("W36-0b.8 the same noise at the control's POSITION in the edit (another row) "
                  "fails as bytes-differ, not under the mask",
                  problems == 1 and lines == ["bytes-differ %s 1" % t2], "%r" % (lines,))
            lines, problems, checked, unchecked = g.compare(traced_with(new_rows[0]), MR, MC, N2)
            check("W36-0b.8 the same noise in the inserted row fails as bytes-differ (no mask for "
                  "a new row)", problems == 1 and lines == ["bytes-differ %s 1" % t2], "%r" % (lines,))

        # row identities of the other stride streams, on the committed pairs that add a row
        for stem, sname, grown in (("wave34-assign-add-second", "Assn/Fixed2Data", 1),
                                   ("wave34-assign-add-second", "Assn/FixedData", 1),
                                   ("wave30-resource-add", "Rsc/Fixed2Data", 1),
                                   ("wave30-resource-add", "Cal/Fixed2Data", 3),
                                   ("wave34-link-add", "Cons/Fixed2Data", 3),
                                   ("wave35-g3-insert-milestone-first", "Task/FixedData", 1)):
            sref, sctl, snul = pair(stem)
            SR_, SC_ = load(sref), load(sctl)
            full = "???114/TBknd" + sname
            ci, ri = g.row_ids(full, SC_.get(full, b""), SC_), g.row_ids(full, SR_[full], SR_)
            check("W36-0b.8 %s of %s: rows named, %d more rows, the control's rows all remain" %
                  (sname, stem, grown),
                  ci is not None and ri is not None and len(ri) == len(ci) + grown and
                  len(set(ri)) == len(ri) and all(i in ri for i in ci), "%r %r" % (ci, ri))

        # every committed file: every stride stream's rows are named (or the stream is empty)
        undecoded = []
        for fn in sorted(os.listdir(DATA)):
            if not fn.endswith(".mpp"):
                continue
            SF = load(os.path.join(DATA, fn))
            for sname in g.STRIDE:
                if sname in SF and g.row_ids(sname, SF[sname], SF) is None:
                    undecoded.append((fn, sname))
        check("W36-0b.8 every stride stream of every committed file has named rows",
              not undecoded, "%r" % (undecoded[:5],))

        # --- W36-0d.1 (R36G0c-1): a placeholder never equals a value a real field can hold ------------
        # The canonical form replaces a derived size word, and the CEdl path record, with a
        # placeholder of zeros.  A traced word that is itself 0 (and wrong) used to equal it.  Each
        # derived field is now named, so a field derived on one side only is `derived <stream> <field>`.
        cedl, cmeta, vprops = "???214/CEdl/Var2Data", "???214/CEdl/VarMeta", "???214/Props"
        rec_at = {rid: pos for pos, length, rid, kind in g.frame_records(R[vprops], 16)}
        check("W36-0d.1 the pair's five derived words are not 0",
              all(struct.unpack_from("<I", R[cedl], at)[0] != 0 for at in (0, 4, 8)) and
              struct.unpack_from("<I", R[cmeta], 20)[0] != 0 and
              struct.unpack_from("<I", R[vprops], rec_at[0x00010007] + 12)[0] != 0)
        for label, sname, at in (("CEdl Var2Data +0", cedl, 0), ("CEdl Var2Data +4", cedl, 4),
                                 ("CEdl Var2Data +8", cedl, 8), ("CEdl VarMeta +20", cmeta, 20),
                                 ("214/Props record 0x00010007", vprops,
                                  rec_at[0x00010007] + 12)):
            zero = put(R, sname, [(at, bytes(4))])
            for which, (tr, rf) in (("traced", (zero, R)), ("reference", (R, zero))):
                problems_, lines_ = verdict(tr, rf)
                check("W36-0d.1 %s set to 0 in the %s fails, and names the field as derived on one "
                      "side only" % (label, which),
                      problems_ >= 1 and any(ln.startswith("derived " + sname) for ln in lines_),
                      "%r" % (lines_,))
            both = put(R, sname, [(at, bytes(4))])
            problems_, lines_ = verdict(both, both)
            check("W36-0d.1 %s set to 0 on both sides is equality and passes" % label,
                  problems_ == 0, "%r" % (lines_,))

        def emptied_path(streams_):
            """The CEdl path record emptied (length 0, no payload), with the three size words, the
            VarMeta length and 0x00010007 all made consistent with the shorter stream."""
            d_ = streams_[cedl]
            p_at, p_len = [(pos - 12, length) for pos, length, rid in records(d_, 20)
                           if rid == 0x26400031][0]
            kept = d_[:p_at] + struct.pack("<III", 0, 0x26400031, struct.unpack_from("<I", d_, p_at + 8)[0]) + \
                d_[p_at + 12 + p_len + (p_len & 1):]
            kept = bytearray(kept)
            struct.pack_into("<III", kept, 0, len(kept) - 4, len(kept) - 8, len(kept) - 8)
            out_ = dict(streams_)
            out_[cedl] = bytes(kept)
            mm_ = bytearray(streams_[cmeta])
            struct.pack_into("<I", mm_, 20, len(kept))
            out_[cmeta] = bytes(mm_)
            pp_ = bytearray(streams_[vprops])
            struct.pack_into("<I", pp_, rec_at[0x00010007] + 12, len(kept))
            out_[vprops] = bytes(pp_)
            return out_

        emptied = emptied_path(R)
        check("W36-0d.1 the emptied-path file is shorter and its path record has no payload",
              len(emptied[cedl]) < len(R[cedl]))
        for which, (tr, rf) in (("traced", (emptied, R)), ("reference", (R, emptied))):
            problems_, lines_ = verdict(tr, rf)
            check("W36-0d.1 an emptied CEdl path record in the %s fails (derived on one side only)"
                  % which, problems_ >= 1 and ("derived %s path" % cedl) in lines_, "%r" % (lines_,))
        problems_, lines_ = verdict(emptied, emptied)
        check("W36-0d.1 the same emptied path on both sides is equality and passes",
              problems_ == 0, "%r" % (lines_,))
        # the Props14 path record: an empty path is not a path
        p14_at, p14_len = [(pos - 12, length) for pos, length, rid in
                           records(R["Props14"][:_u(R["Props14"]) + 4], 16) if rid == 0x35400008][0]
        p14_blob = R["Props14"]
        p14_empty = bytearray(p14_blob[:p14_at] + struct.pack("<III", 0, 0x35400008, 0) +
                              p14_blob[p14_at + 12 + p14_len + (p14_len & 1):])
        struct.pack_into("<II", p14_empty, 0, len(p14_empty) - len(p14_blob) + _u(p14_blob),
                         len(p14_empty) - len(p14_blob) + _u(p14_blob))
        emptied14 = dict(R)
        emptied14["Props14"] = bytes(p14_empty)
        for which, (tr, rf) in (("traced", (emptied14, R)), ("reference", (R, emptied14))):
            problems_, lines_ = verdict(tr, rf)
            check("W36-0d.1 an empty Props14 path record in the %s fails" % which,
                  problems_ >= 1 and any(ln.endswith("Props14") or "Props14 " in ln for ln in lines_),
                  "%r" % (lines_,))

        # W36-0.3 at the function level
        lines, problems, checked, unchecked = g.compare(R, R, C, None)
        check("W36-0.3 compare with no null: one problem 'no null'",
              problems == 1 and lines == ["no null"], "%r" % (lines,))
finally:
    shutil.rmtree(tmp, ignore_errors=True)

print("%d failed" % len(failures) if failures else "all passed")
sys.exit(1 if failures else 0)
