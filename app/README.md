# app/ — the macOS shell (AppKit) over the portable core

A native macOS application that opens a real `.mpp`, shows the tasks in a sheet on
the left and the Gantt on the right, and lets you select, edit, undo, filter,
collapse, export to PDF and save task start dates — behind a real menu bar and a
Preferences panel. Wave 11 adds a **resource sheet**, two **usage grids**
(task/resource, day/week/month) and read-only **computed formula columns**, all
switchable from the View menu and from a visible control bar. Everything below
`main.mm` is portable C++ that is built and tested on Windows too.

This directory is a **standalone CMake project**. It is deliberately *not* added to
the repository's top-level `CMakeLists.txt` (owned by the build stream), it edits no
`core/` file, and it defines no core target of its own except `core_model` (which has
no `CMakeLists.txt` in-tree; every standalone core subdirectory already builds it the
same way).

| file | what it is |
|---|---|
| `CMakeLists.txt` | standalone project: options, core libraries with their tests off, `app_layout`, eight headless tests, AppKit target on `APPLE` only |
| `layout_model.h` / `.cpp` | **pure C++**: `mpp` packed-date → `date32` (identity, see below), task/link/resource/assignment rows → `RecordSet` + `SheetModel` columns, row → `SheetRowText`, `GanttSource`/`Timescale`/`GanttStyle`, `timescale_for_zoom`, plan summary, `sync_plan_from_records`, `make_gantt_source_for_rows`, `apply_edit_policy` |
| `view_model.h` / `.cpp` | **pure C++ (wave 11)**: the four view modes, the stock Mon-Fri calendar, `PlanInput` → `core::view::ResourceRecord`s, the registry-backed resource-sheet columns, the usage grid (labels, period columns, a TOTAL row and a Total column), the two computed formula columns, and the read-only policy |
| `selection_model.h` / `.cpp` | **pure C++**: `RowMap` (display ordinal ↔ RecordSet row ↔ uid), `selected_task`, Gantt hit-test → sheet row, arrow-key navigation, `toggle_row_selection` (Space), `toggle_row`, `scroll_target` |
| `document.h` / `.cpp` | **pure C++ save helper**: diff the RecordSet against the loaded plan, then write the edited starts through `core/mppwrite` to a **new** file |
| `prefs.h` / `.cpp` | **pure C++**: the preference model, its persisted one-line string + tolerant parser, the date format applied to `SheetFormat`, the app-side duration rendering, `cell_text` |
| `menu_model.h` / `.cpp` | **pure C++**: the whole menu bar as data — items, key equivalents, `MenuAction` ids, enablement rules, and the reasons the unsupported items are greyed out |
| `filter_model.h` / `.cpp` | **pure C++**: filter fields/operators from the sheet, a criterion → `core::view::FilterDefinition` with an error message, matching rows, the displayed-row restriction, the printed subset |
| `export_pdf.h` / `.cpp` | **pure C++**: Export PDF through `core/render` — sheet and Gantt, the filtered rows, the preference formats, the shading-off calendar |
| `reader_adapter.h` | the one header that includes `core/mpp/reader.h`; `mpp::Document` → `app::PlanInput` (tasks, links, resources, assignments) and the measured `core::usage::usage_input_from_mpp` bridge |
| `layout_model_tests.cpp` | mapping, date identity (including two dates pinned by the spec), row texts, Gantt geometry, zoom; `corpus/probe` counts when present |
| `selection_model_tests.cpp` | two-way selection, Gantt clicks (bar and dependency), collapse, editing/undo/redo through `SheetModel`, the read-only policy |
| `document_tests.cpp` | `default_save_name`, the start-date diff, the **real save round trip** on `tests/data/synthetic-base.mpp` |
| `prefs_tests.cpp` | the identifiers, the parser (including bad/old strings), the three date styles, the three duration renderings, `cell_text`, the zoom mapping |
| `menu_tests.cpp` | six menus, every action present once per menu, unique key equivalents, the documented shortcuts, enablement rules, the three permanently disabled items |
| `filter_tests.cpp` | the field/operator lists, name/duration/date/blank criteria, two-rule All/Any, every error path, the row-list restriction, the printed subset |
| `export_tests.cpp` | real PDFs: sheet + Gantt, filtered, duration preference, determinism, the shading-off calendar, the name/subtitle/footer helpers, failures |
| `views_tests.cpp` | **new (wave 11)**: resource rows/fields from a synthetic plan, usage day/week/month buckets + totals row/column, formula evaluation, computed columns read-only + never a save edit, view-mode and preference round trip |
| `main.mm` | Objective-C++ / AppKit: window, control bar (view switcher + usage period), menu bar built from `menu_model`, Preferences panel, filter dialog, save/export panels, dirty prompts, clipboard, keyboard, drawing (task sheet + Gantt, or a flat resource/usage grid) |
| `../.github/workflows/app-macos.yml` | the macOS CI job: configure `app/`, build everything with `-Werror`, run the tests (the job greps the seven wave-10 names and `ctest` runs all eight), prove the bundle exists and links AppKit + UniformTypeIdentifiers |

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
cmake -S app -B build\vs-w47 -G "Visual Studio 17 2022" -A x64 \
      -DCMAKE_BUILD_TYPE=Release -DAPP_BUILD_TESTS=ON -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build build\vs-w47 --config Release
