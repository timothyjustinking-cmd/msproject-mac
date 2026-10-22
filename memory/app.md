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

## W51 (wave 12): Insert/Delete Task, stored custom fields, the file's formulas

2026-10-22. Files: `app/edit_model.{h,cpp}` (new), `app/fields_model.{h,cpp}` (new),
`app/app_edit_tests.cpp` (new), `app/app_fields_tests.cpp` (new), and edits to
`app/layout_model.{h,cpp}`, `app/reader_adapter.h`, `app/prefs.{h,cpp}`,
`app/document.{h,cpp}`, `app/menu_model.{h,cpp}`, `app/menu_tests.cpp`, `app/main.mm`,
`app/CMakeLists.txt`, `app/README.md`. No `core/**` file was touched.

### What landed

1. **Insert/Delete Task.** `app::StructuralEdits` keeps the pending edits (insert: name +
   anchor uid + placeholder uid; delete: uid + the dependant count the dialog names) with
   its own undo/redo. `apply(loaded)` is the display plan (deleted tasks gone, dependent
   links/assignments gone like the writer's cascade, inserts placed after their anchor or
   at the end). `without_structural_edits(shown, loaded)` is its inverse for a rebuild, so
   a Start edit made before an insert is not lost. `save_structural_edits()` applies the
   list through `mppwrite::delete_task_row` / `add_task_row` and then the pending starts
   through `mppwrite::save_date_edits`; the app then RELOADS the target and adopts it
   (`PlanState::owns_path`), never patching a uid by hand. `add_task_row` appends, so the
   saved file lists the new row last (the save message says so).
   Undo rule: a structural edit ends the sheet's cell-edit history (the rebuild makes a
   new `SheetModel`), Cmd-Z prefers the sheet stack and falls back to the structural one,
   and a cell edit clears the structural redo list.
   Confirmation: `count_delete_dependants` = links naming the uid + assignments for it.
   Delete of a *pending insert* drops the insert instead of writing anything.
2. **Stored custom-field columns.** `TaskRow::custom_fields` carries
   `mpp::CustomFieldValue` (W48); `app::value_of_custom_field` maps Text/Number/Date/
   Duration/Flag, unmeasured (Cost, Flag2..20) and absent values are Null, never guessed.
   `make_record_set` adds every stored field to the schema and writes the values. Six
   columns (Text1/Number1/Number2/Date1/Duration1/Flag1) come from
   `mpp::custom_field_id(kind, index)` with descriptor captions, are read-only, start
   hidden (`Preferences::stored_custom_fields`) and format through the same
   `app::cell_text` the plan columns use (date/duration prefs, flags Yes/No).
3. **Formulas from the file.** `props_stream_from_file()` (`reader_adapter.h`, a second
   CFB read) -> `read_file_formulas()` (`core/props` W50, `[MSPJ<decimal>]` tokens
   kept) -> `formula_columns()` (file formulas first, the two demo columns only fill
   gaps) -> `core::formula::ComputedColumns`. A formula that does not parse or targets a
   non-Task field is reported and skipped. `[Start]` is stored as StartText
   (`0x0b400505`), which `core/formula` has no fallback for (only DurationText has one),
   so `make_record_set` fills StartText/FinishText from Start/Finish — inference, the
   token ids are measured.
4. **Preferences.** New `columns=0x0b400033:1,...` key: per-column overrides for the
   stored/formula columns (`ColumnVisibility`), only deviations from the default are
   stored; `custom=0|1` is the stored columns' default. Malformed entries are skipped.
5. **Menus.** `insert_task` is supported and enabled (Cmd-I); `Edit > Delete Task`
   (Cmd-Backspace) is new; `insert_resource` and `cut` stay permanently disabled.

### Invariants a successor must preserve

- **The opened file is never written.** `save_structural_edits` refuses
  `target == source` unless `allow_source_overwrite` (true only for a file this app saved
  and reloaded). `document_tests` still pins the refusal.
- **Reload, never patch.** After a structural save the plan/records/views come from the
  written file; no uid is edited in memory.
- **The start diff is by UniqueID** (`collect_start_edits`), not row index: a pending
  insert has no file row (skipped) and a pending delete is not displayed.
- **A structural edit is a rebuild.** Do not try to insert/erase RecordSet rows; the
  SheetModel is replaced (`build_editor`), which is why its history is cleared.
- **Computed/stored columns never reach a save.** `collect_start_edits` reads Start only;
  every custom/formula column is read-only.
- **`main.mm` is AppKit and compile-unproven here** — keep its addition calls behind the
  plain-C++ helpers, as the wave did.

### Verification (2026-10-22, MSVC 19.44, Release, /W4 /WX /permissive-)

