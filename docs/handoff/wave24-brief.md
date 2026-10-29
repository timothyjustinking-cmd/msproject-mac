# Wave 24 brief - verify-and-fix work order (for DeepSeek, coordinator role)

_Written 2026-10-23 (night) by the Opus verifier session after wave 23 closed.
Baseline: branch `wave24`, cut from local `wave23` = `d236f91` (wave 23's tip).
`wave23` is NOT merged to `main`; `origin/wave23` = `63cd087` (the later wave-23
commits were held from GitHub, see section 0).  Sources: the verifier's fresh-tree
gates and independent review (`docs/handoff/wave23-verification/gates.md`,
`review.md`, ids R1..R18), the watcher's commit-by-commit notes (ids V1..), and
the items `wave23-complete.md` deferred.  This file is the one document the fix
session reads; where it conflicts with an older handoff, this file wins._

## 0. Publication hold (read first; the operator decides, you do not)

Client data is already on the PUBLIC GitHub repo: corpus task names, both client
project titles and two people's names, in about 15 files on `main` (mostly from
wave 22's `abeafd2`) and in pushed wave-23 commits.  The operator has not yet ruled on making
the repo private or rewriting history.  Until the operator does:

- **Do not push anything.** Commit locally on `wave24` only.  A local pre-push
  guard (`.git/hooks/pre-push` + `build/audit-w22/guard_names.py`) refuses any push
  that adds an image under `tests/data/reference/`, anything under `corpus/`, or a
  corpus name; the classifier also blocks pushes of the branch.  Do not try to get
  round either.
- **Do not rewrite history or force-push** any branch.  Do not delete the local
  `wave23-with-screenshot` / `wave23-pre-push` branches.
- **CI:** the end-of-wave dispatch happens only if the operator has lifted the hold by then
  (look for a line in `docs/START-HERE.md` or a commit from the operator saying so).
  Otherwise record "CI not run: publication hold" in `wave24-complete.md` and rely on
  the local gates.
- **Never write a client task name, project title, resource/person name or the
  company Dropbox path** into any file, comment, test or commit message.  Refer to
  rows by ID and to strings by hash.  Check your own diff before each commit with
  `python -I build/audit-w22/scan_client_names.py HEAD --messages HEAD~1..HEAD`
  (prints labels only, never names; it must report 0 hits for your commit).

## 1. Standing rules (unchanged from wave 23 section 1, restated)

- Target: 100 % parity with MS Project Professional 2021 (16.0.20430.20182) on
  this machine; Project wins every disagreement; nothing faked.
- Branch `wave24`; one finding id per commit (`W24-n` plus the R/V id); no merge.
- **Every** gate before **every** commit, with the numbers in the commit message:
  core `ctest -E plan_recalc`, `plan_recalc_tests` with `MPP_NO_CORPUS=1`, M2
  `recalc --compare` 332/332, the engine suite lane, app ctest, app_qt ctest,
  `scripts/check_fixtures.py`, and the client-name scan above.  Wave 23 omitted M2
  and the suite from several commit messages; that is not acceptable this wave.
- Goldens/fixtures: a change under `tests/data/` is its own commit justified by a
  Project measurement.  Fixtures are built by Project through `oracle_lock.py` on a
  copy saved under a NEUTRAL path (for example `C:\w24\fixture.mpp`) with
  `ProjectSummaryInfo` Author/Manager/Company and the application user name set to
  neutral values before the save (R14: the wave-23 fixtures embed the operator's
  name and the company Dropbox path).
- Oracle/COM only through `oracle_lock.py`, Project closed afterwards; serial
  builds, output redirected; screenshots stay local (git-ignored); line endings
  LF (V: `docs/handoff/coordinator.md` was churned to CRLF in `dba88e1`).
- Report after, not before; nothing silently dropped.

## 2. Authorised now - in this order

### 2.1 Data integrity (do these first)

**W24-1  R1 (Critical) / R5 / R6 / R7  Sheet row vs task index after row 0.**
B-1 inserted a synthetic project row 0 into the sheet but `doc_.plan().tasks` has
no such row, and several call sites index tasks by sheet row.  Confirmed in source:
`app_qt/main_window.cpp` `show_task_fields()` passes
`doc_.plan().tasks[selected.row]`, so Custom Fields opens on, and Save writes to,
the task BELOW the selection.  Also: the Delete Task confirmation names the wrong
task (`:2585`), the context menu's Auto/Manual label reads the wrong row (`:2788`),
the printed Gantt is one row off the printed sheet (`app_qt/print_page.cpp:138`),
Task Information on the project row edits through the sheet model and is then
silently dropped (`apply_task_information`, `:2855`), and Insert Task templates the
pending row from the highest-ID task while the writer clones the last stored row
(`app/reader_adapter.h:145` vs the writer).  Fix: resolve every selection to a
**uid** (`SelectedTask::uid` already exists) and look tasks up by uid; never index
`plan().tasks` with a sheet row; make the project row non-editable everywhere (or
editable exactly as Project allows, through the same guarded path).  Acceptance:
for each call site a test that selects row N (N = 1 and a middle row) with the
project row shown and asserts the uid the action acts on; a save-and-reread test
proving a Custom Fields edit lands on the selected task; a print test asserting the
sheet and Gantt rows align; Insert Task's pending row equals what Save writes.
Verifier: do not "fix" by subtracting 1; it breaks again when the row is hidden.

**W24-2  R2 (High) / V  The project summary row IS stored in the file.**  214/Props
record `0x03400009` (int16) is 0 in a Project save with the summary row off and 1
with it on (the reviewer measured the pair in `build/w23/summary-off.mpp` and
`summary-on.mpp`; re-measure it yourself on a fresh oracle pair before relying on
it).  Decode it in the reader, show row 0 only when the file says so (project_a's
saves carry 1; every committed fixture carries 0), and revert the five gates that
were changed to expect a row Project would not show (shell, chrome, save_reopen,
stored_table, task_order) back to file-driven expectations.  A new project follows
Project's default for a new file (measure it).  Commit an on/off fixture pair.

**W24-3  R3 (High) / V  Timeline visibility: measure before you decide.**  The
"application state" conclusion used the same 214-only diff that missed W24-2.
Build a shown/hidden save pair in Project and byte-diff EVERY CFB stream (not only
214/ and Props); then the behavioural test: hide the strip, save, close, open a
different file - if it is hidden there too, it is application state.  Drive the
strip from whatever the evidence shows and restore the brief's project_a acceptance
if it is file state.  Record the measurement in the commit.

**W24-4  R4 (High)  CI lost the corpus-free plan_recalc checks.**  `ci.yml` runs
`-E plan_recalc`, which drops the 266 corpus-free checks.  Add a step that runs
`plan_recalc_tests` with `MPP_NO_CORPUS=1` (the golden is read-only now, so it is
safe).  Also make `PLAN_RECALC_UPDATE_GOLDEN` accept only the exact value `1` (V).

### 2.2 Gates that still do not test their claim

**W24-5  R8  ID-order sort is untested in CI.**  Both committed fixtures are already
in ID order, so deleting the sort leaves CI green.  Build (Project, neutral path) a
fixture whose stored row order differs from its ID order and pin it.

**W24-6  V  Double-click opens Task Information.**  The gate emits `doubleClicked`
programmatically; the audit's live test saw a real double-click do nothing.  Use
`QTest::mouseDClick` on the sheet viewport and assert the dialog (and that the
project row does not open an editable dialog, R5).

**W24-7  R10 / V  Engine lane checks the count only.**  `--expect 7/8` would pass if
constraint-mso started passing and another scenario broke.  Pin the failing set by
name (`--expect-fail constraint-mso`).  Add provenance (Project version, recorded-at,
source sha256) to the eight existing oracles from git history or a re-record, and
say which.

**W24-8  V  The Predecessors/Successors golden is one hash over 83 rows.**  A failure
cannot name the row.  Store one hash per row (ID + hash of that row's Pre/Suc text).
Fix the dangling "2e177c5" reference in `c6edd1d`'s rationale in the docs.

**W24-9  R13 / R11  Tautologies.**  The property-name reader test compares constants
with constants; the project row's SUMMARY=Yes check mirrors a hard-coded value.
Make each test read an independent source (the committed TSV vs the reader's parse of
fixture bytes; the file's stored flag for the project row).

**W24-10  R9 / V  CI workflow_run.**  It only fires from `main`'s workflow file (so
it has never fired for a wave branch), checks out `head_branch` instead of
`head_sha`, has no `conclusion == 'success'` filter, and runs ci twice on a tag.
Fix all four; until merged, dispatch both workflows explicitly.

**W24-11  V  Reference capture without bars.**  `tests/data/reference/
wave23-outline-project.measures.json` has `colour.task_bar` found=false because the
capture's chart was scrolled to October while the tasks are in December.  Recapture
in Project after Scroll to Task (keep the PNG local), regenerate the JSON, and make
the gate fail if any colour measure is not found.

**W24-12  R15 / V  Ribbon pin and reachability.**  Assert the pin's `kind` column
(a combo built as a button must fail).  Q17 was answered "restore Task Sheet,
Resource Sheet, Resource Usage to View > Task Views / Resource Views drop-downs";
`e4b2bd9` says they are reachable from the status bar, which is untrue for Task
Sheet.  Restore them where Project has them.

### 2.3 Parity corrections to wave-23 work

**W24-13  R12 / V  Text styles.**  "Milestone italic" was hard-coded as Project's
stock style from project_a's own capture; the port's "critical = red" is the same
class.  Measure a NEW blank project in Project (add a milestone and a critical task,
capture locally): drive italic/red/font from the FILE's decoded text styles, with
Project's measured stock style as the fallback.

**W24-14  V  Custom Fields.**  Project > Custom Fields is the field-DEFINITION dialog
(type, rename, formula, lookup, graphical indicators; captured in
`corpus/reference/2026-10-23-audit/`).  Build it laid out with unimplemented actions
disabled; move the per-task value editor into Task Information's Custom Fields tab.

**W24-15  V  Flag10 generically.**  Decode Flag1..Flag20 by the same one-bit
measurement and name the field Flag10, not `summary_flag`.

**W24-16  R18 / V  Claims.**  Reconcile the reader check count (1016 -> 1025 vs ~14
new checks) and any other `wave23-complete.md` number the diff does not support.

### 2.4 Wave-23 deferrals, carried

**W24-17** W23-3 timescale tiers (decode the unit encoding in `0x22400008`; six
`TimescaleEdit` variants are in `build/w23/`).  **W24-18** W23-4 bar styles (colour
records in the same view blob).  **W24-19** W23-5 file font/size and highlighted
summary rows.  **W24-20** W23-14 distinct icons with a uniqueness gate.
**W24-21** W23-15 control kinds from `app_qt/ribbon_pin_unmodelled.tsv`.
**W24-22** W23-16 backstage pages, Account/About, the 11-page Options dialog, the
B-14 dialogs.  **W24-23** W23-18 dark theme (chrome only; sheet/chart unchanged).
**W24-24** W23-20 remaining dialogs and the split view.  **W24-25** W23-21/22.

### 2.5 Forward privacy scrub (in the working tree only)

**W24-26  Section 0 / R14.**  Replace every client task name, project title and
person name in the CURRENT tree (tests, comments, docs, specs, scripts,
`docs/analysis/oracle/*.json`) with IDs or hashes, so the tip is clean whatever the operator
decides about history.  The scan above lists the files (labels only).  Do not touch
history.

## 3. Fixes where the obvious change is wrong

| finding | tempting fix | why wrong | correct fix |
|---|---|---|---|
| W24-1 | `tasks[row - 1]` | breaks when the row is hidden or filtered | look up by uid |
| W24-2 | keep forcing row 0 on | Project shows it only when 0x03400009 = 1 | decode and follow the file |
| W24-3 | trust the 214-only diff | it already missed W24-2 | diff every stream + behavioural test |
| W24-4 | drop `-E plan_recalc` with the corpus present | CI has no corpus; locally it reads the corpus | run the binary with `MPP_NO_CORPUS=1` |
| W24-11 | relax the gate when a colour is missing | that is the A-1 gap again | recapture with bars in view |
| W24-26 | `git filter-branch` / force-push | history is the operator's decision | scrub the tree forward only |

## 4. Verification gate (before `wave24-complete.md` says done)

1. Every W24 item fixed with commit id, or deferred with a reason.
2. All section-1 gates green on fresh trees, numbers recorded; the name scan reports
   0 hits on every wave-24 commit and on the tip for the files W24-26 touched.
3. A separate review pass over `git diff wave23..wave24` (fresh session or
   subagent) for regressions and weakened gates; findings fixed or listed.
4. CI per section 0.
5. Live same-file check against Project on a copy of `corpus/project_a.mpp`,
   captures local, a table of what still differs.
6. START-HERE, INDEX, `memory/coordinator.md` updated; the 16 decisions re-scored.
