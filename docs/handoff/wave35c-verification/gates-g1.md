# Wave 35c, group G1: builder record

Branch `wave35c`. Eleven items, in order, one finding id per commit, each with the trailer
`Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35c)` (checked by `build/w35c/commit.sh`
before the commit and counted after: 1 each). No writer added, no refusal lifted, no pair
measured, no COM, no push, no workflow dispatch, no history rewrite.

Gate tooling: `build/w35c/gates.sh <label> [qt]` (Visual Studio 17 2022, Release,
`CORE_WARNINGS_AS_ERRORS=ON`; trees `build/w35b-close-fresh`, `-app`, `-qt`). Logs are
`build/w35c/<parent-sha>-W35c-<label>-*.log` and `*-gates.status` (git-ignored `build/`).
Scans each time: `check_fixtures.py`, `check_markers.py --range main..HEAD`, `guard_names.py`,
`scan_client_names.py`, and `git grep "client-string-[0-9a-f]{8}"` (none).

| Item | Finding | Commit | Status |
|---|---|---|---|
| 1.1 | R35bG3-1 | `c408a07` | fixed: one guard over the outline changes while a level is queued |
| 1.2 | R35bG4-1 | `aa1cef8` | fixed: a queued Show Level cannot come back on a Redo unseen |
| 1.3 | R35bW-1 | `e439dd2` | fixed: a grid edit after a queued Sort or Show Level is refused |
| 1.4 | R35bW-2 | `37e0e1f` | fixed: a replaced or cleared queued Sort cannot come back on a Redo |
| 1.5 | R35bG0-3 | `cfe2e8a` | fixed, with residual limits recorded (below) |
| 1.6 | R35bG0-4 | `7853816` | fixed (tests only) |
| 1.7 | R35bG0-5 | `b9588b7` | the FixedMeta and VarMeta part fixed; the tie part closed by 1.11 |
| 1.8 | R35bG1-1 | `eee629d` | fixed; the Release needle tests are a regression pin, not the detector |
| 1.9 | R35bG1-2 | `7666219` | fixed |
| 1.10 | R35bG1-3 | `4a2133e` | fixed for Sort and the removal; PARTLY fixed for Show Level (below) |
| 1.11 | R35bG2-1 (also R35bG3-2, the tie of R35bG0-5) | `4144385` | fixed, both layers |

## Gate lines

Each row is the run at the parent of the item's commit (the label names the item). All rows:
core build 0, core `ctest -E plan_recalc` 65/65, app build 0, app ctest 34/34, fixtures 0,
markers 0, guard_names 0, scan_client_names 0, client-string grep none, 0 compiler warnings.

