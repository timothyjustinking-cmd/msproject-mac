# START HERE — MS Project → native port

_Updated 2026-10-25, wave 28 closed on branch `wave28` (local only: the pre-push
guard refuses the branch on one intermediate blob from `076438f`; rewriting that
commit is Tim's decision — see the wave record).  Keep this file current at every
wave close; it is the only file a new session must read first._

## The 30-second state

* **Wave 28 is closed on branch `wave28`** (cut from `wave27` = `bc52cb8`) from
  `docs/handoff/wave28-brief.md`.  It ran as G0..G5 + CLOSE, one fresh session
  each; the record is **`docs/handoff/wave28-complete.md`** (the gate).  G0..G4
  are DONE; G5's split-task item is **DEFERRED** with its committed Project pair,
  byte measurement and decision log.  **The branch is not pushed and no CI run
  exists for wave 28**: the local pre-push guard refuses the push range because
  the blob added in `076438f` still embeds the operator's name, although
  `d1b5a8a` replaced it at the tip.  Section 0 forbids rewriting history or
  working round the guard; that is Tim's decision.
* **The actuals writer is enabled** (W28-1.2/1.3): each of the five % buttons
  and Task Information's Percent complete field queues one
  `app::StructuralKind::set_percent` edit through `app::make_percent_edit`, and
  File > Save writes Project's own actuals with
  `mppwrite::write_percent_actuals`.  Mark on Track (Task > Schedule) and Update
  Project (Project > Status, Entire project or Selected tasks) queue one
  `update_actuals` edit built by `app::make_update_actuals_edit`, which measures
  each row's percent and actual ticks through the date with the percent-through
  rule (`app::percent_through`).  The seven Project save pairs
  (`tests/data/wave28-percent-*`, `wave28-mark-*`, `wave28-update*`) match on
  FixedData, the edited rows' FixedMeta counts, VarMeta and Var2Data after
  masking step ids, plus the update pair's status-date word; every stream
  outside those stays the control's.  Update Project's "100% complete" and
  rescheduling choices stay disabled with the decision-9 tooltip.  A percent
  edit on a row with no saved task is refused before it is queued; step 5a
  refuses one stale percent edit with a warning instead of failing the save.
* **Outline editing is enabled** (W28-2.1/2.2): Indent/Outdent (Task tab, Edit
  menu, Option+Shift+Right/Left) queue an outline edit built from the file's
  stored outline and calendar; `mppwrite::write_outline_edits` writes the
  measured FixedData/Fixed2Data/FixedMeta/Var2Data words.  The sheet and Gantt
  show the new level, collapse triangle, indentation, OutlineNumber and summary
  bracket at once, with undo/redo (`qt_outline_display`).  No CPM reschedule and
  no Assignment write; those rows are reported, not silently skipped.
* **The new-task mode is measured and written** (W28-3.1): `114/Props` record
  `0x024013c8` stores Project's `0x00FF` (manual) / `0x0000` (auto);
  `mppwrite::write_project_props` changes the two payload bytes and nothing
  else.  Options > Schedule queues the edit; the status bar shows the loaded
  file's stored mode with the last pending edit applied; Insert Task stamps it
  into the new row exactly as Project's own InsertTask probe does.
* **The docked Task Form edits through the sheet's own model** (W28-4.1):
  Name/Duration/Start/Finish commit through `SheetModel::begin_edit`/
  `commit_edit`, percent queues the measured `set_percent` edit, and each field
  is enabled only where the sheet's column is editable.  Predecessors and
  Resources stay read-only (no assignment/link writer); selection-uid guards
  drop stale commits; undo is proved through the real menu action.
* **Split Task stays disabled** (W28-5.1 DEFERRED): Project stores one split as
  a segmented 128-byte planned-work contour on the ASSIGNMENT (`0x0F400031`,
  VarMeta relocation, new `+168` bit `0x2000`) plus a whole-plan slack/date
  rewrite.  `mppwrite` decodes only the flat contour and refuses an existing
  entry; the committed pair `tests/data/wave28-split(-control).mpp` is the
  later writer's comparison, and the button keeps the exact decision-9 tooltip.
* **The wave-27 verifier's two Highs are fixed** (G0): the seven fixtures that
  embedded the operator's local save path are clean (six re-derived through
  Project 2021 on `C:/w28` with neutral metadata; `synthetic-calendars.mpp` was
  neutralised in place so its pinned oracle payload stands), and
  `check_fixtures.py` now fails on any tracked test-data file embedding a drive
  path or user-profile path; outline numbers now follow the stored ID column
  (Project measures "1" and "2"); the fixture builder's FAT vector is sized to
  what the writer reads; Unlink is KEPT because Project's reopen of the port's
  save shows the same date/link facts as Project's own save.
