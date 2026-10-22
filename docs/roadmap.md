# Roadmap and progress

Updated at the end of each wave. Percentages are **effort-weighted judgements
against evidence**, not line counts. Each stage lists what would move its number
so the figure can be argued with.

Last update: wave 10, 2026-10-22 (row add/delete in the writer, resource/usage
views, custom-field formulas, app shell v3). Weights are relative effort for a
solo build. Total = 100.

| # | Stage | Weight | Complete | What is actually done | What moves it |
|---|---|---|---|---|---|
| 0 | Reconnaissance | 5 | **80%** | 54,450 functions decompiled, 2,060 classes, 89,486 vtable slots, 3,207 fields inventoried, naming at ~16% meaningful | only targeted lookups now, as the engine demands them |
| 1 | Container & record format | 7 | **94%** | OLE2 layout, `114`/`214`, 202-byte task row, Var2Data/VarMeta, Fixed2Data, `114/Props`, `114/TBkndCal`, the manual mode bit; **rows can now be added and tombstoned** (`FixedMeta` header +4 bit 0x02; entry +0=0x02), proven by Project reopening the emitted files | MPP12 variants, what in `114/Props` is validated |
| 2 | Field model in code | 3 | **100%** | Generated 3,207-row C++ table, C++ API, C ABI, 119,614 checks | — |
| 3 | `.mpp` reader | 8 | **96%** | Task/resource/link/assignment tables; PROJECT_A golden 83/83; link liveness 88/88; effective dates, calendar uid, stored milestone; assignment `+60` = Resume; error paths, CFB hardening, 4,000-task parse in ~4 ms; **tombstoned rows are now skipped by the FixedMeta +0 rule (PROJECT_A/PROJECT_B unchanged)** | elapsed-DURATION format word (PjFormatUnit), MPP12 |
| 4 | Scheduling engine | 20 | **72%** | Spec + primitives + CPM: eight constraints, deadlines, four slack fields, 4 link types, measured lags, 0-d milestones, FF leads, backward-lead clamp, free-slack clamp, the file's calendar consumed by recalc; ALAP resolved (20/20), elapsed lag measured and applied | recurring calendar exception parameters, elapsed-duration format, per-task calendars in CPM |
| 5 | `.mpp` writer (round-trip) | 9 | **82%** | Byte-identical CFB round trip, patching, stream replacement, consistent row writer, `save_date_edits` (SNET + mode bit); **`add_task_row` appends a legal row and `delete_task_row` tombstones one, both re-read from disk; Project reopens add files with the new task and delete files without the deleted one (2/2/3/4/4)** | exact-date writes on linked rows, add/delete for links/assignments, `214/` regeneration (deliberately not done) |
| 6 | App shell (macOS) | 5 | **55%** | AppKit shell with menus (File/Edit/View/Insert/Help), preferences (date/duration format, zoom, shading), filter dialog, Export PDF via `core/render`, dirty-state save prompts, clipboard, keyboard navigation; 7 headless app tests, 1,043 checks; `app-macos` CI builds the ObjC++ shell | multi-window, printing, drag/drop, manual-Mac smoke |
| 7 | Core views | 24 | **36%** | `core/view` RecordSet/tables/filters/groups/sorts + real Task/Resource tables; `core/sheet`; `core/gantt`; `core/session`; **`core/usage` day/week/month buckets, task-usage and resource-usage projections, resource RecordSet (386 checks)** | wiring usage/resource views into the app, remaining Gantt/sheet behaviours, contour shaping |
| 8 | Editing, undo, custom fields, formulas | 10 | **30%** | Field model; sheet/app editing + undo; dates persist; **row add/delete**; **`core/formula`: parser/evaluator, `IIf`/comparisons/date & duration arithmetic, custom-field registry and measured storage encodings, computed columns (1,179 checks)** | exposing formulas/custom fields in the app, writing custom-field values, flag/cost storage oracle |
| 9 | Output: print, PDF, MSPDI, CSV | 5 | **55%** | MSPDI import/export with `mspdi_cmp` parity, CSV, dependency-free PDF writer + sheet/Gantt reports (pypdf-validated); **the app now has an Export PDF path with a save panel and filtered rows** | a print path, layout decisions with a visual oracle |
| 10 | Parity long tail | 4 | **6%** | Error paths, CFB hardening, perf harness, the Resume correction, client-data tooling, **tombstone liveness and the boolean-flag mirror bits measured** | last |

## Overall

```
Stage  0  Reconnaissance            ████████████████░░░░  80%
Stage  1  Container/record format   ███████████████████░  94%
Stage  2  Field model in code       ████████████████████ 100%
Stage  3  .mpp reader               ███████████████████░  96%
Stage  4  Scheduling engine         ██████████████░░░░░░  72%
Stage  5  .mpp writer               ████████████████░░░░  82%
Stage  6  App shell (macOS)         ███████████░░░░░░░░░  55%
Stage  7  Core views                ███████░░░░░░░░░░░░░  36%
Stage  8  Editing / custom fields   ██████░░░░░░░░░░░░░░  30%
Stage  9  Output                    ███████████░░░░░░░░░  55%
Stage 10  Parity long tail          █░░░░░░░░░░░░░░░░░░░   6%
                                  ────────────────────────
TOTAL (weighted)                  ████████████░░░░░░░░░░  ~60%
```

## Honest reading of the number

60% and the two structural blockers are gone. **The writer can now add and
delete rows**, and Project itself confirms both: an added task reopens in the
project (4/5/4 tasks in the fixtures, priority preserved), and a tombstoned row
disappears (2/2/3/4/4 across the five delete fixtures). The one non-obvious
requirement measured this wave is `FixedMeta` **header** word +4 bit 0x02 —
without it Project ignores the entry-level tombstone; with it, both our cleaner
VarMeta removal and Project's retained-entry shape are accepted.

The other shape change is breadth: `core/usage` gives day/week/month work
buckets and task/resource projections, and `core/formula` gives a real
expression engine with measured custom-field storage. Both are model-complete and
tested but not yet wired into the app, which is why the view and editing bars
move less than the code volume suggests. The app shell is now a menu-driven
editor with preferences, a filter dialog and Export PDF; its ObjC++ half is
proved only by `app-macos` CI, as always.

The honest gaps are now: **custom fields/formulas are not surfaced in the UI or
written to the file**, **usage/resource views have no UI**, **linked exact-date
writes and add/delete for links/assignments** remain open, and the parity tail
(recurring calendar exceptions, per-task calendars, MPP12, `114/Props`
validation) is unchanged. PROJECT_B's `FixedMeta +4 = 6` also confirms the tombstone
bit appears in a real client file, so the reader fix has real-file reach beyond
the fixtures.

## Milestones

| Milestone | Definition | State |
|---|---|---|
| M1 | opens a real `.mpp` and lists tasks | **done** |
| M2 | recalculates dates correctly and writes a file MS Project reopens | **done** -- 332/332, a written date reopens on that date (auto-scheduled conversion, documented) |
| M3 | editable sheet + undo | **mostly done** -- editing/undo plus **task add and delete that Project reopens**; save persists **start dates only** |
| M4 | Gantt and the other views | **partial** -- Gantt with real-calendar shading; resource/usage models exist, no resource/usage UI |
| M5 | prints/exports a PDF worth sending | **partial** -- PDF writer, reports and an app Export PDF path; no visual oracle, no print dialog |
| M6 | daily-driver parity for the projects actually worked on | not started -- needs custom fields in the UI, usage views, links/assignments editing |
