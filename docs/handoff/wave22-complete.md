# Wave 22 complete — the measured ribbon, the stored columns' values, the new-task mode

_2026-10-23.  `main` at **`abeafd2`** (the code CI validates; the docs close
follows it).  CI run **39912313201**.  Successor: read `docs/START-HERE.md`
first; this is the wave-22 gate._

Wave 22 took the next two items from the wave-21 gate: **decision 1** (the seven
ribbon tabs must carry Project's real inventory) and the rest of **decision 7**
(the stored columns Project fills), plus the **new-task scheduling default** the
operator comparison exposed.

## The gate

Locally on `build\vs-w22` (Release, `/W4`), CI run 39912313201 on `abeafd2`:

| gate | result |
|---|---|
| `ctest --test-dir build\vs-w22 -C Release -E plan_recalc` | **50/50 passed** |
| M2 `recalc corpus\project_a.mpp --compare corpus\probe\project_a-recalc.mpp` | **332/332** (268/268 leaves, 64/64 summaries) |
| `plan_recalc_tests` (`MPP_NO_CORPUS=1`) | **266 checks, 0 failures** |
| `engine_diff_tests` self-test | **265 checks, 0 failures** |
| `mppwrite_roundtrip_cfb` | **1693 checks, 0 failures** |
| `mpp_reader_tests` / `cal_tests` | **1016** (was 984) / **785 checks** |
| engine parity `--suite` | **8 recorded, 7 pass, 1 fail** |
| app_qt ctest (local Qt 6.5.3, `/W4 /WX`) | **15/15** (14 in CI: the corpus stored-table entry is only registered with `corpus/project_a.mpp`) |
| app/ headless | **15/15** |
| UI parity (`parity_report.py`, 1920×1032) | **9 comparable, 0 failing** |
| fixtures | `fixtures OK (3 documented hashes, 3 tracked .mpp)` |

CI run **39912313201** on `abeafd2`: `windows-qt`, `macos-qt` and `linux-qt`
all **success**, each **14/14** ctest (the corpus stored-table entry exists only
locally; locally app_qt is 15/15).  Packages below.

## What landed

### Decision 1 — the measured ribbon (R6)

Every tab now carries the group/button inventory and order read from the clean
MS Project 2021 captures (Task from the loaded-file capture; the other six from
the ribbon strips).  Two readings corrected the brief:

* View ▸ Data stacks **Highlight above Filter**, above Group by;
* View ▸ Window carries **Switch Windows** between New Window and Arrange All.

| tab | groups / buttons at 1920 |
|---|---|
| Task | 8 / 30 |
| Resource | 5 / 13 (Team Planner; Assignments; Insert; Properties; Level x6) |
| Report | 3 / 10 |
| Project | 6 / 14 |
| View | 7 / 26 (incl. Resource Views, Split View, Window, Macros) |
| Help | 1 / 3 |
| Gantt Chart Format | 6 / 17 (incl. the caption-only 16-swatch style group) |

The new `qt_ribbon_inventory` gate pins tab titles, group titles, every button
label and its kept `MenuAction`, the disabled tooltip/enablement rule, the 1920
visible-prefix/no-clip/chevron rule and the same at 900, and the contextual tab
only in the Gantt view.  The generated backlog is now **113 ribbon + 43 menu
rows** (102 ribbon disabled).  Commands without an action are `none` → disabled
with the exact tooltip.

### Decision 7 — the stored columns' values (S6)

Predecessors, Successors, Resource Names and % Complete get their text from the
plan/links/assignments and are written into the RecordSet under the stored field
ids, so the model and delegate paint them with no view special case.  Measured on
`corpus/project_a.mpp`:

* Predecessors: `9FS+50 days`, `33SS,57`, `17FS+20 days`, `3`;
* Successors: `10FS+50 days`, `18,33FS+20 days,56,57,25`, `40,36`;
* % Complete: `57%`, `100%`, `29%` (Project's sign now printed).

The link syntax is taken from the operator's clean capture: commas without
spaces, a bare id for a lag-free FS, FS kept when lagged, SS/FF/SF always
printed, file link order.  The wave-22 gate checked **83/83 rows** against the
app's own `mpp::load_file` reader, adapter and formatter path -- a wiring check,
not an independent oracle (the audit's A-2).  Wave 23 replaces the expected text
with Project's own `Task.Predecessors`/`Task.Successors` strings read through
COM and pinned as a hash (`tests/data/project_a_preds_golden.sha256`).

Two columns stay blank, asserted with byte reasons rather than invented:

* **SUMMARY** is `Task.Flag10 0x0b400051` (not `Task.Summary`), a FixedMeta bit
  the reader decodes for Flag1 only — a reader task;
* **Resource Names** is not in this file's table and all 67 assignment rows carry
  the mpxj null-resource word (`0xFFFF0001`); the derivation itself is proven on
  a constructed plan.

### The new-task scheduling default (E7)

Project stores it in the project props record **`0x024013c8`** (kind 2, u16):
**0 = Auto Scheduled, 1 = Manually Scheduled**.  The id comes from Project's own
property-name table (`QNEWTASKSAREMANUAL` at index 64), cross-checked on its
neighbouring properties and against Project's MSPDI `<NewTasksAreManual>` on
10/10 exports; 133 `.mpp` files were scanned (record present and kind 2 / length 2
every time; absent only on original 2007 MPP12).  A down-saved MPP12 keeps it in
`112/Props`.

The reader exposes `ProjectRow::new_tasks_manual` / `_stored` (and a path-only
`read_new_task_mode`); the Qt status centre now says **"New Tasks : Auto
Scheduled"** for `corpus/project_a.mpp` and the committed Manual fixture, keeps
Manual for a new/empty project, and falls back to Manual with an explicit tooltip
when the file does not store the setting.  No writer for the setting yet.

## Packages

CI run **39912313201**, all three jobs green:

| artifact | file | verified |
|---|---|---|
| `msproject-windows-abeafd2…` | `msproject-windows.zip` (48.2 MB, 30 entries) | `dist/msproject_qt.exe` + windeployqt DLLs |
| `msproject-macos-abeafd2…` | `msproject-macos.zip` (21.6 MB, 73 entries) | `MS Project Shell.app/Contents/MacOS/msproject_qt` + frameworks/cocoa plugin |
| `msproject-linux-abeafd2…` | `msproject-linux-x86_64.AppImage` (32.8 MB) | type-2 AppImage (`\x7fELF`, `AI\x02`); the smoke step rendered 900x560 offscreen |
| `qt-window-<OS>-abeafd2…` | PNGs | the 1920x1032 parity render scored **9/9 on all three**, plus the empty/chrome/header-band renders |

The Linux Build step took **2m10s** with `--parallel 2` on this larger tree
(serial was 2m55s at wave 20; wave 21's smaller tree was 1m42s).  No graph race
and no `jobserver unavailable`.

## The 16 decisions — status after wave 22

| # | decision | state | evidence / gap |
|---|---|---|---|
| 1 | all seven ribbon tabs | **DONE** | measured inventories/order for all seven; gate pins them.  Reachability caveat below. |
| 2 | full backstage | **DONE** | 11 entries, Options pinned, back arrow/Escape. |
| 3 | original icon set | **DONE** | monochrome QPainter glyphs; no Microsoft bitmaps. |
| 4 | measured chrome metrics | **DONE** | 30/95/64/32, QAT 24, no menu bar. |
| 5 | timeline strip | **PARTIAL** | draws every task, not the tasks added to the timeline. |
| 6 | real Entry table | **DONE** | nine Entry columns, header menu, gutter, first row. |
| 7 | render the file's stored table | **PARTIAL** | columns/order/titles/widths and Pre/Suc/Resource Names/% Complete values now render; SUMMARY (Flag10) is a reader gap, bar styles are not decoded. |
| 8 | status bar | **DONE** | Ready left; the centre follows the loaded file's new-task mode. |
| 9 | disabled commands + generated backlog | **DONE** | 113 ribbon + 43 menu rows generated; disabled tooltip. |
| 10 | light AND dark themes | **PARTIAL** | selection/critical measured; dark custom surfaces still light; file bar styles not decoded. |
| 11 | new project defaults | **DONE** | today/Manual/callout/timeline; plus the loaded-file mode now follows the file. |
| 12 | structural measurement | **PARTIAL** | structural gates; no per-screen pixel diff. |
| 13 | Carlito bundled | **DONE** | four OFL faces, per-exe resources, gated. |
| 14 | every dialog on every tab | **PARTIAL** | seven dialogs; the rest of the inventory missing. |
| 15 | dialog semantics | **PARTIAL** | Task Information General functional, five tabs laid out. |
| 16 | View ▸ Timeline / Details split | **NOT DONE** | no toggle, no Details pane. |

### Reachability caveats introduced by the measured ribbon

Inventory-faithful, but with the menu bar hidden some commands lost their mouse
path and need a home in wave 23:

* **About and Keyboard Shortcuts** left Help (Project's Help tab is Help /
  Feedback / Show Training) → **About is currently mouse-unreachable**; Project
  exposes it under File ▸ Account, which the spec's backstage does not list.
* **Save-as-PDF / Export-PDF** left the Report ribbon (Project's Report tab has
  only Visual Reports); the backstage Export/Print paths remain.
* The **zoom buttons**, **toggle shading** and the three **view buttons** left
  their ribbon groups; the status-bar zoom slider/four view buttons and the
  hidden menu/shortcuts remain.
* The Font box now reads **Arial 8**, which is what the operator's loaded project
  shows; our actual UI font is Carlito.  It is a cosmetic control value pinned by
  the gate — a deliberate decision, not drift.

## Open items, ranked

1. **Restore reachability without breaking the inventory**: put About/Keyboard
   Shortcuts (and Save-as-PDF/Export-PDF) in the backstage, or add a Help page
   that is still Project-shaped; decide the font-box value.
2. **SUMMARY (Flag10) decoding** in `core/mpp` so the file's SUMMARY column
   renders Yes/No; then no `app_qt` change is needed.
3. **A fixture with a real link and a real assignment** (coordinator, COM
   oracle): CI cannot show a non-empty file-derived Predecessor today, and no
   corpus file has a populated Resource Names column.
4. **Dialogs 14/15** and the **split view 16**, then dark custom surfaces (10)
   and a per-screen pixel diff (12).
5. **Bar-style decoding**, the clipboard display-order limitation, the
   render-selection workaround.
6. **New-task writer** (needs a Project acceptance fixture); macOS smoke and
   notarization; sf248 task recovery.

## Traps this wave paid for

* **Pixel-over-brief**: the brief's starting inventory was wrong in two places;
  the captures won (Highlight above Filter, Switch Windows).  Always re-read the
  strip, not the prose.
* **`Task.Flag10` is not `Task.Summary`**: the file's SUMMARY column is a custom
  flag bit, not the Summary field.  A blank column is better than a wrong one.
* **A file's table can be wider than the 1920 pane**: project_a's 1200 px table
  clips Pre/Suc/SUMMARY at 1920 (the wide 2680 render shows them).  Scaling the
  pane or the column widths would be worse; this is not a regression.
* **CI has no corpus**: the corpus stored-table gate registers only locally, so
  app_qt is 14 in CI, 15 here.
* A transient `LNK1104` appeared while two lanes wrote the tree; rebuilding the
  target fixed it (a coordination artefact, not a code defect).
* Serial builds, `newline="\n"`, `gh run download` fresh dir, `plan_recalc` never
  run with the corpus.

## Next three actions

1. Restore About/Keyboard Shortcuts and the PDF/shading/zoom paths through the
   backstage (wave 23), and decide the font-box value.
2. Decode Flag10 in `core/mpp`; add a link/assignment fixture through the oracle.
3. The dialog inventory (14/15) and the split view (16).
