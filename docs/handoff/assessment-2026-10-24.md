# Progress assessment and workflow review - 24 October 2026

_Written by the Opus verifier session after wave 26, at the operator's request: "stop and give
me a thorough assessment of where we stand", plus "look over the DeepSeek chats and
work out if we can improve the workflow".  Sources: the audit of wave 22
(`audit-w22-findings.md`), the four verification records (`wave23-verification/` ..
`wave26-verification/`), the wave records, fresh-tree gate runs by the verifier, and
the DeepSeek session transcripts.  No client data is quoted._

## 1. Bottom line

- **The port now looks and behaves much more like Project than it did on Friday.**
  The audit found a loaded file shown in the wrong order, placeholder icons, no file
  styles, and gates that tested nothing.  After four waves, the loaded file renders
  in Project's order with its outline, the file's own text styles, bar colours and
  timescale tiers; the seven ribbon tabs carry Project's controls and original
  icons; the backstage, Options and the main Project dialogs exist; the split view
  and dark chrome work.
- **It is not ready to merge.** The wave-26 review found two High defects in the
  new % Complete buttons: they save one value where Project also writes the actual
  start, actual and remaining duration and the project roll-up, and they accept
  summary tasks.  A file saved at 40 % would reopen in Project with wrong progress.
- **Saving is the weak side.** Reading and scheduling are strong and measured;
  writing covers a narrow set of edits, and two wave-26 write paths were proven only
  against the port's own reader.
- **"100 % parity" is not yet in sight**, but the remaining gap is now a list of
  concrete, mostly mechanical items (section 5), not unknowns.

## 2. Where it stands against the 16 decisions

| # | decision | audit (wave 22) | now (wave 26) | verifier's note |
|---|---|---|---|---|
| 1 | all seven tabs | PARTIAL | DONE | labels, order, kinds, stacks and glyphs pinned against the captures; live widgets checked |
| 2 | full backstage | PARTIAL | DONE | eleven pages, Account/About; online entries laid out and disabled |
| 3 | original icons | NOT DONE | DONE (ribbon) | 114 ribbon commands; menu, QAT and status-bar artwork still the older set |
| 4 | chrome metrics | UNVERIFIED | DONE | measured against capture numbers, not the port's own constants |
| 5 | timeline strip | PARTIAL | DONE | file-stored visibility, live toggle |
| 6 | Entry table | DONE | DONE | |
| 7 | render the file | FAILING | PARTIAL | order/outline/styles/bars/tiers match; stored column widths (5 vs 8 columns at 1920), the yellow row fill, bar-label coverage and the timescale origin still differ |
| 8 | status bar | PARTIAL | PARTIAL | five buttons, Ready, light bar; two view buttons disabled |
| 9 | disabled + backlog | DONE | DONE | 83 of 95 ribbon commands still disabled: shape is right, function is thin |
| 10 | themes | NOT DONE | PARTIAL | dark chrome live; header bands and ribbon ink stay light |
| 11 | new-project defaults | DONE | DONE | Options' new-task mode does not reach Insert Task yet (Medium) |
| 12 | measurement | PARTIAL | PARTIAL | far stronger gates; not every screen; pixel score not re-run |
| 13 | Carlito | DONE | DONE | |
| 14 | every dialog | PARTIAL | PARTIAL | Text Styles, Gridlines, Bar Styles, Page Setup, Visual Reports, Compare Projects still missing |
| 15 | dialog semantics | PARTIAL | PARTIAL | new dialogs honest; few are functional |
| 16 | split view | NOT DONE | DONE | the Task Form is display-only |

**Count: 9 DONE, 7 PARTIAL, 0 NOT DONE** (audit: 4 DONE, 8 PARTIAL, 2 NOT DONE, 1
FAILING, 1 UNVERIFIED).  "DONE" means the shape matches Project; the share of
commands that actually work is much lower (83 of 95 ribbon commands are disabled).

## 3. Engine and file fidelity

| area | state | evidence |
|---|---|---|
| reader | strong | 1100 checks with the corpus, 420 without; flags, view settings, styles, timescale decoded by controlled Project saves |
| scheduler | strong on what is measured | M2 recalculation of the real file 332/332; engine scenarios 7/8 (constraint-mso deliberately left failing) |
| writer | narrow | 1693 round-trip checks; supports field values, dates, add/delete, links; **no actuals writer**, no outline (indent/outdent) edit, no split, no baseline, no calendar edit, no new-task-mode write |
| new write paths in wave 26 | **not proven** | % Complete (High: one value of the eight Project writes); project Priority (unmeasured against Project) |
| real-file coverage | thin | two client files and synthetic fixtures; one of the two client files drives most acceptance |

The standing lesson from every review: **a write path tested only by re-reading
with the port's own reader proves nothing about Project.**  Every save path needs a
Project comparison (the same edit made through COM, saved, and the streams diffed)
before it is enabled.

## 4. Evidence, privacy and branches

- **Gate honesty is much better.** The tautological gates the audit found (UI
  parity, S6 oracle, ribbon pin) were replaced with independent references; each
  wave's gates were re-run by the verifier on fresh trees and matched.
- **CI has not run since wave 23** (`wave23` at its old tip, before the scrub).
  Waves 24-26 were held back by the publication hold, so `app/main.mm` (macOS) has
  been edited in waves 25-26 and **never compiled anywhere**.  This is the biggest
  untested risk.
- **Decoding evidence is partly local.** The one-change-per-save Project files that
  prove the style, bar and timescale offsets sit under `build/w26/`, not in the repo.
