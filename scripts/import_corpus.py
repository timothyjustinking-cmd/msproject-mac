#!/usr/bin/env python3
"""Inventory real .mpp files (imported or probe) without modifying them.

Answers, per file: how big is it, what does its content hash to, which MPP
family wrote it, how many OLE streams does it carry, and does OUR reader accept
it.  Nothing is extracted from the file except counts and stream names -- no
task, resource or project names ever reach the output, so an inventory can be
pasted into a report even when the file itself is client data.

    python scripts/import_corpus.py                       # scans corpus/imported
    python scripts/import_corpus.py corpus/probe          # scans another tree
    python scripts/import_corpus.py corpus/imported \
        --reader-bin build/vs-w19/Release/engine_diff_tests.exe \
        --out corpus/imported/inventory.tsv

Columns (TSV, header row first):

    file         basename; the directory is the argument, not repeated
    bytes        file size
    sha256       content hash, upper case (matches scripts/check_fixtures.py)
    version      MPP8 | MPP9 | MPP12 | MPP14 | unknown | not-ole
    evidence     the streams the guess came from, e.g. "Props14;114,214"
    props        the root Props stream name, or "-"
    storages     top-level storage names that look versioned, e.g. "114,214"
    streams      number of leaf streams in the container
    tbknd_task   yes when a TBkndTask table is present, else no
    reader       ok | rejected | timeout | crash | exit-N | skipped
    reader_ms    wall time of the reader invocation
    reader_rows  tasks the reader decoded (reader_dump rows, or the dump's task list)
    reader_note  reader stderr's first line, with the file path removed

Version guessing is signature-based and deliberately conservative:

    Props (unversioned) + storages 1,2      -> MPP8  (Project 98)
    Props9              + storages 19,29    -> MPP9  (Project 2000-2003)
    Props12             + storages 112,212  -> MPP12 (Project 2007)
    Props14             + storages 114,214  -> MPP14 (Project 2010+)

When the root stream says one family and the numeric storages say another, the
version column is the *newest* marker (what the content is written in) and
`evidence` adds `mixed(...)` with every marker found.  That is a real finding, not
something to average away: Project 2007 leaves the Project 2003 storages behind
when it re-saves a converted file (`corpus/probe/mpp12viewstate.mpp`).

The reader is probed only when --reader-bin is given:

* a binary whose name contains "reader_dump" is called as `reader_dump <file>`
  and the row count comes from stdout;
* anything else is treated as `engine_diff_tests --dump <file> <scratch.json>`
  and the counts come from the dump's task list.

Scratch JSON goes into the scratch directory (default `<first dir>/.import-scratch`)
and is deleted afterwards unless --keep-scratch.  Exit codes: 0 all files
inventoried, 1 a file changed under us or a path was refused, 2 bad usage.

No file is opened for writing, and every file's (size, mtime) is re-checked at
the end; a mismatch aborts with exit 1 rather than reporting a lie.
"""

import argparse
import csv
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

try:
    import olefile
except ImportError:  # pragma: no cover - the error is the point
    sys.exit("import_corpus.py needs olefile (already used by scripts/mpp_rows.py)")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DIR = os.path.join(ROOT, "corpus", "imported")
PROTECTED = os.path.join(ROOT, "tests", "data")

# Root Props stream -> MPP family.  Project 98 writes an unversioned "Props".
PROPS_FAMILY = {
    "Props": "MPP8",
    "Props8": "MPP8",
    "Props9": "MPP9",
    "Props12": "MPP12",
    "Props14": "MPP14",
}
# Top-level storage names are not counters: the pair is a format constant.
STORAGE_FAMILY = {
    "1": "MPP8",
    "2": "MPP8",
    "19": "MPP9",
    "29": "MPP9",
    "112": "MPP12",
    "212": "MPP12",
    "114": "MPP14",
    "214": "MPP14",
}

# Windows NTSTATUS values Python reports as a large positive exit code.
CRASH_CODES = {
    0xC0000005: "access violation",
    0xC00000FD: "stack overflow",
    0xC0000374: "heap corruption",
    0xC0000409: "stack buffer overrun",
    0xC0000135: "missing DLL",
    0xC0000139: "missing entry point",
    0xC0000006: "in-page error",
    0xC000001D: "illegal instruction",
    0xC0000142: "DLL init failed",
}

