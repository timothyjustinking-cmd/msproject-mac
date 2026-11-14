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

  W36-0d   a derived field is told from a raw one (0d.1); streams are keyed by their raw name (0d.2);
           the view GUID string is a version-1 GUID (0d.3); the string-slot type test, the Props14
           first entry and size words and the 2048-byte cap are seen red (0d.4, 0d.5); a masked field
           holds a value a save writes (0d.6); each OPEN entry is pinned as (pair, stream, count)
           (0d.7); and test_gate_guards_meta.py removes each named guard in turn and asserts this
           test goes red, run at the end of a default run (0d.8).

  W36-0e   the meta-test derives its candidate guards from the gate (0e.1) and this test turns a gate
           that raises, or a gate that returns the wrong shape, into a named FAIL, so a removal is red
           by a verdict and not by a crash; four guards the G0d review found green (the Props14 tail
           count word, the SummaryInformation section size, the first value after the table, the
           LastSave type word) have cases of their own (0e.1); a masked value is accepted only inside
           the range the committed saves show: a stamp of 1 or 0x7fffffff, two unequal Props save
           words and a LastSave in 2001 or 2600 FAIL (0e.2); an emptied Props14 tail name is not a
           name, so two emptied names do not equal their placeholder and FAIL (0e.3).

    python test_trace_gate.py [--gate PATH | --gate-dir DIR] [--cli-only] [--no-meta] [--fail-fast]

