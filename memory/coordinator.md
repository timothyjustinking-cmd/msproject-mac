# memory — coordinator (2026-10-24, wave 27 closed on branch)

**Entry point:** `docs/START-HERE.md`.  Wave gate:
`docs/handoff/wave27-complete.md`.  History:
`memory/archive/coordinator-through-wave20-full.md`.

## Where the branches are

`main` = **fcb1c10** = `origin/main`.  **`wave23`** (`e1d40b8` =
`origin/wave23`), **`wave24`** (`6dbf126`), **`wave25`** (`c012813`) and
**`wave26`** (`1299cc7` = `origin/wave26`) are closed and **not merged**; there
is no remote `wave24`/`wave25`.  **`wave27` is closed and pushed** (the CLOSE
commit after `9323db2` = `origin/wave27`); the publication hold was LIFTED on
24 Oct 2026 (history scrubbed) and the pre-push guard still runs the name scan.

Wave 27 on `wave27`: cut from `1299cc7`; brief `c632c8f`; `d30c2e8`+`0961f19`
(W27-0.1), `29f6827` (0.2), `a40ef6d`+`5a991e9` (0.3), `8e7895f` (G0 record);
`5cd1c61` (1.1), `2f80dd1` (1.2), `61772a5`+`231a422`+`9b69e9c`+`07f67b6`+
`1d0a92a` (1.3), `97e0b81` (G1 record); `7a6ec96`+`72bf912`+`3049471`
(2.1), `36cafbf` (G2 record); `1ad385d` (3.1), `032cee2` (3.2),
`8cad902`+`8fe6c18` (3.3), `c7679bc` (3.4), `c7283d7` (3.5), `1bf7b99`
(3.6), `9323db2` (G3 record); then the CLOSE docs commit.  Every group marker
(`WAVE-27-G0-COMPLETE` .. `WAVE-27-G3-COMPLETE`, then `WAVE-27-COMPLETE`) is in
`wave27-complete.md`.

## Numbers (wave 27 close, fresh trees build/w27-fresh, -app, -qt, final tree 9323db2)

* core ctest **51/51**; M2 **332/332**; plan_recalc **512** with corpus /
  **266** without; reader **1100** with corpus / **420** without; app **17/17**;
  app_qt **23/23**; fixtures OK (**46** documented hashes, **26** tracked
  `.mpp`); oracle provenance OK (8).  The log is
  `build/w27/close-fresh-gates.log`; the core stages were re-run from scratch at
  its end after the first pass could not delete the tree (transient handle).
* Engine **7/8** (`constraint-mso` deliberately expected-failing;
  `mppwrite::save_plan` is test-only); selftest 53 checks, suite exit 0.
* parity: wave23-outline **10/10**; wave25-auto colour-only **PASS** (1/0).
* CI (publication hold lifted): `wave27` @ `9323db2` green on qt-shell
  (Linux/Windows/macOS; run 40064777633), ci (Windows/Linux; 40064779873) and
  app-macos (40064782094), first dispatch.
* Privacy: line scan **0** on HEAD; line scan **0** commit messages on
  `wave26..HEAD`; normalised `guard_names.py` **0** on `c632c8f..HEAD` **and 0
  over the whole `main..wave27` ancestry (137 commits)**; no
  `client-string-<8 hex>`.  Residual (history, not the tree): the 13 unsalted
  FNV-1a name hashes added in wave 26 (`912b3fa`) and removed in `c7283d7`
  remain in the older commits; `tests/data/project_a_golden.tsv` still carries
  unsalted 16-hex name hashes.  **Run BOTH scans.**  Coverage gap (found at the
  CLOSE): the 156-name list comes from live `reader_dump` rows, so a deleted
  row's name printed by a corpus-backed reader warning into the local gate log
  is not in it; nothing tracked quotes it (`build/` is local-only).  Wave 28:
  print the uid/row only and include skipped-row names in the scanner.

## Wave 27 result (what a later session must not re-break)

* **The % Complete buttons are DISABLED** until wave 28's actuals writer:
  `ribbon_pin.tsv` column 3 = 0 for `schedule.percent_*`, exact decision-9
  tooltip, `MainWindow::set_percent_complete` removed, no UI route queues
  `set_percent`.  Project's own 50 % save (`tests/data/wave27-percent-actuals.mpp`)
  changes Actual Duration, Remaining Duration, % Complete, Actual Start, two
  date words, a progress flag, the project roll-up and Var2Data 0x0B40015E; the
  old port wrote +92 only.  The edit kind, save path and tests are kept.
* **The percent rules live in the edit model** (W27-0.2): `make_percent_edit`
  refuses uid 0, unknown uid, > 100 and any summary row;
  `StructuralEdits::push(edit, plan)` REQUIRES the plan for `set_percent`.  Step
  5a refuses ONE stale percent edit with a warning and still saves the rest
  (W27-3.1) — a hand-built/stale queue cannot poison every later Save.
* **Priority 750 is measured and KEPT** (W27-0.3): the pair
  `tests/data/wave27-priority-control.mpp` / `-750.mpp` differs in exactly row
  0's +78 word; `app/project_dialogs_tests.cpp` compares the saved
  `114/TBkndTask/FixedData` byte-for-byte with Project's own 750 save.
* **Unlink removes BOTH links touching the task** (W27-3.3):
  `app::saved_links_touching()` returns every saved link at either end (skipping
  a pending delete); the app link test's link table is byte-identical to
  Project's `wave27-unlink.mpp` (`FixedData`, `FixedMeta`, `Fixed2Meta`).
  Multi-row Unlink is NOT implemented (Medium, listed).
* **Delete/undo**: `StructuralEdit::displaced` keeps the field/clear/percent/
  link-add edits a delete removes; undo restores them, redo re-detaches them
  (W27-3.2).
* **Save tests**: every real-save `Loaded` has a named check, so a failed or
  empty load is a failure (W27-3.4).  The corpus task-order case asserts ID
  order, indent, summary and OutlineNumber from `project_a.mpp`, with no names
  and no hashes (W27-3.5; `fnv1a64` deleted).
* **Line endings**: `app/edit_model.cpp`, `app_qt/project_dialogs_qt.cpp` and
  its gate test are LF (W27-3.6).  173 grandfathered CRLF files remain.
* **CI**: both workflows plus `app-macos` have `fetch-depth: 0`; macOS does not
  surface the About box window title (the gate reads its text); the Linux
  backstage ink checks use the 12-per-channel tolerance.

## Next

`wave27-complete.md` CLOSE "Found, not fixed": the wave-26 residuals (stored
sheet column widths, PROJECT_A's yellow fill, bar-label coverage, dark-theme
header bands/ribbon ink, menu/QAT/status-bar artwork, display-only Task Form,
undecoded combination view, White theme, six disabled dialogs, no-writer
engines) plus the wave-27 review's remaining items: the latent percent
pending-insert refusal (fix before re-enabling the % buttons), the Unlink test
comment, the SKIP-on-missing fixture blocks, the operator path embedded in
the new fixtures, `mpp_diff.py`'s ten-offset truncation (it caused a false
High in the record audit), the name-list/scanner coverage gap (skipped-row
names), and two process items (five G3 messages omit their
gate log's name; fixture commits also carry the hash registry and provenance).
Wave 28 per
`docs/handoff/plan-waves27-30.md`: the actuals writer (re-enable the % buttons),
outline editing, the Options new-task mode reaching `insert_task()`, the
editable Task Form, split.  Keep the CLOSE gates green before every commit and
run BOTH name scans.
