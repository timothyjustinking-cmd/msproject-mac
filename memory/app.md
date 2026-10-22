# memory: app stream (W32 shell v1, W38 shell v2, W46 shell v3)

_2026-10-22. Owner: `app/**`, `.github/workflows/app-macos.yml`, `memory/app.md`.
No git commands were run: the files are on disk in the main checkout for the
coordinator to commit. Build tree used: `build\vs-w46` only._

Read this, then `app/README.md` (the user-facing account: menus, preferences,
filter, export, what persists on save, the date identity, verification), then
`app/main.mm` (the only ObjC++), `app/menu_model.h`, `app/prefs.h`,
`app/filter_model.h`, `app/export_pdf.h`, `app/selection_model.h`, `app/document.h`,
and `memory/sheet.md` / `memory/gantt.md` for the models the shell drives.

## State at the end of W46

| File | Contents |
|---|---|
| `app/layout_model.{h,cpp}` | v1 mapping + W38 additions + **new** `timescale_for_zoom(PlanInput, GanttZoom)` (zoom preference) |
| `app/selection_model.{h,cpp}` | `RowMap`, `selected_task`, `select_cell`, `select_from_gantt`, `row_after`, `toggle_row`, `scroll_target` + **new** `toggle_row_selection` (Space) |
| `app/document.{h,cpp}` | `collect_start_edits`, `default_save_name`, `save_start_edits`, `accept_start_edits` (unchanged) |
| `app/prefs.{h,cpp}` | **new**: `Preferences` (date style / duration style / zoom / shading), `serialize_preferences`/`parse_preferences` (tolerant), `apply_preferences`, `date_format_for`, `gantt_zoom`/`zoom_level_of`, `format_duration`, `cell_text`, `preferences_summary` |
| `app/menu_model.{h,cpp}` | **new**: `MenuAction` (30 values), `MenuItemSpec`/`MenuSpec`, `menu_specs()` (App/File/Edit/View/Insert/Help), `all_menu_actions`, `find_menu_item`, `MenuState`, `menu_item_enabled/supported`, `menu_disabled_reason`, `menu_action_name` |
| `app/filter_model.{h,cpp}` | **new**: `FilterField`/`filter_fields(sheet)`, `FilterOperatorChoice`/`filter_operators`, `FilterRule`, `FilterJoin`, `build_filter` (rule list -> `FilterDefinition`, All/Any), `describe_filter`, `filter_rows`, `filtered_record_set`, `restrict_to_filter` |
| `app/export_pdf.{h,cpp}` | **new**: `ExportRequest`/`ExportOutcome`, `export_report` (sheet + Gantt through `core/render`), `shading_calendar`, `export_subtitle`, `export_footer`, `export_default_name`, `kDurationTextField` |
| `app/main.mm` | v3: menu bar built by walking `menu_specs()` with one `performMenuAction:` switch; Preferences panel; filter alert; export panels; Save/Save As with a remembered target; dirty dot + Save/Don't Save/Cancel on close/quit; copy/paste; zoom; shading; Space row toggle; display-row drawing (filter-aware) |
| `app/prefs_tests.cpp` | **new**, 82 checks |
| `app/menu_tests.cpp` | **new**, 457 checks |
| `app/filter_tests.cpp` | **new**, 60 checks |
| `app/export_tests.cpp` | **new**, 58 checks (writes synthetic PDFs under `build\vs-w46\test-out`) |
| `app/CMakeLists.txt` | adds the four new sources to `app_layout`, adds `core/render` (with `RENDER_BUILD_TESTS=OFF`) and links `core_render`, registers the four new tests |
| `app/README.md` | rewritten for v3 (menus table, preferences + why app-side, filter, export, dirty prompts, shortcuts, verification counts) |
| `.github/workflows/app-macos.yml` | v3: asserts and runs **seven** tests; mentions core_render and the panels |

## The one invariant to preserve