ctest --test-dir build\vs-w47 -C Release --output-on-failure
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
| File | Open… (Cmd-O), Save (Cmd-S), Save As… (Cmd-Shift-S), Export PDF (Sheet)… (Cmd-Shift-E), Export PDF (Gantt)… (Cmd-Option-E), Close (Cmd-W) |
| Edit | Undo (Cmd-Z), Redo (Cmd-Shift-Z), Cut (Cmd-X, **disabled**), Copy (Cmd-C), Paste (Cmd-V), Select All (Cmd-A) |
| View | Task Sheet (Cmd-Option-1), Resource Sheet (Cmd-Option-2), Task Usage (Cmd-Option-3), Resource Usage (Cmd-Option-4), Usage Period: Day/Week/Month, Zoom In (Cmd-=), Zoom Out (Cmd--), Day/Week/Month Timescale (Cmd-1/2/3), Working-Day Shading (Cmd-Shift-D), Filter… (Cmd-F), Clear Filter (Cmd-Shift-F), Collapse All, Expand All, Columns ▸ (per-column check marks plus the pending stored-custom-field entry) |
| Insert | Task…, Resource… — **both permanently disabled** |
| Help | Keyboard Shortcuts, About MS Project Shell |

**Greyed out rather than faked.** `Insert > Task` and `Insert > Resource` need the
shell's add/delete editing, which wave 12 owns; wave 11's new views are read-only, so
a resources/usage grid must not pretend it can add a row. `Cut` needs a `SheetModel`
operation that does not exist — copy and paste do. All three are in the menu, disabled
in every state, with `menu_disabled_reason()` as their tooltip and the reason pinned in
`menu_tests.cpp`.

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
through `SheetModel::set_column_visible`. The task sheet's column set is FIXED — the
seven task columns plus the two computed columns — on every load, so the submenu's
`tag` is the model column index.

**Views (wave 11).** A segmented control and a Usage period popup sit in a control bar
above the scrolling document; the View menu has the same commands (Cmd-Option-1..4 for
the views). The choice and the period are remembered in the preferences and selected
again on the next load.

| view | what it shows | selection/editing |
|---|---|---|
| Task Sheet | the task sheet + Gantt, as before | yes (the only view with a Gantt) |
| Resource Sheet | one row per live resource: ID, Name, Type, Initials, Max. Units, Work | none (read-only) |
| Task Usage | one row per task, one column per day/week/month period, a Total column and a TOTAL row | none (read-only) |
| Resource Usage | the same grid per resource | none (read-only) |

The resource rows and columns come from `core/view/resource_tables.h`:
`build_resource_record_set()` supplies ID/Name/Type/Initials/MaxUnits, the shell shows
the Type as its display name and adds **Work** (the resource's assignment total, in
duration ticks, from `core::usage`). **Cost is not shown** — the reader decodes no
resource rates, so a cost column would be invented. The Entry table's captions and
widths are read from the registry (`find_resource_table("resource-entry")`), so a
registry change moves this view with it.

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

**Computed columns (wave 11).** The task sheet has two read-only formula columns,
registered through `core::formula::ComputedColumns`:

