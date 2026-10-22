# memory — coordinator (wave 14 end, 2026-10-22)

Successor: read `docs/handoff/wave14-complete.md` (the gate), then
`docs/handoff/coordinator.md` "Wave 14 final", then this.

## Where main is

`1684718` (W61 fix) is the code tip; wave-14 docs commits follow it. Wave-14
commits: `eb607b7` + `b873535` oracle tooling/results, `d111531` W58,
`3158b3f` W59, `4a24326` W61, `bd5ee46` W60 + reader Cost decode, `569c58e` docs
corrections, `1684718` `NSPrintingPaginationModeClip`. `app-macos` was green on
`1684718`; check `gh run list` for the core `ci` tip.

## The number

* `build\vs-w14`: **48/48 ctest** (wave 13: 47; new test `viewstate_writer`).
* `build\vs-w14-app`: **14/14 tests, 2,289 checks** (`app_print` 66,
  `app_start_warning` 41; menu 744 -> 794).
* **M2 332/332** (83/83 x 4, 0 unexplained); schedin 20/20 + 10/10.
* Units: viewstate **212**, props **720** (was 520), field_values **802** (was
  477), `mpp_field_value` **290**.

## The findings that matter

1. **A table column is replaced, not appended.** `TableEdit` coerces `FieldName`
   to a string; a Long raises "The field \"188743767\" does not exist." A column
   changes only with a field NAME **and** `ColumnPosition`, and `ColumnPosition=0`
   overwrites the column at index 1 (9 columns stay, block length unchanged,
   115-byte record). `ColumnPosition` 8/9 is rejected, so **append is unmeasured
   and unwritten**.
2. **Formulas are one `114/Props` record `0x2401390`** (8 B empty, 168 B with two)
   plus the custom-field-in-use flag `0x02401392` (u16, not formula-specific).
   The net stream delta was **+144** (record +160, name record -16). `core/props`
   now writes it byte-identically to Project's record; W59 inverted the block
   grammar (`u32 13, u32 ffffffff, 7 span words, UTF-16 text, NUL [, trailer]`).
3. **Assignment custom fields use `0x0f4040xx` keys, not the PjField ids**:
   Number1 `0x0f40403c`, Text1 `0x0f404064`, stride 1. Only `FixedMeta +2`
   changes (no presence bit). Task Cost is the opposite: key = field id
   (`0x0b40006a`), f64 **hundredths** (decimal-rounded: 0.07 -> 7.0), presence bit
   at entry `+26` bit `0x08`; both families add an 88-byte table-Props record.
4. **Project displays all five W60 fixtures**: $123.45; 1.11..11.10 (the inferred
   Cost2..10 bits); 1.23/0.07; assignment Number1 7.5 / Text1 "assign-text";
   Number20 2.5 / Text30 "t30". The inferred bits and high-index keys are
   validated by Project, not just by our reader.
5. **A manual start move is not a pin**: it removes the Start/Finish VarMeta
   blocks and carries dates in FixedData, so the app warns instead of storing one.

## Open, ranked

1. **`214/` append** + `CV_iew` item encoding + `CEdl`/Props semantics; wire
   `encode_view_state` into the app save path (nothing calls it yet).
2. Wire the **formula writer** into the app save path; the flag-clearing case
   (formulas removed while custom-field values remain) needs the app not to
   blind-clear `0x02401392`.
3. MPP12, MPP14 resource Type, summary-calendar rollup, elapsed mixes.
4. App multi-window, the **manual Mac smoke**, LeakSanitizer/UBSan, visual print
   oracle.

## Traps (do not relearn)

* **A crashed oracle run poisons the tree.** It leaves a `python` blocked in COM
  holding `.oracle.lock` and **WINPROJ holding `tests/data/synthetic-base.mpp`
  open**; the fixture then reads 238,080 B and every delete/write fails with a
  share violation. Recovery: `taskkill` both pids, delete `.oracle.lock`, then
  delete + `git checkout HEAD -- tests/data/synthetic-base.mpp`, verify SHA256
  `55C109F2…`. `\\?\D:\...` opens read-only where the plain path does not.
* **`FileSaveAs` reports a padded size** (344,064 / 360,448) but Project
  **compacts on close** (239,104). Only the post-close file is evidence.
* `TableEdit` argument coercion (above); `Assignment.SetField` does not exist
  (use the property); `Application.Calculate` does not exist.
* **AppKit deprecations are errors** on `app-macos` (`-Werror`):
  `NSClipPagination` -> `NSPrintingPaginationModeClip`. Budget one CI iteration
  for every `main.mm` touch.
* Dropbox: mtimes lie, and a file synced from elsewhere can be a cloud
  placeholder; `git hash-object` vs `HEAD` is the only fixture check.
* `ctest` is not on PATH; run it through `vcvars64.bat`.
* PowerShell backtick escapes: `` `0 `` is NUL and `` $7 `` interpolates — build
  doc edits from single-quoted here-strings or the file ends up binary.

## Counters (wave-14 end)

99 agent sessions (95 + W58–W61), 10 coordinator sessions; **~$7.63** at
~$0.077/session; ~760M tokens. Roadmap **~76%**.

## Next three actions

1. Manual Mac smoke test of the v4 app (print, Save as PDF, the clamp warning,
   insert/delete, fields, predecessors, undo, save/reopen, filters, export).
2. `214/` append probe (find an API/position that grows the table) and `CV_iew`
   decoding; then wire view-state + formula writes into the app save.
3. Hand `docs/audit-handoff.md` to the external audit; fix findings; human review.
