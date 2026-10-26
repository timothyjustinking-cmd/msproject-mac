# START HERE — MS Project → native port

_Updated 2026-10-26, wave 30 closed on branch `wave30` (pushed; CI on the final
code tree `0aca9d2`).  Keep this file current at every wave close; it is the
only file a new session must read first._

## The 30-second state

* **Wave 30 is closed on branch `wave30`** (cut from `wave29` = `2a7fa4b`)
  from `docs/handoff/wave30-brief.md`.  It ran as G0..G3 + CLOSE, one fresh
  session each; the record is **`docs/handoff/wave30-complete.md`** (the gate).
  G0..G3 items are DONE or DEFERRED with a committed Project save pair and its
  measurement log; no item was deferred without evidence.
* **G0 closed the wave-29 review's save-path queue** (W30-0.0..0.7): every
  enabled edit path refuses up front the shapes no committed Project pair
  measured — an assignment on an indent's new summary, an automatic
  indent/outdent on a linked file, an outline move over progress, the 0 → 0
  date-update target, a second progressed leaf in a summary's chain, every
  contour but the measured 9600-tick / 100 % / project-calendar one, and Add
  Link's second-incoming-link and progressed-successor shapes.  The
  "0x0f first link / 0x07 later" rule is falsified: the byte is the successor's
  driving state.
* **G1/G2/G3 enabled eight controls** across the wave: Manually Schedule and
  Mode's manual entry (the measured task-mode writer), Task Details (Task
  Form), Scroll to Task, Fill (Start only), Inspect, Resource Details
  (read-only Resource Form) and Spelling (read-only, port-owned word list).
  Insert Summary/Milestone, Inactivate, Respect Links, Move, Notes, Add to
  Timeline, the text-style group, Add Resources, Information/Notes, Assign
  Resources, Level All, Calculate Project, Set Baseline and WBS are DEFERRED
  with committed pairs; Deliverable, Resource Pool, Team Planner, Subproject
  and the Add-ins commands stay disabled with the exact tooltip.
* **The CLOSE review found 0 Critical / 4 High / 7 Medium / 10 Low** over
  `git diff wave29..wave30`.  All Highs are fixed: C1/C2 by `ec29e31` (unused
  symbols GCC/Clang reject), C3 by `0aca9d2` (physical assignment rows,
  skipped links and the raw resource word now refused up front), A1 by
  correcting the record.  U2 is fixed by `0770e94`, the Set Baseline backlog
  row by `f24ad25`, `memory/writer.md` by `2db8df2`.  The remaining Mediums
  and Lows are queued under Found, not fixed.
