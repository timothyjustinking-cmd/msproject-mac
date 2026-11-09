# Calculate Project (F9): what Project rewrites, and what a port writer must do

_W35-1.1 decode note, 9 November 2026. It consolidates
[`scheduler-save.md`](scheduler-save.md) (the W34-0.2 note) with the engine facts and
fixes the rule set that W35-1.2 builds a writer from. Sources: the committed pairs
`wave30-calculate` and `wave34-calculate-second` (with their null saves), the pair
reports under [`evidence/`](evidence/), `spec/scheduling.md` section 3 for the engine,
and the Ghidra model for the entry points. COM was not run for this note._

## 1. What F9 is

`Application.CalculateProject()` runs the engine object `TBkndRecalc`
(`spec/scheduling.md` section 3.1):

- **Entry:** `FUN_1402b5360` (slot 11 of the 94-slot vtable at `140fcb5e8`) is the public
  recalculation: forward pass, backward pass, an optional repeat pass, then
  `XP_RecomputeOutline` (`FUN_1402bb2b0`, the bottom-up summary roll-up) and a validation
  step (`FUN_1402bc5d4`). The per-pass routine is `FUN_1402b558c`; the fixpoint loop
  over the task elements is `FUN_1402b570c`.
- **What it skips:** an element whose `+0x170` test is non-zero (manual, inactive) keeps
  its stored dates.
- **The arithmetic is known.** `plan_recalc` reproduces Project's start, finish, slack
  and critical flag for all 83 tasks of the real file (M2 332/332: 268 leaves and 64
  summaries; `docs/engine-parity.md`). Linked leaves, lags, calendars and the six
  non-MSO constraint types are covered; `constraint-mso` stays an expected failure.
- **What is not decoded:** the static model has no routine that writes the engine's
  results back into the table rows. The write-back is attributed to the ordinary save
  path (`FUN_140780df0` -> `FUN_1408a9bc0`, per-container `FUN_140a50264` /
  `FUN_140a51ec0`), so the rules below are measured, not read from code.

## 2. The shape of the save

