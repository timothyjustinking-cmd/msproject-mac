# START HERE — MS Project → native port

_Updated 2026-10-30, wave 33 in progress on branch `wave33` (cut from
`main` = `17f5782`).  Keep this file current at every wave close; it is the
only file a new session must read first._

## The 30-second state

* **The merge state is the one fact the old entry records got wrong.**  Waves
  23-32 were merged into `main` on 27 October 2026 as **`17f5782`** after an
  operator-approved `git filter-branch` rewrite (the pre-push guard refused
  the range: seven fixtures' original blobs carried the operator's save path
  or name).  **GitHub holds only `main`**; `origin/wave23`..`origin/wave32`
  were deleted after the merge.  The local branches `wave23`..`wave31` still
  point at pre-rewrite commits and are superseded history; `wave32` points at
  `17f5782` itself.
* **Every commit id quoted in the wave 23-32 records is a PRE-rewrite id**,
  including the CI run ids' commits and the "final code tree" ids.  To map
  one, look its left-hand column up in
  `docs/handoff/wave32-verification/rewrite-sha-map.txt` (329 pairs; left =
  old, right = current):

  ```
  git show 17f5782:docs/handoff/wave32-verification/rewrite-sha-map.txt | grep <old-id>
  git show <right-hand-id>
  ```

  For example the wave-32 tip `92baa32` is now `d7d0088`, and its final code
  tree `7727760` is now `6a4dcc2`.
* **Wave 32 is closed and merged.**  G0 (the wave-31 verifier's three Highs,
  refuse-first) + CLOSE; the independent review's High and four required
  Mediums fixed in W32-C4..W32-C8; record `docs/handoff/wave32-complete.md`.
  Two verifier Mediums remained: **R32-1** (Task Information could leave a
  custom-field edit queued behind a percent refusal) is **closed by wave 33
  W33-1.5**; **R32-2** (the macOS shell's filter range path acts in file
  order) is **still open**.
* **Wave 33 is the 29 October audit's fix-only work order**
  (`docs/handoff/wave33-brief.md`, from
  `docs/handoff/assessment-2026-10-29.md` sections 7-8).  Groups G0-G3 are
  **DONE** (record `docs/handoff/wave33-complete.md`):
  * **G0** — atomic `.mpp` saves through one helper that writes beside the
    target, re-reads and self-checks, then renames; the non-structural save
    route adopts its written file as the open document.
  * **G1** — refusals and disables: Delete Task on a summary, Insert Task
    cloning a progressed last row, formula SET outside Text/Number, formula
    CLEAR, a second pending formula op, Task Information/Task Fields decided
    on a trial copy, the shown table's date format, and FAT-slack files open
    read-only with a reason.
  * **G2** — writer DIFAT walk bound and visited set, Resource Fixed2Meta
    count bound, CFB v4 refused, fuzz targets for both container parsers and
    the MSPDI importer, and an `ubuntu-asan` job.
  * **G3** — the `app/` suite runs in CI on windows-msvc and ubuntu-gcc,
    `app-macos.yml` runs on push and derives its registration check from
    CMake, the save-pair mask is exactly the noisy bytes and the appended
    tail is compared, a missing committed fixture is a FAILURE, both app_qt
    counts are quoted, the backlog states the pin truth, the first CI ctest
    pass sets `MPP_NO_CORPUS=1`, and `qt_render_parity` is
    `qt_render_1920`.
* **G4 (records and privacy) is this group.**  It rewrites these records, the
  coordinator memory and INDEX; restores LF and adds the `.gitattributes` /
  CI line-ending gate; neutralises the operator's name in
  `tests/data/synthetic-base.mpp` and `analysis/test_project.mpp` in place;
  adds `scripts/check_markers.py` (tracked-file and commit-message marker
  guard in `ci.yml`); cleans the 69 tracked text files and the phase-script
  paths; and shares the oracle neutralisation helper.  CLOSE follows G4.
