# Wave 35f: completion record

## G0: the Mediums

Builder record: `wave35f-verification/gates-g0.md`. Commits: `fd457b4` (0.1), `08b83f9` (0.2),
`1a2436e` (0.3), `fdff5d8` (0.4). The trailer is on each, checked by script.

| Item | Finding | Status |
|---|---|---|
| 0.1 | R35eG1-1 | fixed `fd457b4`; only the QUEUE of a Level under a shown session Sort is refused, a Level that could never queue is shown for the session as in wave 31; tested in `qt_outline_persist` |
| 0.2 | R35eG2-1 | fixed `08b83f9`; Project > Custom Fields... refuses a formula beside a queued Calculate before its dialog; a `custom_fields` case in `qt_calculate_gate` |
| 0.3 | R35eG1-3 | fixed `1a2436e`; the five-task Undo-Sort-then-Level test in `qt_sort_persist`; the 1.1 records corrected; the wrong "Found, not fixed" item withdrawn |
| 0.4 | R35eG1-2, with R35eG2-9 and R35eG1-5 | fixed `fdff5d8` (records): the open queue, the four partly fixed 35b ids with their remainders, R35cW-3's one status, R35cW-4's remainder naming R35dW-5 |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; 0.1
`d884b57-W35f-0.1-*`, the group's final tree `fdff5d8-W35f-G0-record-*`): core build 0 with 0
warnings; `ctest -E plan_recalc` 65/65; app 34/34; app_qt 52/52; `check_fixtures` 0;
`check_markers --range main..HEAD` 0; `guard_names` 0; `scan_client_names` 0; client-string grep
none. 0.4 is records only (scans, fixtures and `git diff --check`, 0). The measured pairs still
write and match with effects on.

**Guards seen red:**
- 0.1: the queue-branch `sort_active()` check, 3 FAILs in `qt_outline_persist`
  (`build/w35f/0.1-red-run.log`).
- 0.2: the `show_custom_fields` guard, 2 FAILs in `qt_calculate_gate` (`0.2-red-run.log`).
- 0.3: the queue-branch check mutated so it cannot fire, 4 FAILs in the new `qt_sort_persist`
  block and 3 knock-on FAILs (`0.3-red-run.log`).

**Deferred:** nothing.

**Found, not fixed:**
- Under a shown Sort a Level the builder would accept now changes the display for the session
  while File > Save keeps the file's outline, as every session-only Level does; the note says so
  (`gates-g0.md`).

No writer added, no refusal of a persisted shape lifted, no pair measured, no COM, no VM or trace
touched, nothing pushed, no workflow dispatched.

WAVE-35f-G0-COMPLETE

## G1: the Lows

Builder record: `wave35f-verification/gates-g1.md`. Commits: `9e37503` (1.1), `6d93f13` (1.2),
`ae36884` (1.3), `d880529` and `b3c19d3` (1.4), `e303559` (1.5), `5506509` (1.6), `4ffd8a7` (1.7),
`a0bf0c2` (1.8), `66c1555` (1.9), `26be753` (1.10), `76c8927` (1.11), `5ce1c73` (1.12), `7f63a01`
(1.13), `6a687db` (1.14), `5676398` (1.15), `336f197` (1.16). The trailer is on each, checked by
script. Nothing below is called closed: that is the coordinator's review.

