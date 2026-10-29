#!/usr/bin/env python3
"""Fail when a tracked file or a pushed commit message carries an operator marker.

Wave 33 (W33-4.4 / audit B-4 marker half).  The pre-push hook's client-term
guard is untracked on purpose: its term list is the client corpus, which is
git-ignored (B-12).  This is the OTHER half -- the operator's identifying
strings and paths -- and it is deliberately broader than that hook:

  * EVERY tracked file is scanned, binary included (analysis/*.mpp, *.bin,
    *.tsv), not only text and not only tests/data;
  * each marker is searched as case-insensitive ASCII, and as UTF-16LE and
    UTF-16BE at BOTH byte alignments (a CFB property set stores UTF-16 and a
    string can start on an odd byte);
  * commit messages in the pushed range are scanned too, because the history
    is where a pasted path most often survives.

The marker strings are assembled from code points rather than written as
literals.  A guard that scans every tracked file would otherwise flag itself,
and the standing rule keeps these identifying strings out of tracked text.
The labels are stable and are what every report prints; the literal is never
printed, even on a hit (the file content already carries it, and a CI log is
public).  Check out the file and look at the byte offset if you need to see it.

The client-term half stays local: the corpus and its reader are git-ignored,
so CI neither has those terms nor may carry them.

usage:
    python scripts/check_markers.py                  # index files + main..HEAD
    python scripts/check_markers.py --range main..HEAD
    python scripts/check_markers.py --no-messages    # files only
    python scripts/check_markers.py --worktree       # working tree, not the index
    python scripts/check_markers.py --self-test      # planted markers must be found
exit 0 = clean, 1 = hit, 2 = usage or git error.
"""

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _spell(*codes):
    """A marker from its code points, so this file does not contain it."""
    return "".join(chr(code) for code in codes)


# The committed marker list (audit B-3/B-4).  The client-term list is NOT here.
MARKERS = [
    ("operator name", _spell(84, 105, 109, 32, 75, 105, 110, 103)),
    ("operator first name", _spell(84, 105, 109, 111, 116, 104, 121)),
    ("Windows account name", _spell(116, 105, 109, 111, 116)),
    ("company folder", _spell(83, 116, 97, 102, 102, 32, 68, 114, 111, 112,
                              98, 111, 120)),
    # User-profile directory fragments, in all three spellings this machine
    # has seen: Windows backslash, Windows drive-slash and POSIX.
    ("profile path, backslash", _spell(92, 85, 115, 101, 114, 115, 92)),
    ("profile path, slash", _spell(67, 58, 47, 85, 115, 101, 114, 115, 47)),
    ("profile path, POSIX", _spell(47, 85, 115, 101, 114, 115, 47)),
    # The pre-Vista profile directory name, still present in old records.
    ("legacy profile folder", _spell(68, 111, 99, 117, 109, 101, 110, 116,
                                     115, 32, 97, 110, 100, 32, 83, 101,
                                     116, 116, 105, 110, 103, 115)),
]


def _ascii_class(char):
    """A case-insensitive regex unit for one ASCII character."""
    if "a" <= char <= "z" or "A" <= char <= "Z":
        return b"[" + re.escape(char.lower().encode("ascii")) + \
            re.escape(char.upper().encode("ascii")) + b"]"
    return re.escape(char.encode("ascii"))


def _utf16_pattern(text, little_endian):
    """The marker as UTF-16 code units at any byte offset, case-insensitive.

    Building the pattern as bytes (one unit per character, the low byte padded
    with NUL on the matching side) means `re.search` finds the string whether
    it starts on an even or an odd byte -- which is exactly what "both
    alignments" asks for, without decoding an entire binary file.
    """
    units = []
    for char in text:
        unit = _ascii_class(char)
        units.append(unit + b"\x00" if little_endian else b"\x00" + unit)
    return re.compile(b"".join(units))


# (label, ASCII, UTF-16LE, UTF-16BE) compiled once.
_PATTERNS = [
    (label,
     re.compile(re.escape(text.encode("ascii")), re.IGNORECASE),
     _utf16_pattern(text, True),
     _utf16_pattern(text, False))
    for label, text in MARKERS
]


