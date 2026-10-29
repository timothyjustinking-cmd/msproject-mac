# app/ — the macOS shell (AppKit) over the portable core

A native macOS application that opens a real `.mpp`, shows the tasks in a sheet on
the left and the Gantt on the right, and lets you select, edit, undo, filter,
collapse, export to PDF and save task start dates — behind a real menu bar and a
Preferences panel. Wave 11 adds a **resource sheet**, two **usage grids**
(task/resource, day/week/month) and read-only **computed formula columns**, all
switchable from the View menu and from a visible control bar. Wave 12 makes it a
real editor: **Insert Task / Delete Task** (pending structural edits the writer
applies on Save, then the app reloads the file), the **stored custom-field
columns** the reader decodes (Text1/Number1/Number2/Date1/Duration1/Flag1) and the
**formula columns defined by the file's own `114/Props`**. Wave 13 makes the custom
fields and the links editable through two dialogs: **Task Fields…** queues the six
measured field values (`save_field_values` / `clear_field_value`) and
**Predecessors…** queues link adds/deletes (`add_task_link` / `delete_task_link`);
both are applied by the one Save path, then the file is reloaded. Wave 14 adds
**printing**: File > Print… (Cmd-P) opens the standard print panel and File > Save as
PDF… (Cmd-Shift-P) runs the same `NSPrintOperation` as a save job, so the two use one
page layout (`app/page_layout.h`) and Export PDF uses the same page geometry. It also
warns before a Start save that Project will clamp a linked pin or reschedule a manual
task. Everything below `main.mm` is portable C++ that is built and tested on Windows
too.

This directory is a **standalone CMake project**. It is deliberately *not* added to
the repository's top-level `CMakeLists.txt` (owned by the build stream), it edits no
`core/` file, and it defines no core target of its own except `core_model` (which has
no `CMakeLists.txt` in-tree; every standalone core subdirectory already builds it the
same way).

| file | what it is |
|---|---|
| `CMakeLists.txt` | standalone project: options, core libraries with their tests off (including `core/props`), `app_layout`, twenty-five headless tests, AppKit target on `APPLE` only |
| `layout_model.h` / `.cpp` | **pure C++**: `mpp` packed-date → `date32` (identity, see below), task/link/resource/assignment rows → `RecordSet` + `SheetModel` columns, row → `SheetRowText`, `GanttSource`/`Timescale`/`GanttStyle`, `timescale_for_zoom`, plan summary, `sync_plan_from_records`, `make_gantt_source_for_displayed_rows`, `begin_cell_edit`, `apply_edit_policy` |
| `view_model.h` / `.cpp` | **pure C++ (wave 11)**: the four view modes, the stock Mon-Fri calendar, `PlanInput` → `core::view::ResourceRecord`s, the registry-backed resource-sheet columns, the usage grid (labels, period columns, a TOTAL row and a Total column), the two demo formula columns, and the read-only policy |
| `fields_model.h` / `.cpp` | **pure C++ (wave 12)**: the six stored custom-field columns from the field registry, `114/Props` → the file's formulas (`core/props` → `core/formula`), the sheet's complete column set (default + stored + formula, one column per field), read-only enforcement and the per-column visibility the preferences own |
| `edit_model.h` / `.cpp` | **pure C++ (wave 12/13)**: the pending edit stack — Insert/Delete Task, custom-field set/clear and link add/delete — with one undo/redo, the display plan and its inverse (so a rebuild keeps a cell edit), the dependant count the delete confirmation names, and the writer sequence (link deletes/adds, task insert/delete, `save_field_values`/`clear_field_value`, then `save_date_edits`) that a Save runs |
| `field_link_edits.h` / `.cpp` | **pure C++ (wave 13)**: the two dialog models — the six measured field specs, the initial text and parsing into the writer's types, the clear-vs-set rule; the predecessor list, the lag syntax (`0`/`2d`/`-1d`/`4h`), the add validation (self, duplicate, unknown, pending insert/delete) and the delete builder |
| `selection_model.h` / `.cpp` | **pure C++**: `RowMap` (display ordinal ↔ RecordSet row ↔ uid), `selected_task`, Gantt hit-test → sheet row, arrow-key navigation, `toggle_row_selection` (Space), `toggle_row`, `scroll_target` |
| `document.h` / `.cpp` | **pure C++ save helper**: diff the RecordSet against the loaded plan, then write the edited starts through `core/mppwrite` to a **new** file |
| `prefs.h` / `.cpp` | **pure C++**: the preference model, its persisted one-line string + tolerant parser, the date format applied to `SheetFormat`, the app-side duration rendering, `cell_text` |
| `menu_model.h` / `.cpp` | **pure C++**: the whole menu bar as data — items, key equivalents, `MenuAction` ids, enablement rules, and the reasons the unsupported items are greyed out |
| `filter_model.h` / `.cpp` | **pure C++**: filter fields/operators from the sheet, a criterion → `core::view::FilterDefinition` with an error message, matching rows, the displayed-row restriction, the printed subset |
| `export_pdf.h` / `.cpp` | **pure C++**: Export PDF through `core/render` — sheet and Gantt, the filtered rows, the preference formats, the shading-off calendar; `ExportRequest::page` is the same `PageSpec` printing uses |
| `page_layout.h` / `.cpp` | **new (wave 14), pure C++**: page size/margins, the page's vertical bands, rows per page (sheet and Gantt), page count, and the header/footer text; `build_print_job()` paginates once for both the printer and the PDF route |
| `start_edit_warning.h` / `.cpp` | **new (wave 14), pure C++**: `warning_for_start_edit()` — none / clamped_to_predecessor / manual_task_will_be_rescheduled / no_effect, with the clamped date — and the per-save report the shell shows as one `NSAlert` |
| `reader_adapter.h` | the one header that includes `core/mpp/reader.h`; `mpp::Document` → `app::PlanInput` (tasks, links, resources, assignments) and the measured `core::usage::usage_input_from_mpp` bridge |
| `layout_model_tests.cpp` | mapping, date identity (including two dates pinned by the spec), row texts, Gantt geometry, zoom; `corpus/probe` counts when present |
| `selection_model_tests.cpp` | two-way selection, Gantt clicks (bar and dependency), collapse, editing/undo/redo through `SheetModel`, the read-only policy |
| `document_tests.cpp` | `default_save_name`, the start-date diff, the **real save round trip** on `tests/data/synthetic-base.mpp` |
| `prefs_tests.cpp` | the identifiers, the parser (including bad/old strings), the three date styles, the three duration renderings, `cell_text`, the zoom mapping |
| `menu_tests.cpp` | six menus, every action present once per menu, unique key equivalents, the documented shortcuts, enablement rules, the three permanently disabled items |
| `filter_tests.cpp` | the field/operator lists, name/duration/date/blank criteria, two-rule All/Any, every error path, the row-list restriction, the printed subset |
| `export_tests.cpp` | real PDFs: sheet + Gantt, filtered, duration preference, determinism, the shading-off calendar, the name/subtitle/footer helpers, failures |
| `views_tests.cpp` | **wave 11**: resource rows/fields from a synthetic plan, usage day/week/month buckets + totals row/column, formula evaluation, computed columns read-only + never a save edit, view-mode and preference round trip |
| `app_edit_tests.cpp` | **new (wave 12)**: the pending edit model (placement, dependants, undo/redo, the display-plan inverse) and the real writer sequence on a copy of `synthetic-base.mpp` under the build tree: insert/delete counts, the uid the writer allocated, tombstoned dependants, a combined start+structural save, and the refusals with no target / same file / unknown uid |
| `app_fields_tests.cpp` | **wave 12**: the stored custom-field ids from the registry, decoded values → `RecordSet`, the cell formatting (dates/durations honour the preference, flags Yes/No), the column set and its read-only rule, `114/Props` → the six formulas of `tests/data/synthetic-props.bin`, their evaluation, and the per-column preference round trip |
| `app_edits_fields_tests.cpp` | **new (wave 13)**: the Task Fields dialog model (specs, initial text, parse/clear rules, push validation, display plan, undo/redo) and the real field round trip on a copy of `synthetic-base.mpp`: six values written via `save_field_values`, re-read; six clears via `clear_field_value`; a field + insert + start-pin save; refusals leaving the target untouched |
| `page_layout_tests.cpp` | **new (wave 14)**: rows per page for a 3-row and an 83-row plan (sheet and Gantt), page count, header/footer strings, the printer/PDF seam (`build_print_job` returns identical pages for both routes), and a cross-check that Export PDF's `core/render` page count equals the shared layout |
| `start_edit_warning_tests.cpp` | **new (wave 14)**: predecessor-finish clamp, pin-later no-warning, manual-task warning, unlinked none, unknown/cleared/unchanged/summary no-effect, FS-only and lag, the message text and the per-save report |
| `app_edits_links_tests.cpp` | **new (wave 13)**: the Predecessors dialog model (lag syntax, type names, listing, validation, display plan, undo/redo) and the real link round trip on a copy of `synthetic-base.mpp`: two adds (0 → 2 links), one delete (2 → 1), delete+re-add in one save (deletes run first), a mixed delete+add, and a two-op refusal that leaves no target |
| `main.mm` | Objective-C++ / AppKit: window, control bar (view switcher + usage period), menu bar built from `menu_model`, Preferences panel, filter dialog, save/export panels, dirty prompts, clipboard, keyboard, drawing (task sheet + Gantt, or a flat resource/usage grid) |
| `../.github/workflows/app-macos.yml` | the macOS CI job (push, tag or dispatch since wave 33): configure `app/`, build everything with `-Werror`, compare `ctest -N` against the `add_test(NAME ...)` list in `CMakeLists.txt`, run every registered test, prove the bundle exists and links AppKit + UniformTypeIdentifiers |