**A filter restricts the DISPLAYED ROW LIST, never the RecordSet.** `app::RowMap`
(visible outline rows, restricted by `plan.filter_rows`) is what the sheet draws, what
the Gantt is built from (`make_gantt_source_for_rows`), what the keyboard walks and
what the hit test uses. Because the RecordSet is untouched, edits still write to the
same rows `app::collect_start_edits` diffs against `loaded`, and Save is unaffected by
a filter. The only copy of the data is `app::filtered_record_set`, used by the PDF
export and read-only. If a future change makes the filter remove rows from the
RecordSet, the save diff and undo history break.

After any filter/collapse change, `ensure_selection_visible()` moves the selection
onto a displayed row (arrow keys have nowhere to go otherwise), and the scroll target
uses `row_map.ordinal_of(row)`, not the outline ordinal.

## Preferences: measured boundary (do not "improve" this into the file)

`docs/analysis/oracle/w10-oracle.md` §4: Project writes the **table** date format into
`114/Props` (with `214/`/`Props14` churn) and exposes **no** global date-format,
duration-format or timescale setter. So the shell persists its four preferences in
`NSUserDefaults` under `com.msprojectdecompile.shell.preferences` as the one-line
string from `app::serialize_preferences`. Nothing is written into a `.mpp`. Writing
the per-table date format would need a Props decoder (not this wave).

- Date format: real `SheetFormat::date`; sheet, status line and PDF agree.
- Duration format: **app-side** rendering (`app::format_duration`/`cell_text`),
  because `core/sheet` owns no duration-unit switch; the PDF gets it through an
  app-private Text column (`kDurationTextField`) substituted for the Duration column.
- Timescale and shading: view state; the PDF Gantt gets shading-off as an
  **all-working calendar** (every day 00:00-24:00) with `adopt_calendar_week_start`
  false, since `core/render` always shades.

## Verification (last run 2026-10-22, MSVC 19.44, Release, /W4 /WX /permissive-)

```powershell
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul && cmake -S app -B build\vs-w46 -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w46 --config Release"
$ctest='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
& $ctest --test-dir build\vs-w46 -C Release --output-on-failure
```

Result: build exit 0 with no warnings; `ctest` **7/7**; checks per binary:
`layout 253`, `selection 90`, `document 43`, `prefs 82`, `menu 457`, `filter 60`,
`export 58` (1,043 in total). Configure printed `app: core/mppwrite/save.h found --
saves use mppwrite::save_date_edits`. `app_document` still writes only under
`build\vs-w46\test-out` and leaves `tests/data/synthetic-base.mpp` byte-identical;
`app_export` writes its four synthetic PDFs there too.

**Unverified: all of `main.mm`.** There is no ObjC++ compiler here. The macOS CI job
(`app-macos.yml`) is the only proof; it also now asserts the seven test names are
registered.

## Traps hit (W46)

