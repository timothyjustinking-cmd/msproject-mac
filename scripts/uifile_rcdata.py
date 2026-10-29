#!/usr/bin/env python3
"""Classify the PJINTL RT_RCDATA resources (1,071 blobs) and decode the
localised string tables that dominate them.

The layout (all lengths in bytes), verified on 638, 431, 14 and 1078:

    u16 count
    count * { u32 id, u32 textOffset, u16 byteLength }   (10 bytes/record)
    UTF-16LE text at absolute offsets

Everything that does not fit is reported as opaque, not forced into a
grammar.

Usage:
  python scripts/uifile_rcdata.py [--res RESDIR] [--outdir docs/analysis/ui]
"""
from __future__ import annotations

import argparse
import collections
import csv
import pathlib
import re
import struct
import sys

DEFAULT_RES = pathlib.Path(
    r"D:\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile\analysis\res\pjintl"
)
DEFAULT_OUT = pathlib.Path(
    r"D:\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile\docs\analysis\ui"
)

U16_RE = re.compile(rb"(?:[\x20-\x7e]\x00){3,}")
ASCII_RE = re.compile(rb"[\x20-\x7e]{4,}")


def utf16_runs(b: bytes) -> list[str]:
    out = []
    for m in U16_RE.finditer(b):
        try:
            s = m.group().decode("utf-16le")
        except UnicodeDecodeError:
            continue
        if s.strip():
            out.append(s)
    return out


def ascii_runs(b: bytes) -> list[str]:
    return [m.group().decode("ascii") for m in ASCII_RE.finditer(b)]


def _text(b: bytes, off: int, ln: int) -> str:
    return b[off : off + ln].decode("utf-16le", "replace")


def _printable(b: bytes, off: int, ln: int) -> bool:
    if ln <= 0 or ln % 2 or off < 0 or off + ln > len(b):
        return False
    s = _text(b, off, ln)
    if not s:
        return False
    good = sum(1 for ch in s if 32 <= ord(ch) < 127)
    return good / len(s) >= 0.9


def try_table(b: bytes):
    """Try the two 10-byte record layouts.  Returns the better fit or None."""
    if len(b) < 24:
        return None
    count = struct.unpack_from("<H", b, 0)[0]
    end = 2 + 10 * count
    if count < 8 or end > len(b):
        return None
    best = None
    for layout, fmt in (("HIHH", "<HIHH"), ("IIH", "<IIH")):
        recs, pads = [], 0
        bad = False
        for i in range(count):
            v = struct.unpack_from(fmt, b, 2 + 10 * i)
            if layout == "IIH":
                idx, off, ln = v
            else:
                pad, off, ln, idx = v
                pads += pad == 0
            # The first string may start 2 bytes before index_end (observed
            # in 638 and 431); allow that slop but nothing below.
            if off + ln > len(b) or off < end - 2:
                bad = True
                break
            recs.append((idx, off, ln))
        if bad:
            continue
        if layout == "HIHH" and pads / count < 0.98:
            continue
        ok = sum(1 for _, off, ln in recs if _printable(b, off, ln))
        if ok / count < 0.9:
            continue
        if best is None or ok > best["ok"]:
            best = {"family": layout, "count": count, "index_end": end,
                    "ok": ok,
                    "recs": [r for r in recs if _printable(b, r[1], r[2])]}
            break
    return best


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--res", default=str(DEFAULT_RES))
    ap.add_argument("--outdir", default=str(DEFAULT_OUT))
    ap.add_argument("--top", type=int, default=20)
    args = ap.parse_args()

    root = pathlib.Path(args.res) / "RT_RCDATA"
    files = sorted(root.glob("*.bin"), key=lambda p: int(p.stem.split("_")[0]))
    if not files:
        print(f"no blobs under {root}", file=sys.stderr)
        return 1

    rows, tables = [], []
    for p in files:
        b = p.read_bytes()
        rid = int(p.stem.split("_")[0])
        tb = try_table(b)
        kind = f"strtab{tb['family']}" if tb else "opaque"
        rows.append(dict(rid=rid, size=len(b), kind=kind, tb=tb,
                         head=b[:16].hex(),
                         u16=[] if tb else utf16_runs(b),
                         ascii=[] if tb else ascii_runs(b)))
        if tb:
            tables.append((rid, len(b), tb, b))

    by_kind = collections.Counter(r["kind"] for r in rows)
    print(f"{len(rows)} RT_RCDATA blobs: {dict(by_kind)}")
    print(f"string tables: {len(tables)}, strings recovered: "
          f"{sum(len(t['recs']) for _, _, t, _ in tables)}")

    print("\nlargest string tables:")
    for rid, size, t, b in sorted(tables, key=lambda x: -len(x[2]["recs"]))[:args.top]:
        first = _text(b, t["recs"][0][1], t["recs"][0][2]) if t["recs"] else ""
        print(f"  RCDATA {rid:5} {size:7}B family {t['family']} "
              f"count={t['count']:5} first={first[:70]!r}")

    groups = collections.Counter(r["head"][:8] for r in rows if r["kind"] == "opaque")
    print(f"\nopaque blobs: {sum(groups.values())}, "
          f"distinct first-8-hex signatures: {len(groups)}")
    for sig, n in groups.most_common(10):
        print(f"  {sig} n={n}")

    outdir = pathlib.Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    with open(outdir / "rcdata_scan.tsv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["rid", "bytes", "kind", "head16", "n_strings", "sample"])
        for r in rows:
            n = len(r["tb"]["recs"]) if r["tb"] else 0
            sample = ""
            if r["tb"] and r["tb"]["recs"]:
                b = (root / (str(r["rid"]) + "_1033.bin")).read_bytes()
                idx, off, ln = r["tb"]["recs"][0]
                sample = _text(b, off, ln)
            w.writerow([r["rid"], r["size"], r["kind"], r["head"], n, sample[:200]])

    with open(outdir / "rcdata_strings.tsv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["resource", "family", "string_id", "text"])
        for rid, size, t, b in tables:
            for idx, off, ln in t["recs"]:
                w.writerow([rid, t["family"], idx, _text(b, off, ln)])
    print(f"\nwrote rcdata_scan.tsv, rcdata_strings.tsv to {outdir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
