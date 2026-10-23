# Qt shell handoff — wave 20 stream Q5 (structural cut)

**Status: first structural cut on disk. Qt compile + new gates pending coordinator
CI. Portable app gate green 15/15 locally.**

This cut implements build-order steps 1–2 of `docs/ui-parity-spec.md`: the empty
state and chrome structure, the Office themes, and the File backstage. It also
lands the measured header/filter/row menus and the generated command backlog.
The dialog set from the earlier brief is present (Preferences, Table/Date Format,
Custom Formula, Task Fields, Predecessors, Filter) with fixed geometry and a
per-screen gate, but the *full* dialog inventory of decision 14 is the next cut,
not this one.

The required portable gate was run exactly as briefed:

```sh
cmake -S app -B build\qt-app-w20 -G "Visual Studio 17 2022" -A x64 \
      -DCORE_BUILD_TESTS=ON -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build build\qt-app-w20 --config Release
ctest --test-dir build\qt-app-w20 -C Release -E plan_recalc --output-on-failure
```

Result **15/15 passed**. No `app/`, `core/`, `tests/data/`, `.github/` or
top-level CMake file was modified. No git command was run.

## What landed (build order 1–2)

| area | what |
|---|---|
| empty state | A new project opens on **today** (week containing today at the weekly zoom), not the 1984 epoch. The Entry header band is present with the nine measured columns/widths, the first empty row exists (Task Mode cell + gutter number), and the timeline Start/Finish block shows today. |
| ribbon | Project 2021 metrics: 30 px tab row, 95 px group row, 64 px button, **32×32 icon over a two-line label**, 14 px group caption, fixed separators. All seven tabs are populated. A styled File tab opens the backstage. The Tell-me box is at the tab row's right. A narrow window shows **one `>>` group-overflow chevron** (no wrap) and clips the last two tabs behind a tab chevron; the contextual Gantt Chart Format tab is shown only in the Gantt view. Every ribbon command carries our monochrome icon; unimplemented commands are disabled with the exact tooltip. |
| icons | `app_qt/shell_icons.cpp`: original QPainter glyphs, no Microsoft bitmaps. 32 px ribbon variants are the same 16 px drawings at 2×. |
| quick access | 24 px QAT above the ribbon with the shared Save/Undo/Redo QActions (same enablement as the menu). |
| title | The open document's name and path are in the window title; identity stays "MS Project Shell". |
| timeline | Full strip: vertical TIMELINE label, Start/Finish block (today for a new project), rounded timeline bar with the centred empty prompt, and task bars drawn at each task's fraction of the project window. |
| table | Entry headers always present (empty project included), frozen 60 px gutter with row numbers, no stretched/filler column (`stretchLastSection(false)`); the splitter stays free. Column header menu: Arial mini format bar (family, size, B/I/U, alignment) then Cut · Copy · Paste · Paste Special… · Wrap Text · Insert Column · Hide Column · Font… · Text Styles… · Field Settings · Custom Fields ▸ · Data Type ▸. Cut/Copy/Paste, Insert (reveals a hidden column) and Hide act; the rest are disabled with the backlog tooltip. |
| filter control | View ▸ Filter opens the combined drop-down: Sort Earliest→Latest · Sort Latest→Earliest · Group by: No Group · Clear Filter from Start · Filters ▸ · Select All · a checkable year ▸ months ▸ days date tree · OK / Cancel. OK applies the checked days as an Any-of-equals Start filter through `app::build_filter`; Cancel closes. |
| row menu | Task-row right-click: Cut · Copy · Paste · Paste Special… · Scroll to Task · Insert Task · Delete Task · Inactivate Task · Auto/Manually Schedule (reflects the row) · Assign Resources… · Fill Down · Clear Contents · Information… · Notes… · Add to Timeline · Link. Implemented items act; the rest are disabled. |
| status bar | "Ready" left; centred "New Tasks : Manually Scheduled"; four icon view buttons; zoom minus / slider / plus (no percentage). |
| backstage | File opens a full backstage: 11-entry left rail (Home, New, Open, Info, Save, Save As, Print, Share, Export, Close; **Options pinned at the bottom**) and 11 real pages. New/Open/Save/Save As/Print/Export/Close call the same MainWindow commands; Info carries honest local file facts; Share states there is no cloud/OneDrive integration; Options carries **Office Theme** (Options ▸ General) and a Preferences button. |
| themes | `app_qt/theme.cpp` swaps Office Light (parity reference) and Office Dark palettes through QApplication. The dark values are provisional (Office-dark family: window `#1f1f1f`, base `#262626`, text `#e6e6e6`, accent `#2f6ba8`). The theme is persisted under its own QSettings key and restored at launch; `--render` deliberately keeps the light default for deterministic artifacts. |
| disabled commands | `app_qt/command_status.h` is the single "implemented" list used by menus, the ribbon and the generator. Unimplemented commands are greyed with `not implemented in this port`. |
| backlog | `docs/ui-command-backlog.md` is **generated** by `python app_qt/tools/command_backlog.py` from `app/menu_model.cpp`, `app_qt/ribbon_bar.cpp` and `app_qt/command_status.h` (65 ribbon + 43 menu rows). Do not edit it by hand. |

