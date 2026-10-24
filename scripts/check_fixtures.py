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
import re
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
    # Wave 28 (W28-0.1): the two UTF-16LE save-path strings (Props14 and the
    # 214/CEdl/Var2Data entry) named the operator and the company Dropbox
    # folder.  Because this file is the engine suite's calendar base and its
    # recorded oracle depends on its bytes, the brief allowed them to be
    # replaced IN PLACE with a neutral same-length string (C:\w19\... padded;
    # scripts/w28_neutralise_path.py).  The operator's name, still stored in
    # the SummaryInformation properties and one Var2Data value of the same
    # blob, was then replaced the same way (scripts/w28_neutralise_operator.py)
    # because the pre-push guard refuses any new blob that carries it.  Every
    # other byte is unchanged; only the path and name text differs, hence the
    # new hash.
    "tests/data/synthetic-calendars.mpp":
        "BAF83A50C19A82F834CD7CCBF833E53C2DB9130C9627F2EAB6EBE44F3C5D68AE",
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
    # Wave 28 (W28-0.1): re-derived by scripts/oracle_w28_privacy.py under
    # C:/w28 because the wave-23 save embedded the operator's local save path.
    # Same Project save of the same base: Task FixedData is byte-identical to
    # wave23-outline.mpp and Task FixedMeta differs only at entry byte +25 bit
    # 0x04 (offset 323, 00 -> 04).
    "tests/data/wave23-flags.mpp":
        "72ACC2086FA9A97781F96EBBD243D4C2CF91E7902BEE9FE19D8139303E3E802A",
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
    # Wave 27 (W27-0.1): synthetic-base opened on a copy and Task ID 1 set to
    # 50 % PercentComplete THROUGH PROJECT 2021, saved with neutral metadata.
    # The committed evidence that Project's own % edit writes actual duration
    # (+80), remaining duration (+88), actual start (+120), two dates
    # (+132/+136), a progress flag (+168), the project row's roll-up
    # (+80/+88/+92) and Var2Data field 0x0B40015E -- not the single +92 word the
    # port used to write.  It is also the project-with-actuals fixture the Move
    # Project prompt gate opens now that the % buttons are disabled.
    # Wave 28 (W28-0.1): re-derived by scripts/oracle_w28_privacy.py under
    # C:/w28; the wave-27 save embedded the operator's local save path.
    "tests/data/wave27-percent-actuals.mpp":
        "56198DEA93A518B32E8813858855368C33C472B191D49AAD5E23E27CE8196C2C",
    # Wave 27 (W27-0.3): the SAME Project 2021 session's save of synthetic-base
    # before and after Project Summary Task > Priority was set to 750, with
    # neutral metadata.  The pair's TBkndTask/FixedData differs in exactly the
    # project row's +78 word (500 = 0x01F4 -> 750 = 0x02EE); every other stream
    # difference is per-save bookkeeping (the wave-24 same-session pair shows
    # the same GUID/edit-list churn).  The 750 file is the fixture the port's
    # saved FixedData stream is compared with byte for byte.
    # Wave 28 (W28-0.1): re-derived in one session by
    # scripts/oracle_w28_privacy.py under C:/w28; the wave-27 saves embedded
    # the operator's local save path.  The pair still differs only at +78/+79.
    "tests/data/wave27-priority-control.mpp":
        "5ACA3BBA6E4B49CF6E91F8E582F1E1DCF45E23CFC0C424C3632E10A2F2C72152",
    "tests/data/wave27-priority-750.mpp":
        "C5A6F06E39665323323BE796EA484FDBD1C0ED2CF9261F4F104565DA48FB6252",
    # Wave 27 (W27-3.3): the SAME Project 2021 session's save of
    # wave23-outline.mpp before and after Task > Unlink Tasks on the one task
    # with both a predecessor and a successor (uid 4: 3 -> 4 FS+2 d and
    # 4 -> 6 FF), with neutral metadata, built by the committed
    # scripts/oracle_w27_unlink.py.  Project removes BOTH links touching the
    # task (TaskDependencies 3 -> 1; 3 -> 5 SS+1 d survives; still 1 after
    # reopen).  The link FixedData stays byte-identical, FixedMeta tombstones
    # entries 0 and 2 and sets header flags 4 -> 6.  The unlinked file is the
    # oracle the port's Unlink save is compared with.
    # Wave 28 (W28-0.1): re-derived in one session by
    # scripts/oracle_w28_privacy.py under C:/w28; the wave-27 saves embedded
    # the operator's local save path.  The control's three TBkndCons streams
    # still equal wave23-outline's, and the unlink save still changes only
    # FixedMeta's tombstones and header flags.
    "tests/data/wave27-unlink-control.mpp":
        "55CF09C7EAE1CD29E8A707394C1F3E49D065E368E6AF4FF113BB071A9D91C171",
    "tests/data/wave27-unlink.mpp":
        "83DFA7677E75CEE5F4810F07E86149E372BA71FEB0D57BE4C84D23B27E6DB178",
    # Wave 26 (W26-3.1): the Gantt view's style record and the 214/Props
    # FONT_BASES payload, extracted read-only from tests/data/synthetic-base.mpp
    # (its first decodable Gantt view, "Gantt &with Timeline"; the W26-3.1
    # fixture commit message said "view 2, &Gantt Chart" -- the errata is in
    # wave26-complete.md).  Project 2021's own MPP14 bytes; the blobs carry no
    # path, host name or task text (checked by the name/path scan).  The
    # decoder's offsets are pinned by scripts/oracle_w26.py's one-change-per-save
    # session (build/w26/g3/), so these are a real file's bytes, not synthetic.
    "tests/data/synthetic-gantt-style.bin":
        "B9FD3240CCB0EBD7B94AF17824C1FE104F44BC860AD4DCE093AF7BFD75B23CF4",
    "tests/data/synthetic-gantt-fonts.bin":
        "BEC0B057A37AE959A123BBC28C052366C6134DCC7DF6C5DE308FA897C568F478",
    # Wave 27 (W27-2.1): the wave-26 one-change-per-save evidence, extracted
    # from Project 2021's own saves of tests/data/synthetic-base.mpp (the
    # applied view's `214/CV_iew` main block, record 0x22400008).  The saves
    # were made by the local, git-ignored scripts under build/w26/g3
    # (oracle_styles.py, oracle_probe.py, oracle_probe3.py); each blob is the
    # record of one save, and control.style.bin is the first session's unedited
    # save.  The two font blobs are the `214/Props` FONT_BASES payload of the
    # font-family and font-size saves; every other save's payload is
    # byte-identical to synthetic-gantt-fonts.bin.  A record carries no path,
    # author, project title or task text (only Project's stock bar-style names
    # and font families), so no metadata needed neutralising.  The source .mpp
    # hashes are in tests/data/README.md; the .mpp files stay local.
    "tests/data/wave26-gantt-saves/control.style.bin":
        "37A84A7963DD671CEADF9CFE2DAB06D1CBD798F00A0614420B9238418E849686",
    "tests/data/wave26-gantt-saves/arial.style.bin":
        "D5E493B8AB72EE8DAFA2FBA0006A8F4F734886167D59D343FAE32253CE26C98C",
    "tests/data/wave26-gantt-saves/arial.fonts.bin":
        "3CBE0D5459C1E30B5F37552D13669064768523C598003CBFFE7FA504C0C29A16",
    "tests/data/wave26-gantt-saves/size8.style.bin":
        "43E14459F7D37FEFC56C36B065D66CA4705C868508AC1DF3C7AB78CADB39E88B",
    "tests/data/wave26-gantt-saves/size8.fonts.bin":
        "DFDD77C6D17133401510414B75AE175244A4C24D5E9111CBCFED074796D8210A",
    "tests/data/wave26-gantt-saves/critical-blue.style.bin":
        "FAF026BB99504B7A8C20A9504FDB4740C5C5FA395A03E0BF84C6402C0FEC3A0E",
    "tests/data/wave26-gantt-saves/milestone-italic.style.bin":
        "B3C4F76B11244AF4DEB7D18DDB6D4C1E58C9D89ED7E5BA6F57A749BB6521620E",
    "tests/data/wave26-gantt-saves/milestone-bold.style.bin":
        "873EDE49F8C21681265B5DCC53B2B221CA13DFF4FED716A76014E4EFDFFED722",
    "tests/data/wave26-gantt-saves/milestone-underline.style.bin":
        "3E250AF7329D9BA3E843BF0A21E1F89BBE237A60ABE2789A3A2D3B1F9D1174B9",
    "tests/data/wave26-gantt-saves/highlight-fill.style.bin":
        "DECD741A59CAE93EFE5027B1B1FA2FCE1D950C517E521C1C2C07136C900A308A",
    "tests/data/wave26-gantt-saves/task-red.style.bin":
        "9CA275EA9945F362E3B369B491F05799E07E215FA70A82430AB0E9D40CAD5610",
    "tests/data/wave26-gantt-saves/summary-green.style.bin":
        "42C316FCFED265E4703846373C75502747E4BB142FE6A49DC426D34E906A3BAB",
    "tests/data/wave26-gantt-saves/milestone-blue.style.bin":
        "C13550BC657B240198B88CDFEFCCF89A940AEAE61E0187C601BA091E78513C39",
    "tests/data/wave26-gantt-saves/ts-yqm.style.bin":
        "24332C5A66BDEA6A101E17FB6EC32471362F98DF08DE3D734696ECA323D61E6B",
    "tests/data/wave26-gantt-saves/ts-ymd.style.bin":
        "E13B487DDAF942C3DB40E7FF98EA61926765C234CFB3DBAEFEA4F2F9768DEC3C",
    "tests/data/wave26-gantt-saves/ts-yhq.style.bin":
        "78C8D721A02628517821FC4A88F4F6F1DF4F2A5250B63B16BF5A4991690E0A43",
}


