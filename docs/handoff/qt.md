# Qt shell handoff -- wave 19 stream Q4 (commands and dialogs)

**Status: first cut written, portable gates green, Qt compile pending CI.**
Qt 6 is not installed on the Windows development box, so nothing under `app_qt/`
could be compiled here.  Everything that does not need Qt was built and run:

```sh
cmake -S app -B build\qt-app-w19 -G "Visual Studio 17 2022" -A x64 \
      -DCORE_BUILD_TESTS=ON -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build build\qt-app-w19 --config Release
ctest --test-dir build\qt-app-w19 -C Release -E plan_recalc --output-on-failure
```

Result: **15/15 pass** (the app/ headless suite; `app_smoke` needed one retry
because Windows Application Control transiently blocked the freshly linked exe
-- running the identical rebuilt binary passed, 98 checks).  No `app/` source was
changed: `app/` was only built as the consumer's dependency.

`docs/handoff/wave18-complete.md`'s 9/9 UI parity baseline is **not** expected to
move: the task sheet, its nine Entry columns, the frozen 60 px gutter, the
0.459 splitter and the 1920x1032 render are untouched.  That claim needs the CI
`qt_parity_geometry`, `qt_render_parity` and the P1 harness to confirm.

## Done

| area | what landed |
|---|---|
| editing | `SheetTableModel::flags/setData` run the real path: `SheetModel::begin_edit` -> `set_edit_text` -> `commit_edit`.  Double-click / Return are the triggers.  A rejected value shows `EditOutcome::error` in the status line. |
| undo/redo | Edit ▸ Undo/Redo first use `SheetModel`'s stack, then the structural `app::StructuralEdits` stack; a cell edit clears the structural redo list (the app's chronological rule). |
| Insert/Delete Task | Insert asks for a name (`QInputDialog`) and pushes a pending insert; Delete confirms the cascade size and pushes a pending delete (or drops a pending insert).  Both rebuild through `PlanDocument::apply_structural_edits`, which folds pending cell values back via `StructuralEdits::without_structural_edits`. |
| dirty state | `MainWindow::document_dirty()` = pending `collect_start_edits()` **or** a non-empty `StructuralEdits`.  The window title gains ` *`. |
| discard guard | `closeEvent` and File ▸ Open ask Save / Discard / Cancel when dirty; Save goes through the normal save path, and a cancelled panel or writer failure aborts the close. |
| Save / Save As | One call site: `app::Document::save` (start pins) or `save_structural` (link/task/field/start cascade), with `SaveOptions::allow_source_overwrite = owns_path_`.  Save As uses `QFileDialog::getSaveFileName` + `DontUseNativeDialog` and `app::default_save_name()`.  A structural save reloads the target and adopts it.  `SaveOutcome` warnings/notes/failure are surfaced in the status line, and warnings in a dialog. |
| start-edit warning | `app::warn_start_edits` runs once per interactive save, before the panel; Save Anyway / Cancel. |
| Print | `QPrinter` + `QPrintDialog`, paginated by `app::build_print_job` (A4 landscape, 24 pt) and painted into the `app::PageLayout` bands; the printed Gantt is `core::gantt` geometry.  New file `print_page.{h,cpp}`. |
| Export PDF | `app::export_report` (core/render) with the visible columns, the filtered rows and the same `PageSpec`; File ▸ Save as PDF uses the same export route, so its bytes are the shared renderer's. |
| filter | `app::filter_fields` / `filter_operators` / `build_filter` in a Qt dialog; the definition is stored in `PlanDocument`, so a structural rebuild re-evaluates it.  Only the displayed row list is restricted. |
| views | `app::ViewModel` wired to all four View actions and the status-bar buttons: Task Sheet (Entry table + Gantt), Resource Sheet, Task Usage, Resource Usage; usage period Day/Week/Month rebuilds the grid.  The generic grids are read-only, titled from the sheet model. |
| dialogs | Task Fields (`app::field_link_edits`), Predecessors (add/delete with lag), Filter, Insert name, save-changes prompt, start-edit warning, About.  All Qt, no native panels, no Q_OBJECT/AUTOMOC. |
| acceptance test | `app_qt/tests/save_reopen_test.cpp` (`qt_save_reopen`): copies the committed fixture into `test-out/`, edits a cell through the shell model, exercises Undo/Redo via the menu actions, saves through `app::Document`, reopens, checks the SNET constraint persisted, and proves a no-edit save is byte-identical. |

