# Wave 21 brief — the file's own table, the writer's slack, a faster Linux job

_2026-10-23, wave 21.  `main` at `22a765a`, clean; wave 20 closed in
`docs/handoff/wave20-complete.md`._

Wave 21 takes the **first open item from the wave-20 gate**: build-order step 3,
render what the file says (decision 7).  Two smaller, independent lanes ride
with it.  The dialog inventory (decision 14/15), the split view (16) and the dark
custom surfaces (10) are **not** in this wave; they are wave 22.

## Lanes and ownership (disjoint; no two lanes in one directory)

| lane | owns | goal |
|---|---|---|
| **Q6** | `app/view_model.*`, `app/document.*` (read access only), `app/CMakeLists.txt`, `app_qt/**`, `docs/handoff/q6.md`, `memory/q6.md` | Build-order step 3: the loaded task table uses the column set, order, titles and widths from the file's `214/CTable`; Entry is the default only for a new/empty project. |
| **E6** | `core/mppwrite/**`, `core/plan/**` (only if the MSO rule is implemented), `tests/mppwrite_*`, `tests/engine_diff_tests.cpp` (the `--dump` path), `docs/handoff/e6.md`, `memory/e6.md` | Mirror the CFB slack tolerance in `mppwrite::CfbDocument::parse` so `--dump sf248.mpp` loads with a warning; decide `constraint-mso` (implement the measured rule with the nine-row regression, or document why the dump stops comparing it). |
| **C6** | `.github/workflows/qt.yml`, `docs/handoff/ci3.md`, `memory/ci3.md` | Parallelise the Linux Make build (the job is ~18 min serial); keep every step, guard and artifact unchanged. No dispatch — the coordinator dispatches at the wave close. |

The coordinator owns: `docs/**` outside the lane handoffs, `memory/coordinator.md`,
`docs/roadmap.md`, oracle/COM, all git, all CI dispatch, and the wave gate.

## Gates that must stay green (each lane reports the exact command and tail)