# W28-0.1: a committed fixture must not carry a local absolute path.  Project
# records the save path in every save (`Props14` and the `214/CEdl/Var2Data`
# entry, UTF-16LE), and a save made in the working tree therefore named the
# operator and the company Dropbox folder.  The six wave-23/27 fixtures were
# re-derived under C:/w28 and the wave-19 calendar fixture's two records were
# edited in place, so every Project record now names only a neutral oracle
# root.  Those roots are the ONE allowed form: `C:\wNN\...` (any NN, either
# separator, with or without a trailing separator).  Any other drive path --
# and any user-profile path, whichever drive -- fails.
#
# Both encodings are searched: UTF-16LE as a byte sequence (this finds a
# string wherever it starts in the file, i.e. at either byte alignment) and
# raw ASCII.  The stock `gbui://mainpage.htm` record contains "i:/" but is not
# a drive path, hence the look-behind for a preceding alphanumeric; in UTF-16LE
# that look-behind is the two-byte pair `[A-Za-z]\0`.
def _u16(text):
    return text.encode("utf-16le")


NEUTRAL_ROOT = re.compile(r"^[Cc]:[\\/]w\d+(?:[\\/]|$)")
LOCAL_PATH_UTF16 = re.compile(
    rb"(?<![A-Za-z0-9]\x00)[A-Za-z]\x00:\x00[\\/]\x00")
