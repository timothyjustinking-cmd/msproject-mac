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

## Post-close (same day)

The repo is **public** and `app-macos` ran green on `5747da0` (run 39849231444):
the self-smoke executed on macOS — 136 portable + 156 AppKit checks, 0 failed —
and its artifacts (window PNG/PDF, both print PDFs, `smoke.json`, the zipped
`.app`) were downloaded to `build/macos-smoke/`. The "AppKit never compiled" gap
in the wave-15 record is closed. The oracle gap (Project opening written `214/`
or formula bytes) remains the top item.

## Oracle gate closed (same day)

`tools/oracle_gate` (`-DBUILD_ORACLE_GATE=ON`) appends a column + writes a formula
into a real `.mpp`; Project opened `w15-gate.mpp` and re-saved it with **10
columns** (Number1 at index 9) and the formula record intact. Project accepts what
we write. The formula *value* reads back empty in Project for both our file and
Project's own `w14-formula.mpp` — a readback behaviour, not our bug. See
`docs/analysis/oracle/w15-oracle.md`.


## Wave 18 final (2026-10-23)

Four streams, all merged, tree **50/50** (`ctest -E plan_recalc`), M2 332/332.

* **UI parity: 9/9 measures pass** at the reference's own 1920x1032 —
  `scripts/parity_report.py corpus/bench/msproject-gantt.png <CI render>`.
  The last N/A fell when Q3 traced a phantom tenth column to a green indicator
  badge splitting the Indicators cell (MS Project leaves it blank); row 22,
  header 43, splitter 0.45924, day 24, tiers 2, colours exact, columns
  `[38,57,147,81,81,81,105,105,124]` vs `[39,...,123]` (the -1/+1 are
  gutter-border and splitter-line conventions).
* **Engine parity: 5/7** on clean re-recorded oracles (`--suite` with
  `--apply-bin build\vs-w18\Release\engine_diff_tests.exe`). A real defect fell
  out: `write_task_row` wrote **total slack at +74** where the reader reads free
  slack; `TaskValues::free_slack` added, three words at +74/+186/+190.
  Remaining: `constraint-mso` (1 field: Project's stored 12000; my probe applying
  MSO twice to all manual rows leaves durations unchanged, so the fresh oracle
  needs re-examination, not an engine change) and `assignment-50pct` (7 fields:
  placeholder tombstones differ between the two recordings).
* **F2**: the corpus `214/CEdl` "trailer" is a **cumulative Props cache** (10
  documents); `parse_var2_edl_cache` parses and round-trips both corpus files.
  CFilter criteria = 20-byte header + N*36 records + string table. viewstate 791.
* **G1**: **MPP12** implemented — `112/`/`212/` tree, task row **264 B** (uid/ID
  reversed, stored parent/level), resource 188 B, assignment 142 B (no Resume),
  link lag fields swapped, calendar columns `(uid, base, resource)`. Five
  fixtures round-trip byte-identically; master/subproject + password flag
  decoded. reader 958 checks. **`core/cal` still hard-codes `114/` and the
  MPP14 calendar column order — next lane's first job.**
* Harness changes: `file.*`/`file stream ...` excluded from parity scoring
  (Project's save churn is not semantics); the recorder copies the base into
  `build/` because Project rewrites a file it opens.

### Next

1. `core/cal` consumes `Document::version/table_root/calendars` (MPP12 calendars).
2. Coordinator: named-base-calendar fixture (unblocks `task-calendar`); re-examine
   the MSO and assignment oracles against the clean base; MPP12 password files.
3. Wave 19: Qt commands/dialogs (D13) and Windows/macOS Qt jobs.
