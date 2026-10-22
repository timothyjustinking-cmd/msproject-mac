# Wave 15 complete — the successor's gate

**This file is the wait gate.** Its existence means the wave-15 coordinator
finished: every wave-15 stream is merged, the repository is public with a
scrubbed history, and a successor may work.

* **Final commit:** the wave-15 docs commit on `main` (this file is part of it).
  The pre-rewrite private history is in `build/pre-public-history.bundle`.
* **CI:** both workflows run only on `workflow_dispatch` or a `v*` tag
  (documented in the yaml, so a public repo cannot silently spend minutes).
  `app-macos` had not been run since wave 14 when this wave closed.
* **Full local suite:** `build\vs-w15` — **49/49 ctest** (wave 14: 48); app
  `build\vs-w15-app` — **15/15 tests, 2,513 checks** (wave 14: 14 / 2,289).
* **M2:** **332/332** (83/83 x 4, 0 unexplained); fixtures verified by
  `scripts/check_fixtures.py`.
* **Counters:** 102 agent sessions + 11 coordinator sessions, ~$7.86, ~780M
  tokens. Roadmap **~78%**.

## What wave 15 delivered

| # | Stream | Deliverable |
|---|---|---|
| W62 | `214/` append + `CV_iew` | `append_table_column` (Project's automation cannot append), `apply_table_edits` seam, item-framed `CV_iew` decode, VarMeta-ordinal decoder fix |
| W63 | app self-smoke | `--smoke` drives the real window, prints both jobs to PDF, snapshots to PDF/PNG, writes `smoke.json`, exits non-zero on failure; portable half runs on Windows |
| W64 | save-path wiring | view-state (date format, column replace) and formula edits ride `Document::save`; `0x02401392` repaired; a no-edit save is byte-identical |

## The gap this wave leaves explicitly open

**Project has never opened a file we wrote with a `214/` edit or a formula.** All
byte-level claims are proved by tests and by byte-diff against Project's own
fixture pairs; the end-to-end oracle gate is not done. The procedure is
`docs/handoff/w62.md` §8: write the four `214/CTable` parts into a copy of
`synthetic-base.mpp`, let Project open and re-save it, then decode the re-saved
`214/CTable` and expect ten 115-byte records with `last_column_index == 9`.

The AppKit half of the self-smoke is also **uncompiled** — CI minutes were
exhausted mid-wave. Do not treat the print/snapshot path as proved until
`app-macos` has run once (dispatch is free now that the repo is public).

## What wave 15 did not do (next)

1. The oracle gate above.
2. Wire `Document::save`'s options into `main.mm`; prove on `app-macos`.
3. `CV_iew` editing, row height/splitter, a column-width oracle save.
4. **The three-platform Qt shell** (waves 16-17, `docs/ui-qt-port.md`): one UI,
   Windows/macOS/Linux, identical client area enforced by cross-OS golden images.
5. MPP12, MPP14 resource Type, summary-calendar rollup, elapsed mixes; the manual
   Mac smoke; LeakSanitizer/UBSan.

## Traps this wave paid for

* **Project cannot append a column through COM** (`ColumnPosition` 0 replaces,
  8/9 rejected, `ColumnInsert` hangs, `Tables()` wedges). Ours is the only
  implementation; validation must come from a Project re-save.
* **GitHub Actions minutes**: macOS bills 10x; the repo is public now and both
  workflows are dispatch/tag-only. Do not restore `push:` blindly.
* **The Dropbox folder resurrects the damaged fixture** (238,080 then 245,760
  bytes, old mtimes). `scripts/check_fixtures.py` is the gate.
* **History rewrite**: hashes quoted in older handoffs no longer resolve; the
  bundle holds the private history.
