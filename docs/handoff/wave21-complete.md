# Wave 21 complete — the file's own table, the writer's slack, a faster Linux job

_2026-10-23.  `main` at **`f156ee3`** (the docs close; the code CI validated is
**`0f5920a`**, CI run 39904570421).  Successor: read `docs/START-HERE.md` first;
this file is the wave gate for wave 21._

Wave 21 took the first open item from the wave-20 gate — build-order step 3,
**decision 7, render the file's own table** — and closed the two smaller lanes
that rode with it.  It also landed the operator's review fixes from comparing the
packaged build with MS Project on the same real file.

## The gate

Locally on `build\vs-w21` (Release, `/W4`), CI run 39904570421 on `0f5920a`:

| gate | result |
|---|---|
| `ctest --test-dir build\vs-w21 -C Release -E plan_recalc` | **50/50 passed** |
| M2 `recalc corpus\project_a.mpp --compare corpus\probe\project_a-recalc.mpp` | **332/332** (268/268 leaves, 64/64 summaries) |
| `plan_recalc_tests` (`MPP_NO_CORPUS=1`) | **266 checks, 0 failures** |
| `engine_diff_tests` self-test | **265 checks, 0 failures** |
| `mppwrite_roundtrip_cfb` | **1693 checks, 0 failures** |
| `mpp_reader_tests` / `cal_tests` | **984** / **785 checks** |
| engine parity `--suite` | **8 recorded, 7 pass, 1 fail** |
| app_qt ctest (local Qt 6.5.3, `/W4 /WX`) | **14/14** (13 in CI: the corpus entry is only registered when `corpus/project_a.mpp` exists) |
| app/ headless | **15/15** (`views_tests` 215 checks) |
| UI parity (`parity_report.py`, 1920×1032) | **9 comparable, 0 failing** |
| fixtures | `fixtures OK (3 documented hashes, 3 tracked .mpp)` |

CI run **39904570421** on `0f5920a`: `windows-qt`, `macos-qt` and `linux-qt`
all **success**, each **13/13** ctest (the corpus entry exists only on this
box).  See "Packages" below.

## What landed

### Decision 7 — the stored table (Q6)

The task sheet now builds **both its model and its display** from the first
`214/CTable` table of the loaded file: column set, order, titles and widths, with
the fixed Entry presentation only for a new/empty project (or a file with no
readable CTable).

* `app::table_sheet_columns(TableDef)` is a pure, unit-tested API; the three
  `…Text` ids Entry tables store map to the base fields
  (`DurationText`→`Duration`, `StartText`→`Start`, `FinishText`→`Finish`),
  which is required for the values to render at all.
* Editable through the sheet: Name, Duration, Start, Finish.  Everything else is
  read-only (ID/Indicators/Task Mode/Predecessors/Successors/Resource Names/
  % Complete/unknown fields).
* Proven on the committed fixture (whose stored table *is* Entry, so parity does
  not move), on a synthetic non-Entry table written under `build/`, and on
  `corpus/project_a.mpp` read-only: titles `""/Task Name/Dur/Start/Finish/
  % Complete/Pre/Suc/SUMMARY/Add New Column`, widths
  `39/423/57/117/129/45/81/81/105/123`.

### The operator's review fixes (Q6, from the MS Project comparison)

* The classic menu bar is **hidden** (the QActions stay alive for shortcuts and
  the gates); the QAT/ribbon move up.
* The File backstage has the **circular top-left back arrow**; Escape also
  leaves it.
* **Double-click opens the six-tab Task Information dialog** (General
  functional over `app/`; Predecessors/Resources/Advanced/Notes/Custom Fields
  laid out with their primary action disabled and a note), and double-click no
  longer starts a cell edit.  F2 and typing still edit.
* Palette: selection **`#A2D3A0`**, critical bars **`#FF0000`**, and a critical
  bar stays red at 100 % complete instead of being covered by the progress fill.
* Header captions reserve the chevron/sort-arrow rect before wrapping, wrap at
  spaces/hyphens and never elide a fitting word; the painted band is written to
  `test-out/qt-header-band.png`.
* Status bar left is **"Ready"**; the diagnostic summary moved to the right
  segment (with the full text as its tooltip).

