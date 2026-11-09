# Wave 35e: completion record

## G0: the High

Builder record: `wave35e-verification/gates-g0.md`. Commit: `b8ee455` (0.1). The trailer is on it,
checked by script.

| Item | Finding | Status |
|---|---|---|
| 0.1 | R35dG0-1 (High), with R35dW-1 | fixed `b8ee455`; a partly applied Task Information or Task Form edit ends the structural redo list; tested in `qt_sort_persist`, `qt_outline_persist` and `qt_calculate_gate`; the sweep of the other cell-commit paths found none (Cut, Paste and Fill Down are one transaction) |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`;
`43fee2a-W35e-0.1-*`): core build 0 with 0 warnings; `ctest -E plan_recalc` 65/65; app 34/34;
app_qt 52/52; `check_fixtures` 0; `check_markers --range main..HEAD` 0; `guard_names` 0;
`scan_client_names` 0; client-string grep none. The measured pairs still write and match with
effects on.

**Guards seen red:**
- 0.1: the `if (!ok && applied)` clear off, 9 FAILs, three in each of the three tests
  (`build/w35e/0.1-red-run.log`).

**Deferred:** nothing.

**Found, not fixed:**
- Nothing new. A partly applied edit still leaves its committed fields in place (no rollback), by
  the brief's fix; the status line names the failing field and Undo takes the committed cells back.

No writer added, no refusal lifted, no pair measured, no COM, nothing pushed, no workflow
dispatched.

WAVE-35e-G0-COMPLETE

## G1: the Mediums

Builder record: `wave35e-verification/gates-g1.md`. Commits: `39c8e37` (1.1), `1da3997` (1.2),
`02f1900` (1.3), `f244d02` (1.4). The trailer is on each, checked by script.

| Item | Finding | Status |
|---|---|---|
| 1.1 | R35dG1-1 | fixed `39c8e37`; (a) a Level 1 or deeper is refused while a session Sort is shown, (b) a sort choice ends a Show Level on the redo list; both tested in `qt_outline_persist` on the six-task outline |
| 1.2 | R35dG1-2, with R35dW-4 | fixed `1da3997`; `calculate.md`'s Scope paragraph corrected (the Notes and Text1 removals, the percent-complete and indent and outdent saves, and the pin as the last check) |
| 1.3 | R35dG1-3, R35dW-2, R35dW-3 | fixed `02f1900`; the open queue is in START-HERE, `memory/coordinator.md` and `plan-waves27-30.md` from `wave35d-verification/coord-gates-close.md`; R35cG1-2, R35cG1-4, R35cW-1, R35cG0-3 and R35cW-4 read "partly fixed"; R35cW-CI1 and R35cW-CI2 read "fixed in source; CI is its gate"; the Show Level sentence is corrected |
| 1.4 | R35dG0-2 | fixed `f244d02`; 0.5's record reads "partly fixed: Task Information partial apply", closed by W35e-0.1 `b8ee455` |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; 1.1
`5d733ae-W35e-1.1-*`, the group's final tree `f244d02-W35e-G1-record-*`): core build 0 with 0
warnings; `ctest -E plan_recalc` 65/65; app 34/34; app_qt 52/52; `check_fixtures` 0;
`check_markers --range main..HEAD` 0; `guard_names` 0; `scan_client_names` 0; client-string grep
none. 1.2 to 1.4 are records only (scans, fixtures and `git diff --check`, 0). The measured pairs
still write and match with effects on.

**Guards seen red:**
- 1.1: the shown-Sort Level refusal and the Show Level redo clear off together, 6 FAILs in
  `qt_outline_persist` (`build/w35e/1.1-red-run.log`); not run one at a time, each needle belongs
  to one guard.

**Deferred:** nothing.

**Found, not fixed:**
- A session-only Level chosen while a Sort sits on the redo list leaves that Sort there; Ctrl+Y
  would re-queue it over the session Level. Not reproduced and not in the brief (`gates-g1.md`).

No writer added, no refusal of a persisted shape lifted, no pair measured, no COM, nothing pushed,
no workflow dispatched.

WAVE-35e-G1-COMPLETE

## G2: the Lows

Builder record: `wave35e-verification/gates-g2.md`. Commits: `086e111` (2.1), `5f9a5e7` (2.2),
`4bc2815` (2.3), `87afb79` (2.4), `ae4782a` (2.5), `c2880d7` (2.6), `e03da3e` (2.7), `da5f81d`
(2.8), `aa06507` (2.9), `d697a05` (2.10), `6fa4c19` (2.11), `9f0cb92` (2.12), `3946d9a` (2.13).
The trailer is on each, checked by script.

| Item | Finding | Status |
|---|---|---|
| 2.1 | R35dG0-3 | fixed `086e111`; the queued-Sort re-apply maps the writer's field id to the display field (`app::queued_sort_display_key`, tested for Name and Duration) |
| 2.2 | R35dG0-4 | fixed `5f9a5e7`; a direct `apply_session_sort` after Undo ends the redo list; the "rows show" check is dropped because no route reaches the re-apply with a different display (reason in the test) |
| 2.3 | R35dG1-5 | fixed `4bc2815`; a sorted display under a queued Level, then a clear, keeps the level and shows the file's order; reached through `apply_session_sort` (a seam, not a production route) |
| 2.4 | R35dG1-6 | fixed `87afb79`; the sort_view branch of `view_change_refusal` seen red; "clearing a session sort" is written into the 35d-1.2 rule as its exception |
| 2.5 | R35dG2-1 | **partly fixed** `ae4782a`: Task Information, Cut and Paste are tested; Fill Down's guard stays untested (`fill_down()` is private and its controls are disabled, W31-0.4) |
| 2.6 | R35dG2-2 | fixed `c2880d7`; Table / Date Format and Custom Field Formula are refused beside a queued Calculate, and Calculate beside a pending edit of either, both orders tested |
| 2.7 | R35dW-5 | fixed in source; CI is its gate `e03da3e`: the AppKit shell refuses a cell edit and a paste beside a queued Calculate (a refusal mirroring the Qt shell's); `app/main.mm` is not compiled or tested here |
| 2.8 | R35dG0-5 | fixed `da5f81d`; the two `check_props_pin` tail branches each have a test with their own needle |
| 2.9 | R35dG1-4 | fixed `aa06507`; the fault seam also fires after the Props edit, the target is byte-identical, the seam is reset by a scope guard |
| 2.10 | R35dG2-3 | fixed `d697a05`; the unmarked tombstone case asserts the builder accepts and the edit is the removal asked for |
| 2.11 | R35dG2-4 | fixed `6fa4c19`; the scratch-probe seam takes a file type and a not_found probe with an error code proceeds |
| 2.12 | R35dG2-5 | fixed `9f0cb92` (record) |
| 2.13 | R35dW-6 | fixed `3946d9a` (records); R35cW-3 is "fixed `6f5cc18` (record)", R35cG0-2 stays partly fixed |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; the group's final tree
`3946d9a-W35e-G2-final-*`): core build 0 with 0 warnings; `ctest -E plan_recalc` 65/65; app 34/34;
app_qt 52/52; `check_fixtures` 0; `check_markers --range main..HEAD` 0; `guard_names` 0;
`scan_client_names` 0; client-string grep none. 2.7, 2.12 and 2.13 ran the scans without a build.
The measured pairs still write and match with effects on.

**Guards seen red:**
- 2.1: the writer's id returned as the display key, 1 FAIL (`build/w35e/2.1-red-run.log`).
- 2.2: the re-apply's clear removed, 2 FAILs (`2.2-red-run.log`).
- 2.3: `clear_sort` made to `expand_all`, 3 FAILs (`2.3-red-run.log`).
- 2.4: the sort_view branch of `view_change_refusal` removed, 19 FAILs in `qt_sort_persist`
  (`2.4-red-run.log`).
- 2.5: the three guards changed to `refuse_view_change`, 5 FAILs (`2.5-red-run.log`).
- 2.6: the three guards made inert, 5 FAILs (`2.6-red-run.log`).
- 2.8: each tail branch made inert, 2 FAILs (`2.8-red-run.log`).
- 2.9: `save_props_edit` made to write the target, 1 FAIL (`2.9-red-run.log`).
- 2.10: the builder made to refuse another text, the two new checks FAIL and the old one does not
  (`2.10-red-run.log`).
- 2.11: the refusal made to bite under the seam whatever the file type, 2 FAILs
  (`2.11-red-run.log`).
- 2.7 has none: nothing compiles or tests `app/main.mm` here. 2.12 and 2.13 are records.

**Deferred:** nothing.

**Found, not fixed:**
- Fill Down's Calculate guard has no test and no route to one (2.5).
- The AppKit refusal (2.7) has no test; CI on `macos-qt` is its only gate.
- `apply_session_sort` has no queued-view guard (2.3). The shell's menu does not reach it, so
  nothing is wrong today.

No writer added, no refusal of a persisted shape lifted, no pair measured, no COM, nothing pushed,
no workflow dispatched.

WAVE-35e-G2-COMPLETE
