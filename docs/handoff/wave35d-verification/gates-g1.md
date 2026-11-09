# Wave 35d, group G1: builder record

Branch `wave35d`. Six items, in order, one finding id per commit, each with the trailer
`Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35d)` (checked by `build/w35d/commit.sh`
before the commit and counted after). No writer added, no refusal lifted, no pair measured, no
COM, no push, no workflow dispatch, no history rewrite.

Gate tooling: `build/w35d/gates.sh <label> [qt]` for a source change and
`build/w35d/docgates.sh <label>` for a records-only commit (Visual Studio 17 2022, Release,
`CORE_WARNINGS_AS_ERRORS=ON`; trees `build/w35b-close-fresh`, `-app` and `-qt`). Logs are
`build/w35d/<parent-sha>-W35d-<label>-*.log` and `*-gates.status` (git-ignored `build/`).
Scans each time: `check_fixtures.py`, `check_markers.py --range main..HEAD`, `guard_names.py`,
`scan_client_names.py`, and `git grep "client-string-[0-9a-f]{8}"` (none).

| Item | Finding | Commit | Status |
|---|---|---|---|
| 1.1 | R35cG1-3 | `e967858` | fixed: Paste's guard has a test in both persist tests; mutation Q10 turns it red |
| 1.2 | R35cG1-4 | `e2eca38` | fixed: one rule under both kinds, recorded in `view-state.md`, every kept refusal tested |
| 1.3 | R35cG2-1 | `41f7a2c` | fixed: a test-only fault seam reaches the `save_calculate` scratch chain again |
| 1.4 | R35cG0-3 | `6552f11` | fixed as a record and a refusal text; no refusal lifted |
| 1.5 | R35cW-2 | `454521d` | fixed (records): four ids now read "partly fixed", each with its remaining R35c id |
| 1.6 | R35cW-1, R35cG2-8 | `4410935` | fixed (records): the open queue is in START-HERE, `coordinator.md` and `plan-waves27-30.md`, Highs first |

## Gate lines

- 1.1 (`86dbee9-W35d-1.1-*`): core `-E plan_recalc` 65/65, app 34/34, app_qt 52/52, 0 warnings in
  all three builds, fixtures 0, markers 0, guard_names 0, scan_client_names 0, client-string grep
  none.
- 1.2 (`e967858-W35d-1.2-*`): core 65/65, app 34/34, app_qt 51/52 on the first run
  (`qt_resource_add_gate` failed once in 0.65 s with no output) and 52/52 on the rerun
  (`e967858-W35d-1.2-qt-ctest-rerun.log`); the test passes alone and the commit touches nothing it
  reads. 0 warnings, scans 0.
- 1.3 (`e2eca38-W35d-1.3-*`): core 65/65, app 34/34, app_qt 52/52, 0 warnings, scans 0.
- 1.4 (`41f7a2c-W35d-1.4-*`): core 65/65, app 34/34, app_qt 52/52, 0 warnings, scans 0.
- 1.5 (`6552f11-W35d-1.5-*`) and 1.6 (`454521d-W35d-1.6-*`): records only, so no build:
  fixtures 0, markers 0, guard_names 0, scan_client_names 0, client-string grep none,
  `git diff --check` clean, 0 CRs.

The last source change is 1.4 (`calculate.cpp` and `tests/roundtrip_calculate.cpp`). The measured
pairs still write and match with effects on in every row (`mppwrite_roundtrip_calculate`
`unlinked_pair`, `app_edits_calculate`, and the `qt_sort_persist` and `qt_outline_persist`
save-and-compare blocks, all inside the suites above).

## 1.1 (R35cG1-3)

Both persist tests copy a cell and paste it after a queued Sort or Show Level, through the
window's own Copy and Paste, and assert the refusal in the status line and that no row changed.
Red (`1.1-red-run.log`): the `refuse_view_change("a pasted cell")` call removed, the mutation Q10;
both new cases FAIL on "the status line names the queued Sort" and "... the queued level".

