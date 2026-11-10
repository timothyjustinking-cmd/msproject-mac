# Wave 35e, group G2: builder record

Branch `wave35e`. One finding id per commit, each with the trailer
`Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35e)` (checked by `build/w35e/commit.sh`
before the commit and counted after). No writer added, no refusal of a persisted shape lifted, no
pair measured, no COM, no push, no workflow dispatch, no history rewrite. `app/main.mm` cannot be
compiled here; its one change (2.7) is a refusal that mirrors the Qt shell's, and CI is its gate.

Gate tooling as in `gates-g0.md`: `build/w35e/gates.sh <label> qt` for a commit that changes
source, `build/w35e/docgates.sh <label>` (the scans and the fixture check) for a records-only
commit. Logs are `build/w35e/<parent-sha>-W35e-<label>-*.log` and `*-gates.status`. The red and
green runs are `build/w35e/<label>-red-run.log` and `<label>-green-run.log`.

| Item | Finding | Commit | Status |
|---|---|---|---|
| 2.1 | R35dG0-3 | `086e111` | fixed (the queued-Sort re-apply maps the writer's field id to the display field) |
| 2.2 | R35dG0-4 | `5f9a5e7` | fixed (a direct `apply_session_sort` after Undo; the "rows show" check dropped, with its reason) |
| 2.3 | R35dG1-5 | `4bc2815` | fixed (a sorted display under a queued Level, then a clear; reached through `apply_session_sort`) |
| 2.4 | R35dG1-6 | `87afb79` | fixed (the Sort branch of `view_change_refusal` seen red; "clearing a session sort" written into the rule) |
| 2.5 | R35dG2-1 | `ae4782a` | **partly fixed**: Task Information, Cut and Paste are tested; Fill Down's guard stays untested (not reachable) |
| 2.6 | R35dG2-2 | `c2880d7` | fixed (both orders of Table / Date Format and Custom Field Formula against a queued Calculate) |
| 2.7 | R35dW-5 | `e03da3e` | fixed in source; CI is its gate (AppKit refusal, not compiled here) |
| 2.8 | R35dG0-5 | `da5f81d` | fixed (the two `check_props_pin` tail branches, each with its own needle) |
| 2.9 | R35dG1-4 | `aa06507` | fixed (the fault seam fires after the Props edit; scope-guard reset) |
| 2.10 | R35dG2-3 | `d697a05` | fixed (the unmarked tombstone case asserts the builder's exact outcome) |
| 2.11 | R35dG2-4 | `6fa4c19` | fixed (the scratch-probe seam takes a file type; a not_found probe with an error code is tested) |
| 2.12 | R35dG2-5 | `9f0cb92` | fixed (record) |
| 2.13 | R35dW-6 | `3946d9a` | fixed (records) |

## Gate line

Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`.

- Source rows 2.1 to 2.6, 2.8 to 2.11 (`07c4ef6-W35e-2.1-*`, `086e111-W35e-2.2-*`,
  `5f9a5e7-W35e-2.3-*`, `4bc2815-W35e-2.4-*`, `87afb79-W35e-2.5-*`, `ae4782a-W35e-2.6-*`,
  `e03da3e-W35e-2.8-*`, `da5f81d-W35e-2.9-*`, `aa06507-W35e-2.10-*`, `d697a05-W35e-2.11-*`): core
  build 0 / 0 warnings, `ctest -E plan_recalc` 65/65, app build 0 / 0 warnings, 34/34, app_qt
  build 0 / 0 warnings, 52/52, fixtures 0, markers 0, guard_names 0, scan_client_names 0,
  client-string grep none. In 2.9 the first app run had one "Process not started" on
  `app_edits_task_mode` (a launch failure under `-j4`, not a test result); the rerun was 34/34,
  recorded in that row's `gates.status`.
- 2.7 (`c2880d7-W35e-2.7-*`): scans, fixtures and `git diff --check` only; `app/main.mm` is in none
  of the three trees (the trees were gated in 2.6).
- 2.12, 2.13 (records only, `6fa4c19-W35e-2.12-*`, `9f0cb92-W35e-2.13-*`): fixtures 0, markers 0,
  guard_names 0, scan_client_names 0, client-string grep none, `git diff --check` 0.
- The group's final tree (`3946d9a-W35e-G2-final-*`): the full set again, same numbers (65/65,
  34/34, 52/52, 0 warnings, scans 0).

The measured pairs still write and match with effects on (the save-and-compare blocks of
`qt_sort_persist`, `qt_outline_persist` and `qt_calculate_gate` and the app and core calculate pair
tests are inside the suites above).

## Guards seen red

Each by a temporary mutation, restored from a copy (`git diff` showed the test only) before the
green run.

- 2.1: `queued_sort_display_key` made to return the writer's id: 1 FAIL, "sort-duration-desc: the
  queued sort's display key is the display field: got 188744967 want 188743709" (`2.1-red-run.log`).
- 2.2: the re-apply's clear removed: 2 FAILs, "Undo-first: apply_session_sort ends the redo list
  (Redo is disabled)" and "Undo-first: Redo after apply_session_sort queues nothing". The first
  attempt, an `if (false && ...)`, did not build (C4127) and was replaced.
- 2.3: `PlanDocument::clear_sort` made to `expand_all`: 3 FAILs (the sorted display keeps the level's rows, clearing keeps them, and the file's order shows again).
- 2.4: the sort_view branch of `view_change_refusal` removed: 19 FAILs in `qt_sort_persist`.
- 2.5: the three guards changed to `refuse_view_change`, as the finding describes: 5 FAILs in `qt_calculate_gate` (the task form edit and the cut not refused with the Calculate named, among them).
- 2.6: the two new guards and the `calculate_project` guard made inert: 5 FAILs. The first run also
  inerted an older `has_pending_edits()` check, because the sed pattern was too broad; the source
  was restored from a copy, only the intended lines were mutated and the clean log is the one
  quoted in the commit.
- 2.8: each `check_props_pin` tail branch made inert with `&& props.empty()`: 2 FAILs, each falling
  back to the generic "114/Props tail differs" text.
- 2.9: `save_props_edit` made to write the target instead of the scratch path: 1 FAIL, "fault after
  the Props edit: target unchanged"; the appends 1, 3 and 5 stay green.
- 2.10: `make_assignment_edit` made to refuse (another text) when the plan's
  `skipped_resource_sessions_unsaved` is set: the two new checks FAIL, and the old absence check
  does not (the gap). The mutation also caught other cases, so the log has many FAILs.
- 2.11: the refusal condition made to refuse under the seam whatever the file type: 2 FAILs, "the
  save proceeds" and "the target is written". A broader first mutation (`if (ignored)`) ended the
  test on an unhandled exception in an ordinary save and is kept as
  `2.11-red-run-broad-mutation.log`.

2.7 has no red (nothing compiles or tests `app/main.mm` here). 2.12 and 2.13 are records.

## Items worth reading

**2.2 (R35dG0-4), a check dropped.** The finding asked for a "rows show the queued order" check
after the re-apply. No route reaches the re-apply with a different display: every sort path ends the
redo list first, so Redo cannot re-queue a Sort over a changed display. The check could not fail
for the guarded cause, so it was removed, with the reason in the test's comment. The key mapping is
tested at the helper level in 2.1.

**2.3 (R35dG1-5), a seam, not a route.** Both routes the finding named were closed by W35e-1.1, so
a sorted display under a queued Level is built through `apply_session_sort`, which has no
queued-view guard. It is a test seam; no production route reaches it.

**2.5 (R35dG2-1), partly fixed.** `fill_down()` is private and its controls are disabled (W31-0.4),
so no test reaches its Calculate guard. Task Information, Cut and Copy+Paste are tested, each
refused with the Calculate named. Recorded as partly fixed, not fixed.

**2.6 (R35dG2-2).** The new guard is Calculate-only: a queued Sort or Show Level is not newly
refused on those two dialogs. The dialog tests close any modal dialog that opens with a timer, so a
missing refusal fails and does not hang `exec()`.

**2.7 (R35dW-5).** `calculate_queued_refusal(plan, change)` in `app/main.mm`, the same text as
`MainWindow::data_change_refusal`'s Calculate branch, called at the top of `commit_open_edit` and
in `paste_from_pasteboard`. The AppKit Cut is a disabled stub and it has no Fill or task form, so
those are the shell's two cell writers. Not compiled, not run, not tested.

**2.11 (R35dG2-4).** The probe of a missing scratch path sets an error code on this MSVC build, so
the `type != not_found` half of the condition is load-bearing on Windows, not only in theory.

**2.13 (R35dW-6).** `docs/START-HERE.md`'s header already read wave 35e (fixed since the finding),
so only `wave35d-complete.md` and `status-2026-11-10.md` changed; START-HERE and
`plan-waves27-30.md` already said "fixed" for R35cW-3.

## Found, not fixed

- Fill Down's Calculate guard has no test and no route to one (2.5).
- The AppKit refusal (2.7) has no test; CI on `macos-appkit` (`app-macos.yml`, `-Werror`, runs on
  push), and on `macos-qt`, which builds `app/` too, is its only gate (corrected by W35f-1.14,
  R35eG2-5).
- The `apply_session_sort` entry point has no queued-view guard (2.3). It is a seam the shell's
  menu does not reach, so nothing is wrong today; a later caller would need to add the guard.
