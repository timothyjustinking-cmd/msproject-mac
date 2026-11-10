# Wave 35d, group G2: builder record

Branch `wave35d`. Fifteen items (the Lows, and the Medium R35cG0-2 with its record twin), in the
brief's order, one finding id per commit (2.1 closes two ids, as the brief says), each with the
trailer `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35d)` (checked by
`build/w35d/commit.sh` before the commit and counted after). No writer added, no refusal lifted,
no pair measured, no COM, no push, no workflow dispatch, no history rewrite.

Gate tooling as for G1: `build/w35d/gates.sh <label> [qt]` for a source change and
`build/w35d/docgates.sh <label>` for a records-only commit (Visual Studio 17 2022, Release,
`CORE_WARNINGS_AS_ERRORS=ON`). Logs are `build/w35d/<parent-sha>-W35d-<label>-*.log` and
`*-gates.status` (git-ignored `build/`); red runs are `build/w35d/<label>-red-*.log`. Scans each
time: `check_fixtures.py`, `check_markers.py --range main..HEAD`, `guard_names.py`,
`scan_client_names.py`, and `git grep "client-string-[0-9a-f]{8}"` (none).

| Item | Finding | Commit | Status |
|---|---|---|---|
| 2.1 | R35cG0-2 (R35cW-3 is fixed `6f5cc18`, a record; W35f-0.4, R35eG2-9) | `6f5cc18` | **partly fixed** (R35cG0-2): the writer half is recorded as closed by W35c-2.2 (its +104 check runs before 0.2); the plan half is open, since the plan never reads +96; the stale test comment is corrected |
| 2.2 | R35cG0-4 | `caa6cb1` | **partly fixed**: the plan layer still has no Props digest, now a recorded gap, and the two wrong sentences that explained it are corrected |
| 2.3 | R35cG0-5 | `ec2429c` | fixed: a time word of 24:00 or later is refused in `calculate_measured_first_working_start` |
| 2.4 | R35cG0-6 | `7d908c6` | fixed: the 0.2 and 0.3 tests use the record-kind needle, the start's kind check is tested, a different start that still maps is accepted |
| 2.5 | R35cG2-2 | `fc12254` | fixed: the row-0 ConstraintType refusal states the word it read; writer only (the plan holds no project row), recorded |
| 2.6 | R35cG2-3 | `f3c38d9` | fixed: the active-task check requires the Fixed2Meta entry to name the row's Fixed2Data record |
| 2.7 | R35cG2-6 | `51c474b` | fixed: a scratch path that cannot be examined is refused with the OS message (a test-only seam reaches it) |
| 2.8 | R35cW-5 | `492f10c` | fixed: the Props pin is `read_calculate_shape`'s last check |
| 2.9 | R35cG2-4 | `bb4a398` | fixed by the review's own option: the five-task outline on the second text style is refused by the builder under test, and the inference behind the six-task acceptance is stated in `view-state.md` |
| 2.10 | R35cG2-5 | `e415fe2` | fixed: `require_measured_view_block`'s `alt_digest_measured` has no default (a signature guard) |
| 2.11 | R35cW-4 | `4dcfcd7` | **partly fixed** (remainders R35dW-1, closed by W35e-0.1, and R35dW-5, the AppKit shell; W35f-0.4): a queued Calculate Project refuses a later cell edit, Cut, Paste, Fill and task-form edit with the Calculate named; the Redo route was closed by W35d-0.5 |
| 2.12 | R35cG1-6 | `f9771b6` | fixed: the plan layer reads the session byte of the resource rows the reader skips; one gap recorded (a row with no FixedMeta entry) |
| 2.13 | R35cG1-5 | `c250aef` | fixed (record): the `4a2133e` subject overstates Show Level, said in both records |
| 2.14 | R35cG1-7 | `a85d8b3` | fixed: the VarMeta keys are collected in the reader's existing per-task walk; the second scan is gone |
| 2.15 | R35cG2-7 | `6652ded` | fixed (record): the g3 fixtures' project starts are listed by pair from a decode (12 pairs at 13:00, 5 at 21:00) |

