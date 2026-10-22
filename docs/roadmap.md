# Roadmap and progress

Updated at the end of each wave. Percentages are **effort-weighted judgements
against evidence**, not line counts. Each stage lists what would move its number
so the figure can be argued with.

Last update: wave 13, 2026-10-22 (assignment/resource add, app field/link editing,
214 view-state decode, fuzz hardening). Weights are relative effort for a solo
build. Total = 100.

| # | Stage | Weight | Complete | What is actually done | What moves it |
|---|---|---|---|---|---|
| 0 | Reconnaissance | 5 | **80%** | 54,450 functions decompiled, 2,060 classes, 89,486 vtable slots, 3,207 fields inventoried, naming at ~16% meaningful | only targeted lookups now, as the engine demands them |
| 1 | Container & record format | 7 | **96%** | OLE2 layout, all tables, VarMeta, Fixed2Data, `114/Props` records, per-table Props, `114/TBkndCal`, **214/CTable view-state framing (DateFormat measured)** | MPP12, genuinely added-column layout |
| 2 | Field model in code | 3 | **100%** | Generated 3,207-row table, C++ API, C ABI, 119,614 checks | — |
| 3 | `.mpp` reader | 8 | **98%** | All tables; PROJECT_A 83 / PROJECT_B 73; tombstone liveness; resource Type/MaxUnits; custom fields; effective dates, milestones, Resume | MPP12, MPP14 resource Type |
| 4 | Scheduling engine | 20 | **84%** | Constraints, deadlines, slack, links/lags, milestones, ALAP, elapsed lag/duration, per-task calendars, **recurring exceptions (both monthly-positional shapes measured)** | summary-calendar rollup, mixed elapsed cases |
| 5 | `.mpp` writer (round-trip) | 9 | **97%** | CFB round trip, task add/delete + cascade, custom fields Project-visible, link add/delete, field clear, **resource add + assignment add/delete (rows byte-identical to Project's)** | `214/` regeneration, `114/Props` writing, assignment custom fields |
| 6 | App shell (macOS) | 5 | **85%** | Menus, prefs, filter, Export PDF, dirty prompts, resource/usage views, insert/delete, stored/formula columns, **Task Fields and Predecessors dialogs with one pending stack** | multi-window, printing, manual smoke |
| 7 | Core views | 24 | **47%** | RecordSet/tables/filters/groups/sorts, sheet/Gantt/session, usage/resource models, real tables | wiring remaining edit gestures, contour shaping, remaining Gantt/sheet behaviours |
| 8 | Editing, undo, custom fields, formulas | 10 | **70%** | Sheet/app editing + undo, task/resource/assignment/link editing, custom fields read/write Project-verified, formula engine + file formula columns, **field/link dialogs and save ordering** | row move/indent, multi-select editing |
| 9 | Output: print, PDF, MSPDI, CSV | 5 | **55%** | MSPDI parity, CSV, PDF writer/reports, app Export PDF | print path, visual oracle |
| 10 | Parity long tail | 4 | **18%** | Error paths, CFB hardening, perf, tombstone/resource/elapsed/Props/exceptions, **formula nesting guard + deterministic fuzz + ASan pass** | MPP12, true column-write layout, CV_iew state |

## Overall

```
Stage  0  Reconnaissance            ████████████████░░░░  80%
Stage  1  Container/record format   ███████████████████░  96%
Stage  2  Field model in code       ████████████████████ 100%
Stage  3  .mpp reader               ███████████████████░  98%
Stage  4  Scheduling engine         █████████████████░░░  84%
Stage  5  .mpp writer               ███████████████████░  97%
Stage  6  App shell (macOS)         █████████████████░░░  85%
Stage  7  Core views                █████████░░░░░░░░░░░  47%
Stage  8  Editing / custom fields   ██████████████░░░░░░  70%
Stage  9  Output                    ███████████░░░░░░░░░  55%
Stage 10  Parity long tail          ████░░░░░░░░░░░░░░░░  18%
                                  ────────────────────────
TOTAL (weighted)                  ███████████████░░░░░░░  ~73%
```

## Honest reading of the number

73% and the structural writer is complete: tasks, links, resources and
assignments can all be added, and tasks/links/assignments deleted, with
Project-visible results; custom fields read and write with Project display. The
app now edits all of it (insert/delete, Task Fields, Predecessors) through one
pending-edit stack and one save order. Recurring exceptions are measured for
every type, including both monthly-positional shapes. The 214/ decoder found
where table date format lives (`CTable` payload +7) and corrected the earlier
"added column" premise (that oracle call never actually added one).

What is genuinely left: **`214/` view-state writing** (read is proven, the
column layout is not), assignment custom fields, `114/Props` formula writing,
MPP12/MPP14 variants, app multi-window/printing, and the manual Mac smoke test.
The fuzz/ASan pass is the last audit gate; one real bug (formula stack overflow)
was found and fixed with `kMaxFormulaDepth`.

## Milestones

| Milestone | Definition | State |
|---|---|---|
| M1 | opens a real `.mpp` and lists tasks | **done** |
| M2 | recalculates dates correctly and writes a file MS Project reopens | **done** -- 332/332; dates, task/link/resource/assignment edits and custom fields reopen |
| M3 | editable sheet + undo | **done** -- editing/undo, insert/delete, field and link dialogs, one save pipeline |
| M4 | Gantt and the other views | **partial** -- Gantt + Resource Sheet/Task/Resource Usage |
| M5 | prints/exports a PDF worth sending | **partial** -- PDF writer + app Export PDF; no visual oracle/print dialog |
| M6 | daily-driver parity for the projects actually worked on | **close** -- task/edit side works; needs view-state persistence and the Mac smoke |