--gate runs the command-line cases against another copy of the gate (a red run against an older
version); --gate-dir runs every case, in-process ones too, against DIR/trace_gate.py (a gate
with one guard removed); --cli-only skips the cases that import the gate's functions; --no-meta
does not run the guard meta-test after a pass (the meta-test itself runs this test with it);
--fail-fast stops at the first FAIL.  Exit 0 all pass, 1 a case failed, 77 olefile is not
installed (ctest SKIP).
"""

import datetime
import os
import io
import re
import shutil
import struct
import types
import uuid
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
no_meta = False
fail_fast = False
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
    elif a == "--no-meta":
        no_meta = True
    elif a == "--fail-fast":
        fail_fast = True
    else:
        print(__doc__)
        sys.exit(2)

failures = []


def check(label, ok, detail=""):
    print(("PASS " if ok else "FAIL ") + label + ((" -- " + detail) if detail and not ok else ""))
    if not ok:
        failures.append(label)
        if fail_fast:
            print("1 failed (--fail-fast)")
            sys.exit(1)


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
    if "Traceback (most recent call last)" in p.stderr:   # a raise is a finding, not a verdict
        check("gate raised on the command line: %s" % (p.stderr.strip().splitlines() or [""])[-1][:80],
              False)
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

        # A gate that raises on a file is a failed case named "gate raised", not a crash of this test
        # (W36-0e.1): the meta-test tells a removal that a case catches from one that only kills the
        # run.  The gate must FAIL a damaged file cleanly, never raise on it (see the damage sweep).
        _compare = g.compare

        def _compare_guarded(*a, **k):
            try:
                return _compare(*a, **k)
            except Exception as exc:   # noqa: BLE001 - any exception is the finding
                check("gate raised %s in compare: %s" % (type(exc).__name__, str(exc)[:60]), False)
                return ["gate raised"], 1, 0, 0
        g.compare = _compare_guarded

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
            "114/TBkndAssn/Fixed2Data": "row +32..+37 and +41: a per-save value nobody decoded "
                                        "(a control and its null differ there with no edit); masking "
                                        "it would also pass an edit of the real field",
            "114/TBkndAssn/Fixed2Meta": "meta items at +8, +20, +68..+78, +121..+131, +174; not decoded",
            "114/TBkndAssn/FixedMeta": "meta items at +4, +8, +16..+20, +50..+54, +86..+97; not decoded",
            "114/TBkndRsc/FixedMeta": "meta items at +176 and +213; not decoded",
            "114/TBkndTask/Fixed2Data": "row +42 and +46..+48: per-save values nobody decoded (a "
                                        "control and its null differ there with no edit, e.g. wave30-"
                                        "baseline +42 00 -> 20 and +46..+48 c0 12 f2 -> 78 1e f1); "
                                        "masking them would also pass an edit of the real field",
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
        # W36-0d.7 (R36G0c-7): each OPEN entry is pinned as (pair, stream, what the gate prints for
        # it): `control-mask <n>` (the count of bytes that differ with no field naming them) or
        # `size-differs <a>-><b>`.  A new undecoded byte in a stream a pair already lists changes its
        # count, and the pin fails.  Generated by build/w36/g0d_open.py from the sweep below as of
        # 14 November 2026: 170 rows (122 control-mask, 48 size-differs) in 56 pairs.
        OPEN_PINNED = (
    ('wave23-flags', '???114/Props', 'control-mask 4'),
    ('wave23-flags', '???114/TBkndAssn/Fixed2Data', 'control-mask 7'),
    ('wave23-flags', '???114/TBkndAssn/Fixed2Meta', 'control-mask 15'),
    ('wave23-flags', '???114/TBkndAssn/FixedMeta', 'control-mask 30'),
    ('wave23-flags', '???114/TBkndAssn/Var2Data', 'size-differs 460->520'),
    ('wave23-flags', '???114/TBkndAssn/VarMeta', 'size-differs 144->180'),
    ('wave23-flags', '???114/TBkndTask/Fixed2Data', 'control-mask 4'),
    ('wave23-flags', '???214/CEdl/Var2Data', 'size-differs 588->600'),
    ('wave23-flags', '???214/CFilter/FixedData', 'size-differs 230->460'),
    ('wave23-flags', '???214/CFilter/FixedMeta', 'size-differs 26->36'),
    ('wave23-flags', '???214/CFilter/Var2Data', 'size-differs 108->224'),
    ('wave23-flags', '???214/CFilter/VarMeta', 'size-differs 36->48'),
    ('wave23-flags', '???214/CV_iew/Var2Data', 'control-mask 107'),
    ('wave23-flags', '???214/Props', 'control-mask 2'),
    ('wave23-flags', 'Props14', 'size-differs 428->416'),
    ('wave27-percent-actuals', '???114/Props', 'control-mask 1'),
    ('wave27-percent-actuals', '???114/TBkndAssn/Fixed2Meta', 'control-mask 3'),
    ('wave27-percent-actuals', '???114/TBkndAssn/FixedMeta', 'control-mask 3'),
    ('wave27-percent-actuals', '???114/TBkndAssn/Var2Data', 'size-differs 336->396'),
    ('wave27-percent-actuals', '???114/TBkndAssn/VarMeta', 'size-differs 96->132'),
    ('wave27-percent-actuals', '???114/TBkndRsc/FixedMeta', 'control-mask 1'),
    ('wave27-percent-actuals', '???114/TBkndTask/Fixed2Data', 'control-mask 4'),
    ('wave27-percent-actuals', '???114/TBkndTask/FixedMeta', 'control-mask 1'),
    ('wave27-percent-actuals', '???214/CEdl/Var2Data', 'size-differs 474->486'),
    ('wave27-percent-actuals', '???214/CFilter/FixedData', 'size-differs 230->460'),
    ('wave27-percent-actuals', '???214/CFilter/FixedMeta', 'size-differs 26->36'),
    ('wave27-percent-actuals', '???214/CFilter/Var2Data', 'size-differs 108->224'),
    ('wave27-percent-actuals', '???214/CFilter/VarMeta', 'size-differs 36->48'),
    ('wave27-percent-actuals', '???214/CV_iew/Var2Data', 'control-mask 106'),
    ('wave27-percent-actuals', '???214/Props', 'control-mask 2'),
    ('wave27-percent-actuals', 'Props14', 'size-differs 428->416'),
    ('wave27-priority-750', '???214/CEdl/Var2Data', 'size-differs 422->486'),
    ('wave27-priority-750', '???214/CV_iew/Var2Data', 'control-mask 4'),
    ('wave27-unlink', '???114/TBkndAssn/Fixed2Data', 'control-mask 7'),
    ('wave27-unlink', '???214/CEdl/Var2Data', 'size-differs 536->600'),
    ('wave27-unlink', '???214/CV_iew/Var2Data', 'control-mask 4'),
    ('wave28-mark-auto', '???214/CV_iew/Var2Data', 'control-mask 6'),
    ('wave28-mark-manual', '???214/CV_iew/Var2Data', 'control-mask 4'),
    ('wave28-newtask-auto', '???214/CEdl/Var2Data', 'size-differs 422->486'),
    ('wave28-newtask-auto', '???214/CV_iew/Var2Data', 'control-mask 4'),
    ('wave28-newtask-manual', '???214/CEdl/Var2Data', 'size-differs 422->486'),
    ('wave28-newtask-manual', '???214/CV_iew/Var2Data', 'control-mask 4'),
    ('wave28-outline-indent', '???214/CV_iew/Var2Data', 'control-mask 4'),
    ('wave28-outline-outdent', '???114/Props', 'control-mask 2'),
    ('wave28-outline-outdent', '???114/TBkndAssn/Fixed2Meta', 'control-mask 4'),
    ('wave28-outline-outdent', '???114/TBkndAssn/FixedMeta', 'control-mask 6'),
    ('wave28-outline-outdent', '???214/CV_iew/Var2Data', 'control-mask 4'),
    ('wave28-outline-summary', '???114/Props', 'control-mask 2'),
    ('wave28-outline-summary', '???114/TBkndAssn/Fixed2Meta', 'control-mask 3'),
    ('wave28-outline-summary', '???114/TBkndAssn/FixedMeta', 'control-mask 5'),
    ('wave28-outline-summary', '???214/CV_iew/Var2Data', 'control-mask 4'),
    ('wave28-percent-auto0', '???214/CV_iew/Var2Data', 'control-mask 6'),
    ('wave28-percent-auto100', '???214/CV_iew/Var2Data', 'control-mask 6'),
    ('wave28-percent-auto100', 'Props14', 'control-mask 1'),
    ('wave28-percent-auto40', '???214/CV_iew/Var2Data', 'control-mask 6'),
    ('wave28-percent-auto40', 'Props14', 'control-mask 1'),
    ('wave28-percent-manual0', '???214/CV_iew/Var2Data', 'control-mask 4'),
    ('wave28-split', '???214/CEdl/Var2Data', 'size-differs 422->486'),
    ('wave28-split', '???214/CV_iew/Var2Data', 'control-mask 6'),
    ('wave28-update', '???214/CV_iew/Var2Data', 'control-mask 4'),
    ('wave29-outline-auto-indent', '???214/CV_iew/Var2Data', 'control-mask 2'),
    ('wave29-outline-auto-outdent', '???114/Props', 'control-mask 2'),
    ('wave29-outline-auto-outdent', '???114/TBkndAssn/Fixed2Meta', 'control-mask 6'),
    ('wave29-outline-auto-outdent', '???114/TBkndAssn/FixedMeta', 'control-mask 6'),
    ('wave29-outline-auto-outdent', '???214/CV_iew/Var2Data', 'control-mask 2'),
    ('wave29-outline-auto-summary', '???114/Props', 'control-mask 2'),
    ('wave29-outline-auto-summary', '???114/TBkndAssn/Fixed2Meta', 'control-mask 5'),
    ('wave29-outline-auto-summary', '???114/TBkndAssn/FixedMeta', 'control-mask 5'),
    ('wave29-outline-auto-summary', '???214/CV_iew/Var2Data', 'control-mask 2'),
    ('wave29-outline-manual-parent', '???114/Props', 'control-mask 2'),
    ('wave29-outline-manual-parent', '???114/TBkndAssn/Fixed2Meta', 'control-mask 8'),
    ('wave29-outline-manual-parent', '???114/TBkndAssn/FixedMeta', 'control-mask 6'),
    ('wave29-outline-manual-parent', '???214/CV_iew/Var2Data', 'control-mask 2'),
    ('wave29-percent-leaf', '???114/TBkndAssn/Fixed2Data', 'control-mask 5'),
    ('wave29-percent-mid25', '???114/TBkndAssn/Fixed2Data', 'control-mask 5'),
    ('wave29-percent-mid25', '???214/CV_iew/Var2Data', 'control-mask 2'),
    ('wave29-percent-milestone', '???114/TBkndAssn/Fixed2Data', 'control-mask 5'),
    ('wave29-percent-work', '???114/TBkndAssn/Fixed2Data', 'control-mask 5'),
    ('wave30-assign-add', '???114/TBkndRsc/FixedMeta', 'control-mask 2'),
    ('wave30-assign-delete', '???114/TBkndAssn/Fixed2Data', 'control-mask 4'),
    ('wave30-assign-delete', '???114/TBkndRsc/FixedMeta', 'control-mask 2'),
    ('wave30-baseline', '???114/TBkndTask/Fixed2Data', 'control-mask 12'),
    ('wave30-inactivate', '???214/CEdl/Var2Data', 'size-differs 384->422'),
    ('wave30-inactivate-second', '???214/CEdl/Var2Data', 'control-mask 12'),
    ('wave30-level', '???114/TBkndAssn/Fixed2Data', 'control-mask 12'),
    ('wave30-level', '???114/TBkndRsc/FixedMeta', 'control-mask 1'),
    ('wave30-resource-add', '???114/TBkndRsc/FixedMeta', 'control-mask 1'),
    ('wave30-resource-notes', '???114/TBkndRsc/FixedMeta', 'control-mask 2'),
    ('wave30-resource-rename', '???114/TBkndRsc/FixedMeta', 'control-mask 2'),
    ('wave30-respect-links-auto', '???114/TBkndAssn/Fixed2Data', 'control-mask 4'),
    ('wave30-respect-links-manual', '???114/TBkndAssn/Fixed2Data', 'control-mask 4'),
    ('wave30-task-mode-auto', '???214/CV_iew/Var2Data', 'control-mask 2'),
    ('wave30-wbs', '???114/TBkndTask/Fixed2Data', 'control-mask 12'),
    ('wave31-resource-sheet', '???114/Props', 'control-mask 1'),
    ('wave31-resource-sheet', '???114/TBkndAssn/Fixed2Meta', 'control-mask 3'),
    ('wave31-resource-sheet', '???114/TBkndAssn/FixedMeta', 'control-mask 3'),
    ('wave31-resource-sheet', '???114/TBkndAssn/Var2Data', 'size-differs 336->396'),
    ('wave31-resource-sheet', '???114/TBkndAssn/VarMeta', 'size-differs 96->132'),
    ('wave31-resource-sheet', '???114/TBkndRsc/FixedMeta', 'control-mask 1'),
    ('wave31-resource-sheet', '???114/TBkndTask/Fixed2Data', 'control-mask 4'),
    ('wave31-resource-sheet', '???114/TBkndTask/FixedMeta', 'control-mask 1'),
    ('wave31-resource-sheet', '???214/CEdl/Var2Data', 'size-differs 474->486'),
    ('wave31-resource-sheet', '???214/CFilter/FixedData', 'size-differs 230->460'),
    ('wave31-resource-sheet', '???214/CFilter/FixedMeta', 'size-differs 26->36'),
    ('wave31-resource-sheet', '???214/CFilter/Var2Data', 'size-differs 108->224'),
    ('wave31-resource-sheet', '???214/CFilter/VarMeta', 'size-differs 36->48'),
    ('wave31-resource-sheet', '???214/CV_iew/Var2Data', 'control-mask 106'),
    ('wave31-resource-sheet', '???214/Props', 'control-mask 2'),
    ('wave31-resource-sheet', 'Props14', 'size-differs 428->416'),
    ('wave31-tables', '???114/Props', 'control-mask 1'),
    ('wave31-tables', '???114/TBkndAssn/Fixed2Meta', 'control-mask 3'),
    ('wave31-tables', '???114/TBkndAssn/FixedMeta', 'control-mask 3'),
    ('wave31-tables', '???114/TBkndAssn/Var2Data', 'size-differs 336->396'),
    ('wave31-tables', '???114/TBkndAssn/VarMeta', 'size-differs 96->132'),
    ('wave31-tables', '???114/TBkndRsc/FixedMeta', 'control-mask 1'),
    ('wave31-tables', '???114/TBkndTask/Fixed2Data', 'control-mask 4'),
    ('wave31-tables', '???114/TBkndTask/FixedMeta', 'control-mask 1'),
    ('wave31-tables', '???214/CEdl/Var2Data', 'size-differs 474->486'),
    ('wave31-tables', '???214/CFilter/FixedData', 'size-differs 230->460'),
    ('wave31-tables', '???214/CFilter/FixedMeta', 'size-differs 26->36'),
    ('wave31-tables', '???214/CFilter/Var2Data', 'size-differs 108->224'),
    ('wave31-tables', '???214/CFilter/VarMeta', 'size-differs 36->48'),
    ('wave31-tables', '???214/CV_iew/Var2Data', 'control-mask 106'),
    ('wave31-tables', '???214/Props', 'control-mask 2'),
    ('wave31-tables', 'Props14', 'size-differs 428->416'),
    ('wave31-usage', '???114/Props', 'control-mask 1'),
    ('wave31-usage', '???114/TBkndAssn/Fixed2Meta', 'control-mask 3'),
    ('wave31-usage', '???114/TBkndAssn/FixedMeta', 'control-mask 3'),
    ('wave31-usage', '???114/TBkndAssn/Var2Data', 'size-differs 336->396'),
    ('wave31-usage', '???114/TBkndAssn/VarMeta', 'size-differs 96->132'),
    ('wave31-usage', '???114/TBkndRsc/FixedMeta', 'control-mask 1'),
    ('wave31-usage', '???114/TBkndTask/Fixed2Data', 'control-mask 4'),
    ('wave31-usage', '???114/TBkndTask/FixedMeta', 'control-mask 1'),
    ('wave31-usage', '???214/CEdl/Var2Data', 'size-differs 474->486'),
    ('wave31-usage', '???214/CFilter/FixedData', 'size-differs 230->460'),
    ('wave31-usage', '???214/CFilter/FixedMeta', 'size-differs 26->36'),
    ('wave31-usage', '???214/CFilter/Var2Data', 'size-differs 108->224'),
    ('wave31-usage', '???214/CFilter/VarMeta', 'size-differs 36->48'),
    ('wave31-usage', '???214/CV_iew/Var2Data', 'control-mask 106'),
    ('wave31-usage', '???214/Props', 'control-mask 2'),
    ('wave31-usage', 'Props14', 'size-differs 428->416'),
    ('wave34-assign-add-second', '???114/Props', 'control-mask 2'),
    ('wave34-assign-add-second', '???114/TBkndAssn/Fixed2Data', 'control-mask 4'),
    ('wave34-assign-add-second', '???114/TBkndAssn/Fixed2Meta', 'control-mask 6'),
    ('wave34-assign-add-second', '???114/TBkndAssn/FixedMeta', 'control-mask 10'),
    ('wave34-assign-add-third', '???114/Props', 'control-mask 1'),
    ('wave34-assign-add-third', '???114/TBkndAssn/Fixed2Data', 'control-mask 8'),
    ('wave34-assign-add-third', '???114/TBkndAssn/Fixed2Meta', 'control-mask 7'),
    ('wave34-assign-add-third', '???114/TBkndAssn/FixedMeta', 'control-mask 11'),
    ('wave34-assign-add-third', 'Props14', 'control-mask 1'),
    ('wave35-g3-assign-remove-port-saved', '???114/Props', 'control-mask 2'),
    ('wave35-g3-assign-remove-port-saved', '???114/TBkndAssn/Fixed2Data', 'control-mask 7'),
    ('wave35-g3-assign-remove-port-saved', '???114/TBkndAssn/Fixed2Meta', 'control-mask 6'),
    ('wave35-g3-assign-remove-port-saved', '???114/TBkndAssn/FixedMeta', 'control-mask 10'),
    ('wave35-g3-assign-remove-port-saved', '???114/TBkndRsc/FixedMeta', 'control-mask 2'),
    ('wave35-g3-assign-remove-port-saved', '???214/CEdl/Var2Data', 'size-differs 486->422'),
    ('wave35-g3-assign-remove-port-saved', '???214/CV_iew/Var2Data', 'control-mask 4'),
    ('wave35-g3-assign-remove-port-saved', 'Props14', 'control-mask 1'),
    ('wave35-viewstate-zoom-custom-narrow', '???214/CEdl/Var2Data', 'control-mask 7'),
    ('wave36-r7-calc-assign-1', '???114/TBkndAssn/Fixed2Data', 'control-mask 2'),
    ('wave36-r7-calc-assign-2', '???114/TBkndAssn/Fixed2Data', 'control-mask 8'),
    ('wave36-r7-dsi-total-work', '???114/TBkndAssn/Fixed2Data', 'control-mask 4'),
    ('wave36-r7-font-bold-session2', '???214/CEdl/Var2Data', 'size-differs 384->422'),
    ('wave36-r7-note-assignment', '???114/TBkndAssn/Fixed2Data', 'control-mask 2'),
    ('wave36-r7-table1-column-replace', '???214/CTable/FixedData', 'control-mask 1'),
    ('wave36-r7-table1-date-format', '???214/CTable/FixedData', 'control-mask 1'),
    ('wave36-r7-w37-b-assign-one', '???114/TBkndAssn/Fixed2Data', 'control-mask 2'),
    ('wave36-r7-w37-b-assign-two', '???114/TBkndAssn/Fixed2Data', 'control-mask 4'),
    ('wave36-r7-w37-b-rebaseline-changed', '???114/Props', 'control-mask 1'),
    ('wave36-r7-w37-b-rebaseline-same', '???114/Props', 'control-mask 2'),
        )
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
        pos_tails = {}
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
                for ln in lines:
                    if len(ln.split()) > 1:
                        pos_tails.setdefault((stem, ln.split()[1]), []).append(
                            " ".join([ln.split()[0]] + ln.split()[2:]))
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
        observed_pins = sorted((stem_, stream_, "; ".join(sorted(t_)))
                               for (stem_, stream_), t_ in pos_tails.items())
        check("W36-0d.7 the OPEN pairs fail by exactly the %d pinned (pair, stream, count) rows"
              % len(OPEN_PINNED), observed_pins == sorted(OPEN_PINNED),
              "%r" % ([r for r in observed_pins if r not in OPEN_PINNED][:4] +
                      [r for r in OPEN_PINNED if r not in observed_pins][:4],))
        check("W36-0d.7 the pins name every OPEN pair and every stream of its entry",
              {(r[0], r[1]) for r in OPEN_PINNED} ==
              {(s_, n_) for s_ in OPEN for n_ in OPEN[s_]},
              "%r" % (sorted({(r[0], r[1]) for r in OPEN_PINNED} ^
                             {(s_, n_) for s_ in OPEN for n_ in OPEN[s_]})[:4],))
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
            canon_pair = g.canonical(RS, with_noise=True)
            gate_canon = canon_pair[1] if isinstance(canon_pair, tuple) else {}   # W36-0e.1: the case below says why
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
        # a stamp or save word is noise inside the committed span (W36-0e.2), so a changed one holds
        # a value inside it; the two save words are one value and move together
        in_span = struct.pack("<I", 0x3D200000)
        word_rids = [rid for rid, cls in sorted(g.NOISE[props_n]["props"].items()) if cls == "word"]
        for rid, cls in sorted(g.NOISE[props_n]["props"].items()):
            at, length = recs[rid]
            edits_ = [(recs[r_][0], in_span) for r_ in word_rids] if cls == "word" else [(at, v1(99))]
            problems, lines = verdict(put(AR, props_n, edits_), AR)
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
        problems, lines = verdict(put(AR, task_n, [(last + 128, in_span)]), AR, short)
        check("W36-0b.4 Task FixedData: the creation stamp of a row the control lacks is noise",
              problems == 0, "%r" % (lines,))
        problems, lines = verdict(put(AR, task_n, [(last + 132, b"\x01")]), AR, short)
        check("W36-0b.4 Task FixedData: the byte after the stamp is data and fails",
              problems >= 1, "%r" % (lines,))
        problems, lines = verdict(put(AR, task_n, [(base + size + 128, in_span)]), AR, short)
        check("W36-0b.4 Task FixedData: the stamp of a row the control has fails",
              problems >= 1 and task_n in "".join(lines), "%r" % (lines,))
        # W36-0e.2: with the span a stamp outside it fails on its own, so the guard that a row the
        # control has (on either side) is not new is held by exact counts of the bytes that differ:
        # the stamp's four bytes count as data when the traced row, or the reference's, is the control's
        stamp_at = base + size + 128
        edited = put(AR, task_n, [(stamp_at, in_span), (base + size, bytes([AR[task_n][base + size] ^ 0x40]))])
        ndiff = sum(a != b for a, b in zip(edited[task_n], AR[task_n]))
        problems, lines = verdict(edited, AR, short)
        check("W36-0e.2 Task FixedData: the traced row is new but the reference's is the control's, so the "
              "stamp counts: %d bytes differ" % ndiff,
              lines == ["bytes-differ %s %d" % (task_n, ndiff)] and ndiff >= 2, "%r" % (lines,))
        var_n, meta_n2 = "???114/TBkndAssn/Var2Data", "???114/TBkndAssn/VarMeta"
        known = {uid for uid, _, _ in g._meta_blocks(AC[meta_n2])}
        stamps = [(uid, off) for uid, off, key in g._meta_blocks(AR[meta_n2])
                  if key == 0x0F40027A]
        fresh_s = [off + 4 for uid, off in stamps if uid not in known]
        old_s = [off + 4 for uid, off in stamps if uid in known]
        check("W36-0b.4 the Assn Var2Data case has a new block and an old one",
              fresh_s and old_s, "%r %r" % (fresh_s, old_s))
        problems, lines = verdict(put(AR, var_n, [(fresh_s[0], in_span)]), AR, AC)
        check("W36-0b.4 Assn Var2Data: the stamp of an assignment the control lacks is noise",
              problems == 0, "%r" % (lines,))
        problems, lines = verdict(put(AR, var_n, [(old_s[0], in_span)]), AR, AC)
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
                    want(sname, at, in_span)      # both save words to one value inside the span
                elif cls == "guid" and g._is_v1_guid(data[at:at + 16]):
                    want(sname, at, fresh_guid())
                elif cls == "guidstr":
                    for d in g._guidstr_digits(data[at:at + length]):
                        if d // 2 in (17, 20):      # the version and variant digits are data (W36-0d.3)
                            continue
                        want(sname, at + d, b"7" if data[at + d:at + d + 1] != b"7" else b"8")
            if "new_row" in rule:
                base, size = g.STRIDE[sname]
                known_uids = g._task_uids(AC.get(sname))
                lo, hi = rule["new_row"]
                for r in range((len(data) - base) // size):
                    row = base + r * size
                    if g._u32(data, row) not in known_uids:
                        want(sname, row + lo, struct.pack("<I", 0x3D200000 + r))
        for uid, off, key in g._meta_blocks(AR[meta_n2]):
            if key == 0x0F40027A and off != 0xFFFFFFFF and uid not in known:
                want(var_n, off + 4, struct.pack("<I", 0x3D210000 + uid % 4096))
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

        # --- W36-0e.3 (R36G0d-3): an emptied Props14 tail name is not a name ------------------------
        # The canonical form replaces a name by an empty byte string, so a name emptied by hand equalled
        # its own placeholder and passed (the path record has had `payload and` since 0d.1).  The tail
        # names are now derived fields as well: an emptied one on one side only is a problem.
        lone_text = swapped(R, P14, p14_join(p14_head, [(second[0], text_name)]))
        for label, entries_, ref_ in (
                ("both tail names emptied", [(first[0], b""), (second[0], b"")], R),
                ("the 0xffffffff name emptied", [(first[0], b""), second], R),
                ("the 0x3540000b name emptied", [first, (second[0], b"")], R),
                ("the lone 0x3540000b name emptied (the reference's text)", [(second[0], b"")], lone_text)):
            emptied_ = swapped(R, P14, p14_join(p14_head, entries_))
            for which, (tr, rf) in (("traced", (emptied_, ref_)), ("reference", (ref_, emptied_))):
                problems_, lines_ = verdict(tr, rf)
                check("W36-0e.3 %s in the %s save fails" % (label, which),
                      problems_ >= 1 and any(P14 in ln for ln in lines_), "%r" % (lines_,))
        emptied_ = swapped(R, P14, p14_join(p14_head, [(first[0], b""), (second[0], b"")]))
        problems_, lines_ = verdict(emptied_, emptied_)
        check("W36-0e.3 both tail names emptied on both sides is equality and passes",
              problems_ == 0, "%r" % (lines_,))
        # With the name a derived field, one junk name against the reference's text is caught by the
        # derived flag as well as by the canon, so the 0xffffffff name's guard needs a case the flag
        # cannot see: two different junk names of one length, neither a name, in that entry.
        junk_b = bytes([3, 0, 4, 0]) * 100
        junk_a_ = swapped(R, P14, p14_join(p14_head, [(first[0], junk), second]))
        junk_b_ = swapped(R, P14, p14_join(p14_head, [(first[0], junk_b), second]))
        for which, (tr, rf) in (("traced", (junk_a_, junk_b_)), ("reference", (junk_b_, junk_a_))):
            problems_, lines_ = verdict(tr, rf)
            check("W36-0e.3 two different junk names in the 0xffffffff entry fail (%s the first)" % which,
                  problems_ >= 1 and any(P14 in ln for ln in lines_), "%r" % (lines_,))

        # --- W36-0d.2 (R36G0c-2): streams are keyed by their raw directory name ---------------------
        # The printable rendering of a name is lossy: \x05SummaryInformation and \x06SummaryInformation
        # both render as "?SummaryInformation", and a dict keyed by the rendering kept only one.  A
        # directory entry is patched in place (its UTF-16 name and its length word), so the file stays
        # a valid compound file.
        def rename_entry(src, dst, old, new):
            shutil.copyfile(src, dst)
            ole_ = olefile.OleFileIO(dst)
            try:
                sid_ = [i for i, e in enumerate(ole_.direntries) if e is not None and e.name == old][0]
                chain_, sect_ = [], ole_.first_dir_sector
                while sect_ < 0xFFFFFFFA:
                    chain_.append(sect_)
                    sect_ = ole_.fat[sect_]
                per_ = ole_.sectorsize // 128
                at_ = (chain_[sid_ // per_] + 1) * ole_.sectorsize + (sid_ % per_) * 128
            finally:
                ole_.close()
            raw_ = bytearray(open(dst, "rb").read())
            enc_ = (new + "\0").encode("utf-16le")
            raw_[at_:at_ + 64] = enc_.ljust(64, b"\0")
            struct.pack_into("<H", raw_, at_ + 64, len(enc_))
            open(dst, "wb").write(bytes(raw_))

        def entry_count(path):
            ole_ = olefile.OleFileIO(path)
            try:
                return len(ole_.listdir(streams=True, storages=False))
            finally:
                ole_.close()

        dsi_old, si_old = "\x05DocumentSummaryInformation", "\x05SummaryInformation"
        clash = os.path.join(tmp, "clash.mpp")       # DocumentSummaryInformation renamed to render
        rename_entry(ref, clash, dsi_old, "\x06SummaryInformation")   # as SummaryInformation does
        n_clash = entry_count(clash)
        check("W36-0d.2 the patched file has the same number of entries and two that render alike",
              n_clash == entry_count(ref))
        for label, tr, rf in (("on both sides", clash, clash), ("in the traced", clash, ref),
                              ("in the reference", ref, clash)):
            rc, out = run_cli(tr, rf, ctl, nul)
            check("W36-0d.2 a stream name collision %s FAILs and is named" % label,
                  rc == 1 and "stream name collision ?SummaryInformation" in out, "rc=%d %r" % (rc, out[:200]))
        rc, out = run_cli(clash, clash, ctl, nul)
        check("W36-0d.2 the head line counts the %d raw entries, not the printable names" % n_clash,
              head_counts(out)[0] == n_clash, out.splitlines()[0])
        # a raw name that differs but renders alike (no collision): not the same stream
        moved = os.path.join(tmp, "moved.mpp")
        rename_entry(ref, moved, si_old, "\x06SummaryInformation")
        for label, tr, rf in (("traced", moved, ref), ("reference", ref, moved)):
            rc, out = run_cli(tr, rf, ctl, nul)
            check("W36-0d.2 a stream whose raw name differs in the %s FAILs" % label,
                  rc == 1 and "stream name differs ?SummaryInformation" in out, "rc=%d %r" % (rc, out[:200]))
            # two raw entries (\x05 and \x06 SummaryInformation) that render as one name: the head
            # line counts both (entry_count(ref) + 1), a count of printable names would count one
            check("W36-0d.8 the head line counts both raw names of a renamed stream (%s)" % label,
                  head_counts(out)[0] == entry_count(ref) + 1, out.splitlines()[0])
        rc, out = run_cli(moved, moved, ctl, nul)
        check("W36-0d.2 the same renamed stream on both sides is equality and passes",
              rc == 0, "rc=%d %r" % (rc, out[:200]))
        # a stream on one side only is a FAIL (the name is kept, not merged into another)
        gone = os.path.join(tmp, "gone.mpp")
        rename_entry(ref, gone, dsi_old, "\x05ZzzNewStream")
        for label, tr, rf in (("traced", gone, ref), ("reference", ref, gone)):
            rc, out = run_cli(tr, rf, ctl, nul)
            check("W36-0d.2 a stream on one side only (%s) FAILs" % label,
                  rc == 1 and ("only-in-" + label) in out, "rc=%d %r" % (rc, out[:200]))

        # --- W36-0d.3 (R36G0c-3): the view GUID string is noise only as a version-1 GUID ------------
        # The string is parsed to its 16 bytes and held to what a binary GUID field is held to: version
        # 1, the RFC variant, and a timestamp that is not 0.  All-zero digits, a changed version digit
        # and a zero timestamp are data.  The string is the little-endian rendering of the bytes, so
        # the version digit is the third digit of the third group (string index 17), the variant digit
        # the first of the fourth group (index 20); index 15 is the high nibble of time_hi.
        vs_at = [pos for pos, length, rid in records(R[view_props], 16) if rid == 0x03400001][0]
        vs_text = R[view_props][vs_at:vs_at + 76].decode("utf-16le")
        vs_raw = uuid.UUID(vs_text).bytes_le
        check("W36-0d.3 the pair's view GUID string parses to a version-1 GUID with a timestamp",
              vs_raw[6] >> 4 == 1 and vs_raw[8] >> 6 == 2 and any(vs_raw[:6]) and vs_text[17] == "1",
              vs_text)
        upper = vs_text == vs_text.upper()

        def guid_text(raw_):
            t_ = "{%s}" % uuid.UUID(bytes_le=bytes(raw_))
            return t_.upper() if upper else t_

        def with_text(streams_, text_):
            return put(streams_, view_props, [(vs_at, text_.encode("utf-16le"))])

        check("W36-0d.3 the helper rebuilds the pair's string from its bytes", guid_text(vs_raw) == vs_text,
              "%s %s" % (guid_text(vs_raw), vs_text))
        zero_ts = bytearray(vs_raw)
        zero_ts[0:6] = bytes(6)
        zero_ts[6] = 0x10
        zero_ts[7] = 0
        bad_ver = bytearray(vs_raw)
        bad_ver[6] = (bad_ver[6] & 15) | 0x40         # version 4
        bad_var = bytearray(vs_raw)
        bad_var[8] = 0xC0 | (bad_var[8] & 63)         # not the RFC variant
        for label, text_ in (("all-zero digits", "{00000000-0000-0000-0000-000000000000}"),
                             ("a version digit that is not 1", guid_text(bad_ver)),
                             ("a variant that is not RFC", guid_text(bad_var)),
                             ("a version-1 GUID with a zero timestamp", guid_text(zero_ts))):
            for which, (tr, rf) in (("traced", (with_text(R, text_), R)),
                                    ("reference", (R, with_text(R, text_)))):
                problems_, lines_ = verdict(tr, rf)
                check("W36-0d.3 %s in the %s view GUID string fails" % (label, which),
                      problems_ >= 1 and any(view_props in ln for ln in lines_), "%r" % (lines_,))
        # the version digit by index: a flip of string index 17 fails; index 15 (time_hi) is noise
        for idx, expect in ((17, "fails"), (20, "fails"), (15, "noise")):
            chars = list(vs_text)
            chars[idx] = "4" if chars[idx] != "4" else "5"
            if idx == 20:
                chars[idx] = "c"
            problems_, lines_ = verdict(with_text(R, "".join(chars)), R)
            check("W36-0d.3 view GUID string index %d changed: %s" % (idx, expect),
                  (problems_ >= 1) == (expect == "fails"), "%r" % (lines_,))
        # another v1 GUID with another timestamp is still noise
        other = bytearray(vs_raw)
        other[0] ^= 0x55
        problems_, lines_ = verdict(with_text(R, guid_text(other)), R)
        check("W36-0d.3 a different version-1 GUID string is noise and passes", problems_ == 0,
              "%r" % (lines_,))

        # --- W36-0d.4 (R36G0c-4): a string slot is a string only by its type word ----------------------
        # Author, LastAuthor and RevNumber are noise only in a VT_LPSTR slot.  The slot of the pair, with
        # its type word set to VT_LPWSTR (0x1f) or to 0 and nothing else changed, is not a string slot:
        # its bytes stay raw and the difference fails.  (The same slot with the type word kept is noise.)
        for pid, label in ((4, "Author"), (8, "LastAuthor"), (9, "RevNumber")):
            slot_ = bytearray(dict(si_slots(R[SI])[2])[pid])
            check("W36-0d.4 the reference %s slot is a VT_LPSTR (type 0x1e)" % label,
                  struct.unpack_from("<I", slot_, 0)[0] == 0x1E)
            for tname, tword in (("0x1f", 0x1F), ("0", 0)):
                bad_ = bytearray(slot_)
                struct.pack_into("<I", bad_, 0, tword)
                for which, (tr, rf) in (("traced", (swapped(R, SI, si_build(R[SI], {pid: bytes(bad_)})), R)),
                                        ("reference", (R, swapped(R, SI, si_build(R[SI], {pid: bytes(bad_)}))))):
                    problems_, lines_ = verdict(tr, rf)
                    check("W36-0d.4 %s with its type word %s in the %s save fails" % (label, tname, which),
                          problems_ >= 1 and any(SI in ln for ln in lines_), "%r" % (lines_,))
            same_ = swapped(R, SI, si_build(R[SI], {pid: bytes(slot_)}))
            problems_, lines_ = verdict(same_, R)
            check("W36-0d.4 %s with its type word kept is the pair's own stream and passes" % label,
                  problems_ == 0, "%r" % (lines_,))

        # --- W36-0d.5 (R36G0c-5): three Props14 / text guards that no case reached ------------------
        # (a) the first tail entry starts right after the count word; (b) the blob's two size words
        # agree; (c) a path or name longer than 2048 bytes is not a name.  Each is a wrong file of the
        # same length as the right one, so no size difference fails it first.
        def g_compare(tr_, rf_):
            lines_, problems_, checked_, unchecked_ = g.compare(tr_, rf_, C, N)
            return problems_, lines_

        body_ = b"".join(struct.pack("<II", eid, 0x1E) + (name[:-4] if k == 0 else name)
                         for k, (eid, name) in enumerate(p14_entries))
        shifted = p14_head + struct.pack("<I", len(p14_entries)) + bytes([0x11, 0, 0x22, 0]) + body_
        check("W36-0d.5 the shifted tail is as long as the pair's Props14", len(shifted) == len(R[P14]))
        for which, (tr, rf) in (("traced", (swapped(R, P14, shifted), R)),
                                ("reference", (R, swapped(R, P14, shifted)))):
            problems_, lines_ = g_compare(tr, rf)
            check("W36-0d.5 a Props14 tail whose first entry is not at +4 (junk after the count) in the "
                  "%s fails" % which, problems_ >= 1 and any(P14 in ln for ln in lines_), "%r" % (lines_,))
        for which, off in (("+4", 4), ("+0", 0)):
            wrong = bytearray(R[P14])
            struct.pack_into("<I", wrong, off, _u(R[P14][off:]) + 16)
            for side, (tr, rf) in (("traced", (swapped(R, P14, bytes(wrong)), R)),
                                   ("reference", (R, swapped(R, P14, bytes(wrong))))):
                problems_, lines_ = g_compare(tr, rf)
                check("W36-0d.5 the Props14 blob size word %s changed by 16 in the %s fails" % (which, side),
                      problems_ >= 1 and any(P14 in ln for ln in lines_), "%r" % (lines_,))
        for chars, expect in ((1024, "noise"), (1025, "fails")):
            long_a = swapped(R, P14, p14_join(p14_head, [first, (second[0], ("a" * chars).encode("utf-16le"))]))
            long_b = swapped(R, P14, p14_join(p14_head, [first, (second[0], ("b" * chars).encode("utf-16le"))]))
            problems_, lines_ = g_compare(long_a, long_b)
            check("W36-0d.5 two different tail names of %d bytes: %s" % (2 * chars, expect),
                  (problems_ >= 1 and any(P14 in ln for ln in lines_)) if expect == "fails"
                  else problems_ == 0, "%r" % (lines_,))

        # --- W36-0d.6 (R36G0c-6): a masked noise field must hold a value a save writes -------------------
        # A creation stamp and a save word are noise as a 32-bit value that is not 0 and not all-ones;
        # LastSave as a FILETIME after 2000-01-01 and not all-ones; Author, LastAuthor and RevNumber as a
        # Windows-1252 text that ends in its NUL, with no control character.  The value bounds and the
        # committed range they come from are in spec/writers/evidence/trace-gate-fields-g0b.md.  The font
        # word's top byte has no bound (8 distinct values in the committed saves, no pattern).
        zero4, ones4 = bytes(4), b"\xff" * 4
        for label, stream_, at_, ref_, ctl_ in (
                ("Task FixedData new-row stamp", task_n, last + 128, AR, short),
                ("Assn Var2Data new-block stamp", var_n, fresh_s[0], AR, AC)):
            for vname, bad_ in (("0", zero4), ("0xffffffff", ones4)):
                for which in ("traced", "reference"):
                    badf = put(ref_, stream_, [(at_, bad_)])
                    tr, rf = (badf, ref_) if which == "traced" else (ref_, badf)
                    problems_, lines_ = verdict(tr, rf, ctl_)
                    check("W36-0d.6 %s of %s in the %s save fails" % (label, vname, which),
                          problems_ >= 1 and any(stream_ in ln for ln in lines_), "%r" % (lines_,))
            badf = put(ref_, stream_, [(at_, zero4)])
            problems_, lines_ = verdict(badf, badf, ctl_)
            check("W36-0d.6 %s of 0 on both sides is equality and passes" % label,
                  problems_ == 0, "%r" % (lines_,))
        for rid in sorted(g.NOISE[props114]["props"]):
            hits = [(pos, length) for pos, length, r_ in records(R[props114], 16) if r_ == rid]
            if not hits or hits[0][1] != 4:
                continue
            for vname, bad_ in (("0", zero4), ("0xffffffff", ones4)):
                for which in ("traced", "reference"):
                    badf = put(R, props114, [(hits[0][0], bad_)])
                    tr, rf = (badf, R) if which == "traced" else (R, badf)
                    problems_, lines_ = verdict(tr, rf)
                    check("W36-0d.6 114/Props save word %#x of %s in the %s save fails" %
                          (rid, vname, which),
                          problems_ >= 1 and any(props114 in ln for ln in lines_), "%r" % (lines_,))
        saved_slot = si_have[13]
        later = struct.pack("<I", 0x40) + struct.pack("<Q", 0x01DD6A00_00000000)
        check("W36-0d.6 a LastSave of another date inside the committed span is noise",
              verdict(swapped(R, SI, si_build(R[SI], {13: later})), R)[0] == 0)
        for vname, ft_ in (("0", 0), ("0xffffffffffffffff", 0xFFFFFFFFFFFFFFFF),
                           ("a date before 2000", 0x01BF53EB256D4000 - 1)):
            slot_ = struct.pack("<I", 0x40) + struct.pack("<Q", ft_)
            for which in ("traced", "reference"):
                badf = swapped(R, SI, si_build(R[SI], {13: slot_}))
                tr, rf = (badf, R) if which == "traced" else (R, badf)
                problems_, lines_ = verdict(tr, rf)
                check("W36-0d.6 LastSave of %s in the %s save fails" % (vname, which),
                      problems_ >= 1 and any(SI in ln for ln in lines_), "%r" % (lines_,))
        no_nul = struct.pack("<II", 0x1E, 3) + b"abc\0"
        for pid, label in ((4, "Author"), (8, "LastAuthor"), (9, "RevNumber")):
            for what, slot_ in (("a control byte 0x01", lpstr("a\x01b")),
                                ("a DEL byte", lpstr("a\x7fb")),
                                ("a byte undefined in Windows-1252 (0x81)",
                                 struct.pack("<II", 0x1E, 4) + b"a\x81b\0"),
                                ("a length of 0", struct.pack("<II", 0x1E, 0)),
                                ("no terminating NUL", no_nul)):
                for which in ("traced", "reference"):
                    badf = swapped(R, SI, si_build(R[SI], {pid: slot_}))
                    tr, rf = (badf, R) if which == "traced" else (R, badf)
                    problems_, lines_ = verdict(tr, rf)
                    check("W36-0d.6 %s with %s in the %s save fails" % (label, what, which),
                          problems_ >= 1 and any(SI in ln for ln in lines_), "%r" % (lines_,))
            for what, slot_ in (("an accented name", struct.pack("<II", 0x1E, 5) + b"Ren\xe9\0\0\0\0"),
                                ("33 bytes of 0xff and a NUL (valid Windows-1252)",
                                 struct.pack("<II", 0x1E, 34) + b"\xff" * 33 + b"\0\0\0")):
                problems_, lines_ = verdict(swapped(R, SI, si_build(R[SI], {pid: slot_})), R)
                check("W36-0d.6 %s with %s is noise" % (label, what), problems_ == 0, "%r" % (lines_,))

        # --- W36-0e.2 (R36G0d-2): a masked value is accepted only inside the range the saves show -------
        # The bounds of 0d.6 refused 0 and all-ones only, so a stamp of 1 or 0x7fffffff, two unequal save
        # words, and a LastSave in 2001 or 2600 were noise.  A stamp is noise inside the span of the 624
        # committed saves (0x3d141482 to 0x3d282db4), the two 114/Props save words only when equal, and
        # LastSave inside its committed span (0x01dd62019f6a0e00 to 0x01dd72130aac6c00, 22 October to
        # 11 November 2026).  The numbers are written here from the Value bounds table of
        # spec/writers/evidence/trace-gate-fields-g0b.md, not read from the gate.
        span_lo, span_hi = 0x3D141482, 0x3D282DB4
        ls_lo, ls_hi = 0x01DD62019F6A0E00, 0x01DD72130AAC6C00

        def filetime_of(year):
            return (datetime.date(year, 1, 1) - datetime.date(1601, 1, 1)).days * 86400 * 10 ** 7

        gate_spans = (getattr(g, "STAMP_SPAN", None), getattr(g, "LASTSAVE_SPAN", None))
        check("W36-0e.2 the gate's stamp span is the committed span", gate_spans[0] == (span_lo, span_hi),
              "%r" % (gate_spans[0],))
        check("W36-0e.2 the gate's LastSave span is the committed span", gate_spans[1] == (ls_lo, ls_hi),
              "%r" % (gate_spans[1],))
        for label, stream_, at_, ref_, ctl_ in (
                ("Task FixedData new-row stamp", task_n, last + 128, AR, short),
                ("Assn Var2Data new-block stamp", var_n, fresh_s[0], AR, AC)):
            for vname, val_ in (("1", 1), ("0x7fffffff", 0x7FFFFFFF), ("one below the span", span_lo - 1),
                                ("one above the span", span_hi + 1)):
                for which in ("traced", "reference"):
                    badf = put(ref_, stream_, [(at_, struct.pack("<I", val_))])
                    tr, rf = (badf, ref_) if which == "traced" else (ref_, badf)
                    problems_, lines_ = verdict(tr, rf, ctl_)
                    check("W36-0e.2 %s of %s in the %s save fails" % (label, vname, which),
                          problems_ >= 1 and any(stream_ in ln for ln in lines_), "%r" % (lines_,))
            # the ends and the inside of the span are noise; each is set against another value inside
            # the span, so the two saves differ whatever the pair held
            for vname, val_, other_ in (("the span's lower end", span_lo, 0x3D200000),
                                        ("the span's upper end", span_hi, 0x3D200000),
                                        ("a value inside it", 0x3D200000, 0x3D200001)):
                badf = put(ref_, stream_, [(at_, struct.pack("<I", val_))])
                othr = put(ref_, stream_, [(at_, struct.pack("<I", other_))])
                problems_, lines_ = verdict(badf, othr, ctl_)
                check("W36-0e.2 %s of %s against another value inside the span is noise" % (label, vname),
                      problems_ == 0 and badf[stream_] != othr[stream_], "%r" % (lines_,))
        save_at = {r_: pos for pos, length, r_ in records(R[props114], 16)
                   if r_ in (0x02400007, 0x024013B1) and length == 4}
        check("W36-0e.2 the pair's Props holds both save words, equal",
              len(save_at) == 2 and len({R[props114][p_:p_ + 4] for p_ in save_at.values()}) == 1,
              "%r" % (save_at,))
        w_a, w_b = (pos for _, pos in sorted(save_at.items()))
        for vname, val_ in (("1", 1), ("0x7fffffff", 0x7FFFFFFF)):
            for which in ("traced", "reference"):
                badf = put(R, props114, [(w_a, struct.pack("<I", val_)), (w_b, struct.pack("<I", val_))])
                tr, rf = (badf, R) if which == "traced" else (R, badf)
                problems_, lines_ = verdict(tr, rf)
                check("W36-0e.2 both 114/Props save words %s in the %s save fails" % (vname, which),
                      problems_ >= 1 and any(props114 in ln for ln in lines_), "%r" % (lines_,))
        for vname, edits_ in (("the first word alone", [(w_a, struct.pack("<I", 0x3D200000))]),
                              ("the second word alone", [(w_b, struct.pack("<I", 0x3D200000))]),
                              ("the two words to two values inside the span",
                               [(w_a, struct.pack("<I", 0x3D200000)), (w_b, struct.pack("<I", 0x3D200001))])):
            for which in ("traced", "reference"):
                badf = put(R, props114, edits_)
                tr, rf = (badf, R) if which == "traced" else (R, badf)
                problems_, lines_ = verdict(tr, rf)
                check("W36-0e.2 unequal 114/Props save words (%s, inside the span) in the %s save fail" %
                      (vname, which), problems_ >= 1 and any(props114 in ln for ln in lines_), "%r" % (lines_,))
        for vname, val_ in (("the span's lower end", span_lo), ("the span's upper end", span_hi),
                            ("a value inside it", 0x3D200000)):
            badf = put(R, props114, [(w_a, struct.pack("<I", val_)), (w_b, struct.pack("<I", val_))])
            othr = put(R, props114, [(w_a, struct.pack("<I", 0x3D200001)), (w_b, struct.pack("<I", 0x3D200001))])
            problems_, lines_ = verdict(badf, othr)
            check("W36-0e.2 both 114/Props save words at %s against another value inside it are noise" % vname,
                  problems_ == 0 and badf[props114] != othr[props114], "%r" % (lines_,))
        for vname, ft_ in (("the year 2600", filetime_of(2600)), ("the year 2001", filetime_of(2001)),
                           ("one tick below the span", ls_lo - 1), ("one tick above the span", ls_hi + 1)):
            slot_ = struct.pack("<I", 0x40) + struct.pack("<Q", ft_)
            for which in ("traced", "reference"):
                badf = swapped(R, SI, si_build(R[SI], {13: slot_}))
                tr, rf = (badf, R) if which == "traced" else (R, badf)
                problems_, lines_ = verdict(tr, rf)
                check("W36-0e.2 LastSave in %s in the %s save fails" % (vname, which),
                      problems_ >= 1 and any(SI in ln for ln in lines_), "%r" % (lines_,))
        mid_ = (ls_lo + ls_hi) // 2
        for vname, ft_, other_ in (("the span's lower end", ls_lo, mid_), ("the span's upper end", ls_hi, mid_),
                                   ("a time inside it", mid_, ls_lo)):
            badf = swapped(R, SI, si_build(R[SI], {13: struct.pack("<I", 0x40) + struct.pack("<Q", ft_)}))
            othr = swapped(R, SI, si_build(R[SI], {13: struct.pack("<I", 0x40) + struct.pack("<Q", other_)}))
            problems_, lines_ = verdict(badf, othr)
            check("W36-0e.2 LastSave at %s against another time inside the span is noise" % vname,
                  problems_ == 0 and badf[SI] != othr[SI], "%r" % (lines_,))

        # --- W36-0d.9 (R36G0c-8): the text test rejects every Unicode C category --------------------
        # U+0085 (a C1 control), U+200E (format), U+E000 (private use) and U+0378 (unassigned) were
        # "printable" (>= U+0020 and not U+007F): a path of twenty of them passed as noise.
        for what, ch_ in (("a C1 control (U+0085)", "\u0085"), ("a format character (U+200E)", "\u200e"),
                          ("a private-use character (U+E000)", "\ue000"), ("an unassigned one (U+0378)", "\u0378")):
            junk_ = (ch_ * 20).encode("utf-16le")
            for label_, name_, at_ in (("CEdl path record", cedl, p_at), ("Props14 path record", P14, p14_path)):
                for which in ("traced", "reference"):
                    badf = put(R, name_, [(at_, junk_)])
                    tr, rf = (badf, R) if which == "traced" else (R, badf)
                    problems_, lines_ = verdict(tr, rf)
                    check("W36-0d.9 %s of 20 x %s in the %s save fails" % (label_, what, which),
                          problems_ >= 1 and any(name_ in ln for ln in lines_), "%r" % (lines_,))
        check("W36-0d.9 _text_ok takes an accented name, CJK text and a trailing NUL",
              all(g._text_ok(t.encode("utf-16le")) for t in ("Ren\u00e9", "\u540d\u524d\0", "C:\\w30\\a-b!\0")))
        check("W36-0d.9 _text_ok refuses a C1 control, a format character, private use and an unassigned one",
              not any(g._text_ok(t.encode("utf-16le")) for t in ("a\u0085b", "a\u200eb", "a\ue000b", "a\u0378b")))

        # --- W36-0e.1 (R36G0d-1): the guards the derived meta-test found no case for ----------------------
        # test_gate_guards_meta.py derives its candidates from the gate (ast) and removes each one in
        # turn.  The cases below are what turned the green ones red, each on a file or a stream that the
        # guard refuses and the gate without it takes.  A gate that raises on a damaged input is a FAIL
        # named "gate raised": the guard that keeps it from raising is a guard.
        RAISED = object()

        def calls(label, fn, *args):
            try:
                return fn(*args)
            except Exception as exc:   # noqa: BLE001 - any exception is the finding
                check("gate raised %s in %s: %s" % (type(exc).__name__, label, str(exc)[:60]), False)
                return RAISED

        def expect(label, got, want):
            check("W36-0e.1 %s" % label, got == want, "%r != %r" % (got, want))

        # the tail count word is the number of entries the tail holds; a wrong count is not noise
        p14_both = [first, second]
        for count_ in (0, 1, 3):
            p14_verdict("a count word of %d over the two entries" % count_, p14_both, count=count_)
        # the section size word of SummaryInformation is the section's length
        si_len = len(R[SI])
        for delta in (-4, 4):
            for which, (tr, rf) in (("traced", (put(R, SI, [(si_sec, struct.pack("<I", si_len - si_sec + delta))]), R)),
                                    ("reference", (R, put(R, SI, [(si_sec, struct.pack("<I", si_len - si_sec + delta))])))):
                problems_, lines_ = verdict(tr, rf)
                check("W36-0e.1 a SummaryInformation section size word %+d in the %s save fails" % (delta, which),
                      problems_ >= 1 and any(SI in ln for ln in lines_), "%r" % (lines_,))
        # the first value follows the offset table: four junk bytes between them are not noise
        si_count = struct.unpack_from("<I", R[SI], si_sec + 4)[0]
        si_table_at = si_sec + 8
        gap = bytearray(R[SI][:si_table_at])
        struct.pack_into("<I", gap, si_sec, len(R[SI]) + 4 - si_sec)
        for k in range(si_count):
            pid_, off_ = struct.unpack_from("<II", R[SI], si_table_at + 8 * k)
            gap += struct.pack("<II", pid_, off_ + 4)
        gap += b"\xa5" * 4 + R[SI][si_table_at + 8 * si_count:]
        for which, (tr, rf) in (("traced", (swapped(R, SI, bytes(gap)), R)),
                                ("reference", (R, swapped(R, SI, bytes(gap))))):
            problems_, lines_ = verdict(tr, rf)
            check("W36-0e.1 a SummaryInformation whose first value does not follow the table in the %s save fails"
                  % which, problems_ >= 1 and any(SI in ln for ln in lines_), "%r" % (lines_,))
        # the LastSave slot is a type 0x40 FILETIME: the same eight bytes under another type are not noise
        for tword in (0x41, 0x1E, 0):
            slot_ = struct.pack("<I", tword) + si_have[13][4:]
            for which, (tr, rf) in (("traced", (swapped(R, SI, si_build(R[SI], {13: slot_})), R)),
                                    ("reference", (R, swapped(R, SI, si_build(R[SI], {13: slot_}))))):
                problems_, lines_ = verdict(tr, rf)
                check("W36-0e.1 LastSave with the type word %#x in the %s save fails" % (tword, which),
                      problems_ >= 1 and any(SI in ln for ln in lines_), "%r" % (lines_,))
        # the view GUID string has the shape {8-4-4-4-12}: the hyphens moved, the digits still a GUID, is not
        # a GUID string.  Both saves carry the moved shape and differ in a first digit, a masked place.
        moved = list(vs_text)
        moved[8], moved[9] = moved[9], moved[8]
        moved_b = list(moved)
        moved_b[1] = "4" if moved[1] != "4" else "5"
        for which, (tr, rf) in (("traced", (with_text(R, "".join(moved_b)), with_text(R, "".join(moved)))),
                                ("reference", (with_text(R, "".join(moved)), with_text(R, "".join(moved_b))))):
            problems_, lines_ = verdict(tr, rf)
            check("W36-0e.1 a view GUID string with a hyphen moved, differing in a digit, in the %s save fails"
                  % which, problems_ >= 1 and any(view_props in ln for ln in lines_), "%r" % (lines_,))
        pair_ = calls("canonical", g.canonical, R, True)
        check("W36-0e.1 canonical(streams, with_noise=True) is (canonical streams, noise offsets)",
              isinstance(pair_, tuple) and len(pair_) == 2 and set(pair_[1]) == set(R), "%r" % (type(pair_),))

        # streams(): three directory entries that render alike are one collision, named once, and the
        # later ones are kept apart
        class StubOle:
            def __init__(self, path):
                self.table = STUB[path]

            def listdir(self, streams=True, storages=False):
                return [list(k) for k in self.table]

            def openstream(self, entry):
                return io.BytesIO(self.table[tuple(entry)])

            def close(self):
                pass

        STUB = {"three": {("a\x01b",): b"1", ("a\x02b",): b"22", ("a\x03b",): b"333"}}
        real_ole = g.olefile
        g.olefile = types.SimpleNamespace(OleFileIO=StubOle)
        try:
            three = calls("streams", g.streams, "three")
        finally:
            g.olefile = real_ole
        if three is not RAISED:
            expect("three entries of one printable name: one collision", three.collisions, ["a?b"])
            expect("three entries of one printable name: none dropped",
                   sorted(three.items()), [("a?b", b"1"), ("a?b#1", b"22"), ("a?b#2", b"333")])

        # _text_ok: a control character that is the last one is not a trailing NUL; an odd length is no text
        expect("_text_ok refuses a text that ends in a control character",
               calls("_text_ok", g._text_ok, "ab\x01".encode("utf-16le")), False)
        expect("_text_ok refuses an odd length", calls("_text_ok", g._text_ok, b"a\0b"), False)

        # frame_records: records chain exactly to the end, each padded to an even length
        fr_head = bytes(16)
        expect("frame_records: a record longer than the data is not a chain",
               calls("frame_records", g.frame_records, fr_head + struct.pack("<III", 8, 1, 0) + b"abcd", 16), None)
        expect("frame_records: a last record of odd length with no pad byte is not a chain",
               calls("frame_records", g.frame_records, fr_head + struct.pack("<III", 1, 1, 0) + b"a", 16), None)
        expect("frame_records: a last record of odd length with its pad byte is a chain",
               calls("frame_records", g.frame_records, fr_head + struct.pack("<III", 1, 1, 0) + b"a\0", 16),
               [(16, 1, 1, 0)])
        expect("frame_records: a header that is cut short is not a chain",
               calls("frame_records", g.frame_records, fr_head + bytes(8), 16), None)

        # the CEdl path record is the record 0x26400031 only; another record of text that ends in "!" is data
        bang = "a!\0".encode("utf-16le")
        other_rec = bytes(20) + struct.pack("<III", len(bang), 0x26400030, 1) + bang
        path_rec = bytes(20) + struct.pack("<III", len(bang), g.CEDL_PATH_ID, 1) + bang
        expect("a CEdl record that is not the path record, text ending in '!', is not replaced",
               calls("_canon_cedl_var2", g._canon_cedl_var2, other_rec, {}), (other_rec, set(), set()))
        got_ = calls("_canon_cedl_var2", g._canon_cedl_var2, path_rec, {})
        expect("the CEdl path record is replaced (and named)", got_[2] if got_ is not RAISED else None, {"path"})
        expect("a CEdl stream shorter than its header is as it is",
               calls("_canon_cedl_var2", g._canon_cedl_var2, b"\0" * 10, {}), (b"\0" * 10, set(), set()))

        # CEdl VarMeta and 214/Props name the length of 214/CEdl/Var2Data: with no such stream, or a
        # VarMeta too short to hold the word, nothing is replaced and nothing raises
        expect("CEdl VarMeta with no CEdl Var2Data is as it is",
               calls("_canon_cedl_meta", g._canon_cedl_meta, R["???214/CEdl/VarMeta"], {}),
               (R["???214/CEdl/VarMeta"], set(), set()))
        expect("a CEdl VarMeta of 10 bytes is as it is",
               calls("_canon_cedl_meta", g._canon_cedl_meta, b"\0" * 10, {CEDL: b"x"}), (b"\0" * 10, set(), set()))
        expect("214/Props with no CEdl Var2Data is as it is",
               calls("_canon_view_props", g._canon_view_props, R[view_props], {}), (R[view_props], set(), set()))
        expect("a 214/Props of 10 bytes is as it is",
               calls("_canon_view_props", g._canon_view_props, b"\0" * 10, {CEDL: b"x"}), (b"\0" * 10, set(), set()))
        v2_ = b"v" * 40
        size_rec = fr_head + struct.pack("<III", 4, g.VIEW_CEDL_SIZE_ID, 1) + struct.pack("<I", len(v2_))
        got_ = calls("_canon_view_props", g._canon_view_props, size_rec, {CEDL: v2_})
        expect("214/Props record 0x00010007 equal to the CEdl length is replaced (and named)",
               (got_[2], got_[1]) if got_ is not RAISED else None, ({"rec 0x00010007"}, set(range(28, 32))))
        for what, rec_ in (("another id", struct.pack("<III", 4, 0x00010008, 1) + struct.pack("<I", len(v2_))),
                           ("a length of 8", struct.pack("<III", 8, g.VIEW_CEDL_SIZE_ID, 1) +
                            struct.pack("<II", len(v2_), 0))):
            expect("a 214/Props record of %s whose first word is the CEdl length is not replaced" % what,
                   calls("_canon_view_props", g._canon_view_props, fr_head + rec_, {CEDL: v2_}),
                   (fr_head + rec_, set(), set()))

        # the Props14 tail: [count][entries]; each entry starts with an id of the two names
        tail_one = struct.pack("<I", 1) + struct.pack("<II", 0x3540000B, 0x1E)
        expect("a tail shorter than its count word is none", calls("_tail_entries", g._tail_entries, b"\0\0"), None)
        expect("an empty tail is none", calls("_tail_entries", g._tail_entries, b""), None)
        expect("an id that is not a name followed by 0x1e inside a name is part of the name",
               calls("_tail_entries", g._tail_entries,
                     tail_one + b"aa" + struct.pack("<II", 0x1234, 0x1E)),
               [(0x3540000B, b"aa" + struct.pack("<II", 0x1234, 0x1E))])
        expect("a tail of count 0 and junk is none",
               calls("_tail_entries", g._tail_entries, struct.pack("<I", 0) + b"junkjunk"), None)
        expect("a tail of count 0 and nothing else is no entries",
               calls("_tail_entries", g._tail_entries, struct.pack("<I", 0)), [])
        expect("a tail whose count is not the number of entries is none",
               calls("_tail_entries", g._tail_entries, struct.pack("<I", 3) + tail_one[4:] + b"aa"), None)
        for n_ in (0, 8, 12, 14):
            expect("a Props14 of %d bytes is as it is" % n_,
                   calls("_canon_props14", g._canon_props14, bytes(n_), {}), (bytes(n_), set(), set()))
        bad_magic = bytearray(R[P14])
        struct.pack_into("<I", bad_magic, 8, 0x20000)
        expect("a Props14 blob with another magic word is as it is",
               calls("_canon_props14", g._canon_props14, bytes(bad_magic), {}), (bytes(bad_magic), set(), set()))
        p14_end = len(p14_head)
        for label, entries_, count_noise in (
                ("a lone 0x3540000b entry with a text name", [(0x3540000B, text_name)], True),
                ("a lone 0xffffffff entry with a text name", [(0xFFFFFFFF, text_name)], False),
                ("a lone 0x3540000b entry with a junk name", [(0x3540000B, junk)], False)):
            got_ = calls("_canon_props14", g._canon_props14, p14_join(p14_head, entries_), {})
            expect("%s: the count word is %s" % (label, "derived" if count_noise else "not derived"),
                   set(range(p14_end, p14_end + 4)) <= got_[1] if got_ is not RAISED else None, count_noise)
        got_ = calls("_canon_props14", g._canon_props14, p14_join(p14_head, [(first[0], junk), second]), {})
        junk_at = p14_end + 4 + 8
        expect("a junk tail name is not among the replaced bytes",
               got_[1].isdisjoint(range(junk_at, junk_at + len(junk))) if got_ is not RAISED else None, True)

        # a stamp and a FILETIME are whole values; short ones are no value and do not raise
        for raw_ in (b"", b"\x01\x02\x03"):
            expect("_stamp_ok of %d bytes is False" % len(raw_), calls("_stamp_ok", g._stamp_ok, raw_), False)
        for raw_ in (b"", b"\x01\x02", b"\x01" * 12):
            expect("_filetime_ok of %d bytes is False" % len(raw_), calls("_filetime_ok", g._filetime_ok, raw_), False)
        # the two 114/Props save words are told by their id and a length of 4 (W36-0e.2)
        word_a = struct.pack("<III", 4, 0x02400007, 1) + struct.pack("<I", 0x3D200000)
        word_b = struct.pack("<III", 4, 0x024013B1, 1) + struct.pack("<I", 0x3D200001)
        wide_b = struct.pack("<III", 8, 0x024013B1, 1) + struct.pack("<II", 0x3D200001, 0)
        other_rec = struct.pack("<III", 4, 0x02401394, 1) + struct.pack("<I", 0x3D200002)
        expect("_save_words_equal: two save words that differ is False",
               calls("_save_words_equal", getattr(g, "_save_words_equal", None), fr_head + word_a + word_b), False)
        expect("_save_words_equal: a record of another id is not a save word",
               calls("_save_words_equal", getattr(g, "_save_words_equal", None), fr_head + word_a + other_rec), True)
        expect("_save_words_equal: a save-word id with a length of 8 is not a save word",
               calls("_save_words_equal", getattr(g, "_save_words_equal", None), fr_head + word_a + wide_b), True)
        for raw_ in (b"", b"\x1e\0\0\0", b"\x1e\0\0\0\x01\0\0"):
            expect("_lpstr_slot_ok of %d bytes is False" % len(raw_), calls("_lpstr_slot_ok", g._lpstr_slot_ok, raw_),
                   False)
        expect("_is_v1_guid of 12 bytes of a version-1 GUID is False",
               calls("_is_v1_guid", g._is_v1_guid, v1(1)[:12]), False)

        # SummaryInformation: a stream that does not hold its property set as it says stays raw
        for n_ in (0, 20, 47, 50):
            expect("a SummaryInformation of %d bytes is as it is" % n_,
                   calls("_canon_summary", g._canon_summary, bytes(n_), {}), (bytes(n_), set(), set()))
        count_big = bytearray(R[SI])
        struct.pack_into("<I", count_big, si_sec + 4, 0xFFFFFFFF)
        count_zero = bytearray(R[SI])
        struct.pack_into("<I", count_zero, si_sec + 4, 0)
        beyond = bytearray(R[SI])
        ents_ = [struct.unpack_from("<II", R[SI], si_table_at + 8 * k) for k in range(si_count)]
        last_k = max(range(si_count), key=lambda k: ents_[k][1])
        struct.pack_into("<I", beyond, si_table_at + 8 * last_k + 4, len(R[SI]) - si_sec + 100)
        for what, blob_ in (("a count of 0xffffffff", count_big), ("a count of 0", count_zero),
                            ("a last value offset beyond the stream", beyond)):
            expect("a SummaryInformation with %s is as it is" % what,
                   calls("_canon_summary", g._canon_summary, bytes(blob_), {}), (bytes(blob_), set(), set()))

        # the font rows of a view's bag: found by parsing the bag block the VarMeta names
        def bag(records_, declared=None):
            body = bytes(16) + b"".join(struct.pack("<III", len(p_), rid_, kind_) + p_ + bytes(len(p_) & 1)
                                        for rid_, kind_, p_ in records_)
            return struct.pack("<I", len(body) if declared is None else declared) + body

        def vmeta(blocks_):
            return bytes(8) + struct.pack("<I", len(blocks_)) + bytes(12) + \
                b"".join(struct.pack("<III", *b_) for b_ in blocks_)

        rows2 = (g.FONT_ITEM_ID, 1, bytes(2 * g.FONT_ROW))
        bag_ok, meta_ok = bag([rows2]), vmeta([(1, 0, g.BAG_KEY)])
        expect("font_item_garbage: two font rows, payload +43 of each",
               calls("font_item_garbage", g.font_item_garbage, bag_ok, meta_ok), [75, 119])
        for what, data_, meta_ in (
                ("a block of another key", bag_ok, vmeta([(1, 0, g.BAG_KEY + 1)])),
                ("a font record of 50 bytes", bag([(g.FONT_ITEM_ID, 1, bytes(50))]), meta_ok),
                ("a font record longer than its block", bag([(g.FONT_ITEM_ID, 1, bytes(g.FONT_ROW))],
                                                            declared=16 + 12 + 10), meta_ok),
                ("a block offset past the stream", bag_ok, vmeta([(1, len(bag_ok) + 100, g.BAG_KEY)])),
                ("a block offset 0xffffffff", bag_ok, vmeta([(1, 0xFFFFFFFF, g.BAG_KEY)])),
                ("no VarMeta", bag_ok, None), ("a VarMeta of 10 bytes", bag_ok, bytes(10))):
            expect("font_item_garbage: %s finds nothing" % what,
                   calls("font_item_garbage", g.font_item_garbage, data_, meta_), [])
        expect("noise_windows: the font rows of a CV_iew bag", calls("noise_windows", g.noise_windows,
               g.CVIEW_VAR2, bag_ok, meta_ok, {}, bag_ok), {75, 119})
        expect("noise_windows: a bag of another stream's VarMeta is not font rows",
               calls("noise_windows", g.noise_windows, "???114/TBkndAssn/Var2Data", bag_ok, meta_ok, {}, bag_ok),
               set())

        # noise_windows: a Props record is a word by its id and length 4, a GUID by its id and length 16,
        # a GUID string by its id; with no other side it is compared with itself
        pr_word = fr_head + struct.pack("<III", 8, 0x02400007, 1) + struct.pack("<II", 0x3D200000, 0x3D200001)
        pr_guid = fr_head + struct.pack("<III", 20, 0x02400029, 1) + v1(1) + bytes(4)
        pr_ok = fr_head + struct.pack("<III", 16, 0x02400029, 1) + v1(1)
        expect("noise_windows: a word record of 8 bytes is not a save word",
               calls("noise_windows", g.noise_windows, props114, pr_word, None, {}, pr_word), set())
        expect("noise_windows: a GUID record of 20 bytes is not a GUID",
               calls("noise_windows", g.noise_windows, props114, pr_guid, None, {}, pr_guid), set())
        expect("noise_windows: a GUID record of 16 bytes holding a version-1 GUID is noise",
               calls("noise_windows", g.noise_windows, props114, pr_ok, None, {}, pr_ok), set(range(28, 44)))
        expect("noise_windows: with no other side the stream is compared with itself",
               calls("noise_windows", g.noise_windows, props114, pr_ok), set(range(28, 44)))
        gstr = guid_text(v1(1)).encode("utf-16le")
        digits_at = {28 + 2 * i for i in range(38) if i not in (0, 9, 14, 19, 24, 37)}   # the 32 hex digits
        for rid_, want_ in ((0x03400002, set()), (0x03400001, digits_at)):
            gs_ = fr_head + struct.pack("<III", len(gstr), rid_, 1) + gstr
            expect("noise_windows: 214/Props record %#010x holding a GUID string" % rid_,
                   calls("noise_windows", g.noise_windows, view_props, gs_, None, {}, gs_), want_)

        # row identity: a stream is named by its rows only when its length fits its stride and FixedData
        t_fixed, t_fixed2 = "???114/TBkndTask/FixedData", "???114/TBkndTask/Fixed2Data"
        expect("row_ids of a stream with no identity rule is None", calls("row_ids", g.row_ids, "nope", b"", {}), None)
        expect("row_ids of a missing stream is None", calls("row_ids", g.row_ids, t_fixed, None, {}), None)
        expect("row_ids of a FixedData with a stray byte is None",
               calls("row_ids", g.row_ids, t_fixed, R[t_fixed] + b"\0", R), None)
        expect("row_ids of a Fixed2Data with no FixedData is None",
               calls("row_ids", g.row_ids, t_fixed2, R[t_fixed2], {}), None)
        expect("row_ids of a Fixed2Data whose FixedData has a stray byte is None",
               calls("row_ids", g.row_ids, t_fixed2, R[t_fixed2], dict(R, **{t_fixed: R[t_fixed] + b"\0"})), None)
        expect("row_ids of the pair's own Fixed2Data names every row",
               len(calls("row_ids", g.row_ids, t_fixed2, R[t_fixed2], R) or []),
               len(R[t_fixed2]) // g.STRIDE[t_fixed2][1])
        expect("_task_uids of no stream is empty", calls("_task_uids", g._task_uids, None), set())
        expect("_meta_blocks of no stream is empty", calls("_meta_blocks", g._meta_blocks, None), [])
        expect("_meta_blocks of 10 bytes is empty", calls("_meta_blocks", g._meta_blocks, bytes(10)), [])

        # the control mask: a row named twice in the control is not masked; no control or null is no mask
        expect("_mask_by_id: a row identity twice in the control is not masked",
               calls("_mask_by_id", g._mask_by_id, (0, 4), 8, bytes(8), b"\0\0\0\0\1\0\0\0", ["a", "a"], ["a"]), set())
        expect("control_mask with no control is None",
               calls("control_mask", g.control_mask, "x", 4, None, b"abcd"), None)
        expect("control_mask with no null is None",
               calls("control_mask", g.control_mask, "x", 4, b"abcd", None), None)

        # the command line: the wrong number of arguments is usage, exit 2; with no olefile it says so, exit 2
        p_ = subprocess.run([sys.executable, gate_path, ref, ref, ctl], capture_output=True, text=True)
        check("W36-0e.1 the gate with three arguments prints usage and exits 2",
              p_.returncode == 2 and "Traceback" not in p_.stderr, "rc=%d %s" % (p_.returncode, p_.stderr[-80:]))
        hide = "import sys, runpy; sys.modules['olefile'] = None; sys.argv = sys.argv[1:]; " \
               "runpy.run_path(sys.argv[0], run_name='__main__')"
        p_ = subprocess.run([sys.executable, "-c", hide, gate_path, ref, ref, ctl, nul],
                            capture_output=True, text=True)
        check("W36-0e.1 the gate with no olefile says so and exits 2",
              p_.returncode == 2 and "install olefile" in p_.stdout, "rc=%d %s" % (p_.returncode, p_.stderr[-80:]))

        # W36-0.3 at the function level
        lines, problems, checked, unchecked = g.compare(R, R, C, None)
        check("W36-0.3 compare with no null: one problem 'no null'",
              problems == 1 and lines == ["no null"], "%r" % (lines,))
finally:
    shutil.rmtree(tmp, ignore_errors=True)

print("%d failed" % len(failures) if failures else "all passed")
if failures:
    sys.exit(1)
# W36-0d.8: the guards are removed one at a time in copies of the gate and this test must go red on
# each.  Only the plain run does it: --gate, --gate-dir and --cli-only run another gate or a subset,
# and the meta-test runs this file itself with --no-meta.
if not no_meta and gate_dir == HERE and gate_path == os.path.join(HERE, "trace_gate.py") and not cli_only:
    print("--- guard meta-test (test_gate_guards_meta.py)")
    sys.stdout.flush()
    sys.exit(subprocess.run([sys.executable, os.path.join(HERE, "test_gate_guards_meta.py"),
                             "--skip-baseline"]).returncode)
sys.exit(0)
