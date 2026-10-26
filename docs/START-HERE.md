# START HERE — MS Project → native port

_Updated 2026-10-26, wave 29 closed on branch `wave29` (pushed; CI green on all
five jobs at the final code tree `7e14aaa`).  Keep this file current at every
wave close; it is the only file a new session must read first._

## The 30-second state

* **Wave 29 is closed on branch `wave29`** (cut from `wave28` = `21646f4`, the
  rewritten clean tree) from `docs/handoff/wave29-brief.md`.  It ran as G0..G5 +
  CLOSE, one fresh session each; the record is
  **`docs/handoff/wave29-complete.md`** (the gate).  **Every item is DONE;
  nothing is deferred.**
* **G0 closed the wave-28 review's four Highs and the enabled-command gate**
  (W29-0.0..0.4): every enabled command now refuses an unmeasured shape; the
  save-pair tests compare every owned stream field by field against Project's
  file instead of masking it; Project reopened 10 port saves and found **0
  differing fields** (project row + IDs 1..3); the percent writer is measured
  on three real schedules (leaf 0→40 under a summary, work-bearing assignment
  0→40, zero-duration milestone 0→100) and the mid-to-mid transition stays
  refused with 50 named `EXPECTED FAILURE` checks; Mark on Track / Update
  Project lower recorded progress exactly as Project does (60→40 before the
  date, 60→80 after); indent/outdent write Project's assignment tombstone and
  re-create on automatic rows.
* **G1 closed the PROJECT_A gaps** (W29-1.0..1.4): stored `CTable` sheet column
  widths, the stored per-row yellow fill, every bar label the stored bar styles
  call for, and a saved timescale origin/zoom.  A stock start plus the stock
  100 % day tier is Project's default and deliberately changes nothing (it is
  the shell's own week zoom; W29-C6).
* **G2/G3/G4 added the six wave-28 backlog dialogs**: Text Styles, Gridlines
  and Bar Styles (Text/Bar styles apply to the open view; Gridlines is laid out
  and inert), Page Setup (six tabs, fully wired to the shared print layout),
  and Visual Reports / Compare Projects (every current-affecting control
  disabled with the exact decision-9 tooltip and an inline note).
* **G5 finished the dark chrome**: the sheet caption band, gutter/corner and
  the Gantt timescale band paint from the measured Dark Gray/Black palettes,
  the ribbon ink follows the theme, and the menu-bar/QAT/status-bar glyphs are
  re-authored on the one-ink grid and re-inked live per theme.
* **The CLOSE review found 0 Critical / 2 High / 2 Medium / 2 Low** over
  `git diff wave28..wave29`.  Both Highs are fixed (`02b720a` C1 the two
  unused functions GCC/Clang reject; `4b03ecb` C2 the measured-only percent
  transitions).  The first CI dispatch then found C7 (High, macOS unused
  constant, `f7434d2`) and C9 (High, GCC `OutlineEdit` initializer,
  `133cb3f`), the record audit found C8 (Medium, dark tier boundary painted
  strong while the measurement is weak, fixed `31bb11b`), and the parity gate
  found C10 (High, the stock-timescale axis shift, fixed `7e14aaa`).  C3-C7
  (the remaining Mediums/Lows) are queued under Found, not fixed.
