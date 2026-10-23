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
    # hidden in Project (214/Props 0x03400008 = empty).  Wave 25 (W25-10/S12)
    # commits its SHOWN partner from the SAME session (the record is
    # "Time&line"), so the pair reproduces from the repo; synthetic-base is a
    # different session and cannot.
    "tests/data/wave24-timeline-hidden.mpp":
        "9BE6D2E9343149F58DE7800F3F840B158AC90C4F0403423BF8167396EC2E61E4",
    "tests/data/wave24-timeline-shown.mpp":
        "829AE5E8DBAF6C33FF4043B5F210CCAB78730E18866BE491186BA2B3EE288EC5",
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
    # Wave 24 (W24-15 review): single-bit controls from the same one-flag-at-a-
    # time measurement -- Flag2 (+24/0x04) and Flag20 (+34/0x04).  They pin the
    # NON-CONTIGUOUS positions object-by-object, not just the all-20 aggregate.
    "tests/data/wave24-flag2.mpp":
        "614097D1031CA82B2F171170A5EB613349DA9BEEF6432CFB61900FBD91294BC6",
    "tests/data/wave24-flag20.mpp":
        "1C1C9F2433962AFE309535074D92CAEAFA8052572B7AA7AFEC22541F7479F64F",
    # Wave 25 (W25-2/S2): wave23-outline.mpp opened as a COPY, EVERY task
    # switched to automatic scheduling (Manual = False) and saved by Project
    # 2021 with neutral metadata.  Same table/geometry as the wave23 reference,
    # so the controlled difference is the bar colour: (138,187,237) auto, where
    # wave23-outline's all-manual capture measured (116,204,212).  The reference
    # that makes wave 24's manual-vs-auto mislabel fail the parity gate.
    "tests/data/wave25-auto.mpp":
        "BD8503EE4BDF7E117491E3C245D1A84E64ECF91F51B1AA0625AE4F064E96ED16",
    "tests/data/reference/wave25-auto.measures.json":
        "054CDBFAA0048F8FAECC20FD73A431E0CB0F57F919B27585351270CA0F8E7637",
    # Wave 25 (W25-9/S11): ONE single-bit control per flag in the +33 group
    # (Flag11..Flag17).  Built by the committed scripts/measure_flags.py from a
    # copy of wave23-outline.mpp: every flag False on ID 3, then only that flag
    # True, saved through Project 2021.  They pin the bit ORDER inside the
    # group, which the all-20 aggregate cannot.
    "tests/data/wave25-flag11.mpp":
        "A420D4C1639DCB8E1C16F6606417E7DBD48A0A95D444C64CF8889C5092E06965",
    "tests/data/wave25-flag12.mpp":
        "AD0C9F7A588F6A89945A0A3EBAD3B53D7DA1B7C522A1157B44A928FBA9151987",
    "tests/data/wave25-flag13.mpp":
        "023D40E4E568C244C3F3319FC64605BBFF52C31F75794FAFD1CDCDA4615DF521",
    "tests/data/wave25-flag14.mpp":
        "736090E534CC69AF08EB467D672B985E901C2B0F96A1C4720555D020862F22D4",
    "tests/data/wave25-flag15.mpp":
        "9873616512655C002AE9D0FAEC172D46E7D85216F4D259323616E6D82DBD36AB",
    "tests/data/wave25-flag16.mpp":
        "0D1F5FF13FB55F98D3AAE8647FB7FAA725C5B85E561B92B99A3D78E5065A92AE",
    "tests/data/wave25-flag17.mpp":
        "B7F9EDF6A35D61E6B17FEBFFC8FB6378325CD29E9F01FAB9D8002A23330AC0EB",
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


SCENARIO_DIR = os.path.join(ROOT, "tests", "data", "scenarios")


def _oracle_body(text):
    """The oracle's measured body, canonicalised: everything but provenance."""
    import hashlib
    import json
    data = json.loads(text)
    data.pop("provenance", None)
    canonical = json.dumps(data, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def _last_body_commit(rel):
    """The newest commit whose measured body differs from its predecessor's."""
    # --follow, per the brief: a renamed oracle must keep the commit that last
    # changed its body, not restart at the rename.
    commits = git("log", "--follow", "--format=%h", "--", rel).stdout.split()
    previous = None
    previous_sha = None
    for sha in commits:
        text = git("show", f"{sha}:{rel}").stdout
        if not text:
            continue
        body = _oracle_body(text)
        if previous is not None and body != previous:
            # `previous_sha` is the NEWER commit of the pair, so it is the one
            # whose body differs from its parent's.
            return previous_sha
        previous = body
        previous_sha = sha
    return commits[-1] if commits else None


def check_oracle_provenance():
    """W25-6/S7: provenance.recording_commit must be the last BODY change.

    The wave-24 provenance named the commit that introduced six oracles and one
    more for task-calendar, but every oracle's measured body was re-recorded by
    e91f19b.  This reconstructs the answer from git (a provenance-only edit does
    not count as a body change) and requires the JSON to hold it.
    """
    import glob
    import json
    import os as _os
    failures = []
    checked = 0
    for path in sorted(glob.glob(_os.path.join(SCENARIO_DIR, "*.oracle.json"))):
        rel = _os.path.relpath(path, ROOT).replace("\\", "/")
        try:
            with open(path, encoding="utf-8") as handle:
                data = json.load(handle)
        except (OSError, ValueError) as exc:               # noqa: BLE001
            failures.append(f"{rel}: unreadable oracle ({exc})")
            continue
        provenance = data.get("provenance") or {}
        claimed = str(provenance.get("recording_commit") or "")
        actual = _last_body_commit(rel) or ""
        if not claimed:
            failures.append(f"{rel}: provenance has no recording_commit")
            continue
        if not actual.startswith(claimed) and not claimed.startswith(actual):
            failures.append(
                f"{rel}: recording_commit {claimed} is not the last body change "
                f"({actual})")
            continue
        recorded = git("show", f"{actual}:{rel}").stdout
        if not recorded:
            failures.append(f"{rel}: {actual} does not contain the oracle")
            continue
        if _oracle_body(recorded) != _oracle_body(open(path, encoding="utf-8").read()):
            failures.append(
                f"{rel}: working body differs from the body recorded at {actual} "
                f"(re-record or update provenance)")
            continue
        checked += 1
    return failures, checked


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
        # A fixture staged for its FIRST commit is in the index but not in
        # HEAD; its documented SHA256 above is the check, and the HEAD
        # comparison starts applying on the next run.
        resolved = git("rev-parse", "--verify", "-q", f"HEAD:{rel}")
        head = resolved.stdout.strip()
        if resolved.returncode != 0 or not head:
            continue
        work = git("hash-object", "--", rel).stdout.strip()
        if work != head:
            failures.append(f"differs from HEAD: {rel} (work {work[:12]} head {head[:12]})")

    provenance_failures, oracles = check_oracle_provenance()
    failures.extend(provenance_failures)

    if failures:
        print("FIXTURE CHECK FAILED")
        for f in failures:
            print(f"  {f}")
        print("\nRestore with: Remove-Item <file>; git checkout HEAD -- <file>")
        return 1
    print(f"fixtures OK ({len(DOCUMENTED)} documented hashes, {len(tracked)} tracked .mpp)")
    print(f"scenario oracle provenance OK ({oracles} oracle(s), recording_commit = "
          f"last measured-body change)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
