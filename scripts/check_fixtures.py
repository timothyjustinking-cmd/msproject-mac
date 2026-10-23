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
    # Wave 23 (A-1d): the MEASUREMENTS of Project 2021's own capture of that
    # fixture.  The screenshot itself stays local (operator ruling 2026-10-23);
    # only the numbers are committed, and this is the CI parity reference.
    "tests/data/reference/wave23-outline-project.measures.json":
        "3CA8261885BD930F234149C8C65894B892F797A89FAE0DFB249FEA85271D60E0",
    # Wave 23 (W23-6): wave23-outline.mpp opened by Project 2021 on a copy,
    # Task.Flag10 set True on ID 3 and saved (the control save first).  The
    # reader's Flag10 decode is checked against this file.
    "tests/data/wave23-flags.mpp":
        "6B74A9DAEA67F0759A6A465BAD63D20556C8A06EFFFEDCE3EBF71CF4F1B80958",
    # Wave 24 (W24-2): Project 2021's own save of synthetic-base with Format >
    # Project Summary Task OFF and ON, built by scripts/oracle_w24.py under a
    # neutral path with neutral Author/Manager/Company/UserName.  The reader
    # decodes 214/Props 0x03400009 (0/1) from them; both fixtures store the
    # Timeline shown (0x03400008 = "Time&line").
    "tests/data/wave24-summary-off.mpp":
        "0EF4DE69CC9C2CF6D3C6568C86B9A8B708DE6E8CCC8D404BC4FBE82AE4A6C391",
    "tests/data/wave24-summary-on.mpp":
        "90BF1055E1FF415E453208C04123C2A2F869E38D950237DF116D59D2E53C7C05",
    # Wave 24 (W24-3): the same synthetic project saved with the Timeline strip
    # hidden in Project (214/Props 0x03400008 = empty).  Its shown partner is
    # synthetic-base.mpp, whose record is "Time&line".
    "tests/data/wave24-timeline-hidden.mpp":
        "9BE6D2E9343149F58DE7800F3F840B158AC90C4F0403423BF8167396EC2E61E4",
    # Wave 24 (W24-5): a FileNew project with four tasks, then a task inserted
    # above row 1 and saved.  Project appends the new FixedData row but displays
    # it as ID 2, so the stored order (Alpha, Bravo, Charlie, Delta, Inserted
    # Row) differs from ID order -- deleting the adapter's ID sort now fails CI.
    "tests/data/wave24-order.mpp":
        "515537D2B86415F85A3D15FB320C3F5A0EBB7A0951FF43CBCD00DC6B2BF6DDAF",
    # Wave 24 (W24-15): wave23-outline.mpp with Task.Flag1..Flag20 all True on
    # ID 3, built by the one-flag-at-a-time measurement (measure_flags.py).
    # The reader's generic flag table is checked against this file.
    "tests/data/wave24-flags20.mpp":
        "1C74D403B7EB5280D57F1D6264D6A17B28A2F9578AF15F40282FDDC44CC471FF",
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
