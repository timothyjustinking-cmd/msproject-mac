# memory — coordinator (wave 15 end, 2026-10-23)

Successor: read `docs/handoff/wave15-complete.md` (the gate), then
`docs/handoff/coordinator.md` "Wave 15 final", then this.

## Where main is

The public history is **rewritten and condensed** (see below); the private
pre-rewrite history is in `build/pre-public-history.bundle` (git-ignored). Wave-15
work: W62 (`214/` append + `CV_iew`), W63 (`--smoke`), W64 (save-path wiring),
plus the CI cost-control changes and the three-platform decision.

## The number

* `build\vs-w15`: **49/49 ctest**; app `build\vs-w15-app`: **15/15 tests, 2,513
  checks**.
* **M2 332/332**; fixtures `55C109F2…` / `DB81218E…` verified by
  `scripts/check_fixtures.py`.
* Units: viewstate **361** (was 212), document **169** (was 43), app_smoke 98.

## The findings that matter

1. **Project's automation cannot append a table column.** `ColumnPosition` 0
   replaces index 1, 8/9 is rejected, `Application.ColumnInsert` hangs, and
   `Application.Tables()` wedges the build. `append_table_column` writes it
   ourselves: a 115-byte record after the last column, block length and
   `last_column_index` grown, later blocks/VarMeta shifted.
2. **`214/CV_iew` is item-framed, not Props-framed**: u16 count at +12, items
   `{u32 len, u32 id, u32 kind, payload}`, 2-byte pad after odd lengths. View
   name `0x2240002a`, table `0x2240000a`, filter `0x2240000b`, group
   `0x22400018`, notes `0x22400014`, margins `0x22400009`. Column widths live in
   the CTable column record, not here.
3. **Decoder bug fixed**: VarMeta `uid` is the row *ordinal*, not the index;
   corpus views skipping ordinals 13/22 no longer shift later views.
4. **Both writers now ride `Document::save`**: view-state (date format, column
   replace) and formulas, with the `0x02401392` flag repaired to 1 when values
   remain; a save with no pending edit is byte-identical (whole file).
5. **The AppKit half of the self-smoke has never been compiled** — CI minutes ran
   out before it could run. Its selector list is the review surface
   (`docs/handoff/w63.md`); the portable half is verified on Windows.

## Open, ranked

1. **Oracle gate for written `214/` + formula bytes**: Project has never opened a
   file we wrote with them. Procedure: `docs/handoff/w62.md` §8.
2. Shell wiring: `main.mm` does not call `Document::save`'s new options yet.
3. `CV_iew` editing, row height/splitter, a column-width oracle save.
4. Three-platform Qt shell (waves 16-17, `docs/ui-qt-port.md`), MPP12/MPP14,
   manual Mac smoke, LeakSanitizer/UBSan.

## Traps

* **GitHub Actions minutes**: the repo is now public (free runners). If it ever
  goes private again, both workflows are `workflow_dispatch`/`v*`-tag only — do
  not restore `push:` without checking the minutes.
* **The Dropbox folder resurrected the damaged fixture twice** (238,080 then
  245,760 bytes, old mtimes). `scripts/check_fixtures.py` is the only reliable
  gate; run it before every commit.
* A history rewrite makes every commit hash quoted in older handoffs dangling;
  `docs/history-note.md` records that, and the bundle holds the real history.
* PowerShell backticks (`` `0 `` = NUL, `$` interpolates) corrupt docs — build
  doc edits with Python or single-quoted here-strings.
* AppKit deprecations are `-Werror` errors (`NSClipPagination` →
  `NSPrintingPaginationModeClip`).
* A crashed oracle run leaves a `python` blocked in COM and WINPROJ holding
  `tests/data/synthetic-base.mpp`; kill both, drop `.oracle.lock`, restore.

## Counters

102 agent sessions + 11 coordinator sessions; **~$7.86** at ~$0.077/session;
~780M tokens. Roadmap **~78%**.

## Next three actions

1. The **oracle gate on written `214/`/formula bytes** (write, Project re-save,
   decode) — the one proof the byte-level work still lacks.
2. Wire `Document::save`'s options into `main.mm` and confirm on `app-macos`.
3. Start the **Qt shell** (Q1/Q2 in `docs/ui-qt-port.md`) so all three platforms
   share one UI.