* **Fresh-tree gates at the final code tree `7e14aaa`** (log
  `build/w29/close-final-fresh-gates.log`, filed as
  `build/w29/gates-7e14aaa-W29-CLOSE.log`; the CLOSE docs commit after it is
  docs-only): core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` exit 0;
  core ctest `-E plan_recalc` **52/52**; `plan_recalc` **512** with corpus /
  **266** without; M2 **332/332**, **268/268** and **64/64**; engine selftest
  **53 checks, 0 failures** and the suite **7 pass + `constraint-mso`
  expected**, 8 recorded, 0 unrecorded; reader **1100** with corpus / **420**
  without; app **20/20**; app_qt **27/27**; fixtures **OK** (98 documented
  hashes, 101 tracked `.mpp`), paths OK (148), oracle provenance **OK (8)**.
* **Parity**: wave23-outline **10/10** and wave25-auto colour-only **1/0**,
  every measure PASS, re-run locally on the fresh Qt shell at `7e14aaa`
  (`build/w29/c6-parity-fixed.log`, `build/w29/c6-parity-wave25.log`).  The
  pixel score is still not re-run (no PROJECT_A reference PNG local).
* **Privacy**: the line-based `scan_client_names.py HEAD --messages
  wave28..HEAD` reports **0 files / 0 hits** and **0 commit messages**; the
  whitespace-normalised `guard_names.py` is **0 hits, exit 0** on
  `wave28..HEAD` and on the push range `origin/wave28..HEAD`; the hex grep has
  no matches (exit 1, expected).  The wave-28 push blocker is gone: Tim
  approved the `076438f` rewrite, `wave28` was replayed clean and pushed
  (`21646f4`), and the guard now passes the whole range.  Two pre-wave
  fixtures (`synthetic-base.mpp`, `wave23-outline.mpp`) still carry the
  operator's name three times each and 69 tracked text files carry it; a
  repo-wide purge is Tim's decision.  **Run BOTH scans.**
* **Push and CI**: `origin/wave29` was advanced without force three times
  during CLOSE, after a clean preflight each time.  Both workflows were
  dispatched explicitly on `wave29` (the `workflow_run` trigger only fires from
  `main`); the first dispatch was red on C7/C9/C10, and the second runs on the
  final code tree `7e14aaa` are **green**: qt.yml **40248190323** (`windows-qt`,
  `macos-qt`, `linux-qt`, including both parity gates) and ci.yml
  **40248193087** (`windows-msvc`, `ubuntu-gcc`).  Status snapshots are
  `build/w29/close-ci4-{qt,ci}-status.json`.
* **Still open** (wave-29 CLOSE "Found, not fixed"): C3 the `add_task_link`
  Fixed2Meta +8 rule for a second incoming link (needs a Project pair); C4 the
  null-save classifier's masked tail (tooling); C5 the late refusal of a
  percent edit on a linked row; C6 the Props-mask sentence (documentation);
  the wave-28 residuals (whole-tree operator-name purge, the UTF-16
  user-profile check, save-time ordering/late refusals, the Task Form's
  view-only fields, SKIP-on-missing fixture blocks, the `tests/data/README.md`
  inventory gap, the process items); the theme gate's missing
  tier-boundary sample; the stored combination view and the White theme; Split
  Task (deferred with its measurement); the no-writer engines (project move,
  baselines, WBS, calendars); the wave-26/27 review Mediums/Lows; the 13
  unsalted FNV-1a hashes in older history and the golden TSV (Tim's decision);
  174 grandfathered CRLF files.
* **Branch state**: `main` = **fcb1c10** = `origin/main`.  `wave23`
  (`e1d40b8` = `origin/wave23`), `wave24` (`6dbf126`), `wave25` (`c012813`),
  `wave26` (`1299cc7` = `origin/wave26`), `wave27` (`bc52cb8` =
  `origin/wave27`) and `wave28` (`21646f4` = `origin/wave28`, rewritten clean
  with Tim's approval) are closed and **not merged**.  **`wave29` is closed,
  pushed and not merged** (`origin/wave29` = the CLOSE docs commit, code tree
  `7e14aaa`).
* Engine stays **7/8** (`constraint-mso` deliberately left failing;
  `mppwrite::save_plan` is test-only).

## Reading order

**Coordinator starting wave 30** (≈5 files):

1. this file;
2. `docs/handoff/wave29-complete.md` — the gate: G0..G5 + CLOSE, the fresh-tree
   gates, both privacy scans, the independent review and its errata, the push
   and the CI runs, the re-scored 16 decisions, "Found, not fixed";
3. `docs/handoff/wave29-brief.md` (the work order); the verifier's
   `docs/handoff/wave29-verification/` may follow this close;
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
  through Project on a NEUTRAL path (`C:/w28`, `C:/w29`) with neutral
  Author/Manager/Company/UserName/Title.
* **Displayed rows resolve through their uid** (wave 25): `app::print_row_list`
  takes RecordSet rows; never index `plan.tasks` with a RecordSet row; uid 0 is
  the project row and refuses task edits.
* **Saveable structural edits** (waves 26-29): Priority is measured against
  Project and enabled; PercentComplete, Mark on Track, Update Project and the
  Task Form percent field run on the measured actuals writer; outline
  Indent/Outdent run on the measured outline writer; the new-task mode runs on
  the measured `114/Props` writer.  A percent edit and a date update need the
  loaded plan at `push()`; step 5a refuses one stale percent edit instead of
  failing the save.  Every enabled new edit path needs a save-and-reopen test
  AND the standing-rule-1 Project measurement.  Split Task is measured but NOT
  enabled.  The wave-29 dialogs are session-only over the open view; Studio
  style writes do not exist, so Page Setup is layout-only and no Style dialog
  writes the stored style records.
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
  again with the reason recorded.  Wave 29 dispatched twice (the first runs
  40245417658 / 40245429060 were red on C7/C9/C10; the second runs
  40248190323 / 40248193087 are on `7e14aaa`).
* **Commits:** one finding id per commit; honest numbers; a gate number must
  belong to the tree it is written in and name its log under `build/w29/`; no
  test claimed unless run.  A known gap is an expected failure **by name**, never
  an assertion of the wrong output.

## Start a session with this (paste)

```
Read docs/START-HERE.md and docs/handoff/wave29-complete.md.  Wave 29 is closed
on branch wave29 (pushed; G0..G5 + CLOSE, every item DONE; CI runs on 7e14aaa
are recorded in the record's Push and CI section).  Wave 30 follows
docs/handoff/plan-waves27-30.md: the 83 disabled ribbon commands, highest use
first (Task: Move, Inspect, Deliverable; Resource: Assign Resources, Resource
Pool, levelling; Project: baselines, WBS, Calculate; View: Sort, Outline,
Filter, Group by, Highlight), then the second view family (Resource Sheet,
Task Usage, Resource Usage) at Gantt fidelity.  Each command that writes needs
its own Project measurement before it is enabled (standing rule 1), and the
wave-29 CLOSE "Found, not fixed" list (C3-C6 and the carried residuals) stays
the queue behind it.  Keep every gate in wave29-complete CLOSE green before
each commit and run BOTH the line-based and the whitespace-normalised
client-name scans.
```
