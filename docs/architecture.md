# Architecture

## The bet

MS Project's engine is table- and descriptor-driven: one `PropInfoData` table
(3,207 entries) describes every field, and every surface — sheet, Gantt, usage,
export — reads through the same dispatch chain. Reproducing that structure is
what makes parity reachable; a hand-rolled data model per view would diverge
immediately.

So the port keeps the same shape:

```
core/model    record structs + generated 0xA8-style descriptor table
      |
core/sched    CPM, calendars, units, constraints, leveling
      |
core/view     tables, filters, groups, sorts, custom fields, formulas
      |
core/io       .mpp reader/writer, MSPDI XML, CSV, print
      |
app/          SwiftUI — binds to core, owns no business rules
```

## Layers

### `core/` — portable C++20

No Win32, no COM, no MFC. Builds with clang on macOS and MSVC on Windows (so the
same code can be tested against the oracle on the Windows box).

- **`model/`** — `Task`, `Resource`, `Assignment`, `Calendar`, `Project` records.
  Field access goes through a generated descriptor table committed as source:
  `scripts/gen_field_tables.py` reads `analysis/field_metadata_all.tsv` and emits
  `field_table.gen.cpp` (type code, storage class, slot index, flags).
  Generated, not hand-written — the TSV is the source of truth.
- **`sched/`** — the calendar engine first (working time, exceptions, base
  calendars, elapsed durations), then the CPM forward/backward pass, then
  constraint types, then assignment contours, then leveling last. Everything
  date-related goes through the calendar engine; nothing may compute dates
  inline.
- **`view/`** — table/view/filter/group/sort definitions as data, so Gantt,
  sheet, usage and calendar views are configurations of one engine rather than
  four implementations.
- **`io/mpp/`** — OLE2/CFB container, stream inventory, record serialisation.
  Reader first, writer second, round-trip test between them always.
- **`io/`** — MSPDI XML (`Project` schema), CSV, and a print model that the app
  renders.

### `app/` — Swift, SwiftUI + AppKit

Swift 5.9+ C++ interop (or a thin Objective-C++ shim if interop fights back).
Grid and Gantt drawing will need AppKit (`NSView`/`CALayer`) rather than pure
SwiftUI for large-project scrolling performance; SwiftUI for inspectors,
dialogs, toolbars, settings.

Business rules live in `core/` only. If a rule leaks into Swift it will drift
from the oracle and be invisible until it costs a week.

## Verification

The oracle is MS Project on Windows, driven by COM (`win32com`, already proven
in the RE work — 84 scripts use it).

1. **Golden fixtures.** A scenario (tasks, links, calendars, constraints) is
   built by COM, recalculated by MS Project, and every field captured via COM
   into `tests/fixtures/*.json`. The port must match exactly.
2. **Differential file diffs.** Change one property via COM, diff the `.mpp`
   bytes — the technique that cracked runtime memory transfers directly to the
   file format.
3. **Round-trip corpora.** Open → save → reopen in MS Project, compare all
   fields.

Rule: no engine feature is "done" until a fixture proves it matches.

## Milestones

| # | Milestone | Proves |
|---|---|---|
| M0 | Repo, spec skeleton, generator from TSV | scaffolding |
| M1 | `.mpp` reader → read-only task sheet | format spec |
| M2 | CPM recalculation + `.mpp` writer, round-trip | engine + writer |
| M3 | Editing, undo, custom fields, formulas | interactive core |
| M4 | Gantt, usage, calendar, timeline, network views; tables/filters/groups | parity of surfaces |
| M5 | Printing/reports, MSPDI, CSV | output parity |
| M6 | Leveling, EVM, splits, materials, budgets | long tail |

M1 and M2 are the whole risk of the project. Everything after is volume.

## Non-goals (v1)

VBA/macros, ActiveX/COM automation, Project Server/PSI (the 5 WSDL/5 XSD
resources), Teams/Planner integration, multi-user co-authoring.