def find_markers(data):
    """[(label, first byte offset)] for every marker present in bytes."""
    hits = []
    for label, ascii_rx, le_rx, be_rx in _PATTERNS:
        offset = None
        for regex in (ascii_rx, le_rx, be_rx):
            match = regex.search(data)
            if match is not None and (offset is None or match.start() < offset):
                offset = match.start()
        if offset is not None:
            hits.append((label, offset))
    return hits


def find_markers_in_text(text):
    """Labels present in already-decoded text (a commit message, a property)."""
    lowered = text.lower()
    return [label for label, marker in MARKERS if marker.lower() in lowered]


def labels(hits):
    return " ".join(label for label, _ in hits)


def redact(text):
    """Replace every marker in decoded text with its label, for a failure line."""
    out = text
    for label, marker in sorted(MARKERS, key=lambda item: -len(item[1])):
        out = re.sub(re.escape(marker), "[%s]" % label, out, flags=re.IGNORECASE)
    return out


# ---------------------------------------------------------------------------
# git plumbing
# ---------------------------------------------------------------------------


def _git(args, binary=False):
    result = subprocess.run(["git", "-C", ROOT] + list(args),
                            capture_output=True)
    if result.returncode != 0:
        message = result.stderr.decode("utf-8", "replace").strip()
        raise SystemExit("check_markers: git %s failed: %s"
                         % (" ".join(args), message))
    return result.stdout


def tracked_entries():
    """[(path, blob oid)] for every stage-0 entry in the index, sorted."""
    raw = _git(["ls-files", "-s", "-z"])
    entries = []
    for record in raw.split(b"\x00"):
        if not record:
            continue
        meta, _, path = record.partition(b"\t")
        fields = meta.split()
        if len(fields) != 3:
            continue
        _mode, oid, stage = fields
        if stage != b"0":
            continue
        entries.append((path.decode("utf-8", "surrogateescape"),
                        oid.decode("ascii")))
    return entries


def worktree_entries():
    """[(path, None)] for tracked files read from the working tree."""
    raw = _git(["ls-files", "-z"])
    return [(path.decode("utf-8", "surrogateescape"), None)
            for path in raw.split(b"\x00") if path]


def stream_blobs(oids):
    """Yield (oid, bytes) from one `git cat-file --batch` run.

    The whole request list goes in through `input=`, so `subprocess.run` owns
    the concurrent write/read.  The earlier one-request-one-reply loop avoided
    a Windows pipe deadlock but could still leave a `git cat-file` child alive
    under Git Bash; this form has no interactive pipe to deadlock and no child
    to outlive the scan.  The tracked tree is about 80 MB today, which one
    batch can hold.
    """
    request = b"".join(oid.encode("ascii") + b"\n" for oid in oids)
    result = subprocess.run(["git", "-C", ROOT, "cat-file", "--batch"],
                            input=request, stdout=subprocess.PIPE)
    if result.returncode != 0:
        raise SystemExit("check_markers: git cat-file failed with exit %d"
                         % result.returncode)
    data = result.stdout
    at = 0
    for oid in oids:
        end = data.find(b"\n", at)
        if end < 0:
            raise SystemExit("check_markers: truncated git cat-file output")
        header = data[at:end].split()
        at = end + 1
        if len(header) != 3:      # "<oid> missing" has no payload
            yield oid, b""
            continue
        size = int(header[2])
        if at + size > len(data):
            raise SystemExit("check_markers: truncated git cat-file output")
        yield oid, data[at:at + size]
        at += size + 1           # the record's trailing newline


# ---------------------------------------------------------------------------
# the two scans
# ---------------------------------------------------------------------------


def scan_files(entries):
    """[(path, hits)] for every tracked file that carries a marker."""
    cached = {}
    oids = list(dict.fromkeys(oid for _, oid in entries if oid is not None))
    if oids:
        for oid, data in stream_blobs(oids):
            hits = find_markers(data)
            if hits:
                cached[oid] = hits
    found = []
    for path, oid in entries:
        if oid is None:
            try:
                with open(os.path.join(ROOT, path), "rb") as handle:
                    data = handle.read()
            except OSError as error:
                raise SystemExit("check_markers: cannot read %s: %s"
                                 % (path, error))
            hits = find_markers(data)
        else:
            hits = cached.get(oid, [])
        if hits:
            found.append((path, hits))
    return found