| column | field | formula |
|---|---|---|
| Duration x2 | `Number1` | `[Duration]*2` |
| Start +7 Days | `Date1` | `[Start]+7` |

They are re-evaluated after every edit and rendered by the sheet like any other column
(a Duration and a Date). Their columns are declared read-only and `begin_edit` refuses
them; they are also outside the save diff (`collect_start_edits` reads task starts
only), so **Save can never write them**. View ▸ Columns toggles them like any column and
the choice persists. **Stored custom fields are pending**: `mpp::Task` exposes no stored
custom-field values, so the Columns chooser shows a disabled "Stored custom fields
(pending)" entry rather than a column of invented zeroes.

In the resource/usage views the task-only commands (undo/redo, clipboard, Select All,
zoom, filter, collapse, PDF export) are disabled rather than acting on the hidden task
sheet; Save stays available because it still writes task starts.

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
preference. The current **selection** is named in the report footer; `core/render`
has no selection concept and this wave does not edit `core/`, so the selected row is
*not* highlighted on the page, and the README says so rather than implying otherwise.
Shading off is implemented by handing the renderer an all-working calendar (every day
00:00–24:00, so no non-working span exists) with the week anchor pinned, which
changes nothing else: shading never moves a bar.

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
(`version=1;date=...;duration=...;zoom=...;shading=...;view=...;usage=...;formula1=0|1;formula2=0|1;custom=0|1`).
The wave-11 keys are additive: a wave-10 string without them parses and keeps the new
defaults.
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
Start in the RecordSet differs from the file as loaded (`app::collect_start_edits`).
That is the only edit `core/mppwrite` can persist, so it is the only edit that lights
the window's edited dot — a renamed task or a changed Finish does not claim unsaved
file changes. Clicking the close box (or Quit) with pending start edits asks **Save /
Don't Save / Cancel**: Save goes through the normal save path (a cancelled panel or a
writer failure keeps the window open), Don't Save discards, Cancel aborts the close.
The prompt text says that only start dates are written and that the other edits are
view-only.

`File > Save` (Cmd-S) writes the edited starts to the **last save target**
(`<name>-edited.mpp` until you choose another path); the first Save, and every
`Save As…` (Cmd-Shift-S), shows an `NSSavePanel`. `app::save_start_edits` refuses
`target == source` and reports rows written, the writer path, its notes and the
target path in the status bar; a failure raises an alert and leaves the opened file
untouched.

### What persists on save, and what does not

Persists: **task start dates only**, written as a real **Start No Earlier Than
constraint** (`PjConstraint` 4 at row `+64`, date at `+66`) — the one date store MS
Project was measured to honour on open (`memory/writer.md`, W29; W37 extended it to
manual rows by clearing the row's manual bit). Because Project recalculates on open,
the task's Start follows the pin and the rest of the schedule can move with it.

Does **not** persist: every other edit the sheet accepts — Name, Finish, Duration,
percent complete, and the undo history itself — is **view-only in memory**. Saving a
name does not change the file; the app never claims it does. The opened file is
never modified: the helper refuses `target == source`, and a failure leaves no output
file behind.

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
(configure + `cmake --build build\vs-w47 --config Release`, no `--parallel`), then
`ctest`:

```
1/8 Test #1: app_layout .....   Passed
2/8 Test #2: app_selection ..   Passed
3/8 Test #3: app_document ...   Passed
4/8 Test #4: app_prefs ......   Passed
5/8 Test #5: app_menu .......   Passed
6/8 Test #6: app_filter .....   Passed
7/8 Test #7: app_export .....   Passed
8/8 Test #8: app_views ......   Passed
100% tests passed, 0 tests failed out of 8

layout_model_tests:     253 checks passed
selection_model_tests:   90 checks passed
document_tests:          43 checks passed
prefs_tests:             82 checks passed
menu_tests:             664 checks passed
filter_tests:            60 checks passed
export_tests:            58 checks passed
views_tests:            164 checks passed
                                                 (1,414 checks in total)
```

The build took the **W37 path** (`core/mppwrite/save.h` is present, so
`document.cpp` compiles the single `mppwrite::save_date_edits` call), and
`document_tests` is a real end-to-end save: it loads
`tests/data/synthetic-base.mpp`, edits a Start through `SheetModel` exactly as the
keyboard does, writes to the build tree, re-reads the file with the .mpp reader and
checks the task count, that the source fixture's size is unchanged (its SHA-256 still
matches `tests/data/README.md`), that target == source is refused, that a cleared
start writes nothing, and that an unknown uid fails without leaving a file.

