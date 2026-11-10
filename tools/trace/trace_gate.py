#!/usr/bin/env python3
"""Trace gate (docs/handoff/trace-runbook.md section 5): does a traced save equal the committed
pair's file, outside the null-save noise?  Runs INSIDE THE TRACE VM, where the saved file stays.

    python trace_gate.py TRACED REFERENCE CONTROL CONTROL_NULL

TRACED       the traced save (C:\\trace\\work\\<target>.mpp)
REFERENCE    the committed file the trace must reproduce (the pair's edited file, or the
             committed null save for the null target)
CONTROL, CONTROL_NULL
             the committed control and its committed null save; the bytes where these two
             differ are the noise mask, and a stream whose size they change is skipped whole.

Prints one summary line, then one line per problem stream (no names, no paths, only stream
paths and byte counts), then the SHA-256 of everything printed before it, so a copy read off
the screen can be checked.  Exit 0 when there are no problems, 1 otherwise.
"""

import hashlib
import sys

import olefile


def streams(path):
    ole = olefile.OleFileIO(path)
    try:
        return {"/".join("".join(ch if " " < ch < "\x7f" else "?" for ch in part) for part in s):
                ole.openstream(s).read() for s in ole.listdir(streams=True, storages=False)}
    finally:
        ole.close()


def main(argv):
    if len(argv) != 5:
        print(__doc__)
        return 2
    traced, ref, ctl, nul = (streams(p) for p in argv[1:])
    lines = []
    problems = 0
    checked = 0
    names = sorted(set(traced) | set(ref))
    for name in names:
        if name not in ref:
            lines.append("only-in-traced %s %d" % (name, len(traced[name])))
            problems += 1
            continue
        if name not in traced:
            lines.append("only-in-reference %s %d" % (name, len(ref[name])))
            problems += 1
            continue
        c, n = ctl.get(name), nul.get(name)
        if c is not None and n is not None and len(c) != len(n):
            lines.append("noise-whole %s %d->%d" % (name, len(ref[name]), len(traced[name])))
            continue
        a, b = ref[name], traced[name]
        if len(a) != len(b):
            lines.append("size-differs %s %d->%d" % (name, len(a), len(b)))
            problems += 1
            continue
        checked += 1
        mask = set()
        if c is not None and n is not None:
            mask = {i for i in range(len(c)) if c[i] != n[i]}
        diff = sum(1 for i in range(len(a)) if a[i] != b[i] and i not in mask)
        if diff:
            lines.append("bytes-differ %s %d" % (name, diff))
            problems += 1
    head = "%d streams, %d checked, %d problems" % (len(names), checked, problems)
    text = "\n".join([head] + lines) + "\n"
    sys.stdout.write(text)
    print("sha256 " + hashlib.sha256(text.encode("ascii")).hexdigest())
    return 0 if problems == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