| Item | Finding | Status |
|---|---|---|
| 1.1 | R35eG0-1 | fixed `9e37503`; the untouched-field skip compares the rendering the values came from; a `qt_task_information_apply` case |
| 1.2 | R35eG0-2 | fixed `6d93f13` (tests): the Task Form entry and the structural-trial partial apply, in `qt_calculate_gate` |
| 1.3 | R35eG0-3 | fixed `ae36884`; a partly applied Task Information edit names the field that stuck and Undo |
| 1.4 | R35eG1-4 | fixed `d880529`, record `b3c19d3`; a cleared session sort ends a Sort on the redo list only, a Show Level stays |
| 1.5 | R35eG2-2 | fixed `e303559`; a pending document edit ends the structural redo list, on the three surfaces |
| 1.6 | R35eG2-4 | fixed `5506509`; Calculate's refusal beside a pending document edit gives advice that holds |
| 1.7 | R35eG2-6 | fixed `4ffd8a7` (test): a pending formula, then Calculate Project |
| 1.8 | R35eW-1 | fixed `a0bf0c2`, with a coverage limit: the refusals at Resource Information, Add Resources, Assign Resources, Font Bold and Define WBS Code share one helper and are exercised through the Manually Schedule case only |
| 1.9 | R35eW-3 | recorded `66c1555`: 2.1's Qt call site is defensive and unreached; the review's seam test would need a mutable queue accessor, not added |
| 1.10 | R35eG2-3 | **fixed in source `26be753`; `macos-appkit` is its gate** (the AppKit mirror of 2.6, the redo clear and 1.8; not compiled, run or tested here) |
| 1.11 | R35eG2-7 | fixed `76c8927`; the fault seam has the step after the re-read compare |
| 1.12 | R35eG2-8 | fixed `5ce1c73`; the scratch-probe seam has a scope guard, with a test of the guard |
| 1.13 | R35eG1-6 | fixed `7f63a01` (records) |
| 1.14 | R35eG2-5, R35eW-4 | fixed `6a687db` (records): `macos-appkit` named as `app/main.mm`'s gate |
| 1.15 | R35eW-2 | fixed `5676398` (record) |
| 1.16 | R35eG2-10 | fixed `336f197`; 18 lines over 100 columns rewrapped in the five files named, style only |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; for example 1.8
`4ffd8a7-W35f-1.8-*` and 1.16 `5676398-W35f-1.16-*`): core build 0 with 0 warnings;
`ctest -E plan_recalc` 65/65; app 34/34; app_qt 52/52; `check_fixtures` 0;
`check_markers --range main..HEAD` 0; `guard_names` 0; `scan_client_names` 0; client-string grep
none. The records-only commits (1.4's record, 1.9, 1.13, 1.14, 1.15) and 1.10 ran the scans,
the fixture check and `git diff --check` only (all 0). The group's final tree (`G1-final`) ran the
full set again. The measured pairs still write and match with effects on.

**Guards seen red** (counts are the commit messages'): 1.1 1 FAIL; 1.2 2; 1.3 1; 1.4 4 (and the
`apply_session_sort` clear alone 2, the `apply_sort_choice` clear alone none, so it is defence in
depth); 1.5 6; 1.6 1; 1.7 2; 1.8 4 in each of three tests; 1.11 2 and 1 (two mutations); 1.12 4.
1.9 and 1.13 to 1.15 are records, 1.10 cannot be built here, 1.16 is a rewrap.

**Deferred:** nothing.

**Found, not fixed:**
- Nothing new. Fill Down's guard (wave 35e 2.5) and `apply_session_sort`'s missing queued-view
  guard (2.3) stand as recorded in `wave35e-verification/gates-g2.md`.

No writer added, no refusal that guards a save lifted, no new Project pair measured, no COM, no VM
or trace touched, nothing pushed, no workflow dispatched.

WAVE-35f-G1-COMPLETE

## CLOSE

### CLOSE gates

Fresh trees `build/w35f-close-fresh`, `-app` and `-qt` at `135d253` (the last code change is
`76c8927`/`5ce1c73` in `core/mppwrite` and `26be753` in `app/main.mm`; every later commit is a
record or a rewrap), configured from scratch with `CORE_WARNINGS_AS_ERRORS=ON`; script
`build/w35f/W35f-CLOSE-fresh-gates.ps1`, log `build/w35f/W35f-CLOSE-fresh-gates.log` (summary:
`build/w35f/close_log_summary.py`). This record, written after that run, is a document and no gate
reads it.

- core build exit 0 with warnings as errors; `ctest -E plan_recalc` 65/65;
- `plan_recalc_tests` 512 checks with the corpus, 266 with `MPP_NO_CORPUS=1`;
- engine self-test 265 checks, 0 failures; `engine_diff.py --selftest` 53 checks; engine suite 8
  scenarios, 7 pass and the expected `constraint-mso` failure (1 of 247 fields);
- reader tests 1469 checks with the corpus, 789 without; M2 `recalc --compare` 332 of 332 fields;
- app build exit 0 and ctest 34/34, with and without `MPP_NO_CORPUS=1`;
- app_qt build exit 0 and ctest 52/52; no run reported "No tests were found";
- `check_fixtures.py`, `check_markers.py` (self-test 19 checks, worktree, `main..HEAD`) and
  `check_command_backlog.py` (114 pins, 114 rows) exit 0; `gen_calculate_props_pin.py --check` up
  to date; `scan_client_names.py` on the head and on the `main..HEAD` messages 0 hits;
  `guard_names.py main..HEAD` exit 0; the client-string grep finds none; 0 CR in the LF-pinned
  types; `git diff --check main..HEAD` exit 0.
- The gcc, clang and AppleClang jobs cannot run here. **CI is the only proof of W35f-1.10**
  (`app/main.mm`, the AppKit mirror): its gate is the `macos-appkit` job of `app-macos.yml`, which
  runs on push, so the merge check "CI green on three OSs" must include that run for `wave35f`.
  R35cW-CI1, R35cW-CI2 and R35dW-5 stay "fixed in source; CI is its gate" until the coordinator's
  run is green.
- Trailer `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35f)`: present on all 23 builder
  commits in `47665e1..135d253` (checked by script at CLOSE). The one commit without it,
  `d884b57`, is the coordinator's wave 35f brief.

### Ids, with the commit that closes each

Nothing here is called closed: only the coordinator's review closes an id (R35eG1-5).

| Group | Ids | Result |
|---|---|---|
| G0 | R35eG1-1 | fixed `fd457b4` |
| G0 | R35eG2-1 | fixed `08b83f9` |
| G0 | R35eG1-3 | fixed `1a2436e` |
| G0 | R35eG1-2, R35eG2-9, R35eG1-5 | fixed `fdff5d8` (records) |
| G1 | R35eG0-1, R35eG0-2, R35eG0-3 | fixed `9e37503`, `6d93f13`, `ae36884` |
| G1 | R35eG1-4 | fixed `d880529`, record `b3c19d3` |
| G1 | R35eG2-2, R35eG2-4, R35eG2-6 | fixed `e303559`, `5506509`, `4ffd8a7` |
| G1 | R35eW-1 | fixed `a0bf0c2`, with a coverage limit (one helper, exercised through the Manually Schedule case) |
| G1 | R35eW-3 | recorded `66c1555`: the Qt call site is defensive and unreached |
| G1 | R35eG2-3 | **fixed in source `26be753`; `macos-appkit` is its gate** |
| G1 | R35eG2-7, R35eG2-8 | fixed `76c8927`, `5ce1c73` |
| G1 | R35eG1-6, R35eG2-5 with R35eW-4, R35eW-2, R35eG2-10 | fixed `7f63a01`, `6a687db`, `5676398`, `336f197` (records, rewrap) |

Every id in the wave 35e open queue (`wave35e-verification/coord-gates-close.md`) appears in this
file. Nothing in this wave is deferred. Only R35eG2-3 waits on CI. Carried, not part of this wave's
queue: R35dG2-1 (Fill Down's guard has no test route), R35cG0-2 (the plan half) and R35cG0-4 (the
plan-side Props digest), both refused at the writer or the save; the open and unassigned R35bG0-9,
R35bG1-10 and R35bG2-9.

**Guards seen red:** per group in the G0 and G1 rows above; the logs are
`build/w35f/*-red-*.log` and `*-mut-*.log`. CLOSE adds no guard.

**Found, not fixed** (carried from the group rows; nothing new at CLOSE):
- Under a shown Sort a Level the builder would accept changes the display for the session while
  File > Save keeps the file's outline, as every session-only Level does (G0).
- Fill Down's Calculate guard has no test and no route to one; `apply_session_sort` has no
  queued-view guard (`wave35e-verification/gates-g2.md`).
- The AppKit mirror (1.10) has no test and was not compiled here.
- The plan half of R35cG0-2 and the plan-side Props digest of R35cG0-4.

**For the coordinator, after CLOSE:** push `wave35f` and dispatch `qt.yml` and `ci.yml`
(`macos-appkit` runs on push); mutations at each new guard; an independent review of
`main..wave35f`. 4(e) is a rerun only if a writer's output changed; this wave adds no writer.
Merging waits on CI green on all three OSs and no open Critical or High.

No writer added, no refusal that guards a save lifted, no pair measured, no COM, no VM or trace
touched, nothing pushed, no workflow dispatched, no history rewrite.

WAVE-35f-COMPLETE
