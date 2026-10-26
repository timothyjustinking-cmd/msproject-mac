# START HERE — MS Project → native port

_Updated 2026-10-27, wave 31 closed on branch `wave31` (the last wave of the
unattended run of waves 27-31; pushed; CI recorded in the record's Push and CI
section).  Keep this file current at every wave close; it is the only file a new
session must read first._

## The 30-second state

* **Wave 31 is closed on branch `wave31`** (cut from `wave30`'s verification
  commit `ca9fd50`) from `docs/handoff/wave31-brief.md`.  It ran as G0..G5 +
  CLOSE, one fresh session each; the record is
  **`docs/handoff/wave31-complete.md`** (the gate).  Every item is DONE; none was
  deferred.  The final tree is `072773b`; the CLOSE fixes are W31-C1..W31-C7
  (W31-C5 and W31-C6 came from the first CI dispatch, W31-C7 from the second)
  and the CLOSE docs commit after them changes docs only.
* **G0 closed the wave-30 review's four Highs** (W31-0.1..0.4): a link added
  after a queued automatic outline edit is refused in both orders; the
  automatic outline save refuses file shapes no Project pair measured
  (progress/actuals, constraints, deadlines, other summaries); percent/date
  edits refuse progress outside the edited rows unless the edit is the measured
  Entire project update; and Project's Fill Down pair was measured and shows a
  save the port's start writer cannot reproduce, so Fill stays disabled with
  decision 9's exact tooltip.
* **G1 enabled the View tab's data commands** -- Sort (five keys, the Sort
  dialog), Outline (Level 1..9 / All Subtasks) and Tables (the file's stored
  task tables) -- as **session-only** changes: Project persists these in the
  file, so the port applies them to the loaded session and keeps the file's
  bytes (asserted by SHA-256 in `qt_data_commands`).  Group by and Highlight
  are combos, not command controls, so the backlog does not list them disabled.
* **G2 enabled Zoom (the Zoom dialog), Entire Project and Selected Tasks** as
  session-only timescale fits, measured against Project's own windows on two
  fixtures (`tests/data/reference/w31-zoom.measures.json`); New Window, Switch
  Windows, Arrange All, Hide and Macros stay disabled with the exact tooltip
  and their own backlog reasons.
* **G3 brought the Resource Sheet to Gantt fidelity** (presentation only): the
  file's stored resource Entry table, its view Text Styles (overallocated rows
  bold RGB 165,0,0), the stored widths under the measured `chars * unit + 3`
  rule, the tombstone-skipped base resource, Project's computed PeakUnits and
  overallocation, the (i) Indicators badge.  A live Qt gate reads the opened
  window back against Project's COM facts.
* **G4 brought Task Usage and Resource Usage to Gantt fidelity** (presentation
  only): the stored Details lists (Work + Actual Work / Work + Cost), the
  stored two-tier timescale, assignment rows under each task/resource, typed
  per-period Work/Cost values and Project's measured widths.  A live Qt gate
  compares every bucket with COM `TimeScaleData` on
  `tests/data/reference/wave31-usage.measures.json`.
* **G5 decided, and did not build,** Network Diagram, Calendar and the two Other
  Views entries: all four stay disabled with the exact tooltip and the
  generated backlog names the scope reason.
* **No persisted view-state writer was added or enabled.**  Sort order, applied
  table, outline level shown, zoom and window arrangement are all session-only
  this wave, and the record says so item by item (wave-31 brief section 1).
* **The CLOSE review found 1 Critical / 4 High / 3 Medium / 8 Low** over
  `git diff wave30..wave31` (save-path/core, UI and record audits).  All
  Critical/High are fixed: A1/A3 by committing the record and docs, A2 by the
  push and CI, U1 by `5fa3d5d` (usage captions no longer paint over the task
  sheet) and U2 by `165e497` (Open/New from a usage view tear the presentation
  down).  The three Mediums are fixed too: the unsupported counts and log gaps
  are corrected as errata, and U3 by `113d16a` (the backlog now lists all 114
  pin command controls, checked both ways).  The only queued review findings
  are four presentation/contract Lows (C1, C2, S1, S2).  The CI dispatches
  then found three platform-only failures: W31-C5 (`cb69a0b`) names the eight
  disabled View commands in the ribbon switch so GCC/Clang `-Wswitch` builds,
  W31-C6 (`c6e4314`) makes the Resource Sheet gate scan the painted badge and
  glyph areas instead of one pixel, which `windows-qt` failed only because of
  the sample's device-pixel assumption, and W31-C7 (`072773b`) removes the
  `row_map_uids` helper GCC/Clang reject under `-Werror=unused-function`.  All
  three are fixed; the third dispatch is green on the final tree.