* **Fresh-tree gates at the final code tree `0aca9d2`** (log
  `build/w30/W30-CLOSE-fresh-gates.log`; the CLOSE docs commits after it are
  docs-only): core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` exit 0;
  core ctest `-E plan_recalc` **52/52**; `plan_recalc` **512** with corpus /
  **266** without; M2 **332/332**, **268/268** and **64/64**; engine selftest
  **53 checks, 0 failures** and the suite **7 pass + `constraint-mso`
  expected**, 8 recorded, 0 unrecorded; reader **1100** with corpus / **420**
  without; app **22/22**; app_qt **32/32**; fixtures **OK** (131 documented
  hashes, 167 tracked `.mpp`), paths OK (214), oracle provenance **OK (8)**.
* **Parity**: wave23-outline **10/10** and wave25-auto colour-only **1/0**,
  every measure PASS, re-run locally on the fresh Qt shell at `0aca9d2`
  (`build/w30/W30-CLOSE-parity.log`).  The pixel score is still not re-run (no
  PROJECT_A reference PNG local).
* **Privacy**: the line-based `scan_client_names.py HEAD --messages
  wave29..HEAD` reports **0 files / 0 hits** and **0 commit messages**; the
  whitespace-normalised `guard_names.py` is **0 hits, exit 0** on
  `wave29..HEAD` and on the push range `origin/wave29..HEAD`; the hex grep has
  no matches (exit 1, expected).  The 66 new fixtures use the neutral `C:/w30`
  root and neutral metadata; no client/person name or hash is in any.  Two
  pre-wave fixtures and 69 tracked text files still carry the operator's name,
  and the 13 unsalted hashes remain in older history — Tim's decision.  **Run
  BOTH scans.**
* **Push and CI**: `origin/wave30` was advanced without force after a clean
  preflight; both workflows were dispatched explicitly on `wave30` (the
  `workflow_run` trigger only fires from `main`).  qt.yml **40307814439**
  (`windows-qt`, `macos-qt`, `linux-qt`, including both parity gates) and
  ci.yml **40307818793** (`windows-msvc`, `ubuntu-gcc`) are **green** on
  `0aca9d2`, the first dispatch; status snapshots are
  `build/w30/close-ci-{qt,ci}-status.json`.
* **Still open** (wave-30 CLOSE "Found, not fixed"): C4 (task-mode exclusivity
  vs a queued view-state/formula edit), C5 (off-chain progress and the project
  row's Resume/Stop), U1 (Fill on a summary row), U3 (Mode-menu tooltip), U4
  (cross-view Details/Inspect), U5 (Resource Form dock height), U6 (dead
  `TaskInspectorValues::project`), U7/C6 (machine paths), A1 (measurement
  commits carry scripts), the G1/G2/G3 writer gaps (reader fractional ordinal
  and `Task.Active`, save-pair mask bound, VarMeta/214 writers, TBkndCal
  resource calendar, baseline/WBS/scheduler/DSI writers), Spelling's modest
  word list, the wave-29 queue (F3, F6, F7, R28-5/6/8/9/11, the named
  refusals and conventions, the theme-gate sample gap, the stored combination
  view and White theme, Split Task), the 13 unsalted hashes (Tim's decision)
  and the 174 grandfathered CRLF files.
* **Branch state**: `main` = **fcb1c10** = `origin/main`.  `wave23`
  (`e1d40b8` = `origin/wave23`), `wave24` (`6dbf126`), `wave25` (`c012813`),
  `wave26` (`1299cc7` = `origin/wave26`), `wave27` (`bc52cb8` =
  `origin/wave27`), `wave28` (`21646f4` = `origin/wave28`) and `wave29`
  (`2a7fa4b` = `origin/wave29`) are closed and **not merged**.  **`wave30` is
  closed, pushed and not merged** (`origin/wave30` = the CLOSE commit, final
  code tree `0aca9d2`).
* Engine stays **7/8** (`constraint-mso` deliberately left failing;
  `mppwrite::save_plan` is test-only).

## Reading order

**Coordinator starting wave 31** (≈5 files):

1. this file;
2. `docs/handoff/wave30-complete.md` — the gate: G0..G3 + CLOSE, the fresh-tree
   gates, both privacy scans, the independent review and its errata, the push
   and the CI runs, the re-scored 16 decisions, "Found, not fixed";
3. `docs/handoff/wave30-brief.md` (the work order);
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
  through Project on a NEUTRAL path (`C:/w28`, `C:/w29`, `C:/w30`) with neutral
  Author/Manager/Company/UserName/Title.
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
  and no Style dialog writes the stored style records.
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
  again with the reason recorded.  Wave 30 dispatched once; both runs were
  green on `0aca9d2` (qt.yml 40307814439, ci.yml 40307818793).
* **Commits:** one finding id per commit; honest numbers; a gate number must
  belong to the tree it is written in and name its log under `build/w30/`; no
  test claimed unless run.  A known gap is an expected failure **by name**, never
  an assertion of the wrong output.

## Start a session with this (paste)

```
Read docs/START-HERE.md and docs/handoff/wave30-complete.md.  Wave 30 is closed
on branch wave30 (pushed; G0..G3 + CLOSE; eight controls enabled, the rest
deferred with committed Project pairs; CI runs on 0aca9d2 are recorded in the
record's Push and CI section).  Wave 31 continues the plan's wave-30 work
(`docs/handoff/plan-waves27-30.md`): the View tab's commands (Sort, Outline,
Filter, Group by, Highlight) and the second view family (Resource Sheet, Task
Usage, Resource Usage) at Gantt fidelity.  Each command that writes needs its
own Project measurement before it is enabled (standing rule 1), and the
wave-30 CLOSE "Found, not fixed" list (C4, C5, U1, U3-U7, A1 and the carried
residuals) stays the queue behind it.  Keep every gate in wave30-complete CLOSE
green before each commit and run BOTH the line-based and the
whitespace-normalised client-name scans.
```