LOCAL_PATH_BYTES = re.compile(rb"(?<![A-Za-z0-9])[A-Za-z]:[\\/]")
PROFILE_UTF16 = re.compile(b"|".join(_u16(text) for text in (
    "\\Users\\", "/Users/", "/home/", "\\Documents and Settings\\")))
PROFILE_BYTES = re.compile(
    rb"\\Users\\|/Users/|/home/|\\Documents and Settings\\")


def _path_token(data, at, wide):
    """The path-like token containing byte offset `at` (quotes/NUL delimit)."""
    path_chars = ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
                  "0123456789:\\/._-~$ ")

    def path_char(value):
        return 0x20 <= value < 0x7F and chr(value) in path_chars

    if wide:
        start = at
        while start >= 2 and data[start - 1] == 0 and path_char(data[start - 2]):
            start -= 2
        end = at
        while end + 1 < len(data) and data[end + 1] == 0 and path_char(data[end]):
            end += 2
        return data[start:end].decode("utf-16le", "replace")
    start = at
    while start >= 1 and path_char(data[start - 1]):
        start -= 1
    end = at
    while end < len(data) and path_char(data[end]):
        end += 1
    return data[start:end].decode("latin-1")


def _path_hits(data, rel):
    hits = []
    for regex, kind, wide in ((LOCAL_PATH_UTF16, "drive path", True),
                              (PROFILE_UTF16, "user-profile path", True),
                              (LOCAL_PATH_BYTES, "drive path", False),
                              (PROFILE_BYTES, "user-profile path", False)):
        for match in regex.finditer(data):
            at = match.start()
            value = _path_token(data, at, wide)
            if kind == "user-profile path" or not NEUTRAL_ROOT.match(value):
                hits.append((rel, kind, at, value))
    return hits


def check_fixture_paths():
    """W28-0.1: no committed fixture may embed a local absolute path."""
    failures = []
    tracked = git("ls-files", "tests/data").stdout.split()
    for rel in tracked:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            continue
        with open(path, "rb") as handle:
            data = handle.read()
        for rel_name, kind, offset, value in _path_hits(data, rel):
            failures.append(f"{rel_name}: {kind} at byte {offset}: {value!r}")
    return failures, len(tracked)


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
        if not actual:
            # A staged-but-never-committed oracle has no history to verify; a
            # claimed commit would otherwise pass the empty-prefix test.
            failures.append(
                f"{rel}: not committed; its recording_commit cannot be verified")
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

    path_failures, scanned = check_fixture_paths()
    failures.extend(path_failures)

    if failures:
        print("FIXTURE CHECK FAILED")
        for f in failures:
            print(f"  {f}")
        print("\nRestore with: Remove-Item <file>; git checkout HEAD -- <file>")
        return 1
    print(f"fixtures OK ({len(DOCUMENTED)} documented hashes, {len(tracked)} tracked .mpp)")
    print(f"fixture paths OK ({scanned} tracked test-data files, no local path)")
    print(f"scenario oracle provenance OK ({oracles} oracle(s), recording_commit = "
          f"last measured-body change)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
