# app/ -- the first macOS shell (AppKit) over the portable core

A minimal native macOS application that opens a real `.mpp`, shows the tasks in
a sheet on the left and the Gantt on the right, and draws both from the portable
C++ core that already exists in this repository. No editing, no scheduling, no
toolkit types below `main.mm`.

This directory is a **standalone CMake project**. It is deliberately *not* added
to the repository's top-level `CMakeLists.txt` (owned by the build stream), it
edits no `core/` file, and it defines no core target of its own except
`core_model` (which has no `CMakeLists.txt` in-tree; every standalone core
subdirectory already builds it the same way).

| file | what it is |
|---|---|
| `CMakeLists.txt` | standalone project: options, core libraries with their tests off, `app_layout`, headless test, AppKit target on `APPLE` only |
| `layout_model.h` / `.cpp` | **pure C++**, no AppKit and no reader types: `mpp` packed-date -> `date32`, task/link rows -> `RecordSet` + `SheetModel` columns, row -> `SheetRowText`, `GanttSource`/`Timescale`/`GanttStyle`, plan summary |
| `reader_adapter.h` | the one header that includes `core/mpp/reader.h`; `mpp::Document` -> `app::PlanInput` (header only) |
| `layout_model_tests.cpp` | the headless test: synthetic rows always, `corpus/probe` probes when present, counts only (no client names) |
| `main.mm` | Objective-C++ / AppKit: `NSApplication` + `AppDelegate` + one `NSScrollView` + a flipped `PlanView` that draws the whole window |
| `../.github/workflows/app-macos.yml` | the macOS CI job: configure `app/`, build everything, run `ctest`, prove the bundle exists and links AppKit |

## Build and run

macOS (the only place `main.mm` compiles):

```sh
cmake -S app -B build-app -G "Unix Makefiles" \
      -DCMAKE_BUILD_TYPE=Release -DAPP_BUILD_TESTS=ON -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build build-app --config Release
ctest --test-dir build-app -C Release --output-on-failure --timeout 60
open build-app/msproject_app.app                 # or:
build-app/msproject_app.app/Contents/MacOS/msproject_app /path/to/file.mpp
```

Windows (headless test only -- there is no ObjC++/AppKit here, so a non-Apple
configure skips the app target and must still succeed):

```bat
set vc=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat
call "%vc%"
cmake -S app -B app\build\vs -G "Visual Studio 17 2022" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build app\build\vs --config Release --target layout_model_tests
app\build\vs\Release\layout_model_tests.exe
```

Never `--parallel` on this box (see `docs/build.md`). A transient
`LNK1104: cannot open ...app_layout.lib` is the known Dropbox/AV file-lock: retry
the build once. Once, an executable launched in the same second the linker
finished died with `0xC0000005` and ran fine on the next invocation (same
environmental class of event); re-run before investigating.

Options: `APP_BUILD_TESTS` (default `ON`), `CORE_WARNINGS_AS_ERRORS` (default
`OFF`; the workflow passes `ON`, which also reaches the core subdirectories).

## What the window does

One window, one `NSScrollView`, one document view. No split view, no editing.

- **Sheet (left).** Columns: UID, ID, Outline number, Name, Start, Finish,
  Duration. Text and formatting come from `core::sheet::SheetModel` (the model
  owns date/duration/percent text and the outline), so the window shows what the
  core believes, not a second implementation. Summary rows are bold with a light
  band, critical rows are red, the Name column is indented by the row's
  `indent_points`.
- **Gantt (right).** Two header tiers (month over day numbers, weekend days
  shaded in the header), weekend shading over the row band, one bar per task
  (normal bar with a darker percent-complete fill, black summary bar with
  approximate bracket ends, milestone diamond), dependency polylines with
  arrowheads, and the status-date marker as a dashed vertical line.
- **Footer.** A status line: task/link/summary/milestone/critical counts, the
  project date span, pixels per day, the reader's warning count and the file
  name.

