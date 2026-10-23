# memory/qt.md -- the Qt shell, durable notes

Stream Q4, wave 19 ("Qt commands and dialogs").  Read this with
`docs/ui-qt-port.md` (fidelity contract), `docs/parity.md` (measured geometry)
and `docs/handoff/qt.md` (this stream's report).

## State

* The shell is a moc-free Qt 6 Widgets `QMainWindow` in `app_qt/`.  Never add
  `Q_OBJECT`/custom signals without deliberately switching the target to AUTOMOC;
  every connection today is a lambda on a Qt signal.
* Wave 18 froze the reference geometry at 1920x1032 (60 px gutter, row 22,
  header 42, splitter 0.459, 24 px/day, nine Entry columns
  `[38,57,147,81,81,81,105,105,124]`).  Wave 19 must not move any of it: the
  task-sheet widget tree, `SheetHeaderView`, the delegate and
  `apply_split_fraction` keep the same numbers.  The only additions in that view
  are edit triggers and longer status-bar text.
* Qt 6 is **not installed** on the Windows box.  Everything in `app_qt/` is
  written blind and compiled by CI (`qt-linux`, plus the new Windows/macOS jobs).
  The local gate is the portable `app/` suite in `build\qt-app-w19` (15/15 at the
  time of writing).
* CMake needs the local `cmake` path on this box:
  `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
  (the same directory has `ctest.exe`).  It is not on PATH.

## The one rule the command surface follows

`app/main.mm` is the reference behaviour; the Qt shell only builds controls.  A
decision that both shells must make lives in `app/` (save, structural edits,
filter build, task fields, predecessor validation, print pagination, start
warnings).  Do not re-implement one of those in `app_qt/`.

## Save architecture (do not grow a second writer)

* `MainWindow` owns one `app::Document` (source path = the file that was opened)
  and one `app::StructuralEdits` stack.
* Plain Save: `Document::save(start_edits, target, {allow_source_overwrite})`
  then `app::accept_start_edits` on the loaded baseline.
* Any pending structural edit: `Document::save_structural(...)`, then **reload
  the target** (`open_file`) and set `owns_path_ = true`.  The added rows' real
  uids are the file's; never patch one by hand.
* `allow_source_overwrite` is true only after that reload.  The file the user
  opened is never overwritten by a plain save.
* Dirty = `collect_start_edits()` non-empty OR `StructuralEdits` non-empty; that
  is exactly what a save writes.  Name/Finish/Duration edits are view-only.
* A save with nothing pending is byte-identical to the source (`Document`'s
  invariant; asserted in `app/document_tests.cpp` and in `qt_save_reopen`).

## Editing

* `SheetTableModel::setData` is the only edit path: select the cell, then
  `begin_edit`/`set_edit_text`/`commit_edit(false)`.  `flags()` reads the
  column's `read_only` bit and must never call `begin_edit` (a view asks for
  flags constantly).
* The commit callback runs **after** the edit session closed.  Do not reset the
  model from inside `setData` -- `after_cell_commit` only rebuilds the Gantt and
  the status.  Menu Undo/Redo (outside a commit) may reset.
* A cell edit clears the structural redo list; pushing/reverting a structural
  edit replaces the `SheetModel` (and so its history), which is the app's
  documented chronological rule.

## Filter and views

* The filter is stored as `core::view::FilterDefinition` in `PlanDocument` and
  re-evaluated after every structural rebuild; the RecordSet is never filtered.
  `display_rows()` is the one displayed list (table + Gantt).
* Generic views (resource sheet, usage grids) use the SAME `SheetTableModel` in
  `generic` mode, a separate `RecordSet`/`SheetModel`, read-only columns, and a
  header that titles from the model.  The Gantt is hidden; `apply_split_fraction`
  returns early while a non-task view is active.
* Usage grids are `core::usage::project_usage` + `app::make_usage_view` on
  `app::standard_calendar()` (the app's documented stock week), not the file's
  calendar.

## Print vs PDF

* Print = `QPrinter`/`QPrintDialog` + `app::build_print_job` (page geometry and
  pagination) + our QPainter page in `print_page.cpp`.  New CMake requirement:
  `Qt6::PrintSupport` (Linux CI must have `libqt6printsupport6`).
* Export PDF and File ▸ Save as PDF = `app::export_report` (core/render), i.e.
  the shared renderer's bytes on the same `PageSpec`.  Do not add a second page
  arithmetic anywhere.

## Traps paid for

* **Dropbox touches mtimes**: the editing tools report "file changed since it
  was read" constantly.  Re-read the file immediately before writing/editing.
* **MSVC/Application Control** transiently blocked a freshly linked
  `smoke_tests.exe` ("An Application Control policy has blocked this file") in
  `build\qt-app-w19`; deleting and rebuilding the target fixed it.  It is an
  environment issue, not a test failure.
* The fixture is `tests/data/synthetic-base.mpp` and is **read-only**: every
  test copies it under the build tree first.  MS Project rewrites a file it
  opens; never open the committed fixture in place.
* `QFileDialog` statics need `DontUseNativeDialog` for the fidelity contract.
* The row-number gutter is the view's vertical header; it is not a model column.

## Next three actions

1. Compile `app_qt` on CI; fix the reported Qt 6.2 API slips (keep each uncertain
   API behind one helper: `print_page.cpp`, `dialogs.cpp`).
2. Confirm `qt_parity_geometry`/`qt_render_parity` still pass at 1920x1032 and
   P1 still scores 9/9.
3. Then wire clipboard (Copy/Paste through `SheetModel`), and optionally persist
   the chosen view/preferences.
