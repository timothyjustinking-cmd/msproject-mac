# START HERE — MS Project → native port

_Updated 2026-10-24, wave 26 closed on branch `wave26` (not merged, not
pushed).  Keep this file current at every wave close; it is the only file a new
session must read first._

## The 30-second state

* **Wave 26 is closed on branch `wave26`** (cut from local `wave25` = 3b4da5f)
  from `docs/handoff/wave26-brief.md`.  It ran as groups G0..G6 + CLOSE, one
  fresh session each; the record is **`docs/handoff/wave26-complete.md`** (the
  gate).  Every group's items are DONE; nothing is deferred.  G0 fixed the one
  High from the wave-25 review; G1..G6 did the parity work waves 23-25 had
  deferred.  The publication hold still applies: nothing is pushed or merged.
* **Parity landed this wave**: the ribbon's real control kinds (combos, split
  buttons, check boxes, a date field, a 16-swatch gallery, launchers) with live
  widget-class and measured-geometry gates (G1); an original one-ink glyph for
  all 114 command controls on the seven tabs with uniqueness and monochrome
  gates (G2); the FILE's text styles, bar styles and timescale tiers decoded
  from the `0x22400008` Gantt view and rendered — PROJECT_A's italic red
  milestones, navy/red/green bars and year/quarter/month tiers (G3); the File
  rail/backstage pages and the 11-page Options dialog (G4); Project Information
  with a saveable Priority, read-only Change Working Time, the Custom Fields
  definition dialog plus Task Information's Custom Fields tab, and the
  Update/Move/Baseline/WBS/Links dialogs (G5); the View task/resource
  drop-downs, the Dark Gray/Black chrome themes and the docked Task Form split
  view (G6).
* **Fresh-tree gates at the close** (final tree `3d78191`, after the review's
  High fixes; configured from scratch): core **51/51**, `plan_recalc` **512**
  with corpus / **266** without, M2 **332/332**, engine **7 pass +
  `constraint-mso` expected**, reader **1100** with corpus / **420** without,
  app **17/17**, app_qt **23/23**, fixtures **OK** (25 documented hashes, 21
  tracked `.mpp`), parity **10/10** plus the colour-only **1/0**.  The single
  log is `build/w26/final-gates.log`.
* **W26-0.1**: the print row list comes from the RecordSet through
  `app::print_row_list()` (the filter's rows when a filter is active, every
  RecordSet row otherwise) and is shared by the macOS, Qt and headless paths;
  `app_layout` and `app_smoke` run it on the row-shown
  `tests/data/wave24-order.mpp`, and the smoke round-trip leaves the fixture
  byte-identical.
* **New saveable edits**: the ribbon's % Complete buttons use
  `StructuralKind::set_percent` and Project Information's Priority uses
  `set_project_priority`; both are written by step 5a of
  `save_structural_edits` (one `mppwrite::TaskTable` write) and the headless
  tests re-read 40 % and 750 from the saved file.  The stored table's % Complete
  column remains presentation-only; a typed cell edit is refused, never
  silently dropped.