## Gate lines

Source rows (core `-E plan_recalc` 65/65, app 34/34, app_qt 52/52 where built; 0 warnings in
every build; fixtures 0, markers 0, guard_names 0, scan_client_names 0, client-string grep none):

- 2.1 (`2ebcfd8-W35d-2.1-*`): core and app built; the change to source is a test comment.
- 2.3 (`caa6cb1-W35d-2.3-*`), 2.4 (`ec2429c-W35d-2.4-*`), 2.5 (`7d908c6-W35d-2.5-*`),
  2.6 (`fc12254-W35d-2.6-*`), 2.7 (`f3c38d9-W35d-2.7-*`), 2.8 (`51c474b-W35d-2.8-*`): core 65/65,
  app 34/34, app_qt 52/52, except 2.4 (core and app built, no app_qt: a core test only).
- 2.9 (`492f10c-W35d-2.9-*`): core 65/65, app 34/34.
- 2.10 (`bb4a398-W35d-2.10-*`), 2.11 (`e415fe2-W35d-2.11-*`), 2.12 (`4dcfcd7-W35d-2.12-*`),
  2.14 (`c250aef-W35d-2.14-*`): core 65/65, app 34/34, app_qt 52/52.
- 2.2, 2.13 and 2.15 are records only: the scans without a build
  (`6f5cc18-W35d-2.2-*`, `f9771b6-W35d-2.13-*`, `a85d8b3-W35d-2.15-*`), plus `git diff --check`.

The last source change is 2.14 (`core/mpp/reader.cpp`); the final qt build and run is its row.
The measured pairs still write and match with effects on in every source row
(`mppwrite_roundtrip_calculate` and the viewstate pairs are in the core 65).

## Guards seen red

- 2.3: the 24:00 check removed, the relation table and the writer case FAIL (`2.3-red-run.log`).
- 2.4: the old needle and the kind test, FAIL (`2.4-red-run.log`; the Saturday-accept case shows an
  empty reason because the pin throws, which is the failure the item names).
- 2.5: the old text, FAIL (`2.5-red-run.log`).
- 2.6: the pairing check removed, FAIL (`2.6-red-run.log`).
- 2.7: the old "already exists ... move it aside" text appears (`2.7-red-run.log`).
- 2.8: the pin moved back to the first check, FAIL (`2.8-red-run.log`).
- 2.9: the guard removed, FAIL (`2.9-red-run.log`).
- 2.10: a signature guard; with a caller that omits the argument MSVC reports C2660
  (`2.10-red-build.log`).
- 2.11: the guard absent, four FAILs (`2.11-red-run.log`).
- 2.12: the plan check disabled (an always-false extra condition, since MSVC treats a constant
  condition as an error), two FAILs: the needle and `push()` (`2.12-red-run.log`). The builder
  still refuses on its own, because `make_assignment_edit` runs the writer's dry run; so the red
  is the plan layer's own text and `push()`, not the builder.
- 2.14: a mutant with the key collection removed fails `app_edits_calculate` (`2.14-red-run.log`);
  the commit is behaviour-preserving.
- 2.1, 2.2, 2.13 and 2.15: none; records (2.1 also a comment).

## Deferred

Nothing deferred. Two findings stay open in part, named in the table and in the G2 row of
`wave35d-complete.md`: the plan half of R35cG0-2 (the plan reads Start word +104 only) and the
plan-side Props digest of R35cG0-4.

## Found, not fixed

- 2.12: a resource row with no FixedMeta entry has no session byte, so the plan layer refuses it;
  the writer reads that row by entry index and may name a different byte. Recorded in
  `view-state.md`.
- 2.11: the Calculate-queued guard is tested through the cell gate; Cut, Paste, Fill and the task
  form share its helper but have no Calculate-queued test of their own.
- 2.14: no timing was taken; the claim is the removed walk.
- The G0 and G1 question stands: whether Redo should end the list for a Show Level, as it does
  for a Sort.
