#!/usr/bin/env python3
"""Bisect why MS Project refuses the in-place-patched file (writer §8).

Builds byte-level variants of corpus/probe/base.mpp -- one field each -- by
locating the 856-byte TBkndTask/FixedData payload inside the file and editing it
in place, exactly the primitive core/mppwrite's InPlacePatcher uses. Then opens
each variant in Project and records what Project shows, so the rejection can be
attributed to one field.

    python scripts/oracle_patch_variants.py make     # build corpus/probe/pv-*.mpp
    python scripts/oracle_patch_variants.py open     # open each and record
    python scripts/oracle_patch_variants.py diff     # compare with the C++ output
"""

import argparse
import datetime as dt
import json
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import olefile  # noqa: E402
from oracle_lock import oracle_session, log, project_pids  # noqa: E402
from oracle_w3 import Guard, read_task, rows_of, save, val  # noqa: E402
from oracle_writer import window_texts  # noqa: E402

PROBE = os.path.join(ROOT, "corpus", "probe")
BASE = os.path.join(PROBE, "base.mpp")
OUT = os.path.join(ROOT, "docs", "analysis", "oracle")
ROW = 1                 # task A is the patched row everywhere
OFF = 48 + 202 * ROW    # start of that row inside FixedData


def fixed_data(path):
    ole = olefile.OleFileIO(path)
    try:
        for p in ole.listdir(streams=True, storages=False):
            if "/".join(x.strip() for x in p).endswith("TBkndTask/FixedData"):
                return ole.openstream(p).read()
    finally:
        ole.close()
    raise SystemExit("no FixedData")


def stream_bytes(path, suffix):
    ole = olefile.OleFileIO(path)
    try:
        for p in ole.listdir(streams=True, storages=False):
            if "/".join(x.strip() for x in p) == suffix:
                return ole.openstream(p).read()
    finally:
        ole.close()
    raise SystemExit(f"no stream {suffix}")


def u32(b, off):
    return struct.unpack_from("<I", b, off)[0]


def p32(b, off, v):
    struct.pack_into("<I", b, off, v)


DATE_0302 = 0x3C2A12C0          # 2026-03-02 08:00 (day 15402); 0x3C2E is 03-06


def variants():
    """name -> list of (label, apply(fixed_bytes))"""
    def priority(b):
        struct.pack_into("<H", b, OFF + 78, 900)

    def percent(b):
        struct.pack_into("<H", b, OFF + 92, 50)

    def percent_consistent(b):
        struct.pack_into("<H", b, OFF + 92, 50)
        p32(b, OFF + 80, 12000)       # ActualDuration = 2.5 d
        p32(b, OFF + 88, 12000)       # RemainingDuration

    def duration(b):
        p32(b, OFF + 84, 33600)

    def duration_consistent(b):
        p32(b, OFF + 84, 33600)
        p32(b, OFF + 88, 33600)       # RemainingDuration

    def start90(b):
        p32(b, OFF + 96, DATE_0302)

    def start_all(b):
        for o in (96, 104, 112):
            p32(b, OFF + o, DATE_0302)
        for o in (100, 108, 116):
            p32(b, OFF + o, 0x3c3617d8)   # 2026-03-10 17:00

    def duration_both(b):
        p32(b, OFF + 84, 33600)
        # Fixed2Data carries a second copy: entry (row+3), duration at +58
        p32(b, 64 * (ROW + 3) + 58, 33600)

    def all_four(b):
        priority(b)
        percent(b)
        duration(b)
        start90(b)

    return {
        "priority": [("+78 = 900", priority)],
        "percent": [("+92 = 50", percent)],
        "percent-consistent": [("+92 = 50, +80/+88 = 12000", percent_consistent)],
        "duration": [("+84 = 33600", duration)],
        "duration-consistent": [("+84/+88 = 33600", duration_consistent)],
        "start-96-only": [("+96 = 2026-03-02 08:00", start90)],
        "start-all-copies": [("+96/+104/+112 and +100/+108/+116", start_all)],
        "duration-both-tables": [("FixedData +84 and Fixed2Data +58 = 33600",
                                  duration_both)],
        "all-four": [("priority+pct+duration+start (+96 only)", all_four)],
    }


