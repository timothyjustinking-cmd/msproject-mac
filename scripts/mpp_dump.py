"""Dump the OLE2 / Compound File Binary structure of a Microsoft Project .mpp file.

Read-only. Prints the storage/stream tree with sizes, the first bytes of every
stream, and redacted document metadata. This is step 1 of the .mpp format
specification: find out what is actually in the container before interpreting it.

Usage:
    python scripts/mpp_dump.py <file.mpp> [--head N] [--filter SUBSTR]
    python scripts/mpp_dump.py <file.mpp> --extract <stream> --out <path>

The same walker will be reused by the port's reader tests.
"""

import argparse
import sys

import olefile

SENSITIVE = {"author", "last_saved_by", "company", "title", "subject",
             "keywords", "comments", "manager", "revision_number", "template"}


def hexdump(data, limit=64, width=16):
    """Compact hex + ascii of the first `limit` bytes."""
    data = data[:limit]
    lines = []
    for off in range(0, len(data), width):
        chunk = data[off:off + width]
        hexpart = " ".join(f"{b:02x}" for b in chunk).ljust(width * 3 - 1)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"    {off:04x}  {hexpart}  |{text}|")
    return "\n".join(lines)


def walk(ole, entry, prefix=""):
    """Yield (path, size) for every stream, depth-first."""
    for kid in entry.kids:
        path = f"{prefix}{kid.name}"
        if kid.entry_type == olefile.STGTY_STREAM:
            yield path, kid.size
        else:
            yield from walk(ole, kid, path + "/")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--head", type=int, default=64,
                    help="bytes of each stream to show (0 = none)")
    ap.add_argument("--filter", default=None,
                    help="only show streams whose path contains this substring")
    ap.add_argument("--extract", default=None,
                    help="write one stream to --out and exit")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    if not olefile.isOleFile(args.path):
        sys.exit(f"not an OLE2 compound file: {args.path}")

    ole = olefile.OleFileIO(args.path)
    streams = list(walk(ole, ole.root))
    total = sum(s for _, s in streams)

    print(f"file      : {args.path}")
    print(f"streams   : {len(streams)}   storages: "
          f"{len(ole.listdir(streams=False, storages=True))}")
    print(f"stream bytes: {total:,}")

    meta = ole.get_metadata()
    print("metadata  :")
    for key in ("creating_application", "app_version", "doc_version",
                "create_time", "last_saved_time", "num_pages", "num_words"):
        val = getattr(meta, key, None)
        if val not in (None, "", 0):
            print(f"    {key:22} {val!r}")
    for key in ("title", "author", "last_saved_by", "company", "subject"):
        val = getattr(meta, key, None)
        if val:
            print(f"    {key:22} <redacted, {len(str(val))} chars>")
    print()

    if args.extract:
        target = args.extract.lower()
        matches = [p for p, _ in streams if p.lower() == target
                   or p.lower().endswith("/" + target)]
        if len(matches) != 1:
            sys.exit(f"stream match for {args.extract!r}: {matches}")
        data = ole.openstream(matches[0]).read()
        with open(args.out, "wb") as fh:
            fh.write(data)
        print(f"wrote {len(data):,} bytes from {matches[0]} -> {args.out}")
        return

    print(f"{'size':>10}  stream")
    print(f"{'-'*10}  {'-'*60}")
    for path, size in streams:
        if args.filter and args.filter.lower() not in path.lower():
            continue
        print(f"{size:>10,}  {path}")

    if args.head:
        print()
        for path, size in streams:
            if args.filter and args.filter.lower() not in path.lower():
                continue
            if size == 0:
                continue
            print(f"\n== {path}  ({size:,} bytes)")
            try:
                data = ole.openstream(path).read(args.head)
            except Exception as exc:            # noqa: BLE001 - diagnostic tool
                print(f"    <read error: {exc}>")
                continue
            print(hexdump(data, args.head))


if __name__ == "__main__":
    main()