def resolve_range(explicit):
    """The pushed range: explicit, else the first of origin/main/main..HEAD."""
    if explicit:
        return explicit
    for upstream in ("origin/main", "main"):
        probe = subprocess.run(
            ["git", "-C", ROOT, "rev-parse", "--verify", "--quiet",
             upstream + "^{commit}"], capture_output=True)
        if probe.returncode == 0:
            return upstream + "..HEAD"
    return None


def scan_messages(range_text):
    """[(short sha, labels)] for commit messages in the range."""
    raw = _git(["log", "--format=%h%x00%B%x01", range_text])
    found = []
    for record in raw.decode("utf-8", "replace").split("\x01"):
        if "\x00" not in record:
            continue
        sha, body = record.strip().split("\x00", 1)
        hit = find_markers_in_text(body)
        if hit:
            found.append((sha, hit))
    return found


# ---------------------------------------------------------------------------
# self-test: the detector, not the tree
# ---------------------------------------------------------------------------


def self_test():
    checks = 0
    failures = []

    def expect(condition, label):
        nonlocal checks
        checks += 1
        if not condition:
            failures.append(label)

    name = MARKERS[0][1]
    first = MARKERS[1][1]
    account = MARKERS[2][1]
    folder = MARKERS[3][1]

    planted = [
        ("ASCII, mixed case", b"prefix " + name.swapcase().encode("ascii") +
         b" suffix", "operator name"),
        ("UTF-16LE at offset 1", b"X" + first.encode("utf-16-le"),
         "operator first name"),
        ("UTF-16LE at offset 1, mixed case",
         b"X" + first.swapcase().encode("utf-16-le"), "operator first name"),
        ("UTF-16BE at offset 1", b"X" + account.encode("utf-16-be"),
         "Windows account name"),
        ("UTF-16BE at offset 1, mixed case",
         b"X" + folder.swapcase().encode("utf-16-be"), "company folder"),
    ]
    for label, data, expected in planted:
        expect(expected in [hit for hit, _ in find_markers(data)], label)

    clean = (b"a clean line with no operator marker, and a UTF-16LE word\n"
             + "a clean line".encode("utf-16-le"))
    expect(find_markers(clean) == [], "a clean blob reports no marker")
    expect(find_markers_in_text("a clean commit message") == [],
           "a clean message reports no marker")
    expect("operator name" in find_markers_in_text(name.upper()),
           "a message in upper case is found")
    expect(redact("x" + name + "y") == "x[operator name]y",
           "redact replaces the literal with its label")

    if failures:
        for failure in failures:
            print("FAIL: self-test: " + failure)
        print("check_markers self-test: %d checks, %d failure(s)"
              % (checks, len(failures)))
        return 1
    print("PASS: check_markers self-test: %d checks, 0 failures" % checks)
    return 0


# ---------------------------------------------------------------------------


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--range", dest="range_text", default=None,
                        help="commit range for the message scan "
                             "(default: origin/main..HEAD, else main..HEAD)")
    parser.add_argument("--no-messages", action="store_true",
                        help="scan files only")
    parser.add_argument("--worktree", action="store_true",
                        help="read tracked files from the working tree "
                             "(default: the index, which is what a commit "
                             "and CI both see)")
    parser.add_argument("--self-test", action="store_true",
                        help="prove the detector finds planted markers")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()

    entries = worktree_entries() if args.worktree else tracked_entries()
    files = scan_files(entries)
    print("check_markers: %d tracked file(s), %d marker(s)"
          % (len(entries), len(MARKERS)))
    for path, hit in files:
        print("  %s  (%s; first byte %d)"
              % (path, labels(hit), min(offset for _, offset in hit)))

    message_hits = []
    if not args.no_messages:
        range_text = resolve_range(args.range_text)
        if range_text is None:
            print("check_markers: no origin/main or main ref; "
                  "commit-message scan skipped")
        else:
            message_hits = scan_messages(range_text)
            print("check_markers: commit messages on %s: %d hit(s)"
                  % (range_text, len(message_hits)))
            for sha, hit in message_hits:
                print("  %s  (%s)" % (sha, " ".join(hit)))

    if files or message_hits:
        print("check_markers: FAILED -- %d file(s), %d commit message(s)"
              % (len(files), len(message_hits)))
        return 1
    print("check_markers: OK -- no operator marker in a tracked file or "
          "commit message")
    return 0


if __name__ == "__main__":
    sys.exit(main())
