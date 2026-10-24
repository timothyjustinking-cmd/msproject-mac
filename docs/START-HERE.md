# START HERE — MS Project → native port

_Updated 2026-10-24, wave 27 closed on branch `wave27` (pushed; CI on all three
OSs).  Keep this file current at every wave close; it is the only file a new
session must read first._

## The 30-second state

* **Wave 27 is closed on branch `wave27`** (cut from local `wave26` = `1299cc7`)
  from `docs/handoff/wave27-brief.md`.  It ran as G0..G3 + CLOSE, one fresh
  session each; the record is **`docs/handoff/wave27-complete.md`** (the gate).
  Every item is DONE; nothing is deferred.  The wave made the save safe and got
  CI green on all three OSs; the publication hold is **lifted** (history scrubbed
  24 Oct 2026), and `wave27` is pushed.
* **The % Complete buttons are DISABLED** (exact decision-9 tooltip "not
  implemented in this port") until wave 28's actuals writer.  Project's own 50 %
  save (`tests/data/wave27-percent-actuals.mpp`) writes Actual Duration,
  Remaining Duration, % Complete, Actual Start, two date words, a progress flag,
  the project row's roll-up and Var2Data field 0x0B40015E; the old port wrote +92
  only.  No UI path may queue `StructuralKind::set_percent` before wave 28.
  The edit kind, save path and tests are kept, marked NOT ENABLED.
* **The edit model owns the percent rules** (W27-0.2): `app::make_percent_edit`
  refuses uid 0, an unknown uid, a value above 100 and any summary row;
  `StructuralEdits::push(edit, const PlanInput*)` REQUIRES the plan for a
  percent edit and refuses a plan-less one.  Step 5a of `save_structural_edits`
  refuses one stale percent edit with a warning instead of throwing, so it
  cannot fail every later save (W27-3.1).
* **Project Priority 750 is measured and KEPT** (W27-0.3): the Project pair
  `tests/data/wave27-priority-control.mpp` / `-750.mpp` differs in exactly row
  0's +78 word; `app/project_dialogs_tests.cpp` saves from the control fixture
  and compares the saved `114/TBkndTask/FixedData` byte-for-byte with Project's
  750 save.
* **Other wave-27 fixes**: delete/undo restores the field/percent/link edits a
  delete displaces (W27-3.2); Unlink removes BOTH links touching the task and
  its link table matches Project's own UnlinkTasks save (W27-3.3,
  `wave27-unlink*.mpp`; the task table is not compared -- Project's Unlink also
  reschedules, which the port does not); the
  real save tests fail on a failed or empty load (W27-3.4); the 13 invertible
  FNV-1a name hashes are gone, replaced by ID/level/summary/OutlineNumber
  structure (W27-3.5); the three wholly-CRLF sources are LF (W27-3.6).  The
  wave-26 Gantt style/font controlled saves are committed fixtures with one
  decoder test per change (W27-2.1).
* **Fresh-tree gates at the close** (final code tree **9323db2**; configured
  from scratch under `build/w27-fresh{,-app,-qt}`, built serially):
  core **51/51**, `plan_recalc` **512** with corpus / **266** without, M2
  **332/332**, engine **7 pass + `constraint-mso` expected**, reader **1100**
  with corpus / **420** without, app **17/17**, app_qt **23/23**, fixtures **OK**
  (46 documented hashes, 26 tracked `.mpp`), oracle provenance **OK (8)**,
  parity **10/10** plus the colour-only **1/0**.  Logs
  `build/w27/close-fresh-gates.log` (and the core re-run at its end).
* **CI**: `wave27` **9323db2** green on qt-shell (Linux/Windows/macOS; run
  40064777633), ci (Windows/Linux; 40064779873) and app-macos (40064782094),
  first dispatch, no re-run.
* **Privacy**: the line-based scan reports **0** on HEAD and **0** commit
  messages on `wave26..HEAD`; the whitespace-normalised `guard_names.py` is
  **0** on `c632c8f..HEAD` **and on the whole `main..wave27` ancestry (137
  commits)**; the `client-string-<8 hex>` grep has no matches.  The scrubbed
  ancestry adds no name.  A coverage gap found at this close: the 156-name
  list is built from live rows, so a deleted row's name printed by a
  corpus-backed reader warning into the local gate log is not in it; nothing
  tracked quotes it and `build/` is local-only (the warning and the scanner are
  a wave-28 fix, listed).  Residuals: the **13 unsalted FNV-1a name hashes
  removed by W27-3.5 remain in the older wave-26 commits** (`912b3fa`..
  `c7283d7`), and `tests/data/project_a_golden.tsv` still carries unsalted
  16-hex name hashes (the reader's oracle).  The brief forbids rewriting
  history; both are Tim's decision.  **Run BOTH scans** (line-based and
  normalised).
