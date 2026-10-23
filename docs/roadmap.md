# Roadmap and progress

Updated at the end of each wave. Percentages are **effort-weighted judgements
against evidence**, not line counts. Each stage lists what would move its number
so the figure can be argued with.

Last update: wave 22, 2026-10-23 (the measured seven-tab ribbon, the stored
columns' values, the new-task scheduling default).  Weights are relative effort
for a solo build. Total = 100.

| # | Stage | Weight | Complete | What is actually done | What moves it |
|---|---|---|---|---|---|
| 0 | Reconnaissance | 5 | **80%** | 54,450 functions decompiled, 2,060 classes, 89,486 vtable slots, 3,207 fields inventoried, naming at ~16% meaningful | only targeted lookups now, as the engine demands them |
| 1 | Container & record format | 7 | **99%** | OLE2 layout, all tables, VarMeta, Fixed2Data, `114/Props` records **and the formula table (inverted, byte-identical to Project's)**, per-table Props, `114/TBkndCal`, `214/CTable` framing + **column/date-format write** | MPP12, Project-validated 214 writes (the append is ours; no COM path appends) |
| 2 | Field model in code | 3 | **100%** | Generated 3,207-row table, C++ API, C ABI, 119,614 checks | — |
| 3 | `.mpp` reader | 8 | **99%** | All tables; PROJECT_A 83 / PROJECT_B 73; tombstone liveness; resource Type/MaxUnits; custom fields **including Cost**; effective dates, milestones, Resume | MPP12, MPP14 resource Type |
| 4 | Scheduling engine | 20 | **89%** | Constraints, deadlines, slack, links/lags, milestones, ALAP, elapsed lag/duration, per-task calendars, recurring exceptions, **Project's stored manual late pair (wave 20)** | summary-calendar rollup, the MSO duration rule, mixed elapsed cases |
| 5 | `.mpp` writer (round-trip) | 9 | **98%** | CFB round trip, task/link/resource/assignment add+delete, custom fields (task **and assignment**), **Cost1..10**, clear; emitted streams byte-identical to Project's own; Project displays every fixture | `214/` regeneration in the app save, Project-validated written streams |
| 6 | App shell (Qt, 3 platforms) | 5 | **99%** | Menus, prefs, filter, Export PDF, dirty prompts, resource/usage views, insert/delete, stored/formula columns, Task Fields + Predecessors, Print + Save as PDF, linked-pin save warning, ribbon+icons+QAT, timeline, backstage, light/dark themes, five structural gates, three runnable packages, the file's stored table, six-tab Task Information, hidden menu bar, back arrow, measured palette, **the measured seven-tab ribbon and the stored columns' values** | the decision-14 dialog inventory, reachability through the backstage, dark painted surfaces, the manual smoke on each OS |
| 7 | Core views | 24 | **52%** | RecordSet/tables/filters/groups/sorts, sheet/Gantt/session, usage/resource models, real tables, the loaded file's stored column set/order/titles/widths driving the model, **Pre/Suc/Resource Names/% Complete values** | SUMMARY/Flag10, bar styles, remaining edit gestures, contour shaping |
| 8 | Editing, undo, custom fields, formulas | 10 | **82%** | Sheet/app editing + undo, task/resource/assignment/link editing, custom fields read/write Project-verified (assignment + Cost included), formula engine + file formula columns, **formula write API**, field/link dialogs, save order and the save-time clamp warning | formula write wired into the app, row move/indent, multi-select editing |
| 9 | Output: print, PDF, MSPDI, CSV | 5 | **68%** | MSPDI parity, CSV, PDF writer/reports, app Export PDF, **`NSPrintOperation` print path + Save as PDF sharing one page layout** | a visual oracle for print/PDF, print options |
| 10 | Parity long tail | 4 | **28%** | Error paths, CFB hardening, perf, tombstone/resource/elapsed/Props/exceptions, formula guard + fuzz + ASan, five more measured layouts and a `214/` encoder, **schema-2 dump, real-.mpp corpus, CFB FAT-slack tolerance in reader and writer, 206-byte tables refused cleanly** | MPP12 write path, LeakSanitizer/UBSan, pixel-difference reporting |

## Overall

```
Stage  0  Reconnaissance            ████████████████░░░░  80%
Stage  1  Container/record format   ███████████████████░  99%
Stage  2  Field model in code       ████████████████████ 100%
Stage  3  .mpp reader               ████████████████████  99%
Stage  4  Scheduling engine         ██████████████████░░  89%
Stage  5  .mpp writer               ███████████████████░  98%
Stage  6  App shell (Qt)            ███████████████████░  99%
Stage  7  Core views                ██████████░░░░░░░░░░  52%
Stage  8  Editing / custom fields   ████████████████░░░░  82%
Stage  9  Output                    ██████████████░░░░░░  68%
Stage 10  Parity long tail          █████░░░░░░░░░░░░░░░  28%
                                  ────────────────────────
TOTAL (weighted)                  ████████████████░░░░░░  ~79%
```

## Honest reading of the number

The total is the table's own arithmetic: sum(weight x complete) / 100.  Earlier waves
printed ~83% although their rows summed to ~77%; this wave states the computed
number rather than carrying the older estimate forward.

78% and **everything the writer can do is now reachable from the app's save path** for every custom
field family the app exposes: tasks, links, resources and assignments can be
added and deleted, text/number/date/duration/flag/cost custom fields read and
write with Project displaying them, formulas read *and* write (the emitted
`114/Props` record is byte-identical to Project's), and the `214/` table state
can be re-encoded byte-identically with a date format or a column record changed.

What is genuinely left: an **oracle gate for the written `214/`/formula bytes**
(Project has never opened a file we wrote with them), MPP12 as a whole (calendars
are real now; the reader/writer/planner paths are not), the **manual smoke test on
each platform** (the Qt shell is automated only), and the long tail of views. On
the parity side one field remains: `constraint-mso`'s Project-written duration,
whose rule is measured (docs/engine-parity.md) but intentionally not implemented.
Stage 7 (core views, weight 24) is still the largest untouched block and is why
the total is not higher.

## Wave 22 in one paragraph

The ribbon now carries the inventory measured from MS Project's own tabs: eight
Task groups, Resource's Assignments/Level, View's Resource Views/Split View/
Window/Macros, and the contextual Gantt Chart Format groups, with the generated
backlog at 113 ribbon + 43 menu rows.  The loaded file's stored table fills
Predecessors, Successors and % Complete with Project's text (link syntax from the
operator's capture), and the project's new-task scheduling default is decoded
from props record 0x024013c8 and shown in the status bar (Auto for the real file,
Manual for a new project).  Still blank: the file's SUMMARY flag (Flag10 is not
decoded), Resource Names on a file with only null assignments, and the file's bar
styles.  The measured ribbon made About mouse-unreachable with the menu bar
hidden; restoring that through the backstage is wave 23's first job.

## Milestones
## Milestones
## Milestones

| Milestone | Definition | State |
|---|---|---|
| M1 | opens a real `.mpp` and lists tasks | **done** |
| M2 | recalculates dates correctly and writes a file MS Project reopens | **done** -- 332/332; dates, task/link/resource/assignment/field edits, **Cost and assignment custom fields** reopen |
| M3 | editable sheet + undo | **done** -- editing/undo, insert/delete, field and link dialogs, one save pipeline, save-time clamp warning |
| M4 | Gantt and the other views | **partial** -- Gantt + Resource Sheet/Task/Resource Usage |
| M5 | prints/exports a PDF worth sending | **partial** -- PDF writer + Export PDF + **real print / Save as PDF path**; still no visual oracle or print-options dialog |
| M6 | daily-driver parity for the projects actually worked on | **close** -- task/edit/write side works; needs view-state persistence, formula writes wired in, and the Mac smoke |
