#!/usr/bin/env python3
"""build_calendar_fixture.py -- build tests/data/synthetic-calendars.mpp.

COORDINATOR ONLY (COM).  Wave 19: the `task-calendar` scenario needs a base
fixture that already carries a second NAMED base calendar.  `Project.Calendars`
does not exist in this Project build; `Application.BaseCalendarCreate` and
`Project.BaseCalendars` do (measured 2026-10-23, wave 19 probe):

    Application.BaseCalendarCreate: PRESENT
    Application.BaseCalendarEditDays: PRESENT
    Project.BaseCalendars: PRESENT   (name/index access, NO UniqueID property)
    Project.Calendars: absent
    Task.Calendar: name get/put

Because `BaseCalendars` has no UniqueID, the harness cannot ask Project
"which name is calendar uid N?" -- the uid lives only in the file.  The
uid -> name map for the synthetic fixture is therefore recorded in
scripts/engine_diff.py BASE_FILES["synthetic-calendars"]["calendars"] and is
checked here (the calendar this script creates must be the one recorded there).

WHAT IT DOES
  1. hashes tests/data/synthetic-base.mpp against the pinned SHA-256 and refuses
     on a mismatch (the same guard engine_diff.py uses);
  2. copies it into build/oracle-w19/ and opens THE COPY in MS Project -- the
     committed fixture is never opened in place (MS Project rewrites a file it
     opens; the Dropbox folder resurrected a damaged fixture three times);
  3. converts the three tasks to automatic (`Task.Manual = False`): a manual row
     keeps its dates, so a calendar change on one would be stored but not
     rescheduled, and the scenario would measure nothing;
  4. creates the base calendar "Shift A" from "Standard" and gives it Mon-Fri
     09:00-13:00 + 14:00-18:00 (a one-hour shift from Standard's 08:00-12:00 +
     13:00-17:00: the same working days, a different day boundary);
  5. CalculateProject, FileSaveAs the absolute output path, FileClose(0);
  6. re-opens the post-close file with our reader (engine_diff_tests --dump) and
     prints the calendar table and the tasks' manual flags, so the bytes are
     evidence rather than a claim.  FileSaveAs pads and Project compacts on
     close, so only the post-close file is evidence.

The output is synthetic (created here, no client data) and committed with its
SHA-256 pinned in engine_diff.py.  Re-run with --force only to rebuild it
deliberately; that changes the hash every scenario using it was recorded from.

    python scripts/build_calendar_fixture.py [--force]
"""

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from oracle_lock import oracle_session  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
BASE = ROOT / "tests" / "data" / "synthetic-base.mpp"
# Wave 33 (W33-4.3): synthetic-base's operator name was replaced in place with
# an equal-length neutral string (see scripts/check_fixtures.py); the
# scheduling payload this builder reads is unchanged, so rebuilding produces
# the same calendar data with the new base's neutral strings.
BASE_SHA = "6c120fbcc3d9dd8c5b4064c8555b10ba8e197dde35cdf7c78f2d099622c3777c"
OUT = ROOT / "tests" / "data" / "synthetic-calendars.mpp"
WORK = ROOT / "build" / "oracle-w19"
CALENDAR_NAME = "Shift A"


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--force", action="store_true",
                    help="overwrite an existing synthetic-calendars.mpp")
    ap.add_argument("--dump-bin", default="build/vs-w18/Release/engine_diff_tests.exe",
                    help="engine_diff_tests built with a --dump mode")
    args = ap.parse_args()

    if OUT.exists() and not args.force:
        sys.exit(f"{OUT} exists; pass --force to rebuild it (this changes the "
                 "SHA-256 every task-calendar oracle was recorded from)")
    if not BASE.exists():
        sys.exit(f"missing base fixture: {BASE}")
    digest = sha256(BASE)
    if digest != BASE_SHA:
        sys.exit(f"base fixture hash {digest} != pinned {BASE_SHA}; refusing to "
                 "build a fixture from a damaged base")

    WORK.mkdir(parents=True, exist_ok=True)
    opened = WORK / "calendars-work.mpp"
    shutil.copyfile(BASE, opened)
    assert sha256(opened) == BASE_SHA

    with oracle_session() as app:
        app.FileOpen(str(opened))
        proj = app.ActiveProject
        print(f"[fixture] opened copy {opened.name}, {proj.Tasks.Count} tasks")

        # 3. automatic scheduling: a manual row ignores its own constraint and
        # its calendar (wave 18 E2, measured), so the scenario must run on auto.
        manual_before = []
        for i in range(1, proj.Tasks.Count + 1):
            task = proj.Tasks(i)
            manual_before.append(bool(task.Manual))
            task.Manual = False
        print(f"[fixture] Manual before -> after: {manual_before} -> "
              f"{[bool(proj.Tasks(i).Manual) for i in range(1, proj.Tasks.Count + 1)]}")

        # 4. the second named base calendar.
        names_before = [proj.BaseCalendars(i).Name
                        for i in range(1, proj.BaseCalendars.Count + 1)]
        print(f"[fixture] base calendars before: {names_before}")
        if CALENDAR_NAME not in names_before:
            app.BaseCalendarCreate(CALENDAR_NAME, "Standard")
        names_after = [proj.BaseCalendars(i).Name
                       for i in range(1, proj.BaseCalendars.Count + 1)]
        print(f"[fixture] base calendars after: {names_after}")

        # Mon-Fri (WeekDay 2..6; VBA numbers 1=Sunday) 09:00-13:00, 14:00-18:00.
        # Signature: BaseCalendarEditDays(Name, StartDate, EndDate, WeekDay,
        # Working, From1..To5).  pywin32's dynamic dispatch rejects named
        # arguments (measured: TypeError), and the date bounds are optional, so
        # they are passed as VT_EMPTY: empty means "the whole calendar", which is
        # what a base-calendar week edit wants.
        import pythoncom
        for weekday in range(2, 7):
            app.BaseCalendarEditDays(
                CALENDAR_NAME, pythoncom.Empty, pythoncom.Empty, weekday, True,
                "09:00", "13:00", "14:00", "18:00")
        print("[fixture] edited Mon-Fri to 09:00-13:00 + 14:00-18:00")

        app.CalculateProject()
        app.FileSaveAs(str(OUT))
        # Only the post-close file is evidence (FileSaveAs pads).
        app.FileClose(0)

    digest = sha256(OUT)
    size = OUT.stat().st_size
    print(f"[fixture] wrote {OUT} ({size} B) sha256={digest}")

    # 6. re-open the bytes with our own reader and show the evidence.
    dump_bin = ROOT / args.dump_bin
    if dump_bin.exists():
        dump_path = WORK / "synthetic-calendars-dump.json"
        subprocess.run([str(dump_bin), "--dump", str(OUT), str(dump_path)],
                       check=True)
        dump = json.loads(dump_path.read_text(encoding="utf-8"))
        cals = dump["calendar_table"]["calendars"]
        print("[reader] calendars: " +
              ", ".join(f"uid={c['uid']} base={c['base_uid']} "
                        f"resource={c['resource_uid']} week={c['has_week_data']}"
                        for c in cals))
        print("[reader] tasks (uid, manually_scheduled, duration, start, finish):")
        for t in dump["tasks"]:
            print(f"         {t['uid']} {t['manually_scheduled']} "
                  f"{t['duration_ticks']} {t['start']} {t['finish']}")
    else:
        print(f"[reader] SKIP: {dump_bin} not built")
    print("\nNext: pin this SHA-256 in scripts/engine_diff.py "
          "BASE_FILES['synthetic-calendars'] and record scenario 07.")


if __name__ == "__main__":
    main()
