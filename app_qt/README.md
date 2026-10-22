# app_qt/ — the three-platform Qt shell

One `QMainWindow` over the portable `app/` models: a ribbon-like toolbar and a
timeline strip above a task sheet (a `QTableView` over `core::sheet::SheetModel`)
and a `QPainter` Gantt side by side in a 0.459 split, the menu bar from
`app/menu_model`, and a status bar with view buttons and a zoom slider.

Wave 17 stream Q2 made the client area look like MS Project's measured
reference (`docs/parity.md`).  The numbers are P1's measurements, not estimates:

| measure | value |
|---|---|
| sheet row pitch | 22 px |
| sheet header band | 42 px |
| row-number gutter | 60 px (the sheet's first grid line is at x = 60) |
| Entry columns | 39, 57, 147, 81, 81, 81, 105, 105, 123 px |
| sheet/Gantt splitter | 0.459 of the client width (881 / 1920) |
| Gantt day cell | 24 px (week 168), two tiers of 21 px |
| colours | header white, grid (212,212,212), weekend (247,247,247) |
| typography | Calibri 11 pt (Carlito if installed; fixed fallback list) |

## Files

| file | what it is |
|---|---|
| `main.cpp` | process contract: interactive window, `--open <file.mpp>`, `--render <out.png>` (fixed 900x560, offscreen) |
| `main_window.{h,cpp}` | the window: Fusion palette, font resolution, ribbon + timeline + splitter, sheet, Gantt, status bar, menus, command dispatch, `--open`, 43/0.459 split, data-driven column widths |
| `shell_style.h` | the fidelity contract's fixed numbers and colours, in one place |
| `entry_columns.{h,cpp}` | the nine Entry columns: titles, model fields, stock character widths, `214/CTable` field ids, `chars*6+3` rule |
| `sheet_table_model.{h,cpp}` | `QAbstractTableModel` over `core::sheet::SheetModel`: gutter column 0 + the nine Entry columns |
| `sheet_views.{h,cpp}` | 42 px caption-band painter, Indicators/Task Mode delegate, splitter handle with a visible drag thumb |
| `ribbon_bar.{h,cpp}` | tabs (Task/Resource/Report/Project/View/Help + contextual Gantt Chart Format), groups, labelled text buttons, 105 px |
| `timeline_strip.{h,cpp}` | Start/Finish labels + "Add tasks with dates to the timeline", 44 px |
| `gantt_canvas.{h,cpp}` | `QWidget` + `QPainter`: two-tier weekly/day header, bars, links, weekend shading, `header_info()` |
| `plan_document.{h,cpp}` | load a `.mpp` through `app/reader_adapter.h`; owns the RecordSet, SheetModel, RowMap, GanttLayout; reads `214/CTable` widths; 24 px/day weekly timescale |
| `menus.{h,cpp}` | the one `app::menu_model` → `QKeySequence` translation (`Ctrl` portable, Command on macOS) |
| `tests/shell_test.cpp` | Q1's offscreen gate: fixture rows, full menu command list, 900x560 PNG |
| `tests/parity_test.cpp` | `qt_parity_geometry`: the measured geometry/typography gate |
| `CMakeLists.txt` | Qt6 Widgets target, `app/` as a subdirectory with its headless tests OFF, four CTest entries |

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
* `qt_parity_geometry` — asserts row 22, header 42, gutter 60, the nine column
  widths within 2 px (plus the `214/CTable` readback that produces them), the
  0.459 split, a visible horizontal scrollbar, 24 px/day, two 21 px tiers, the
  measured colours, the Calibri-11/fallback typography, the seven-tab ribbon,
  the timeline strip and the status-bar zoom slider wired to Day/Week/Month.

## How the column widths are produced

The file's `214/CTable` stores a column's width in characters (the first of its
nine opaque bytes; `core/viewstate` and `docs/handoff/w62.md`).  `PlanDocument`
reads the CTable on load (`table_column_chars`), and `MainWindow` converts each
Entry column with MS Project's rule **`width = characters * 6 + 3`**:
24 chars → 147 px, 13 → 81, 17 → 105, 20 (the Add New Column stub) → 123.  The
stock character widths are the fallback when a file has no CTable for a field.
The status line reports the stored characters.

## Wired

Open (`--open` or the dialog), the menu bar and its enablement, the sheet and
Gantt views, selection (click a sheet cell → the Gantt row/bar is outlined),
status line and view buttons, zoom (View ▸ Zoom, Day/Week/Month timescale, and
the status-bar slider), working-day shading, Collapse All / Expand All, Select
All, Undo/Redo.  The ribbon's buttons that map to modelled commands dispatch
through the same handler as the menu; the rest report "not wired" in the status
line.

## Deliberately not wired / not matched

* The Q3 commands (dialogs, save, print, export, filter, view switching,
  Insert/Delete Task, clipboard) are present in the menu with
  `app::menu_model`'s enablement and report "not wired in the Qt shell yet".
* The ribbon is structure, not artwork: text buttons and simple glyphs (the
  brief puts pixel-exact ribbon icons out of scope).  At 900 px the rightmost
  groups are clipped, as a ribbon is at a narrow width.
* The row-number gutter scrolls with the columns (it is model column 0); it is
  not frozen.  Freezing it needs a second viewport overlay.
* The task-bar colour is the shell palette: the current reference capture has
  no bars (docs/parity.md §2.3), so no bar colour is claimed.
* Cell editing is off (`NoEditTriggers`); Q3 wires the edit controllers.

## Fidelity notes for Q4

* `QApplication::setStyle("Fusion")`, a palette built in code, fixed point
  sizes, integer pixel column/row geometry, `QFileDialog::DontUseNativeDialog`.
* The font family is resolved from the fixed list in `shell_style.h`; Carlito
  is preferred but is not installed here and downloading is forbidden, so this
  machine resolves Calibri and the Linux CI will resolve a DejaVu/Liberation
  fallback.  The chosen family is reported in the About box and the status line
  and is readable through `msqt::ui_font_family()`.
* No status-date marker is drawn and `PlanDocument` passes no status date, so
  the render does not depend on today's date.
* A geometry-only mock of the same design (no Qt) can be scored against the
  reference: `python build/q2-scratch/mock.py` then
  `python scripts/parity_report.py corpus/bench/msproject-gantt.png build/q2-scratch/mock.png`
  (git-ignored scratch; it passes every comparable measure).