* **Privacy**: the line-based scan reports 0 hits on every wave-26 tree, but the
  independent review showed that pass is a **false negative for a wrapped
  name**: a PROJECT_A task name was quoted across two lines in the G3 record.
  A whitespace-normalised scan found it on 18 commit trees; it is removed in
  `759a856` and the normalised scan is clean on the close tree and on every
  commit message.  The name remains in those 18 commits' history — removing it
  needs a rewrite, which the hold leaves to Tim, as with the wave-23/24 names.
  **Run the normalised pass, not only `git grep -F`.**  Residuals:
  `tests/data/project_a_golden.tsv` still carries an unsalted 16-hex hash of
  each task name (the reader's oracle), and the plaintext names in waves 23-24
  history remain — both are Tim's decision with the hold.
* **Publication hold still in force:** client data is already on the public
  repo; do not push anything and do not rewrite history until Tim rules (brief
  section 0).  CI was therefore **not dispatched** for wave 26; the local
  fresh-tree gates are the evidence.
* **Still open** (wave26-complete CLOSE "Found, not fixed"): the stored sheet
  column widths, PROJECT_A's yellow per-row fill (decode-only highlight), the
  bar-label coverage, the dark themes' light sheet/chart header bands and fixed
  ribbon ink, the menu-bar/QAT/status-bar artwork, the display-only Task Form,
  the undecoded stored combination view, the White theme, six dialogs still
  disabled (Text Styles, Gridlines, Bar Styles, Page Setup, Visual Reports,
  Compare Projects), the engines with no writer (actuals, project move,
  baselines, WBS, calendars), and the review's remaining Mediums: the
  ribbon check-box accessible-name sync, the Task Information
  validate-then-commit ordering, the Options new-task mode not reaching
  `insert_task()`, and the pending-insert % edit that poisons a save.
* **Wave 25 is closed on branch `wave25`** (cut from local `wave24` =
  `2561a51`), record `docs/handoff/wave25-complete.md`; `wave23` and `wave24`
  are closed, verified and not merged.  `main` = **3f973d5**.  `origin/wave23` =
  `63cd087`; remote `wave24`/`wave25`/`wave26` do not exist.
* Engine stays **7/8** (`constraint-mso` deliberately left failing;
  `mppwrite::save_plan` is test-only).
* Parity still deferred/remaining (wave26-complete CLOSE): the items in the
  "Still open" bullet above; the wave-25 deferred list (W25-12..W25-21) is
  closed by wave 26 except for those residuals.

## Reading order

**Coordinator starting wave 27** (≈5 files):

1. this file;
2. `docs/handoff/wave26-complete.md` — the gate: G0..G6 + CLOSE, the fresh-tree
   gates, the independent review, the live comparison, the re-scored 16
   decisions, "Found, not fixed";
3. `docs/handoff/wave26-brief.md` (the work order) and
   `docs/handoff/wave25-verification/` (what G0 answered);
4. `docs/ui-parity-spec.md` — the binding 16-decision UI contract;
5. `docs/ui-reference-set.md` — the local captures and palettes (git-ignored);
6. `memory/coordinator.md` — the compact current memory.

**Lane agent** (≈4 files):

1. this file;
2. `docs/handoff/wave26-brief.md`;
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
  `python -I build/audit-w22/scan_client_names.py HEAD` before every commit and
  expect 0 hits; also `git grep -nE "client-string-[0-9a-f]{8}"` expect none.
* **Fixtures:** never open a committed fixture in place (Project rewrites files
  it opens); oracle scripts use `oracle_lock.working_copy()`.  Run
  `python scripts/check_fixtures.py` before every commit (it also checks the
  scenario oracle provenance).  Nothing under `tests/data` is written by a lane.
  New fixtures are built through Project on a NEUTRAL path (`C:/w25`) with
  neutral Author/Manager/Company/UserName.
* **Displayed rows resolve through their uid** (wave 25): `app::print_row_list`
  takes RecordSet rows; never index `plan.tasks` with a RecordSet row; uid 0 is
  the project row and refuses task edits.
* **Saveable structural edits** (wave 26): % Complete and project Priority go
  through `save_structural_edits` step 5a (`mppwrite::TaskTable`), not the
  presentation-only sheet column.  Every new edit path needs a save-and-reopen
  test.
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
  with LF line endings (two wave-26 dialog files landed CRLF; normalise when
  touched).
* **CI:** one dispatch per milestone; red runs are triaged, fixed and dispatched
  again with the reason recorded.  **Wave 26's end-of-wave CI was not run
  because of the publication hold.**
* **Commits:** one finding id per commit; honest numbers; a gate number must
  belong to the tree it is written in and name its log under `build/w26/`; no
  test claimed unless run.  A known gap is an expected failure **by name**, never
  an assertion of the wrong output.

## Start a session with this (paste)

```
Read docs/START-HERE.md and docs/handoff/wave26-complete.md.  Wave 26 is closed
on branch wave26 (not merged, not pushed; publication hold).  If Tim has ruled
on the client data, merge/handle the branches as he directs; otherwise continue
wave26-complete's CLOSE "Found, not fixed" list: the stored sheet column widths,
PROJECT_A's yellow per-row fill, the dark themes' light header bands, the
menu/QAT/status-bar artwork, the stored combination view, the six dialogs still
disabled (Text Styles, Gridlines, Bar Styles, Page Setup, Visual Reports,
Compare Projects), and the review's four Mediums (ribbon check-box sync, Task
Information ordering, the Options new-task mode, the pending-insert % edit).
Keep every gate in wave26-complete CLOSE green before each commit and run BOTH
the line-based and the whitespace-normalised client-name scans.
```
