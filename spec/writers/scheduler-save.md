# A scheduler SAVE path (recalculate, then write back)

_W34-0.2 decode note, 30 October 2026. Companion to the pair reports under
[`evidence/`](evidence/). Static evidence is from the git-ignored Ghidra
model. No dynamic trace this wave (W34-0.4 decision)._

## Rule

A recalculation does not write a new stream. The scheduling engine updates the
task, assignment and project rows and the var store in memory, and the ordinary
save path then writes them with the rules in [`table-growth.md`](table-growth.md)
and [`var-meta.md`](var-meta.md). The measured shape is:

- **No row is inserted or deleted.** FixedData, FixedMeta, Fixed2Data and
  Fixed2Meta keep their sizes; the engine rewrites words inside existing rows
  and entries — date, duration and summary words in the task rows, and the
  pointer/offset words in the meta entries for the rows whose outline or
  data pointer it moved.
- **New var data is appended at the old Var2Data end.** Calculate Project adds
  nine 12-byte blocks (a 4-byte length word plus an 8-byte payload) at the
  previous stream length and adds nine 12-byte VarMeta entries at their sorted
  `(uid, key)` positions. No existing block offset moves.
- **The project summary strings are rewritten** in
  `DocumentSummaryInformation` (see
  [`document-summary-information.md`](document-summary-information.md)). That
  is the "summary strings" half of the brief's scheduler writer.
- **`214/CEdl/Var2Data` is save-path state, not engine output.** It shrinks on
  every save, and all of its movement sits inside the normalised save-path
  record, so it is a mask limitation rather than the edit's effect.
- A no-edit save leaves every one of these streams byte-identical
  (control vs null noise is zero in all the relevant streams).

Measured on Calculate Project (`wave30-calculate`):

- Task FixedData 654 B, 41 effect bytes, all inside the three live rows:
  row 0 (project row, uid 0) 22 bytes at +84/+85, +88/+89 and +98..+119;
  row 1 (uid 1) 4 bytes at +174..+177 (set to `ff ff ff ff`, a "no date"
  word); row 2 (uid 2) 15 bytes at +98..+119 and +174..+176.
- FixedMeta entries 3, 4 and 5 rewritten: entry 3 at +2 `0x03 → 0x09`,
  +9 `0xbf → 0xff`, +18/+19 `0x00af → 0x1fef`; entries 4 and 5 at +2 and +9.
- Fixed2Data records 0..2 rewritten at +42 and +46..+48 (record 0 also
  +58..+61, `ff ff ff ff → 00 00 00 00`); Fixed2Meta entries 3..5 rewritten
  at +95 (`0x00 → 0x08`, `0x10`, `0x14`).
- VarMeta 252 → 360 B, header count 19 → 28, nine entries added:
  uid 0 keys `0x0b4000a4` ActualOvertimeWork, `0x0b4000a5`
  RemainingOvertimeWork, `0x0b4000a8` OvertimeCost, `0x0b4000a9`
  ActualOvertimeCost, `0x0b4000aa` RemainingOvertimeCost, `0x0b4004e4`;
  uid 1 key `0x0b4004e5`; uid 2 keys `0x0b4004e3`, `0x0b4004e5`.
  The offsets are 264, 276, 288, 300, 312, 324, 336, 348, 360 — each the
  previous + 12 — and Var2Data grows 264 → 372 B.
- The nine payloads are 8-byte words: five all-zero, and
  `01 00 00 00 01 00 00 00` (uid 0 `0x4e4`, uid 2 `0x4e3`) or
  `01 00 00 00 01 00 ff ff` (uid 1 `0x4e5`, uid 2 `0x4e5`).
- DocumentSummaryInformation 1100 → 1104 B, 175 effect bytes, tail 4
  (a duration string crossing a four-byte pad).

The same write-back shape is visible in the other scheduler-driven pairs:

- `wave31-fill` (Start/Finish/Duration fill) — 25 effect bytes across task
  rows 0 and 2; one FixedMeta byte (entry 5 +2 `0x09 → 0x08`) and one
  Fixed2Meta byte; the uid 0 `0x0b4004e4` block grows 8 → 12 B and is
  re-appended at 350, and the uid 2 `0x0b4004e5` entry is removed, leaving its
  block orphaned (VarMeta count 27 → 26). Var2Data 350 → 366 B. DSI
  1104 → 1100 B, 181 effect bytes.