## Files touched

* `app_qt/sheet_table_model.{h,cpp}` -- edit path, generic (non-Entry) presentation, filtered display list, commit callback.
* `app_qt/sheet_views.{h,cpp}` -- caption band can title from the model; the Task Mode pin is Entry-only.
* `app_qt/plan_document.{h,cpp}` -- records accessor, structural rebuild, filter state/display rows, start-edit baseline acceptance.
* `app_qt/main_window.{h,cpp}` -- the command surface: save/print/export/filter/views/dialogs/insert/delete/undo/redo/dirty/close guard.
* `app_qt/dialogs.{h,cpp}` (new) -- Task Fields, Predecessors, Filter, Insert-name dialogs.
* `app_qt/print_page.{h,cpp}` (new) -- QPrinter route and the shared-page painter.
* `app_qt/tests/save_reopen_test.cpp` (new) -- the acceptance gate.
* `app_qt/CMakeLists.txt` -- `PrintSupport` component + link, two new sources, the new test.
* `app_qt/README.md`, `docs/handoff/qt.md`, `memory/qt.md`.

Nothing under `app/`, `core/`, `tests/data/`, `.github/` or the top-level
`CMakeLists.txt` changed.  No git commands were run.

## Needs CI

1. **Compile** the Qt target on the `qt-shell` workflow (Qt 6.5.3, Linux/Windows/
   macOS).  The Windows job runs `/W4 /WX`: the first run already caught C4267 in
   the wave-18 `parity_test.cpp`, now fixed
   (`columnViewportPosition(static_cast<int>(columns.size()) - 1)` became an
   `int last_column`).  Expect the next run to find the API slips in the new
   code; keep them isolated in `print_page.cpp` (QPrinter/QPage*) and
   `dialogs.cpp` (dialog widgets).
2. **`Qt6PrintSupport` must be installed** in the Linux job (Ubuntu:
   `qt6-base-dev` normally carries it; a slim image also needs
   `libqt6printsupport6`).  `app_qt/CMakeLists.txt` now requires the component.
3. Run the whole Qt CTest set: `qt_render_smoke`, `qt_render_document`,
   `qt_render_parity` (must still print `rendered 1920x1032`), `qt_shell_offscreen`,
   `qt_parity_geometry`, `qt_save_reopen`.
4. P1's harness on the 1920x1032 render: expect 9/9 unchanged.

## A lifecycle rule the next stream must keep

`SheetTableModel` holds a raw `core::sheet::SheetModel*`; `PlanDocument` (and
`view_sheet_`) own the model.  **Detach the Qt adapter (`set_sheet(nullptr)`)
before anything replaces or destroys the SheetModel** -- `open_file`,
`after_structural_change` and `show_view` all do.  Without it a model reset asks
the view for rows on freed memory.

## Known open / deliberate gaps

* **Save as PDF** writes the core/render bytes (the byte-identical shared
  renderer), not a QPrinter PDF; **Print** is the QPrinter route.  The brief asked
  for both "QPrinter for print" and "PDF bytes equal the shared renderer", so the
  split is explicit.  Both use `app::PageSpec`; `app/page_layout_tests.cpp` pins
  that they paginate the same.
* **Clipboard** (Copy/Paste/Cut) is still not wired: the menu items stay disabled.
* **Preferences, Table Date Format and Custom Field Formula** commands still
  report "not wired"; they were not in the wave-19 dialog list.  `Document`'s
  view-state/formula pending-edit API is already used by the save path if a later
  stream wires them.
* The **filter dialog authors one rule** (the app model supports All/Any trees;
  the reference's dialog is one criterion).
* The view buttons do not persist the chosen view across a launch (no prefs
  store in the Qt shell yet); a newly opened file starts in the Task Sheet.
* Print's chart is the same `core::gantt` geometry at the same `PageSpec` bands,
  but it is our own QPainter page, not the AppKit `PrintView`; ink/colour choices
  are the shell's.

## Next three actions

1. Fix the CI compiler log (one helper per uncertain Qt API: `print_page.cpp`
   for QPrinter/QPage*, `dialogs.cpp` for the dialog widgets).
2. Run `qt_save_reopen` on CI; if the fixture's manual row makes the persisted
   Start reopen at a different *effective* date, keep the constraint assertion
   (that is the writer's measured contract) and add an explicit note.
3. Once the Qt job is green, wire the clipboard through
   `SheetModel::copy_selection`/`paste` and re-score parity at 1920x1032.