COLUMNS = [
    "file",
    "bytes",
    "sha256",
    "version",
    "evidence",
    "props",
    "storages",
    "streams",
    "tbknd_task",
    "reader",
    "reader_ms",
    "reader_rows",
    "reader_note",
]


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:  # read-only
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


# Ordering used to pick the family a file's CONTENT is written in.  Project 2007
# does not delete the Project 2003 storages when it re-saves a converted file:
# corpus/probe/mpp12viewstate.mpp carries Props9 AND Props12 and the storages
# 19/29 as well as 112/212.  The highest marker is the live one; the rest is
# reported as "mixed" rather than averaged away.
FAMILY_ORDER = ["MPP8", "MPP9", "MPP12", "MPP14"]


def ole_facts(path):
    """Return (fields, error).  Never writes; olefile opens the file read-only."""
    facts = {
        "props": "-",
        "storages": "-",
        "streams": 0,
        "tbknd_task": "no",
        "version": "unknown",
        "evidence": "",
    }
    try:
        ole = olefile.OleFileIO(path)
    except Exception as error:  # not OLE2 at all, or too damaged to list
        facts["version"] = "not-ole"
        facts["evidence"] = str(error)[:120]
        return facts, None
    try:
        streams = ole.listdir(streams=True, storages=False)
        storages = ole.listdir(streams=False, storages=True)
        top_streams = [p[0].strip() for p in streams if len(p) == 1]
        top_storages = [p[0].strip() for p in storages if len(p) == 1]
        leaf_names = [part.strip() for p in streams for part in p]

        props = [name for name in top_streams if name in PROPS_FAMILY]
        numeric = [name for name in top_storages if name in STORAGE_FAMILY]
        families = sorted(
            {PROPS_FAMILY[name] for name in props} |
            {STORAGE_FAMILY[name] for name in numeric},
            key=FAMILY_ORDER.index)
        version = families[-1] if families else "unknown"

        evidence = []
        if props:
            evidence.append("+".join(sorted(props)))
        if numeric:
            evidence.append(",".join(sorted(numeric)))
        if len(families) > 1:
            evidence.append("mixed(" + "+".join(families) + ")")

        facts.update(
            props=props[0] if props else "-",
            storages=",".join(sorted(numeric)) if numeric else "-",
            streams=len(streams),
            tbknd_task="yes" if "TBkndTask" in leaf_names else "no",
            version=version,
            evidence=";".join(evidence),
        )
        return facts, None
    finally:
        ole.close()


def strip_path(text, path):
    """Remove the absolute path from a message so the TSV stays location-free."""
    for needle in (os.path.abspath(path), path, os.path.basename(path)):
        if needle:
            text = text.replace(needle, "<file>")
    return " ".join(text.split())


def count_dump(dump_path):
    """Task count and extra counts from an engine_diff_tests dump."""
    try:
        size = os.path.getsize(dump_path)
    except OSError:
        return "", "dump-not-written"
    if size > (1 << 28):
        return "", "dump-too-large"
    try:
        with open(dump_path, "r", encoding="utf-8") as handle:
            document = json.load(handle)
    except Exception as error:
        blank_file(dump_path)
        return "", "dump-unreadable: " + strip_path(str(error), dump_path)[:80]
    blank_file(dump_path)  # holds task names; nothing may read it afterwards
    tasks = document.get("tasks")
    if not isinstance(tasks, list):
        return "", "dump-has-no-task-list"
    return str(len(tasks)), ""


