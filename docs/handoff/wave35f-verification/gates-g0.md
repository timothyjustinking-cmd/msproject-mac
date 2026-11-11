# Wave 35f, group G0: builder record

Branch `wave35f`. One finding id per commit, each with the trailer
`Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35f)` (checked by `build/w35f/commit.sh`
before the commit and counted after). No writer added, no refusal of a persisted shape lifted, no
pair measured, no COM, no VM or trace touched, no push, no workflow dispatch, no history rewrite.

Gate tooling: `build/w35f/gates.sh <label> qt` for a commit that changes source,
`build/w35f/docgates.sh <label>` (the scans and the fixture check) for a records-only commit
(Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; trees `build/w35b-close-fresh`,
`-app`, `-qt`, built incrementally). Logs are `build/w35f/<parent-sha>-W35f-<label>-*.log` and
`*-gates.status` (git-ignored `build/`). Scans each time: `check_fixtures.py`,
`check_markers.py --range main..HEAD`, `guard_names.py`, `scan_client_names.py`, and
`git grep "client-string-[0-9a-f]{8}"` (none).

| Item | Finding | Commit | Status |
|---|---|---|---|
| 0.1 | R35eG1-1 | `fd457b4` | fixed |
| 0.2 | R35eG2-1 | `08b83f9` | fixed |
| 0.3 | R35eG1-3 | `1a2436e` | fixed (test and record) |
| 0.4 | R35eG1-2, with R35eG2-9 and R35eG1-5 | `fdff5d8` | fixed (records) |

## Gate line

- 0.1 (`d884b57-W35f-0.1-*`): core build 0 / 0 warnings, core ctest `-E plan_recalc` 65/65, app
  build 0 / 0 warnings, app 34/34, app_qt build 0 / 0 warnings, app_qt 52/52, fixtures 0, markers
  0, guard_names 0, scan_client_names 0, client-string grep none.
- 0.2 (`fd457b4-W35f-0.2-*`) and 0.3 (`08b83f9-W35f-0.3-*`): the same numbers (65/65, 34/34,
  52/52, 0 warnings, scans 0).
- 0.4 (records only, `1a2436e-W35f-0.4-*`): fixtures 0, markers 0, guard_names 0,
  scan_client_names 0, client-string grep none, `git diff --check` 0.
- The group's final tree (`fdff5d8-W35f-G0-record-*`): the full set again, same numbers (65/65,
  34/34, 52/52, 0 warnings, scans 0).

The measured pairs still write and match with effects on (the save-and-compare blocks of
`qt_sort_persist`, `qt_outline_persist` and `qt_calculate_gate` and the app and core calculate
pair tests are inside the suites above).

## 0.1 (R35eG1-1)

1.1(a)'s refusal sat before the queue decision, so it also refused Levels that could never be
queued (a read-only file, Level 3 to 9, a file the builder refuses), which wave 31 shows for the
session only, and its text was false for them. The `doc_.sort_active()` check is now on the queue
branch of `apply_outline_level` only: the edit is built as before, and when it would queue under a
shown session Sort the Level is shown for the session, the edit is built but not pushed, and the
note reads "session only; a session Sort is shown and File > Save would write the file's own order,
so the level is not queued". A Level that could never queue keeps its own note. The message is a
note, not an error (as the other session-only notes are); 35e's was an error.

Tests (`qt_outline_persist`, six-task outline where the Sort stays session only): a Level 1 under
the shown Sort queues nothing, is shown over the sorted rows in the sorted order (fewer rows than
the sort, a subsequence of it) and names the shown sort; All Subtasks shows every sorted row again
and queues nothing; Level 3 under the Sort is shown for the session with the builder's note and not
the sort's; with the sort cleared the same Level 1 queues. Red with `main_window.cpp` at the
parent (`build/w35f/0.1-red-run.log`): 3 FAILs ("the status line names the shown sort and the
refused queue", "the session Level is shown, over the sorted rows in the sorted order", "an
unmeasured Level under a session Sort is shown for the session with the builder's note"). Green:
`0.1-green-run.log`.

## 0.2 (R35eG2-1)

`show_custom_fields` has the queued-Calculate guard (`refuse_data_change`, the Calculate named)
before its dialog, as `show_custom_formula`. `qt_calculate_gate` 2c gets a `custom_fields` case:
the action opens no dialog, the status line names the queued Calculate, the queue stays at one
edit. Red with `main_window.cpp` at the parent (`0.2-red-run.log`): 2 FAILs ("Custom Fields beside
a queued Calculate Project opens no dialog" and "... the status line names it"). Green:
`0.2-green-run.log`. `spec/writers/calculate.md`'s 2.6 paragraph names the third surface.

## 0.3 (R35eG1-3)

The 1.1 record's claim that (a) was reachable only where the Sort stays session only is corrected
in `wave35e-verification/gates-g1.md` and `wave35e-complete.md`: (a) was also reachable on the
five-task file through an Undo of a queued Sort. The new `qt_sort_persist` block ("Undo then
Level") queues Name descending, Ctrl+Z, Level 1; it asserts the queue is empty, the status line
names the shown sort and the refused queue, the rows keep the sorted order, Ctrl+Y re-queues the
Sort the display shows and every row is shown again in sorted order. The "Found, not fixed" item
(Sort queued, Ctrl+Z, a session-only Level, Ctrl+Y) is withdrawn in both records.

## 0.4 (R35eG1-2, with R35eG2-9 and R35eG1-5)

Records only: START-HERE, `memory/coordinator.md` and `plan-waves27-30.md` list the open queue from
`wave35e-verification/coord-gates-close.md`; R35dG0-1 is closed (the coordinator's G0 review); the
four partly fixed 35b ids appear with their remainders; R35cW-3 has one status; R35cW-4's remainder
names R35dW-5; "closed by W35e-1.x" reads "fixed in source by W35e-1.x, not yet closed by the
coordinator" in `wave35c-complete.md` and `wave35d-complete.md`; the G1 lines name 1.4 `f244d02`;
the stale "still open, one High" paragraph of `status-2026-11-10.md` is dated as a snapshot.

## Guards seen red

- 0.1: the narrowing against the parent's refusal (the parent's `main_window.cpp` has the broader
  35e refusal and no queue-branch check), 3 FAILs (the note, the shown Level, Level 3) in
  `qt_outline_persist` (`build/w35f/0.1-red-run.log`). This run does not show the new branch
  turned off; that was seen red only by 0.3's mutation below.
- 0.2: the `show_custom_fields` guard (the parent's `main_window.cpp`), 2 FAILs in
  `qt_calculate_gate` (`0.2-red-run.log`).
- 0.3: the queue-branch check mutated so it cannot fire (`level < 0 && doc_.sort_active()`, then
  restored; a constant `false &&` is refused by MSVC's warnings as errors), 4 FAILs in the new
  block and 3 knock-on FAILs in the Undo-first block of `qt_sort_persist` (`0.3-red-run.log`).
  This is the run that shows the queue-branch `sort_active()` check (0.1's branch) seen red.
- 0.4: records; no guard.

## Deferred

Nothing.

## Found, not fixed

- Under a shown Sort a Level that the builder would accept now changes the display (collapses
  rows) for the session while File > Save keeps the file's outline, as every session-only Level
  does; the note says so. Nothing is written that the display does not show.
- The status of wave 35e's other "closed by W35e-0.1" wording (R35dG0-1, R35dW-1) is left as
  "closed": the coordinator's G0 review closed both.