```
cmd /c '"...vcvars64.bat" >nul && cmake -S app -B build\vs-w51 -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON -DCORE_WARNINGS_AS_ERRORS=ON'
cmd /c '"...vcvars64.bat" >nul && cmake --build build\vs-w51 --config Release'
cmd /c '"...vcvars64.bat" >nul && ctest --test-dir build\vs-w51 -C Release --output-on-failure'
```

`ctest` **10/10**, 0 failures. Checks: layout 253, selection 90, document 43, prefs 82,
menu 696, filter 60, export 58, views 164, **app_edit 120**, **app_fields 199**
(**1,765 total**). Configure prints `core/mppwrite/save.h found -- saves use
mppwrite::save_date_edits`; the build took the W37 path. `app_edit_tests` copies
`synthetic-base.mpp` into `build\vs-w51\test-out` and asserts the committed fixture's
size is unchanged; `app_fields_tests` reads `tests/data/synthetic-props.bin`.

### Verified vs unverified

- **Verified**: the pending-edit model and its undo/redo; `apply`/`without_structural_edits`
  placement and restoration; the dependant count; the real writer sequence on the fixture
  (insert -> 4 tasks, the writer's uid read back from the file and appended last; delete
  -> 3 tasks, the dependent assignment tombstoned, no dependants left naming the uid); a
  combined structural+start save (SNET constraint verified on re-read); refusals (empty
  edit list, empty target, source==target, unknown uid leave no file); in-place save of
  the app's own file; the stored-field conversion/formatting and the six columns; the six
  file formulas extracted from `synthetic-props.bin` and evaluated ([Duration]*2,
  [Duration]/480, [Duration]/2, [Start]+7, Name & "-" & Duration, IIf(%>50,1,0)); the
  column set/read-only rules; the per-column preference round trip.
- **Unverified: every line of `main.mm`** (no ObjC++ compiler on this box). New AppKit
  surface: `insertTask:` (NSAlert + NSTextField accessory), `deleteTask:` (NSAlert with
  the dependant count), `saveStructuralEdits:target:` (reload + window resize),
  `rebuildColumnsMenu`, `build_editor`/`apply_structural_change` call sites, the two undo
  handlers, and the `View > Columns` rebuild on load. `app-macos.yml` is the gate: it
  builds `main.mm` with `-Werror` and runs `ctest` (10 tests now). No runner opens a
  window.

### Traps hit / learned (W51)

