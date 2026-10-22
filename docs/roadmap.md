# Roadmap and progress

Updated at the end of each wave. Percentages are **effort-weighted judgements
against evidence**, not line counts. Each stage lists what would move its number
so the figure can be argued with.

Last update: wave 7, 2026-10-22 (M2 dates green, calendar decoded, writer authority measured). Weights are relative effort for a solo build. Total = 100.

| # | Stage | Weight | Complete | What is actually done | What moves it |
|---|---|---|---|---|---|
| 0 | Reconnaissance | 5 | **80%** | 54,450 functions decompiled, 2,060 classes, 89,486 vtable slots, 3,207 fields inventoried, naming at ~16% meaningful | only targeted lookups now, as the engine demands them |
| 1 | Container & record format | 7 | **88%** | OLE2 layout, `114`/`214`, 202-byte task row, Var2Data/VarMeta, Fixed2Data, `114/Props` validated content; **`114/TBkndCal` decoded (calendars, work weeks, dated exceptions) and the task-calendar uid found at row +178** | the remaining row words; what in `114/Props` is validated; MPP12 variants |
| 2 | Field model in code | 3 | **100%** | Generated 3,207-row C++ table, C++ API, C ABI, 119,614 checks | — |
| 3 | `.mpp` reader | 8 | **90%** | Task, resource, link and assignment tables with measured strides; PROJECT_A golden 83/83; scheduling fields; **link liveness settled (88/88 vs Project's MSPDI export) and actuals measured against it** | task-calendar uid exposure (+178, `core/mpp`), assignment dates +52/+56/+60, MPP12 |
| 4 | Scheduling engine | 20 | **62%** | Spec + primitives + CPM: eight constraints, deadlines, four slack fields, 4 link types, measured lags, **0-d milestone placement, FF lead + project-start floor, backward-lead clamp, free-slack clamp**; **the file's calendar is decoded and consumed by recalc**; **PROJECT_A dates 83/83 start and finish** | slack/critical (47/53) — backward pass/TotalSlack, summary W3-P7 rule, ALAP, elapsed lag, effort-driven edge cases |
| 5 | `.mpp` writer (round-trip) | 9 | **50%** | Byte-identical CFB round trip, in-place patching, stream replacement, consistent row writer; **copy authority measured on nine single-variable fixtures** (effective Start/Finish are +104/+108, +96/+100 is a calculated copy; a manual Start store is still unlocated) | a write strategy Project honours (SNET constraint, or reproduce Project's manual-save encoding), then regenerate `214/` |
| 6 | App shell (macOS) | 5 | **5%** | CI builds and tests on macOS runners; no app | an Xcode target and a window |
| 7 | Core views | 24 | **12%** | `core/view` RecordSet/tables/filters/groups; `core/sheet` model; `core/gantt` geometry | a macOS shell that draws them; the remaining Gantt/sheet behaviours |
| 8 | Editing, undo, custom fields, formulas | 10 | **2%** | field model only | after reader/writer |
| 9 | Output: print, PDF, MSPDI, CSV | 5 | **24%** | MSPDI import and export, CSV; **independent MSPDI-vs-reader cross-check tool (`mspdi_cmp`), 830 field comparisons, import bugs fixed (ConstraintType, decimal LinkLag)** | a PDF path worth sending; the writer's LinkLag spelling decision |
| 10 | Parity long tail | 4 | **0%** | — | last |

## Overall

```
Stage  0  Reconnaissance            ████████████████░░░░  80%
Stage  1  Container/record format   █████████████████░░░  88%
Stage  2  Field model in code       ████████████████████ 100%
Stage  3  .mpp reader               ██████████████████░░  90%
Stage  4  Scheduling engine          ████████████░░░░░░░░  62%
Stage  5  .mpp writer                ██████████░░░░░░░░░░  50%
Stage  6  App shell (macOS)          █░░░░░░░░░░░░░░░░░░░   5%
Stage  7  Core views                 ██░░░░░░░░░░░░░░░░░░  12%
Stage  8  Editing / custom fields    ░░░░░░░░░░░░░░░░░░░░   2%
Stage  9  Output                     █████░░░░░░░░░░░░░░░  24%
Stage 10  Parity long tail           ░░░░░░░░░░░░░░░░░░░░   0%
                                  ────────────────────────
TOTAL (weighted)                    ████████░░░░░░░░░░░░  ~42%
```

## Honest reading of the number

42% and the shape has changed: the reader and the calendar are no longer the
risk. The M2 recalculation now reproduces **every stored start and finish** on
the real 83-task programme (83/83 each; 266/332 fields including slack and
critical). What remains in the engine is slack/criticality — 45 link-family
fields, 16 summary fields and 5 milestone fields, with 0 unexplained — followed
by ALAP and elapsed lag.

The writer is the honest gap. Nine single-variable fixtures and a Project-saved
reference now say exactly what Project believes (`+104/+108` effective dates,
`+96/+100` a calculated copy, Fixed2Data +54 a finish authority), but no write
path has yet made a manual task's dates survive an open. That is research, not
volume, and it is the next big decision: pin with an `SNET` constraint (measured
to work for an auto task) or reproduce Project's manual-save encoding.

The views (24 units, 12%) remain the largest untouched block and the thing that
will make the app feel like the product.

## Milestones

| Milestone | Definition | State |
|---|---|---|
| M1 | opens a real `.mpp` and lists tasks | **done** (83/83 fields vs Project's export) |
| M2 | recalculates dates correctly and writes a file MS Project reopens | **dates done** (start 83/83, finish 83/83 on PROJECT_A); the writer half is open — files reopen, but a manual task's written dates are not honoured |
| M3 | editable sheet + undo | not started |
| M4 | Gantt and the other views | not started |
| M5 | prints/exports a PDF worth sending | not started |
| M6 | daily-driver parity for the projects actually worked on | not started |