## Build and run

macOS (the only place `main.mm` compiles):

```sh
cmake -S app -B build-app -G "Unix Makefiles" \
      -DCMAKE_BUILD_TYPE=Release -DAPP_BUILD_TESTS=ON -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build build-app --config Release
ctest --test-dir build-app -C Release --output-on-failure --timeout 120
open build-app/msproject_app.app                 # or:
build-app/msproject_app.app/Contents/MacOS/msproject_app /path/to/file.mpp
```

Windows (headless tests only — there is no ObjC++/AppKit here, so a non-Apple
configure skips the app target and must still succeed):

```bat
set vc=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat
call "%vc%"
cmake -S app -B build\w61-app -G "Visual Studio 17 2022" -A x64 \
      -DCMAKE_BUILD_TYPE=Release -DAPP_BUILD_TESTS=ON -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build build\w61-app --config Release
ctest --test-dir build\w61-app -C Release --output-on-failure
```

Never `--parallel` on this box (see `docs/build.md`). A transient
`MSB6003`/`C1083` is the known Dropbox/AV file-lock: retry the build once. Building
`app/` compiles `core/` from source, so another stream's half-written file fails the
app build exactly as it fails the root build: retry, do not "fix" their file.

Options: `APP_BUILD_TESTS` (default `ON`), `CORE_WARNINGS_AS_ERRORS` (default `OFF`;
the workflow passes `ON`, which also reaches the core subdirectories).

## The menu bar

`app/menu_model.cpp` is the single description of the menu bar; `main.mm` only walks
it (one `NSMenuItem` per `MenuItemSpec`, `tag` = the `MenuAction`) and dispatches by a
switch on that tag. `app/menu_tests.cpp` pins the structure, the key equivalents and
every enablement rule, which is the only way to test a menu bar without a window.

