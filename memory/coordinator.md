# memory — coordinator (2026-10-30, wave 33 in progress on branch)

**Entry point:** `docs/START-HERE.md`.  Wave gate:
`docs/handoff/wave33-complete.md` (G0-G3 appended; G4 and CLOSE still to come).
History: `memory/archive/coordinator-through-wave20-full.md`.

## Where the branches are (the truth)

`main` = **`17f5782`** = `origin/main`.  Waves 23-32 were **merged into `main`
on 27 October 2026 as `17f5782` after an operator-approved
`git filter-branch` rewrite**; GitHub holds only `main` and the
`origin/wave23`..`origin/wave32` branches were deleted.  **Every commit id
quoted in the wave 23-32 records is a pre-rewrite id.**  Map one with
`git show 17f5782:docs/handoff/wave32-verification/rewrite-sha-map.txt` and
look for it in the left-hand column; the right-hand column is the current id
(329 pairs; old wave-32 tip `92baa32` -> `d7d0088`, old final code tree
`7727760` -> `6a4dcc2`).  The local `wave23`..`wave31` branches are
pre-rewrite leftovers; `wave32` points at `17f5782`.  `wave33` is the active
branch, cut from `17f5782`.

## Wave 33 state (fix-only, the 29 October audit)

Work order `docs/handoff/wave33-brief.md`; record
`docs/handoff/wave33-complete.md`.  **G0, G1, G2 and G3 are DONE; G4 is in
progress; CLOSE remains.**  Current wave-33 ids (post-merge history):

* G0: `c176a0f` W33-0.1 atomic save helper; `a6cfa05` W33-0.2 non-structural
  save adopts its target; record `ed4864b`.
* G1: `22218de` summary delete refused; `f18b208` insert-clone refusal;
  `3c996b0` formula SET Text/Number only; `9660453` formula CLEAR disabled;
  `0c83489` Task Information trial copy; `95e484a` shown-table date format;
  `3b53b9d`+`964594d`+`397eaf9` FAT-slack read-only; `6a58e05` one pending
  formula; record `e1fa518`.
* G2: `227105f` DIFAT bound; `d19e201` Fixed2Meta bound; `60d8f06` CFB v4
  refused; `a069d97` fuzz + ASan job; record `4285d29`.
* G3: `1b51802` app suite in CI + CMake-derived macOS registration;
  `c25d7b8` save-pair mask/tail/effects; `c92100a` committed fixture missing
  is a failure; `f6bbb96` both app_qt counts, pin truth, `MPP_NO_CORPUS`
  first pass, `qt_render_1920`; record `ae667a9`.
* G4 (this group): records/privacy.  The brief's `A-`..`D-` ids apply
  (W33-4.1 C-1/C-7, W33-4.2 C-3, W33-4.3 B-2, W33-4.4 B-4/B-12, W33-4.5
  B-3, W33-4.6 B-13).

## Numbers (G3 group gates, fresh trees on f6bbb96, log build/w33/W33-G3-fresh-gates.log)

* core `ctest -E plan_recalc` **55/55**; `plan_recalc` **512** with corpus /
  **266** without; engine selftest 53 checks, suite **7 pass +
  `constraint-mso` expected**, 0 unrecorded; reader **1136 / 456**; M2
  **332/332**, **268/268**, **64/64**; app **25/25**; app_qt **41/41**
  locally / **39** registered without `corpus/`; fixtures OK (**140**
  documented hashes, **174** tracked `.mpp`), paths OK (223), provenance OK
  (8).  Engine stays **7/8**.
* Wave 33 adds no writer and no Project pair: every change is a refusal, a
  disable, a safety mechanism, a test, a CI job or a record.

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
  text; Task Information/Task Fields decide on a trial copy; table edits use
  the shown table index; FAT-slack files are read-only at open with the
  reason; a second pending formula is refused.
* **Wave 33 G2**: both container parsers bound DIFAT/Fixed2Meta walks; CFB
  v4 is refused by design (no v4 fixture to measure); fuzz targets cover
  both parsers and the MSPDI importer; the `ubuntu-asan` job must be green at
  CLOSE.
* **Wave 33 G3**: the `app/` suite runs in CI on both OSs; a missing
  committed fixture fails with its path (only `corpus/` may skip, printed
  "corpus absent"); the save-pair mask is exactly the noisy bytes and the
  three named 214 flag-byte signatures are expected failures by name; the
  first CI pass uses `MPP_NO_CORPUS=1`.

## Privacy and the push

* At the start of G4, `tests/data/synthetic-base.mpp` and
  `analysis/test_project.mpp` carry the operator's name as Author/LastAuthor
  (UTF-16LE), and 69 tracked text files (15 `.md`, 5 `.json`, 49 `.py`)
  publish the name, the company folder or the Windows username; 49
  `scripts/phase*.py` hard-code the old absolute root.  W33-4.3 and W33-4.5
  remove them; `scripts/check_markers.py` (W33-4.4) is the acceptance test and
  runs in CI on both OSs.
* **Push blocker (disposition approved)**: `3b53b9d`'s intermediate blob (the
  first FAT-slack fixture, imported `synthetic-base` bytes) carries the
  pre-wave residual, so the local `guard_names.py` pre-push guard refuses the
  range even though `964594d` cleaned the tip.  The operator approved a
  coordinator replay of the unpushed range on 30 October (brief, 05:50 CLOSE
  note); CLOSE does not push.  The replay is the only sanctioned rewrite.
* Residuals left for the operator: the 13 unsalted name hashes in older
  history plus `tests/data/project_a_golden.tsv` (Q3), `6056f15`'s commit
  message (Q4), and the pre-scrub public history (Q1).  Run BOTH name scans
  and the marker check.

## Next

G4 is complete with its marker line.  CLOSE runs the fresh-tree gates on the
final commit (all three trees with `CORE_WARNINGS_AS_ERRORS=ON`, the sanitizer
build if local tooling allows), both name scans plus `check_markers.py`, a
review pass over `git diff 17f5782..wave33`, then appends the final
`WAVE-33-COMPLETE` marker.  The push and the two CI dispatches are NOT
CLOSE's: the coordinator does them after CLOSE and after the approved replay
of the unpushed range (brief, 05:50 note).  The operator's step after that is
to verify and merge `wave33`, then choose wave 34 (the parity/shared-writers
brief exists).