## Per-screen structural gates

Each gate is offscreen (`QT_QPA_PLATFORM=offscreen`, `QT_SCALE_FACTOR=1`). They
assert presence/position/size/count/colour, never a golden hash. Pixel
difference reporting is still to come (decision 12).

| screen | test | asserted measures / values |
|---|---|---|
| Empty project 1920×1032 and 900×560 | `qt_empty_state` | timescale origin is the Sunday of the current week, never `1984-01-02`; today is inside the first week; first empty row = 1; 9 Entry captions and stock widths 39/57/147/81/81/81/105/105/123 with cumulative x; 42 px header; timeline vertical label at x=0, 22 px; Start/Finish block at x=30, width ≥160, height ≥24; bar after the block; four view buttons with icons + labels; zoom slider day..month; no percentage; QAT 3 actions; rotated 22 px view caption; manual-scheduling callout 24 px; ribbon 30/95/64/32 metrics, all groups at 1920, one `>>` chevron at 900, tab chevron at 900, no clipped label; backstage 11 rail entries with Options pinned; light and dark palettes; renders written at both sizes in both themes. |
| Loaded chrome | `qt_chrome_gate` | document name in the title; 60 px gutter paints numbers for all 3 rows; QAT 3 icons; four status view buttons with icons; zoom slider; centred scheduling mode; rotated view caption; callout hidden; column header menu order + Arial mini-bar + disabled Paste Special; filter drop-down sort/group/filter order + current-year date tree (12 months, day 1 checkable) + Cancel closes; row menu order + row-mode item + disabled Paste Special. |
| Dialog set | `qt_dialog_gate` | Preferences: 480×360, margins 12, spacing 8, five 120/280×24 form rows, fixed min=max, buttons on the bottom margin; theme/date/duration/zoom/shading round trip. Table/Date: 500×340, five rows, enabling/result. Formula: 520×280, two rows, seven fields, set/clear results. Task Fields: 480×400, six rows, one changed value = one edit. Predecessors: 640×420, list 140 px min, combos 220/90, lag 70, add queues one link. Filter: 520×300, three rows, Apply builds a definition, Clear reports cleared. Preferences apply/persist: ISO dates and hours durations change the loaded cells; the QSettings ini round-trips. |
| Resource Sheet + Usage | `qt_view_gate` | 60 px gutter, 42 px header, 22 px row pitch in every grid; Resource Sheet six columns ID/Resource Name/Type/Initials/Max. Units/Work at 50/140/77/77/70/77 px; Task Usage label 210, day period 98, Total 84; Week 126; Month 84; Resource Usage "Resource Name" … "Total"; chart pane hidden in grids; Task Sheet restores it. |
| Clipboard | `qt_clipboard` | Copy/Cut disabled with no document; enabled with a selection; Paste disabled until a block exists; 2×1 and 2×2 range paste is one undo step and Undo restores it; Cut copies then clears in one undoable transaction; single-cell paste works. |

