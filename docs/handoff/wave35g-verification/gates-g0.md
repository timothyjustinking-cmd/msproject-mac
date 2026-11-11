# Wave 35g, group G0: builder record

Branch `wave35g`. One finding id per commit, each with the trailer
`Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35g)` (checked by `build/w35g/commit.sh`
before the commit and counted after). No writer added, no refusal that guards a save lifted, no
pair measured, no COM, no VM or trace touched, no push, no workflow dispatch, no history rewrite.

Gate tooling: `build/w35g/gates.sh <label> qt` (a copy of wave 35f's with `35g` for `35f`; core,
app and app_qt builds and ctest, `check_fixtures.py`, `check_markers.py --range main..HEAD`,
`guard_names.py`, `scan_client_names.py` and the client-string grep). Visual Studio 17 2022,
Release, `CORE_WARNINGS_AS_ERRORS=ON`; trees `build/w35b-close-fresh`, `-app` and `-qt`, built
incrementally. A log is named for the PARENT commit of the item: `build/w35g/<parent-sha>-W35g-<label>-*.log`
and `*-gates.status` (git-ignored `build/`). The red and green runs are
`build/w35g/<label>-red-run.log` and `<label>-green-run.log`.

| Item | Finding | Commit | Status |
|---|---|---|---|
| 0.1 | R35fG1-1 | `284c3cb` | fixed (Custom Fields... opens beside a queued exclusive edit; Formula... is refused) |
| 0.2 | R35fG0-1 | `24a5c1d` | fixed (record corrected, five-task test added) |

## Gate line

- 0.1 (`1735250-W35g-0.1-*`): core build 0 warnings, core ctest `-E plan_recalc` 65/65, app build
  0 warnings, app 34/34, app_qt build 0 warnings, app_qt 52/52, fixtures 0, guard_names 0,
  scan_client_names 0, client-string grep none, `check_markers` 1 (see "Found, not fixed").
- 0.2 (`284c3cb-W35g-0.2-*`): the same numbers (65/65, 34/34, 52/52, 0 warnings, scans 0, markers
  1 as above).
- No out-of-memory build error occurred in either run.

## 0.1 (R35fG1-1)

The `refuse_document_edit` call that W35f-1.8 put before the dialog stopped Project > Custom
Fields... opening beside all ten exclusive kinds, although the dialog queues nothing unless a
formula is accepted. Now:
- only a queued Calculate Project refuses before the dialog (W35f-0.2's guard and its
  `qt_calculate_gate` case are unchanged, and green);
- beside any other exclusive edit the dialog opens, and the `set_formula_editor` callback refuses
  when Formula... is used: it sets the reason, returns a null `QString` before the input dialog
  opens, and `show_custom_fields` puts the reason on the status line once the dialog closes (even
  if the dialog is then cancelled, so the reason is not overwritten by "custom fields cancelled").
  Nothing is queued and the queue is unchanged.

Tests: `qt_sort_persist` (a queued Name descending Sort) and `qt_task_mode_document_edit_gate` (a
queued Manually Schedule, a second exclusive kind): the dialog opens and cancels cleanly (queue
unchanged, status "custom fields cancelled"); with Formula... used on Text1 and the dialog
accepted, no input dialog opens, the status line names the refusal (the Sort's own wording; the
generic "File > Save writes on its own, so it would refuse a custom-field formula edit beside it"),
the queue is unchanged and, once the Sort is cleared, nothing is pending.

Red:
- With `main_window.cpp` at the parent (`0.1-red-run.log`): 6 FAILs, the dialog does not open
  ("Custom Fields opens beside a queued sort", "... cancelling ... the status line says so",
  "... opens again", and the three for the mode edit).
- With the callback refusal mutated so it cannot fire (`formula_refusal.clear()`, then restored;
  `0.1-red2-run.log`): the input dialog opens and the formula queues, 5 FAILs on the new needles
  ("Formula... beside a queued sort opens no input dialog", "... the status line names the refusal",
  "no custom-field formula was queued beside the sort", and the two for the mode edit) and knock-on
  FAILs in later blocks (29 FAIL lines in all).

Green: `0.1-green-run.log` (`qt_sort_persist`, `qt_task_mode_document_edit_gate`,
`qt_calculate_gate` all pass).

## 0.2 (R35fG0-1)

The 1.1 record's sentence "(b) only where the Sort stays session only, since on the five-task file
the Sort queues" was wrong: only a single Name key queues on the five-task file. It is rewritten in
`wave35e-verification/gates-g1.md` as "(b) wherever the chosen Sort stays session only, including a
non-Name sort on the five-task file"; `wave35e-complete.md`'s 1.1 row names the new test. No other
record copied the sentence (searched `docs/` and `memory/`).

The review's Fix offers a test or a note that the six-task test covers the guard; a test was added
because the claim was wrong in the first place. `qt_sort_persist` ("Level-first"): Level 1 queues one
`show_level` edit on the five-task file, Ctrl+Z withdraws it onto the redo list, Start descending
stays session only and leaves Redo disabled, Ctrl+Y queues nothing. Red with
`redo_top_is_view_edit` narrowed to a Sort only (mutated, then restored; `0.2-red-run.log`): the
two new FAILs "Level-first: a session Start sort ends the Level on the redo list" and
"Level-first: Ctrl+Y queues no Level under the Start-sorted rows", plus knock-on FAILs from the
re-queued Level (20 FAIL lines). A first mutation that narrowed only the `apply_sort_choice` call
site stayed green, because the Start sort reaches `apply_session_sort`'s own call of the same
helper; the helper is the mutation point. Green: `0.2-green-run.log`.

## Guards seen red

- 0.1: the pre-open refusal at the parent (6 FAILs) and the callback refusal mutated away (5 new
  FAILs and knock-ons).
- 0.2: `redo_top_is_view_edit` narrowed to a Sort only (2 new FAILs and knock-ons).

## Deferred

Nothing.

## Found, not fixed

- `check_markers.py --range main..HEAD` reports 1 hit:
  `spec/writers/evidence/trace-captures-r5.md` (marker "operator short first name", first byte
  16100). The file arrived in `5863054` (TRACE round 5c), before this wave, and the brief bars this
  wave from `spec/writers/evidence/`, so it is not touched. It is not a wave 35g change; the
  coordinator or the trace agent owns the scrub. Until then the marker check cannot read 0 for the
  wave's range.
- With Calculate the one kind that still refuses before the dialog, Custom Fields... cannot be
  opened to look beside a queued Calculate Project either, which the review's wording ("opening to
  look stays free") would also free. The brief says to keep W35f-0.2's guard and its
  `qt_calculate_gate` case, so it was kept.
