# Wave 35f, group G1: builder record

Branch `wave35f`. One finding id per commit, each with the trailer
`Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35f)` (checked by `build/w35f/commit.sh`
before the commit and counted after). No writer added, no refusal that guards a save lifted, no
new Project pair measured, no COM, no VM or trace touched, no push, no workflow dispatch, no
history rewrite. `app/main.mm` cannot be compiled here; its change (1.10) is a mirror of the Qt
shell's, and CI job `macos-appkit` (`app-macos.yml`, `-Werror`, runs on push) is its gate.

Gate tooling as in `gates-g0.md`: `build/w35f/gates.sh <label> qt` for a commit that changes
source, `build/w35f/docgates.sh <label>` (the scans, the fixture check and `git diff --check`) for
a records-only commit. A log is named for the PARENT commit of the item, so item N's logs are
`build/w35f/<parent-sha>-W35f-<label>-*.log` and `*-gates.status`. The red and green runs are
`build/w35f/<label>-red-run.log` and `<label>-green-run.log` (git-ignored `build/`).

| Item | Finding | Commit | Status |
|---|---|---|---|
| 1.1 | R35eG0-1 | `9e37503` | fixed (Task Information skips an untouched field by the rendering it built) |
| 1.2 | R35eG0-2 | `6d93f13` | fixed (tests: the Task Form entry and the structural-trial partial apply) |
| 1.3 | R35eG0-3 | `ae36884` | fixed (a partly applied edit says what stuck) |
| 1.4 | R35eG1-4 | `d880529`, record `b3c19d3` | fixed (a cleared session sort keeps a Show Level on the redo list; `view-state.md` rows split) |
| 1.5 | R35eG2-2 | `e303559` | fixed (a pending document edit ends the structural redo list) |
| 1.6 | R35eG2-4 | `5506509` | fixed (Calculate's refusal beside a pending document edit gives advice that holds) |
| 1.7 | R35eG2-6 | `4ffd8a7` | fixed (test: a pending formula, then Calculate Project) |
| 1.8 | R35eW-1 | `a0bf0c2` | fixed, with a coverage limit (below) |
| 1.9 | R35eW-3 | `66c1555` | recorded: 2.1's Qt call site is defensive and unreached; no test (below) |
| 1.10 | R35eG2-3 | `26be753` | **fixed in source; `macos-appkit` is its gate** (AppKit mirror, not compiled here) |
| 1.11 | R35eG2-7 | `76c8927` | fixed (the fault seam gets the step its header said it had) |
| 1.12 | R35eG2-8 | `5ce1c73` | fixed (the scratch-probe seam gets a scope guard) |
| 1.13 | R35eG1-6 | `7f63a01` | fixed (records) |
| 1.14 | R35eG2-5, R35eW-4 | `6a687db` | fixed (records: `macos-appkit` named as `app/main.mm`'s gate) |
| 1.15 | R35eW-2 | `5676398` | fixed (record) |
| 1.16 | R35eG2-10 | `336f197` | fixed (rewrap; style only) |

## Gate line

Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; trees `build/w35b-close-fresh`,
`-app` and `-qt`, built incrementally.

- Source commits with the full set (1.1 to 1.8, 1.16: `79d24be-W35f-1.1-*`, `9e37503-W35f-1.2-*`,
  `6d93f13-W35f-1.3-*`, `ae36884-W35f-1.4-*`, `b3c19d3-W35f-1.5-*`, `e303559-W35f-1.6-*`,
  `5506509-W35f-1.7-*`, `4ffd8a7-W35f-1.8-*`, `5676398-W35f-1.16-*`): core build 0 / 0 warnings,
  `ctest -E plan_recalc` 65/65, app build 0 / 0 warnings, 34/34, app_qt build 0 / 0 warnings,
  52/52, fixtures 0, markers 0, guard_names 0, scan_client_names 0, client-string grep none.
- 1.11 and 1.12 (`26be753-W35f-1.11-*`, `76c8927-W35f-1.12-*`; the seams are in core and the
  tests in `tests/`, so no app_qt tree): core and app builds 0 / 0 warnings, 65/65 and 34/34, scans
  0.
- Records-only and `app/main.mm` commits (1.4's record, 1.9, 1.10, 1.13, 1.14, 1.15:
  `d880529-W35f-1.4b-*`, `a0bf0c2-W35f-1.9-*`, `66c1555-W35f-1.10-*`, `5ce1c73-W35f-1.13-*`,
  `7f63a01-W35f-1.14-*`, `6a687db-W35f-1.15-*`): fixtures 0, markers 0, guard_names 0,
  scan_client_names 0, client-string grep none, `git diff --check` 0. `66c1555-W35f-1.10-*` also
  records "SOURCE CHANGED": `app/main.mm` is in none of the three trees, so the build and test
  numbers of the tree gated in 1.8 stand for 1.10.
- The group's final tree (`G1-final`, run after the records commit): the full set again.

The measured pairs still write and match with effects on (the save-and-compare blocks of
`qt_sort_persist`, `qt_outline_persist` and `qt_calculate_gate` and the app and core calculate
pair tests are inside the suites above).

## Guards seen red

Each with the parent's source file or a temporary mutation, restored afterwards (backups in
`build/w35f/*.new*`) before the green run. The counts below are the ones in each commit message;
the raw logs also list the failing tests after the FAIL lines.

- 1.1: `1.1-red-run.log`, 1 FAIL ("R35eG0-1: the untouched Duration keeps the sheet's value"),
  `main_window.cpp` at the parent.
- 1.2: `1.2-red-run.log`, 2 FAILs ("the dropped trial queues no percent edit", "the status names
  the task form's Duration failure"), the apply mutated to commit the trial on a failed apply.
- 1.3: `1.3-red-run.log`, 1 FAIL ("partial apply: the status names the field that stuck and
  Undo"), `main_window.cpp` at the parent.
- 1.4: `1.4-red-run.log`, 4 FAILs (the Level stays on the redo list, and Ctrl+Y re-queues it, on
  each of the two cleared-sort paths). Redundancy: the clear in `apply_sort_choice` removed alone
  stays green (`1.4-mut-a-run.log`), so it is defence in depth; the clear in `apply_session_sort`
  removed alone is 2 FAILs in `qt_sort_persist` (`1.4-mut-b-run.log`), so it holds the rule.
- 1.5: `1.5-red-run.log`, 6 FAILs (the Redo-disabled and re-queues-nothing checks on each of the
  three surfaces).
- 1.6: `1.6-red-run.log`, 1 FAIL ("Calculate Project beside a pending edit: the advice names the
  formula case and the reopen").
- 1.7: `1.7-red-run.log`, 2 FAILs ("Calculate Project beside a pending formula edit queues
  nothing" and "... the status line names it"), the guard narrowed to the date-format half.
- 1.8: three reds, one per test (`1.8-red-tm-run.log`, `1.8-red-sp-run.log`,
  `1.8-red-op-run.log`), 4 FAILs in each (for example "the Table / Date Format dialog does not
  open beside a queued mode edit"), `main_window.cpp` at the parent.
- 1.11: two reds. (a) `1.11-red-a-run.log`, `calculate.cpp` at the parent: 2 FAILs ("fault after
  the re-read: the seam fires after that append" and "... target unchanged"). (b)
  `1.11-red-b-run.log`, the seam present and `work.save(scratch)` mutated to `work.save(path)`:
  1 FAIL ("fault after the re-read: target unchanged"); cases 1 to 6 stay green, which was the
  gap.
- 1.12: `1.12-red-run.log`, 4 FAILs (the new block's two checks and the two a leaked seam then
  causes in a later block), the guard's destructor leaving the seam armed.

1.9 and 1.13 to 1.15 are records and have no red. 1.10 has none either: nothing here compiles or
tests `app/main.mm`. 1.16 is a rewrap, so no red is possible; its gate is that every suite still
builds and passes.

## Items worth reading

**1.8 (R35eW-1), a coverage limit.** The other exclusive kinds now refuse a table / date format or
custom-field formula edit before it can be queued beside them, and the reverse (the document edit
refuses the other kinds' commands). The tests exercise the refusal at the shared helper through
Manually Schedule in both orders (`task_mode_document_edit_gate_test.cpp`), and Sort and Show
Level in both orders (`sort_persist_test.cpp`, `outline_persist_test.cpp`). The resource
information, add resources, assign resources, Font Bold and Define WBS Code call sites are
covered only by the shared helper's test, not one case each. The commit message says so.

*Erratum, W35g-1.8 (R35fG1-3):* that limit is lifted, and the one it understated is corrected.
The wave 35g tests drive each of Add Resources, Assign Resources, Font Bold and Define WBS Code
beside a pending date format (`qt_task_mode_document_edit_gate`), Resource Information the same
way (`qt_resource_information_gate`), and Custom Field Formula beside a queued Manually Schedule
(`qt_task_mode_document_edit_gate`) and beside a queued Sort (`qt_sort_persist`). The widened
Custom Fields refusal at the Formula... button was tested by W35g-0.1. Before that the tests
drove only Table / Date Format beside a non-Calculate kind, so reverting the Custom Field Formula
or Custom Fields refusal to the Calculate-only check would have left every test green.

**1.9 (R35eW-3), a branch left untested.** At the Qt call site the Sort dialog maps a Duration key
to `SortKeyKind::other`, which `make_sort_edit` refuses; for Name the display and writer ids agree;
and every route that changes the shown sort while a Sort is on the redo list clears that list, so
the branch that sets the key never runs on a Redo. Reverting the call site would fail no test. The
review's seam test (a hand-built Duration sort_view) needs a mutable queue accessor on the window,
a new way round `push()`; not added. The 2.1 row in `wave35e-complete.md` now says the call site
is defensive and unreached. If a later route reaches it, a test is due then.

**1.10 (R35eG2-3), the AppKit mirror.** `app/main.mm` gets `document_edit_refusal` and
`pending_document_edit_refusal` beside `calculate_queued_refusal`: Table Date Format and Custom
Field Formula refuse while any exclusive edit is queued; Calculate (with the 1.6 wording), Add
Resources, Resource Information, Assign Resources, Font Bold and Define WBS Code refuse while a
document edit is pending; both document edits end the structural redo list once queued (1.5). The
shell has no Sort, Show Level or Manually Schedule command, so those have nothing to mirror. Not
compiled, not run, not tested here. Fixed in source; CI on `macos-appkit` is its gate, and the
coordinator's review decides whether it is closed. `spec/writers/calculate.md` records it.

**1.11 and 1.12 (R35eG2-7, R35eG2-8), seams.** The fault seam now has step `size(kAppends) + 2`,
after the scratch save and the re-read compare, before the target is written; the loop in `roundtrip_calculate.cpp` runs
{1, 3, 5, 6, 7}. The scratch-probe seam has an RAII reset in both blocks and a guard test that
arms it, throws, then saves successfully. Neither is production surface: both are set only by
tests.

**1.16 (R35eG2-10).** The finding's line numbers had drifted. Every line over 100 columns in the
five files it names was rewrapped (`calculate.h` had none left), 18 lines in all. Lines over 100
columns in other files were not touched.

## Found, not fixed

- Nothing new. The Fill Down guard remains unreachable (R35dG2-1, wave 35e 2.5), and
  `apply_session_sort` has no queued-view guard (2.3), as recorded in
  `wave35e-verification/gates-g2.md`.
