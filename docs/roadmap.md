# Roadmap and progress

Updated at the end of each wave. Percentages are **effort-weighted judgements
against evidence**, not line counts. Each stage lists what would move its number
so the figure can be argued with.

Last update: wave 12, 2026-10-22 (app Insert/Delete Task, stored fields and file
formulas in the app, link add/delete, recurring calendar exceptions). Weights are
relative effort for a solo build. Total = 100.

| # | Stage | Weight | Complete | What is actually done | What moves it |
|---|---|---|---|---|---|
| 0 | Reconnaissance | 5 | **80%** | 54,450 functions decompiled, 2,060 classes, 89,486 vtable slots, 3,207 fields inventoried, naming at ~16% meaningful | only targeted lookups now, as the engine demands them |
| 1 | Container & record format | 7 | **95%** | OLE2 layout, task/cons/assn tables, VarMeta, Fixed2Data, `114/Props` records, per-table Props, `114/TBkndCal`; add/tombstone/link-add/custom-field contracts measured | MPP12, view-state streams |
| 2 | Field model in code | 3 | **100%** | Generated 3,207-row table, C++ API, C ABI, 119,614 checks | — |
| 3 | `.mpp` reader | 8 | **98%** | All tables; PROJECT_A 83 / PROJECT_B 73; tombstone liveness; resource Type/MaxUnits; custom fields; effective dates, milestones, Resume | MPP12, MPP14 resource Type |
| 4 | Scheduling engine | 20 | **84%** | Constraints, deadlines, slack, links/lags, milestones, ALAP, elapsed lag/duration, per-task calendars, **recurring calendar exceptions** (all types, Project-count validated) | summary-calendar rollup, mixed elapsed cases |
| 5 | `.mpp` writer (round-trip) | 9 | **94%** | CFB round trip, row writer, SNET save, task add/delete + cascade, **custom fields Project-visible**, **link add/delete (four Cons streams)**, field clear | assignment row add/delete, `214/`, `114/Props` writing |
| 6 | App shell (macOS) | 5 | **75%** | Menus, prefs, filter, Export PDF, dirty prompts, clipboard, keyboard, resource/usage views, **Insert/Delete Task with undo and save-reload**, **stored-field and file-formula columns** | custom-field/link editing UI, multi-window, printing, manual smoke |
| 7 | Core views | 24 | **47%** | RecordSet/tables/filters/groups/sorts, sheet/Gantt/session, usage/resource models, real tables | wiring remaining edit gestures, contour shaping, remaining Gantt/sheet behaviours |
| 8 | Editing, undo, custom fields, formulas | 10 | **55%** | Sheet/app editing + undo, task add/delete, custom fields read/write Project-verified, formula engine + file formula columns, **app structural editing** | writing custom fields from the UI, link/assignment editing, row move/indent |
| 9 | Output: print, PDF, MSPDI, CSV | 5 | **55%** | MSPDI parity, CSV, PDF writer/reports, app Export PDF | print path, visual oracle |
| 10 | Parity long tail | 4 | **14%** | Error paths, CFB hardening, perf, Resume, client-data tooling, tombstones, resource type, elapsed, Props, **recurring exceptions** | MPP12, view state, assignment add |

## Overall

```
Stage  0  Reconnaissance            ████████████████░░░░  80%
Stage  1  Container/record format   ███████████████████░  95%
Stage  2  Field model in code       ████████████████████ 100%
Stage  3  .mpp reader               ███████████████████░  98%
Stage  4  Scheduling engine         █████████████████░░░  84%
Stage  5  .mpp writer               ███████████████████░  94%
Stage  6  App shell (macOS)         ███████████████░░░░░  75%
Stage  7  Core views                █████████░░░░░░░░░░░  47%
Stage  8  Editing / custom fields   ███████████░░░░░░░░░  55%
Stage  9  Output                    ███████████░░░░░░░░░  55%
Stage 10  Parity long tail          ███░░░░░░░░░░░░░░░░░  14%
                                  ────────────────────────
TOTAL (weighted)                  ██████████████░░░░░░░░  ~71%
```

## Honest reading of the number

71% and the product can now do what a daily driver needs on the task side: read,
recalculate, draw, add tasks, delete tasks (with the dependent links/assignments
tombstoned), edit starts and save a file Project reopens; write custom fields
Project displays; add and delete links; and apply recurring calendar exceptions
exactly as Project counts them. The app exposes insert/delete, stored custom
fields and formulas, resource/usage views, filters, export.

The two remaining structural gaps are **assignment row add/delete** (resource
assignment) and **view-state persistence** (`214/CTable`/`214/CV_iew`). The
polish gaps are the app's write-custom-field and link-editing UI, printing,
MPP12/MPP14 variants, and the manual Mac smoke test. The adversarial/ASan audit
is the next gate before a human review (`docs/audit-handoff.md`).

## Milestones

| Milestone | Definition | State |
|---|---|---|
| M1 | opens a real `.mpp` and lists tasks | **done** |
| M2 | recalculates dates correctly and writes a file MS Project reopens | **done** -- 332/332; dates, task add/delete, links and custom fields all reopen |
| M3 | editable sheet + undo | **done for tasks** -- editing/undo, insert/delete with save, stored fields; writing custom fields from the UI is open |
| M4 | Gantt and the other views | **partial** -- Gantt + Resource Sheet/Task/Resource Usage |
| M5 | prints/exports a PDF worth sending | **partial** -- PDF writer + app Export PDF; no visual oracle/print dialog |
| M6 | daily-driver parity for the projects actually worked on | **close** -- task side works; needs assignment editing, view-state persistence, Mac smoke |