- **The `edit` tool repeatedly failed with "file changed since it was read"** on this
  Dropbox folder (mtime churn), including immediately after a read. Several main.mm
  patches were therefore applied by `python` scripts under `build\vs-w46\` that
  `str.replace(old, new, 1)` with a `count == 1` assertion. That is a workaround, not
  a preference: use `edit` when it works, and re-read immediately before editing.
- **Objective-C string literals must not be concatenated with plain C literals**
  (`@"a" "b"`): clang warns `-Wobjc-string-concatenation`, which is an error in CI.
  Every continuation line of an `@"..."` must start with `@`. The C++ strings in
  `main.mm` (e.g. the no-fixture error) are a different thing and are fine.
- **`main.mm` cannot be compiled here at all.** All ObjC++ was written against the
  AppKit API signatures from memory: verify mentally, keep it mechanical, and let CI
  be the compiler. This wave added ~800 lines of ObjC++, which is the biggest risk in
  the tree right now.
- **Unused file-scope `constexpr` in an anonymous namespace is a warning**
  (`-Wunused-const-variable` under `-Wall`). `kPixelsPerDay` was removed when the zoom
  preference took over the timescale.
- **Menu actions must be exhaustive in the dispatch switch**: `app::MenuAction` has 30
  values and `performMenuAction:` covers all of them (checked with a script; a missing
  case is a `-Wswitch` error on clang).
- `README.md` counts/paths: the old README said `build\vs-w38` and 3 tests. It is now
  `build\vs-w46` and 7.

## Open, ranked

1. **Prove `main.mm` on macOS CI.** Expect any failure in the new AppKit: the menu
   walk (`installMenu`), `NSMenuItemValidation`/`NSMenuDelegate`, the Preferences
   panel's control construction, the filter `NSAlert` accessory view, `UTType` in the
   export panel, and `windowShouldClose:`/`applicationShouldTerminate:`.
2. **Manual smoke test on a Mac**: Cmd-O, Cmd-F (filter), Cmd-C/V, Cmd-1/2/3,
   Cmd-Shift-D, Preferences panel changes, Export PDF both kinds, edit a Start then
   Cmd-S, then close the window and check the Save prompt.
3. **Multi-rule filter UI** (the model already builds and tests All/Any).
4. **Real calendar shading** (`core/cal` -> `GanttSource::set_calendar`) and a
   `NSPrintOperation` if printing (as opposed to exporting) matters.
5. **Non-date saving**: Name/Finish/Duration still persist nowhere; the prompt and the
   README say so and must keep saying so until a writer API exists.


## W47 (wave 11): resource/usage views + computed columns

_2026-10-22. Owner paths used: `app/**`, `memory/app.md`, `docs/handoff/w47.md`.
Build tree: `build\vs-w47` only. No git commands were run._

### State at the end of W47

| File | Change |
|---|---|
| `app/view_model.{h,cpp}` | **new**. `ViewMode` (task_sheet/resource_sheet/task_usage/resource_usage), `standard_calendar()` (stock Mon-Fri 08:00-12:00/13:00-17:00), `usage_input_from_plan`, `resource_records_from_plan`, `make_resource_view_records` (registry schema + Type display name + Work), `resource_sheet_columns`, `UsageView`/`make_usage_view`/`make_usage_record_set`/`usage_sheet_columns`, app-private usage field ids, `FormulaColumnSpec`/`formula_column_specs`, `task_sheet_columns` (7 + 2 computed), `build_computed_columns`, `apply_computed_read_only`, `formula_column_index` |
| `app/layout_model.{h,cpp}` | `PlanInput` gained `resources`/`assignments` (`ResourceRow`, `AssignmentRow`); include `resource_tables.h` for `ResourceType`. Task path unchanged. |
| `app/reader_adapter.h` | `resource_row_from_mpp`, `assignment_row_from_mpp`, `resource_type_from_mpp`, `usage_input_from_document` (wraps `core::usage::usage_input_from_mpp`). Still the only app file including `core/mpp/reader.h`. |
| `app/prefs.{h,cpp}` | `Preferences` gained `view`, `usage_period`, `formula_duration_column`, `formula_date_column`, `stored_custom_fields`; serialized as additive keys (`view`/`usage`/`formula1`/`formula2`/`custom`); tolerant parser; `formula_column_visible`/`set_formula_column_visible`; `preferences_summary` names the view/period/count; `cell_text` now applies the duration-style preference to ANY Duration cell (so usage grids respect it). Version stays 1 (additive keys). |
| `app/menu_model.{h,cpp}` | 7 new actions (4 view modes, 3 usage periods) in the View menu; `MenuState::task_view` (default true) gates task-only commands (undo/redo, clipboard, Select All, zoom, filter, collapse, both PDF exports) while Save/shading/open stay view-independent; new `MenuAction` names and disabled reasons. |
| `app/main.mm` | control bar (`NSSegmentedControl` views + usage-period `NSPopUpButton`) above the scroll view in a container; `syncViewControls` + target/action; View menu dispatch for the 7 actions; `PlanState` gained view mode/period/usage input/calendar/projection/UsageView/secondary grid/computed columns; `rebuild_view` branches (task: row map + Gantt; other: `rebuild_secondary_view`); `draw_plain_grid` for the flat grids; task-only guards in mouse/key/undo/redo/copy/paste/select-all/export; Columns submenu uses `task_sheet_columns()` and adds a disabled "Stored custom fields (pending)" item; check marks for view/period/shading/zoom; status line per view; window title/print counts. |
| `app/views_tests.cpp` | **new**, 164 checks (registered as `app_views`); synthetic PlanInput only. |
| `app/menu_tests.cpp` | extended: four view actions with Cmd-Option-1..4, three usage periods, and `task_view=false` gating. 664 checks. |
| `app/CMakeLists.txt` | adds `core/usage` + `core/formula` (both `*_BUILD_TESTS=OFF`), `view_model.cpp` to `app_layout`, links `core_usage`/`core_formula`, registers the 8th test; version 0.4. |
| `app/README.md` | updated for v4 (views section, computed columns, preference keys, 8/8 and 1,414 checks, open items). |

### The invariants a successor must preserve

1. **Selection/editing belongs to the Task Sheet view only.** In the other three,
   `plan.row_map` is empty, `draw_plain_grid` draws every RecordSet row, and
   mouse/key/clipboard/undo/filter/collapse/export are refused. Do not give the
   secondary grids a selection without redesigning the hit-test (there is no RowMap).
2. **The secondary grid owns its RecordSet.** `core::sheet::SheetModel` holds a
   reference to the RecordSet it was built from; `PlanState::secondary_records` is a
   member for that reason, rebuilt in `rebuild_secondary_view`. `plan.records` (task)
   and `plan.secondary_records` are never the same object.
3. **The usage input comes from the reader, once per load**:
   `plan.usage_input = app::usage_input_from_document(document)` in `load_plan`.
   `app::usage_input_from_plan` is the equivalent pure builder for tests. The calendar
   is `app::standard_calendar()`; `core::cal` is NOT wired (documented choice: it
   re-opens the file and a wrong calendar silently changes every bucket).
4. **Computed columns are read-only and outside Save.** They are real PjFields
   (`Number1`, `Date1`) evaluated by `core::formula::ComputedColumns::evaluate` into
   `plan.records`, declared read-only by `task_sheet_columns()` and
   `apply_computed_read_only()`, re-evaluated in `refresh_computed_columns()` on every
   rebuild, and invisible to `collect_start_edits` (task Start only). Never write them
   through the sheet; never assume the reader populates them.
5. **The task sheet's column set is FIXED at 9** (7 default + 2 computed) on every
   load, because the dynamically built View > Columns submenu uses the model column
   index as its tag. `default_sheet_columns()` stays 7 for the existing tests/export;
   `task_sheet_columns()` is the shell's set.
6. **MenuState.task_view defaults to true** so every wave-10 enablement test still
   means what it did; the shell sets it from `app::view_mode_is_task`.

### Verification (last run 2026-10-22, MSVC 19.44, Release, /W4 /WX /permissive-)

```powershell
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake -S app -B build\vs-w47 -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON -DCORE_WARNINGS_AS_ERRORS=ON'
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\vs-w47 --config Release'
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && ctest --test-dir build\vs-w47 -C Release --output-on-failure'
```

Result: configure OK (prints `core/mppwrite/save.h found`), build exit 0 with
**no warnings**, `ctest` **8/8**. Checks per binary: `layout 253`, `selection 90`,
`document 43`, `prefs 82`, `menu 664`, `filter 60`, `export 58`, `views 164`
(**1,414 total**). `app_document`/`app_export` still write only under
`build\vs-w47\test-out`; `tests/data/synthetic-base.mpp` is untouched.

### Verified vs unverified

- **Verified on the Windows box**: `PlanInput` resource/assignment mapping; resource
  row count/fields/Type/Work and registry columns; usage day/week/month bucket sums,
  totals row/column and period labels; formula evaluation ([Duration]*2, [Start]+7) and
  read-only/no-save-edit; view-mode identifiers; preference round trip incl. a wave-10
  string; menu structure/enablement with task-view gating; all 8 binaries.
- **Unverified: every line of `main.mm`** (no ObjC++ compiler here). New AppKit surface:
  the container view + control bar (`NSSegmentedControl`, `NSPopUpButton`,
  `NSTextField` label, autoresizing masks), `syncViewControls`, the 7 menu dispatch
  cases, `draw_plain_grid`, and the per-view guards. macOS CI compiles it with
  `-Werror`; no runner opens a window, so interaction is by construction.

### Traps hit / learned (W47)

- **The `edit`/`write` tools repeatedly failed with "file changed since it was read"**
  on this Dropbox folder (mtime churn). Workaround used: read immediately before the
  edit, and for the big `main.mm`/`README.md` patches a throwaway Python script under
  `build\vs-w47\` doing `str.replace(old, new, 1)` with a `count == 1` assertion (32
  main.mm replacements, 16+4 README replacements). Nothing outside `app/**`,
  `memory/app.md`, `docs/handoff/w47.md`, `build\vs-w47` was written.
- **`core/usage` and `core/formula` are not in app/CMakeLists.txt by default.** They
  must be added to the `foreach` list (order: usage after sched, formula after sheet)
  with their `*_BUILD_TESTS=OFF` cache entries, or `view_model.cpp` fails to link.
- **`usage_mpp.h` includes `reader.h`**, so including it from `reader_adapter.h` keeps
  the "only one app file includes the reader" rule; do not include it from
  `view_model.cpp`.
- **`RecordSet` for a usage grid uses `RecordType::Assignment` and app-private field
  ids** (`0x0b4f0001/2`, periods at `0x0b4f1000+i`). They are display-only and never
  written; `field_title` would otherwise print `0x...`, so every column carries an
  explicit title.
- **`cell_text` broadening**: it now applies the duration preference to any
  `ValueKind::Duration` cell, not only the task Duration column. Existing prefs tests
  still pass, and this is what makes "8 hrs" work in the usage grids.
- **Menu check marks**: the Columns loop now skips separators and tag < 0 (the pending
  custom-field item) and disables column toggles outside the task view.
- **CI workflow (`app-macos.yml`) still says "seven tests"** in comments and greps only
  the seven wave-10 names; `ctest` runs all eight, so `app_views` does run. The file is
  outside W47's lane — the coordinator can update the wording/grep if desired.

### Open, ranked

1. **Prove `main.mm` on macOS CI.** Highest risk: the control bar/container, view
   switching, and the per-view drawing branch (all new AppKit).
2. **Manual Mac smoke test**: open a file, Cmd-Option-1..4 and the segmented control,
   change the usage period, resize the window, View > Columns on/off, Saved prefs after
   relaunch, Start edit + undo in Task Sheet only.
3. **Wire the file calendar** (`core::cal::load_project_calendar` → the one
   `app::standard_calendar()` call site) once a fixture proves Project's buckets.
4. **Stored custom-field values** through the reader (measured keys: Text1/Number1/
   Duration1/Date1 VarMeta, Flag1 in the FixedMeta tail) → the third chooser column.
5. **Wave 12**: task/resource insert/delete, resource/usage editing, resource cost/rates.

Next three concrete actions: (a) run `app-macos.yml` and fix compile warnings in
`main.mm`; (b) manually exercise the control bar and view switching on a Mac; (c) pick
up the `core::cal` calendar wiring with a fixture.