def probe_reader(binary, path, timeout, dump_path):
    """Run the reader once.  Returns (result, ms, rows, note).

    `dump_path` is a unique file per invocation, or None for a reader_dump-style
    binary.  The dump is written once and is only removed with the scratch tree:
    on Windows the indexer/Dropbox can hold a just-written file, and deleting it
    between runs then fails with a sharing violation even after the reader exits.
    """
    binary = os.path.abspath(binary)
    if not os.path.exists(binary):
        return "skipped", "", "", "reader binary not found: " + binary
    if dump_path is None:
        command = [binary, path]
    else:
        command = [binary, "--dump", path, dump_path]

    started = time.time()
    try:
        completed = subprocess.run(command, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return "timeout", f"{(time.time() - started) * 1000:.0f}", "", \
            f"no exit within {timeout}s"
    except OSError as error:
        return "error", f"{(time.time() - started) * 1000:.0f}", "", str(error)[:120]
    elapsed = f"{(time.time() - started) * 1000:.0f}"

    stderr = completed.stderr.decode("utf-8", "replace").strip()
    note = strip_path(stderr.splitlines()[0], path) if stderr else ""
    code = completed.returncode & 0xFFFFFFFF

    if code == 0:
        rows, problem = "", ""
        if dump_path:
            rows, problem = count_dump(dump_path)
            if problem:
                note = (note + " " if note else "") + problem
        else:
            lines = completed.stdout.decode("utf-8", "replace").splitlines()
            rows = str(max(0, len(lines) - 1))  # reader_dump prints a header line
        return "ok", elapsed, rows, note
    if code in CRASH_CODES:
        return "crash", elapsed, "", CRASH_CODES[code] + (": " + note if note else "")
    if code == 1:
        return "rejected", elapsed, "", note
    if code == 2:
        return "exit-2", elapsed, "", note or "reader rejected its own arguments"
    return f"exit-{code}", elapsed, "", note


def remove_tree(path, attempts=4):
    """shutil.rmtree with retries: Windows can hold the directory handle for a
    moment after the last file in it is closed, which leaves an empty directory
    behind and hides the fact from ignore_errors=True."""
    for _ in range(attempts):
        shutil.rmtree(path, ignore_errors=True)
        if not os.path.exists(path):
            return True
        time.sleep(0.25)
    return not os.path.exists(path)


def blank_file(path):
    """Truncate a file we are done with, so a failed cleanup cannot leave a dump
    holding task names behind.  Best effort: a sharing violation is ignored, the
    scratch tree is deleted wholesale at the end anyway."""
    try:
        with open(path, "wb"):
            pass
    except OSError:
        pass


def looks_like_mpp(path):
    with open(path, "rb") as handle:
        head = handle.read(8)
    return head == b"\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1"


def collect(directory):
    files = []
    for name in sorted(os.listdir(directory)):
        full = os.path.join(directory, name)
        if os.path.isfile(full) and name.lower().endswith(".mpp"):
            files.append(full)
    return files


def guard_output(out_path):
    """Refuse to write into tests/data -- that tree is committed."""
    target = os.path.abspath(out_path)
    try:
        common = os.path.commonpath([target, PROTECTED])
    except ValueError:  # different drives on Windows
        return
    if common == PROTECTED:
        sys.exit(f"import_corpus.py: refusing to write under tests/data: {out_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Inventory .mpp files without modifying them.")
    parser.add_argument("directories", nargs="*", default=None,
                        help=f"directories to scan (default {DEFAULT_DIR})")
    parser.add_argument("--reader-bin", default=None,
                        help="reader binary; 'reader_dump' or 'engine_diff_tests "
                             "--dump' style")
    parser.add_argument("--out", default=None,
                        help="write the TSV here (default: stdout, summary on stderr)")
    parser.add_argument("--timeout", type=float, default=120.0,
                        help="seconds per reader run (default 120)")
    parser.add_argument("--scratch", default=None,
                        help="scratch directory (default: <first dir>/.import-scratch)")
    parser.add_argument("--keep-scratch", action="store_true",
                        help="do not delete the scratch directory")
    parser.add_argument("--list-only", action="store_true",
                        help="do not run the reader even if --reader-bin is given")
    args = parser.parse_args()

    directories = args.directories or [DEFAULT_DIR]
    for directory in directories:
        if not os.path.isdir(directory):
            sys.exit(f"import_corpus.py: not a directory: {directory}")
    if args.out:
        guard_output(args.out)

    scratch = os.path.abspath(
        args.scratch or os.path.join(directories[0], ".import-scratch"))
    # The scratch is deleted at the end; it must never be able to take a corpus
    # tree with it.
    for directory in directories:
        resolved = os.path.abspath(directory)
        try:
            inside = os.path.commonpath([resolved, scratch]) == scratch
        except ValueError:
            inside = False
        if inside:
            sys.exit("import_corpus.py: --scratch must not contain a scanned "
                     f"directory (would delete it): {scratch}")
    os.makedirs(scratch, exist_ok=True)

    files = []
    for directory in directories:
        files.extend(collect(directory))
    if not files:
        sys.exit("import_corpus.py: no .mpp files found in " + ", ".join(directories))

    before = {}
    rows = []
    reader_bin = os.path.abspath(args.reader_bin) if args.reader_bin else None
    dump_style = reader_bin is not None and \
        "reader_dump" not in os.path.basename(reader_bin).lower()
    try:
        for index, path in enumerate(files):
            stat = os.stat(path)
            before[path] = (stat.st_size, stat.st_mtime_ns)
            facts, error = ole_facts(path)
            row = {
                "file": os.path.basename(path),
                "bytes": str(os.path.getsize(path)),
                "sha256": sha256_of(path),
                "version": facts["version"],
                "evidence": facts["evidence"],
                "props": facts["props"],
                "storages": facts["storages"],
                "streams": str(facts["streams"]),
                "tbknd_task": facts["tbknd_task"],
                "reader": "skipped",
                "reader_ms": "",
                "reader_rows": "",
                "reader_note": "",
            }
            if error:
                row["reader_note"] = error[:120]
            elif not looks_like_mpp(path):
                row["reader_note"] = "not an OLE2 compound file"
            if reader_bin and not args.list_only:
                dump_path = os.path.join(scratch, f"dump-{index:04d}.json") \
                    if dump_style else None
                result, ms, count, note = probe_reader(
                    reader_bin, path, args.timeout, dump_path)
                row.update(reader=result, reader_ms=ms, reader_rows=count,
                           reader_note=note[:200])
            rows.append(row)
    finally:
        if not args.keep_scratch:
            remove_tree(scratch)

    changed = []
    for path in files:
        stat = os.stat(path)
        if (stat.st_size, stat.st_mtime_ns) != before[path]:
            changed.append(path)
    if changed:
        print("import_corpus.py: these files CHANGED while being inventoried:",
              file=sys.stderr)
        for path in changed:
            print(f"  {path}", file=sys.stderr)
        return 1

    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=COLUMNS, delimiter="\t",
                                    lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)
    else:
        writer = csv.DictWriter(sys.stdout, fieldnames=COLUMNS, delimiter="\t",
                                lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)

    # ---- summary ----
    total_bytes = sum(int(row["bytes"]) for row in rows)
    versions = {}
    readers = {}
    for row in rows:
        versions[row["version"]] = versions.get(row["version"], 0) + 1
        readers[row["reader"]] = readers.get(row["reader"], 0) + 1
    hashes = {}
    for row in rows:
        hashes.setdefault(row["sha256"], []).append(row["file"])
    duplicates = [names for names in hashes.values() if len(names) > 1]

    out = sys.stderr if not args.out else sys.stdout
    print(f"files {len(rows)}  bytes {total_bytes} "
          f"({total_bytes / (1 << 20):.2f} MiB)", file=out)
    print("version  " + "  ".join(f"{k}={v}" for k, v in sorted(versions.items())),
          file=out)
    print("reader   " + "  ".join(f"{k}={v}" for k, v in sorted(readers.items())),
          file=out)
    if duplicates:
        for names in duplicates:
            print("duplicate content: " + ", ".join(names), file=out)
    else:
        print("duplicate content: none", file=out)
    no_task = [row["file"] for row in rows if row["tbknd_task"] != "yes"]
    if no_task:
        print("no TBkndTask table: " + ", ".join(no_task), file=out)
    mixed = [row for row in rows if "mixed(" in row["evidence"]]
    if mixed:
        print("mixed version markers (newest wins):", file=out)
        for row in mixed:
            print(f"  {row['file']}: {row['evidence']}", file=out)
    if args.out:
        print(f"wrote {args.out}", file=out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
