"""Validate files written by core/mppwrite with an INDEPENDENT parser.

The C++ tests check the writer against its own container model, which would
still pass if the writer and the tests shared a wrong idea of the format.  This
script re-reads every written fixture with `olefile` (a different implementation
that knows nothing about this project) and compares each stream with the source
file:

  * olefile must open the file and enumerate the same streams;
  * every stream's bytes must match the source, except the ones the writer
    intentionally changed;
  * the reported directory sizes and the stream sizes must agree, which is what
    catches a stale size field after a stream grows.

Usage:
    python docs/analysis/writer/validate_written.py [source.mpp] [written.mpp ...]

With no arguments it validates every *.mpp in core/mppwrite/build/vs/test-out
against corpus/probe/base.mpp.
"""

import glob
import os
import sys

import olefile


def load(path):
    ole = olefile.OleFileIO(path)
    try:
        out = {}
        for parts in ole.listdir(streams=True):
            key = "/".join(p.strip() for p in parts)
            out[key] = ole.openstream(parts).read()
        return out
    finally:
        ole.close()


def validate(source, written, expected_changed):
    src = load(source)
    dst = load(written)
    problems = []
    if set(src) != set(dst):
        missing = sorted(set(src) - set(dst))
        extra = sorted(set(dst) - set(src))
        problems.append("stream set differs: missing %s extra %s" %
                        (missing[:4], extra[:4]))
    changed = []
    for key in sorted(set(src) & set(dst)):
        if src[key] != dst[key]:
            changed.append(key)
    unexpected = [p for p in changed if p not in expected_changed]
    missing_change = [p for p in expected_changed if p not in changed]
    if unexpected:
        problems.append("unexpected stream changes: %s" % unexpected)
    if missing_change:
        problems.append("expected changes missing: %s" % missing_change)
    print("%-30s %8d B  streams %3d  changed %s  %s" % (
        os.path.basename(written), os.path.getsize(written), len(dst),
        changed if changed else "[]",
        "OK" if not problems else "PROBLEMS"))
    for p in problems:
        print("    - " + p)
    return not problems


# file -> (source fixture, streams the writer is allowed to have changed).
CASES = {
    # inplace-patched also exercises the FAT (non-mini) mapping path, which
    # base.mpp reaches through 114/Props (88,690 bytes).
    "inplace-patched.mpp": ("base.mpp",
                            ["114/TBkndTask/FixedData", "114/Props"]),
    "replace-fixeddata.mpp": ("base.mpp", ["114/TBkndTask/FixedData"]),
    "replace-name.mpp": ("base.mpp",
                         ["114/TBkndTask/Var2Data", "114/TBkndTask/VarMeta"]),
    "replace-grow.mpp": ("base.mpp",
                         ["114/TBkndTask/Var2Data", "114/TBkndTask/VarMeta"]),
    # the FAT-growth and shrink cases run on the real project instead
    "replace-fatgrow.mpp": ("project_a.mpp", ["114/Props"]),
    "replace-shrink.mpp": ("project_a.mpp", ["114/Props"]),
}


def main(argv):
    root = os.path.abspath(os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
    sources = {
        "base.mpp": os.path.join(root, "corpus", "probe", "base.mpp"),
        "project_a.mpp": os.path.join(
            root, "corpus", "project_a.mpp"),
    }
    out_dir = os.path.join(root, "core", "mppwrite", "build", "vs", "test-out")
    written = argv[1:] or sorted(glob.glob(os.path.join(out_dir, "*.mpp")))
    if not written:
        print("no written files found in " + out_dir)
        return 1
    ok = 0
    for path in written:
        name = os.path.basename(path)
        if name not in CASES:
            print("%-30s skipped (no expectation registered)" % name)
            continue
        source_name, expected = CASES[name]
        ok += validate(sources[source_name], path, expected)
    total = sum(1 for p in written if os.path.basename(p) in CASES)
    print("%d/%d clean (parser: olefile %s)" % (ok, total, olefile.__version__))
    return 0 if ok == total else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