A recalculation inserts and deletes no row and creates no stream. Against a control
saved before F9 (and the control's own null save, which separates per-save noise):

| stream | size | what changes |
|---|---|---|
| `114/TBkndTask/FixedData` | same | words inside rows 0..2 (section 3) |
| `114/TBkndTask/FixedMeta` | same | one or two bytes of entries 3, 4, 5 (section 3) |
| `114/TBkndTask/Fixed2Data` | same | records 0..2 (section 3) |
| `114/TBkndTask/Fixed2Meta` | same | byte +95 of entries 3, 4, 5 |
| `114/TBkndTask/VarMeta` | 252 -> 360 | header count 19 -> 28; nine 12-byte entries |
| `114/TBkndTask/Var2Data` | 264 -> 372 | nine 12-byte blocks appended at the old end |
| `114/TBkndAssn/FixedData` | same | the assignment row's start and finish copies (8 bytes) |
| `114/TBkndCons/Fixed2Meta` | same | byte 24 `0x07 -> 0x0f` (link present) |
| `114/TBkndRsc/FixedMeta` | same | byte 139 `0x1c -> 0x5c` |
| `114/Props` | same | records `0x02400003` (4 bytes) and `0x02400024` (2 bytes); the tail key `0x00010001` (the Var2Data length) 264 -> 372 |
| `Props14` | shift | record `0x3540000a` payload `0x0000 -> 0x0001` (the universal edit marker, present on every edited save) |
| `DocumentSummaryInformation` | 1100 -> 1104 | the summary strings (section 5) |
| `214/CEdl/Var2Data` | 606 -> 590 | save-path state only; the effect bytes sit in the normalised path record |
| everything else | same | noise only |

`wave34-calculate-second` (F9 with nothing pending) writes none of this: every stream is
equal to its control outside the mask except the Props14 marker. One pair measures it, so
"a recalculation writes back only when a change is pending" is the measured shape, not a
proven law.

## 3. Task rows (FixedData, FixedMeta, Fixed2Data, Fixed2Meta)

Measured on `wave30-calculate`: row 0 is the project summary row (uid 0), row 1 task A
(10 days after the pending edit), row 2 task B (an FS successor of A, still at its stale
date in the control). The byte pairs below are quoted as stored (little-endian); the
"reading" column is an inference from the position and the COM facts, not a decoded field
name.

| row | offset | control -> edit | reading |
|---|---|---|---|
| 0 | +84/+85, +88/+89 | `00 00 -> c0 f3` | two words that were empty in the control (unnamed) |
| 0 | +98..+119 | three 6-byte runs `18 3d c0 12 18 3d -> f2 3b d8 27 02 3c` | date words (unnamed) |
| 1 | +174..+177 | `c0 12 f2 3b -> ff ff ff ff` | a date word set to the no-date value |
| 2 | +98..+119 | the same 6-byte-run pattern as row 0 | B's date words moved (its start and finish moved in COM) |
| 2 | +174..+176 | `c0 12 f2 -> d8 27 fd` | a date word, the slot row 1 set to no-date |

- FixedMeta entry 3 (the project row): +2 `0x03 -> 0x09`, +9 `0xbf -> 0xff`, +18/+19
  `0x00af -> 0x1fef`. Entries 4 and 5 (tasks A and B): +2 `0x08 -> 0x09` and
  `0x08 -> 0x0a`, +9 `0xbf -> 0xff`. Their meaning is not decoded; the common +9
  `0xbf -> 0xff` suggests presence bits for the words the engine filled in.
- Fixed2Data records 0, 1, 2: +42 gets `0x40`, `0x30`, `0x28`; records 1 and 2 also carry
  a date at +46..+48; record 0 also +58..+61 `ff ff ff ff -> 00 00 00 00`.
- Fixed2Meta entries 3, 4, 5: +95 `0x00 -> 0x08`, `0x10`, `0x14`.
- The Assn FixedData words (+54, +58, +62, +106) are the assignment's start/finish copies
  of the task row's date triples, by the same arithmetic.

Which engine output each of these words carries is open (section 7); the offsets are
measured positions in the 202-byte task row, not named fields.

## 4. The nine VarMeta entries

Calculate Project on `wave30-calculate` adds nine entries at their sorted `(uid, key)`
positions and nine 8-byte payloads appended to Var2Data at the old end (264), each with a
4-byte length word, so each block is 12 bytes and every offset is the previous one plus 12
(264 .. 360). No existing block offset moves.

| uid | key | field | payload |
|---|---|---|---|
| 0 | `0x0b4000a4` | ActualOvertimeWork | 8 zero bytes |
| 0 | `0x0b4000a5` | RemainingOvertimeWork | 8 zero bytes |
| 0 | `0x0b4000a8` | OvertimeCost | 8 zero bytes |
| 0 | `0x0b4000a9` | ActualOvertimeCost | 8 zero bytes |
| 0 | `0x0b4000aa` | RemainingOvertimeCost | 8 zero bytes |
| 0 | `0x0b4004e4` | not named | `01 00 00 00 01 00 00 00` |
| 1 | `0x0b4004e5` | not named | `01 00 00 00 01 00 ff ff` |
| 2 | `0x0b4004e3` | not named | `01 00 00 00 01 00 00 00` |
| 2 | `0x0b4004e5` | not named | `01 00 00 00 01 00 ff ff` |

Reading (inference): the five overtime fields are zero roll-ups on the project row. The
three `0x4e3`/`0x4e4`/`0x4e5` keys carry a `01 00 00 00 01 00` prefix with a `00 00` or
`ff ff` tail; their meaning is not decoded. What decides the set (row count, links,
assignments) is open (section 7), because one pair has one row set.
The append-and-sort rule is the one in [`var-meta.md`](var-meta.md); the header count and
the `Props` tail key `0x00010001` must be updated with it.

## 5. The summary strings

`DocumentSummaryInformation` is rewritten whole by the save path
([`document-summary-information.md`](document-summary-information.md)). After F9 the
changed strings are the project Finish and Duration summary properties and the per-task
rows' start, finish and duration text: `Mon 26/10/26 -> Wed 21/01/26`, `0 days? -> 13 days?`
and the rows' own strings. The stream moves by 0 or 4 bytes only through the four-byte pad
of a string; the 175 effect bytes of `wave30-calculate` are the strings plus the shifted
suffix. `core/mppwrite/summary_info` already writes and independently re-reads these
(W34-3.3).

## 6. Rule

For a pending change F9 does, in this order:

1. recompute every automatic, active row (the engine: `plan_recalc`), including the
   summary roll-ups;
2. rewrite, in place, the date/duration/late-date words of every row whose value moved,
   and set the matching FixedMeta and Fixed2Meta presence bytes (section 3);
3. copy the dates into the assignment rows of those tasks;
4. append the nine var blocks (section 4) when the rows first carry the calculation
   records;
5. rewrite the DocumentSummaryInformation strings from the new row values;
6. let the ordinary save path write the Props, Props14 and 214 state.

`mppwrite::save_plan` today does step 2's date triples only, in rows the caller names
(`recalculate_write_path`), and refuses any other changed byte. Steps 2 (meta bytes), 3, 4
and 5 are what a measured writer must add; step 1 is the engine.

## 7. Limits and open questions

- The decode is from one linked pair (A -> B, FS +0, one pending Duration change) and one
  no-op pair. _Corrected, 9 November 2026 (W35-1.3): the unlinked-leaf shape now has a
  pair, `wave35-calculate-unlinked`, and a writer reproduces it byte for byte; see
  section 8._
- Whether the three calculation-record keys depend on the number of rows, the link, or the
  assignment placeholder is open (the "pairs still needed" 1 in `scheduler-save.md`).
- The row-word to field mapping is positional: which engine output each of the date
  triples carries (early, late, remaining, constraint-adjusted) is not named.
- A recalculation that inserts or deletes rows (an outline change) and one that changes an
  assignment's work are not measured.
- Respect Links, Move, Auto Schedule and Level All are other commands with other effects;
  none is measured as a save here, and they stay disabled.

## 8. Errata from the wave-35 pairs (W35-1.2, W35-1.3)

The four pairs `wave35-calculate-unlinked`, `-linked`, `-lag` and `-snet` (reports under
[`evidence/`](evidence/)) correct this note in five places. `core/mppwrite/calculate.h`
is the writer for the first one; the other three are refused.

- **Five overtime entries, not nine.** On an automatically built file the write-back
  appends the five project-row overtime records (`0x0b4000a4`, `a5`, `a8`, `a9`, `aa`,
  8 zero bytes each) and nothing else. The three `0x4e3`/`0x4e4`/`0x4e5` keys of section 4
  are not appended by any of the four wave-35 pairs (each grows Task Var2Data by exactly
  five 12-byte blocks, 60 bytes), so section 4's "nine" is the `wave30-calculate` file's
  count; what decides the extra four there is still open.
- **The row words are named.** `core/mppwrite/table.h` (`task_row`) names the FixedData
  words: `kDuration` +84 and `kRemainingDuration` +88 (the project row takes the critical
  leaf's duration), `kFinish` +100, `kFinish2` +108 and `kFinish3` +116 (the project row
  takes the latest leaf Finish), `kStart3` +112 and `kFinish3` +116 on a leaf (late
  Start and Finish), `kStartAnchor` +174 (cleared to `0xffffffff`), and the slack words
  +74 (free), +186 (start) and +190 (total). `core/mppwrite/row_writer.h` (`fixed2_row`)
  names the Fixed2Data record words.
- **Fixed2Meta +95 does not change.** Section 2 and section 3 list `+95 0x00 -> 0x08, 0x10,
  0x14`; none of the four wave-35 pairs changes Task Fixed2Meta at all (no report lists
  it), so the byte is not part of the write-back the unlinked shape needs. The unlinked writer leaves Fixed2Meta untouched.
- **Fixed2Data +46 is the Props project start.** Each leaf's record gets `+42 = 0x30` and
  `+46 =` Props `0x02400002`, now verified by the byte match of the unlinked pair; record
  0 +58 becomes 0. The stamp differs per row and per shape (the linked, lag and snet pairs
  store other stamps, for example `0x28`), which is why those shapes are refused.
  Props `0x02400003` is the project Finish and `0x02400024` is the "calculation pending"
  flag (`01 00 -> 00 00`).
- **The unlinked-leaf shape has its pair** (replacing section 7's first bullet). The
  slack words follow the rule `build_outline_slack` (`app/edit_model.cpp`) already uses
  for the automatic outline save:
  slack is the working time between a leaf's Finish and the project Finish, free, start
  and total slack are equal, the late Finish is the project Finish and the late Start is
  that date less the leaf's duration. `app_edits_calculate` proves the engine's words equal
  Project's own for this pair.
- **Enforced, not trusted (W35b-2.2, R35G1-5).** The writer's request check requires every
  leaf's late Finish to equal the project Finish, and the leaf that ends the project to
  carry total slack 0 and a late Start equal to its early Start. The writer cannot recompute
  the per-leaf slack (it has no calendar), so the app proves the file is unchanged since the
  edit was built: `StructuralEdit::source_digest` (FNV-1a 64 of the container) is compared
  at save, and a different file, or an edit with no digest, is refused ("has changed since
  Calculate Project was queued").

- **The pairs' dates are shifted by the host's UTC offset (W35b-2.8, R35G1-9).**
  `scripts/oracle_w35_calculate.py` passes naive `datetime` values through pywin32, and COM
  stored them 11 hours earlier (the host's UTC offset): the script's ProjectStart 2026-01-05
  08:00 is stored, and Props `0x02400002` holds, 2026-01-04 21:00, and the SNET date set as
  2026-01-26 08:00 reads back as 2026-01-25 21:00 (`build/w35/W35-1.2-calculate.json`).
  Every measured shape therefore has a project start in non-working time (a Sunday evening)
  while the leaves start on Monday 2026-01-05 08:00, so Fixed2Data +46 (the Props project
  start) differs from the leaves' own start. The pairs, and what the writer reproduces, are
  exactly these stored values; the `tests/data/README.md` rows now say so. A file whose
  project start is 08:00 on a working day (the usual user file) is an unmeasured variant of the
  shape: the writer stamps `+46` with the Props value as the pair does, but no pair shows that
  Project does the same there. Not re-measured in this wave (no COM); the oracle script is
  unchanged and would need explicit time-zone-safe dates before a re-run.
- **One arrangement is measured (W35b-2.7, RW35-W3).** In the unlinked pair the first leaf row
  ends the project and the other leaf finishes before the stored project finish; the leaf stamp
  `0x30` is measured for that arrangement alone (the linked pair stores `0x30` and `0x28`). Two
  leaves that finish together, and the second leaf row ending the project, are refused
  ("finish together", "is not the first leaf row") rather than saved with the constant.

What stays refused: any link (the linked, lag and snet pairs), a constraint, a manual
row, a summary beyond the project row, progress, a task calendar, any row count other than
the project row plus two leaves, and a file whose recalculation is not pending. Respect
Links, Move, Auto Schedule and Level All are unchanged.

### Pinned parameters (W35b-0.5, R35G1-3)

The pair varies one Duration. Everything else the unlinked save leaves untouched is pinned
to the measured control (`wave35-calculate-unlinked-control.mpp`), in the writer
(`check_unvaried_parameters`, `core/mppwrite/calculate.cpp`) and in the plan
(`calculate_plan_refusal`, `app/edit_model.cpp`, shared by `make_calculate_edit` and
`push()`). A file or plan that differs is refused, and the measured shape still writes and
matches its pair.

| Parameter | Measured value | Refusal needle |
|---|---|---|
| Resource table (`TBkndRsc/FixedData`) | 48 + 172 bytes: Project's null row only, no resource on the Resource Sheet | `resource table has` / `resources on the Resource Sheet` |
| Assignments | Project's `0xffff0001` placeholder only (also refused in `task_mode.cpp`) | `assignment to a resource` |
| Calendar table | 64 + 12 x 2 bytes: Standard (uid 1, one stored block, its name `0x0d400001`) and the calendar derived for the null resource (uid 2, based on 1, no block) | `is not the measured Standard calendar` |
| Baseline 0 | no block: task `0x0b400001`, `06`, `1b`, `2b`, `2c`; resource `0x0c40000f`, `11`; assignment `0x0f400010`, `20`, `92`, `93` | `carries a baseline` / `with a baseline` |
| Task variable records | project row `0x0b40000e`, `4e4`, `563`, `564`; each leaf `0x0b40000e`, `a4`, `a5`, `a8`, `a9`, `aa`, `4e5`, `563`, `564`; nothing else (no note, calendar, deadline or task-type `0x0b400080` block) | `stores variable record` / `stored task type` |
| Assignment variable records | `0x0f400031`, `27a`, `299` for each of the two assignments | `stores variable record` |
| Resource variable records | null row `0x0c4002d6`, `2e3` | `stores variable record` |
| Task.Active | Fixed2Meta +8 bit `0x40` set on every leaf | `is inactive` |
| Leaf FixedMeta entry | bytes 0-3 `00 00 09 00`; bytes 16-20 `07 48 ef 1f 10`; bytes 21-46 zero (bytes 4-15 are the row's offset and stamp) | `metadata flags differ` |

Not measured, so not separable here: no pair varies Effort-driven or Task Type, so their
FixedMeta bits (byte 16, `07` in the control) are pinned as one value rather than decoded
(the only precedent, `assignment_growth.cpp`, compares the same entry for them and is
inferred from its comment, not from a pair). A pair that varies either would let this table
decode them.

The recalculated words themselves are recomputed, not trusted (W35b-3.7, RW35-R4):
`calculate_measured_late` gives each leaf's slack (4,800 ticks per working day after its
Finish up to the project finish, on the Monday to Friday 08:00 to 17:00 calendar) and its late
start (its duration's working days back from the project finish).  The writer's request check
refuses a leaf whose slack or late start differs (`is not the working time between its finish
and the project finish`), and `push()` recomputes the same words from the plan and refuses a
hand-built edit whose project finish, project duration, slack, late start or late finish
differs, so a hand-built edit is refused at the queue and not only at the save.
