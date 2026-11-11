#!/usr/bin/env python3
"""Guards for tools/trace/trace_gate.py (wave 36 G0, W36-0.1 to 0.4).

Runs the gate on committed pairs (tests/data) and asserts it FAILS where it must:

  W36-0.1  no by-name whole-stream waiver: a flipped byte in a stream the old gate skipped whole
           (214/CEdl/Var2Data, Props14) is no longer hidden;
  W36-0.2  no wrong-length mask: a control/null difference is not applied to a reference of
           another length, and a fixed-stride stream is masked record by record;
  W36-0.3  an absent control null, or the control passed as its own null, is a FAIL "no null";
  W36-0.4  one flipped byte in EVERY stream of a committed pair (at an offset the control mask
           and the noise windows do not cover) is a FAIL, with `checked > 0` and an equal stream
           set; the unflipped pair is a PASS.

    python test_trace_gate.py [--gate PATH] [--cli-only]

--gate runs the command-line cases against another copy of the gate (a red run against an older
version); --cli-only skips the cases that import the gate's functions.  Exit 0 all pass, 1 a case
failed, 77 olefile is not installed (ctest SKIP).
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
args = sys.argv[1:]
while args:
    a = args.pop(0)
    if a == "--gate":
        gate_path = args.pop(0)
        cli_only = True
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
        sys.path.insert(0, HERE)
        import trace_gate as g

        def load(path):
            return g.streams(path)

        R, C, N = load(ref), load(ctl), load(nul)

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
        found = g.font_item_garbage(FR[view], FR[view_meta])
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
        # save outside SummaryInformation.  Two pairs are known not to be a same-state control and
        # null (their records differ in length), so they FAIL the positive case; two edits equal
        # their control outside the noise (a no-op percent edit), so they PASS the negative one.
        NOT_SAME_STATE = {"wave30-inactivate", "wave35-g3-assign-remove-port-saved"}
        NOOP_EDITS = {"wave28-percent-auto100", "wave28-percent-auto40"}
        stems = sorted(f[:-len("-control-null.mpp")] for f in os.listdir(DATA)
                       if f.endswith("-control-null.mpp"))
        pos_n = pos_bad = neg_n = neg_bad = 0
        pos_failed, neg_passed = set(), set()
        for stem in stems:
            e_path, c_path, n_path = pair(stem)
            SC, SN = load(c_path), load(n_path)
            lines, problems, checked, unchecked = g.compare(SN, SC, SC, SN)
            pos_n += 1
            if problems:
                pos_failed.add(stem)
                check("W36-0b.2 positive %s fails only on CEdl/Var2Data size" % stem,
                      all(ln.startswith("size-differs ???214/CEdl/Var2Data") for ln in lines),
                      "%r" % (lines,))
            if not os.path.exists(e_path):
                continue
            SE = load(e_path)
            if all(SE.get(n) == SN.get(n) for n in set(SE) | set(SN) if n != "?SummaryInformation"):
                continue
            neg_n += 1
            lines, problems, checked, unchecked = g.compare(SN, SE, SC, SN)
            if not problems:
                neg_passed.add(stem)
        print("W36-0b.2 positive: %d pairs, %d FAIL; negative: %d edits, %d PASS" %
              (pos_n, len(pos_failed), neg_n, len(neg_passed)))
        check("W36-0b.2 positive sweep: every pair PASSes except the two not-same-state pairs",
              pos_failed == NOT_SAME_STATE, "%r" % (sorted(pos_failed),))
        check("W36-0b.2 negative sweep: no edit PASSes except the two no-op edits",
              neg_passed == NOOP_EDITS, "%r" % (sorted(neg_passed),))
        for stem in sorted(NOOP_EDITS):
            e_path, c_path, n_path = pair(stem)
            lines, problems, checked, unchecked = g.compare(load(e_path), load(c_path),
                                                            load(c_path), load(n_path))
            check("W36-0b.2 %s: the no-op edit equals its control outside the noise" % stem,
                  problems == 0, "%r" % (lines,))

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

        def zero_or_v1(raw):
            return len(raw) == 16 and (not any(raw) or (raw[6] >> 4 == 1 and raw[8] >> 6 == 2))

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

        def spec_windows(name, data, meta, ctl):
            """The offsets of the named noise fields of one stream, by the evidence note."""
            out = set()
            for row in range(len(data) // ROW_SIZE[name] if name in ROW_SIZE else 0):
                for lo in ROW_GUIDS[name]:
                    at = row * ROW_SIZE[name] + lo
                    if zero_or_v1(data[at:at + 16]):
                        out.update(range(at, at + 16))
            for at in ABS_GUIDS.get(name, ()):
                if zero_or_v1(data[at:at + 16]):
                    out.update(range(at, at + 16))
            if name == "???114/Props":
                for at, length, rid in records(data, 16):
                    if rid in PROPS_WORDS and length == 4:
                        out.update(range(at, at + 4))
                    if rid in PROPS_GUIDS and zero_or_v1(data[at:at + length]):
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
                out.update(g.font_item_garbage(data, meta))
            return out

        def window_audit(stem, only=None):
            """Problems found in the gate's windows of one pair, by name; the empty list is clean."""
            rp, cp, np_ = pair(stem)
            RS, CS = load(rp), load(cp)
            canon_noise = g.canonical(RS, with_noise=True)[1]
            found = []
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
                    if name in g.CANON:
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
                starts = [s for s in starts if zero_or_v1(data[s:s + 16])]
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
        for at in g.font_item_garbage(AR[cview], AR["???214/CV_iew/VarMeta"]):
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

        # W36-0.2: a mask is built only from equal-length control and null, and per record
        # for a fixed-stride stream
        stride_name = "???114/TBkndTask/Fixed2Data"
        ctl_s = bytearray(128)
        nul_s = bytearray(128)
        nul_s[64 + 60] = 1                       # row 1, +60: noise in the control
        ref_len = 192                            # the reference has a third row
        mask = g.control_mask(stride_name, ref_len, bytes(ctl_s), bytes(nul_s))
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

        # W36-0.3 at the function level
        lines, problems, checked, unchecked = g.compare(R, R, C, None)
        check("W36-0.3 compare with no null: one problem 'no null'",
              problems == 1 and lines == ["no null"], "%r" % (lines,))
finally:
    shutil.rmtree(tmp, ignore_errors=True)

print("%d failed" % len(failures) if failures else "all passed")
sys.exit(1 if failures else 0)
