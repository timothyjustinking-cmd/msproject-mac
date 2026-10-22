#!/usr/bin/env python3
"""Extract the `214/` (and optionally `114/`) stream tree of an .mpp.

`core/viewstate`'s CLI (`viewstate_report`) deliberately does not open CFB files,
so it takes an extracted tree, one file per stream part:

    python scripts/extract_viewstate.py corpus/probe/w14-tablecol-A.mpp build/dump-A
    python scripts/extract_viewstate.py some.mpp build/dump --all

Read-only; streams are written verbatim.  CFB directory names carry three
leading spaces in this file family and are stripped, as `core/mpp/cfb.cpp` does
for its own lookups.  `214/CEdl` carries a host path, so never commit an
extracted tree (build/ is git-ignored).
"""

import argparse
import os
import sys

import olefile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mpp")
    ap.add_argument("out")
    ap.add_argument("--all", action="store_true",
                    help="extract every stream, not just 214/ and 114/")
    ap.add_argument("--prefix", action="append", default=None,
                    help="stream path prefix to extract (repeatable)")
    args = ap.parse_args()

    prefixes = [p if p.endswith("/") else p + "/" for p in
                (args.prefix or ([] if args.all else ["214", "114"]))]

    ole = olefile.OleFileIO(args.mpp)
    written = 0
    try:
        for entry in ole.listdir():
            full = "/".join(entry)
            clean = full.strip()
            # OLE internals carry control characters (\x01CompObj); they are not
            # table streams and cannot be written to a Windows path.
            if any(ord(ch) < 32 for ch in clean):
                continue
            if prefixes and not any(clean == p.rstrip("/") or clean.startswith(p)
                                    for p in prefixes):
                continue
            target = os.path.join(args.out, *clean.split("/"))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with open(target, "wb") as fh:
                fh.write(ole.openstream(full).read())
            written += 1
    finally:
        ole.close()
    print(f"{written} stream(s) -> {args.out}")


if __name__ == "__main__":
    sys.exit(main())