* **Still open** (wave27-complete CLOSE "Found, not fixed"): the stored sheet
  column widths, PROJECT_A's yellow per-row fill, bar-label coverage, the dark
  themes' light sheet/chart header bands and fixed ribbon ink, the
  menu-bar/QAT/status-bar artwork, the display-only Task Form, the undecoded
  stored combination view, the White theme, six dialogs still disabled (Text
  Styles, Gridlines, Bar Styles, Page Setup, Visual Reports, Compare Projects),
  the engines with no writer (actuals, project move, baselines, WBS, calendars),
  the wave-26 review's remaining Mediums (the ribbon check-box accessible-name
  sync, the Task Information validate-then-commit ordering, the Options
  new-task mode not reaching `insert_task()`, the 0xFFFF stored point size
  reaching Qt, multi-row Unlink), this wave's review (the latent percent
  pending-insert refusal; Low: the Unlink test comment, the SKIP-on-missing
  blocks, the operator path inside the new fixtures, `mpp_diff.py`'s
  ten-offset truncation), the name-scan coverage gap above (skipped-row names;
  print the uid/row only), two process items (five G3 messages omit their gate
  log's name; fixture commits also carry the hash registry and provenance),
  and the repo-wide CRLF grandfathered files.
* **Branch state**: `main` = **fcb1c10** = `origin/main`.  `wave23`
  (`e1d40b8` = `origin/wave23`), `wave24` (`6dbf126`), `wave25` (`c012813`),
  `wave26` (`1299cc7` = `origin/wave26`) and `wave27` (the CLOSE commit after
  `9323db2` = `origin/wave27`) are closed and **not merged**.  No remote
  `wave24`/`wave25`.
* Engine stays **7/8** (`constraint-mso` deliberately left failing;
  `mppwrite::save_plan` is test-only).
* Parity still deferred/remaining: the "Still open" bullet above; the wave-25
  deferred list (W25-12..W25-21) is closed by wave 26 except for those residuals.

## Reading order

**Coordinator starting wave 28** (≈5 files):

1. this file;
2. `docs/handoff/wave27-complete.md` — the gate: G0..G3 + CLOSE, the fresh-tree
   gates, the two privacy scans, the review passes, the CI runs, the re-scored
   16 decisions, "Found, not fixed";
3. `docs/handoff/wave27-brief.md` (the work order); a verifier's
   `docs/handoff/wave27-verification/` may follow this close;
4. `docs/ui-parity-spec.md` — the binding 16-decision UI contract;
5. `docs/ui-reference-set.md` — the local captures and palettes (git-ignored);
6. `memory/coordinator.md` — the compact current memory.

**Lane agent** (≈4 files):

1. this file;
2. `docs/handoff/wave27-brief.md`;
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
  `python scripts/check_fixtures.py` before every commit (it also checks the
  scenario oracle provenance).  Nothing under `tests/data` is written by a lane.
  New fixtures are built through Project on a NEUTRAL path (`C:/w27`) with
  neutral Author/Manager/Company/UserName.
* **Displayed rows resolve through their uid** (wave 25): `app::print_row_list`
  takes RecordSet rows; never index `plan.tasks` with a RecordSet row; uid 0 is
  the project row and refuses task edits.
* **Saveable structural edits** (waves 26-27): Priority is measured against
  Project and enabled; PercentComplete is NOT enabled — the five % buttons are
  disabled and no UI path may queue `set_percent` until wave 28's actuals
  writer.  A percent edit needs the loaded plan at `push()`; step 5a refuses one
  stale percent edit instead of failing the save.  Every enabled new edit path
  needs a save-and-reopen test AND the standing-rule-1 Project measurement.
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
  with LF line endings (173 grandfathered CRLF files remain; do not add more).
* **CI:** one dispatch per milestone; red runs are triaged, fixed and dispatched
  again with the reason recorded.  `wave27`'s close runs are recorded in
  wave27-complete.
* **Commits:** one finding id per commit; honest numbers; a gate number must
  belong to the tree it is written in and name its log under `build/w27/`; no
  test claimed unless run.  A known gap is an expected failure **by name**, never
  an assertion of the wrong output.

## Start a session with this (paste)

```
Read docs/START-HERE.md and docs/handoff/wave27-complete.md.  Wave 27 is closed
on branch wave27 (pushed; CI green on all three OSs; not merged).  Continue
wave27-complete's CLOSE "Found, not fixed" list, or start wave 28's plan
(docs/handoff/plan-waves27-30.md): the actuals writer and the % buttons, outline
editing, the Options new-task mode, the editable Task Form.  Keep every gate in
wave27-complete CLOSE green before each commit and run BOTH the line-based and
the whitespace-normalised client-name scans.
```