| menu | items |
|---|---|
| MS Project Shell | Preferences… (Cmd-,), Hide (Cmd-H), Quit (Cmd-Q) |
| File | Open… (Cmd-O), Save (Cmd-S), Save As… (Cmd-Shift-S), Print… (Cmd-P, **wave 14**), Save as PDF… (Cmd-Shift-P, **wave 14**), Export PDF (Sheet)… (Cmd-Shift-E), Export PDF (Gantt)… (Cmd-Option-E), Close (Cmd-W) |
| Edit | Undo (Cmd-Z), Redo (Cmd-Shift-Z), Cut (Cmd-X, **disabled**), Copy (Cmd-C), Paste (Cmd-V), Select All (Cmd-A), Delete Task (Cmd-Backspace, **wave 12**), Task Fields… (**wave 13**), Predecessors… (**wave 13**) |
| View | Task Sheet (Cmd-Option-1), Resource Sheet (Cmd-Option-2), Task Usage (Cmd-Option-3), Resource Usage (Cmd-Option-4), Usage Period: Day/Week/Month, Zoom In (Cmd-=), Zoom Out (Cmd--), Day/Week/Month Timescale (Cmd-1/2/3), Working-Day Shading (Cmd-Shift-D), Filter… (Cmd-F), Clear Filter (Cmd-Shift-F), Collapse All, Expand All, Columns ▸ (rebuilt per load: the seven default columns, the stored custom fields and the file's formula columns) |
| Insert | Task… (Cmd-I, **wave 12**), Resource… — Resource is permanently disabled |
| Help | Keyboard Shortcuts, About MS Project Shell |

**Greyed out rather than faked.** Wave 12 enabled `Insert > Task` (it records a
pending row; the writer appends it on Save) and added `Edit > Delete Task` (the
writer tombstones the row and cascades into its link/assignment rows). What is still
disabled is `Insert > Resource` — the resource sheet is read-only and there is no
resource writer — and `Cut`, which needs a `SheetModel` operation that does not exist
(copy and paste do). Both are in the menu, disabled in every state, with
`menu_disabled_reason()` as their tooltip and the reason pinned in `menu_tests.cpp`.

## What the window does

One window, one `NSScrollView`, one document view.

**Open.** `argv[1]` at launch; otherwise the first of `corpus/probe/{w3-o1,holiday1,
base}.mpp`, then `tests/data/synthetic-base.mpp` (committed). `File > Open…` (Cmd-O)
shows an `NSOpenPanel` filtered to `.mpp` through `UTType`. A file that does not
parse raises an alert with the reader's message and leaves the previous state
cleared rather than half-loaded. The window title is **project title + full path**
(both from the file — no project or task name is ever hardcoded), and the title bar
shows the standard edited dot while start-date edits are pending.

**Status bar** (two lines at the bottom): task/link counts (plus skipped links),
summary/milestone/critical counts, the plan's start–finish span **recalculated from
the current rows**, the number of edited starts, the active filter (with *shown of
total*), the preference summary, the reader's warning count and the file path; then
the last action, in red when it was a rejected edit or a failed save/export/open.

**Selection, two ways.** Clicking a sheet cell selects it; clicking a Gantt bar (or a
dependency) selects the sheet row of that bar (the successor, for a link — a sheet
selects one row); Shift-click extends the range; arrows move through the **displayed**
rows and columns; **Space** toggles between the single cell and the whole row.
Selecting a row highlights its Gantt bar (outlined) and the row band in both views,
and scrolls it into view (by displayed position, so a filter cannot point the scroll
at the wrong row). `SheetModel` owns the selection; the mapping lives in
`app::RowMap`/`selected_task` and is unit-tested.

**Editing.** Double-click or Return starts an edit seeded with the cell text; a
printable character starts an edit that *replaces* the text (as Project does);
Backspace edits the text, or clears the cell when no edit is open; Return commits;
Esc cancels; Tab commits and moves right. A rejected commit (`EditOutcome::error` —
a bad date, a read-only column) keeps the session open and shows the message in the
status bar. `Cmd-Z` / `Cmd-Shift-Z` are menu items over `SheetModel::undo/redo`, so
edits and undos re-derive the Gantt and the status line through
`app::sync_plan_from_records`. UID, ID and Outline number are declared read-only by
`apply_edit_policy` (the sheet does not infer that); Name, Start, Finish and Duration
are editable.

**Clipboard.** Cmd-C copies the selection as tab-separated text through the general
pasteboard (`ClipboardBlock::to_text`), Cmd-V pastes it (`from_text` plus
`SheetModel::paste`, one undo step, refused cells reported in the status line). Cut is
disabled.

**Outline and columns.** A summary row shows a `-`/`+` box in the Name column's
gutter; clicking it (or Cmd-Left / Cmd-Right on a selected summary) calls
`SheetModel::toggle_collapsed`. The visible-row list is then re-read and the Gantt is
**rebuilt** from exactly those rows. View ▸ Columns toggles each column's visibility
through `SheetModel::set_column_visible`. The column set is the seven task columns, then
the stored custom fields and the file's formula columns; the View ▸ Columns submenu is
rebuilt per load (`rebuildColumnsMenu`) because the file's formulas change the set, so
its `tag` is the current model column index.

**Views (wave 11).** A segmented control and a Usage period popup sit in a control bar
above the scrolling document; the View menu has the same commands (Cmd-Option-1..4 for
the views). The choice and the period are remembered in the preferences and selected
again on the next load.

| view | what it shows | selection/editing |
|---|---|---|
| Task Sheet | the task sheet + Gantt, as before | yes (the only view with a Gantt) |
| Resource Sheet | one row per live resource, the file's stored Entry table (ID gutter, Indicators, Name, Type, Material Label, Initials, Group, Max. Units, Std. Rate, Ovt. Rate, Cost/Use, Accrue At, Base Calendar, Code), plus Work | none (read-only) |
| Task Usage | one row per task, one column per day/week/month period, a Total column and a TOTAL row | none (read-only) |
| Resource Usage | the same grid per resource | none (read-only) |

The resource rows and columns come from `core/view/resource_tables.h`:
`build_resource_record_set()` supplies the stored Entry fields (ID, Name, Type,
Material Label, Initials, Group, Max. Units, Std. Rate, Ovt. Rate, Cost/Use, Accrue At,
Base Calendar, Code) and Project's computed PeakUnits/Overallocated. The shell shows
Type and Accrue At as their product words, the measured "$"-formatted rate and
Cost/Use text, and adds **Work** (the resource's assignment total, in duration ticks,
from `core::usage`). A loaded file's columns are its own stored `214/CTable` resource
table (`app::resource_sheet_columns(const TableDef&)`): the stored widths and captions,
with ID hidden because Project draws it as the row-number gutter; a file without one
falls back to the registry Entry table (`find_resource_table("resource-entry")`). An
overallocated resource is painted with the view's slot-3 red bold style (wave 31).

The two usage grids are `core::usage::project_usage()` rows rendered as a sheet:
one label column, one column per period (day, week or month), a **Total** column and a
final **TOTAL** row of period totals. Values are stored as duration ticks, so the
Duration-format preference applies ("8 hrs" in hours mode, "1 day" in auto mode).
Period labels are `Mon 19/10/26`, `Wk Mon 19/10/26` and `Oct 2026`; a week period is a
clipped run of days (core/usage decision 1: weeks start on Sunday). The period list
covers the assignments that can be phased; work that cannot be placed stays in the row
and table totals and is counted in the status line, exactly as `core/usage` documents.
**The calendar is the stock Monday-Friday 08:00-12:00/13:00-17:00 week**
(`app::standard_calendar()`), not the file's own calendar: `core::cal` is not wired this
wave (see Open), because it re-opens the `.mpp` and a wrong calendar would silently
change every bucket.

**Custom-field columns (wave 12).** The task sheet shows the custom fields the reader
actually decodes, all read-only:

| column | field | where the value comes from | formats as |
|---|---|---|---|
| Text1 / Number1 / Number2 / Date1 / Duration1 / Flag1 | the registry ids `mpp::custom_field_id(kind, n)` (Text1 `0x0b400033`, Flag1 `0x0b400048`, …) | `mpp::Task::custom_fields` (W48's measured storage) → `RecordSet` | text / number / number / the date preference / the duration preference / `Yes`-`No` |
| the file's own formulas | the field each formula is defined for | `114/Props` → `core/props` (W50) → `core/formula` | the computed value's kind |
| Duration x2, Start +7 Days | `Number1`, `Date1` | the app's two demo formulas, used only for a field the file does not define | Duration, Date |

Six stored columns would crowd the sheet, so they start **hidden** and are toggled
from View ▸ Columns (the choice is per column and persists). A formula column replaces
the stored column for the same field — one column per field, and the value shown is the
one Project's own formula would compute.

The **file's formulas** are read on load through `core/props` (the six blocks the
wave-10 oracle measured), mapped to their PjField ids and registered in
`core::formula::ComputedColumns`; a formula that does not parse or names a non-Task
field is reported in the status line and skipped, never evaluated as a partial guess.
`[Duration]` is stored as `[MSPJ188744967]` (DurationText) and `[Start]` as
`[MSPJ188744965]` (StartText); `core/formula` falls DurationText back to the Duration
value, and the app fills StartText/FinishText from Start/Finish, so `[Start]+7` evaluates
against the date. Both mappings are documented as inference: the *tokens* are measured,
the fallback semantics are not.

Every custom/formula column is declared read-only (`begin_edit` refuses it) and is
outside the save diff (`collect_start_edits` reads task starts only), so **Save can never
write a stored or computed value**.

**Insert and Delete Task (wave 12).** `Insert ▸ Task…` (Cmd-I) asks for a name and
records a **pending insert** anchored on the selected row (or at the end); `Edit ▸
Delete Task` (Cmd-Backspace) records a **pending delete** after a confirmation that
names how many dependent link/assignment rows the writer's cascade will tombstone (the
count is `app::count_delete_dependants`, links plus assignments). A **summary row is
refused before the confirmation** (wave 33, A-3): tombstoning it alone would leave its
live subtasks with a parent identity pointing at a tombstone, a shape no Project save
pair measured, so the status line says "delete the subtasks first, or outdent them".
Neither touches the
file until Save: the sheet and Gantt are rebuilt from `PlanState::base`, the loaded plan
with the pending edits applied (`app::StructuralEdits::apply`), so a new row appears at
once (a placeholder carrying the name and a copy of the file's last row's dates — the
writer's documented add shape) and a deleted row disappears together with its links and
assignments. Deleting a row that is still a pending insert simply drops the insert.

The two undo stacks are kept chronological: **a structural edit ends the sheet's
cell-edit history** (the rebuild replaces the `SheetModel`), and Cmd-Z first undoes
whatever the sheet still has, otherwise the last structural edit; a cell edit clears the
structural redo list. The trade-off is documented rather than hidden: cell edits made
before a structural edit are no longer undoable afterwards (their *values* survive — the
rebuild folds the current cell values back through
`StructuralEdits::without_structural_edits`, so a pending Start edit is not lost).
That inverse also gives an **undone** percent edit back to the file: `apply()` does
not replay an edit that has been undone, so the display plan must not keep the
percent it wrote. `without_structural_edits` restores the file's stored
`percent_complete` for every task a `set_percent` or `update_actuals` edit in the
redo list touched (W28-4.1), so the sheet — and the Qt shell's Task Form, which
reads the same plan — never shows a percent the file does not have.

On Save the writer applies the whole set: each delete through
`mppwrite::delete_task_row` (the task plus its link/assignment cascade), each insert
through `mppwrite::add_task_row` (the writer allocates the uid and clones the last live
row), and then any pending start dates through the normal
`mppwrite::save_date_edits` path — one Save, one file. The app then **reloads the target
and adopts it** (`plan.owns_path`), because the added rows' real uids and ids are the
file's; a uid is never patched by hand. The source file is still never modified: the
structural save refuses `target == source` unless this app wrote the target itself (the
reload case), and a failure leaves the opened file untouched.

In the resource/usage views the task-only commands (undo/redo, clipboard, Select All,
zoom, filter, collapse, PDF export) are disabled rather than acting on the hidden task
sheet; Save stays available because it still writes task starts.

**Custom fields and predecessors (wave 13).** Two Edit-menu commands queue edits
the Save path can write; the grid columns stay read-only.

| command | what it does | writer calls |
|---|---|---|
| Edit ▸ Task Fields… | One form for the six measured custom fields Text1, Number1, Number2, Date1, Duration1, Flag1, prefilled from the task's stored values. A blank field clears the stored value; any other value is parsed with the sheet's own parser (so "3d", "2026-10-26" and "Yes" behave as in a cell) and converted to the writer's types: finite f64, packed date word, file ticks (COM minutes × 10), bool. | `mppwrite::save_field_values` (one file for all set values) and `mppwrite::clear_field_value` (the measured empty-value shape, one call per cleared field) |
| Edit ▸ Predecessors… | Lists the selected task's incoming links (predecessor ID/name, FS/SS/FF/SF, lag) and can delete a file link and/or add one (predecessor ID + type + lag). Lag syntax: `0`, `2` (days), `2d`, `-1d`, `4h`, `30m`. Refused before queueing: a self-link, an unknown endpoint, a pending-insert endpoint, a pending-deleted endpoint, and a duplicate (predecessor, successor, type). | `mppwrite::delete_task_link` then `mppwrite::add_task_link` |

Both dialogs only **queue** an edit. It appears in the sheet/Gantt at once
(`app::StructuralEdits::apply` sets the field value and adds/removes the displayed
link), participates in the same undo/redo stack as Insert/Delete Task, lights the
edited dot, and reaches a file only on Save. The Save sequence is fixed: link
deletes, link adds, task deletes, task inserts, field values, then start dates — all
on one document, and every step but the last writes an intermediate file, so an
earlier refusal leaves the target untouched. The app then reloads and adopts the
file exactly as it does for a structural save. Pushing a task delete drops the field
edits and link adds that name it (they could never reach the file).

**A cleared value and the reader.** `clear_field_value` keeps the field's VarMeta
entry and points it at the measured empty block, clearing the FixedMeta presence
bits (W52). `core/mpp` lists a VarMeta entry it can decode, so after a clear the
reloaded row still reports Text1 `""`, Number 0, Date `0xffffffff` and Duration 0;
the app maps the not-set date to Null (blank) and the Task Fields form treats a
blank input as clear again. Project itself shows the field blank; the empty shape is
what the wave-13 tests prove from disk, not a second reader rule.

**Zoom.** View ▸ Zoom In/Out steps through the three core zoom levels (day → week →
month) and View ▸ Day/Week/Month sets one; the Gantt is rebuilt through
`app::timescale_for_zoom`, which takes the unit and the pixels per unit from
`core::gantt::zoom_spec()` and seeds the origin from the plan. The choice persists.

**Filter.** View ▸ Filter… (Cmd-F) opens an alert with column / operator / value
controls and Apply / Clear / Cancel. The criterion is parsed with the same parser the
cell editor uses (`parse_edit_text`), so a Duration thinks in "3d" and a Date in
"2026-10-20"; a value the column cannot parse is refused with the field named.
Applying a filter **restricts the displayed row list** (`app::RowMap`), never the
`RecordSet`: the same rows stay editable, the save diff is unchanged, the Gantt draws
only the matching bars, and the status bar says *shown of total*. A filter with no
match shows an empty sheet and a red status line, not an error dialog.
`app/filter_model.h` also builds All/Any trees of several rules; this wave's dialog
authors one criterion, and the model's multi-rule path is tested.

**Export PDF.** `File > Export PDF (Sheet)…` / `(Gantt)…` write through `core/render`
(`render_sheet_report` / `render_gantt_report`) to a path you choose (default
suggestion `<name>-sheet.pdf` / `<name>-gantt.pdf`). The report is built from the
window's own state: the **filtered rows**, the **visible columns**, the **preference
date format**, and — for the Gantt — the timescale level and the working-day shading
preference. The page geometry (`ExportRequest::page`, an `app::PageSpec`) is the
same one `app/page_layout.h` uses for printing, so the two cannot drift: the defaults
are `core/render::ReportStyle`'s own, so wave-13 PDFs are byte-identical. The current
**selection** is named in the report footer; `core/render`
has no selection concept and this wave does not edit `core/`, so the selected row is
*not* highlighted on the page, and the README says so rather than implying otherwise.
Shading off is implemented by handing the renderer an all-working calendar (every day
00:00–24:00, so no non-working span exists) with the week anchor pinned, which
changes nothing else: shading never moves a bar.

**Printing.** `File > Print…` (Cmd-P) builds an `app::PrintJob` with
`app::build_print_job()` (A4 landscape, 24 pt margins, 36 sheet rows / 35 Gantt rows
per page) and runs an offscreen `PrintView` through `NSPrintOperation`. The view draws
the same table + Gantt chart the window shows, one page per `-rectForPage:`, with the
project name in the header and the date plus "Page i of n" in the footer.
`File > Save as PDF…` (Cmd-Shift-P) is the same job and the same view with an
`NSPrintSaveJob`, so the PDF paginates exactly like the printout. `File > Export PDF`
stays: it writes through `core/render` on the same `PageSpec`, and
`page_layout_tests.cpp` proves the page counts agree. Printing is available in the Task
Sheet view only (the resource/usage grids have no report layout yet).

**Preferences.** Cmd-, (or the app menu) opens a panel with four controls:

| control | values | what it changes |
|---|---|---|
| Date format | `Tue 20/10/26` (day/month/year, default), `Tue 10/20/26`, `2026-10-20` (ISO) | `SheetFormat::date`, so every date cell, the status-line span and the PDF report agree |
| Duration format | days + hours (default: `3 days`, `4 hrs`), hours only (`24 hrs`), days only (`0.5 days`) | the shell's Duration column rendering (`app::cell_text` / `app::format_duration`) in the sheet and in the PDF; the sheet's own parser still accepts `3d`/`4h` etc. |
| Timescale | Days (default), Weeks, Months | the Gantt's timescale unit/width |
| Working-day shading | on (default), off | whether the Gantt draws non-working spans and weekend header tints; the PDF Gantt uses the all-working calendar when off |

The **view** and the **usage period** are set from the control bar / View menu, not
from this panel; they persist the same way (below) and the next load opens in the
chosen view. The two visible computed columns are toggled from View ▸ Columns and also
persist.

Applying a preference repaints immediately; closing the app is not needed.

### Where the preferences persist — and why not in the file

They are stored **app-side**, in `NSUserDefaults` under
`com.msprojectdecompile.shell.preferences`, as one string produced by
`app::serialize_preferences`
(`version=1;date=...;duration=...;zoom=...;shading=...;view=...;usage=...;formula1=0|1;formula2=0|1;custom=0|1;columns=0x0b400033:1,0x0b400057:0`).
The wave-11/12 keys are additive: an older string without them parses and keeps the new
defaults. `custom` is the default visibility of the six stored custom-field columns and
`columns` records a per-column override (only a choice that differs from its default, so
the line stays short); a malformed entry is skipped, never fatal.
`app::parse_preferences` is deliberately tolerant: an unknown key is ignored, a bad
value keeps the default, and a missing key keeps the default, so an old or
hand-edited defaults string can never stop the app from launching.

That is a measured decision, not laziness. `docs/analysis/oracle/w10-oracle.md` §4
measured Project's own stores: **the table's date format is a property of the table
definition and is written into `114/Props`** (with Project's own `214/`/`Props14`
churn), and there is **no exposed setter for the global date format, duration format
or Gantt timescale** (`Application.DateFormat`/`DurationFormat` are conversion
functions, `Application.Timescale`/`Options*` are dialog methods). Writing the
per-table date format through `114/Props` would need a Props decoder this wave does
not have, so **nothing about a preference is written into a `.mpp`** — the app does
not fake file persistence. Working-day shading and the timescale are view state, and
the duration rendering is the shell's own.

## Dirty state, save and close prompts

The document is **dirty exactly when a save would write something**: when a task's
Start in the RecordSet differs from the file as loaded
(`app::collect_start_edits`, matched by UniqueID since wave 12) **or** when an
Insert/Delete Task, custom-field or link edit is pending. Those are the edits
`core/mppwrite` can persist, so they are what lights the window's edited dot — a
renamed task or a changed Finish does not claim unsaved file changes. Clicking the
close box (or Quit) with any of them asks **Save / Don't Save / Cancel**: Save goes
through the normal save path (a cancelled panel or a writer failure keeps the window
open), Don't Save discards, Cancel aborts the close. The prompt names how many start
edits and how many insert/delete, custom-field and link edits are pending and says the
other edits are view-only.

`File > Save` (Cmd-S) writes to the **last save target** (`<name>-edited.mpp` until
you choose another path); the first Save, and every `Save As…` (Cmd-Shift-S), shows an
`NSSavePanel`. With any pending edit it runs `app::save_structural_edits` (link
deletes/adds, task insert/delete, custom fields, then the start dates) and reloads the
written file; otherwise `app::save_start_edits` writes the starts. Both refuse `target == source` (except for a
file this app itself saved and reloaded) and report what was written, the writer path,
its notes and the target in the status bar; a failure raises an alert and leaves the
opened file untouched.

### What persists on save, and what does not

Persists: **task start dates** (written as a real **Start No Earlier Than** constraint,
`PjConstraint` 4 at row `+64`, date at `+66` — the one date store MS Project was measured
to honour on open; `memory/writer.md`, W29; W37 extended it to manual rows by clearing
the row's manual bit), **Insert/Delete Task** (wave 12), and since wave 13 the **six
measured custom-field values** and **link rows** queued by the two dialogs. Because
Project recalculates on open, a pinned task's Start follows the constraint and the rest
of the schedule can move with it; a deleted task's dependants are tombstoned by the
cascade and a new task is appended with the name the dialog was given (the writer
clones the file's last live row for everything else). A written link keeps the
predecessor, successor, type and lag exactly as the dialog stated; the writer allocates
the row's uid/identity and the app reloads it.

Does **not** persist: Name, Finish and Duration typed in the sheet, the computed
columns (whose columns are read-only anyway), and the undo history itself — all
**view-only in memory**. Percent complete is not a sheet edit either (that column is
read-only): the ribbon's five % buttons and Task Information's Percent complete field
queue a structural edit that File > Save writes as Project's own actuals (wave 28,
`core/mppwrite/actuals.h`). Wave 28 also enabled Mark on Track and Update Project
(Task > Schedule and Project > Status) on the same writer: both queue an
`update_actuals` edit measured against Project's own save, and Update Project writes
the project status date in `114/Props` when the file stores one. 100% complete and
reschedule-the-rest stay disabled, with the decision-9 tooltip, because they have no
measured save. Wave 28 (W28-3.1) also persists Options > Schedule's **new tasks
created** mode: OK queues a `set_new_task_mode` edit, File > Save writes Project's
measured `114/Props` record `0x024013c8` (`core/mppwrite/project_props.h`; 0x00FF
manual, 0x0000 auto), and Insert Task schedules its new row with that mode. A file
that does not store the record (an original Project 2007 MPP12 file) refuses the
change rather than inventing one. The stored custom fields and links
are written only through their dialogs, not by typing in a grid cell (the stored
columns are read-only).
Saving a name does not change the file; the app never claims it does. The Qt shell's
docked Task Form (View > Details) commits through exactly these paths — its
Name/Duration/Start/Finish are sheet cell edits and its % Complete is the measured
structural edit — so it adds no writer of its own and its persistence matches the
sheet's: Start and percent reach the file, Name/Finish/Duration stay in memory. Its
Predecessors and Resources fields are disabled because those sheet columns are
read-only and there is no field-level link/assignment writer (wave 28, W28-4.1).
The file the user
opened is never modified: the helpers refuse `target == source`, and a failure leaves no
output file behind.

One behaviour to know: **saving a start for a manually scheduled task converts that
task to auto-scheduled**, because the SNET pin is only honoured for an auto task and
W37 clears the manual bit to make the date stick. W37's writer returns a warning line
for each such row; the app counts those as "writer note(s)" in the status bar and
prints their full text to the console (they are otherwise per-row "re-read from
disk" receipts). The app's own warnings — a start cleared in the sheet (nothing to
write), a post-write table-consistency note — raise the alert instead.

### The save path is one call site, behind a feature check

`app/CMakeLists.txt` looks for `core/mppwrite/save.h` at configure time:

* present → `app/document.cpp` calls `mppwrite::save_date_edits(doc, edits, path)`
  (stream W37's API, exactly the signature it landed), and the configure log says so;
* absent → the same helper writes the pins with `mppwrite::pin_task_start` and emits
  the container with `mppwrite::CfbDocument::save`, and the log says that instead.

Nothing outside `app/document.cpp` knows which ran; `SaveOutcome::api` records it for
the status line. The switch is a CMake `if(EXISTS)`, so it is visible in the build log
rather than hidden in a `#ifdef`.

## The date encoding: `date32` and the file's packed date are the SAME counter

The file stores `(day << 16) | ticks_of_day` with **day 0 = 1983-12-31** and 6-second
ticks. `core::sched::date32` has **day 1 = 1984-01-01** and 6-second tenths of a
minute. Both count from 1983-12-31, so the conversion is the **identity**.

This is a W38 correction: v1 added one day (`day32 = packed_day + 1`) and therefore
showed every date one day late. Evidence, all re-runnable: `spec/mpp-records.md` §3
pins packed `15346` = 2026-01-05 and `15636` = 2026-10-22; `core/sched/date.h` pins
day 1 = 1984-01-01 and `weekday_of_day(0) = 6` (1983-12-31 was a Saturday); the
round-trip test in `layout_model_tests.cpp` checks both pinned dates and that PROJECT_A's
Monday reads back as a Monday; and the corpus probe round-trips **every** stored
start/finish in `w3-o1`, `holiday1` and PROJECT_A through both directions and asserts
`w3-o1`'s project start exactly (2026-10-19 08:00 — it read 2026-10-20 before).
The one unrepresentable instant is 1983-12-31 00:00, which packs to 0, the file's
"not set" value; every other value round-trips exactly.

## Verification

Local, Windows MSVC 19.44, Release, `/W4 /permissive- /utf-8 /WX`
(`-DCORE_WARNINGS_AS_ERRORS=ON`), 2026-10-22. Full standalone CMake build of `app/`
(configure + `cmake --build build\w61-app --config Release`, no `--parallel`), then
`ctest` (14 tests):

```
 1/14 Test #1:  app_layout .......   Passed
 2/14 Test #2:  app_selection ....   Passed
 3/14 Test #3:  app_document .....   Passed
 4/14 Test #4:  app_prefs ........   Passed
 5/14 Test #5:  app_menu .........   Passed
 6/14 Test #6:  app_filter .......   Passed
 7/14 Test #7:  app_export .......   Passed
 8/14 Test #8:  app_views ........   Passed
 9/14 Test #9:  app_edit .........   Passed
10/14 Test #10: app_fields .......   Passed
11/14 Test #11: app_edits_fields .   Passed
12/14 Test #12: app_edits_links ..   Passed
13/14 Test #13: app_print ........   Passed
14/14 Test #14: app_start_warning    Passed
100% tests passed, 0 tests failed out of 14

layout_model_tests:      253 checks passed
selection_model_tests:    90 checks passed
document_tests:           43 checks passed
prefs_tests:              82 checks passed
menu_tests:              794 checks passed
filter_tests:             60 checks passed
export_tests:             58 checks passed
views_tests:             164 checks passed
app_edit_tests:          120 checks passed
app_fields_tests:        199 checks passed
app_edits_fields_tests:  160 checks passed
app_edits_links_tests:   159 checks passed
page_layout_tests:        66 checks passed
start_edit_warning_tests: 41 checks passed
                                                 (2,289 checks in total)
```

The build took the **W37 path** (`core/mppwrite/save.h` is present, so
`document.cpp` compiles the single `mppwrite::save_date_edits` call), and
`document_tests` is a real end-to-end save: it loads
`tests/data/synthetic-base.mpp`, edits a Start through `SheetModel` exactly as the
keyboard does, writes to the build tree, re-reads the file with the .mpp reader and
checks the task count, that the source fixture's size is unchanged (its SHA-256 still
matches `tests/data/README.md`), that target == source is refused, that a cleared
start writes nothing, and that an unknown uid fails without leaving a file.

`export_tests` writes real PDFs under `build\w61-app\test-out` (sheet, Gantt, filtered,
hours-style) and checks the `%PDF-` header, the `%%EOF` trailer, the page count, the
printed row count, that two identical exports are byte-identical, that the hours
rendering reaches the page, that the shading-off calendar makes Saturday and Sunday
working days, and every failure path.

**Verified** (on the Windows box): the date identity and its corpus round trip; the
selection/collapse/edit/undo mapping; the save helper end to end on the synthetic
fixture; the preference model and parser (including the wave-11 keys); the menu bar's
structure, enablement and task-view gating; the filter model end to end; PDF export end
to end; the resource sheet's rows/fields and columns; the usage day/week/month buckets,
totals and labels; the computed columns' values and read-only rule; the stored
custom-field decoding/formatting and the file's formulas (extraction and evaluation);
the pending Insert/Delete Task model and the real writer sequence; the Task Fields
and Predecessors dialog models; the real field write/clear round trip (six values, six
clears) and the real link round trip (0→2→1 links, delete+re-add and mixed delete+add
in one save); every refusal leaving the target/source untouched; the printed page
arithmetic (3/83 rows, both report kinds), the header/footer strings, the printer/PDF
seam and the Export-PDF page-count cross-check; the four start-edit warning outcomes,
the FS/lag rule and the per-save report; `app/`'s C++ compiles and links with the CI
warning flags; `ctest` 14/14 (2,289 checks). Wave-13 export PDFs were compared
byte-for-byte with the wave-13 build and are identical (`page_layout`'s defaults are
`core/render`'s defaults).

**Inferred / to be proved by macOS CI**: that `main.mm` compiles, links and behaves.
There is no Objective-C++ compiler on the development box. `app-macos.yml` is that
gate: configure `app/`, build `msproject_app.app` with `-Werror`, run the tests (all
twenty-five; wave 33 replaced the hand-typed name check with a diff of `ctest -N`
against the `add_test(NAME ...)` list in `CMakeLists.txt`), and assert the bundle
executable exists and links AppKit + UniformTypeIdentifiers. Wave 14's print path — `NSPrintOperation`, `PrintView`'s
`-knowsPageRange:`/`-rectForPage:`, the `NSPrintInfo` paper/margin/job settings and
`NSPrintSaveJob` — has never been compiled, so it is the largest unproven surface in
the tree. No runner ever opens a window, so the actual interaction (a click, a
keystroke, a panel, a menu) is verified by construction and by the pure helpers it
calls — not by an automated UI test.

## The self-smoke (`--smoke`)

The manual Mac smoke test this project never had is replaced by an automated one, in
two halves:

* `app/smoke.{h,cpp}` (plain C++, no AppKit) runs the sequences the shell runs for a
  user -- load, filter, the Task Fields / Predecessors dialog models, a Start edit with
  undo/redo, the save-time warning, the real writer, a reopen that checks the SNET
  constraint, `build_print_job` for both report kinds and File > Export PDF -- and
  records a named check for every step.  It is unit-tested on Windows as `app_smoke`.
* `app/main.mm` runs that report, then the AppKit paths: it loads the copy through the
  real load path, prints both print jobs through `run_print_view` with a save URL
  instead of the print panel, and snapshots the document view with
  `-dataWithPDFInsideRect:` (PDF) and `NSBitmapImageRep` (PNG).

```
msproject_app --smoke tests/data/synthetic-base.mpp --out build-app/smoke
```

The fixture is read-only: the app copies it to `<out>/smoke-input.mpp` and saves
`<out>/smoke-edited.mpp`; `tests/data/synthetic-base.mpp` is never opened for writing
(the run checks its bytes are unchanged).  Artifacts: `smoke.json` (every check with
its step), `smoke-window.pdf`, `smoke-window.png`, `smoke-print-gantt.pdf`,
`smoke-print-sheet.pdf` (macOS only) and `smoke-report-export.pdf` (written on every
platform).  Exit code 0 means every check passed; anything else is a red job.

The portable half is proved on Windows (`app_smoke`, 98 checks).  The AppKit half --
and every line of `main.mm` -- compiles only on `app-macos`, which has not run since
2026-10-22 because the Actions minutes are exhausted; see `docs/handoff/w63.md`.

## What the window does not do

- **No Cut** (the sheet has copy/paste, not cut), **no row move and no
  indent/outdent** (blocked on `RecordSet` having no erase/reorder — see
  `memory/sheet.md`), **no column resize/reorder** (only visibility), no fill handle.
  Insert/Delete Task ARE wired (wave 12) as pending structural edits.
- **The resource sheet and usage grids are read-only**: no cell editing, no resource
  insert/delete, no assignment editing, no units/contour editing. Pay rates and Cost
  are not decoded or shown. The stored custom-field columns and the file's formula columns remain read-only in the grid; the six stored fields are edited through Task Fields… (wave 13), and the formulas have no editor.
- **The usage views use the stock Mon-Fri calendar**, not the file's own project
  calendar (`core::cal` is not wired; see Open).
- **Stored custom-field values are read but never written**: the six columns are
  read-only, and editing a stored value or a formula's definition is not a wave-12
  feature (the writer has `save_field_values`, but no UI calls it).
- **No Chart/graph views**, no Team Planner, no multiple tables per view: the four views
  are the only modes.
- **No IME/composition** in the in-cell editor: it is drawn by the view from
  `SheetModel::edit_text()` and fed by `NSEvent` characters, with a caret drawn as
  `|`. There is no selection within the cell and no cursor movement beyond backspace.
- **No real project calendar**: shading is Saturday/Sunday only. `core/cal` can build
  the file's calendar and `GanttSource::calendar` accepts it; that join is not wired.
- **Printing is fixed to A4 landscape** at 24 pt margins (the `app::PageSpec` default)
  and is offered in the Task Sheet view only: there is no page-setup UI, no paper-size
  preference and no report layout for the resource/usage grids. Save as PDF… uses the
  print layout, not the Export PDF report settings, but both take the same page spec.
- **No multiple windows or multiple open files**; one document at a time.
- **No selection highlight in the PDF**: the selected task is named in the footer,
  not marked on the page (see Export PDF).
- **No recalculated schedule**: editing a date does not re-run CPM; the Gantt and the
  status span follow the edited row values, and Project recalculates when it opens
  the saved file.
- **The filter dialog authors one criterion per apply** (the model supports All/Any
  trees of rules; the dialog does not build one yet).

## Two decisions worth knowing

1. **The Gantt is rebuilt from displayed rows, not panned.** `RowMap` is derived from
   `SheetModel::visible_rows()`, restricted by the active filter, and
   `make_gantt_source_for_displayed_rows` builds the chart from exactly those rows
   (resolving each through its uid, wave 25), so Gantt row
   N is displayed ordinal N — the invariant the drawing code relies on. A collapsed
   or filtered-out task has no bar and a link to it is dropped.
2. **`app::kUidField`** (`0x0b4fff01`) is an app-private `RecordSet` field carrying
   the row's UniqueID for the UID column: the descriptor table has no
   `Task.UniqueID` (UniqueID is RecordSet identity, not a display field). The id is
   checked against `core/model/field_table.gen.cpp` to be unused and never goes near
   a file. `app::kDurationTextField` (`0x0b4fff02`) is the same idea for the printed
   duration text.

## Open, ranked

1. **Prove `main.mm` on macOS CI** — the whole AppKit surface (menus, control bar,
   view switching, Preferences panel, filter dialog, export panel, dirty prompts,
   clipboard) is compile-unproven here. Wave 14's additions are the riskiest yet:
   `PrintView` (`-knowsPageRange:`/`-rectForPage:`/`-drawRect:`), the `NSPrintInfo`
   paper-margin-job settings, `NSPrintSaveJob` + `NSPrintJobSavingURL`, and the
   per-save start-edit `NSAlert`. A manual Mac print (Cmd-P to a printer and to PDF)
   is still the only end-to-end proof.
1. **Printing is A4 landscape only** and Task-Sheet only, with no page-setup UI; a
   paper-size preference and a resource/usage report layout are the next steps.
1. **Wire the file calendar into the usage views** (`core::cal` →
   `app::standard_calendar()`'s one call site) once a fixture proves the decoded
   calendar matches Project's buckets; today the stock Mon-Fri week is a documented
   choice.
1. **A `114/Props` formula writer** is deliberately absent: Task Fields… writes the
   six measured values, not a formula definition. Editing a formula is the remaining
   custom-field step and needs an oracle fixture first.
1. **Field-presence in the reader**: `clear_field_value` clears the FixedMeta presence
   bits, but `core/mpp` lists any decodable VarMeta entry, so the app reloads a cleared
   Number/Duration as 0 (Text blank, Date Null) where Project shows blank. A reader-side
   presence-bit decode would make the two agree; `core/**` is another stream's lane.
1. **Resource cost/rates** if the reader ever decodes them: the Resource Sheet has a
   Work column, not a Cost column, because inventing rates would be worse.
2. **Selection fidelity on macOS** is unmeasured against the product: row band vs
   cell-only selection, and the disclosure box's exact look, are our choices.
3. **A real in-cell text editor** (IME, cursor movement, selection) if the
   self-drawn editor becomes limiting.
4. **Two synced scroll views** (frozen sheet, horizontally scrolling Gantt).
5. **Real calendar shading** (`core/cal` → `GanttSource::set_calendar`), and the
   per-task calendar once the reader exposes it usefully.
6. **Multi-rule filter UI** over the All/Any path that `filter_model` already builds
   and tests.
7. **Row move / indent / outdent**: the writer can append and tombstone a Task row, but
   not reorder one or change its outline position; a moved row would need both.
8. **Non-date edits and saving**: Name/Finish/Duration persist nowhere. Only a writer
   API for those fields would change that.