- `wave30-level` — DSI 1100 → 1104 B, 162 effect bytes; the decoded values
  are the project row's date and duration strings ("5 days?" → "10 days?").
- `wave30-task-mode-manual` — DSI 1100 → 1096 B, 335 effect bytes.
- `wave30-move-back` — DSI unchanged in size, 7 effect bytes (the date text
  keeps its width).
- `wave30-inactivate-second` — DSI unchanged in size, 11 effect bytes.

So the scheduler's save shape is: mutate row words in place, append new var
blocks, rewrite the summary strings, and change no stream size except
VarMeta/Var2Data (and DSI by a four-byte string pad). The engine does not
choose a compact layout; the same append/orphan rule as
[`var-meta.md`](var-meta.md) applies.

## Evidence

### Measured (pairs)

The bullet list above is the measured evidence; the exact effect runs are in
`evidence/wave30-calculate.md`, `evidence/wave31-fill.md` and the other
reports' Task-table and DocumentSummaryInformation sections. All are single
editor saves through `scripts/oracle_lock.py`; no COM call wrote a stream.

### Static (Ghidra model)

- The scheduling engine is not identifiable in the static model: the string
  `Calculation` (`0x140f80e78`) has no referencing function, and
  `CalculationType` (`0x140ec4c0`) is referenced only by
  `FUN_140a1be88`/`FUN_140a1cc30`, a data-set property. No function or string
  names "recalculate" or "Calculate Project". The engine's arithmetic is
  decoded elsewhere (G5's W34-5.1: `plan_recalc` reproduces Project's numbers
  on the same fixtures); this note covers only the save shape.
- The save driver is `FUN_140780df0` @ `0x140780df0` (SaveIStorageDocument) →
  `FUN_1408a9bc0` @ `0x1408a9bc0` (SaveStarting, Save, SaveCompleted). The
  table streams are built by `FUN_1408ad2d4`/`FUN_1408b05ac`; the var-store
  half of that routine does not match the measured editor save (see
  [`var-meta.md`](var-meta.md)), so the write-back is attributed to the
  in-memory store's incremental path, not to a scheduler-specific serialiser.
- `FUN_140a50264` @ `0x140a50264` and `FUN_140a51ec0` @ `0x140a51ec0` are the
  per-container save dispatch (TBkndAssn 6, TBkndTask 3, TBkndCal 8,
  CV_iew 11); the scheduler's results reach the file through them.

### COM facts

Calculate Project was invoked through `scripts/oracle_lock.py`; the Fill,
Level, Move, task-mode and Inactivate pairs are single editor commands that
trigger the same engine. No COM call set a computed value directly.

## Limits

- The row-word mapping is not decoded: the offsets above are measured byte
  positions in the 202-byte task row, not named fields. Which scheduler
  output each word carries is open.
- The nine `0x0b4004e3`/`0x4e4`/`0x4e5` keys have no PjField name in the
  evidence; only the five overtime fields are named.
- The static model does not expose the engine or its write-back, so the
  append/row rules are measured, not proven.
- Whether a particular command recalculates is not decoded: the
  respect-links pairs, for instance, show almost no file effect (one byte in
  `Props14`), while Calculate Project shows the full nine-entry write-back.
- A recalculation that inserts or deletes rows (an outline change) is outside
  this shape; no measured pair does it.
- The nine-entry set may depend on the fixture's assignment count; it is
  measured on one calculate pair.

## Pairs still needed

1. **Calculate Project on the synthetic base with 0, 1 and 2 assignments**
   (W34-5.x): does the nine-entry set vary with the number of assignments, or
   is it a fixed per-task set?
2. **A second Calculate with no model change** (open, calculate, save twice):
   does the engine append nothing on the second save, or re-append the same
   nine blocks?
3. **A duration change with an odd digit count** (e.g. 9 → 10 days): pairs
   with the DSI ±4 rule and shows which row word carries the duration.
4. **Auto Schedule** on a manually scheduled task: does the same nine-entry
   set appear, and does the row-word pattern match Calculate Project?
5. **A recalculation that changes an assignment's work** (not just the task's
   dates): shows whether assignment rows and their var entries are rewritten
   by the same path.

No dynamic trace this wave (W34-0.4 decision). The engine's write-back is the
strongest live-trace candidate for a future wave, because the static model
does not expose it at all.