* **Fresh-tree gates at the G3 tree** (`f6bbb96`; log
  `build/w33/W33-G3-fresh-gates.log`): core `ctest -E plan_recalc` **55/55**;
  `plan_recalc` **512 / 266** (with / without corpus); engine selftest 53
  checks, suite 7 pass + `constraint-mso` expected; reader **1136 / 456**;
  M2 **332/332**, **268/268**, **64/64**; app **25/25**; app_qt **41/41**
  locally (**39** registered without `corpus/`); fixtures OK (**140**
  documented hashes, **174** tracked `.mpp`), paths OK (223), oracle
  provenance OK (8).
* **Privacy before G4**: `synthetic-base.mpp` and `analysis/test_project.mpp`
  carry the operator's name as Author/LastAuthor, and 69 tracked text files
  (15 `.md`, 5 `.json`, 49 `.py`) publish the name, the company folder or the
  Windows username; 49 `scripts/phase*.py` hard-code the old absolute root.
  W33-4.3 and W33-4.5 remove them.  The 13 unsalted name hashes in older
  history and `tests/data/project_a_golden.tsv` remain the operator's
  decision (Q3).  **Run BOTH name scans and the new marker check.**
* **Push blocker carried from G1 (disposition approved)**: commit `3b53b9d`
  added the FAT-slack fixture as imported `synthetic-base` bytes, and that
  intermediate blob carries the pre-wave privacy residual; `964594d`
  replaced the tip blob, but the local pre-push guard (`guard_names.py`)
  checks every blob in the range and refuses `3b53b9d`.  The operator
  approved a coordinator replay of the unpushed range on 30 October (brief,
  05:50 CLOSE note); CLOSE does not push -- the coordinator replays, pushes
  and dispatches both workflows after CLOSE.  The tip tree is clean.
* **Engine stays 7/8** (`constraint-mso` deliberately expected-failing;
  `mppwrite::save_plan` is test-only).
* **Branch state**: `main` = **`17f5782`** = `origin/main`.  `wave33` is this
  branch.  The unmerged pre-rewrite branches are local leftovers; their
  content is on `main` under the mapped ids.

## Still open

* **The wave-32 verifier's two Mediums** (`wave32-verification/review.md`):
  **R32-1** (Task Information could leave a custom-field edit queued behind a
  refused percent push) is **closed by W33-1.5**; **R32-2** (the macOS
  shell's filter range path still selects, copies, pastes and indents in file
  order) is **still open** — since wave 32 no such sheet edit reaches a save,
  but it remains a Medium for the shell.
* **The carried review queues**: R31-5..R31-12 and the wave-32 review's
  L1..L6 (test strength, the filter wording) are queued in
  `plan-waves27-30.md` and `wave32-complete.md` with directions; the four core
  review Lows C1/C2/S1/S2 and the wave-30/29 queues stand.
* **The push blocker before CLOSE** (disposition approved): the intermediate
  blob added by `3b53b9d` carries the pre-wave privacy residual, so the local
  `guard_names.py` pre-push guard refuses the range even though `964594d`
  cleaned the tip.  The operator approved the coordinator replay on 30
  October (brief, 05:50 CLOSE note); push and CI: by the coordinator after
  the replay.
* **Privacy residuals the operator owns**: the pre-scrub public history and
  CI artifacts (Q1), the unsalted name hashes in older history and
  `tests/data/project_a_golden.tsv` (Q3), and `6056f15`'s commit message plus
  the two local pre-push backup branches (Q4).
* **The after-run list**: Team Planner, Network Diagram, Calendar and Report
  views, the `constraint-mso` engine case, macOS launch/notarisation, and the
  hashed task names.

## Reading order

**Coordinator starting the next group or wave** (≈5 files):

1. this file;
2. `docs/handoff/wave33-complete.md` — the current record: G0-G3 detail,
   per-commit and fresh-tree gates, "Found, not fixed";
3. `docs/handoff/wave33-brief.md` — the work order;
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
  expect 0 hits; also `python scripts/check_markers.py <range>` (wave 33 G4)
  and `git grep -nE "client-string-[0-9a-f]{8}"`, expect none.