### mppwrite CFB slack (E6)

`CfbDocument::parse` mirrors the reader: a FAT shorter than the file's sector
count is a **warning** (`warnings()`), while a chain that walks past the FAT
stays a positioned error.  `engine_diff_tests --dump corpus\imported\sf248.mpp`
now exits 0 with the 191/768/959 warning; a slack document is readable but not
re-emittable (`check()` says not round-trippable, `to_bytes()` refuses).  A
synthetic corpus-free regression covers both directions (1693 checks).

### The MSO save-time word — measured, not scored

`constraint-mso`'s field is Project's save-time duration word on a manual pinned
MSO row with an off-calendar start: `+84 = plan duration + the remaining
standard-day windows after the start clock` (12000 = 9600 + 2400).  The rule is
implemented as a pure function and pinned by the nine-row table plus five
end-to-end cases, but **only behind an explicit self-test opt-in**
(`Applier::model_project_save`).  The scored scenarios and the production save
path still write 9600, so the suite stays **7/8**.  An 8/8 obtained by patching
the comparison applier would not be a product result.

### Linux CI is parallel (C6)

The Build step branches on `runner.os`: Linux (Unix Makefiles) runs
`cmake --build build-qt --config Release --parallel 2`; Windows (Ninja) and
macOS keep the exact previous command.  The whole-file diff is one hunk, +14/−1;
a validator built from a pre-edit snapshot asserts the 17-step order, matrix, Qt
pin and all four artifacts are otherwise unchanged.  The wave dispatch proved
the build itself: **2m55s → 1m42s**, no graph race, no `jobserver unavailable`.

## Packages

CI run **39904570421**, all three jobs green:

| artifact | file | verified |
|---|---|---|
| `msproject-windows-0f5920a…` | `msproject-windows.zip` (48.1 MB, 30 entries) | `dist/msproject_qt.exe` + windeployqt DLLs |
| `msproject-macos-0f5920a…` | `msproject-macos.zip` (21.5 MB, 73 entries) | `MS Project Shell.app/Contents/MacOS/msproject_qt` + frameworks/cocoa plugin |
| `msproject-linux-0f5920a…` | `msproject-linux-x86_64.AppImage` (32.8 MB) | type-2 AppImage (`\x7fELF`, `AI\x02`); the smoke step rendered 900x560 offscreen |
| `qt-window-<OS>-0f5920a…` | PNGs | the 1920x1032 parity render scored 9/9 on **all three**, plus the empty/chrome/header-band renders |

The Linux **Build step** went **2m55s serial (run 39898703443) → 1m42s with
`--parallel 2`** (~1.7x) on a larger tree.  The whole Linux job took 13m10s vs
10m53s because the apt step alone took 8m13s this run; the build measurement,
not the job wall-clock, is the evidence for C6.  No graph race and no
"jobserver unavailable" appeared in the log.

## The 16 decisions — status after wave 21