def build(name, edits):
    data = bytearray(open(BASE, "rb").read())
    if name == "props-510":
        # exactly what tests/inplace_patch.cpp does: a scratch u32 at stream
        # offset 510 of 114/Props, the FAT-chained path.
        props = stream_bytes(BASE, "114/Props")
        at = data.find(props[:128])
        if at < 0 or data.find(props[:128], at + 1) >= 0:
            raise SystemExit("114/Props payload not uniquely locatable")
        struct.pack_into("<I", data, at + 510, 0xA1B2C3D4)
        path = os.path.join(PROBE, f"pv-{name}.mpp")
        with open(path, "wb") as fh:
            fh.write(data)
        return path
    fixed = fixed_data(BASE)
    at = data.find(fixed)
    if at < 0:
        raise SystemExit("FixedData payload not found verbatim in base.mpp")
    if data.find(fixed, at + 1) >= 0:
        raise SystemExit("FixedData payload occurs more than once; refusing to guess")
    buf = bytearray(fixed)
    for _, fn in edits:
        fn(buf)
    data[at:at + len(fixed)] = buf
    path = os.path.join(PROBE, f"pv-{name}.mpp")
    with open(path, "wb") as fh:
        fh.write(data)
    return path


def cmd_make(args):
    path = build("props-510", [("+510 = 0xA1B2C3D4", lambda b: None)])
    log(f"{os.path.relpath(path, ROOT):34} {os.path.getsize(path):,} B  "
        f"114/Props +510 = 0xA1B2C3D4 (exactly the C++ test's scratch)")
    for name, edits in variants().items():
        path = build(name, edits)
        log(f"{os.path.relpath(path, ROOT):34} "
            f"{os.path.getsize(path):,} B  {', '.join(e[0] for e in edits)}")
    # the same four edits the C++ test made, for an independent comparison
    cpp = os.path.join(ROOT, "core", "mppwrite", "build", "vs", "test-out",
                       "inplace-patched.mpp")
    mine = os.path.join(PROBE, "pv-all-four.mpp")
    if os.path.exists(cpp):
        a, b = open(cpp, "rb").read(), open(mine, "rb").read()
        diff = [i for i in range(min(len(a), len(b))) if a[i] != b[i]]
        log(f"python patcher vs C++ InPlacePatcher: {len(diff)} differing bytes")
        if diff:
            log(f"  first differences at {diff[:8]}")
        else:
            log("  identical -- the file content is the variable, not our edit path")


def cmd_open(args):
    files = sorted(f for f in os.listdir(PROBE)
                   if f.startswith("pv-") and f.endswith(".mpp"))
    results = {}
    with oracle_session() as app:
        with Guard(240):
            for f in files:
                path = os.path.join(PROBE, f)
                rec = {"file": f}
                try:
                    app.FileOpen(path)
                except Exception as exc:                # noqa: BLE001
                    rec["open_error"] = f"{exc.__class__.__name__}: {exc}"
                time.sleep(1)
                proj = app.ActiveProject
                if proj is None:
                    rec["opened"] = False
                    rec["windows"] = window_texts((project_pids() or [None])[0])
                    log(f"  {f:30} REFUSED (no document open)")
                else:
                    rec["opened"] = True
                    rec["name"] = val(getattr(proj, "Name", None))
                    try:
                        rec["tasks"] = rows_of(proj)
                        log(f"  {f:30} opened: " + "; ".join(
                            f"uid{t['ID']} prio={t['Priority']} "
                            f"pct={t['PercentComplete']} dur={t['Duration']} "
                            f"start={t['Start']} finish={t['Finish']}"
                            for t in rec["tasks"]))
                    except Exception as exc:            # noqa: BLE001
                        rec["read_error"] = f"{exc.__class__.__name__}: {exc}"
                        log(f"  {f:30} opened but read failed: {rec['read_error']}")
                    try:
                        app.FileClose(0)
                    except Exception as exc:            # noqa: BLE001
                        rec["close_error"] = f"{exc.__class__.__name__}: {exc}"
                results[f] = rec
    save("patch-variants", results)


CASES = {"make": cmd_make, "open": cmd_open}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("case", choices=sorted(CASES))
    args = ap.parse_args()
    CASES[args.case](args)


if __name__ == "__main__":
    main()
