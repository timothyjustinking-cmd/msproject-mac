# memory — coordinator (2026-10-23, wave 21 in progress)

**Entry point:** `docs/START-HERE.md`.  Full history:
`memory/archive/coordinator-through-wave20-full.md` and
`docs/archive/coordinator-handoff-through-wave20.md`.  Read this file only for
the current state; the archive is for investigating a past decision.

## Where main is

`main` = **d51ab41**, pushed, clean apart from Q6's in-flight edits.  Wave 21:
C6 (`e691794`, Linux `--parallel 2`) and E6 (`d51ab41`, mppwrite CFB slack +
MSO rule measured) are committed; **Q6 is running** (app_qt stored-table
rendering + the operator's four fixes).  The wave gate
`docs/handoff/wave21-complete.md` does **not** exist yet.

## The numbers (last measured)

* `build\e6-w21`: ctest `-E plan_recalc` **50/50**; M2 **332/332**; reader
  **984**; cal **785**; engine self-test **265**; mppwrite CFB **1693**;
  fixtures OK.
* Engine suite **7/8** -- `constraint-mso` reads 9600 vs 12000 on purpose: the
  rule is measured and pinned but applied only by the self-test opt-in, not by
  the scored applier or production.
* app_qt **11/11** and UI parity **9/9** at `6a506cd` (before Q6); CI run
  39898703443 green on all three with the three packages.  Wave 21 still owes
  one dispatch.

## Wave 21 (in progress)

* Brief: `docs/handoff/wave21-brief.md`.
* **Q6** (running): decision 7, the loaded task table's columns/order/titles/
  widths from `214/CTable`, then hide the menu bar, backstage back arrow,
  double-click -> Task Information, selection `#A2D3A0` + critical `#FF0000`,
  header captions, status-bar Ready.
* **E6** (committed): CFB slack tolerance in `mppwrite`; `--dump sf248` loads
  with a warning.  MSO save-time word measured/pinned, *not* scored.
* **C6** (committed): Linux Build uses `--parallel 2`; shell syntax and timing
  unverified until the next dispatch.
* Next: verify Q6, run the full gates, ONE dispatch, then
  `wave21-complete.md` + coordinator/roadmap updates + push.

## Facts that matter (short)

* The real file's stored table: Task Name, Dur, Start, Finish, %Com, Pre, Suc,
  SUMMARY, Yes; selection `#A2D3A0`; critical bars `#FF0000`; its green/navy
  bars are file-specific bar styles, not defaults.
* Project has no classic menu bar; backstage has a top-left circular back arrow;
  status is "Ready" + "New Tasks : <mode>"; QAT lives in the title bar.
* The operator's reference set is local-only at
  `corpus/reference/2026-10-23-msproject/` (git-ignored), indexed by
  `docs/ui-reference-set.md`.

## Open, ranked

1. Q6: decision 7 + the operator fixes; then the wave-21 gate and dispatch.
2. Ribbon tab inventories (decision 1 is PARTIAL: our Resource/View tabs are not
   Project's group sets; the clean strips are the target).
3. Dialogs 14/15, split view 16, dark custom surfaces 10, per-screen pixel diff.
4. Bar-style decoding (the file's green/navy bars), clipboard display order.
5. constraint-mso in the product save path.
6. macOS launch smoke + notarization.

## Traps (top)

* Qt 6 offscreen has no fonts: Carlito is bundled per executable (not in the
  static library, where the linker drops it).
* P1 reads grey strips below x=60 as weekend/columns: the view caption must be
  white, borderless and inside the sheet band.
* Serial builds only; no `--parallel`/`/m` or piped `cmake --build` in the
  sandbox.  Python text writes need `newline="\n"`.  `gh run download` refuses
  existing files.  Dropbox mtime churn makes the edit tool re-read.
* `tests/data` is never written by a lane; `plan_recalc` is never run; `corpus/`
  is git-ignored and nothing from it is committed.

## Next three actions

1. Verify and commit Q6; re-run app_qt 11/11 + parity 9/9 and the root gates.
2. Dispatch the wave once; verify the three packages and the three renders.
3. Write `wave21-complete.md`, update roadmap/handoff, push.
