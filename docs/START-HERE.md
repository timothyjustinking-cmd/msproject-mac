# START HERE — MS Project → native port

_Updated 2026-10-30, wave 33b complete on branch `wave33b` (cut from
`wave33` = `da5f4da`, itself cut from `main` = `17f5782`;
`WAVE-33b-COMPLETE` appended, not yet pushed -- the coordinator pushes and
dispatches both CI workflows after CLOSE).  Keep this file current at every
wave close; it is the only file a new session must read first._

## The 30-second state

* **The merge state is the one fact the old entry records got wrong.**  Waves
  23-32 were merged into `main` on 27 October 2026 as **`17f5782`** after an
  operator-approved `git filter-branch` rewrite (the pre-push guard refused
  the range: seven fixtures' original blobs carried the operator's save path
  or name).  **GitHub holds `main`, `wave33` and `wave33b`**; the old
  `origin/wave23`..`origin/wave32` branches were deleted after the merge.  The
  local branches `wave23`..`wave31` still point at pre-rewrite commits and are
  superseded history; `wave32` points at `17f5782`.
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
* **Wave 33 is closed and pushed after the approved replay** (`wave33` =
  `origin/wave33` = `da5f4da`; `WAVE-33-COMPLETE` in
  `docs/handoff/wave33-complete.md`).  The 29 October audit's fix-only work
  order: G0 atomic saves + save adoption; G1 the refusal set (summary delete,
  Insert Task clone, formula SET field kinds, formula CLEAR disabled, trial
  copies, shown-table edits, FAT-slack read-only, one pending formula); G2
  DIFAT/Fixed2Meta bounds, CFB v4 refused, fuzz targets and an `ubuntu-asan`
  job; G3 the `app/` suite in CI, the save-pair mask, fixture SKIPs, honest
  numbers; G4 records and privacy; CLOSE two independent reviews, the final
  gates and the START-HERE / INDEX / memory update.
* **Its post-replay CI was red in three jobs, fixed in wave 33b.**
  `ci` 40947728764 (da5f4da) failed ubuntu-gcc and ubuntu-asan; `app-macos`
  40947715383 failed macos-appkit; `qt-shell` 40947723497 was green.  The
  compile errors stopped each build at the first one, so 33b fixed them in
  order and re-dispatched: **CI-1** `tests/fixture_guard.h`'s `/*` glob inside
  a block comment; **CI-2** `tests/atomic_write.cpp`'s unused `join` (plus
  four orphaned `file_exists` helpers and the CAL/MPP forced-absent switch);
  **CI-3** `core/plan/plan.cpp`'s dead `manual_pin_failures` counter (found
  from the CI run of the 33b G0 commit, `ci` 40958074116).
