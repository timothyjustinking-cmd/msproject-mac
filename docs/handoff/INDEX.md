# Handoff index — read only the group you need

116 handoffs, ~2.0 MB: do **not** read the directory.  Start with
`docs/START-HERE.md`, then the current wave.  Historical handoffs are
kept for investigation; each is the record of its stream at that wave.

## Now (27 October 2026)

| file | what |
|---|---|
| `standing-rules.md` | **binds every wave from 27**: save paths measured against Project, small context, gate cadence, CI every wave, one review per wave |
| `assessment-2026-10-24.md` | where the port stands after wave 26, and the workflow review |
| `plan-waves27-30.md` | the plan of the 27-31 run (wave 31 was its last wave) |
| `wave31-complete.md` | **the wave-31 gate** (below), the current record |

## Wave 31 (closed on branch `wave31` - pushed; CI on the final tree; not merged)

| file | what |
|---|---|
| `wave31-complete.md` | **the wave-31 gate**: G0..G5 + CLOSE, every item DONE, the fresh-tree gates on the final code tree, both privacy scans, the independent review and its findings, the push and the three CI dispatches (the third green on `072773b`), the re-scored 16 decisions, "Found, not fixed" and what the end of the run leaves |
| `wave31-brief.md` | **the work order**: G0 the wave-30 review's four Highs, G1 the View tab's data commands, G2 zoom and window commands, G3 the Resource Sheet at Gantt fidelity, G4 Task/Resource Usage at Gantt fidelity, G5 the views left out |

## Wave 30 (closed on branch `wave30` - pushed; CI on the final code tree; not merged)

| file | what |
|---|---|
| `wave30-complete.md` | **the wave-30 gate**: G0..G3 + CLOSE, every item DONE or DEFERRED with a committed Project pair, the fresh-tree gates on the final code tree, both privacy scans, the independent review (0 Critical / 4 High / 7 Medium / 10 Low - all Highs fixed) and its errata, the push and the two CI dispatches, the re-scored 16 decisions, "Found, not fixed" |
| `wave30-brief.md` | **the work order**: G0 the wave-29 review's save-path findings, G1 the Task tab, G2 the Resource tab, G3 the Project tab, the measured-shape rule for every enabled edit |

## Wave 29 (closed on branch `wave29` - pushed; CI on the final code tree; not merged)

| file | what |
|---|---|
| `wave29-complete.md` | **the wave-29 gate**: G0..G5 + CLOSE, every item DONE, the fresh-tree gates on `7e14aaa`, both privacy scans, the independent review (0 Critical / 2 High / 2 Medium / 2 Low, plus the CI findings C7/C9/C10 and the audit's C8 - all Highs fixed) and its errata, the push and the two CI dispatches, the re-scored 16 decisions, "Found, not fixed" |
| `wave29-brief.md` | **the work order**: rule 1 for wave 28's commands (G0 from the wave-28 review), the PROJECT_A gaps, the format dialogs, Page Setup, Visual Reports / Compare Projects, the chrome finish |

## Wave 28 (closed on branch `wave28` - pushed after the operator approved the `076438f` rewrite; not merged)

| file | what |
|---|---|
| `wave28-complete.md` | **the wave-28 gate**: G0..G5 + CLOSE, G0..G4 DONE and G5 DEFERRED with its measurement, the fresh-tree gates on `7a69a40`, both privacy scans and the history residuals, the independent review (0 Critical, 3 High - first in wave 29) and its errata, the push refusal and its resolution, the re-scored 16 decisions, "Found, not fixed" |
| `wave28-brief.md` | **the work order**: the editing core - the actuals writer, outline editing, the new-task mode, the editable Task Form, split |

## Wave 27 (closed on branch `wave27` - pushed; CI green on all three OSs; not merged)

| file | what |
|---|---|
| `wave27-complete.md` | **the wave-27 gate**: G0..G3 + CLOSE, every item DONE, the fresh-tree gates on `9323db2`, the two privacy scans and the history residuals, the independent review, the CI runs, the re-scored 16 decisions, "Found, not fixed" |
| `wave27-brief.md` | **the work order**: publication lifted; the two wave-26 Highs (save safety), CI on all three OSs, the decoding evidence, six queued fixes |

## Wave 26 (closed on branch `wave26` - verified, not merged; pushed 24 Oct for CI)

| file | what |
|---|---|
| `wave26-complete.md` | **the wave-26 gate**: G0..G6 + CLOSE, every item DONE, the fresh-tree gates on the fixed tree, the privacy scans (including the wrapped-name finding), the independent review and its High fixes, the live same-file comparison, the re-scored 16 decisions, "Found, not fixed" |
| `wave26-brief.md` | **the work order**: groups G0..G6 + CLOSE, parity first, one finding id per commit, deferral only with evidence |
| `wave26-verification/` | the verifier's fresh-tree gates and independent review of wave 26 |
| `wave25-verification/gates.md` | the verifier's fresh-tree gate run on wave 25's tip |
| `wave25-verification/review.md` | the independent review of `wave24..wave25` (G0 answers its one High) |

## Wave 25 (closed on branch `wave25` — not merged, not pushed; hold)

| file | what |
|---|---|
| `wave25-complete.md` | **the wave-25 gate**: every W25 item fixed/deferred with commit ids, the wave-24 errata, R16/R17 dispositions, the live comparison, the fresh-tree gates, the 16 decisions |
| `wave25-brief.md` | **the work order**: hold unchanged, tightened rules, W25-1..21 (regressions, privacy scrub done properly, record honesty, gate holes, then parity) |
| `wave24-verification/gates.md` | the verifier's fresh-tree gate run on wave 24's tip |
| `wave24-verification/review.md` | the independent review of `wave23..wave24`: S1..S14 |

## Wave 24 (closed on branch `wave24` — verified NOT ready to merge)

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
