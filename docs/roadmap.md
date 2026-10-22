# Roadmap and progress

Updated at the end of each wave. Percentages are **effort-weighted judgements
against evidence**, not line counts. Each stage lists what would move its number
so the figure can be argued with.

Last update: wave 9/9b, 2026-10-22 (M2 complete including the writer; session
model, PDF output, reader hardening, app shell v2). Weights are relative effort
for a solo build. Total = 100.

| # | Stage | Weight | Complete | What is actually done | What moves it |
|---|---|---|---|---|---|
| 0 | Reconnaissance | 5 | **80%** | 54,450 functions decompiled, 2,060 classes, 89,486 vtable slots, 3,207 fields inventoried, naming at ~16% meaningful | only targeted lookups now, as the engine demands them |
| 1 | Container & record format | 7 | **88%** | OLE2 layout, `114`/`214`, 202-byte task row, Var2Data/VarMeta, Fixed2Data, `114/Props`, `114/TBkndCal`; **task rows can now be re-written and re-read with the manual mode bit** (`Fixed2Meta` entry +8 bit 0x80) | row add/delete (wave 10), MPP12 variants, what in `114/Props` is validated |
| 2 | Field model in code | 3 | **100%** | Generated 3,207-row C++ table, C++ API, C ABI, 119,614 checks | — |
| 3 | `.mpp` reader | 8 | **95%** | Task/resource/link/assignment tables; PROJECT_A golden 83/83; link liveness 88/88; effective dates, calendar uid, stored milestone; **assignment `+60` proven to be Resume (21/21 vs Project's export), error paths and CFB hardening, 4,000-task parse in ~4 ms** | elapsed-DURATION format word (PjFormatUnit), resource Type byte, MPP12 |
| 4 | Scheduling engine | 20 | **72%** | Spec + primitives + CPM: eight constraints, deadlines, four slack fields, 4 link types, measured lags, 0-d milestones, FF leads, backward-lead clamp, free-slack clamp, the file's calendar consumed by recalc; **ALAP resolved (20/20, it was a stored-word misread), elapsed lag measured and applied (LagFormat 7/8, raw-finish anchor)** | recurring calendar exception parameters, elapsed-duration format, per-task calendars in CPM |
| 5 | `.mpp` writer (round-trip) | 9 | **70%** | Byte-identical CFB round trip, patching, stream replacement, consistent row writer; **`save_date_edits`: SNET pin + clear the manual mode bit -- Project reopens the row at the pinned date** (the honoured fixture is the API's own output); every save proven by re-reading the bytes from disk | row add/delete and `214/` regeneration (wave 10), exact-date writes on linked rows |
| 6 | App shell (macOS) | 5 | **35%** | AppKit shell: open dialog + argv, real `.mpp` load, sheet + Gantt, two-way selection, in-cell editing with undo/redo, disclosure collapse, save through the writer, status bar; `app-macos` CI green | menus, preferences, printing/export, multi-window, drag |
| 7 | Core views | 24 | **24%** | `core/view` RecordSet/tables/filters/groups/sorts; `core/sheet` model with editing transactions; `core/gantt` geometry + real-calendar shading; **view tables and Gantt zoom this wave**; `core/session` gives a document model to hang views on | resource and usage views, the remaining Gantt/sheet behaviours |
| 8 | Editing, undo, custom fields, formulas | 10 | **12%** | Field model; sheet transactions/undo/redo plus app-level editing and undo; **dates persist through the writer** | custom fields and formulas (wave 10), add/delete tasks, edit breadth |
| 9 | Output: print, PDF, MSPDI, CSV | 5 | **45%** | MSPDI import/export with `mspdi_cmp` parity (PROJECT_A/PROJECT_B/assign1 all MATCH), CSV; **dependency-free PDF writer + sheet/Gantt reports, externally validated with pypdf** | a print path in the app, layout decisions with a visual oracle |
| 10 | Parity long tail | 4 | **4%** | Error paths and CFB hardening, large-programme perf harness, the Resume correction, client-data tooling | last |

## Overall

```
Stage  0  Reconnaissance            ████████████████░░░░  80%
Stage  1  Container/record format   ██████████████████░░  88%
Stage  2  Field model in code       ████████████████████ 100%
Stage  3  .mpp reader               ███████████████████░  95%
Stage  4  Scheduling engine          ██████████████░░░░░░  72%
Stage  5  .mpp writer                ██████████████░░░░░░  70%
Stage  6  App shell (macOS)          ███████░░░░░░░░░░░░░  35%
Stage  7  Core views                 █████░░░░░░░░░░░░░░░  24%
Stage  8  Editing / custom fields    ██░░░░░░░░░░░░░░░░░░  12%
Stage  9  Output                     █████████░░░░░░░░░░░  45%
Stage 10  Parity long tail           █░░░░░░░░░░░░░░░░░░░   4%
                                  ────────────────────────
TOTAL (weighted)                   ██████████░░░░░░░░░░░░  ~53%
```

## Honest reading of the number

53% and the shape is finally the right one. The engine and the writer are no
longer the risk: M2 is **332/332** (start, finish, slack and critical all 83/83),
and a date edit written by our own code now **reopens in Project on that date** —
via a real SNET constraint plus clearing the task's manual-mode bit. That is a
documented compromise (the row becomes auto-scheduled), not a preserved manual
schedule. The reader is hardened: 28 error-path checks, a 256-byte mutation loop,
and a 4,000-task programme through parse, CPM, RecordSet, sheet and Gantt in
about 10-15 ms.

What moved this wave: `core/session` (a document model the app and any future
view can share), `core/render` (a PDF worth sending, externally validated), the
app shell going from a picture to an editor that saves, and two corrections that
matter more than the percentages — the app was drawing every date one day late,
and `Assignment +60` is Resume, not Stop.

The honest gaps are now: **adding and deleting rows in the writer** (wave 10's
hardest stream; the container limits are documented in `spec/mpp-writer.md` §5),
**custom fields and formulas** (the largest untouched product feature),
**resource and usage views**, and the parity tail — recurring calendar
exceptions, elapsed durations, resource Type. The views block (weight 24, 24%) is
still the largest single opportunity, and it is now cheap to extend because the
session model and the PDF renderer consume the same rows.

## Milestones

| Milestone | Definition | State |
|---|---|---|
| M1 | opens a real `.mpp` and lists tasks | **done** |
| M2 | recalculates dates correctly and writes a file MS Project reopens | **done** -- 332/332, and a written date reopens on that date (auto-scheduled conversion, documented) |
| M3 | editable sheet + undo | **mostly done** -- sheet transactions/undo plus app editing and undo/redo; save persists **start dates only** |
| M4 | Gantt and the other views | **partial** -- Gantt with real-calendar shading; no resource/usage views |
| M5 | prints/exports a PDF worth sending | **partial** -- PDF writer plus sheet/Gantt reports; no visual oracle, no print path in the app |
| M6 | daily-driver parity for the projects actually worked on | not started -- needs add/delete, custom fields, usage views |