* **Fixtures:** never open a committed fixture in place (Project rewrites files
  it opens); oracle scripts use `oracle_lock.working_copy()`.  Run
  `python scripts/check_fixtures.py` before every commit (it checks the
  documented hashes, the property sets of every tracked `.mpp`, the scenario
  oracle provenance and an embedded drive/user-profile path).  Nothing under
  `tests/data` is written by a lane.  New fixtures are built through Project
  on a NEUTRAL path (`C:/wNN`) with neutral Author/Manager/Company/UserName/
  Title.
* **Displayed rows resolve through their uid** (wave 25): `app::print_row_list`
  takes RecordSet rows; never index `plan.tasks` with a RecordSet row; uid 0 is
  the project row and refuses task edits.
* **Saveable structural edits** (waves 26-30): Priority, PercentComplete, Mark
  on Track, Update Project, the Task Form percent field, outline Indent/
  Outdent, the new-task mode and Manually Schedule/Mode are measured against
  Project and enabled.  A percent edit and a date update need the loaded plan
  at `push()`.  The file-side shape harness refuses the shapes no Project pair
  measured (W30-C5).  Every enabled new edit path needs a save-and-reopen test
  AND the standing-rule-1 Project measurement.  Split Task is measured but NOT
  enabled.  The wave-29 dialogs are session-only over the open view; no Style
  dialog writes stored style records.  Waves 31-33 added NO writer: Sort,
  Outline, Tables and Zoom are session state; wave 32 refuses or disables; wave
  33 is fix-only (atomic save, refusals, bounds, tests, CI, records).
* **`plan_recalc`** never rewrites the golden by default; refreshing needs the
  exact value `PLAN_RECALC_UPDATE_GOLDEN=1`, and the local/CI gate runs
  `ctest -E plan_recalc` plus the corpus-free binary/step.
* **Oracle/COM** is coordinator-only, under `scripts/oracle_lock.py`, with
  `FileClose(0)` and the post-close file as the only evidence.
* **`corpus/`** and `decompiled/` are git-ignored: nothing from them is
  committed, and the operator's captures are never quoted for client data.
* **Builds:** serial only; local Qt 6.5.3 in
  `build/Qt/6.5.3/6.5.3/msvc2019_64`.  Qt ctest prepends its own DLL directory.
  Transient Dropbox/MSBuild file locks are common; retry the build rather than
  changing CMake.  Text files are UTF-8 with LF line endings; from wave 33 G4
  `.gitattributes` pins `*.md`, `*.py`, `*.yml`, `*.cpp` and `*.h` to LF and
  CI fails on a CR in those types.  The older CRLF `.tsv`/`.json`/`.txt`
  fixtures stay as committed.
* **CI:** one dispatch per milestone; red runs are triaged, fixed and dispatched
  again with the reason recorded.  Wave 33's push and both dispatches belong to
  the coordinator after CLOSE and after the approved replay; the record carries
  the run ids and the final status once they exist.
* **Commits:** one finding id per commit; honest numbers; a gate number must
  belong to the tree it is written in and name its log under `build/w33/`; no
  test claimed unless run.  A known gap is an expected failure **by name**,
  never an assertion of the wrong output.  The committed
  `scripts/check_command_backlog.py` (ctest `qt_command_backlog`) keeps the
  generated backlog and the ribbon pin in step both ways.

## Start a session with this (paste)

```
Read docs/START-HERE.md and docs/handoff/wave33-complete.md.  Waves 23-32 are
merged into main as 17f5782 (27 Oct 2026, after a history rewrite); every id in
the wave 23-32 records is pre-rewrite and maps through
docs/handoff/wave32-verification/rewrite-sha-map.txt.  Wave 33 is the 29 Oct
audit's fix-only work order on branch wave33: G0-G3 are done (atomic save and
save adoption; the refusal set; parser bounds, CFB v4 and ASan fuzzing; the
app suite in CI, the save-pair mask, fixture SKIPs and honest numbers); G4
(records and privacy) is in progress or done; CLOSE owns the fresh-tree gates
and the wave-33 diff review, and the coordinator pushes and dispatches CI
after CLOSE and after the approved replay.  Keep every gate in
wave33-complete green before each commit and run BOTH name scans and
scripts/check_markers.py.
```
