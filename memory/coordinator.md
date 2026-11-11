# memory — coordinator (2026-11-10, wave 35f closed, G0 and G1 reviewed; waves 34 to 35e merged)

**Entry point:** `docs/START-HERE.md`.  Wave gate:
`docs/handoff/wave35-complete.md` (G0-G4 and CLOSE appended; `WAVE-35-COMPLETE`); reviews in
`docs/handoff/wave35-verification/`.
History: `memory/archive/coordinator-through-wave20-full.md`.

## Wave 35f state (closed on `wave35f`, cut from `main` at `47665e1`, NOT pushed) — 10 November 2026

Fix-only wave; brief `docs/handoff/wave35f-brief.md`, record `docs/handoff/wave35f-complete.md`.
Waves 34 to 35e are merged into `main` (`5e1c8ea`, CI green on all seven jobs at `c0c81e5`, 4(e)
passes), and **no Critical or High is open**.  It works the queue in
`docs/handoff/wave35e-verification/coord-gates-close.md`, which this file does not shorten:
**Mediums** R35eG1-1 (fixed in source by W35f-0.1 `fd457b4`), R35eG2-1 (W35f-0.2 `08b83f9`),
R35eG1-3 (W35f-0.3 `1a2436e`), R35eG1-2 (W35f-0.4, the records), reviewed by
`coord-gates-g0.md`; **Lows** R35eG0-1 to -3, R35eG1-4 to -6, R35eG2-2 to -10, R35eW-1 to -4 (G1,
fixed, reviewed by `coord-gates-g1.md`); open from those reviews: R35fG0-* and R35fG1-*.
**Partly fixed, not closed:** R35dG2-1 (Fill Down's guard has no test route), R35cG0-2 and
R35cG0-4 (plan side), R35cW-4 (remainders R35dW-1, closed by W35e-0.1, and R35dW-5, the AppKit
refusal, fixed in source with `macos-appkit` as its gate), and the four wave 35b ids R35bW-1
(remainders R35cG1-2 and R35cG1-3), R35bW-2 (R35cG1-1), R35bG2-2 (R35cG2-1) and R35bG3-1
(R35cG1-4, then R35dG1-1, whose follow-ons are R35eG1-1 and R35eG1-3).  R35bG0-9, R35bG1-10 and
R35bG2-9 stay open and unassigned.  Only the coordinator's review closes an id.

## Wave 35e state (closed on `wave35e`, `WAVE-35e-COMPLETE`, merged into `main`) — 10 November 2026

Fix-only wave cut from `wave35d` at `2ea87e5`; brief `docs/handoff/wave35e-brief.md`, record
`docs/handoff/wave35e-complete.md`.  The coordinator's reviews closed **R35dG0-1** (the last High,
with R35dW-1) and left the wave 35f queue above (four Mediums and the Lows).  G1's items 1.1
(R35dG1-1 `39c8e37`), 1.2 (R35dG1-2 with R35dW-4 `1da3997`), 1.3 (the records `02f1900`) and 1.4 (R35dG0-2 `f244d02`) are fixed in
source, with the follow-ons R35eG1-1, R35eG1-3 and R35eG1-2; G2's Lows are fixed in the builder's
tree except R35dG2-1 (partly fixed) and R35dW-5 (fixed in source, CI `macos-appkit` its gate).
R35cW-CI1 and R35cW-CI2 were proved by the green wave 35d CI run (`3d22c39`).

## Wave 35b state (closed on `wave35b`, NOT merged, NOT pushed) — 9 November 2026

