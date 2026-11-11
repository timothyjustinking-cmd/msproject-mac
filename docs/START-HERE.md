# START HERE — MS Project → native port

_Updated 2026-11-10, wave 35f (fix-only) in progress on branch `wave35f` (cut from `main` at
`47665e1`).  **Waves 34 to 35e are MERGED into `main`** (`5e1c8ea`, a fast-forward from
`4fd9b90`: CI green on all seven jobs at `c0c81e5`, no Critical or High open, 4(e) passes; see
`docs/handoff/status-2026-11-10.md`).  The "NOT merged" and "NOT pushed" sentences in the wave 34
to 35d bullets below are history, written at each wave's close.  Nothing of wave 35f is pushed.
Keep this file current at every wave close; it is the only file a new session must read first._

## The 30-second state

* **Wave 35f (fix-only) is CLOSED on branch `wave35f`** (`WAVE-35f-COMPLETE`, fresh-tree gates at
  `135d253` all green in `build/w35f/W35f-CLOSE-fresh-gates.log`; the coordinator's push, CI,
  mutations and review are next, and no id below is closed until that review; the list below is
  the queue as the wave opened).  Originally: **in progress on branch `wave35f`** (cut from `main` at `47665e1`;
  `docs/handoff/wave35f-brief.md`, `docs/handoff/wave35f-complete.md`, builder Claude Sonnet 5.5;
  nothing is pushed).  It works the open queue wave 35e left, which is
  `wave35e-verification/coord-gates-close.md` (the coordinator's list; this file does not
  shorten it).  **No Critical or High is open.**  The queue:
  * **Mediums:** R35eG1-1 (fixed in source by W35f-0.1 `fd457b4`), R35eG2-1 (W35f-0.2 `08b83f9`),
    R35eG1-3 (W35f-0.3 `1a2436e`) and R35eG1-2 (W35f-0.4, these records) are fixed in the builder's
    tree (G0) and were reviewed by `wave35f-verification/coord-gates-g0.md`.
  * **Lows:** R35eG0-1 to -3, R35eG1-4 to -6, R35eG2-2 to -10, R35eW-1 to -4 (G1 of wave 35f:
    shell, AppKit, Calculate seams and records) are fixed and were reviewed by
    `wave35f-verification/coord-gates-g1.md`; open from those reviews: R35fG0-* and R35fG1-*.
  * **Partly fixed, not closed:**
    * R35dG2-1: Fill Down's Calculate guard has no test route (wave 35e `ae4782a`).
    * R35cG0-2 (the plan reads Start word +104 only) and R35cG0-4 (the plan has no Props digest),
      both refused at the writer or the save.
    * R35cW-4: remainders R35dW-1 (fixed by W35e-0.1 `b8ee455`, closed by the coordinator's
      review) and R35dW-5 (the AppKit shell's 2.11 refusal, W35e-2.7 `e03da3e`: fixed in source;
      CI's `macos-appkit` job, green at `c0c81e5`, is its gate; to be closed by the coordinator).
      `app-macos.yml` (`macos-appkit`) runs on push, not on dispatch, so the merge check "CI green
      on three OSs" must include that job's run for `wave35f`; the same job gates the AppKit
      mirror of the 2.6 refusals (W35f-1.10, R35eG2-3).
    * The four wave 35b ids that wave 35c recorded partly fixed: **R35bW-1** (remainders R35cG1-2,
      through R35dG0-1 closed by W35e-0.1, and R35cG1-3, closed by W35d-1.1), **R35bW-2** (R35cG1-1,
      closed by W35d-0.4), **R35bG2-2** (R35cG2-1, closed by W35d-1.3) and **R35bG3-1** (R35cG1-4,
      through R35dG1-1 fixed by W35e-1.1, whose follow-ons are R35eG1-1 and R35eG1-3 above).  Their
      remainders are closed or fixed in source; none is closed by this file, only the coordinator's
      review closes an id.
    * R35bG1-3 and R35bG2-6, recorded as defence in depth.
  * **Still open and unassigned:** R35bG0-9 and R35bG1-10 (the untested guards in the wave 35b
    `gates-g0.md` and `gates-g1.md`) and R35bG2-9 (watch: the intermittent
    `qt_assign_resources_gate`).
  * **CI:** R35cW-CI1 and R35cW-CI2 were fixed in source and proved by the green wave 35d run
    (`3d22c39`); wave 35e's run (`c0c81e5`) is green on all seven jobs.  Wave 35f's own CI is the
    coordinator's after CLOSE.
  * **Closed by the coordinator:** R35dG0-1, with R35dW-1 (the last High), by the wave 35e G0
    review; the wave 35d ids not named above are not in the coordinator's open queue
    (`wave35e-verification/coord-gates-close.md`).  R35bG0-1 and R35bG0-2 (the two Calculate
    Project Highs) were fixed in wave 35c (`7d923dc`, `743ad8b`).

* **Wave 35e (fix-only) is closed and merged** (branch `wave35e`, cut from `wave35d` at
  `2ea87e5`; `docs/handoff/wave35e-brief.md`, `docs/handoff/wave35e-complete.md`,
  `WAVE-35e-COMPLETE`, builder Claude Sonnet 5.5; G1's commits `39c8e37` (1.1), `1da3997` (1.2),
  `02f1900` (1.3) and `f244d02` (1.4); fresh-tree gates at `2dbb5d9` in
  `build/w35e/W35e-CLOSE-fresh-gates.log`, all green).  The coordinator's reviews found 0 Critical,
  0 High, four Mediums and the Lows, which are the wave 35f queue above.

* **Wave 35d (fix-only) is closed on branch `wave35d`** (cut from `wave35c`;
  `docs/handoff/wave35d-brief.md`, `docs/handoff/wave35d-complete.md`, `WAVE-35d-COMPLETE`,
  builder Claude Sonnet 5.5; nothing is pushed).  It worked the queue wave 35c left
  (`wave35c-verification/coord-gates-close.md`, Highs first); the fresh-tree gates are in the
  CLOSE section of the record (`build/w35d/W35d-CLOSE-fresh-gates.log`).  The coordinator pushes
  `wave35d`, dispatches `qt.yml` and `ci.yml` (R35cW-CI1 and R35cW-CI2 are gated only there) and
  runs mutations and an independent review of `wave35c..wave35d`.  State at CLOSE:
  * **Highs:** R35cG0-1 (fixed `abc8a60`), R35cG1-1 (fixed `61a451b`) and R35cG1-2 (**partly
    fixed** `6458203`: remainder R35dG0-1, the one High the review of 35d opened; see wave 35e).
  * **CI:** R35cW-CI1 (`23de6e2`) and R35cW-CI2 (`171831b`) are fixed in source; CI on GCC 13 and
    macOS is the only proof of each and its gate.
  * **Mediums:** fixed R35cG1-3 (`e967858`), R35cG2-1 (`41f7a2c`) and R35cW-2 (`454521d`);
    **partly fixed** R35cG1-4 (`e2eca38`, remainder R35dG1-1), R35cG0-3 (`6552f11`, remainder
    R35dG1-2) and R35cW-1 (`4410935`, remainder R35dG1-3 and R35dW-2), with R35cG2-8 fixed.
  * **G2 (the Lows and the Calculate Medium):** fixed R35cG0-5 (`ec2429c`), R35cG0-6
    (`7d908c6`), R35cG1-5 to -7 (`c250aef`, `f9771b6`, `a85d8b3`), R35cG2-2 to -7 (`fc12254`,
    `f3c38d9`, `bb4a398`, `e415fe2`, `51c474b`, `6652ded`) and R35cW-3 and -5 (`6f5cc18`,
    `492f10c`); R35cW-4 (`4dcfcd7`) is **partly fixed** (remainders R35dW-1 and R35dW-5).
    **Partly fixed, still open:** R35cG0-2 (the plan reads Start word
    +104 only; the writer half is closed) and R35cG0-4 (the plan has no Props digest, a recorded
    gap); both are refused at the writer or the save.
  * Every 35d fix still awaits an independent review, so waves 34 to 35d stay NOT merged.

* **Wave 35c (fix-only) is closed on branch `wave35c`** (cut from `wave35b` at `2a97272`;
  `docs/handoff/wave35c-complete.md`, `WAVE-35c-COMPLETE`, builder Claude Sonnet 5.5).  It fixed
  both Calculate Project Highs (R35bG0-1, R35bG0-2), the GCC 13 `dangling-reference` break
  (R35bW-CI1, **CI is its only proof**) and the wave 35b Mediums and Lows, adding no writer;
  partly fixed: R35bG1-3 and R35bG2-6, and R35bW-1, R35bW-2, R35bG2-2 and R35bG3-1 (remainders
  R35cG1-2 with R35cG1-3, R35cG1-1, R35cG2-1 and R35cG1-4; see the wave 35f line above and
  `wave35c-complete.md`).  Still open and unassigned: R35bG0-9 and R35bG1-10.
  Fresh-tree gates at `319d187` (`build/w35c/W35c-CLOSE-fresh-gates.log`) are all green,
  `guard_names` included.  Nothing is pushed: the coordinator pushes `wave35c`, dispatches
  `qt.yml` and `ci.yml`, runs mutations and an independent review of `wave35b..wave35c`.
  Waves 34 to 35c are NOT merged until CI is green on all three OSs and no Critical or High is
  open.

* **Wave 35b (fix-only) is closed on branch `wave35b`** (cut from `wave35`;
  `docs/handoff/wave35b-complete.md`, `WAVE-35b-COMPLETE`, builder Claude Sonnet 5.5; records
  and reviews in `docs/handoff/wave35b-verification/`).  It fixed the wave 35 Highs (Calculate
  Project deadline, pending-recalculation, leaf-date and parameter refusals; Show Level, Sort
  and Show Level state pins; port-saved assignment removal; the macOS resource pop-ups and the
  Qt menu) and the Mediums and Lows, adding no writer.  Fresh-tree gates at `90d8b4e`
  (`build/w35b/W35b-CLOSE-fresh-gates.log`) are green except `guard_names`, which still names
  the two `9fa599b` blobs.  **Not mergeable at its close:** the per-group reviews left two Highs
  on Calculate Project (R35bG0-1: the Props project start is not read; R35bG0-2: the Props
  calculation options are not pinned); **both were fixed in wave 35c** (`7d923dc`, `743ad8b`)
  and are no longer open.  Nothing is pushed; the coordinator's part is the `9fa599b` decision, push, both CI
  dispatches (CI is the only gate for `app/main.mm` and for macos-qt, W35b-0.1), an
  independent review of `wave35..wave35b`, and the 4(e) Project reopen check.  Wave 35c closed the rest of that queue (the G4 and CLOSE review ids, complete in `wave35b-verification/coord-gates-close.md`) except: R35bG0-9 and R35bG1-10 (the untested guards recorded in `gates-g0.md` and `gates-g1.md`; assigned to no item), R35bG2-9 (watch: the intermittent `qt_assign_resources_gate`, green on macOS) and R35bW-3 (answered by the coordinator's map); R35bG1-3 and R35bG2-6 are partly fixed and recorded as defence in depth, as are R35bW-1, R35bW-2, R35bG2-2 and R35bG3-1 (remainders R35cG1-2 with R35cG1-3, R35cG1-1, R35cG2-1 and R35cG1-4; see the wave 35f line at the top).  What 35c left open is in the wave 35f line at the top.

* **Wave 35 (the scheduler save path, view-state persistence, the wave-34 deferrals, the macOS
  shell) is closed on branch `wave35`** (`docs/handoff/wave35-complete.md`, builder Claude
  Sonnet 5.5).  **Built and enabled, each for its measured shape only:** G0 the carry-over
  fixes (resource calendar checks for Assign Resources, the writer-width refusals RC-W1,
  RC-W2, RC-W6..W8, CI-4); G1 `mppwrite::save_calculate` and Calculate Project for the
  unlinked two-leaf shape; G2 Sort (one Name or Duration key on the measured five-task shape)
  and Show Level 1 and 2 on the two measured outlines; G3 Bold off (view writer only); G4 the
  macOS shell wired to the shared builders for rename, Assign Resources, Bold, Define Code
  and Calculate Project.  **Deferred with evidence:** linked, lag and constraint Calculate
  Project, the applied table, zoom, Insert Summary / Milestone, Set Baseline 0, Notes, the
  other font controls, Renumber, and removal from a port-saved file.  Fresh-tree gates at
  `4e3ba80` (`build/w35/W35-CLOSE-fresh-gates.log`) are green except `guard_names`.
* **Wave 35 is NOT mergeable.**  The per-group verifications and the two CLOSE reviews
  (`docs/handoff/wave35-verification/`) leave **High** findings open: Calculate Project
  (R35G1-1..3, RW35-W2: deadline, any pending recalculation, unpinned parameters, leaf dates
  unchecked), view state (R35G2-1..4, RW35-R2: Show Level and Sort pin too little and trust the
  caller's rows and unsaved renames) and removal from a port-saved file (R35G3-1, RW35-W1).
  They are queued as the fix-only `wave35b` in `plan-waves27-30.md`.  The one CI-class break
  the CLOSE review found, RW35-R1 (a `-Wswitch` hole in `app_qt/command_status.h`), is closed
  by `513de29`.  Also open: `9fa599b` added two fixtures whose LastAuthor held the operator's
  real name (fixed forward in `9564ddd`); `guard_names main..HEAD` reports those two blobs
  until the commit is rewritten, which is the operator's or the coordinator's call.
* **Wave 34b (fix-only: no writer added, no refusal lifted, no Project pair
  measured) is closed on branch `wave34b`** (`docs/handoff/wave34b-complete.md`).
  It closed both wave-34 Highs (R34G3-1 / RC-R1: Assign Resources and its removal
  now refuse every shape but the four measured pairs; R34G3-2: the identity
  references are compared, not masked), the other writer Highs (Add Resources
  template row, Font Bold applied view, Define WBS Code IDs and nesting), the CI
  compile failures (unused functions for gcc and clang; the gcc 15 array-bounds
  warning in `core/gantt`) and the Mediums R34G3-3, R34G3-4 / R34G4-9, RC-R3 /
  R34G4-8, R34G4-6 and R34G4-7.  Fresh-tree gates at `980ce9d`
  (`build/w34b/close-gates/`) are green.  The CLOSE review (two passes over
  `git diff wave34..wave34b`) found **0 Critical, 0 High** and three Mediums
  (RW34b-1..3, queued in `plan-waves27-30.md`).  The gcc, clang, AppleClang and
  asan jobs cannot run here: the coordinator's CI run is their gate.
* **Wave 34 (parity: decode Project's save shapes, build the shared writers, enable
  the measured commands) is closed on branch `wave34`** (cut from `main` = `4fd9b90`,
  the merge of waves 33 and 33b; `WAVE-34-COMPLETE` is in
  `docs/handoff/wave34-complete.md`).  **It is NOT mergeable**: the CLOSE review
  (`docs/handoff/wave34-verification/close-review.md`, with the G3 review
  `review-g3.md`) found 0 Critical and **2 High** -- **R34G3-1 / RC-R1** (the
  assignment writer and the Assign Resources UI accept any whole-hour work amount
  although every pair is a 40-hour step) and **R34G3-2** (the assignment test masks
  the task and resource identity references).  A fix-only **`wave34b`** closes them
  before any merge.  Nothing on `wave34` is pushed; push and both CI dispatches are
  the coordinator's.
* **What wave 34 built** (decode notes indexed in `spec/README.md`; pairs in
  `spec/writers/evidence/`): G0 the decoding lane (eight decode notes, 23 pair
  reports, synthetic pairs for the five corpus-only paths); G1 the VarMeta entry
  writer; G2 the `214` view-stream writer and Props record update; G3 the
  assignment growth writer, the resource row and calendar row writer, the
  DocumentSummaryInformation writer with an independent reader, reader gaps, and
  W34-B5 (the `name` column dropped from `tests/data/project_a_golden.tsv`; the old
  hashes remain in public history, the purge request is the operator's).
* **What wave 34 enabled (G4)**, each for its measured shape only: Resource
  Information rename, Add Resources (one work resource, default rates), Assign
  Resources (first assignment and its removal from a Project-saved file), Font Bold
  (one cell), WBS Define Code (prefix W, three flat tasks).  W34-4.0 refused up
  front the five paths whose port save still differed from Project's (Insert Task,
  Delete Task, link ADD, custom-field SET and CLEAR).  **Deferred with evidence:**
  Insert Summary / Insert Milestone (15 streams differ), Set Baseline 0 (94 new var
  blocks, 26 undecoded), Resource and Task Notes (the RichEdit generator string).
  The backlog now lists 114 ribbon commands, 64 disabled counting split menu items
  (the pin: 51 enabled / 63 disabled controls).
* **Stopped after G4** by the brief's stop rule (the 12-hour point passed during
  G4).  **G5 (scheduler save path) and G6 (view-state persistence) are wave 35**,
  queued in `plan-waves27-30.md`.
* **Builder**: G0-G2 DeepSeek (30 October); G3-CLOSE Claude Sonnet 5.5 through
  `tools/deepseek/sonnet-claude.cmd` (`run-wave.ps1 -Builder sonnet`) after the
  DeepSeek balance ran out.
* **The merge state of waves 23-33b**: waves 23-32 were merged into `main` on
  27 October 2026 as `17f5782` after an operator-approved `git filter-branch`
  rewrite; every commit id quoted in the wave 23-32 records is a PRE-rewrite id
  (map one through `docs/handoff/wave32-verification/rewrite-sha-map.txt`, left =
  old, right = current).  Waves 33 and 33b are merged and green on all three
  platforms (`main` = `origin/main` = `4fd9b90`).
* **Fresh-tree gates at the final code tree** are in the CLOSE section of
  `wave34-complete.md` (log `build/w34/W34-CLOSE-fresh-gates.log`, all three trees
  configured from scratch with warnings as errors).  The asan job and the CI
  dispatches have not run for wave 34.
* **Privacy**: `python -I build/audit-w22/guard_names.py main..HEAD` and
  `python scripts/check_markers.py --range main..HEAD` are 0 hits at CLOSE; the
  unsalted name hashes in older public history (Q3 and B-1) remain the operator's.
  **Run BOTH name scans and the marker check.**
* **Engine stays 7/8** (`constraint-mso` deliberately expected-failing;
  `mppwrite::save_plan` is still test-only -- it is G5).

## Still open

* **Before any merge of wave 34 / 34b:** push `wave34b`, dispatch `qt.yml` and
  `ci.yml`, triage any red job, and run the brief section 4(e) Project-reopen check
  of the five enabled saves (COM, coordinator-only).
* **The wave-34 CLOSE review** (`docs/handoff/wave34-verification/close-review.md`;
  the two Highs are closed by `wave34b`, see its dated errata):
  the two Highs R34G3-1 / RC-R1 and R34G3-2 and the Mediums R34G3-3, R34G3-4,
  RC-W5 and RC-R3 go to `wave34b`; the other Mediums and the Lows are queued in
  `plan-waves27-30.md` ("Queued from wave 34") with G5 and G6 as wave 35.  Also
  open before a merge: the brief section 4 (e) Project-reopens-the-port-save
  check for the five enabled saves (COM, coordinator-only), and the first CI run
  of wave 34 (including `ubuntu-asan`).
* **The macOS shell** wires only Add Resources of the wave-34 items (RC-R4,
  RC-R5): a feature gap, not a safety gap, since the builders are shared.
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
2. `docs/handoff/wave35-complete.md` — the current record: G0-G4 and CLOSE,
   the fresh-tree gates, the open Highs, "Found, not fixed"; with
   `docs/handoff/wave35-verification/` (the per-group and CLOSE reviews) and
   `spec/README.md` (the writer decode notes);
3. `docs/handoff/wave35-brief.md` — the work order (and `wave34-complete.md` /
   `wave34b-complete.md` for the code it builds on);
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
  in the save hook, and adds no writer.  **Wave 34** added writers and enabled
  five commands for their measured shapes only (Resource Information rename, Add
  Resources, Assign Resources, Font Bold on one cell, WBS Define Code) and
  refused up front the five paths whose port save differs from Project's
  (Insert Task, Delete Task, link ADD, custom-field SET and CLEAR); see
  `spec/README.md`.  Assign Resources is measured at 40 hours only and its
  refusal of every other amount is a `wave34b` item.
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
Read docs/START-HERE.md, docs/handoff/standing-rules.md and docs/handoff/wave35f-brief.md.
Waves 23-32 are merged into main as 17f5782 (ids in the wave 23-32 records are pre-rewrite;
map through docs/handoff/wave32-verification/rewrite-sha-map.txt); waves 33 and 33b, then waves
34 to 35e, are merged (main = 5e1c8ea at the wave 35e merge; CI green on all seven jobs, no
Critical or High open, 4(e) passes).  The next step is the fix-only wave35f on branch wave35f
(its queue is docs/handoff/wave35e-verification/coord-gates-close.md: four Mediums and the Lows;
see "The 30-second state" above and docs/handoff/plan-waves27-30.md, "Queued from wave 35e").
Keep every gate green before each commit and run BOTH name scans and scripts/check_markers.py.
```
