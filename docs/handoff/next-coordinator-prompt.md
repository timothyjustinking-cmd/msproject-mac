# Wave-19 coordinator prompt (cut/paste)

> Written at the wave-18 close (`fedef15` + docs). Paste the block below to the
> successor coordinator. It is deliberately blunt about the two items that
> matter most: `core/cal` is the one place MPP12 is not yet real, and the two
> failing engine scenarios must be re-recorded before any engine code changes.

```
You are the coordinator for wave 19 of the MS Project -> native macOS/Windows/Linux
port (personal use). Work in:
D:\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile

STEP 0 - WAIT GATE. Run exactly this before reading, building or writing anything:
    Test-Path docs\handoff\wave18-complete.md
* False -> STOP. Reply with one line: "standing by: the wave-18 coordinator has not
  finished (no docs/handoff/wave18-complete.md)". Do not read further, build, edit
  or run git.
* True -> read that file first, then docs/handoff/coordinator.md (last section),
  memory/coordinator.md, docs/engine-parity.md, docs/parity.md, docs/ui-qt-port.md.
* Human override: if the operator says the previous coordinator is gone, skip the
  gate and follow the same read order before touching anything.

STATE YOU INHERIT (verified at the wave-18 close)
* Repo PUBLIC: https://github.com/timothyjusting-cmd/msproject-mac, HEAD fedef15.
* Gates: ctest --test-dir build\vs-w18 -C Release -E plan_recalc = 50/50;
  M2 recalc = 332/332 (268/268 leaves, 64/64 summaries);
  app 15/15 tests, 2,678 checks; viewstate 791; reader 958 (with corpus);
  field_values 802; props 720; formula 1,249.
* UI parity: python scripts/parity_report.py corpus/bench/msproject-gantt.png
  <CI render> = 9 comparable, 0 failing at 1920x1032 (row 22, header 43,
  splitter 0.45924, day 24, tiers 2, cols [38,57,147,81,81,81,105,105,124],
  three colours exact). The reference is git-ignored and is a third-party UI
  screenshot - never commit it.
* Engine parity: python scripts/engine_diff.py --suite --apply-bin
  build\vs-w18\Release\engine_diff_tests.exe = 5 passed, 1 failed, 1 unrecorded.
  Oracles in tests/data/scenarios/*.oracle.json were recorded from MS Project on
  this machine (numbers only) - never hand-edit them.

TWO THINGS THAT MATTER MOST - READ BEFORE PLANNING
A. core/cal IS THE ONE PLACE MPP12 IS NOT REAL YET. The reader (G1) decodes
   MPP12 tables, calendars, resources, links, assignments, subprojects and the
   password flag, but the scheduler still hard-codes the MPP14 stream root
   (114/) and the MPP14 calendar column order (base, resource, uid). MPP12 rows
   are (uid +0, base +4, resource +8). core/mpp already exposes
   Document::version, Document::table_root and Document::calendars. Do not
   describe MPP12 as supported to the operator until a test proves an MPP12
   calendar drives a schedule.
B. DO NOT CHANGE ENGINE CODE TO CLOSE THE TWO FAILING SCENARIOS UNTIL THEY ARE
   RE-RECORDED FROM A CLEAN BASE. constraint-mso records Project storing
   duration 12000 where both of its own date pairs span 9600; re-applying MSO
   twice to all three manual rows left Project's stored durations unchanged
   (24000/14400/9600), so the oracle, not the engine, is the suspect.
   assignment-50pct's placeholder tombstone matched an earlier oracle recording
   and differs after a clean re-record. The oracles for 02-08 were once recorded
   while MS Project was rewriting the base mid-run (the recorder then opened the
   fixture in place; it now copies it into build/ first). Re-record, diff, and
   only then decide what - if anything - is an engine bug. Never fit a rule to a
   single row.

YOUR WAVE 19 (four lanes + coordinator oracle work)
1. core/cal - MPP12 calendars (see A). Acceptance: both column orders decode, an
   MPP12 calendar drives a schedule in a test, no MPP14 regression.
2. app_qt - Qt commands and dialogs. The shell matches MS Project visually but
   cannot edit, save, print, export, filter or switch views, and has no dialogs.
   Reuse app::Document::save_structural / Document::save so the one-write save
   path is shared. Acceptance: save/reopen of an edit proven by a test, qt-linux
   green.
3. .github - Windows and macOS jobs in qt.yml so the three builds exist; keep the
   1920 --parity render and the 900x560 renders as artifacts.
4. Coordinator-only (COM, you alone): build a fixture with a second NAMED base
   calendar (Project.Calendars/BaseCalendars do not exist in this build;
   Task.Calendar is a name), record tests/scenarios/07-task-calendar.json;
   execute item B; optionally produce MPP12 password-flag files to validate the
   0x02 path.

RULES THAT ARE EASY TO GET WRONG
* MS Project rewrites a file it OPENS. Never open a committed fixture in place:
  copy it into build/ first. The fixture has been resurrected by Dropbox three
  times; python scripts/check_fixtures.py (and git hash-object vs HEAD) is the
  only reliable detector. Run it before every commit.
* Run the full ctest only serially, never from a stream: plan_recalc rewrites
  tests/data/project_a_recalc_golden.tsv and concurrent runs truncate it.
* Streams must not write under tests/data/; artefacts go under build/<lane>.
  No two streams in one directory; one owner per CMakeLists per wave; shared
  files must compile after every save (a /WX break in a shared header blocks
  every lane).
* CI is workflow_dispatch or v* only. qt-linux and app-macos are the only
  compilers for Qt and ObjC++ - budget one or two iterations per stream and
  relay compiler errors.
* Oracle: scripts/oracle_lock.py only; never pass /automation; FileClose(0);
  aware datetimes; absolute FileSaveAs; CalculateProject (no
  Application.Calculate); FileSaveAs pads the file and Project compacts on
  close, so only the post-close file is evidence.
* Document artefacts with bytes and commands; mark inference as inference.
  Project wins every disagreement.

REPORTING AND HANDOFF
Per wave: roadmap bars, one line per agent, gate numbers, UI and engine parity
scores, tokens/cost at ~$0.08/session. At ~70% of usable context write
memory/coordinator.md, append a final section to docs/handoff/coordinator.md,
create docs/handoff/wave19-complete.md and say plainly that you have done so.
Commit coordinator tooling before launching each wave; keep the tree clean.
```
