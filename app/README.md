# app/ — the macOS shell (AppKit) over the portable core

A native macOS application that opens a real `.mpp`, shows the tasks in a sheet on
the left and the Gantt on the right, and lets you select, edit, undo, collapse and
save task start dates. Everything below `main.mm` is portable C++ that is built and
tested on Windows too.

This directory is a **standalone CMake project**. It is deliberately *not* added to
the repository's top-level `CMakeLists.txt` (owned by the build stream), it edits no
`core/` file, and it defines no core target of its own except `core_model` (which has
no `CMakeLists.txt` in-tree; every standalone core subdirectory already builds it the
same way).

| file | what it is |
|---|---|
| `CMakeLists.txt` | standalone project: options, core libraries with their tests off, `app_layout`, three headless tests, AppKit target on `APPLE` only |
| `layout_model.h` / `.cpp` | **pure C++**: `mpp` packed-date → `date32` (identity, see below), task/link rows → `RecordSet` + `SheetModel` columns, row → `SheetRowText`, `GanttSource`/`Timescale`/`GanttStyle`, plan summary, `sync_plan_from_records` (edits back into the drawing model), `make_gantt_source_for_rows` (the Gantt follows the visible rows), `apply_edit_policy` (read-only identity columns) |
| `selection_model.h` / `.cpp` | **pure C++**: `RowMap` (visible ordinal ↔ RecordSet row ↔ uid), `selected_task`, Gantt hit-test → sheet row, arrow-key navigation, `toggle_row`, `scroll_target` |
| `document.h` / `.cpp` | **pure C++ save helper**: diff the RecordSet against the loaded plan, then write the edited starts through `core/mppwrite` to a **new** file |
| `reader_adapter.h` | the one header that includes `core/mpp/reader.h`; `mpp::Document` → `app::PlanInput` |
| `layout_model_tests.cpp` | mapping, date identity (including two dates pinned by the spec), row texts, Gantt geometry; `corpus/probe` counts when present |
| `selection_model_tests.cpp` | two-way selection, Gantt clicks (bar and dependency), collapse changing the Gantt row list, editing/undo through `SheetModel`, the read-only policy |
| `document_tests.cpp` | `default_save_name`, the start-date diff, and the **real save round trip** on `tests/data/synthetic-base.mpp` |
| `main.mm` | Objective-C++ / AppKit: window, menu, `PlanView` (drawing + mouse/key), open/save panels, alerts |
| `../.github/workflows/app-macos.yml` | the macOS CI job: configure `app/`, build everything with `-Werror`, run the three tests, prove the bundle exists and links AppKit + UniformTypeIdentifiers |

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
cmake -S app -B build\vs-w38 -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_BUILD_TYPE=Release -DAPP_BUILD_TESTS=ON -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build build\vs-w38 --config Release
ctest --test-dir build\vs-w38 -C Release --output-on-failure
```

Never `--parallel` on this box (see `docs/build.md`). A transient
`MSB6003`/`C1083` is the known Dropbox/AV file-lock: retry the build once. Building
`app/` compiles `core/` from source, so another stream's half-written file fails the
app build exactly as it fails the root build: retry, do not "fix" their file.

Options: `APP_BUILD_TESTS` (default `ON`), `CORE_WARNINGS_AS_ERRORS` (default `OFF`;
the workflow passes `ON`, which also reaches the core subdirectories).

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
the current rows**, the number of edited starts, the reader's warning count and the
file path; then the last action, in red when it was a rejected edit or a failed save.

**Selection, two ways.** Clicking a sheet cell selects it; clicking a Gantt bar (or a
dependency) selects the sheet row of that bar (the successor, for a link — a sheet
selects one row); Shift-click extends the range; arrows move through the **visible**
rows and columns. Selecting a row highlights its Gantt bar (outlined) and the row
band in both views, and scrolls it into view. `SheetModel` owns the selection; the
mapping lives in `app::RowMap`/`selected_task` and is unit-tested.

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

**Outline.** A summary row shows a `-`/`+` box in the Name column's gutter; clicking
it (or Cmd-Left / Cmd-Right on a selected summary) calls
`SheetModel::toggle_collapsed`. The visible-row list is then re-read, the Gantt is
**rebuilt** from exactly those rows (`make_gantt_source_for_rows`), so hidden tasks
have no bars and links to them are dropped; the document shrinks/grows with the row
count and the status line's counts follow.

**Save.** `File > Save Edited Starts…` (Cmd-S) collects the tasks whose Start differs
from the file as loaded, asks for a target path (default suggestion
`<name>-edited.mpp` next to the source) and calls `app::save_start_edits`. It reports
rows written, the writer path, any warnings and the target path in the status bar;
a failure raises an alert and leaves the opened file untouched.

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
(configure + `cmake --build build\vs-w38 --config Release`, no `--parallel`), then
`ctest`:

```
1/3 Test #1: app_layout .....   Passed
2/3 Test #2: app_selection ..   Passed
3/3 Test #3: app_document ...   Passed
100% tests passed, 0 tests failed out of 3

app_layout_tests:      253 checks passed
CORPUS: w3-o1: 4 tasks, 0 links (0 skipped), 0 summaries, 1 milestones, 1 critical
CORPUS: holiday1: 3 tasks, 1 links (0 skipped), 0 summaries, 0 milestones, 2 critical
CORPUS: project_a: 83 tasks, 88 links (0 skipped), 16 summaries, 10 milestones, 35 critical

