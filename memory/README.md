# Memory index

Per-stream durable facts.  Read `memory/coordinator.md` for the current state;
read a domain memory only when working in that area; the archive holds the
pre-wave-20 coordinator history.

## Current / recent

| file | read when |
|---|---|
| `coordinator.md` | every session — the compact current state |
| `q6.md` | Q6 (stored table + operator fixes), if present |
| `e6.md` | mppwrite CFB slack / MSO rule |
| `ci3.md` | the parallel Linux build |
| `qt2.md` | Qt chrome, backstage, themes, bundled Carlito |
| `ci2.md` | three-platform packaging |
| `e5.md`, `m6.md`, `e4.md` | engine late pair, MPP12/CFB, schema-2 dump |

## Domain memories (read only for that area)

| file | area |
|---|---|
| `writer.md` | `.mpp` writer / round-trip |
| `app.md`, `app-save.md` | portable app models and save path |
| `schedcore.md`, `scheduling.md` | scheduling engine |
| `cal.md` | calendars |
| `reader.md`, `io.md`, `container.md`, `props.md`, `viewstate.md` | `.mpp` reading |
| `plan.md`, `sheet.md`, `gantt.md`, `view.md` | plan bridge and views |
| `formula.md`, `fieldkeys.md` | formulas and field ids |
| `render.md` | print/PDF/rendering |
| `session.md` | sessions |
| `fuzz.md` | fuzzing/error paths |
| `codegen.md` | generated field tables |
| `ui.md`, `qt.md`, `ci-qt.md` | UI history and the Qt CI |
| `assignment-flat-buffer.md`, `runtime-storage-architecture.md` | analysis notes |

## Archive

* `memory/archive/coordinator-through-wave20-full.md` — the full coordinator
  memory before this cleanup (waves 15–20).
