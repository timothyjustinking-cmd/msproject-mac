# app_qt/ — the three-platform Qt shell

One `QMainWindow` over the portable `app/` models: a ribbon-like toolbar and a
timeline strip above a task sheet (a `QTableView` over `core::sheet::SheetModel`)
and a `QPainter` Gantt side by side in a 0.459 split, the menu bar from
`app/menu_model`, and a status bar with view buttons and a zoom slider.

Wave 17 stream Q2 made the client area look like MS Project's measured
reference (`docs/parity.md`).  Wave 18 stream Q3 closed the visible deltas that
render exposed.  The numbers are P1's measurements, not estimates:

| measure | value |
|---|---|
| sheet row pitch | 22 px |
| sheet header band | 42 px |
| row-number gutter | 60 px, FROZEN (the view's vertical header, not a column) |
| Entry columns | 39, 57, 147, 81, 81, 81, 105, 105, 123 px |
| sheet/Gantt splitter | 0.459 of the client width (881 / 1920) |
| Gantt day cell | 24 px (week 168), two tiers of 21 px |
| colours | header white, grid (212,212,212), weekend (247,247,247) |
| typography | Calibri 11 pt (Carlito if installed; fixed fallback list) |

## Files

| file | what it is |
|---|---|
| `main.cpp` | process contract: interactive window, `--open <file.mpp>`, `--render <out.png>` (900x560 unless `--parity` / `--render-size WxH`), `--parity` = the reference's 1920x1032 |
| `main_window.{h,cpp}` | the window: Fusion palette, font resolution, ribbon + timeline + splitter, sheet, Gantt, status bar, menus, command dispatch, `--open`, the 0.459/table-edge split, data-driven column widths |
| `shell_style.h` | the fidelity contract's fixed numbers and colours, in one place (`kParityWidth`/`kParityHeight` included) |
| `entry_columns.{h,cpp}` | the nine Entry columns: titles, model fields, stock character widths, `214/CTable` field ids, `chars*6+3` rule |
| `sheet_table_model.{h,cpp}` | `QAbstractTableModel` over `core::sheet::SheetModel`: exactly the nine Entry columns (the gutter is the row header) |
| `sheet_views.{h,cpp}` | 42 px caption-band painter, the FROZEN `SheetRowHeader` gutter, the white top-left corner, the Indicators/Task Mode delegate, splitter handle with a visible drag thumb |
| `ribbon_bar.{h,cpp}` | tabs (Task/Resource/Report/Project/View/Help + contextual Gantt Chart Format), groups, buttons sized from their labels, 105 px, whole-group overflow |
| `timeline_strip.{h,cpp}` | Start/Finish labels + "Add tasks with dates to the timeline", 44 px |
| `gantt_canvas.{h,cpp}` | `QWidget` + `QPainter`: two-tier weekly/day header, bars, labels, links, weekend shading, `header_info()` |
| `plan_document.{h,cpp}` | load a `.mpp` through `app/reader_adapter.h`; owns the RecordSet, SheetModel, RowMap, GanttLayout; reads `214/CTable` widths; 24 px/day weekly timescale |
| `menus.{h,cpp}` | the one `app::menu_model` → `QKeySequence` translation (`Ctrl` portable, Command on macOS) |
| `tests/shell_test.cpp` | Q1's offscreen gate: fixture rows, full menu command list, 900x560 PNG |
| `tests/parity_test.cpp` | `qt_parity_geometry`: the measured geometry/typography gate, the frozen gutter, no clipped ribbon labels, and the 1920 fit |
| `CMakeLists.txt` | Qt6 Widgets target, `app/` as a subdirectory with its headless tests OFF, five CTest entries |

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
  `--open tests/data/synthetic-base.mpp` (read-only), writing
  `build-qt/test-out/qt-render-document.png`.
* `qt_render_parity` — the same document rendered at the reference's own
  1920x1032 (`--parity`), writing `build-qt/test-out/qt-render-parity.png`;
  asserts `rendered 1920x1032`.  This is the image P1's harness scores for the
  column widths.
* `qt_shell_offscreen` — builds the real window against the fixture and checks
  3 rows, the whole menu command list against `app::menu_specs()`, and that the
  saved PNG reads back at 900x560.
* `qt_parity_geometry` — asserts row 22, header 42, the frozen 60 px gutter (it
  stays put when the sheet scrolls), the nine column widths within 2 px (plus
  the `214/CTable` readback that produces them), the 0.459 split, a visible
  horizontal scrollbar, 24 px/day, two 21 px tiers, the measured colours, the
  Calibri-11/fallback typography, the seven-tab ribbon with every visible
  button wide enough for its label, the timeline strip, the status-bar zoom
  slider, and that at 1920x1032 all nine Entry columns fit.

## Scoring against the reference

```sh
# the reference's size, so the column-width comparison is 9 vs 9
build-qt/msproject_qt --open tests/data/synthetic-base.mpp \
    --render build-qt/test-out/qt-render-parity.png --parity
python scripts/parity_report.py corpus/bench/msproject-gantt.png \
    build-qt/test-out/qt-render-parity.png
```

At 900 px the sheet pane shows four of the nine columns, so the harness reports
the widths N/A; at 1920x1032 all nine fit and it scores them.

## How the column widths are produced

The file's `214/CTable` stores a column's width in characters (the first of its
nine opaque bytes; `core/viewstate` and `docs/handoff/w62.md`).  `PlanDocument`
reads the CTable on load (`table_column_chars`), and `MainWindow` converts each
Entry column with MS Project's rule **`width = characters * 6 + 3`**:
24 chars → 147 px, 13 → 81, 17 → 105, 20 (the Add New Column stub) → 123.  The
stock character widths are the fallback when a file has no CTable for a field.
The status line reports the stored characters.

The splitter is the table's own right edge when the window can hold it: gutter
+ nine columns = 879 px, with the 6 px handle after it, which at 1920 is
881 = 0.45885 of the width.  The 0.459 fraction decides the split on a narrower
window, where the table clips exactly as Project clips it.

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
  brief puts pixel-exact ribbon icons out of scope).  Buttons are sized from
  their own labels, and a window too narrow for a group drops the whole group
  (in ribbon order) instead of clipping its labels.
* The task-bar colour is the shell palette: the current reference capture has
  no bars (docs/parity.md §2.3), so no bar colour is claimed.  Bar labels follow
  the product: a label that fits inside the bar (its text plus 4 px padding
  either side) is drawn centred inside it, otherwise it is drawn to the right
  and elided only at the canvas edge.
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
  reference: `python build/q3/mock-parity.py` then
  `python scripts/parity_report.py corpus/bench/msproject-gantt.png build/q3/qt-render-parity-mock.png`
  (git-ignored scratch; it passes every comparable measure at 1920x1032,
  including the nine column widths).
