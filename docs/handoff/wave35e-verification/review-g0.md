# Wave 35e G0 review (43fee2a..5d733ae, item 0.1: R35dG0-1 with R35dW-1)

0C/0H/0M/3L

Read only, from `git show 5d733ae:` and `git diff 43fee2a..5d733ae`, plus the builder's
git-ignored logs under `build/w35e/`. Nothing was built or run.

## Verdict on the six checks

1. **Route closed.** Yes, for all three kinds and both surfaces. The new block at
   `app_qt/main_window.cpp:5927-5935` runs `structural_.clear_redo()` whenever `!ok && applied`.
   `clear_redo()` empties the whole redo list (`app/edit_model.h:874`), so it does not depend on
   whether the withdrawn edit is a `sort_view`, `show_level` or `calculate_project`. The Task Form
   (`commit_task_form_field`, `main_window.cpp:2887-2932`) calls the same function, so it shares
   the fix. In the custom-field or percent case (`structural_applied` true, `ok` false), `trial` is
   still dropped and the live list is only redo-cleared, which is correct. The field loop
   `continue`s after a failure (`:5911-5923`), so a failure *before* a later committed field is
   covered too.
2. **`applied` is the right condition.** `applied` is set only from `outcome.changed`
   (`:5924`). `commit_edit` sets `changed = !values_equal(before, value)` (core/sheet/sheet.cpp,
   `commit_edit`). `values_equal` compares the whole `Value` (kind plus ticks, number, text or
   date; there is no hidden unit), so `changed == false` means the record is byte-for-byte what
   it was, with no undo step. A field whose `begin_edit` succeeded and whose commit then failed is
   `cancel_edit`ed before anything is written (`parse_edit_text` fails before `set_field`). No
   path commits a change while `applied` stays false.
3. **Sweep.** Correct. Cut and Fill Down (`:5029-5066`, `:5276-5303`) and Paste (`:5128-5153`)
   go through `apply_commands`. That validates every staged cell before it writes any, so a
   failure commits nothing. `SheetTableModel::setData` (`app_qt/sheet_table_model.cpp:488-538`)
   commits one cell and clears through `after_cell_commit` (`:3548-3563`). No other Qt model is
   editable (the usage sheet's columns are read-only, `usage_sheet.cpp:233`). The AppKit shell
   (`app/main.mm:1443-1465`, `:1836-1841`) has no multi-field Task Information apply. Its
   `taskFields` path is structural only, and both of its cell paths clear the redo list. I found
   no remaining partial-commit path.
4. **Tests.** All three blocks (`calculate_gate_test.cpp:270-317`, `outline_persist_test.cpp:606-647`,
   `sort_persist_test.cpp:610-654`) reach `!ok && applied`: Name commits, then "not a duration"
   fails to parse. Each asserts that Redo is disabled, that `pending_edits()` is empty after
   Ctrl+Y, and that the name survives. `build/w35e/0.1-red-run.log` shows exactly 9 FAILs, three
   per test, all on the three assertions that only the new block satisfies. The "partial apply:"
   needles are unique to the new blocks. Coverage gaps: see R35eG0-2.
5. **Side effects of `after_cell_change()` on failure.** None found. `after_cell_change`
   (`:3565-3584`) and its callees (`refresh_timeline`, `refresh_task_form`, `refresh_inspector`,
   `update_window_title`, `update_menu_state`, `update_status`) contain no `set_message`, so the
   failing field's message survives. The success path already used the same rebuild, so selection
   handling is unchanged. The Task Form refreshes itself again at `:2931` in any case. The dialog
   is already closed when the apply runs.
6. **Hygiene.** Both commits carry `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35e)` in
   the trailer block. The diff has no names, client strings or personal paths. The change adds no
   new construct that GCC or Clang would warn on: the tests' `select_cell(1, 0)` follows an existing
   pattern (`outline_persist_test.cpp:361` at the parent) and nothing new is discarded or unused.
   The record's "fixed" is justified for the route R35dG0-1 and R35dW-1 describe.

## Findings

### R35eG0-1 (Low): Task Information re-commits an untouched Duration under the hours or days preference, rounding it (pre-existing; this is the Task Form's only route into the partial apply)

- `app_qt/main_window.cpp:5684` (`task_information_values` builds `duration_text` with
  `app::cell_text(..., prefs_)`) against `:5908` (the "untouched field" skip compares with
  `sheet->cell_text`, the sheet's own auto-units rendering).
- Scenario: Preferences, duration "days". A task has a 3 h duration. The sheet shows "3 hrs";
  `task_information_values` gives "0.38 days" (`app/prefs.cpp` `render_quantity`, hundredths).
  The user changes only the Name in Task Information or the Task Form. The skip at `:5908` sees
  "0.38 days" != "3 hrs" and commits Duration. That parses to 0.38 x 8 h = 3 h 2.4 min, so
  `changed` is true and the status says "applied". The session's Duration, Gantt and Finish move
  without being asked. The "hours" style does the same for, for example, 10 h 1 min
  ("10.02 hrs", +12 s). Cell edits are view-only, so Save is not affected. But the comment at
  `:5905-5907` ("an untouched field keeps whatever rendering the sheet gave it") is false under
  either preference. Because the Task Form changes one field per commit, this re-commit is the
  only way the form can reach a multi-field (partial) apply. Outside the reviewed range; the
  record's "Found, not fixed: Nothing new" missed it.
- Fix: in the skip at `:5908`, compare against
  `app::cell_text(*sheet, selected.row, column, prefs_)`, the same rendering the values were built
  from.

### R35eG0-2 (Low): the new block's tests drive only the dialog entry with cell fields; the Task Form entry, the structural-trial variant and the kept message are not asserted

- `app_qt/tests/calculate_gate_test.cpp:270-317`, `outline_persist_test.cpp:606-647`,
  `sort_persist_test.cpp:610-654`.
- Scenario: all three call `apply_task_information(values)` with the default source and no
  custom-field or percent change. A later change could break one of these without failing a test:
  `commit_task_form_field` calling something other than `apply_task_information`, the
  `structural_applied && !ok` case (for example a Text1 edit plus Name plus a bad Duration, where
  `trial` must stay dropped while the redo list is cleared), or `after_cell_change` starting to
  overwrite the failing field's message.
- Fix: in one of the three tests, add a variant with a changed `custom_field_texts` entry. Assert
  that `pending_edits()` is still empty after the failed apply and that the status text still
  names the Duration error.

### R35eG0-3 (Low): after a partial apply the status names only the failure, not the field that stuck

- `app_qt/main_window.cpp:5921` and `:5927-5935`.
- Scenario: Name commits and Duration fails. The status reads "task information: duration: ..."
  with no mention that the Name was applied and is now an undo step. The record leans on this line
  ("the status line names the failing field and Undo takes the committed cells back"). The user is
  not told that there is anything to undo. Pre-existing behaviour, by the brief's
  no-rollback choice.
- Fix: when `!ok && applied`, append "; <fields> applied (Undo takes them back)" to the message.
