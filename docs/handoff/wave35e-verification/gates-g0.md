# Wave 35e, group G0: builder record

Branch `wave35e`. One item, one finding id per commit, with the trailer
`Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35e)` (checked by `build/w35e/commit.sh`
before the commit and counted after). No writer added, no refusal lifted, no pair measured, no
COM, no push, no workflow dispatch, no history rewrite.

Gate tooling: `build/w35e/gates.sh <label> [qt]` (Visual Studio 17 2022, Release,
`CORE_WARNINGS_AS_ERRORS=ON`; trees `build/w35b-close-fresh`, `-app`, `-qt`). Logs are
`build/w35e/<parent-sha>-W35e-<label>-*.log` and `*-gates.status` (git-ignored `build/`).
Scans each time: `check_fixtures.py`, `check_markers.py --range main..HEAD`, `guard_names.py`,
`scan_client_names.py`, and `git grep "client-string-[0-9a-f]{8}"` (none).

| Item | Finding | Commit | Status |
|---|---|---|---|
| 0.1 | R35dG0-1 (High), with R35dW-1 | `b8ee455` | fixed |

## Gate line

- 0.1 (`43fee2a-W35e-0.1-*`): core build 0 / 0 warnings, core ctest `-E plan_recalc` 65/65, app
  build 0 / 0 warnings, app 34/34, app_qt build 0 / 0 warnings, app_qt 52/52, fixtures 0, markers
  0, guard_names 0, scan_client_names 0, client-string grep none.

The measured pairs still write and match with effects on (`qt_sort_persist`,
`qt_outline_persist` and `qt_calculate_gate` save-and-compare blocks, and the app and core
calculate pair tests, are inside the suites above).

## 0.1 (R35dG0-1, R35dW-1)

`MainWindow::apply_task_information` (`app_qt/main_window.cpp`) commits Name, then Duration, then
Finish, each through `commit_edit`, and does not roll back a field that committed when a later one
fails. `clear_redo()` and `after_cell_change()` ran only inside `if (ok)`. After a Sort, Show
Level or Calculate Project, Ctrl+Z, a Name that commits with a Duration that does not parse, and
Ctrl+Y, the withdrawn edit was re-queued without `push()` or the builder.

Fix: `if (!ok && applied)` now clears the structural redo list and runs `after_cell_change()`
(`applied` is true when a field committed with `outcome.changed`). The `ok` branch is unchanged.
The structural edits of the same apply (custom fields, percent) stay unqueued when `ok` is false,
as before: `trial` is dropped.

Tests: a "partial apply" block in each of `qt_sort_persist`, `qt_outline_persist` and
`qt_calculate_gate`: queue the edit, Ctrl+Z, `task_information_values`, set a new Name and an
unparseable Duration, `apply_task_information`. Each asserts the apply fails, the Name stays
committed, Redo is disabled, Ctrl+Y queues nothing and the edited name is as the edit left it.
Red with `main_window.cpp` at the parent (`build/w35e/0.1-red-run.log`): 9 FAILs, three in each
test (Redo enabled, an edit re-queued, the edited name lost). Green: `0.1-green-run.log` and
`43fee2a-W35e-0.1-*`.

Sweep for the same pattern (every shell path that commits a cell and can then fail):
- Cut, Paste and Fill Down stage their cells and apply them as one `SheetModel` transaction
  (`apply_commands`): a failure leaves nothing committed, and success already clears the redo
  list (W35d-0.5, Fill Down, `after_cell_commit`). Not a partial-apply path.
- The single-cell path (`SheetTableModel::setData` then `after_cell_commit`) commits one cell and
  clears on success; a refused cell changes nothing.
- The sheet's own Undo and Redo must not clear the list.
- The Task Form calls `apply_task_information`, so it is covered by the fix.
- No other path commits more than one cell with an independent per-cell failure.

## Guards seen red

- 0.1: the `if (!ok && applied)` clear off, 9 FAILs across the three tests.

## Deferred

Nothing.

## Found, not fixed

- Nothing new. The partial apply still leaves the committed fields in place (no rollback); the
  brief asks only that the structural redo list end, so the status line carries the failing
  field's message and the user's Undo takes the committed cells back.
