#!/usr/bin/env python3
"""Verify the committed test fixtures are the ones HEAD says they are.

The Dropbox folder lies about mtimes and has twice resurrected a Project-saved
`synthetic-base.mpp` over the committed one (same path, different bytes, mtime
from hours earlier).  `git status` can miss it when the stat cache matches, so
the only reliable check is `git hash-object` vs `HEAD`, plus the documented
SHA256 for the one fixture whose content is quoted in docs.

    python scripts/check_fixtures.py          # exit 1 on any mismatch
"""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Fixtures whose exact bytes are documented (SHA256, from docs/handoff/wave14-complete.md).
DOCUMENTED = {
    "tests/data/synthetic-base.mpp":
        "55C109F2F62FD6B9F05789AB002C35991A424AAB18C78F92E4200DF7B3FC6069",
    "tests/data/project_a_recalc_golden.tsv":
        "90BA71C85AF5A1456EAACB92AC86FCA5B81BF334ABC834C689E0E62428F8DAA0",
}


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True,
                          text=True, encoding="utf-8", errors="replace")


def sha256(path):
    import hashlib
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def main():
    failures = []

    for rel, want in DOCUMENTED.items():
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            failures.append(f"MISSING {rel}")
            continue
        got = sha256(path)
        if got != want:
            failures.append(f"SHA256 {rel}\n  want {want}\n  got  {got}")

    # Every tracked .mpp must match HEAD byte-for-byte.
    tracked = git("ls-files", "*.mpp").stdout.split()
    for rel in tracked:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            failures.append(f"MISSING {rel}")
            continue
        head = git("rev-parse", f"HEAD:{rel}").stdout.strip()
        work = git("hash-object", "--", rel).stdout.strip()
        if head and work != head:
            failures.append(f"differs from HEAD: {rel} (work {work[:12]} head {head[:12]})")

    if failures:
        print("FIXTURE CHECK FAILED")
        for f in failures:
            print(f"  {f}")
        print("\nRestore with: Remove-Item <file>; git checkout HEAD -- <file>")
        return 1
    print(f"fixtures OK ({len(DOCUMENTED)} documented hashes, {len(tracked)} tracked .mpp)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
