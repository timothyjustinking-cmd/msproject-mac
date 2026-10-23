# memory — coordinator (2026-10-24, wave 26 closed on branch)

**Entry point:** `docs/START-HERE.md`.  Wave gate:
`docs/handoff/wave26-complete.md`.  History:
`memory/archive/coordinator-through-wave20-full.md`.

## Where the branches are

`main` = **3f973d5** (origin/main; the driver commit).  **`wave23`** (d236f91),
**`wave24`** (2561a51), **`wave25`** (3b4da5f) and **`wave26`** (the CLOSE
commit after `3d78191`) are NOT merged and NOT pushed.  Remote `wave23` =
`63cd087`; there is no `origin/wave24`, `origin/wave25` or `origin/wave26`.
The push is held by the client-name policy decision — see wave25-complete §5
and brief section 0.  The local `wave23-with-screenshot` / `wave23-pre-push`
branches are unpushed backups; **never push them**.  Tim decides whether the
public repo becomes private or its history is rewritten.

Wave 26 on `wave26`: cut from `3b4da5f`; the brief `8ac3137`; the driver fix
`a5dff41`; then `28a9f9b` (W26-0.1), `853e2d3` (G0 record),
`3142c60`..`b868aff` (W26-1.1..1.5), `d4be89a` (G1 record),
`e83e66b`..`e8d0520` (W26-2.1..2.7), `2d1448a` (G2 record), `fde3186`
(W26-2.8), `85ad836` (G2 correction), `a70a0c7`+`cd727ad` (W26-3.1),
`40c1ad2` (3.2), `f7962e7` (3.3), `99304ad`+`4ca7821` (G3 records),
`6fb6ec5`..`6cc5181` (W26-4.1..4.6), `aa10b3d` (G4), `7b2268e`..`712535a`
(W26-5.1..5.4), `4e76142` (G5), `279d44f`..`7eb911c` (W26-6.1..6.3),
`ebb835d` (G6), `759a856` (W26-REV-1, the client-name removal), `3d78191`
(W26-REV-2, the review's four remaining High fixes), then the CLOSE docs
commit.  Every group marker (`WAVE-26-G0-COMPLETE` ..
`WAVE-26-G6-COMPLETE`, then `WAVE-26-COMPLETE`) is in `wave26-complete.md`.

## Numbers (wave 26 close, fresh trees build/w26-fresh, -app, -qt, final tree 3d78191)

* core ctest **51/51**; M2 **332/332** (268 leaf fields, 64 summary fields from
  16 summaries); plan_recalc **512** with corpus / **266** without; reader
  **1100** with corpus / **420** without; app **17/17**; app_qt **23/23**;
  fixtures OK (**25** documented hashes, **21** tracked `.mpp`); oracle
  provenance OK (8).  The single log is `build/w26/final-gates.log`.
* Engine **7/8** (`constraint-mso` deliberately expected-failing;
  `mppwrite::save_plan` is test-only); the suite is in the same log.
* parity: wave23-outline **10/10**; wave25-auto colour-only **PASS** (1/0).
* client-name scan: the line-based pass is **0** on all 42 trees and 0 commit
  messages, but it is a **false negative for a wrapped name**: the normalised
  pass found a PROJECT_A task name over two lines in the G3 record on 18 trees
  (`99304ad`..`ebb835d`).  It is removed in `759a856`; the normalised scan is 0
  on the close tree and messages.  The name remains in those 18 commits'
  history (Tim's rewrite decision).  **Always run both scans.**  8-hex token
  grep 0 trees; positive control on `wave23` finds 14 files.
* CI: **not run — publication hold**.  No push.

## Wave 26 result (what a later session must not re-break)

* **Print rows come from the RecordSet** through `app::print_row_list()`
  (`app/page_layout.h`): the filter's rows when active, every RecordSet row
  otherwise; macOS, Qt and the headless tests share it.  Never use
  `plan.view.tasks.size()` as the row list.  `app_layout` / `app_smoke` run the
  row-shown `wave24-order.mpp`; the smoke leaves the fixture byte-identical.
* **Saveable structural edits**: `StructuralKind::set_percent` (ribbon % buttons)
  and `set_project_priority` (Project Information) are written by step 5a of
  `save_structural_edits` (one `mppwrite::TaskTable` write) and re-read by tests
  (40 %, 750).  `TaskTable::row_for_uid` throws for an unknown uid, so a refused
  save leaves the target untouched.  The stored % Complete column stays
  presentation-only.
* **The FILE's own view state now renders**: `core/viewstate/gantt_style.*`
  decodes the applied Gantt view's `0x22400008` record (17 text-style slots,
  FONT_BASES from `214/Props`, 195-byte bar records, three timescale tiers);
  `app/view_style.*` is the headless read seam.  PROJECT_A renders its italic
  red milestones, navy/red/green bars and year/quarter/month tiers.  The two
  W25-5 expected failures are ordinary checks now.  Do not "correct" the
  TextStyles slots from COM's `PjTextItem` numbers (G3 errata: COM Item=3 wrote
  +506, not MPXJ's +186; the slot order is what the capture pins).
* **Ribbon**: `app_qt/ribbon_bar.*` models button/small/split/combo/checkbox/
  field/gallery/launcher; `control_kind()` reads the LIVE QWidget class.  The
  pin is 9 columns; `ribbon_pin_unmodelled.tsv` is empty.  114 command controls
  on seven tabs have unique one-ink glyphs at 32/16 px.
* **Backstage**: 12 rail entries (Account page + pinned Options action), all 11
  pages; the 11-page Options dialog has 3 functional controls.  Black is
  implemented (White remains disabled).
* **Dark themes**: `chrome_palette(theme)` owns the ribbon/QAT/timeline/status
  surfaces and `set_theme()` repaints live; `qt_theme_gate` requires the exact
  measured dark fills and byte-identical sheet/chart (tolerance 0).  Sheet/chart
  header bands and the ribbon ink stay light (recorded).
* **Split view**: a vertical `detailsSplitter` under the sheet/Gantt splitter;
  the Task Form is display-only and follows the selection by RecordSet row; a
  6-px draggable handle; the preference survives a view switch.
* **Review fixes (W26-REV-1/2, `759a856`/`3d78191`)**: the project-summary
  toggle folds `records_` into `base_` before rebuilding (a pending Start
  survives); Blank Project and a Backstage Recent row run the File > Open
  discard guard; the Font name/size combos are file-driven (first FONT_BASES
  entry on load, Calibri 11 for a new project; the pin marks them `file`); and
  `qt_stored_table_corpus` reads PAINTED canvas/sheet pixels, not only model
  roles.  Both data-loss fixes have falsifying tests.
* **Privacy**: no new name/hash in the close tree, but the line-based scanner
  is blind to a name wrapped across lines; the normalised pass found one in the
  G3 record (18 trees) and it is removed.  The golden TSV's unsalted name
  hashes and pre-wave-25 history remain Tim's.  Run BOTH scans.

## Next

`wave26-complete.md` CLOSE "Found, not fixed": the stored sheet column widths,
PROJECT_A's yellow per-row fill, bar-label coverage, the dark themes' light
header bands, the menu/QAT/status-bar artwork, the display-only Task Form, the
stored combination view, the White theme, the six still-disabled dialogs and
the no-writer engines.  The review's remaining Mediums: ribbon check-box
accessible-name sync, Task Information validate-then-commit, Options
new-task mode not reaching `insert_task()`, and the pending-insert % edit.  Also
normalise the two new CRLF files and correct the `scripts/oracle_w26.py`
pointer.  Keep the CLOSE gates green; no push until Tim rules.