* **Fresh-tree gates at the final code tree `7a69a40`** (log
  `build/w28/close-fresh-gates.log`; the CLOSE docs commit after it is
  docs-only): core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` exit 0;
  core ctest `-E plan_recalc` **52/52**; `plan_recalc` **512** with corpus /
  **266** without; M2 **332/332**, **268/268** and **64/64**; engine
  **7 pass + `constraint-mso` expected**, 8 recorded, 0 unrecorded; reader
  **1100** with corpus / **420** without; app **20/20**; app_qt **24/24**;
  fixtures **OK** (72 documented hashes, 52 tracked `.mpp`), paths OK (94),
  oracle provenance **OK (8)**.
* **Privacy**: the line-based `scan_client_names.py HEAD --messages
  wave27..HEAD` reports **0 files / 0 hits** and **0 commit messages**; the
  whitespace-normalised `guard_names.py` is **0 hits, exit 0** on `3fe29cc..HEAD`
  and on the CLOSE's own range; `client-string-<8 hex>` has no matches.  The
  push-range guard refuses **exactly one** item: the `synthetic-calendars.mpp`
  blob added in `076438f` (the tip's file from `d1b5a8a` is clean).  Two
  pre-wave fixtures at HEAD (`synthetic-base.mpp`, `wave23-outline.mpp`) still
  carry the operator's name three times each, and 69 tracked text files
  (historical docs) carry it; a repo-wide purge is Tim's decision.
  **Run BOTH scans.**
* **The CLOSE review found 0 Critical, 3 High.**  Two read-only reviewers (code
  review + record audit) ran against `7a69a40`; the Highs are **first in wave
  29**: F1 the actuals save does not write Project's FixedMeta `+44`/blob
  changes (the test masks them; brief section 1 makes that a failing check),
  F2 the outline save leaves Project's assignment tombstone/create unwritten
  (the test prints row counts), F3 no gate scans the whole tree for the
  operator's name.  The record's CLOSE carries the full findings table (F1-F7,
  A1-A9), errata for the statements the review falsified, and the wave-29
  queue.  See `docs/handoff/wave28-complete.md`.
* **Still open** (wave-28 CLOSE "Found, not fixed" and the CLOSE review): the
  three Highs above; the push blocker; the operator-name residuals; the stored
  sheet column widths, PROJECT_A's
  yellow per-row fill, bar-label coverage, the dark themes' light sheet/chart
  header bands and fixed ribbon ink, the menu-bar/QAT/status-bar artwork, the
  undecoded stored combination view, the White theme; six dialogs still
  disabled (Text Styles, Gridlines, Bar Styles, Page Setup, Visual Reports,
  Compare Projects); the engines with no writer (project move, baselines, WBS,
  calendars); the wave-26 review's remaining Mediums (the ribbon check-box
  accessible-name sync, the Task Information validate-then-commit ordering, the
  0xFFFF stored point size reaching Qt, multi-row Unlink); the wave-27 review's
  remaining Lows (SKIP-on-missing fixture blocks, `mpp_diff.py`'s ten-offset
  truncation); the CLOSE review's Mediums and Lows (F5 the missing reopen
  artifact, F4/A1 the outline blob mask, F6/F7, A2/A5/A6); the name-scan
  coverage gap (skipped-row names; print the uid/row
  only); two process items (one G4 message quotes gates without naming its log;
  fixture commits also carry the hash registry and provenance); the repo-wide
  CRLF grandfathered files; and the 13 unsalted FNV-1a hashes in the wave-26
  history plus the golden TSV's unsalted hashes (Tim's decision).
* **Branch state**: `main` = **fcb1c10** = `origin/main`.  `wave23`
  (`e1d40b8` = `origin/wave23`), `wave24` (`6dbf126`), `wave25` (`c012813`),
  `wave26` (`1299cc7` = `origin/wave26`) and `wave27` (`bc52cb8` =
  `origin/wave27`) are closed and **not merged**.  **`wave28` is closed locally,
  not pushed** (there is no `origin/wave28`).
* Engine stays **7/8** (`constraint-mso` deliberately left failing;
  `mppwrite::save_plan` is test-only).
* Parity: the wave-28 CLOSE re-ran both on the fresh Qt shell at `7a69a40` —
  wave23-outline **10/10** and wave25-auto colour-only **1/0**, every measure
  PASS (`build/w28/close-parity.log`).  The pixel score is not re-run (no
  reference PNG locally).

## Reading order

**Coordinator starting wave 29** (≈5 files):

1. this file;
2. `docs/handoff/wave28-complete.md` — the gate: G0..G5 + CLOSE, the fresh-tree
   gates, both privacy scans, the independent review, the push refusal, the
   re-scored 16 decisions, "Found, not fixed";
3. `docs/handoff/wave28-brief.md` (the work order); the verifier's
   `docs/handoff/wave28-verification/` may follow this close;
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
  through Project on a NEUTRAL path (`C:/w28`) with neutral
  Author/Manager/Company/UserName/Title.
* **Displayed rows resolve through their uid** (wave 25): `app::print_row_list`
  takes RecordSet rows; never index `plan.tasks` with a RecordSet row; uid 0 is
  the project row and refuses task edits.
* **Saveable structural edits** (waves 26-28): Priority is measured against
  Project and enabled; PercentComplete, Mark on Track, Update Project and the
  Task Form percent field run on the measured actuals writer; outline
  Indent/Outdent run on the measured outline writer; the new-task mode runs on
  the measured `114/Props` writer.  A percent edit and a date update need the
  loaded plan at `push()`; step 5a refuses one stale percent edit instead of
  failing the save.  Every enabled new edit path needs a save-and-reopen test
  AND the standing-rule-1 Project measurement.  Split Task is measured but NOT
  enabled.
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
  again with the reason recorded.  Wave 28 had no dispatch (the push was
  refused); the wave-27 close runs are recorded in wave27-complete.
* **Commits:** one finding id per commit; honest numbers; a gate number must
  belong to the tree it is written in and name its log under `build/w28/`; no
  test claimed unless run.  A known gap is an expected failure **by name**, never
  an assertion of the wrong output.

## Start a session with this (paste)

```
Read docs/START-HERE.md and docs/handoff/wave28-complete.md.  Wave 28 is closed
on branch wave28 (G0..G5 + CLOSE; not pushed: the pre-push guard refuses the
076438f intermediate blob; that history decision is Tim's).  Wave 29 starts
with the CLOSE review's three Highs (F1 actuals FixedMeta +44/blob, F2 outline
assignment tombstone/create, F3 whole-tree operator-name scan), then
wave28-complete's CLOSE "Found, not fixed" list and the plan
(docs/handoff/plan-waves27-30.md): the PROJECT_A gaps, the three format dialogs,
Page Setup, Visual Reports/Compare Projects, and the chrome finish.  Keep every
gate in wave28-complete CLOSE green before each commit and run BOTH the
line-based and the whitespace-normalised client-name scans.
```
