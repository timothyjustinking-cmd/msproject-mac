# memory/qt2.md — wave 20 Q5, Qt structural cut

Successor notes for the Qt shell after the first wave-20 cut. Read
`docs/handoff/qt2.md` for the full state; this is the working memory.

## Where the build is

* Qt 6 is NOT installed on the Windows dev box. Only the portable `app/` gate can
  run locally: `build\qt-app-w20`, 15/15 green. All Qt code below is
  compile-pending on the coordinator's three-platform CI.
* Spec: `docs/ui-parity-spec.md` (wave 20 decisions). Parity reference:
  `docs/parity.md` (light theme; 9/9 sheet/chart baseline).
* New source files: `backstage.{h,cpp}`, `theme.{h,cpp}`,
  `preferences_store.{h,cpp}`, `shell_icons.{h,cpp}`, `view_label.{h,cpp}`,
  `command_status.h`, `tools/command_backlog.py`.
* New tests: `qt_empty_state`, `qt_chrome_gate`, `qt_view_gate`,
  `qt_dialog_gate`, `qt_clipboard` (all in `app_qt/tests/`, registered in
  `app_qt/CMakeLists.txt`).

## Invariants that must not drift

* **Sheet/chart parity**: row 22, header band 42 (P1 measures 43 with its two
  rules), gutter 60, Entry widths 39/57/147/81/81/81/105/105/123, splitter
  0.459, day 24, tiers 2, colours white/(212,212,212)/(247,247,247).
* The 22 px `ViewLabel` lives **inside the chart pane**, not left of the sheet.
  Moving it left would shift P1's sheet-left-edge 60 and break the columns.
* Ribbon metrics are the operator's measured values: tab row 30, group page 95,
  button 64, icon 32, caption 14. `kRibbonHeight = 125`; the ribbon widget is
  `kRibbonHeight + 1` (bottom rule).
* `SheetTableModel::columnCount()` returns 9 even with no sheet, and
  `rowCount()` returns 1 for an empty Entry project (the product's first empty
  row). Do not "fix" either to 0.
* Empty project Gantt: `PlanDocument::build_empty_view(zoom, today)` starts the
  axis on the Sunday of the current week. Never let `Timescale` fall back to
  1984-01-02.
* Menus are built on demand: `SheetHeaderView::build_column_menu`,
  `MainWindow::build_filter_menu`, `MainWindow::build_row_menu`. Tests delete
  them; the header menu's parent is the header, the others are MainWindow
  children.
* `command_status.h` `shell_implements()` is the only implemented set. Keep it
  in step with `handle_action()`; the `// UNIMPLEMENTED` marker is read by the
  backlog generator.
* `docs/ui-command-backlog.md` is generated. Regenerate:
  `python app_qt/tools/command_backlog.py`.
* Cut is enabled locally in `update_menu_state()` (the shared app model still
  lists it unsupported for macOS). Copy/Cut/Paste are SheetModel values-only
  operations; Paste is one undo step, Cut is Copy + one clearing transaction.
* Preferences persist through `PreferencesStore` (QSettings) using
  `app::serialize_preferences` + `app::kPreferencesDefaultsKey`; the theme has a
  sibling key `...theme`. Tests use an explicit ini path under `test-out/`.
* `--render` never loads persisted preferences/themes; the parity artifacts stay
  deterministic.

## Traps already hit

* The workspace is on Dropbox: MSBuild intermittently fails with "permission
  denied" on an `.obj` path. Retrying the exact build command worked; it is not
  a source error.
* `app::menu_item_enabled` returns true for `insert_resource`? No — it is false,
  but `hide_app` has no Qt handler, so `shell_implements()` must exclude both.
* The backlog parser needs the multi-line tab spec and the `UNIMPLEMENTED`
  marker; a naive `case` regex counted the false cases as implemented.
* `QHeaderView::setSectionsClickable(false)` silently stops the column menu from
  opening; it must be true.
* The ribbon overflow buttons are layout-external children so `clear_page()`
  cannot delete them.
* `Backstage` pages must be added in `Page` enum order (Info=3, Options=10).
* `PreferencesDialog` now takes `(preferences, Theme, parent)`; the free
  `edit_preferences()` passes `Theme::light` because the shell applies the theme
  separately.
* `check_vertical_hint` must not require `isVisible()`: it runs after accept in
  one block.
* `/W4`: watch C4267 on `QStringList::size()` (qsizetype), C4100 on unnamed
  lambda parameters, C4458 name shadowing in dialog locals.

## What is not done

Build-order step 3 (file-driven `214/CTable` columns/order/titles), full dialog
inventory (decision 14/15), Details pane, View ▸ Timeline toggle, Gantt bar
context menu, dark custom-painted chrome, QAT customize, Page Setup/print
preview, accelerators, drag-fill, pixel-diff reporting. See the handoff's
deliberate-gaps list.

## Local commands

```sh
# portable app gate (the only Qt-adjacent gate that runs here)
cmake -S app -B build\qt-app-w20 -G "Visual Studio 17 2022" -A x64 \
      -DCORE_BUILD_TESTS=ON -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build build\qt-app-w20 --config Release
ctest --test-dir build\qt-app-w20 -C Release -E plan_recalc --output-on-failure

# regenerate the command backlog after changing menu_model/ribbon/command_status
python app_qt/tools/command_backlog.py
```
