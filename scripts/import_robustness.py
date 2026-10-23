#!/usr/bin/env python3
"""Run the reader over real .mpp files and report what it does: ok, rejected,
timeout, or crash.

This is the edge-case harness.  A crash is a *finding*, not a failure of this
script: every non-zero outcome is recorded with its exit code, the time it took
and the first line of stderr, and the script still exits 0.  Use --strict when
you want crashes (not rejections) to fail a build.

    python scripts/import_robustness.py --reader-bin build/vs-w19/Release/engine_diff_tests.exe
    python scripts/import_robustness.py corpus/imported corpus/probe \
        --reader-bin build/vs-w19/Release/engine_diff_tests.exe \
        --recalc-bin build/w62/tools/recalc/Release/recalc.exe \
        --out corpus/imported/robustness.tsv
    python scripts/import_robustness.py --reader-bin build/vs-w19/Release/engine_diff_tests.exe --mutate

Two stages are available and either can be omitted:

    reader   <bin> --dump <file> <scratch.json>, or `reader_dump <file>` when the
             binary is named reader_dump.  The dump itself is discarded -- only
             the exit code, timing and the decoded stream/row counts are kept,
             because the dump contains task names.
    recalc   <bin> <file>.  Rebuilds the schedule from the graph; a crash here is
             a proof that the reader handed bad data to core::plan.

--mutate writes damaged COPIES of each file into the scratch directory
(`<first dir>/.robustness-scratch`, deleted afterwards) and runs the reader over
those too: truncate to 75% and 25%, zero the 512-byte CFB header, flip one byte
at the midpoint.  The originals are never opened for writing; the script refuses
to run if a mutation path would collide with its source.

Output is a TSV on stdout (or --out) plus a summary of the findings.  Paths under
tests/data are refused for --out.  Exit codes: 0 ran (whatever the files did),
1 --strict found a crash or timeout, 2 bad usage.
"""

import argparse
import csv
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from import_corpus import CRASH_CODES, collect, looks_like_mpp, ole_facts  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROTECTED = os.path.join(ROOT, "tests", "data")
DEFAULT_DIR = os.path.join(ROOT, "corpus", "imported")

COLUMNS = [
    "file",
    "mutation",
    "bytes",
    "version",
    "stage",
    "result",
    "exit",
    "ms",
    "output_bytes",
    "note",
]

MUTATIONS = ("truncate75", "truncate25", "zero-header", "flip-mid")


def guard_output(out_path):
    target = os.path.abspath(out_path)
    try:
        common = os.path.commonpath([target, PROTECTED])
    except ValueError:
        return
    if common == PROTECTED:
        sys.exit(f"import_robustness.py: refusing to write under tests/data: {out_path}")


def remove_tree(path, attempts=4):
    """shutil.rmtree with retries: Windows can hold the directory handle for a
    moment after the last file in it is closed."""
    for _ in range(attempts):
        shutil.rmtree(path, ignore_errors=True)
        if not os.path.exists(path):
            return True
        time.sleep(0.25)
    return not os.path.exists(path)


def blank_file(path):
    """Truncate a dump we are done with: it holds task names."""
    if not path:
        return
    try:
        with open(path, "wb"):
            pass
    except OSError:
        pass


def strip_path(text, path):
    for needle in (os.path.abspath(path), path, os.path.basename(path)):
        if needle:
            text = text.replace(needle, "<file>")
    return " ".join(text.split())


def classify(returncode):
    """Map a process exit status onto our vocabulary."""
    code = returncode & 0xFFFFFFFF
    if code == 0:
        return "ok", "0"
    if code in CRASH_CODES:
        return "crash", f"0x{code:08X}"
    if code == 1:
        return "rejected", "1"
    if code == 2:
        return "exit-2", "2"
    return f"exit-{code}", str(code)