selection_model_tests:  90 checks passed
document_tests:         43 checks passed
SAVE: mppwrite::save_date_edits
SAVE NOTE: uid 1: was manual; written as SNET and converted to auto-scheduled so Project
           honours the date (manual scheduling is not preserved, measured 2026-10-22)
SAVE NOTE: re-read from disk: uid 1 SNET 2026-03-02 08:00 at ConstraintType 4
```

The build took the **W37 path** (`core/mppwrite/save.h` is present, so
`document.cpp` compiles the single `mppwrite::save_date_edits` call), and the save
round trip below ran through it. The same helper was also run through the fallback
branch (`pin_task_start` + `CfbDocument::save`, 45 checks) while `core/mppwrite` was
mid-edit; both are green, which is what the CMake switch exists for.

`document_tests` is a real end-to-end save: it loads
`tests/data/synthetic-base.mpp`, edits a Start through `SheetModel` exactly as the
keyboard does, writes to the build tree, re-reads the file with the .mpp reader and
checks the task count, that the source fixture's size is unchanged (its SHA-256 still
matches `tests/data/README.md`), that target == source is refused, that a cleared
start writes nothing, and that an unknown uid fails without leaving a file. Its SNET
type/date assertion is compiled only on the fallback branch — on the W37 branch the
writer proves that itself by re-reading from disk (`SAVE NOTE` above), and the test
prints what it read.

**Verified** (on the Windows box): the date identity and its corpus round trip; the
selection/collapse/edit/undo mapping; the save helper end to end on the synthetic
fixture, through both writer branches; `app/`'s C++ compiles and links with the CI
warning flags; `ctest` 3/3.

**Inferred / to be proved by macOS CI**: that `main.mm` compiles, links and behaves.
There is no Objective-C++ compiler on the development box. `.github/workflows/
app-macos.yml` is that gate: configure `app/`, build `msproject_app.app` with
`-Werror`, run the three tests, and assert the bundle executable exists and links
AppKit + UniformTypeIdentifiers. No runner ever opens a window, so the actual
interaction (a click, a keystroke, a panel) is verified by construction and by the
pure helpers it calls — not by an automated UI test.

## What the window does not do

- **No clipboard**: `SheetModel::copy_selection`/`paste` exist and are tested in
  `core/sheet`, but no menu item or NSPasteboard bridge is wired.
- **No row insert/delete/move, no indent/outdent** (blocked on `RecordSet` having no
  insert/erase/reorder — see `memory/sheet.md`), **no column resize/reorder**, no
  fill handle.
- **No IME/composition** in the in-cell editor: it is drawn by the view from
  `SheetModel::edit_text()` and fed by `NSEvent` characters, with a caret drawn as
  `|`. There is no selection within the cell and no cursor movement beyond backspace.
  A real `NSTextField` editor is the replacement if that matters.
- **No real project calendar**: shading is Saturday/Sunday only. `core/cal` can build
  the file's calendar and `GanttSource::calendar` accepts it; that join is not wired.
- **No zoom, no printing, no multiple windows/files, no progress line** (only the
  vertical status marker), no baseline bars, no split tasks, no summary-bar end
  shapes beyond the approximation.
- **No AppKit launch in CI**: the runner builds and links the bundle; it never opens
  a window.
- **No recalculated schedule**: editing a date does not re-run CPM; the Gantt and the
  status span follow the edited row values, and Project recalculates when it opens
  the saved file.

## Two decisions worth knowing

1. **The Gantt is rebuilt from visible rows, not panned.** `RowMap` is derived from
   `SheetModel::visible_rows()` and `make_gantt_source_for_rows` builds the chart from
   exactly those rows, so Gantt row N is visible ordinal N — the invariant the
   drawing code relies on. A collapsed task has no bar and a link to it is dropped.
2. **`app::kUidField`** (`0x0b4fff01`) is an app-private `RecordSet` field carrying
   the row's UniqueID for the UID column: the descriptor table has no
   `Task.UniqueID` (UniqueID is RecordSet identity, not a display field). The id is
   checked against `core/model/field_table.gen.cpp` to be unused and never goes near
   a file.

## Open, ranked

1. **Selection fidelity on macOS** is unmeasured against the product: row band vs
   cell-only selection, and the disclosure box's exact look, are our choices.
2. **A real in-cell text editor** (IME, cursor movement, selection) if the self-drawn
   editor becomes limiting.
3. **Clipboard wiring** (Cmd-C/Cmd-V over `ClipboardBlock::to_text/from_text`).
4. **Two synced scroll views** (frozen sheet, horizontally scrolling Gantt) once
   fidelity work starts; the one-scroll-view shell is still a first cut.
5. **Real calendar shading** (`core/cal` → `GanttSource::set_calendar`), and the
   per-task calendar once the reader exposes it usefully.
6. **Recalculation after an edit** — today the app shows the edit, Project
   recalculates it on open; a CPM pass in the app would need `core/plan`.
7. **Status date** should come from the file when the reader exposes it, not from
   `today_date32()`.
8. **Non-date edits and saving**: Name/Finish/Duration persist nowhere. Only a writer
   API for those fields (W37's `save_plan` writes recalculated rows, which is a
   different, bigger operation) would change that.