The six pre-existing tests are untouched (`qt_render_smoke`,
`qt_render_document`, `qt_render_parity`, `qt_shell_offscreen`,
`qt_parity_geometry`, `qt_save_reopen`). The 9/9 numbers are unchanged: row 22,
header 43 (P1's two-rule measurement; our header band is 42), splitter
0.45924, day 24, tiers 2, columns `[38,57,147,81,81,81,105,105,124]` per P1's
delimiters, three colours. The chart pane now contains a 22 px rotated caption,
which shifts chart content inside the pane but not the sheet, splitter fraction,
day width or tiers.

## Dialogs (second cut)

The six dialogs above are implemented over the portable models and gated. Still
missing from decision 14: Task Information (six tabs), Project Information,
Change Working Time + Details, Options (13 pages), Timescale, Text Styles,
Gridlines, Bar Styles, Layout, Insert/Column Settings, Sort, Filter (the dialog
form; the drop-down now exists), Group By, Assign Resources, Leveling Options,
Page Setup, Report/Visual Reports/Compare Projects/WBS/Custom Fields. Policy for
the laid-out-only ones is decision 15: primary action disabled + inline note,
Cancel works, backlog row. The generated backlog currently lists unimplemented
*commands*; dialog backlog rows are added with that cut.

## Clipboard

Copy/Cut/Paste run through `core::sheet::SheetModel::copy_selection()` /
`paste()` on the selected rectangle: typed values only, no formatting. Copy and
Cut also put the TSV rendering on the system clipboard; Paste reads it back if
the internal block is empty. Paste is one atomic `UndoKind::Paste` step; Cut is
Copy plus one clearing transaction (`UndoKind::Transaction`, label "Cut"), so a
single Undo restores either. Range paste is supported by the model and the gate
proves 2×2; plain click selects one cell and Shift-click extends the rectangle.
The shared `app::menu_model` still marks Cut unsupported for the macOS shell, so
the Qt shell enables Cut locally in `update_menu_state()` with the same rule as
Copy.

## Deliberate gaps / next cut

1. **Loaded-file tables** (decision 7) still use the fixed Entry presentation.
   Column order/titles/widths from `214/CTable` are read for widths and the
   status line but do not rebuild the column set. This is build-order step 3.
2. **Dialog inventory** (decision 14) and the laid-out-only dialog policy
   (decision 15) are the second cut.
3. **Details pane / View ▸ Timeline toggle** (decision 16) are not wired.
4. **Dark theme on custom-painted surfaces**: the QPalette swaps and is gated,
   but `shell_style.h` light constants still paint ribbon/timeline/sheet chrome.
   Dark values need a theme-aware palette struct.
5. **Gantt bar right-click menu** was still missing from the operator's captures;
   not built.
6. **QAT customize, Page Setup, print preview, Office accelerator set, drag-fill,
   header Sort/Group actions, row Clear Contents** are disabled and in the
   backlog.
7. **Pixel-difference reporting** (non-gating, decision 12) is not in the test
   harness yet; the structural gates are the gate, and the dark/light renders
   are written for human review.
8. `qt_parity_geometry`'s ribbon checks still assert the old text-only button
   width rule; it remains true because the fixed width is still derived from the
   label. The gate now also checks the 32 px icon and 64 px height.

## Traps for the next stream

* `SheetHeaderView` opens its menu on `sectionClicked`; `setSectionsClickable(true)`
  is required. The menu is built on demand; gates must `delete` it (parent is the
  header).
* The `>>` overflow buttons are children of `page_`/`tab_row_`, **not** in the
  layout, so `clear_page()` does not delete them. Do not add them to
  `page_layout_`.
* `update_tab_overflow()` hides the last two tabs when the ribbon width < 1000.
  `tab_count()`/`tab_titles()` still report all seven modelled tabs.
* `Backstage` pages are indexed by `Page`; Info (3) and Options (10) are inserted
  into their slots before the widgets are added to the `QStackedWidget`. Adding
  one out of order breaks every nav mapping.
* `configure_application()` now applies `Theme::light`; any test that changes the
  theme must restore light if it later measures parity colours.
* `PreferencesStore` takes an explicit ini path in tests; never let the dialog
  gate write the real `QSettings` store.
* `command_status.h` has a `// UNIMPLEMENTED` marker: the backlog generator reads
  only the cases above it as implemented. Keep the marker and keep the list in
  step with `handle_action()`.
* `PlanDocument::clear()` + `MainWindow::new_project()` are the File ▸ New path;
  they do not touch disk.

## Next three actions

1. Dispatch Qt CI: compile all platforms with Qt 6.5.3, run the eleven CTest
   entries, and relay compiler diagnostics. Expect the first pass to find Qt API
   slips in `backstage.cpp`, `theme.cpp`, `sheet_views.cpp` (QMenu/QWidgetAction)
   and the new tests.
2. After green, implement build-order step 3 (file-driven `214/CTable` columns)
   and the dialog inventory (decision 14/15), reusing `app::Document` for
   view-state/formula writes.
3. Add the dark custom-painted palette + pixel-difference reporting (non-gating),
   then the Details pane and View ▸ Timeline toggle.

## CI needs

* No new Qt module: Widgets only (`QMenu`, `QWidgetAction`, `QTreeWidget`,
  `QStackedWidget`, `QSettings` are all in Qt6::Widgets/Core). PrintSupport is
  already linked.
* No new Linux package beyond the existing `qt6-base-dev` (and the offscreen
  platform plugin already used by the six old tests).
* New CTest entries: `qt_empty_state`, `qt_chrome_gate`, `qt_view_gate`,
  `qt_dialog_gate`, `qt_clipboard`. All use the committed
  `tests/data/synthetic-base.mpp` read-only; writes go to
  `build/test-out/` (renders, `prefs-gate.ini`).
* The Windows job must keep `/W4 /WX`; the new code avoids Q_OBJECT/AUTOMOC.