## 1.2 (R35cG1-4)

Rule: a session Level or Sort chosen while the other kind is queued is refused, with the shared
wording; the Sort branch's outline refusals stay, and clearing a sort under a Level is allowed.
Recorded in `spec/writers/view-state.md` ("W35d-1.2"). Tests: `qt_sort_persist` has the outline
gate (Collapse All, the row toggle, the summary toggle, a session Level and All Subtasks, each with
the Sort still queued, no row changed and the Sort named; clearing the Sort accepted);
`qt_outline_persist` has a session Sort under a queued Level (refused, level and rows unchanged)
and the clear. Expand All under a queued Sort is disabled, not refused, because the rebuild
expands every row; the test asserts the action is disabled and the spec says so.
Red (`1.2-red-run.log`): both new call sites removed; five checks FAIL, among them "a session Show
Level: no row changes", "All Subtasks: the status line names the queued Sort", "a session Sort
after a queued level: the status line names the level" and "the refused Sort reorders no row".

## 1.3 (R35cG2-1)

`set_calculate_fault_after_appends` (`core/mppwrite/calculate.h`, test-only) throws after the Nth
overtime append. `tests/roundtrip_calculate.cpp` runs it after appends 1, 3 and 5 and asserts the
existing target is byte-identical and no scratch file is left; the squat-test comment at the old
line 1163 is corrected, and the two records that called R35bG2-2 closed say "partly fixed".
Red: the five `save_var_record` calls writing `path` instead of the scratch, "fault after append
1/3/5: target unchanged" FAIL (`1.3-red-run.log`); the catch block's scratch cleanup removed, "no
scratch file left" FAILs for all three (`1.3-red-scratch-run.log`). The scratch file the second
mutation left was removed.

## 1.4 (R35cG0-3)

The payload refusal now reads "... (an option, a default or a per-file value that no measured
Calculate Project pair varies; Calculate Project is enabled for the measured control's values
only)". `spec/writers/calculate.md` states the scope and names the port saves now refused (Define
WBS Code, a custom-field set, Add Resources, Assign Resources and its removal). A flipped title
byte is refused with that text. Red (`1.4-red-run.log`): the old text FAILs "the refusal names a
per-file value and the control-only scope" and "the refusal does not call the record a
calculation option".

## 1.5 (R35cW-2)

`wave35c-complete.md` (group rows and the CLOSE id table) and `wave35c-verification/gates-g1.md`:
R35bG3-1 (remainder R35cG1-4), R35bW-1 (R35cG1-2 and R35cG1-3), R35bW-2 (R35cG1-1) and R35bG2-2
(R35cG2-1) are "partly fixed", each naming its remaining id and the wave 35d commit that closed it.

## 1.6 (R35cW-1, with R35cG2-8)

START-HERE, `memory/coordinator.md` and `plan-waves27-30.md` carry the wave 35c queue after G1,
Highs first, and the wave 35b "Also open" paragraphs are cut to what 35c left open (R35bG0-9,
R35bG1-10, R35bG2-9, R35bW-3). `gates-g2.md` and `wave35c-complete.md` now give 2.6 one status,
"partly fixed".

## Guards seen red

- 1.1: Paste's guard removed (Q10), both persist tests.
- 1.2: the two cross-kind guards removed.
- 1.3: the atomic-write order and the scratch cleanup, each in turn.
- 1.4: the old refusal text, two checks.
- 1.5 and 1.6: none; records.

## Deferred

Nothing.

## Found, not fixed

- `qt_resource_add_gate` failed once in 0.65 s at `-j2` with no output, and passed alone and in
  the later full runs (1.3, 1.4). Not reproduced; watch.
- The coordinator's decision from G0 stands: whether Redo ends the list for a Show Level as it
  does for a Sort.
- 1.4 names the port saves now refused from the Props records and scalars each writer touches; it
  did not re-save each through Project (no COM this wave).
