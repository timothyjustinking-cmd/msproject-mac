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
        # one byte beyond the equal-size ceiling would be needed for a path stream, so flip a
        # block of bytes that is larger than the ceiling
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

        def _nrec(mod, stream, blob):
            return mod.noise_windows(stream, blob)[1]

        CANON_FLIP = {"???214/CEdl/Var2Data": 12, "???214/CEdl/VarMeta": 8,
                      "???214/Props": 0, "Props14": 8, "?SummaryInformation": 0}
        tested = 0
        skipped = []
        for name in sorted(R):
            data = R[name]
            if not data:
                skipped.append(name + " (empty)")
                continue
            mask = g.control_mask(name, len(data), C.get(name), N.get(name)) or set()
            win, _ = g.noise_windows(name, data, R.get(name[:-len("Var2Data")] + "VarMeta")
                                     if name.endswith("Var2Data") else None)
            free = [i for i in range(len(data)) if i not in mask and i not in win]
            if name in g.CANON:
                # a parsed stream: flip a header byte that precedes every noise field, so the
                # raw and canonical offsets agree (item 7 flips every offset outside the fields)
                at = CANON_FLIP[name]
                if at in mask or at in win:
                    skipped.append(name + " (no free bytes)")
                    continue
                free = [at]
            elif not free:
                # every byte is mask or window: flip one more window byte than the ceiling allows
                over = [i for i in sorted(win) if i not in mask and i < len(data)]
                count = g.ceiling_for(name, _nrec(g, name, data)) + 1
                free = over[:count]
                if len(free) < count:
                    skipped.append(name + " (no free bytes)")
                    continue
            else:
                free = free[:1]
            mutated = dict(R)
            b = bytearray(data)
            for i in free:
                b[i] ^= 0xFF
            mutated[name] = bytes(b)
            lines, problems, checked, unchecked = g.compare(mutated, R, C, N)
            hit = any(name in ln for ln in lines)
            check("W36-0.4 flip in %s fails" % name, problems >= 1 and hit and checked > 0,
                  "%r" % (lines,))
            tested += 1
        # only an empty stream may go unflipped (a stream with no byte outside the mask and
        # windows would be a gate that checks nothing there)
        check("W36-0.4 every non-empty stream was flipped (%d streams)" % tested,
              tested > 0 and all(s.endswith("(empty)") for s in skipped), "%r" % (skipped,))

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

        # noise windows hold within the ceiling and fail beyond it
        name = "???114/TBkndAssn/Fixed2Data"
        for count, want in ((8, 0), (200, 1)):
            mutated = dict(R)
            b = bytearray(R[name])
            idx = [i for i in range(len(b)) if i % 48 < 32 and i not in
                   (g.control_mask(name, len(b), C.get(name), N.get(name)) or set())][:count]
            for i in idx:
                b[i] ^= 0xFF
            mutated[name] = bytes(b)
            lines, problems, checked, unchecked = g.compare(mutated, R, C, N)
            check("W36-0.1 Assn Fixed2Data GUID window: %d bytes -> %d problem(s)" % (len(idx), want),
                  problems == want, "%r" % (lines,))

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
