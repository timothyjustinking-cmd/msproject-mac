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
    # Wave 19: synthetic-base's tasks converted to automatic scheduling plus a
    # second named base calendar ("Shift A", Mon-Fri 09:00-13:00 + 14:00-18:00).
    # Built by scripts/build_calendar_fixture.py; pinned in engine_diff.py too.
    "tests/data/synthetic-calendars.mpp":
        "F486F0C3235BD8A592542E942141FE873C4262E6AFA19E029A9C4C79386017CA",
    "tests/data/project_a_recalc_golden.tsv":
        "36F156F618C94C92D70FBA6D754340D374DDE72E7D2FF136D1E22DBEE5400DB8",
    # Wave 23 (W23-1/W23-7/W23-8): the 3-level outline fixture built through
    # Project 2021 by scripts/oracle_w23.py -- six synthetic tasks, an FS+2 d
    # link, an SS+1 d link, an FF link and "Resource A" assigned to the last
    # task.  No client content.
    "tests/data/wave23-outline.mpp":
        "055721FF07E93F8C8C06B8428E9EF3495D10A38BE8C432506FCB5FF3A786DA20",
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