def run_case(binary, path, timeout, dump_path=None):
    """Run one binary over one file.  Returns a populated row fragment.

    `dump_path` is None for a reader_dump-style or recalc-style binary (the file
    path alone is the argument) and a path for engine_diff_tests --dump.
    """
    started = time.time()
    command = [binary, path] if dump_path is None else [binary, "--dump", path, dump_path]
    try:
        completed = subprocess.run(command, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return {
            "result": "timeout",
            "exit": "-",
            "ms": f"{(time.time() - started) * 1000:.0f}",
            "output_bytes": "0",
            "note": f"no exit within {timeout:g}s",
        }
    except OSError as error:
        return {
            "result": "error",
            "exit": "-",
            "ms": f"{(time.time() - started) * 1000:.0f}",
            "output_bytes": "0",
            "note": str(error)[:200],
        }
    elapsed = f"{(time.time() - started) * 1000:.0f}"
    result, code = classify(completed.returncode)
    produced = len(completed.stdout)
    if dump_path and os.path.exists(dump_path):
        produced = os.path.getsize(dump_path)
    stderr = completed.stderr.decode("utf-8", "replace").strip()
    if stderr:
        note = strip_path(stderr.splitlines()[-1], path)[:240]
    elif result == "ok":
        note = ""
    else:
        note = "no stderr"
    if result == "crash":
        note = CRASH_CODES[completed.returncode & 0xFFFFFFFF] + (
            ": " + note if note else "")
    return {
        "result": result,
        "exit": code,
        "ms": elapsed,
        "output_bytes": str(produced),
        "note": note,
    }


def mutate(source, destination, kind):
    """Write a damaged copy of `source` to `destination`.  Returns bytes written."""
    if os.path.abspath(source) == os.path.abspath(destination):
        sys.exit("import_robustness.py: mutation would overwrite its source")
    with open(source, "rb") as handle:
        data = bytearray(handle.read())
    if not data:
        return 0
    if kind == "truncate75":
        data = data[: max(512, len(data) * 3 // 4)]
    elif kind == "truncate25":
        data = data[: max(512, len(data) // 4)]
    elif kind == "zero-header":
        data[:512] = bytes(512)
    elif kind == "flip-mid":
        index = len(data) // 2
        data[index] ^= 0xFF
    else:  # pragma: no cover
        sys.exit(f"unknown mutation {kind}")
    with open(destination, "wb") as handle:
        handle.write(data)
    return len(data)


def main():
    parser = argparse.ArgumentParser(
        description="Report what the reader does with each .mpp file.")
    parser.add_argument("directories", nargs="*", default=None,
                        help=f"directories to scan (default {DEFAULT_DIR})")
    parser.add_argument("--reader-bin", default=None)
    parser.add_argument("--recalc-bin", default=None)
    parser.add_argument("--timeout", type=float, default=120.0,
                        help="seconds per run (default 120)")
    parser.add_argument("--out", default=None,
                        help="write the TSV here (default: stdout, summary on stderr)")
    parser.add_argument("--scratch", default=None,
                        help="scratch directory (default: <first dir>/.robustness-scratch)")
    parser.add_argument("--keep-scratch", action="store_true",
                        help="keep the mutated copies for inspection")
    parser.add_argument("--mutate", action="store_true",
                        help="also run the reader over damaged copies of each file")
    parser.add_argument("--strict", action="store_true",
                        help="exit 1 if any crash or timeout was seen")
    args = parser.parse_args()

    if not args.reader_bin and not args.recalc_bin:
        sys.exit("import_robustness.py: give --reader-bin and/or --recalc-bin")
    for binary in (args.reader_bin, args.recalc_bin):
        if binary and not os.path.exists(binary):
            sys.exit(f"import_robustness.py: no such binary: {binary}")
    if args.out:
        guard_output(args.out)

    directories = args.directories or [DEFAULT_DIR]
    for directory in directories:
        if not os.path.isdir(directory):
            sys.exit(f"import_robustness.py: not a directory: {directory}")

    scratch = os.path.abspath(
        args.scratch or os.path.join(directories[0], ".robustness-scratch"))
    for directory in directories:
        try:
            inside = os.path.commonpath(
                [os.path.abspath(directory), scratch]) == scratch
        except ValueError:
            inside = False
        if inside:
            sys.exit("import_robustness.py: --scratch must not contain a scanned "
                     f"directory: {scratch}")
    os.makedirs(scratch, exist_ok=True)

    files = []
    for directory in directories:
        files.extend(collect(directory))
    if not files:
        sys.exit("import_robustness.py: no .mpp files found in " +
                 ", ".join(directories))

    reader_bin = os.path.abspath(args.reader_bin) if args.reader_bin else None
    recalc_bin = os.path.abspath(args.recalc_bin) if args.recalc_bin else None
    dump_style = reader_bin is not None and "reader_dump" not in \
        os.path.basename(reader_bin).lower()

    counter = [0]

    def next_dump_path():
        """A fresh dump per run: deleting between runs trips a sharing
        violation on Windows (indexer/Dropbox still holds the file)."""
        if not dump_style:
            return None
        counter[0] += 1
        return os.path.join(scratch, f"dump-{counter[0]:04d}.json")

    rows = []
    try:
        for path in files:
            facts, _ = ole_facts(path)
            base = {
                "file": os.path.basename(path),
                "mutation": "-",
                "bytes": str(os.path.getsize(path)),
                "version": facts["version"] if looks_like_mpp(path) else "not-ole",
            }
            if reader_bin:
                dump_path = next_dump_path()
                outcome = run_case(reader_bin, path, args.timeout, dump_path)
                blank_file(dump_path)
                rows.append({**base, "stage": "reader", **outcome})
            if recalc_bin:
                outcome = run_case(recalc_bin, path, args.timeout)
                rows.append({**base, "stage": "recalc", **outcome})

            if args.mutate and reader_bin:
                stem = os.path.splitext(os.path.basename(path))[0]
                for kind in MUTATIONS:
                    copy = os.path.join(scratch, f"{stem}.{kind}.mpp")
                    size = mutate(path, copy, kind)
                    dump_path = next_dump_path()
                    outcome = run_case(reader_bin, copy, args.timeout, dump_path)
                    blank_file(dump_path)
                    rows.append({**base, "mutation": kind, "bytes": str(size),
                                 "stage": "reader", **outcome})
    finally:
        if not args.keep_scratch:
            remove_tree(scratch)

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

    out = sys.stderr if not args.out else sys.stdout
    counts = {}
    for row in rows:
        key = (row["stage"], row["result"])
        counts[key] = counts.get(key, 0) + 1
    print("", file=out)
    print(f"runs {len(rows)} over {len(files)} file(s)", file=out)
    for (stage, result), count in sorted(counts.items()):
        print(f"  {stage:6s} {result:10s} {count}", file=out)

    findings = [row for row in rows
                if row["result"] not in ("ok", "rejected", "exit-2")]
    if findings:
        print("", file=out)
        print("FINDINGS (anything that is not a clean ok/rejected):", file=out)
        for row in findings:
            print(f"  {row['file']} [{row['mutation']}] {row['stage']}: "
                  f"{row['result']} exit={row['exit']} {row['ms']}ms "
                  f"-- {row['note']}", file=out)
    else:
        print("findings: none (every run was ok or a clean rejection)", file=out)
    if args.out:
        print(f"wrote {args.out}", file=out)

    if args.strict and any(row["result"] in ("crash", "timeout") for row in rows):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
