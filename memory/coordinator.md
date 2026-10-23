# memory — coordinator (2026-10-24, wave 25 closed on branch)

**Entry point:** `docs/START-HERE.md`.  Wave gate:
`docs/handoff/wave25-complete.md`.  History:
`memory/archive/coordinator-through-wave20-full.md`.

## Where the branches are

`main` = **488481f** (wave-22 docs close).  **`wave23`**, **`wave24`** and
**`wave25`** are NOT merged and NOT pushed (remote `wave23` = `63cd087`; no
`origin/wave24`, no `origin/wave25`).  The push is held by the client-name
policy decision — see wave25-complete §7 and brief section 0.  The local
`wave23-with-screenshot` / `wave23-pre-push` branches are unpushed backups;
**never push them**.  Tim decides whether the public repo becomes private or
its history is rewritten.

Wave 25 on `wave25`: cut from `2561a51`, brief in `2fd23f5`, then `b73fd26`
(W25-1), `5f3a12d`+`9ec15a8` (W25-2), `7a64baf` (W25-3), `f96cfcf` (W25-4),
`cbf80ef` (W25-5), `a77779f` (W25-6), `33217e2` (W25-7), `02ecf98` (W25-8),
`c21dd0e`+`3dddcd0` (W25-9), `65a270f`+`97433ce` (W25-10), `b6df460` (W25-11),
then the independent review's six fixes: `a4ac887` (W25-1), `d61af79` (W25-2),
`a35a01f` (W25-3), `b04b22e` (W25-6), `85e03d3` (W25-8), `4c28039` (W25-11),
plus the close record's docs commit.  Review: 20 findings, all fixed or listed
in wave25-complete §11.

## Numbers (wave 25 close, fresh trees: build/w25-fresh, -fresh-app, -fresh-qt)

* core ctest **51/51**; M2 **332/332**; plan_recalc **512** with corpus / **266**
  without; app **15/15**; app_qt **18/18**; reader **1097** with corpus / **417**
  without; fixtures OK (23 documented hashes, 21 tracked `.mpp`); oracle
  provenance OK (8).
* Engine **7/8** (`constraint-mso` deliberately left failing; the lane pins the
  failing SET by name; `save_plan` is test-only).
* parity: wave23-outline **10/10**; the wave25-auto colour-only reference
  **PASS** (task bar (138,187,237)); setting the old manual teal makes it FAIL
  (delta 25 > 24) — the falsification is in the W25-2 log.
* client-name scan: **0 hits** on every wave-25 commit tree, 0 commit messages.
  8-hex `client-string-` grep: clean from `7a64baf` onward (the four earlier
  wave-25 commits inherit wave-24 labels; they add none).
* CI: **not run — publication hold**.  No push.

## Wave 25 result (what a later session must not re-break)

* **Displayed rows are resolved by uid**: `app::make_gantt_source_for_displayed_rows`
  takes the RecordSet and maps every row through `records.uid(row)` (uid 0 =
  project bar); never index `plan.tasks` with a RecordSet row.  `app::begin_cell_edit`
  refuses uid 0 (project row) on both shells.
* **Stock bar palette by scheduling mode**, measured in Project with Critical
  Tasks OFF/ON: auto (138,187,237), manual (116,204,212), auto summary
  (84,84,84), manual summary (43,163,173), milestone black; highlight-ON
  critical (247,138,136), links auto (38,124,211)/manual (27,173,186)/critical
  (221,37,34).  The wave-24 palette was manual-vs-auto, not task-vs-critical.
  `GanttCanvas::set_critical_highlight` defaults OFF.
* **Privacy**: no client names or invertible hash pins in the tree; opaque
  `client-name-NN` labels and structural assertions only.  The writer oracle is
  synthetic and regenerated.  Residual: the golden TSV's 16-hex task-name
  hashes and all pre-wave-25 history (Tim's decision).
* **Gates that now test something**: ribbon kind reads live QWidget classes;
  corpus row count pinned by fixture label; missing flag fixtures FAIL; Flag11..17
  single-bit controls committed with `scripts/measure_flags.py`; the Timeline
  shown/hidden pair decodes identically except `timeline_shown`; oracle
  provenance is recomputed from git by `check_fixtures.py`.
* **Known gap by name**: `qt_stored_table_corpus` lists PROJECT_A row 3's
  italic/red text as two EXPECTED failures until W25-12.

## Next

wave25-complete §3: the FILE's text styles (W25-12, closes the two expected
failures), file bar styles (W25-13), timescale tiers (W25-14), then W25-15..21.
Keep §9's gates green before each commit; no push until Tim rules.