* **Wave 33b is a fix-only follow-up** (`docs/handoff/wave33b-brief.md`;
  record `docs/handoff/wave33b-complete.md`).  G0 and G1 are DONE:
  * **W33b-0.1 (R33-1, High)** — W33-1.6 had carried the shown table index
    into the `214/CTable` date-format save, so File > Save could write the
    date-format byte of ANY table, while only table 0 has a Project save
    pair.  The Qt shell now refuses Table / Date Format with the decision-9
    reason before the dialog opens when the shown table is not the first;
    `Document::request_view_state_edits` returns a bool and refuses before
    queueing; `apply_view_state_edit` refuses the same as a save-hook
    warning, leaving `214/` byte-identical.  The macOS shell checks the
    request result (its view is always table 0).  The wave-33 record gains an
    erratum: its "added no writer" sentence was false.
  * **W33b-1.1 (R33-2, Medium)** — the formula-clear save hook refuses
    `Operation::clear` with the decision-9 reason before reading `114/Props`;
    the target is untouched and the save fails.  The unmeasured clear writer
    stays beneath the refusal for a future Project pair.
  * **W33b-1.2 (R33-3, Medium)** — the macOS Task Fields handler pushes onto
    a trial copy and moves it into `plan.edits` only when every edit is
    accepted, so a refused batch queues nothing (the Qt shell's rule).
* **Fresh-tree gates at the final code tree** (`5efc8a5`; the CLOSE record
  commit differs by documentation only; log
  `build/w33b/W33b-CLOSE-fresh-gates.log`, all three trees configured from
  scratch):
* **Fresh-tree gates at the final code tree** (`5efc8a5`; the CLOSE record
  commit differs by documentation only; log
  `build/w33b/W33b-CLOSE-fresh-gates.log`, all three trees configured from
  scratch): core `ctest -E plan_recalc` **55/55**; `plan_recalc`
  **512 / 266** (with / without corpus); engine selftest 53 checks, suite 7
  pass + `constraint-mso` expected, 0 unrecorded; reader **1136 / 456**; M2
  **332/332**, **268/268**, **64/64**; app **25/25** and **25/25** with
  `MPP_NO_CORPUS=1`; app_qt **41/41**; fixtures OK (**140** documented
  hashes, **174** tracked `.mpp`), paths OK (223), provenance OK (8); markers
  self-test 19 checks, worktree and range 0 hits over 1306 files; the
  client-name scan 0 files/0 hits and 0 message commits; `guard_names` exit
  0; backlog **114/114**, 0 mismatches; block-comment and unused-helper
  sweeps 0; line endings 0 CRLF; `git diff --check` clean (`main..HEAD` shows
  only the 13 known wave-33 CRLF-json hits, RW33-10).
* **Privacy after the replay**: the replay replaced the FAT-slack fixture
  blob and reworded the one commit message the marker guard named
  (`964594d`); `guard_names.py main..HEAD` exits 0 and
  `check_markers.py --range main..HEAD` is 0 hits over 1306 tracked files.
  The 13 unsalted name hashes in older history and
  `tests/data/project_a_golden.tsv` remain the operator's decision (Q3).
  **Run BOTH name scans and the marker check.**
* **Engine stays 7/8** (`constraint-mso` deliberately expected-failing;
  `mppwrite::save_plan` is test-only).
* **Branch state**: `main` = **`17f5782`** = `origin/main`; `wave33` =
  **`da5f4da`** = `origin/wave33`; `wave33b` is this branch, closed with
  `WAVE-33b-COMPLETE`.  `origin/wave33b` = **`6fdf3af`** (the coordinator's
  single pre-CLOSE push at the G0 gates commit, whose CI run found CI-3);
  the CI-3 fix and this record are local until the coordinator pushes after
  CLOSE.  The unmerged pre-rewrite branches are local leftovers; their
  content is on `main` under the mapped ids.

## Still open

* **R33-9** (the wave-33 record's three CI-dependent DONE items) closes only
  when the coordinator's post-replay CI run is green; the asan job has not
  run since CI-3 was fixed, so any further clang error or sanitizer report
  behind it is still unknown.
* **Wave-33b CLOSE-review Lows**: RW33b-1 (the save-hook backstop covers a
  date format but not a hand-built column replacement on a non-zero table)
  and RW33b-2 (the retained clear/flag heuristic beneath the refusal is no
  longer exercised) are queued in `plan-waves27-30.md`.
* **The wave-33 verification review's Lows**: R33-4 (the phase-2d script
  saves onto a tracked file without neutralising it), R33-5 and R33-6 (the
  gap matcher's "exact" wording and the unproven flag-byte mechanism), R33-7
  (the warning still points at the disabled Clear Formulas button) and R33-8
  (the atomic-write temp can truncate a file it did not create) are queued in
  `plan-waves27-30.md`; the wave-33b fixes closed R33-1..R33-3.
* **The wave-32 verifier's two Mediums** (`wave32-verification/review.md`):
  **R32-1** is **closed by W33-1.5**; **R32-2** (the macOS shell's filter
  range path still selects, copies, pastes and indents in file order) is
  **still open** — since wave 32 no such sheet edit reaches a save, but it
  remains a Medium for the shell.
* **The carried review queues**: R31-5..R31-12 and the wave-32 review's
  L1..L6 are queued in `plan-waves27-30.md` and `wave32-complete.md` with
  directions; the four core review Lows C1/C2/S1/S2 and the wave-30/29
  queues stand.  The wave-33 CLOSE review's ten Lows are RW33-1..RW33-10
  (RW33-3 is closed by W33b-1.2).
* **Privacy residuals the operator owns**: the pre-scrub public history and
  CI artifacts (Q1), the unsalted name hashes in older history and
  `tests/data/project_a_golden.tsv` (Q3), and `6056f15`'s commit message plus
  the local pre-push backup branches (Q4).
* **The after-run list**: Team Planner, Network Diagram, Calendar and Report
  views, the `constraint-mso` engine case, macOS launch/notarisation, and the
  hashed task names.

## Reading order

**Coordinator starting the next group or wave** (≈5 files):

1. this file;
2. `docs/handoff/wave33b-complete.md` — the current record: G0/G1 detail,
   CI-3, the CLOSE review, the fresh-tree gates, "Found, not fixed";
3. `docs/handoff/wave33b-brief.md` — the work order;
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
  33 is fix-only (atomic save, refusals, bounds, tests, CI, records).  **Wave
  33b fix-only erratum (W33b-0.1 / R33-1):** wave 33's W33-1.6 gave the
  date-format save the shown table index, so it could write a non-zero table's
  unmeasured byte; wave 33b refuses any table but the first at the request and
  in the save hook, and adds no writer.
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
  again with the reason recorded.  Wave 33's post-replay run was red (CI-1,
  CI-2 and, after those, CI-3); wave 33b fixed all three and left its own
  dispatch to the coordinator after CLOSE.  The record carries the run ids and
  the final status once they exist.
* **Commits:** one finding id per commit; honest numbers; a gate number must
  belong to the tree it is written in and name its log under `build/w33b/` for
  wave 33b (older waves: their own `build/wNN/`); no test claimed unless run.
  A known gap is an expected failure **by name**, never an assertion of the
  wrong output.  The committed `scripts/check_command_backlog.py` (ctest
  `qt_command_backlog`) keeps the generated backlog and the ribbon pin in step
  both ways.

## Start a session with this (paste)

```
Read docs/START-HERE.md and docs/handoff/wave33b-complete.md.  Waves 23-32 are
merged into main as 17f5782 (27 Oct 2026, after a history rewrite); every id in
the wave 23-32 records is pre-rewrite and maps through
docs/handoff/wave32-verification/rewrite-sha-map.txt.  Wave 33 is COMPLETE and
pushed after the approved replay (wave33 = origin/wave33 = da5f4da); its
post-replay CI was red in ubuntu-gcc, ubuntu-asan and macos-appkit.  Wave 33b
is the fix-only follow-up on branch wave33b, cut from wave33: it fixed the
review's High (the unmeasured table date format), its two Mediums (the
formula-clear save hook; the macOS Task Fields queue) and the CI compile
errors CI-1..CI-3, and WAVE-33b-COMPLETE is appended.  origin/wave33b is
6fdf3af (one pre-CLOSE push at the G0 gates commit, whose CI run found CI-3);
the CI-3 fix and the CLOSE record are local.  The coordinator's next step is
to push wave33b and dispatch both CI workflows, then record the run ids;
R33-9 closes when that run is green.  The wave is not merged until CI is green
and the review has no Critical/High (the CLOSE review has none).  Keep every
gate in wave33b-complete green before each commit and run BOTH name scans and
scripts/check_markers.py.
```
