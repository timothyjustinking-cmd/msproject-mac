# app_qt/ — the three-platform Qt shell

One `QMainWindow` over the portable `app/` models: the task sheet on the left, a
`QPainter` Gantt on the right, a menu bar built from `app/menu_model`, and a
status bar.  This is wave 16 stream Q1 (docs/ui-qt-port.md §5): the skeleton the
later streams extend.

## Files

| file | what it is |
|---|---|
| `main.cpp` | process contract: interactive window, `--open <file.mpp>`, `--render <out.png>` (fixed 900x560, offscreen) |
| `main_window.{h,cpp}` | the window: Fusion palette, sheet view, Gantt, status bar, menus, command dispatch, `--open` |
| `sheet_table_model.{h,cpp}` | `QAbstractTableModel` over `core::sheet::SheetModel` (displayed rows/columns, no second data copy) |
| `gantt_canvas.{h,cpp}` | `QWidget` + `QPainter`; paints `core::gantt::GanttLayout`'s bars, links, headers and shading |
| `plan_document.{h,cpp}` | load a `.mpp` through `app/reader_adapter.h`; owns the RecordSet, SheetModel, RowMap and GanttLayout |
| `menus.{h,cpp}` | the one `app::menu_model` → `QKeySequence` translation (`Ctrl` portable, Command on macOS) |
| `shell_style.h` | the fidelity contract's fixed numbers (window 900x560, body 9 pt, header 8 pt) |
| `tests/shell_test.cpp` | offscreen test: fixture row count, menu command list vs `app::menu_model`, 900x560 render |
| `CMakeLists.txt` | Qt6 Widgets target, `app/` added as a subdirectory with its headless tests OFF, three CTest entries |

`app/` and `core/` are linked unchanged; nothing here edits them.

## Build and test

```sh
cmake -S app_qt -B build-qt -DCMAKE_BUILD_TYPE=Release -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build build-qt --config Release
ctest --test-dir build-qt --output-on-failure
```

CTest (all offscreen, `QT_SCALE_FACTOR=1`):

* `qt_render_smoke` — `msproject_qt --render build-qt/qt-render-smoke.png`;
  asserts `rendered 900x560`.
* `qt_render_document` — the same with
  `--open tests/data/synthetic-base.mpp` (read-only).
* `qt_shell_offscreen` — builds the real window against the fixture and checks
  3 rows, the whole menu command list against `app::menu_specs()`, and that the
  saved PNG reads back at 900x560.

## Wired in Q1

Open (`--open` or the dialog), the menu bar and its enablement, the sheet and
Gantt views, selection (click a sheet cell → the Gantt bar is outlined), status
line, zoom (View ▸ Zoom / Day/Week/Month timescale), working-day shading,
Collapse All / Expand All, Select All, Undo/Redo.

## Deliberately not wired yet

The Q2/Q3 commands (dialogs, save, print, export, filter, view switching,
Insert/Delete Task, clipboard) are present in the menu with `app::menu_model`'s
enablement and report "not wired in the Qt shell yet (wave 17)" in the status
line rather than doing nothing silently.  Cell editing is off; `SheetModel` is
already read-only-policed for UID/ID/Outline, so Q2 can turn editing on.

## Fidelity notes for Q4

* `QApplication::setStyle("Fusion")`, a palette built in code, fixed point sizes,
  integer pixel column/row geometry, `QFileDialog::DontUseNativeDialog`.
* The font **family** is still the platform's default; pinning a bundled font
  (docs/ui-qt-port.md §2.2) is the golden-image stream's remaining task.
* No status-date marker is drawn and `PlanDocument` passes no status date, so
  the render does not depend on today's date.
