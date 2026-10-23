# Wave 22 brief — the ribbon inventories, the stored columns' values, the new-task mode

_2026-10-23, wave 22.  `main` at `2911c40`; wave 21 gate:
`docs/handoff/wave21-complete.md`._

Wave 22 takes the first two open items from the wave-21 gate: **decision 1**
(the seven ribbon tabs must carry Project's group/button inventory) and the
remaining half of **decision 7** (stored columns whose values are still blank),
plus the **new-task mode** the operator's comparison exposed.  Dialogs (14/15),
the split view (16), dark surfaces (10) and the pixel diff (12) are wave 23.

## Lanes and ownership

| lane | owns | goal |
|---|---|---|
| **R6** | `app_qt/ribbon_bar.{h,cpp}`, `app_qt/CMakeLists.txt` (the only owner), a ribbon-inventory gate (new test file or `tests/parity_test.cpp`), `docs/ui-command-backlog.md` (regenerate), `docs/handoff/r6.md`, `memory/r6.md` | Every one of the seven tabs carries the group/button inventory and order measured from the clean MS Project strips; unimplemented buttons stay disabled with the exact tooltip; the backlog regenerates. |
| **S6** | `app_qt/entry_columns.{h,cpp}`, `app_qt/sheet_table_model.{h,cpp}`, `app_qt/sheet_views.{h,cpp}`, `app_qt/plan_document.{h,cpp}`, `app_qt/tests/stored_table_test.cpp`, `docs/handoff/s6.md`, `memory/s6.md` | The stored columns Project shows with values — Predecessors, Successors, Resource Names, % Complete, the file's SUMMARY/Yes flags — render their values in the task sheet (or are documented with byte evidence as unsourceable). |
| **E7** | `core/mpp/**`, `app_qt/main_window.{h,cpp}` (status wiring only), `tests/mpp_reader_tests.cpp`, `docs/handoff/e7.md`, `memory/e7.md` | Find the project's new-task scheduling default in the file; expose it on the reader and make the status bar's "New Tasks : …" follow it. If it is not decodable, document the bytes searched and leave the status honest. |

The coordinator owns `docs/**` outside lane handoffs, `memory/coordinator.md`,
`docs/roadmap.md`, git, CI and the wave gate.

**No lane touches another lane's files.**  In particular S6 must not edit
`main_window.cpp`; E7 owns the status wiring there.  If a lane needs a shared
file, report it instead of editing.

## Evidence and acceptance

* **R6**: measure each tab from
  `corpus/reference/2026-10-23-msproject/project-ribbon-{resource,report,project,view,help,gantt-format}.png`
  and the Task tab from `project-gantt-loaded-clean.png` / the Resource
  full-window capture.  Record the inventory in the handoff (group → buttons, in
  order), implement it, regenerate `docs/ui-command-backlog.md`, and gate it:
  for every tab, the model exposes the expected groups/buttons in order; at
  1920 the visible groups are a prefix, no caption/button clips, and the `>>`
  chevron appears when the full inventory does not fit.  Project itself
  overflows, so "all visible at 1920" is NOT the criterion — the inventory is.
* **S6**: prove values on `corpus/project_a.mpp` read-only (it has the non-Entry
  table) and on the synthetic non-Entry table.  Predecessors/Successors/Resource
  Names can come from the plan/records the sheet already holds; % Complete
  already shows values in the current render (verify); SUMMARY/Yes are likely
  custom flag fields — map them by field id/name if the RecordSet carries them,
  otherwise list the exact ids and why.  A blank column that Project fills is a
  failure unless the bytes genuinely do not carry the value.
* **E7**: search `mpp::ProjectRow`/Props/project records for the "new tasks are
  manually scheduled" flag (Project 2021's Options ▸ Schedule default).  Expose
  it (e.g. `ProjectRow::new_tasks_manual`), test it on the committed fixtures and
  on `corpus/project_a.mpp` (the operator's file says Auto Scheduled in Project;
  the fixture's own value is whatever the bytes say), and wire the status label.
  No guessed bit: if a candidate is not provable against a file, do not ship it.

## Gates (each lane reports exact commands and tails)

* Root: `ctest -E plan_recalc` **50/50**, M2 **332/332**, engine suite **7/8**,
  reader **984** and cal **785** unchanged (E7 may raise the reader count).
* app_qt: **14/14** (13 in CI) and UI parity **9/9** must stay; app/ headless
  **15/15**.
* `python scripts/check_fixtures.py` before reporting; nothing under
  `tests/data` is written; `corpus/` is read-only.

## Build recipe (serial, no Qt on PATH)

```
cmake = C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
ctest = the same directory\ctest.exe
Qt    = D:\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile\build\Qt\6.5.3\6.5.3\msvc2019_64
app_qt: cmake -S app_qt -B build\<lane> -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="<Qt>" -DCORE_WARNINGS_AS_ERRORS=ON
root:   cmake -S . -B build\<lane> -G "Visual Studio 17 2022" -A x64 -DCORE_BUILD_TESTS=ON
build:  cmake --build build\<lane> --config Release *> build\<lane>\build.log   (SERIAL; no --parallel, no pipes)
```

`plan_recalc` is never run; `plan_recalc_tests` only with `MPP_NO_CORPUS=1`.
Python text writes need `newline="\n"`.  No git, no CI dispatch, no COM.

## Reporting

Each lane writes `docs/handoff/<lane>.md` (what changed, exact commands/numbers,
verified vs inferred, what could not be verified) and `memory/<lane>.md`, then
reports to the coordinator.  The coordinator commits each lane separately,
dispatches CI once for the wave, and writes `docs/handoff/wave22-complete.md`.