* **Fresh-tree gates on `072773b`** (log
  `build/w31/W31-CLOSE-fresh-gates.log`, logged 08:26 and finished 08:36 on three
  trees configured from scratch; the CLOSE docs commit after it changes docs
  only): core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` exit 0;
  core ctest `-E plan_recalc` **52/52**; `plan_recalc` **512** with corpus /
  **266** without; engine selftest **53 checks, 0 failures** and the suite
  **7 pass + `constraint-mso` expected**, 8 recorded, 0 unrecorded; reader
  **1136** with corpus / **456** without; M2 **332/332**, **268/268** and
  **64/64**; app **25/25**; app_qt **37/37**; fixtures **OK** (139 documented
  hashes, 173 tracked `.mpp`), paths OK (222), oracle provenance **OK (8)**.
* **Parity**: wave23-outline **10/10** and wave25-auto colour-only **1/0**,
  every measure PASS, re-run locally on the fresh Qt shell at `072773b`
  (`build/w31/W31-CLOSE-fresh-gates.log`).  The pixel score is still not re-run
  (no PROJECT_A reference PNG local).
* **Privacy**: the line-based `scan_client_names.py HEAD --messages
  wave30..HEAD` reports **0 files / 0 hits** and **0 commit messages**; the
  whitespace-normalised `guard_names.py` is **0 hits, exit 0** on
  `wave30..HEAD` and on the push range `HEAD --not --remotes=origin`; the hex
  grep has no matches (exit 1, expected).  The six new fixtures use the neutral
  `C:/w31` root and neutral metadata; no client/person name or hash is in any.
  Two pre-wave fixtures and 69 tracked text files still carry the operator's
  name, and the 13 unsalted hashes remain in older history — the operator's
  decision.  **Run BOTH scans.**
* **Push and CI**: `origin/wave31` was pushed three times without force, each
  after a clean preflight; both workflows were dispatched explicitly on
  `wave31` after each push (the `workflow_run` trigger only fires from `main`).
  The first dispatch (qt.yml 40400256617, ci.yml 40400261717) was red on all
  three Qt jobs for W31-C5/W31-C6 and green on ci.yml; the second (qt.yml
  40401372235, ci.yml 40401377893) was red on the two Unix Qt jobs for W31-C7
  and green on ci.yml; the third is **green on `072773b`** -- qt.yml
  **40404390641** (`windows-qt`, `macos-qt`, `linux-qt`, including both parity
  gates) and ci.yml **40404395191** (`windows-msvc`, `ubuntu-gcc`).  Status
  snapshots are `build/w31/close-ci-{qt,ci}-status.json`.
* **Still open** (wave-31 CLOSE "Found, not fixed"): the four core review Lows
  (C1 Accrue At 0 prints "0"; C2 the entire-project flag is caller-set; S1 an
  unchecked rate reaches `llround`/the cost multiply; S2 the Resource Sheet
  style fallback can take a non-resource view), the G3/G4 presentation gaps
  (Add New Column stub, other stored usage fields, the port's Total row/column,
  Actual Work/Cost approximations, selected tier size, decoded-not-applied
  scroll origin), the A5 per-commit log gap, and the carried wave-30/29 queue
  in `wave30-complete.md` / `wave29-complete.md`.  The wave-30 measured-and-
  deferred commands still wait on their shared writers (VarMeta insert/remove,
  214 view stream, TBkndCal + Props counts, RichEdit notes, append-and-re-key,
  scheduler/DSI, baseline, WBS code definition/renumber); the view-state writes
  (sort/table/outline/zoom/window arrangement) were never measured against
  Project.  Wave 31 ends the approved run of waves 27-31: the next step is the
  operator's -- verify and merge the unmerged branches, then choose the next
  scope (the after-run list: Team Planner, Network Diagram, Calendar and Report
  views, the `constraint-mso` engine case, macOS launch/notarisation, the
  hashed task names, the remaining review Lows).
* **Branch state**: `main` = **fcb1c10** = `origin/main`.  `wave23`
  (`e1d40b8` = `origin/wave23`), `wave24` (`6dbf126`), `wave25` (`c012813`),
  `wave26` (`1299cc7` = `origin/wave26`), `wave27` (`bc52cb8` =
  `origin/wave27`), `wave28` (`21646f4` = `origin/wave28`), `wave29`
  (`2a7fa4b` = `origin/wave29`) and `wave30` (`ca9fd50` = `origin/wave30`) are
  closed and **not merged**.  **`wave31` is closed, pushed and not merged**
  (`origin/wave31` = the CLOSE docs commit, final tree `072773b`).
* Engine stays **7/8** (`constraint-mso` deliberately left failing;
  `mppwrite::save_plan` is test-only).

## Reading order

**Coordinator starting the next wave** (≈5 files):

1. this file;
2. `docs/handoff/wave31-complete.md` — the gate: G0..G5 + CLOSE, the fresh-tree
   gates, both privacy scans, the independent review and its errata, the push
   and the CI runs, the re-scored 16 decisions, "Found, not fixed" and what the
   end of the run leaves;
3. `docs/handoff/wave31-brief.md` (the work order);
4. `docs/ui-parity-spec.md` — the binding 16-decision UI contract;
5. `docs/ui-reference-set.md` — the local captures and palettes (git-ignored);
6. `memory/coordinator.md` — the compact current memory.

**Lane agent** (≈4 files):

1. this file;
2. the current wave's brief (`docs/handoff/waveNN-brief.md`);
3. your lane's `docs/handoff/<lane>.md` and `memory/<lane>.md`;
4. the code you own.

**Do not read all of `docs/handoff/`.**  Use `docs/handoff/INDEX.md`: current
wave first, recent waves next, history only when investigating a decision.

## On demand (and what it is for)

| file | use it when |
|---|---|
| `docs/engine-parity.md` | engine scores; `constraint-mso` is deliberately left failing |
| `docs/parity.md` | UI measurement contract and the parity harness numbers |
| `docs/ui-qt-port.md` | the three-platform fidelity contract |
| `docs/build.md` | build commands, the Qt ctest PATH (now automatic), the golden opt-in |
| `docs/import-corpus.md` | the real-.mpp corpus, mutations, robustness |
| `docs/coordination.md` | oracle rules (COM is coordinator-only, the lock, FileClose) |
| `docs/history-note.md` | why old commit hashes in old handoffs are dangling |
| `memory/README.md` | which per-stream memory to read for an area |
| `analysis/project_property_names.tsv` | derived Project property-name table (QNEWTASKSAREMANUAL = index 64) |
| `docs/analysis/newtasks-scan-summary.md` | the committed E7 scan counts |

Archives (only for history): `docs/archive/coordinator-handoff-through-wave20.md`,
`docs/archive/WAVES-coordination-board-wave7.md`,
`memory/archive/coordinator-through-wave20-full.md`.

## Standing rules (from wave 27)

`docs/handoff/standing-rules.md` binds every wave: save paths measured against
Project before they are enabled, small working context, per-commit incremental
gates with full fresh-tree gates per group, CI every wave, one independent review
per wave, one finding id per commit.  `build/` is excluded from Dropbox sync.

## House rules that are easy to get wrong

* **Project screenshots stay local** (operator ruling 2026-10-23; a pre-push
  guard enforces it).  Commit only the numbers measured from them:
  `tests/data/reference/*.measures.json`.
* **Never write a client task/resource/project/person name** into any file,
  comment, test or commit message.  Refer to rows by ID and strings by opaque
  label (`client-name-NN`) or structural relation — never by an unsalted hash,
  which a guess inverts.  Run
  `python -I build/audit-w22/scan_client_names.py HEAD` and
  `python -I build/audit-w22/guard_names.py <range>` before every commit and
  expect 0 hits; also `git grep -nE "client-string-[0-9a-f]{8}"` expect none.
* **Fixtures:** never open a committed fixture in place (Project rewrites files
  it opens); oracle scripts use `oracle_lock.working_copy()`.  Run
  `python scripts/check_fixtures.py` before every commit (it checks the
  scenario oracle provenance and fails on an embedded drive/user-profile path).
  Nothing under `tests/data` is written by a lane.  New fixtures are built
  through Project on a NEUTRAL path (`C:/w28`, `C:/w29`, `C:/w30`, `C:/w31`)
  with neutral Author/Manager/Company/UserName/Title.
* **Displayed rows resolve through their uid** (wave 25): `app::print_row_list`
  takes RecordSet rows; never index `plan.tasks` with a RecordSet row; uid 0 is
  the project row and refuses task edits.
* **Saveable structural edits** (waves 26-30): Priority is measured against
  Project and enabled; PercentComplete, Mark on Track, Update Project and the
  Task Form percent field run on the measured actuals writer; outline
  Indent/Outdent run on the measured outline writer; the new-task mode runs on
  the measured `114/Props` writer; Manually Schedule/Mode run on the measured
  task-mode writer.  A percent edit and a date update need the loaded plan at
  `push()`; step 5a refuses one stale percent edit instead of failing the save.
  The file-side shape harness (`PlanInput::assignment_rows`,
  `unreadable_links`, `resource_uid_raw`) refuses up front the shapes no
  Project pair measured (W30-C5).  Every enabled new edit path needs a
  save-and-reopen test AND the standing-rule-1 Project measurement.  Split Task
  is measured but NOT enabled.  The wave-29 dialogs are session-only over the
  open view; Studio style writes do not exist, so Page Setup is layout-only
  and no Style dialog writes the stored style records.  Wave 31 added NO writer:
  Sort, Outline, Tables and Zoom are in-memory session state only.
* **`plan_recalc`** never rewrites the golden by default; refreshing needs the
  exact value `PLAN_RECALC_UPDATE_GOLDEN=1`, and the local/CI gate runs
  `ctest -E plan_recalc` plus the corpus-free binary/step.
* **Oracle/COM** is coordinator-only, under `scripts/oracle_lock.py`, with
  `FileClose(0)` and the post-close file as the only evidence.
* **`corpus/`** and `decompiled/` are git-ignored: nothing from them is
  committed, and the operator's captures are never quoted for client data.
* **Builds:** serial only; local Qt 6.5.3 in `build/Qt/6.5.3/6.5.3/msvc2019_64`.
  Qt ctest prepends its own DLL directory.  Transient Dropbox/MSBuild file locks
  are common; retry the build rather than changing CMake.  Text files are UTF-8
  with LF line endings (174 grandfathered CRLF files remain, `git grep -Il
  $'\r'`; do not add more).
* **CI:** one dispatch per milestone; red runs are triaged, fixed and dispatched
  again with the reason recorded.  Wave 31 dispatched three times: a green
  ci.yml each time, and the Qt matrix green on the third (`072773b`) after
  W31-C5/W31-C6 and then W31-C7 were fixed (qt.yml 40404390641, ci.yml
  40404395191).
* **Commits:** one finding id per commit; honest numbers; a gate number must
  belong to the tree it is written in and name its log under `build/w31/`; no
  test claimed unless run.  A known gap is an expected failure **by name**, never
  an assertion of the wrong output.  The committed
  `scripts/check_command_backlog.py` (ctest `qt_command_backlog`) keeps the
  generated backlog and the ribbon pin in step both ways.

## Start a session with this (paste)

```
Read docs/START-HERE.md and docs/handoff/wave31-complete.md.  Wave 31 is closed
on branch wave31 (pushed; G0..G5 + CLOSE; the View tab's Sort/Outline/Tables/
Zoom are session-only, the Resource Sheet and both usage views render the
file's stored presentation, and Network Diagram/Calendar/Other Views stay
disabled by scope; CI runs on 072773b are recorded in the record's Push and CI
section).  Wave 31 is the last wave of the approved run of waves 27-31.  The
next step is the operator's: verify and merge the unmerged branches (wave23
through wave31), then choose the next scope from the record's "What remains
after this run" (Team Planner, Network Diagram, Calendar and Report views, the
constraint-mso engine case, macOS launch/notarisation, the hashed task names,
the remaining review Lows).  Keep every gate in wave31-complete CLOSE green
before each commit and run BOTH the line-based and the whitespace-normalised
client-name scans.
```
