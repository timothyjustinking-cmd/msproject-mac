# Handoff index — read only the group you need

64 handoffs, ~780 KB: do **not** read the directory.  Start with
`docs/START-HERE.md`, then the current wave.  Historical handoffs are
kept for investigation; each is the record of its stream at that wave.

## Wave 24 (closed on branch `wave24` — the verify-and-fix work order)

| file | what |
|---|---|
| `wave24-complete.md` | **the wave-24 gate**: every W24 item fixed/deferred with commit ids, exact gate numbers, the live comparison, the 16 decisions, the errata |
| `wave24-brief.md` | **the work order**: section 0 publication hold, W24-1..26 in order, verifier corrections, the gate |
| `wave23-verification/gates.md` | the verifier's fresh-tree gate run on wave 23's tip |
| `wave23-verification/review.md` | the independent review of `main..wave23`: R1..R18, most severe first |

## Wave 23 (closed on branch `wave23`, not merged; verified NOT ready to merge)

| file | what |
|---|---|
| `wave23-complete.md` | **the wave-23 gate**: fixed/deferred table, exact gate numbers, the live same-file comparison, the re-scored 16 decisions, CI run ids |
| `wave23-brief.md` | the work order: W23-1..22, verifier corrections, the gate, operator rulings |
| `audit-w22-findings.md` | the 2026-10-23 adversarial + parity audit of wave 22: A-1..12, B-1..14, the re-scored 16 decisions, re-run recipe |
| `audit-w22/` | per-lane evidence (A2 ribbon, A3 S6 oracle, A4 engine score, A5 E7 evidence, A7 fixtures, A8 UI parity gate) and the audit register |

## Wave 22 (closed — audited; see wave 23)

| file | what |
|---|---|
| `wave22-complete.md` | **the wave-22 gate**: the measured ribbon, stored-column values, new-task mode, packages, open items |
| `wave22-brief.md` | wave-22 scope and owners |

## Wave 21 (closed — previous gate)

| file | what |
|---|---|
| `wave21-complete.md` | **the wave-21 gate**: numbers, packages, the 16-decision scoreboard, open items |
| `wave21-brief.md` | wave-21 scope, owners, gates, operator decisions |
| `q6.md` | Q6 stored-table rendering + the operator fixes |
| `e6.md` | E6 mppwrite CFB slack; MSO rule measured, scenario deliberately left failing (wave 21) |
| `ci3.md` | C6 parallel Linux build, validator output (wave 21) |
| `coordinator.md` | the current coordinator handoff (wave-20 final; wave-21 section appended at the close) |

## Wave 20 (closed; the decision scoreboard)

| file | what |
|---|---|
| `wave20-complete.md` | the wave-20 gate and the honest 16-decision scoreboard |
| `e4.md` | schema-2 dump (task late pair, resource calendar uid) |
| `e5.md` | the stored manual late pair; evidence table (19 rows) |
| `m6.md` | MPP12 recalc verified, CFB slack tolerance, 206-byte tables refused |
| `ci2.md` | macOS .app / Linux AppImage / Windows zip packaging |
| `qt2.md` | Q5 chrome, backstage, themes, icons, structural gates (+ the coordinator repair) |

## Wave 19

| file | what |
|---|---|
| `wave19-complete.md` | the wave-19 gate |
| `cal.md` | core/cal MPP12 calendars |
| `ci-qt.md` | the three-platform Qt workflow |

## Wave 18

| file | what |
|---|---|
| `wave18-complete.md` | the wave-18 gate |
| `f2.md` | 214/CEdl cumulative Props cache; CFilter |
| `g1.md` | MPP12 reader |
| `q3.md` | parity render, phantom column, Gantt/Ribbon/Save |

## History (wave 17 and earlier; stream handoffs)

Read only when investigating the named stream.  `wave9-complete` …
`wave15-complete` are the older wave gates.  `w43`–`w64` are the
per-wave stream records (writer, reader, sheet, view, UI, codegen,
container, scheduling, CI).  The named files are the original
area handoffs (reader, writer, container, fieldkeys, ui, view,
scheduling, schedcore, codegen, ci, qt, f1/e2/q2/a1/b1).

`a1.md`, `b1.md`, `ci.md`, `codegen.md`, `container.md`, `e2.md`, `f1.md`, `fieldkeys.md`, `q2.md`, `qt.md`, `reader.md`, `schedcore.md`, `scheduling.md`, `ui.md`, `view.md`, `w43.md`, `w44.md`, `w45.md`, `w46.md`, `w47.md`, `w48.md`, `w49.md`, `w50.md`, `w51.md`, `w52.md`, `w53.md`, `w54.md`, `w55.md`, `w56.md`, `w57.md`, `w58.md`, `w59.md`, `w60.md`, `w61.md`, `w62.md`, `w63.md`, `w64.md`, `wave10-complete.md`, `wave11-complete.md`, `wave12-complete.md`, `wave13-complete.md`, `wave14-complete.md`, `wave15-complete.md`, `wave9-complete.md`, `writer.md`

## Archived

* `docs/archive/coordinator-handoff-through-wave20.md` — the full
  coordinator handoff chain, waves 7–20 (52 KB; the current file keeps
  only wave 20).
* `docs/archive/next-coordinator-prompt-wave19.md` — the old paste-in
  prompt; `docs/START-HERE.md` replaces it.
* `docs/archive/WAVES-coordination-board-wave7.md` — the old branch/
  worktree coordination board (45 KB).

_Wave 23 used the audit work order; its outcome is in `wave23-complete.md`.
