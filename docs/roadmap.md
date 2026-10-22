# Roadmap and progress

Updated at the end of each wave. Percentages are **effort-weighted judgements
against evidence**, not line counts. Each stage lists what would move its number
so the figure can be argued with.

Last update: wave 11, 2026-10-22 (Project-verified custom fields, delete
cascade, resource/usage app views, reader/engine parity). Weights are relative
effort for a solo build. Total = 100.

| # | Stage | Weight | Complete | What is actually done | What moves it |
|---|---|---|---|---|---|
| 0 | Reconnaissance | 5 | **80%** | 54,450 functions decompiled, 2,060 classes, 89,486 vtable slots, 3,207 fields inventoried, naming at ~16% meaningful | only targeted lookups now, as the engine demands them |
| 1 | Container & record format | 7 | **95%** | OLE2 layout, `114`/`214`, task rows, VarMeta, Fixed2Data, `114/Props` record framing, per-table Props registries, `114/TBkndCal`; **task add/tombstone + custom-field display contract measured and written** | MPP12 variants, `214/CTable`/`214/CV_iew` view state |
| 2 | Field model in code | 3 | **100%** | Generated 3,207-row C++ table, C++ API, C ABI, 119,614 checks | — |
| 3 | `.mpp` reader | 8 | **98%** | Task/resource/link/assignment tables; PROJECT_A 83, PROJECT_B 73; links 88/70 with tombstone liveness; resource Type/MaxUnits; custom fields read; effective dates, milestones, Resume; error paths, 4,000-task parse | MPP12, elapsed mixed cases, MPP14 resource Type |
| 4 | Scheduling engine | 20 | **80%** | Eight constraints, deadlines, four slack fields, 4 link types, measured lags, 0-d milestones, FF leads, ALAP resolved, elapsed lag/duration, **per-task calendars in CPM** | recurring calendar exception parameters (wave 12), summary-calendar rollup, mixed elapsed cases |
| 5 | `.mpp` writer (round-trip) | 9 | **90%** | CFB round trip, patching, stream replacement, row writer, SNET save, **add/delete task + cascade**, **custom-field write (Project displays all six values)**, link/assignment tombstones | link/assignment row create (wave 12), `214/` regeneration, assignment add, field delete/Cost |
| 6 | App shell (macOS) | 5 | **65%** | AppKit shell: menus, preferences, filter dialog, Export PDF, dirty prompts, clipboard, keyboard nav, **Resource Sheet + Task/Resource Usage views**, computed columns; app 8/8, 1,414 checks | Insert/Delete Task + stored-field columns (wave 12), multi-window, printing, manual smoke |
| 7 | Core views | 24 | **45%** | RecordSet/tables/filters/groups/sorts, real Task/Resource tables, sheet/Gantt/session, **core/usage day/week/month + task/resource projections, resource tables** | app editing wiring, remaining Gantt/sheet behaviours, usage contour shaping |
| 8 | Editing, undo, custom fields, formulas | 10 | **40%** | Field model, sheet/app editing + undo, row add/delete, **custom-field read/write Project-verified**, **core/formula parser/evaluator + computed columns**, **core/props formula recovery** | Insert/Delete in the app (wave 12), stored-field/formula columns in the UI |
| 9 | Output: print, PDF, MSPDI, CSV | 5 | **55%** | MSPDI parity, CSV, PDF writer + reports (pypdf), app Export PDF path | print path, visual oracle |
| 10 | Parity long tail | 4 | **10%** | Error paths, CFB hardening, perf, Resume, client-data tooling, tombstones, resource type, elapsed lags, Props decoder | recurring exceptions (wave 12), MPP12, view state |

## Overall

```
Stage  0  Reconnaissance            ████████████████░░░░  80%
Stage  1  Container/record format   ███████████████████░  95%
Stage  2  Field model in code       ████████████████████ 100%
Stage  3  .mpp reader               ███████████████████░  98%
Stage  4  Scheduling engine         ████████████████░░░░  80%
Stage  5  .mpp writer               ██████████████████░░  90%
Stage  6  App shell (macOS)         █████████████░░░░░░░  65%
Stage  7  Core views                █████████░░░░░░░░░░░  45%
Stage  8  Editing / custom fields   ████████░░░░░░░░░░░░  40%
Stage  9  Output                    ███████████░░░░░░░░░  55%
Stage 10  Parity long tail          ██░░░░░░░░░░░░░░░░░░  10%
                                  ────────────────────────
TOTAL (weighted)                  █████████████░░░░░░░░░  ~67%
```

## Honest reading of the number

67% and the writer is now genuinely useful: it can add and delete tasks (Project
reopens them correctly), cascade a delete over links and assignments, and write
custom-field values that **Project displays** — the three-store contract
(VarMeta position, both Props registries, FixedMeta tail bits) is the wave's main
hard-won result. The reader/engine closed the parity items that had names:
resource Type, per-task calendars, elapsed lag/duration, and link/assignment
tombstone liveness (PROJECT_B now matches Project's 70/63).

The app is a real multi-view shell (task sheet/Gantt, resource sheet, task and
resource usage, computed columns) but it cannot yet insert/delete a task or show
stored custom fields in a column — that is wave 12. The remaining structural
gaps are narrow but real: recurring calendar exceptions (fixture ready),
link/assignment creation, `214/` view-state decoding, MPP12, and assignment add.

## Milestones

| Milestone | Definition | State |
|---|---|---|
| M1 | opens a real `.mpp` and lists tasks | **done** |
| M2 | recalculates dates correctly and writes a file MS Project reopens | **done** -- 332/332; dates, task add/delete and custom fields all reopen |
| M3 | editable sheet + undo | **mostly done** -- editing/undo; **task add/delete is writer-complete, app UI in wave 12**; save persists starts only |
| M4 | Gantt and the other views | **partial** -- Gantt + Resource Sheet/Task Usage/Resource Usage in the app |
| M5 | prints/exports a PDF worth sending | **partial** -- PDF writer + app Export PDF; no visual oracle/print dialog |
| M6 | daily-driver parity for the projects actually worked on | not started -- needs app editing, stored custom fields, recurring exceptions, assignment add |