The status date is **today** (UTC `date32`), because reader v0 exposes no status
date: this is a display default (MS Project's own default is the current date) and
is labelled as such. Bar labels are drawn with a real font metric installed in
`GanttStyle::measure_text`, so the "inside vs right of the bar" decision uses the
font that is actually drawn.

Source selection: `argv[1]`, else `corpus/probe/w3-o1.mpp`, then
`corpus/probe/holiday1.mpp`, then `corpus/probe/base.mpp`, then
`tests/data/synthetic-base.mpp` (committed). The two directories are compiled in
absolute, so the app opens something from any working directory.

## What the window does **not** do

- **No editing** (`SheetModel` supports editing; the shell does not wire any
  mouse or keyboard to it). No selection, no clipboard, no undo.
- **No collapse/expand**: all rows are visible, which is also why the sheet row
  ordinal and `GanttSource::tasks` index stay equal.
- **No scroll synchronisation split**: one scroll view moves sheet and Gantt
  together, so horizontal scrolling moves the sheet columns out of view.
- **No real project calendar**: shading is Saturday/Sunday only. `core/cal` can
  decode the file's calendars and `GanttSource::nonworking_days` takes holidays,
  but that join is not wired (the model and the app both allow it).
- **No zoom, no printing, no multiple files, no progress line** (only the
  vertical status marker), no baseline bars, no split tasks, no summary-bar end
  shapes beyond an approximation.
- **No AppKit launch in CI**: the runner builds and links the bundle; it never
  opens a window.

## The mapping, and the two decisions worth knowing

`reader_adapter.h` takes the reader's derivations unchanged: `summary` from
`Task::summary` (child count > 0), `milestone` from `Task::is_milestone()`
(duration == 0, not a summary), `critical` from `Task::critical` (the reader's
slack rule), `outline_level` = Project's 1-based `OutlineLevel` minus one
(`SheetModel` wants 0-based), links whose endpoint the reader did not know are
counted in `PlanInput::skipped_links` and dropped.

1. **`date32` shift.** The file's packed date is `(day << 16) | ticks_of_day`
   with day 0 = 1983-12-31 and one tick = 6 s; `core::sched::date32` has day 1 =
   1984-01-01 and one unit = 1/10 minute = 6 s. So the conversion is
   `day + 1` and the time field is copied. `date32_from_packed()` is the only
   place that is written; the test round-trips it against
   `packed_from_date32()`.
2. **The timescale origin must be seeded.** `core::gantt::build_gantt()` only
   ever moves the origin *earlier*. `timescale_for_plan()` sets it to the
   project start (else the earliest task start) before the snap, otherwise the
   chart would keep the 1984 epoch origin and put thousands of empty day columns
   in front of the first bar.

`app::kUidField` (`0x0b4fff01`) is an app-private `RecordSet` field carrying the
row's UniqueID for the UID column: the descriptor table has no `Task.UniqueID`
(UniqueID is RecordSet identity, not a display field). The id is checked against
`core/model/field_table.gen.cpp` to be unused and never goes near a file.

## Verification

Headless test, Windows MSVC 19.44, `Release`, `/W4 /permissive- /utf-8 /WX`
(`-DCORE_WARNINGS_AS_ERRORS=ON`), 2026-10-22:

```
app_layout_tests: 246 checks passed
CORPUS: w3-o1: 4 tasks, 0 links (0 skipped), 0 summaries, 1 milestones, 1 critical
CORPUS: holiday1: 3 tasks, 1 links (0 skipped), 0 summaries, 0 milestones, 2 critical
CORPUS: project_a: 83 tasks, 88 links (0 skipped), 16 summaries, 10 milestones, 35 critical
100% tests passed, 0 tests failed out of 1
```

The corpus lines print counts only, never a task name. Without `corpus/` the
probes print `SKIP:` and 210 synthetic checks still run (that is the CI case;
`-DAPP_CORPUS_PROBE_DIR=<nonexistent>` reproduces it on a machine that has the
corpus). The corpus numbers are *reported*, not pinned by assertions -- only
`w3-o1`'s task count (4), date sanity, row/bar/sheet counts and the "milestones
are the zero-day rows" consistency are asserted, so concurrent reader work
(`core/mpp` is owned by another stream) changes the printed counts without
breaking this test: during this session PROJECT_A's stored-critical count moved from
56 to 35 and the test stayed green.

**Verified** (on the Windows box): configure + build of `app/` for the headless
test; the 246 assertions; the corpus probe counts; `ctest` 1/1; the top-level
`core/` build is untouched (this project only *adds* subdirectories).

Building `app/` compiles `core/` from source, so it sees another stream's
half-written `core/*.cpp` exactly as the root build would: two build attempts
failed on `core/mpp/reader.cpp` while that stream was mid-edit and the next
attempt succeeded. Retry; do not "fix" another stream's file.

**Inferred / to be proved by CI**: that `main.mm` compiles and links on macOS.
There is no Objective-C++ compiler on the development box (only
`clang-format`, which was used as a syntax sanity check). The macOS workflow
(`.github/workflows/app-macos.yml`) is therefore the verification: configure
`app/`, build `msproject_app.app` with warnings as errors, run `ctest`, then
assert the bundle executable exists and links AppKit.

## Open

1. **Visual fidelity** of the Gantt is unmeasured: bar/milestone/link geometry is
   `core/gantt`'s decision defaults (see `memory/gantt.md`), and the status line
   is a dashed marker, not the product's stepped progress line. A screenshot
   comparison is the next step.
2. **Real calendar shading** (holidays, per-task calendars) needs `core/cal` ->
   `GanttSource::nonworking_days`; the models already support it.
3. **Editing / selection / collapse** exist in `core/sheet` but are not wired, and
   wiring collapse would require the Gantt to be rebuilt from the visible rows
   (today row ordinal == row index).
4. **Two synced scroll views** (frozen sheet, horizontally scrolling Gantt) once
   fidelity work starts; the one-scroll-view shell is a first cut.
5. **Status date** should come from the file when the reader exposes it (or from
   a UI control), not from `today_date32()`.