`export_tests` writes real PDFs under `build\vs-w47\test-out` (sheet, Gantt, filtered,
hours-style) and checks the `%PDF-` header, the `%%EOF` trailer, the page count, the
printed row count, that two identical exports are byte-identical, that the hours
rendering reaches the page, that the shading-off calendar makes Saturday and Sunday
working days, and every failure path.

**Verified** (on the Windows box): the date identity and its corpus round trip; the
selection/collapse/edit/undo mapping; the save helper end to end on the synthetic
fixture; the preference model and parser (including the wave-11 keys); the menu bar's
structure, enablement and task-view gating; the filter model end to end; PDF export end
to end; the resource sheet's rows/fields and columns; the usage day/week/month buckets,
totals and labels; the computed columns' values and read-only rule; `app/`'s C++
compiles and links with the CI warning flags; `ctest` 8/8.

**Inferred / to be proved by macOS CI**: that `main.mm` compiles, links and behaves.
There is no Objective-C++ compiler on the development box. `app-macos.yml` is that
gate: configure `app/`, build `msproject_app.app` with `-Werror`, run the tests (all
eight; the registration step greps the seven wave-10 names),
and assert the bundle executable exists and links AppKit + UniformTypeIdentifiers.
No runner ever opens a window, so the actual interaction (a click, a keystroke, a
panel, a menu) is verified by construction and by the pure helpers it calls — not by
an automated UI test.

## What the window does not do

- **No Cut** (the sheet has copy/paste, not cut), **no row insert/delete/move, no
  indent/outdent** (blocked on `RecordSet` having no insert/erase/reorder — see
  `memory/sheet.md`), **no column resize/reorder** (only visibility), no fill handle.
- **The resource sheet and usage grids are read-only**: no cell editing, no resource
  insert/delete, no assignment editing, no units/contour editing. Pay rates and Cost
  are not decoded or shown. This is deliberate for wave 11; wave 12 owns view editing.
- **The usage views use the stock Mon-Fri calendar**, not the file's own project
  calendar (`core::cal` is not wired; see Open).
- **Stored custom-field values are not read**: `mpp::Task` does not expose them yet, so
  only the two formula columns exist and the chooser marks the stored ones pending.
- **No Chart/graph views**, no Team Planner, no multiple tables per view: the four views
  are the only modes.
- **No IME/composition** in the in-cell editor: it is drawn by the view from
  `SheetModel::edit_text()` and fed by `NSEvent` characters, with a caret drawn as
  `|`. There is no selection within the cell and no cursor movement beyond backspace.
- **No real project calendar**: shading is Saturday/Sunday only. `core/cal` can build
  the file's calendar and `GanttSource::calendar` accepts it; that join is not wired.
- **No print dialog / paper settings**: Export PDF writes a report; there is no
  `NSPrintOperation` and no page-setup UI.
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
   `make_gantt_source_for_rows` builds the chart from exactly those rows, so Gantt row
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
   clipboard) is compile-unproven here. The wave-11 additions to watch: the
   `NSSegmentedControl`/`NSPopUpButton` control bar and its container view, the
   `syncViewControls` target/action path, and the per-view drawing branch.
1. **Wire the file calendar into the usage views** (`core::cal` →
   `app::standard_calendar()`'s one call site) once a fixture proves the decoded
   calendar matches Project's buckets; today the stock Mon-Fri week is a documented
   choice.
1. **Stored custom-field values**: read `Text1`/`Number1`/`Date1`/`Flag1` through the
   reader (the oracle measured the storage keys) and add the third chooser column; the
   formula columns are the read-only substitute until then.
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
7. **Row insert/delete** when W45 lands a writer that can grow a table (then Insert ▸
   Task can be enabled, and the reason it was greyed out goes away).
8. **Non-date edits and saving**: Name/Finish/Duration persist nowhere. Only a writer
   API for those fields would change that.
