# memory — coordinator (2026-10-30, wave 33b complete on branch `wave33b`)

**Entry point:** `docs/START-HERE.md`.  Wave gate:
`docs/handoff/wave33b-complete.md` (G0, G1 and CLOSE appended;
`WAVE-33b-COMPLETE`).  History:
`memory/archive/coordinator-through-wave20-full.md`.

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
