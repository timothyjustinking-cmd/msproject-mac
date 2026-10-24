# memory — coordinator (2026-10-25, wave 28 closed on branch)

**Entry point:** `docs/START-HERE.md`.  Wave gate:
`docs/handoff/wave28-complete.md`.  History:
`memory/archive/coordinator-through-wave20-full.md`.

## Where the branches are

`main` = **fcb1c10** = `origin/main`.  **`wave23`** (`e1d40b8` =
`origin/wave23`), **`wave24`** (`6dbf126`), **`wave25`** (`c012813`), **`wave26`**
(`1299cc7` = `origin/wave26`) and **`wave27`** (`bc52cb8` = `origin/wave27`) are
closed and **not merged**.  **`wave28` is closed locally and NOT pushed**:
there is no `origin/wave28`.  The local pre-push guard refuses the range
`origin/wave27..wave28` on exactly one item — the `synthetic-calendars.mpp` blob
added in `076438f` (operator name; replaced at the tip by `d1b5a8a`).  Section 0
forbids rewriting history or working round the guard, so this is **Tim's
decision**; no CI ran for wave 28.

Wave 28 on `wave28`: cut from `bc52cb8`; brief `bc90915`; `076438f`+`d1b5a8a`
(W28-0.1), `3dbdce0` (0.2), `2b365e5` (0.3), `01c9a0b` (0.4), `40a61c7` (G0
record); `ae1a065`+`18a11ce` (1.1), `a181229`+`137bca7` (1.2),
`cd83e31`+`2362916`+`92f45e8` (1.3), `2648d2a` (G1 record);
`b427bde`+`2bb3bca`+`669d49f`+`40a5827` (2.1), `8027462` (2.2), `7fe86e8` (G2
record); `fc7272e`+`5d22f54`+`d2e89c7`+`feb0ba1`+`da7f030` (3.1), `c233ac8` (G3
record); `167beea` (4.1), `3fe29cc` (G4 record); `cc803c1`+`64c3b4c`+`1d4e830`
(5.1), `7a69a40` (G5 record); then the CLOSE docs commit.  Every group marker
(`WAVE-28-G0-COMPLETE` .. `WAVE-28-G5-COMPLETE`, then `WAVE-28-COMPLETE`) is in
`wave28-complete.md`.

## Numbers (wave 28 close, fresh trees build/w28-fresh, -app, -qt, final code tree 7a69a40)

* core ctest **52/52**; plan_recalc **512** with corpus / **266** without;
  reader **1100** with corpus / **420** without; M2 **332/332**, **268/268** and
  **64/64**; app **20/20**; app_qt **24/24**; fixtures OK (**72** documented
  hashes, **52** tracked `.mpp`), paths OK (94), oracle provenance OK (8); core
  configure/build with `CORE_WARNINGS_AS_ERRORS=ON` exit 0.  The log is
  `build/w28/close-fresh-gates.log`.
* Engine **7/8** (`constraint-mso` deliberately expected-failing;
  `mppwrite::save_plan` is test-only); selftest 53 checks, suite 8 recorded
  (7 pass + 1 expected), 0 unrecorded, exit 0.
* Parity: **wave23-outline 10/10** and **wave25-auto colour 1/0**, re-run on the
  fresh Qt shell at 7a69a40 (`build/w28/close-parity.log`); every measure PASS.
  The pixel score is not re-run (no reference PNG locally).
* CI: **none** — the push was refused (no remote branch).
* Privacy: line scan **0 files / 0 hits** on HEAD and **0 commit messages** on
  `wave27..HEAD`; normalised `guard_names.py` **0 hits, exit 0** on
  `3fe29cc..HEAD` and on the CLOSE's own range; no `client-string-<8 hex>`.
  The push-range guard refuses only the `076438f` blob.  Residuals (Tim's
  decision): two pre-wave fixtures at HEAD (`synthetic-base.mpp`,
  `wave23-outline.mpp`) carry the operator's name three times each, 69 tracked
  text files (historical docs) carry it, and the 13 unsalted FNV-1a name hashes
  removed in wave 27 remain in the older wave-26 commits plus
  `tests/data/project_a_golden.tsv`.  **Run BOTH scans.**  Coverage gap (found
  at the wave-27 CLOSE, still open): the 156-name list comes from live
  `reader_dump` rows, so the corpus-backed reader warning about skipped row
  uid 10856 prints a name the scanner does not know; nothing tracked quotes it
  and `build/` is local-only.  Wave 29: print the uid/row only and include
  skipped-row names in the scanner.

## Wave 28 result (what a later session must not re-break)

* **The CLOSE review's three Highs are first in wave 29**: F1 the actuals save
  does not write Project's FixedMeta `+44`/blob changes (the test masks them;
  brief section 1 makes that a failing check); F2 the outline save leaves
  Project's assignment tombstone (indent/summary) and create (outdent)
  unwritten; F3 no gate scans the whole tree for the operator's name.  The
  record's CLOSE has the full F1-F7 / A1-A9 table, the errata and the queue.