Fix-only wave cut from `wave35`; `WAVE-35b-COMPLETE` in `docs/handoff/wave35b-complete.md`.
Every brief id fixed with a commit or recorded.  Fresh-tree gates at `90d8b4e` green
(`build/w35b/W35b-CLOSE-fresh-gates.log`) except `guard_names` (the two `9fa599b` blobs).
**Highs:** R35bG0-1 and R35bG0-2 (Calculate Project: Props project start and calculation
options) were fixed in wave 35c (`7d923dc`, `743ad8b`) and are NOT open.  Coordinator still owes the
`9fa599b` decision, push, both CI dispatches (macos-qt proves W35b-0.1; `app/main.mm` compiles
only in CI), an independent review of `wave35..wave35b`, and the 4(e) reopen check.
Wave 35c closed the rest of the G4 and CLOSE review queue (`docs/handoff/wave35b-verification/coord-gates-close.md`) except R35bG0-9 and R35bG1-10 (the untested guards recorded in `gates-g0.md` and `gates-g1.md`; assigned to no item), R35bG2-9 (watch: the intermittent `qt_assign_resources_gate`, green on macOS) and R35bW-3 (answered by the coordinator's map); R35bG1-3 and R35bG2-6 are partly fixed and recorded as defence in depth; R35bW-1, R35bW-2, R35bG2-2 and R35bG3-1 were recorded partly fixed by 35c and carry their remainders in the wave 35f state at the top (W35f-0.4, R35eG1-2).  What 35c left open is in the wave 35f state at the top.

## Wave 35d state (closed on `wave35d`, `WAVE-35d-COMPLETE`, NOT merged, NOT pushed) — 10 November 2026

Fix-only wave cut from `wave35c`; brief `docs/handoff/wave35d-brief.md`, record
`docs/handoff/wave35d-complete.md` (`WAVE-35d-G0-COMPLETE` to `-G2-COMPLETE`, then `WAVE-35d-COMPLETE`).  It worked the wave 35c queue
in `docs/handoff/wave35c-verification/coord-gates-close.md`, Highs first.  State at CLOSE:
**Highs** R35cG0-1 (fixed `abc8a60`), R35cG1-1 (fixed `61a451b`), R35cG1-2 (**partly fixed**
`6458203`, remainder R35dG0-1).  **CI** R35cW-CI1 (`23de6e2`) and R35cW-CI2 (`171831b`) are fixed
in source; CI on GCC 13 and macOS is the only proof of each and its gate.  **Mediums:** fixed
R35cG1-3 (`e967858`), R35cG2-1 (`41f7a2c`), R35cW-2 (`454521d`), R35cG2-8; **partly fixed**
R35cG1-4 (`e2eca38`, remainder R35dG1-1), R35cG0-3 (`6552f11`, remainder R35dG1-2), R35cW-1
(`4410935`, remainder R35dG1-3 and R35dW-2).
**G2 done** (`WAVE-35d-G2-COMPLETE`): the Lows R35cG0-5, -6, R35cG1-5 to -7, R35cG2-2 to -7 and
R35cW-3 and -5 are fixed or recorded, R35cW-4 (`4dcfcd7`) is **partly fixed** (remainders R35dW-1 and R35dW-5); **partly fixed, still open:** R35cG0-2 (plan reads Start word
+104 only) and R35cG0-4 (no plan-side Props digest).  Every 35d fix awaits an independent review; the coordinator still owes the
push, both CI dispatches and the mutation runs.

## Wave 35 state (closed on `wave35`, NOT merged, NOT pushed) — 9 November 2026

`wave35` is cut from `wave34b`.  Built, each for its measured shape only: G0 carry-over
fixes (resource calendar refusals, RC-W1/W2/W6..W8, CI-4 hook), G1 `mppwrite::save_calculate`
and Calculate Project (unlinked two-leaf), G2 Sort (one Name or Duration key, five tasks) and
Show Level 1 and 2, G3 Bold off (view writer), G4 macOS shell wiring (five commands).
Deferred with evidence: linked, lag and constraint Calculate, applied table, zoom, Insert
Summary and Milestone, Set Baseline 0, Notes, other font controls, Renumber, port-saved
removal.  Fresh-tree gates at `4e3ba80` green (`build/w35/W35-CLOSE-fresh-gates.log`) except
`guard_names`.  **Not mergeable:** open Highs R35G1-1..3, RW35-W2 (Calculate), R35G2-1..4,
RW35-R2 (view state), R35G3-1, RW35-W1 (port-saved removal) go to a fix-only `wave35b`
(`plan-waves27-30.md`, "Queued from wave 35").  RW35-R1 (`-Wswitch` in
`app_qt/command_status.h`) closed at CLOSE by `513de29`.  **Not pushable:** `9fa599b` added two
fixtures carrying the operator's real name (fixed forward in `9564ddd`); the rewrite is the
operator's or the coordinator's.  Lesson: MSVC hides `-Wswitch`; add every new `MenuAction` to
`app_qt/command_status.h`.  The shell hook blocks backslashes inside heredocs and inline
Python, so scripts go through Write and run by path.

## Wave 34b state (fix-only; closed on `wave34b`, NOT merged) — 8 November 2026

`wave34b` is cut from `wave34`, local only (`WAVE-34b-COMPLETE` in
`docs/handoff/wave34b-complete.md`).  It closed both Highs below (R34G3-1 / RC-R1:
Assign refuses all but the four measured pair shapes; R34G3-2: identity refs
compared), the Add Resources / Bold / Define Code writer Highs, the CI compile
failures (unused functions; gcc 15 array-bounds in `core/gantt`), and the Mediums
R34G3-3/4, RC-R3, R34G4-6/8/9.  Added no writer, lifted no refusal.  Fresh-tree
gates at `980ce9d` green (`build/w34b/close-gates/`); CLOSE review (writers +
refusals) 0 Critical, 0 High, three Mediums RW34b-1..3 queued in
`plan-waves27-30.md`.  **Merge only after:** coordinator pushes, both CI workflows
green (gcc, clang, AppleClang, asan are only checked there), and the brief-4(e)
Project reopen check of the five enabled saves (COM) passes.  Wave 35 = G5, G6 and
the queued Mediums/Lows.

## Wave 34 state (parity writers; closed on `wave34`; Highs below closed by 34b) — 8 November 2026

`main` = `origin/main` = **`4fd9b90`** (waves 33 and 33b merged, green on all three
platforms).  `wave34` is cut from it and is local only; push, both CI dispatches
and the merge decision are the coordinator's.  Work order
`docs/handoff/wave34-brief.md` (its coordinator notes carry the run history);
decode notes `spec/README.md`.

* G0 decoding lane (eight notes, pair anatomy, synthetic pairs), G1 VarMeta
  writer, G2 `214` view-stream + Props update writers: DeepSeek, 30 October 2026.
  DeepSeek stopped at G3 on a 402 (balance); G3, G4 and CLOSE were built by Claude
  Sonnet 5.5 (`tools/deepseek/sonnet-claude.cmd`, `run-wave.ps1 -Builder sonnet`).
* G3: assignment growth, resource + calendar row, DocumentSummaryInformation (with
  an independent reader), reader gaps, W34-B5 (golden `name` column dropped).
* G4 enabled, each for its measured shape only: Resource Information rename,
  Add Resources, Assign Resources, Font Bold (one cell), WBS Define Code; W34-4.0
  refused Insert Task, Delete Task, link ADD, custom-field SET/CLEAR up front.
  Deferred with evidence: Insert Summary/Milestone (4.4), Set Baseline 0 (4.7),
  Notes (4.1).
* **Open Highs (merge blocker): R34G3-1 / RC-R1** - the assignment writer and the
  Assign Resources UI accept any whole-hour work, only 40 h is measured;
  **R34G3-2** - the test masks the task/resource identity refs at +16/+32.  A
  fix-only `wave34b` goes first (Highs; then R34G3-3/4, RC-W5, RC-R3).
* G5 (scheduler save) and G6 (view state) are **wave 35**; queued in
  `plan-waves27-30.md` with the other review Mediums and Lows.
* Not yet done: the brief-4(e) Project-reopen check (COM, coordinator-only), the
  first wave-34 CI run (incl. `ubuntu-asan`), the macOS wiring (only Add
  Resources is wired).
* Gotcha: the G3 handoff said `app/main.mm` was untouched in G4; it was touched in
  4.2.  The inline-python backslash hook blocks heredoc scripts: Write a `.py`.

## Where the branches are (the truth)

`main` = **`17f5782`** = `origin/main`.  Waves 23-32 were **merged into `main`
on 27 October 2026 as `17f5782` after an operator-approved
`git filter-branch` rewrite**; the old `origin/wave23`..`origin/wave32`
branches were deleted.  **Every commit id quoted in the wave 23-32 records is
a pre-rewrite id.**  Map one with
`git show 17f5782:docs/handoff/wave32-verification/rewrite-sha-map.txt` and
look for it in the left-hand column; the right-hand column is the current id
(329 pairs; old wave-32 tip `92baa32` -> `d7d0088`, old final code tree
`7727760` -> `6a4dcc2`).  The local `wave23`..`wave31` branches are
pre-rewrite leftovers; `wave32` points at `17f5782`.

`wave33` = **`da5f4da`** = `origin/wave33`, closed with `WAVE-33-COMPLETE` and
pushed after the operator-approved replay of the unpushed range (the replay
replaced the FAT-slack fixture blob added by the pre-replay `3b53b9d` and
reworded `964594d`'s message; both are gone from the current history -- do not
quote them).  `wave33b` is cut from `wave33` after its verification record.
`origin/wave33b` = **`6fdf3af`** (the coordinator's single pre-CLOSE push at
the G0 gates commit); the CI-3 fix and the CLOSE record are local.  There is
also a local `wave33-pre-replay` backup branch (operator-owned residual Q4).

## Wave 33 state (fix-only, the 29 October audit) — complete and pushed

Work order `docs/handoff/wave33-brief.md`; record
`docs/handoff/wave33-complete.md`; verification
`docs/handoff/wave33-verification/` (gates + review R33-1..R33-9 + the
replay's old-to-new sha map).  G0-G4 and CLOSE are DONE.  Ids: G0 `c176a0f`,
`a6cfa05`; G1 `22218de`, `f18b208`, `3c996b0`, `9660453`, `0c83489`,
`95e484a`, the FAT-slack commits, `6a58e05`; G2 `227105f`, `d19e201`,
`60d8f06`, `a069d97`; G3 `1b51802`, `c25d7b8`, `c92100a`, `f6bbb96`; G4
`2102817`, `ac2ae43`, `b32557f`, `e2d8948`, `d97ba71`, `9af4187`; CLOSE
`dcefd64`, `76e9789`, `9cd7f2a`, `3cc6740`, and the record `877d012`.

Its post-replay CI (`ci` 40947728764, `qt-shell` 40947723497, `app-macos`
40947715383 on `da5f4da`) was green only in qt-shell: ubuntu-gcc,
ubuntu-asan and macos-appkit failed on the first compile errors.  **R33-9
stays open** until the coordinator's post-33b dispatch is green.

## Wave 33b state (fix-only follow-up) — complete, not yet pushed

Work order `docs/handoff/wave33b-brief.md`; record
`docs/handoff/wave33b-complete.md`.  G0, G1 and CLOSE are DONE:

* G0: W33b-0.1 (R33-1, High) `3257a4f`; CI-1 `eeee5c4`; CI-2 `26c0fb1`;
  CI-3 `939ddc8` (added at CLOSE from the G0 push's CI run); record
  `1d5fc07` + gates `6fdf3af`.
* G1: W33b-1.1 (R33-2, Medium) `b5f7e00`; W33b-1.2 (R33-3, Medium)
  `2f2f8fb`; record `21bdf13`.
* CLOSE: the independent review of `wave33..wave33b` (0 Critical / 0 High /
  0 Medium, 7 Low) and its three comment repairs `5efc8a5`; the final
  fresh-tree gates; the two queued review Lows RW33b-1/RW33b-2; the record
  commit appends `WAVE-33b-COMPLETE`.

## Numbers (wave-33b CLOSE fresh gates, final code tree `5efc8a5`, log `build/w33b/W33b-CLOSE-fresh-gates.log`, all three trees from scratch)

All three trees configured from scratch on `5efc8a5`: core
`ctest -E plan_recalc` **55/55**; `plan_recalc` **512 / 266**; engine
selftest 53 checks, suite 7 pass + `constraint-mso` expected, 0 unrecorded;
reader **1136 / 456**; M2 **332/332**, **268/268**, **64/64**; app **25/25**
and **25/25** with `MPP_NO_CORPUS=1`; app_qt **41/41**; fixtures OK (**140**
documented hashes, **174** tracked `.mpp`), paths 223, provenance 8; markers
0 hits over 1306 files and 0 messages; client-name scan 0 files/0 hits and 0
message commits, `guard_names` exit 0; backlog **114/114**, 0 mismatches;
block-comment and unused-helper sweeps 0; 0 CRLF; `git diff --check` clean
(`main..HEAD` has only the 13 known wave-33 CRLF-json hits).

Wave 33b adds no writer and no Project pair: every change is a refusal, a
test, a CI fix or a record.  Engine stays **7/8**.

## What a later session must not re-break

* **Wave 32's refusals stand**: the automatic-outline cross-refusal both
  orders, Start read-only at every shell entry, sorted/filtered range
  operations refused.  Do not enable Start without the measured pair.
* **Wave 33 G0**: every `.mpp` write goes through
  `mppwrite::write_file_atomically` (write beside, re-read, self-check,
  rename); a failure leaves the target byte-identical.  The non-structural
  save route reloads the written file as the document, so a second Save
  keeps the formula/date format.
* **Wave 33 G1**: Delete Task refuses a summary row (model, writer, both
  shells); Insert Task refuses a clone source with progress, actuals, work,
  a task calendar, a non-ASAP constraint or a real assignment; formula SET
  accepts only Text1..30/Number1..20 and CLEAR is disabled with decision-9
  text; Task Information/Task Fields decide on a trial copy; FAT-slack files
  are read-only at open; a second pending formula is refused.
* **Wave 33 G2**: both container parsers bound DIFAT/Fixed2Meta walks; CFB
  v4 is refused by design (no v4 fixture to measure); fuzz targets cover
  both parsers and the MSPDI importer; the `ubuntu-asan` job must be green
  at CLOSE.
* **Wave 33 G3**: the `app/` suite runs in CI on both OSs; a missing
  committed fixture fails with its path (only `corpus/` may skip); the
  save-pair mask is exactly the noisy bytes; the first CI pass uses
  `MPP_NO_CORPUS=1`.
* **Wave 33 CLOSE**: `scripts/check_markers.py` has nine markers; the ninth
  (the short first name) is matched on ASCII word boundaries -- do not add it
  as a bare substring and do not drop the boundary.  The ten CLOSE-review
  Lows are RW33-1..RW33-10 (RW33-3 now closed).
* **Wave 33b**: a `214/CTable` date-format request for a table index other
  than 0 is refused at `Document::request_view_state_edits` (returns a bool)
  and again in `apply_view_state_edit`; keep the index plumbing so a
  measured pair can lift it.  `apply_formula_edit` refuses
  `Operation::clear` before reading `114/Props`.  The macOS Task Fields
  handler pushes onto `app::StructuralEdits trial = plan.edits` and commits
  only on full acceptance.  The CI fixes: no `/*` inside a block comment
  (GCC/Clang), no orphaned test helpers (CI-2), no set-but-unused counters
  (CI-3, clang); local MSVC does not warn, CI is the gate.

## Privacy and the push

* The 30 October replay replaced the FAT-slack fixture blob and reworded
  `964594d`; `guard_names.py main..HEAD` exits 0 and
  `check_markers.py --range main..HEAD` is 0 hits over 1306 tracked files
  (9 markers).  The tracked text and the two `.mpp` metadata blocks were
  neutralised in wave 33 G4; `scripts/check_markers.py` runs in CI on both
  OSs.
* **Push**: `origin/wave33b` = `6fdf3af` (one pre-CLOSE push at the G0 gates
  commit; its `ci` run 40958074116 found CI-3).  CLOSE did not push or
  dispatch; the CI-3 fix `939ddc8`, the review repairs `5efc8a5` and the
  CLOSE record are local.  The coordinator pushes them and dispatches both
  workflows after CLOSE.
* Residuals left for the operator: the 13 unsalted name hashes in older
  history plus `tests/data/project_a_golden.tsv` (Q3), `6056f15`'s commit
  message (Q4), the pre-scrub public history (Q1), and the local
  `wave33-pre-replay` backup branch.  Run BOTH name scans and the marker
  check.

## Next

CLOSE is complete and `WAVE-33b-COMPLETE` is appended.  The coordinator's
next step is to push `wave33b` and dispatch `.github/workflows/ci.yml` and
`qt.yml` (`app-macos.yml` runs on the push), then record the run ids.  R33-9
closes when that run is green; any further clang error or sanitizer report is
a new G0 item of a follow-up fix-only wave (CI-4..), added by the coordinator
on re-dispatch.  The operator then verifies and merges `wave33`, decides
whether `wave33b` merges with it, and chooses wave 34 (the parity/shared-
writers brief exists).