* Root tree: `ctest --test-dir build\vs-w20 -C Release -E plan_recalc` = **50/50**;
  M2 `recalc corpus\project_a.mpp --compare corpus\probe\project_a-recalc.mpp` =
  **332/332**; engine suite **7/8** (only `constraint-mso`'s duration word).
* app_qt: **11/11**, and `parity_report.py corpus\bench\msproject-gantt.png
  <render>` = **9 comparable / 0 failing** with the current numbers (row 22,
  header 43, splitter 0.45924, day 24, tiers 2, columns −1…+1).
* Fixtures: `python scripts/check_fixtures.py` before reporting; nothing under
  `tests/data` may be written by a lane. No oracle hand-edits.

## Build recipe on this box (no Qt on PATH; the local Qt is in build/)

```
cmake = C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
ctest = the same directory\ctest.exe
Qt    = D:\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile\build\Qt\6.5.3\6.5.3\msvc2019_64

cmake -S app_qt -B build\q6-w21 -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_PREFIX_PATH="...\build\Qt\6.5.3\6.5.3\msvc2019_64" -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build build\q6-w21 --config Release *> build\q6-w21\build.log
ctest --test-dir build\q6-w21 -C Release --output-on-failure
```

* **Serial builds only.** `cmake --build --parallel` / MSBuild `/m` and piping
  `cmake --build` into a command both fail under the sandbox; redirect to a file.
* For ctest, prepend the Qt `bin` to `PATH`; the tests run `QT_QPA_PLATFORM=offscreen`.
* `plan_recalc` is never run by a lane (it rewrites the golden):
  use `plan_recalc_tests` with `MPP_NO_CORPUS=1`, or leave it to the coordinator.

## Q6 specifics (read before planning)

* `app_qt/plan_document.cpp::read_table_column_widths()` already parses the four
  `214/CTable` parts with `mppwrite::CfbDocument` +
  `msproject::viewstate::parse_view_state`; it currently keeps `(field_id, chars)`
  only.  The parsed `viewstate::ColumnDef` has `field_id`, `title` and
  `opaque_header[0]` (characters).  `TableDef::columns` is the order.
* `app::task_sheet_columns()` / `default_sheet_columns()` is the FIXED Entry set
  (its comment says so); decision 7 changes that assumption for loaded files.
  `core::sheet::SheetColumn` carries field/width/align/edit-policy.
* `app_qt/entry_columns.*` is the Entry presentation; `app_qt/sheet_table_model.*`
  maps display column ↔ model column through it; `MainWindow::configure_sheet()`
  applies widths from `doc_.table_column_chars()`.  `generic_` mode already
  displays a `SheetModel`'s own visible columns for the non-task views.
* **Acceptance**: a pure `app::` function that turns a `viewstate::TableDef` into
  `std::vector<core::sheet::SheetColumn>`, unit-tested with a constructed
  non-Entry table (order/titles/widths), and the Qt window displaying that set
  with a gate.  A new/empty project must still show the Entry defaults, and the
  nine-column parity render must not move (synthetic-base's stored table is the
  Entry table).
* Do not lose editing/undo, the save path, formula columns or the Gantt row map.
  If a stored column's field has no `SheetColumn` mapping, make it read-only and
  say so in the handoff rather than guessing.

## E6 specifics

* `core/mppwrite/cfb_writer.cpp` `CfbDocument::parse` (~line 376) rejects a short
  FAT; `core/mpp::CompoundFile` now warns instead (`warnings()`, wave 20 M6:
  sf248 = 191 unreferenced of 768/959).  Mirror the rule: short FAT = warning,
  a chain that walks past the FAT = fatal.  Keep the writer's byte output
  unchanged for valid files.
* `engine_diff_tests --dump corpus\imported\sf248.mpp` must then load and report
  the warning (it is a corpus path; do not commit corpus or its bytes).
* `constraint-mso`: prefer a documented decision over a fitted rule.  If you
  implement Project's save-time duration rule, the nine-row table in
  `docs/engine-parity.md` is the regression and no oracle may change; if you
  decide the dump should stop comparing that word, say why and make the suite
  read 8/8 without hiding a real difference.  Either way the four late-pair
  scenarios stay passing.

## C6 specifics

* Only the Linux `Build` step gets `--parallel` (Unix Makefiles); Windows is
  Ninja, macOS is Make.  Do not reorder steps, touch ctest/parity, or change the
  three package steps and the four uploads.
* Validate the YAML with Python (PyYAML 6 is installed) and report step order,
  the matrix, the Qt pin and the artifact names as the wave-20 CI2 handoff did.
  Shell syntax cannot be checked in this sandbox; say so.

## Reporting

Each lane writes `docs/handoff/<lane>.md` (what changed, what was measured, what
is inference) and `memory/<lane>.md` (short durable facts + traps), then reports
to the coordinator with: files changed, exact commands, numbers, and anything it
could not verify.  No git commands; the coordinator commits.


## Visual review — operator's Windows build on a real file (2026-10-23)

The operator ran the packaged Windows build (run 39898703443) on a real 83-task
construction programme at a 1914x1008 client and sent screenshots, including the
column-header and row context menus open.

**Working, seen in the capture:** the file opens with its 30 reader warnings
surfaced honestly; 88 links, 16 summaries, 10 milestones and 35 critical are
reported; summary bars, task bars, milestone diamonds with labels, link lines,
the week/month timescale, the caption strip, timeline Start/Finish, four view
buttons and both context menus are present. This is the first real-project
capture of the shell, not a fixture.

**Verified mismatches (shot, not inferred):**

1. **Painted header captions clip.**  "Task Mode" renders as "Task / Mod"
   because the sort chevron overlaps the final letter at the DEFAULT 57 px
   width; at the file's stored widths "Duration" renders "Dura" and
   "Predecessors" "Predecess…".  The reference fits "Task"/"Mode" + arrow in
   57 px.  Assigned to Q6 (caption layout + a painted gate), after the column
   work.
2. **The status bar's left segment is not "Ready".**  A loaded file shows the
   diagnostic summary there ("83 tasks, 88 links … 30 reader warnings … Entry
   widths …").  Decision 8 says "Ready left".  Assigned to Q6: keep Ready (or
   the active message), move the summary to a tooltip/right detail, keep the
   warning count visible.
3. **The timeline strip draws EVERY task**, not the tasks added to the timeline
   (`MainWindow::refresh_timeline` iterates `doc_.plan().tasks`).  On a real file
   that reads as a segmented bar.  Decision 5 asks for the tasks added to the
   timeline; until the file's timeline entries are decoded (or add-to-timeline
   works), an honest empty prompt is better than all tasks.  Not in Q6's scope.
4. **A classic menu bar is present** (File Edit View Insert Help) above the QAT;
   the reference capture has none.  The spec allows the OS's native menu-bar
   placement but does not ask for a desktop menu bar.  Operator decision needed.
5. **Bar palette in this file:** normal bars are painted with the completion
   colour (`#2f5f9a`) across their full width, so no critical red is visible even
   though 35 tasks are critical.  This is expected if the tasks are 100 %
   complete (the fill covers the body), but it cannot be judged without MS
   Project's own rendering of this file.  Do NOT change bar colours on the
   strength of this shot.

**Needs Project's own capture of the same file** (oracle lock, copy first): the
bar palette/critical treatment, the Indicators badges, the timeline strip's
real content, and the status bar in a loaded file.  Project wins every
disagreement; the default-theme reference has no bars, so these cannot be
settled from `corpus/bench/msproject-gantt.png`.
