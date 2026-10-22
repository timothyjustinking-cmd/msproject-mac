# Parity scope

Parity is the acceptance bar, but "MS Project" is twenty years of features.
Because this is a personal tool, scope is **the set of things I actually use**,
frozen as an explicit list, plus the machinery required to open and save real
files. Everything else is deferred and labelled.

Legend: **have** = analysis already answers it · **partial** · **none** · **out** = deliberate non-goal.

## 0. Platforms (decided 2026-10-23)

Windows, macOS and Linux ship from **one Qt 6 shell** with an identical client
area (same pixels, same commands, same PDF bytes), enforced by cross-OS
golden-image tests — see `docs/ui-qt-port.md`. The AppKit shell used through
wave 15 is replaced once the Qt shell is green on macOS; the `app/` models and
`core/` are unchanged. The tables below are the original scope analysis and
predate the implementation: for what is actually done see `docs/roadmap.md`.

## 1. Documents

| Feature | State | Notes |
|---|---|---|
| Open `.mpp` (MPP12/14, 2010–2021) | none | **highest priority** — no file, no product |
| Save `.mpp` that MS Project reopens cleanly | none | round-trip is the proof |
| Older MPP8/9 (2000–2003) | out | unless a file demands it |
| Templates (`.mpt`) | none | thin wrapper over `.mpp` |
| Multiple projects / subprojects & master projects | none | decide later; big for some users |
| Password-protected / read-only files | none | |
| Autosave, backup, recovery | none | |
| Undo/redo across all edits | none | engine-level, not UI-level |

## 2. Scheduling engine

| Feature | State | Notes |
|---|---|---|
| Working-time calendar (base, project, resource, task) | none | foundation of everything |
| Calendar exceptions & work weeks | none | |
| CPM forward/backward pass, slack, critical path | none | **the engine** |
| Task types (fixed units/work/duration), effort-driven | none | |
| Duration formats incl. elapsed (`5 edays`) | none | |
| Constraints (ASAP…MSO) + deadlines | none | |
| Dependency types SS/FS/FF/SF + lag/lead | none | |
| Summary/outline, WBS, rollup | partial | field model only |
| Splits (task interruptions) | none | |
| Assignment contours / work distribution | none | |
| Resource leveling | none | hardest single feature; defer |
| Multiple baselines (0–10) | partial | 671 fields already mapped |
| Earned value (BCWS/BCWP/ACWP, CPI/SPI) | none | |
| Cost rates, rate tables, overtime | partial | fields mapped |
| Materials & budgets | partial | fields mapped |
| Manual scheduling mode | none | |
| Inactive tasks | none | |

## 3. Fields & customisation

| Feature | State | Notes |
|---|---|---|
| 3,207 built-in fields, typed and named | have | `analysis/field_metadata_all.tsv` |
| Custom fields (text/number/date/flag/cost/duration) | partial | storage class 5 mapped; UI + formula binding missing |
| Formulas in custom fields | none | 61 computed fields; per-field functions in the binary |
| Lookup tables & outline codes | partial | storage class 9 mapped |
| Graphical indicators | none | |
| Field attributes (rollup, calculation, display) | partial | flag bits decoded, semantics not |

## 4. Views & UI

| Feature | State | Notes |
|---|---|---|
| Task sheet (grid, editing, indentation) | none | first UI milestone |
| Gantt chart (bars, links, drag) | none | second |
| Resource sheet, usage views | none | |
| Calendar view, network/PERT, timeline | none | |
| Board (sprint) views | none | |
| Tables (entry/cost/schedule/…), field pickers | partial | 41 column mappings recovered |
| Filters, auto-filters, highlighting | none | |
| Groups, sorts, outline | none | |
| Formatting: bar styles, text, gridlines, fonts | none | |
| Zoom/scroll performance on 10k+ tasks | none | drives the Qt shell's model/view choice |

## 5. Output

| Feature | State | Notes |
|---|---|---|
| Print & page setup (views, headers, legend) | none | |
| PDF export | none | |
| Visual reports (Excel/Visio) | out | replace with native export |
| MSPDI (`.xml`) import/export | partial | schema known, no code |
| CSV / Excel export, copy-paste interop | none | |

## 6. Deliberate non-goals

VBA/macros · ActiveX & COM automation · Project Server/PSI · Teams/Planner ·
co-authoring & shared links · the OOBREPORT report templates (26 ZIP resources).

## 7. Definition of done for v1

Fill this in — it is the actual scope:

- [ ] opens my existing `.mpp` files without loss
- [ ] recalculates dates/slack identically
- [ ] edits tasks, links, durations, resources, calendars
- [ ] saves `.mpp` that reopens in MS Project unchanged
- [ ] Gantt + task sheet usable daily
- [ ] prints/exports a PDF I would send to someone
- [ ] feature list: _…to be completed from the actual projects I work on…_