| Item | Log prefix | app_qt (52 tests) |
|---|---|---|
| 1.1 | `b538646-W35c-1.1-*` | 52/52 |
| 1.2 | `c408a07-W35c-1.2-*` | 52/52 |
| 1.3 | `aa1cef8-W35c-1.3-*` | 52/52 |
| 1.4 | `e439dd2-W35c-1.4-*` | 52/52 |
| 1.5 | `37e0e1f-W35c-1.5-*` | not built (see below) |
| 1.6 | `cfe2e8a-W35c-1.6-*` | not built (test-only commit) |
| 1.7 | `7853816-W35c-1.7-*` | 52/52 |
| 1.8 | `b9588b7-W35c-1.8-*` | not built (core only) |
| 1.9 | `eee629d-W35c-1.9-*` | not built (core only; 1.10's run covers it) |
| 1.10 | `7666219-W35c-1.10-*` | 52/52 |
| 1.11 | `4a2133e-W35c-1.11-*` | 52/52 |

The 1.5, 1.6, 1.8 and 1.9 gates did not build `app_qt`. 1.5 added two defaulted fields to
`layout_model.h` (the qt tree includes it) and 1.8 and 1.9 changed core files the qt tree links;
the qt builds of 1.10 and 1.11 are cumulative over them and pass 52/52 with 0 warnings, so no
state of the branch was left without a qt build except the intermediate commits themselves.

## Red and green, per item

Every red is the new tests against the code without the fix (or with the guard switched off by
a non-constant false, because MSVC here treats constant-condition warnings as errors); the
files were restored after each run. Logs are in `build/w35c/` (git-ignored).

- 1.1: `1.1-red-run.log`: with the guard call non-constant false the row count, the summary
  state, the check box and the status needle fail (and the later Undo and toggle checks).
- 1.2: `1.2-red-run.log`: six checks fail (the Redo row count and status, the redo list left
  enabled after All Subtasks and after a refused level, and the stale queue).
- 1.3: `1.3-red-run.log`: 7 sort and 6 outline checks fail (the edits are accepted, the names
  change, no message).
- 1.4: `1.4-red-run.log`: four checks fail (Redo enabled with the stale sort after the clear
  and after a refused replacement, and the stale sort re-queued).
- 1.5: `1.5-red-run.log`: `mppwrite_roundtrip_calculate` accepts 3 cases and
  `app_edits_calculate` fails 8 checks without the two free-slack checks.
- 1.6: `1.6-red-run.log`: each of the four `reader_adapter` lines (assignment
  `baseline_stored`, deadline, active, task `baseline_stored`) deleted in turn makes
  `app_edits_calculate` fail with 2 failures.
- 1.7: `1.7-red-run.log`: 8 failures without the plan check.
- 1.8: see below; the red is the checked Debug build, not Release.
- 1.9: `1.9-red-run.log`: the writer wrote the file and the app built the edit (4 failures).
- 1.10: `1.10-red-run.log` (`red110.sh`, `mut110.py`): one layer's guards off at a time. App
  guards off: the three core tests pass and `app_edits_assignment`, `app_edits_sort` and
  `app_edits_show_level` fail. Writer guards off: the three core tests fail, `app_edits_assignment`
  and `app_edits_show_level` fail (their save-cascade tests pin the writer), and
  `app_edits_sort` passes, the plan's own pin catching it.
- 1.11: `1.11-red-run.log` (`red111.sh`, `mut111.py`): both plan guards off,
  `app_edits_calculate` fails 6 checks (for the tie and for the swapped order: the builder
  accepts, the reason is empty, `push()` accepts). Green: 665 checks, 0 failures.

## Notes the commit messages cannot carry

- **1.3.** The review's second route (rename, sheet Undo, Sort, Ctrl+Y) cannot reach the
  sheet's Redo in this shell: queueing the edit rebuilds the document and the rebuilt sheet
  carries no history, so Redo is disabled. No guard was added for an unreachable path; both
  tests pin it. Paste and Fill Down carry the guard but have no test of their own (Paste needs a
  clipboard block, Fill Down's ribbon control is disabled); both call the helper the tested Cut
  uses. The Sort path of 1.1's guard is the one 1.3 uses.
- **1.5.** Residual limits (also in `spec/writers/calculate.md` section 8): two edits that
  cancel pass (the file is then as the pair has it); a file whose free slack was never
  calculated is refused rather than saved; whether Project stores a critical bit outside the
  slack words is unmeasured.
- **1.7 and 1.11.** 1.7 closes R35bG0-5 for the FixedMeta entry and the VarMeta keys. The third
  gap, the plan accepting a tie, closes with 1.11, which also adds the second-leaf-ends-the-
  project refusal (R35bG3-2). The plan texts ("for two leaves that end on the same day", "is
  not the first leaf row (task uid N)") differ from the writer's, so a test tells the layers
  apart. The existing file-based tests (the control's plan with a tied or swapped file) pin the
  writer's full texts.
- **1.8.** `sorted_order_of` read `sorted[index]` after `sorted.clear()`. On Release `clear()`
  keeps the storage, so the old code printed the right UniqueID and the new needle tests pass
  against it as well: they are a regression pin, not the detector. The detector is the checked
  Debug build: `1.8-red-run.log` shows `mppwrite_roundtrip_viewstate_sort` on the old code not
  finishing in 60 s (attributed to the Debug assertion on the cleared vector; the dialog was
  not seen) and `1.8-green-run.log` shows it passing in 2.35 s with the fix.
- **1.9.** Row 0 of the resource table carries byte 12 = 0x5C in the measured control, so the
  loop starts at row 1 (and reads row 0 only when it is the target); an earlier attempt read it
  and turned the control red. Whether a real lineage reaches a file whose target row is 0x1C
  and another row 0x5C is not traced (`spec/writers/table-growth.md`); the guard refuses it
  either way.
- **1.10.** For Show Level the view state (the applied view, table, columns and the uid-2 view
  block) is in the file, not the plan, so the plan cannot be the guard. `make_show_level_edit`
  calls `mppwrite::require_measured_outline_view` at a call site of its own, and
  `save_view_outline` makes its checks itself, but both reach the same predicate
  (`require_applied_view`, `require_measured_view_block`): a change inside one removes both
  layers' guard. R35bG1-3 is therefore PARTLY fixed for 1.1 (the Show Level view state); `push()`
  has no file and cannot make the check, so the save re-runs the writer's. Sort (1.3) and the
  removal (1.6) have separate code in each layer.
- **Correction to the wave 35b record.** The line for `114861b` ("push() and the save cascade
  call the writer again") is wrong: `push()`'s `show_level` case does not call the writer; only
  the save cascade does. The commit message cannot be rewritten (no history rewrite), so the
  correction is recorded here and in the 1.10 message.

## Guards seen red

1.1, 1.2, 1.3, 1.4 (qt tests), 1.5 (writer and plan free-slack checks), 1.6 (the four reader
fields, through the reader), 1.7 (plan FixedMeta and VarMeta), 1.9 (every resource row), 1.10
(each layer's own guard for Sort, the removal and Show Level, one layer at a time), 1.11 (the
tie and the swapped order in the plan layer). 1.8: Debug build only (above).

## Deferred

Nothing.

## Found, not fixed

- **R35bG1-3 for Show Level's view state is partly fixed** (1.10): the two layers' guards share
  one predicate, and `push()` has no file to check, so it is the builder and the writer that
  guard it, not the plan.
- **1.5's residual limits** (above): a pair of edits that cancel passes, which is the file as
  the pair has it; a stored critical bit outside the slack words is unmeasured.
- **1.8's Release tests** do not detect the undefined behaviour; only a Debug or sanitizer
  build does; the Release gate cannot go red for it.
- **1.9:** whether a real lineage reaches a mixed 0x1C and 0x5C resource table is not traced.
- **1.3:** Paste and Fill Down have the guard but no test of their own (above).
- **The 1.5, 1.6, 1.8 and 1.9 gates did not build `app_qt`** at their own commit (above); the
  later gates cover it.
