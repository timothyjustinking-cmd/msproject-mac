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
