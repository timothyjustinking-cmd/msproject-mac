# MS Project on macOS — native parity port

Personal-use port of Microsoft Project (WINPROJ.EXE) to a native macOS application.
Single user, not distributed, no Wine or VM in the end state.

## Constraints that shape every decision

| Constraint | Consequence |
|---|---|
| **Personal use, never distributed** | Recovered logic can be ported directly. No clean-room theatre needed. |
| **Native** (AppKit/SwiftUI) | Win32/MFC/ATL UI code is worthless. Only the engine logic transfers. |
| **Parity**, not "good enough" | The acceptance test is byte- and number-identical behaviour against MS Project. |
| **Windows box is full** | Everything lives on `D:`. The RE workspace stays where it is. |

## Strategy in one paragraph

The recovered material is a map of the *engine* — a 3,207-entry field descriptor
table, the storage-class routing, the record layouts, and the class hierarchy.
None of the MFC/ATL UI is reusable, but the engine is where twenty years of
scheduling logic lives. So: rebuild the engine as portable C++ driven by the
descriptor tables, wrap it in a native SwiftUI front end, and use MS Project on
Windows as the oracle — every behaviour verified by differential test before it
is trusted.

## Repository layout

```
core/          Portable C++20 engine (no Win32, builds with clang on macOS)
  mpp/           .mpp container reader/writer (OLE2/CFB + MSProject streams)
  model/         Task/Resource/Assignment/Calendar records, generated descriptor tables
  sched/         CPM, calendar arithmetic, duration/work units, constraints, leveling
  view/          Table/view/filter/group model, custom fields and formulas
  io/            MSPDI XML, CSV, print model
app/           Swift + SwiftUI/AppKit front end (Xcode project, built on the Mac)
spec/          Written specifications: file format, field model, scheduling rules
tests/         Golden fixtures captured from MS Project, differential harness, corpora
analysis/      Recovered field/class model as TSVs + resource dumps — the source of truth
scripts/       Ghidra scripts and Python harness that produced the analysis
docs/          Scope, architecture, status
```

The Ghidra project (770 MB) and the 2.59 M lines of decompiled C are
regenerable and deliberately not committed.

## Where the work stands

Reverse engineering is at **end of reconnaissance**: the binary is fully
decompiled and structurally mapped, the field model is 100% extracted and
COM-validated, and the `.mpp` file format has not been started. Read
[docs/status.md](docs/status.md) before touching anything, then
[docs/scope.md](docs/scope.md) for the parity definition, then
[docs/architecture.md](docs/architecture.md).

## Build order

1. `.mpp` reader → read-only task sheet (proves the format spec)
2. CPM recalculation → `.mpp` writer (proves round-trip)
3. Editing, undo, custom fields
4. Views: Gantt, usage, calendar, timeline, network
5. Tables, filters, groups, formatting
6. Printing/reports, MSPDI import/export
7. Leveling, EVM, splits — the long tail

## Machines

- **Windows 11** — reverse engineering only (Ghidra 12.1.4 + JDK 21 at `C:\dev\tools`, project on `D:`), plus MS Project as the behavioural oracle.
- **macOS** — the app: Xcode, Swift, clang.