- **Privacy: the history scrub is done** (24 Oct 2026).  Every branch was rewritten
  (client task names, project titles and people's names replaced by labels, including
  names wrapped across lines), `main` and `wave23` were force-pushed, and the five
  stale branches were deleted.  A fresh clone of GitHub contains no client strings,
  images or corpus files.  Remaining: GitHub still serves the old commits to anyone
  who has their exact hash until support purges them (request drafted for the operator);
  `tests/data/project_a_golden.tsv` still holds 64-bit unsalted hashes of the 83
  task names (a guessed name can be confirmed); pre-scrub backups are local only
  under `D:\backup\`.
- **Branches.** GitHub: `main` (wave 22 + the audit, scrubbed) and `wave23`.
  Local only: `wave24`, `wave25`, `wave26`, each built on the last; `wave26` contains
  everything.  Commit hashes quoted in older documents refer to the pre-scrub history.

## 5. What remains for 100 % parity, in order

| # | item | size | why first |
|---|---|---|---|
| 1 | Fix or disable the % Complete save; refuse summary tasks; measure Priority | small | High; saved data |
| 2 | Push `wave26`, run CI on all three OSs (macOS compile of `main.mm`) | small | biggest untested risk |
| 3 | An actuals writer measured against Project (enables % buttons, Update Project) | medium | the core progress feature |
| 4 | Outline editing (indent/outdent), split task | medium | core editing; buttons disabled today |
| 5 | Stored sheet column widths, yellow row fill, bar-label coverage, timescale origin | small-medium | the last visible differences on PROJECT_A |
| 6 | The six missing dialogs (Text Styles, Gridlines, Bar Styles, Page Setup, Visual Reports, Compare Projects) | medium | decision 14 |
| 7 | Make the 83 disabled ribbon commands work, highest-use first | large | where most of the remaining parity is |
| 8 | Other views (Task Usage, Resource Usage, Team Planner, Network Diagram, Calendar, Reports) at the same fidelity as Gantt | large | Project is more than the Gantt |
| 9 | Commit the controlled-save evidence; replace the hashed golden names | small | reproducibility, privacy |
| 10 | macOS launch, notarisation; dark header bands; menu/QAT artwork | small-medium | finish |

## 6. Workflow review

### What the numbers show

| wave | wall time | DeepSeek turns | commits | context re-read | est. DeepSeek cost |
|---|---|---|---|---|---|
| 23 | 2.7 h | 863 | 23 | 355M tokens | about $2.00 |
| 24 | 1.7 h | 648 | 12 | 198M | about $0.80 |
| 25 | 2.5 h | 589 | 24 | 198M | about $0.80 |
| 26 | 6.8 h (8 groups) | 2,719 | 45 | 775M | about $3.60 |

Costs are estimates from DeepSeek's published deepseek-flash prices, mostly
off-peak.  All four waves cost roughly **$7 of DeepSeek**.  The Claude side (the
watching session and four independent reviews of 200-330k tokens each) costs far
more, and is where the value of finding defects came from.

### What worked

- **Parity first, small items, evidence-backed deferral.**  Wave 26 finished every
  parity group that waves 23-25 had deferred three times.
- **A fresh session per group** with a marker-based end: no early stops, no
  re-orientation loops, one iteration per group.
- **The independent review per wave.**  Every review found a real Critical or High
  that DeepSeek's own checks missed (wrong-task edits, the macOS Gantt and print
  rows, mislabelled colours, the % Complete save).

### What did not

1. **Context re-reading dominates.** Average context per turn stayed around 285k
   tokens even with fresh sessions, because each session grows towards the 786k
   auto-compact window.  Tool output was large in G4/G5 (7.7-9.7 MB, 60+ results
   over 20 KB each, from reading whole files and logs).
2. **Builds sync to the company Dropbox.** `build/` is now 14 GB inside the
   Dropbox folder; it caused two MSBuild file-lock failures and slows every build.
   The close-out alone ran 170 build/test commands.
3. **Write paths are validated against the port itself.** The same defect class
   recurred in waves 23, 24 and 26.
4. **Process drift recurs**: bundled multi-item commits, amended commits, gate
   numbers quoted from a different tree, a name wrapped across two lines that every
   line-based scan missed.
5. **Launch tooling cost time**: four driver bugs (wrong session resumed, a tool
   schema DeepSeek rejects, a variadic flag swallowing the prompt, a comma list
   passed as one string) before the group driver ran cleanly.

### Recommendations

1. **Mark `build/` Dropbox-ignored, or build outside Dropbox** (for example
   `D:\build\msproject`).  One command; removes the locks and the sync load.
2. **Lower `CLAUDE_CODE_AUTO_COMPACT_WINDOW`** in `tools/deepseek/deepseek-claude.cmd`
   from 786432 to about 200000, and add a brief rule: read logs with `tail`/`grep`,
   never whole.  Expect per-turn cost and latency to fall by more than half.
3. **A standing write-path rule in every brief**: no save path is enabled until a
   Project COM measurement of the same edit has been diffed against the port's save.
4. **Run full fresh-tree gates once per group, incremental gates per commit**, with
   the gate log named for the exact commit.
5. **Keep one independent review per wave; drop the per-commit watching.** The
   watcher should wake only on BLOCKED, STALLED or done; that cuts most of the
   Claude cost without losing the reviews.
6. **Lift the publication hold now** that the history is clean, so CI runs again on
   every wave.

## 7. Decisions for the operator

1. Send the GitHub support request (draft: `build/audit-w22/scrub/github-support-request.md`).
2. Lift the publication hold: push `wave26` and run CI on all three OSs.
3. Apply recommendation 1 (Dropbox-ignore `build/`).
4. Approve the next work order's shape: item 1-2 of section 5, then 3-5.
5. Merge path: after items 1-2 pass, merge `wave26` into `main` (it contains waves
   23-25).