* **The actuals writer is enabled** (W28-1.2/1.3): the five % buttons, Task
  Information's Percent complete field, Mark on Track and Update Project
  (Entire project / Selected tasks) all queue one measured structural edit;
  File > Save writes Project's actuals, dates, slack/late words, progress flag,
  project roll-up/status date and Var2Data steps with
  `mppwrite::write_percent_actuals`.  Seven Project pairs are committed
  (`tests/data/wave28-percent-*`, `wave28-mark-*`, `wave28-update*`).  Update
  Project's "100% complete" and rescheduling stay disabled with the exact
  decision-9 tooltip.  `make_percent_edit` now also refuses `file_row == 0`, so
  a pending insert cannot queue a percent edit; step 5a refuses one stale
  percent edit with a warning.
* **Outline editing is enabled** (W28-2.1/2.2): Indent/Outdent (Task tab, Edit
  menu, Option+Shift+Right/Left) write the measured
  FixedData/Fixed2Data/FixedMeta/Var2Data words through
  `mppwrite::write_outline_edits`, and the sheet/Gantt replay the new level,
  collapse triangle, indentation, OutlineNumber and summary bracket at once
  with undo/redo.  No CPM reschedule, no Assignment write (reported).
* **The new-task mode is written** (W28-3.1): `114/Props` record `0x024013c8`,
  manual `0x00FF` / auto `0x0000`; Options > Schedule queues it, the status bar
  follows the loaded file plus pending edits, and Insert Task stamps the flag
  Project's own InsertTask probe measures.
* **The Task Form edits through the sheet's model** (W28-4.1): no new writer;
  each field is enabled only where the sheet's column is editable;
  Predecessors/Resources stay read-only; stale commits are dropped by uid; undo
  is proved through the real menu action.  The undone-percent display restore
  in `without_structural_edits` also fixes the sheet/form after undoing ribbon,
  Mark on Track and Update Project edits.
* **Split Task is DEFERRED and stays disabled** (W28-5.1): Project stores the
  split as a segmented 128-byte assignment work contour (`0x0F400031`, VarMeta
  relocation, new task `+168` bit `0x2000`) plus a whole-plan slack/date
  rewrite; `mppwrite` decodes only the flat contour and refuses an existing
  entry.  `tests/data/wave28-split(-control).mpp` is the later writer's
  comparison; the button keeps the exact tooltip.
* **G0 fixes stay**: the seven fixture paths are neutral (six re-derived via
  Project on `C:/w28`, the calendar base neutralised in place) and
  `check_fixtures.py` fails on drive/user-profile paths; outline numbers follow
  stored ID order (Project: "1", "2"); the FAT vector is sized to what the
  builder reads; Unlink is KEPT on Project's reopen measurement (the stored
  date cache it does not write is invisible to Project).
* **Still binding from wave 27**: Priority 750 is measured and kept;
  `StructuralEdits::push(edit, plan)` requires the plan for percent edits;
  delete/undo restores displaced edits; Unlink removes both links touching the
  task (multi-row Unlink is not implemented); every real-save `Loaded` has a
  named check; the corpus task-order case asserts ID order/indent/summary/
  OutlineNumber with no names and no hashes; text files are LF (174
  grandfathered CRLF files remain, `git grep -Il $'\r'`).

## Next

Wave 29 starts with the CLOSE review's three Highs (F1 actuals FixedMeta
`+44`/blob and a named check, F2 outline assignment tombstone/create, F3
whole-tree operator-name scan), then F5 (run Project's reopen on the actuals/
outline/new-task port saves), F4/A1/F6/F7 and A2/A5/A6, then
`wave28-complete.md` CLOSE "Found, not fixed": the push blocker and the
operator-name residuals (Tim); the wave-26 residuals (stored sheet column
widths, PROJECT_A's yellow fill, bar-label coverage, dark-theme header
bands/ribbon ink, menu/QAT/status-bar artwork, undecoded combination view,
White theme, six disabled dialogs, no-writer engines for project move,
baselines, WBS and calendars); the wave-26/27 review Mediums/Lows (ribbon
check-box accessible-name sync, Task Information validate-then-commit ordering,
0xFFFF point size, multi-row Unlink, SKIP-on-missing fixture blocks,
`mpp_diff.py`'s ten-offset truncation); the name-scan coverage gap; the two
process items; the CRLF files.  Wave 29 per `docs/handoff/plan-waves27-30.md`:
the PROJECT_A gaps, the three format dialogs, Page Setup, Visual Reports /
Compare Projects, and the chrome finish.  Keep the CLOSE gates green before
every commit and run BOTH name scans.
