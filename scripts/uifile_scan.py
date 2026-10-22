#!/usr/bin/env python3
"""Scan the PJINTL UIFILE blobs.

Container hypothesis (see spec/ui-inventory.md):
  0x00 u16  0x1002            magic/version
  0x02 char[4] "NUB\\0"        tag
  0x06 u32  12                constant (header/format version)
  0x0a u16  0x0301            constant (format version 1.3)
  0x0c u8   flag              0 or 1
  0x0d u32  size              file length in bytes (verified 71/71)
  0x11 ...  payload

This script does not claim to decode the payload grammar. It extracts the
evidence needed to argue about it: header fields, printable identifier runs,
embedded Lua 5.4 chunks, entropy.

Usage:
  python scripts/uifile_scan.py [RESDIR] [--tsv out.tsv]
"""
from __future__ import annotations

import argparse
import collections
import math
import pathlib
import re
import struct
import sys

DEFAULT_RES = pathlib.Path(
    r"D:\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile\analysis\res\pjintl"
)

# Printable identifier runs.  Deliberately permissive; the spec only uses names
# that are also referenced by their length prefix.
IDENT_RE = re.compile(rb"[A-Za-z_][A-Za-z0-9_.:]{2,}")


def entropy(b: bytes) -> float:
    if not b:
        return 0.0
    c = collections.Counter(b)
    n = len(b)
    return -sum(v / n * math.log2(v / n) for v in c.values())


def prefix_names(b: bytes) -> list[tuple[int, str]]:
    """Names stored as 0x01 <u16 len> <ascii>: the 4811/4724/4780 encoding."""
    out = []
    for m in re.finditer(rb"\x01(..)", b, re.S):
        n = struct.unpack("<H", m.group(1))[0]
        if 3 <= n <= 200:
            s = b[m.end() : m.end() + n]
            if len(s) == n and all(32 <= c < 127 for c in s):
                txt = s.decode("ascii")
                if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.:]*", txt):
                    out.append((m.start(), txt))
    return out


def u8_names(b: bytes) -> list[tuple[int, str]]:
    """Names stored as <u8 len> <ascii>, used after the LDXF class marker."""
    out = []
    for m in re.finditer(rb"([\x03-\xc8])([ -~]+)", b, re.S):
        n = m.group(1)[0]
        s = m.group(2)
        if len(s) >= n:
            cand = s[:n]
            if all(32 <= c < 127 for c in cand):
                txt = cand.decode("ascii")
                if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.:]*", txt):
                    out.append((m.start(), txt))
    return out


def scan_one(path: pathlib.Path) -> dict:
    b = path.read_bytes()
    n = len(b)
    hdr = {
        "file": path.name,
        "rid": int(path.stem.split("_")[0]),
        "bytes": n,
        "magic": b[:2].hex(),
        "tag": b[2:6],
        "const6": struct.unpack_from("<I", b, 6)[0],
        "const10": struct.unpack_from("<H", b, 10)[0],
        "flag": b[12],
        "size_field": struct.unpack_from("<I", b, 13)[0],
        "f17": struct.unpack_from("<I", b, 17)[0],
        "size_ok": struct.unpack_from("<I", b, 13)[0] == n,
        "entropy": round(entropy(b), 2),
        "zeros": round(b.count(0) / n, 2),
        "lua_at": b.find(b"\x1bLua"),
        "ldxf_at": b.find(b"LDXF\x00"),
        "prefix_names": prefix_names(b),
        "u8_names": u8_names(b),
        "idents": [(m.start(), m.group().decode("ascii")) for m in IDENT_RE.finditer(b)],
    }
    return hdr


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("resdir", nargs="?", default=str(DEFAULT_RES))
    ap.add_argument("--tsv", default="")
    args = ap.parse_args()

    root = pathlib.Path(args.resdir) / "UIFILE"
    files = sorted(root.glob("*.bin"), key=lambda p: int(p.stem.split("_")[0]))
    if not files:
        print(f"no blobs under {root}", file=sys.stderr)
        return 1

    rows = [scan_one(p) for p in files]
    bad = [r for r in rows if not r["size_ok"]]
    print(f"{len(rows)} UIFILE blobs, size field mismatches: {len(bad)}")
    for r in rows:
        kinds = []
        if r["lua_at"] >= 0:
            kinds.append(f"lua@{r['lua_at']}")
        if r["ldxf_at"] >= 0:
            kinds.append("LDXF")
        print(
            f"{r['rid']:5} {r['bytes']:7} flag={r['flag']} f17=0x{r['f17']:08x} "
            f"H={r['entropy']:.2f} z={r['zeros']:.2f} "
            f"pre={len(r['prefix_names']):3} u8={len(r['u8_names']):3} "
            f"ident={len(r['idents']):3} {' '.join(kinds)}"
        )
        if r["bytes"] <= 2000:
            for off, s in r["prefix_names"] + r["u8_names"]:
                print(f"        {off:6} {s}")

    if args.tsv:
        import csv

        with open(args.tsv, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f, delimiter="\t")
            w.writerow(["rid", "bytes", "flag", "f17", "entropy", "zeros", "lua_at",
                        "ldxf_at", "names"])
            for r in rows:
                names = sorted({s for _, s in r["prefix_names"] + r["u8_names"]},
                               key=str.lower)
                w.writerow([r["rid"], r["bytes"], r["flag"], f"0x{r['f17']:08x}",
                            r["entropy"], r["zeros"], r["lua_at"], r["ldxf_at"],
                            ";".join(names)])
        print(f"wrote {args.tsv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