- **The `edit`/`write` tools race Dropbox's mtime churn** ("file changed since it was
  read", even right after a read). Workaround: throwaway Python patch scripts under
  `build\vs-w51\` doing exact `str.replace(old, new, 1)` with a `count == 1` assertion and
  an all-or-nothing write; the scripts live only in the build tree. A multi-anchor script
  that reports misses must not write before checking every anchor (patch17 did; patch18
  fixed it by splicing two blocks between start/end markers).
- **`synthetic-base.mpp` has three ASSIGNMENT rows and ZERO link rows** (measured through
  this wave's test). The delete-cascade test therefore proves the assignment half on the
  file and the link half in the pure model (`make_plan()` with two links).
- **`mpp::Task::custom_fields` always carries Flag1** for a live row (a clear bit is a
  stored False), so a Flag1 column reads "No" on every row that has no flag set — that is
  the file's value, not a bug.
- **`[Start]` in `114/Props` is StartText (`0x0b400505`)**, not Task.Start
  (`0x0b400023`). `core/formula` has only a DurationText fallback; the app fills
  StartText/FinishText in `make_record_set`. A future core fix would make the app's two
  extra fields unnecessary (for now they are harmless aliases).
- **`add_task_row` appends and clones the file's last live row** (documented shape). The
  placeholder mirrors that (dates/duration copied from the last live task) but a cell the
  user types on the placeholder other than the name is not written; the save message says
  so.
- **The old `apply_formula_column_visibility` is gone**; `build_editor` applies
  `app::apply_column_visibility` and carries the ordinary columns' visibility over by
  field id, so a structural rebuild does not unhide a column the user hid.
- **`core/props` builds a `props_report` executable** when added to `app/CMakeLists.txt`
  (`PROPS_BUILD_TESTS=OFF` only skips its tests). Harmless, but it appears in the app
  build.

### Open, ranked

1. **Prove `main.mm` on macOS CI** — highest risk: `insertTask:`/`deleteTask:`/
   `saveStructuralEdits:target:`/`rebuildColumnsMenu` and the reload-after-save path.
2. **A Mac smoke test**: Insert Task with no selection and with a selection, Delete a task
   with dependants (count in the alert), undo/redo across a structural edit, Save (panel
   first time), reopen the saved file, View > Columns for the stored/formula columns, and
   the close prompt with both kinds of pending edit.
3. **Write custom fields** (`mppwrite::save_field_values` exists; no UI calls it) and,
   separately, a `114/Props` formula writer (deliberately absent).
4. **Row move / indent / outdent**: the writer can append and tombstone a row but cannot
   reorder or re-level one.
5. **`[Start]`-style text-token fallbacks in `core/formula`** would remove the app's
   StartText/FinishText aliases; `core/` is another stream's lane, so message the
   coordinator first.

Next three concrete actions: (a) run `app-macos.yml` and fix any `main.mm` compile
errors; (b) smoke-test the insert/delete/save/reload loop on a Mac; (c) wire an editable
stored custom-field column through `save_field_values` once the app has a place to put an
edit.

## W55 (wave 13): write custom fields, edit links

2026-10-22. Files: `app/field_link_edits.{h,cpp}` (new),
`app/app_edits_fields_tests.cpp` (new), `app/app_edits_links_tests.cpp` (new), and
edits to `app/edit_model.{h,cpp}`, `app/layout_model.h`, `app/reader_adapter.h`,
`app/menu_model.{h,cpp}`, `app/menu_tests.cpp`, `app/main.mm`, `app/CMakeLists.txt`,
`app/README.md`. No `core/**` file was touched.

### What landed

1. **One pending-edit stack, four more kinds.** `StructuralKind` now has `set_field`,
   `clear_field`, `add_link`, `delete_link` beside `insert_task`/`delete_task`;
   `StructuralEdit` carries the field id/kind/value and the link pred/uid/type/lag.
   `StructuralEdits::apply()` also sets/clears the displayed custom field, drops a
   pending-deleted link row by uid, and appends pending link adds (uid 0); the same
   undo/redo/list applies. `push()` validates the writer's types (finite number, valid
   packed date, duration != 0xffffffff, NUL-free text, measured field only), a
   self-link, an out-of-range lag, duplicate pending adds/deletes. Pushing a
   `delete_task` PRUNES the field edits and link adds naming that task (the save order
   would make them unreachable or instantly cascaded).
2. **`save_structural_edits` cascade.** Fixed order: link deletes
   (`delete_task_link`), link adds (`add_task_link`), task deletes
   (`delete_task_row`), task inserts (`add_task_row`), custom fields
   (one `save_field_values` for all sets, one `clear_field_value` per clear), then
   start dates (`save_date_edits`). Last-wins collapse per (uid, field id). Every
   writer call but the LAST writes `<target>.w51-structural-tmp`; an earlier refusal
   removes it and never writes the target. Result gained `links_added`,
   `links_deleted`, `fields_written`, `added_link_uids`.
3. **Dialog models (`app/field_link_edits.*`).** The six `TaskFieldSpec`s
   (Text1/Number1/Number2/Date1/Duration1/Flag1, descriptor captions), initial text
   through `format_value_text`, parsing through `parse_edit_text` + conversion to
   `mpp::CustomFieldValue`; blank = clear, unchanged = no edit, one bad field refuses
   the whole form. Predecessor list (id/name/type/lag/pending), `parse_link_lag`
   (`0`, bare days, `d`, `h`, `m`, signed), `format_link_lag`,
   `validate_link_add` (self/unknown/pending insert/pending delete/duplicate),
   `make_link_add`, `make_link_delete`.
4. **AppKit (unverified).** `Edit > Task Fields...` and `Edit > Predecessors...`
   (no key equivalents), each an `NSAlert` with an accessory view; both only queue.
   Status line/pending summary/close prompt name inserts, deletes, field edits and
   link edits; save message reports links added/deleted and field values written.
   `menu_model` enables both only in a task view with a selection and no open cell
   edit; `menu_tests` 696 -> 744 checks.
5. **LinkRow** gained `uid` and `lag_ticks`; `reader_adapter.h` fills them. Without
   the uid the dialog cannot address a link row (the writer deletes by uid).

### Invariants a successor must preserve

- **The opened file is never written.** Every save path still refuses `target ==
  source` unless the app itself wrote and reloaded it; structural/link/field saves
  reload and adopt (`PlanState::owns_path`).
- **Only the last writer call writes the target.** Do not reorder the cascade so a
  later step writes the target before an earlier one can fail.
- **Reader limitation on clear.** `clear_field_value` clears the FixedMeta presence
  bits but keeps the VarMeta entry; `core/mpp` lists any decodable entry, so a
  reloaded cleared Number/Duration reads 0 (Text "", Date 0xffffffff -> app Null).
  No `core/**` edit was made; a reader presence-bit decode is a coordinator decision.
- **A pending insert has no file row**, so field/link editing refuses it until saved.
- **`main.mm` is AppKit and compile-unproven here**; keep its logic in the helpers.

### Verification (2026-10-22, MSVC 19.44, Release, /W4 /WX /permissive-)

```
cmd /c '"...vcvars64.bat" >nul && cmake -S app -B build\vs-w55 -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON -DCORE_WARNINGS_AS_ERRORS=ON'
cmd /c '"...vcvars64.bat" >nul && cmake --build build\vs-w55 --config Release'
cmd /c '"...vcvars64.bat" >nul && ctest --test-dir build\vs-w55 -C Release --output-on-failure'
```

`ctest` **12/12**, 0 failures. Checks: layout 253, selection 90, document 43, prefs 82,
menu 744, filter 60, export 58, views 164, app_edit 120, app_fields 199,
**app_edits_fields 160**, **app_edits_links 159** (**2,132 total**). Configure prints
`core/mppwrite/save.h found -- saves use mppwrite::save_date_edits`. Build exit 0, no
warnings (`/WX`). The new tests write only under `build\vs-w55\test-out`
(`app-fields-*.mpp`, `app-links-*.mpp`) and assert the committed fixture's size is
unchanged.

### Verified vs unverified

- **Verified**: dialog specs/initial text/parse/clear rules; field write of all six
  through `save_field_values` and read-back from the file (Text1 "alpha", Number1
  42.5, Number2 3.25, Date1 0x3d1812c0 = 2026-10-26 08:00, Duration1 4800 ticks =
  480 COM minutes, Flag1 true); six clears through `clear_field_value` and the
  reader's empty values; a field + insert + start-pin save (4 tasks, SNET 4, field
  value) and its writer-call names; link 0 -> 2 adds (1->2 FS 0, 1->3 SS 4800),
  delete -> 1 live link, delete+re-add of the same relation in one Save (deletes run
  first), mixed delete+add (1->3 SS survives, 2->3 FF -4800 added); multi-op refusal
  leaves no target and no intermediate; unknown-uid field refusal leaves a
  pre-existing target byte-identical; target == source refusal; undo/redo and the
  display plan for field and link edits; delete-prunes behaviour.
- **Unverified: every line of `main.mm`** (no ObjC++ compiler on this box): the two
  `NSAlert` accessory dialogs (`taskFields:`, `editPredecessors:`), the
  `std::vector<NSTextField*>` under ARC, `[(NSTextFieldCell*)[list_label cell]
  setWraps:YES]`, `[NSTextField labelWithString:]`, the new menu dispatch cases and
  the four save/pending/close message changes. `.github/workflows/app-macos.yml`
  (coordinator's lane) builds it with `-Werror`; its test-name grep still lists only
  the wave-10/11 names, so it should be extended with `app_edits_fields` and
  `app_edits_links` (the tests are registered in CMake, so plain `ctest` runs all 12).

### Traps hit (W55)

- **`core::gantt::LinkType`'s enumerators are `finish_to_finish` / `finish_to_start`
  / `start_to_finish` / `start_to_start`** (order FS/SS/FF/SF), NOT
  `finish_finish`. The file order is FF=0/FS=1/SF=2/SS=3, so the writer needs an
  explicit mapping (`writer_link_type`), never a cast; the reader maps the other way.
- **`mpp::FieldKind` and `mppwrite::FieldKind` are different enums** with the same
  layout; `FieldValue::kind` needs `writer_field_kind()` (an explicit conversion, not
  an assignment).
- **`core/mpp` lists every decodable VarMeta custom-field entry** whether or not its
  FixedMeta presence bit is set, so "cleared" at the reader level is the empty value,
  not absence. Test the empty values, not `custom_field() == nullptr`.
- **The edit/write tools race Dropbox's mtime churn on this folder** ("file changed
  since it was read", repeatedly). Workarounds used: re-read the exact lines
  immediately before each `edit`, and for multi-anchor changes a throwaway Python
  script under `build\vs-w55\` that asserts every anchor's `count == 1` before writing
  (`patch_enum_names.py`, `patch_readme.py`).
- **`synthetic-base.mpp` has 0 link rows and 3 assignment rows.** The link round trip
  starts from zero links (add 2, delete 1); do not "fix" the fixture.

### Open, ranked

1. **Run `app-macos.yml`** and fix `main.mm`: the two dialogs' accessory views are
   the only new AppKit surface; the `NSTextField` multi-line list label and the
   `std::vector<NSTextField*>` ARC use are the likely first failures.
2. **Mac smoke test**: Task Fields on a stored row (set + clear a Text1/Flag1),
   Predecessors add/remove, undo/redo across a queued link, Save (panel first time),
   reopen, and the close prompt naming all four pending counts.
3. **Reader presence-bit decode for cleared custom fields** would make the app match
   Project's blank rendering; `core/**` is not this stream's lane.
4. A `114/Props` formula writer stays deliberately absent (no oracle fixture).
5. Row move/indent/outdent still blocked on the writer.

Next three concrete actions: (a) run the macOS CI job; (b) smoke-test the two dialogs
and the Save/reload loop; (c) add the two new test names to the workflow's grep.