| # | decision | state | evidence / gap |
|---|---|---|---|
| 1 | all seven ribbon tabs | **PARTIAL** | all seven tabs exist, but the group/button inventories are not Project's: our Resource tab has 3 groups against 5, our View tab 3 against 7 (missing Resource Views, Split View, Window, Macros, most of Data).  The clean `project-ribbon-*.png` captures in the local reference set are the target. |
| 2 | full backstage | **DONE** | 11 entries with Options pinned; the back arrow and Escape now return; Info/Share honest local text. |
| 3 | original icon set | **DONE** | monochrome QPainter glyphs, 16/32 px; no Microsoft bitmaps. |
| 4 | measured chrome metrics | **DONE** | 30/95/64/32, group caption, QAT 24, Tell-me; menu bar hidden (Project has none). |
| 5 | timeline strip | **PARTIAL** | strip, label, Start/Finish and task bars are real, but `refresh_timeline()` draws **every** task, not the tasks added to the timeline. |
| 6 | real Entry table | **DONE** | nine display columns, header menu, frozen gutter, first empty row; unchanged. |
| 7 | render the file's stored table | **PARTIAL** | columns/order/titles/widths and the four editable fields are done; stored columns with no RecordSet value (Predecessors/Successors/Resource Names/SUMMARY/Yes) render blank and read-only, and the file's bar styles are not decoded. |
| 8 | status bar | **DONE** | Ready left (fixed), "New Tasks : …" centre, four view buttons, zoom; the loaded-file diagnostics moved right. |
| 9 | disabled commands + generated backlog | **DONE** | `command_status.h`, exact tooltip, generated `docs/ui-command-backlog.md`; dialog rows still to be added. |
| 10 | light AND dark themes | **PARTIAL** | selection `#A2D3A0` and critical `#FF0000` are now measured; the dark palette still only swaps the QPalette (custom-painted surfaces stay light), and the file's green/navy bar styles are not decoded. |
| 11 | new project defaults | **DONE** | today, Manually Scheduled, callout, Entry headers, timeline today.  (A *loaded* file's new-task mode is still not decoded — see open items.) |
| 12 | structural measurement | **PARTIAL** | structural gates at 1920×1032 and 900×560, both themes; the pixel-difference report is not run per screen. |
| 13 | Carlito bundled on all platforms | **DONE** | four OFL faces, per-executable Qt resources, family gated; CI green. |
| 14 | every dialog on every tab | **PARTIAL** | seven: Preferences, Table/Date Format, Custom Formula, Task Fields, Predecessors, Filter and the new six-tab Task Information.  The rest of the inventory is still missing. |
| 15 | dialog semantics | **PARTIAL** | Task Information General is functional; its other five tabs are laid out with a disabled primary action and a note.  Other laid-out-only dialogs still do not exist because 14 is incomplete. |
| 16 | View ▸ Timeline / Details split | **NOT DONE** | no toggle and no Details pane; Project's View tab has both (in the clean capture). |

## Open items, ranked

1. **Ribbon tab inventories (decision 1)** — rebuild Resource/Report/Project/
   View/Help/Gantt Format from the clean strips.  This is the top of wave 22.
2. **Stored-column values and bar styles** — Predecessors/Successors/Resource
   Names/SUMMARY/Yes render blank; the file's green/navy bar styles are not
   decoded.  Either map/paint those fields or record the decision.
3. **The loaded file's new-task mode** — Project says Auto Scheduled for the real
   file; our status says Manual.  `mpp::ProjectRow` exposes no such field yet.
4. **Non-Start edits in a save** — Q6 records that only Start edits are written
   by the current save path; verify against `qt_save_reopen` and wire Name/
   Duration/Finish if the gap is real.
5. **Dialogs 14/15** and **split view 16**, then dark custom surfaces (10) and a
   per-screen pixel diff (12).
6. **Bar-style decoding**, the clipboard display-order limitation, and the
   render-selection workaround (see traps).
7. **macOS launch smoke + notarization** (procurement), **sf248 task recovery**.

## Traps this wave paid for

* **`plan_recalc` rewrites the golden.**  It was run once directly (the full
  corpus test, 512 checks) and wrote `tests/data/project_a_recalc_golden.tsv`;
  `git status` showed no change (identical content), and it is never a gate.
  Run `plan_recalc_tests` only with `MPP_NO_CORPUS=1`.
* **P1's rule scan reads a full-width green selection band as a grid rule**, so
  `--render` clears the selection; the interactive window keeps the measured
  green and `qt_ux_gate` gates it.  The parity artifact therefore has no selected
  row; a cell-selection design would remove the workaround.
* **Qt 6 offscreen has no fonts** — Carlito is bundled per executable (a static
  library resource is dropped by the linker).
* **Green/navy bars in the real file are bar styles**, not a universal palette;
  only selection and critical red were adopted.
* **CI needs a fresh look at Linux `--parallel 2`**: a build-graph race that
  serial Make hid would show as "No rule to make target"/missing moc;
  `jobserver unavailable` in the log means the flag was dropped.
* Serial builds only in the sandbox; piped `cmake --build` fails; Python text
  writes need `newline="\n"`; `gh run download` refuses existing files.

## Next three actions

1. Wave 22: the seven ribbon tab inventories from the clean MS Project strips
   (decision 1), then stored-column values/bar styles and the new-task mode.
2. The dialog inventory (14/15) and the View ▸ Timeline/Details split (16).
3. A per-screen pixel-difference report (12) and the dark custom surfaces (10).
