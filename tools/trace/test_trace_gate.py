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
import shutil
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


def head_counts(out):
    first = out.splitlines()[0].split()
    return int(first[0]), int(first[2]), int(first[4])   # streams, checked, problems


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

        def _nrec(mod, stream, blob):
            return mod.noise_windows(stream, blob)[1]

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
            path_rule = g._path_rule(name)
            free = [i for i in range(len(data)) if i not in mask and i not in win]
            if path_rule is not None:
                # a path stream tolerates up to `same` differing bytes: flip one more
                count = path_rule["same"] + 1
                free = free[:count]
                if len(free) < count:
                    skipped.append(name + " (no free bytes)")
                    continue
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
        lines, problems, checked, unchecked = g.compare(
            {"Props14": R["Props14"] + b"\0" * 10}, {"Props14": R["Props14"]}, C, N)
        check("W36-0.4 nothing checked: a problem 'nothing checked'",
              checked == 0 and problems == 1 and "nothing checked" in lines, "%r" % (lines,))

        # a size change in a path-bearing stream: small is noise, large is a problem
        name = "???214/CEdl/Var2Data"
        for grow, want in ((10, 0), (500, 1)):
            mutated = dict(R)
            mutated[name] = R[name] + b"\0" * grow
            lines, problems, checked, unchecked = g.compare(mutated, R, C, N)
            check("W36-0.1 CEdl/Var2Data +%d bytes -> %d problem(s)" % (grow, want),
                  problems == want, "%r" % (lines,))

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
