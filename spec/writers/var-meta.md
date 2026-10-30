# VarMeta — entry insert, remove and resize

_W34-0.2 decode note, 30 October 2026. Companion to the pair reports under
[`evidence/`](evidence/). Static evidence is from the git-ignored Ghidra model
(`decompiled/part_*.c`, `analysis/meta/functions.tsv`, `analysis/meta/strings.tsv`).
No dynamic trace this wave (W34-0.4 decision)._

## Rule

`VarMeta` is a fixed 24-byte header followed by `entry_count` 12-byte entries,
with no padding between entries:

```
header  +0   u32  magic = 0xFADFADBA
        +4   u32  0
        +8   u32  entry_count
        +12  u32  0
        +16  u32  0
        +20  u32  var2_bytes        // total length of the Var2Data stream
entry   +0   u32  uid
        +4   u32  offset            // byte offset of the block's u32 length
                                    // word within Var2Data
        +8   u32  key               // PjField id: 0x0b40xxxx Task,
                                    // 0x0c40xxxx Resource, 0x0f40xxxx
                                    // Assignment, 0x0d40xxxx Calendar,
                                    // 0x0740xxxx Project
```

On save the entry array is rewritten in `(uid, key)` order from the table's
var store; the serialiser does not sort, and every measured insertion landed
at its sorted position (`sorted by (uid, key): True` in every report).

The streams are updated in place, not rebuilt compactly:

- An entry's `offset` is the byte position of its block's `u32 length` word
  in Var2Data. A block costs `4 + payload_length`, with no alignment.
- A **new or changed** block is written at the end of the existing Var2Data
  stream (its current high-water mark); the entry's offset becomes that
  position. The previous block's bytes are left in place, orphaned, and are
  not garbage-collected. **Var2Data never shrinks** in any measured pair.
- An **unchanged** block keeps its offset and its bytes are copied through.
  A changed block that is **larger** moves to the append point.  A changed
  block that is **smaller** is rewritten IN PLACE: the length word and the new
  payload are written at the same offset, the old tail becomes unreferenced
  bytes, the entry does not move, and the freed-bytes counter gains
  `old - new`.  Measured by the wave-28 outline pairs (see
  "re-point in place" below); the grow direction is measured by
  `wave30-resource-rename` and `wave34-note-replace`.
- A **removed** item loses its 12-byte entry, but its block bytes stay
  orphaned in Var2Data and the stream length does not shrink.
- VarMeta is not truncated either. The new 24-byte header and `count × 12`
  entry bytes are written over the old stream from offset 0; any bytes beyond
  `24 + 12 × count` keep their previous values. Measured: count 27 → 26 with
  the stream staying 348 B, the freed last slot retaining a byte-identical
  copy of the previous last entry.
- `entry_count` (header +8) and `var2_bytes` (header +20) are rewritten;
  `var2_bytes` equals the Var2Data high-water mark.
- An entry with no block is written with `offset = 0xFFFFFFFF`; no such entry
  is measured on disk.

The rule the port must follow is therefore:

| change | VarMeta cost | Var2Data cost | other offsets |
|---|---|---|---|
| add a data item | +12 B live, one entry at its sorted position; stream grows only if the array passes the old length | +4 + payload, appended at the old end | none |
| change an item, larger payload | 0 | +4 + new payload, appended at the old end; the old block's bytes stay | the changed entry moves to the append point; none other |
| change an item, smaller payload | 0 | 0; the block is rewritten in place and its old tail stays as unreferenced bytes | the freed counter gains `old - new`; no entry byte moves |
| remove an item | −12 B live; the array shifts; the stream is not truncated | 0; block bytes stay orphaned | the freed counter gains the block's payload length |

`offset` names the block's length word, not its payload. The first block of a
non-empty stream sits at 0 and ends at `4 + length`. Blocks are laid out in a
store order that is *not* entry order: every measured file groups blocks by
field across rows (`0xa4, 0xa5, 0xa8, 0xa9, 0xaa, 0x563, 0x564, 0x00e`), while
the entries are sorted by key. The port must copy the block layout it opened,
not sort it.

## Evidence

### Measured (pairs)

- `wave30-notes` — `114/TBkndTask/VarMeta` 528 → 540 B. Header
  `(0xFADFADBA, 0, 42, 0, 0, 516)` → `(0xFADFADBA, 0, 43, 0, 0, 718)`.
  One entry ADDED at index 21: uid 2, key `0x0b40000f` (Task/Notes),
  offset 516; the insertion point sits between uid 2 / `0x0b40000e` and the
  next entry. `Var2Data` 516 → 718 B, the new 198-byte block at 516.
  202 = 4 + 198 — the length word plus the payload, exactly the rule above.
- `wave30-wbs` — `114/TBkndTask/VarMeta` 528 → 576 B. Header
  `(0xFADFADBA, 0, 42, 0, 0, 512)` → `(0xFADFADBA, 0, 46, 0, 0, 552)`.
  Four entries ADDED (uid 0 key `0x0b40019f`, then uids 1–3 key
  `0x0b40018d`), offsets 512, 522, 532, 542. Each block is 6 B, so each
  entry costs 10 B of Var2Data and every one starts at the previous
  offset + 10. Insertion points (6, 16, 28, 40) are sorted positions.
- `wave30-textstyle` — five entries in `214/CV_iew/VarMeta` RE-POINTED with a
  single delta of +56 B: uid1 `0x22400014` 93548 → 93604, uid2
  `0x22400014` 93933 → 93989, uid3 `0x20400006` 85272 → 85328, uid3
  `0x22400014` 94346 → 94402, uid3 `0x2240002f` 94580 → 94636. The one
  block that grew is uid 2 key `0x20400006` at 42576 (42,692 → 42,748 B
  payload); the five moved entries are exactly the entries whose blocks
  start after it (85,272 and everything later), each moved by the appended
  56 B. The grown block's own entry stays at 42,576.
- `wave30-baseline` — `114/TBkndCal/VarMeta` 36 → 48 B, one entry added:
  uid 4, key `0x0d400001` (Calendar/Name), offset 22 (the first block ends
  at 4 + 18 = 22). Header size word 22 → 122.
- `wave31-fill` (the wave-31 Start/Finish/Duration fill pair) — the
  discriminator for the update rule. `114/TBkndTask/VarMeta` stays 348 B on
  both sides while the header count goes 27 → 26: entry [24] uid 2 key
  `0x0b4004e5` is REMOVED, entries [25] and [26] shift down one slot, and the
  freed slot [26] still carries the previous save's bytes — a byte-identical
  duplicate of the new last entry (uid 2 key `0x0b400564`, offset 312).
  `Var2Data` grows 350 → 366 B: the changed uid 0 key `0x0b4004e4` block
  (8 → 12 B payload) is appended at 350, its entry offset moves 126 → 350
  (delta +224), and the old 8-byte block at 126 is left orphaned; the removed
  uid 2 key `0x0b4004e5` block at 338 is also left orphaned. No other entry
  offset changes. This is the measured removal, the measured growth, and the
  proof that the streams are updated in place rather than compacted.
- `wave34-note-replace` (W34-0.3) — a second measurement of the changed-block
  rule, on a text payload: the task-2 note entry [21] uid 2 key `0x0b40000f`
  is re-pointed 524 → 725, its block grows 197 → 209 B and is appended at
  725; `Var2Data` 725 → 938 B (+213 = 4 + 209); `VarMeta` stays 540 B with
  the count unchanged; the old 197-byte block at 524 stays orphaned; the
  header size word at +20 goes 725 → 938 and the entry offset is the only
  entry byte that moves. FixedMeta is untouched.
- `wave34-note-clear` (W34-0.3) — a second measurement of removal, on a text
  field: header count 43 → 42, `VarMeta` stays 540 B, every later entry
  shifts up one slot and the freed slot [42] is a byte-identical duplicate of
  the new last entry; `Var2Data` stays 716 B and the 188-byte note block at
  524 stays orphaned. The three FixedMeta bytes of the task row are the only
  other effect.
- `wave34-note-two` (W34-0.3) — the composition of one removal and one
  changed block in a single save: header count 44 → 43, `VarMeta` stays
  552 B, task 2's note entry is removed, task 3's entry is re-pointed
  715 → 906, the new 195-byte block is appended at 906 and `Var2Data` grows
  906 → 1105 B (+199); the stale tail slot [43] duplicates the new last
  entry. Both effects compose append-only with no other entry moving.
- `wave28-outline-indent` / `wave28-outline-summary` (W29-0.1; re-measured
  for this note) — the **re-point in place** discriminator, which the wave-30
  and wave-31 pairs do not show.  In both, `114/TBkndTask` entry uid 0 key
  `0x0b4004e4` keeps its offset 90 while its block shrinks: 16 → 12 B
  (indent) and 12 → 8 B (summary).  The freed word
  `0x01000002` moves 0 → 16 (indent: 8 + 2 + 2 of removed entries plus the
  16 − 12 shrink) and 16 → 32 (summary: the same four bytes on top of the
  inherited 16), which is the shrink delta measured on its own.  The same
  pairs also measure that a *grow* re-point adds nothing to the freed word:
  `wave28-outline-outdent` moves uid 0 key `0x0b4004e4` 90 → 548 with 12 → 16 B
  and the freed word gains 8, exactly the 8-byte block of the entry the same
  save removes (count 41 → 41: one removal, one addition), not the grow.
- All 31 pairs: `noise (control -> null): entries 0 added, 0 removed,
  0 re-pointed` in every report where the decoder prints it. A no-edit save
  by Project never re-frames the var store.

### Static (Ghidra model)

- `FUN_1408b05ac` @ `0x1408b05ac` (3,961 B), called only from
  `FUN_1408ad2d4` — a **compacting** table serialiser in the model, and not
  the editor-save writer. It allocates `count × 0xC` entry bytes, reads the
  old 24-byte header and entry array, and for every entry with a block reads
  the block from the input Var2Data and rewrites it at a running position
  starting at 0, assigning that position to the entry's offset word; absent
  data gets `0xFFFFFFFF`; the total is stored at header +20
  (`param_1 + 0x344`). That produces a stream compacted in entry-array order.
  The measured editor saves do the opposite: they keep orphaned blocks, a
  stale duplicate entry slot, and a block layout that is field-grouped rather
  than entry-ordered. Whatever this routine serves (the scratch copy built by
  `FUN_1408ad2d4`), the editor-save writer is a different, not-isolated path;
  the measured rule above is the one the port must reproduce.
- `FUN_140376310` @ `0x140376310` (331 B) — reads and validates the 24-byte
  header: exactly `0x18` bytes, magic `0xFADFADBA`, and a load-time guard that
  rejects `entry_count >= 0xFF000001`. It also reports a non-zero word at
  header +12 (`*(short *)(param_1 + 0x33c)`) as a diagnostic.
- `FUN_1408ad20c` @ `0x1408ad20c` — reads `0x18` bytes into the same header
  and zeroes header +4 first, confirming that +4 is not an entry field.
- `FUN_1408ad0b4` @ `0x1408ad0b4` — initialises the 16-byte FixedMeta and
  Fixed2Meta headers (magic at +0, word `4` at +4), the fixed-stream
  counterpart; it shows the header fields are written, not carried.
- `FUN_1403782a4` @ `0x1403782a4` — opens the eight table streams of a
  container in the order FixedMeta, VarMeta, FixedData, Var1Data, Var2Data,
  CopyUid, Fixed2Meta, Fixed2Data. `FUN_1408ad2d4` @ `0x1408ad2d4` is the
  save-side storage builder that creates the same set in a scratch storage
  and binds them to the serialiser.

### COM facts

The edits are ordinary Project 2021 editor saves through
`scripts/oracle_lock.py`: a note typed into the task, a WBS code scheme
applied with `WBSCodeMaskEdit` + `WBSCodeRenumber`, a baseline set, and one
cell text-style change. No COM call touched VarMeta directly; the entries are
Project's own serialisation of the model.

### W34-0.5: the five synthetic pairs

The Insert Task, Delete Task, link ADD and custom-field SET/CLEAR pairs on
`synthetic-base` add four more measured VarMeta shapes
([evidence](evidence/wave34-insert-task.md), [delete](evidence/wave34-delete-task.md),
[set](evidence/wave34-field-set.md), [clear](evidence/wave34-field-clear.md)):

- **SET** (Text1 on a task with no value): 42 -> 43 entries, 528 -> 540 B,
  entry [21] uid 2 key `0x0B400033` at Var2Data 530; Var2Data 530 -> 574 with
  one 40-byte block. This is the ordinary append at the sorted position.
- **CLEAR** (Text1 with a 19-character value): 43 -> 42 logical entries in an
  UNCHANGED 540-byte stream -- the entry is removed, every later entry shifts
  up one slot, the freed tail slot is a stale duplicate, and Var2Data stays
  574 B (no empty block is written). A custom field's clear is the same
  REMOVE shape the note-clear pair measured, not the W52 empty-value shape.
- **Insert Task**: 528 -> 672 (+144), the new row's entries appended.
- **Delete Task**: 26 effect bytes in an UNCHANGED 528-byte stream -- the
  removed row's entries shift logically, the count drops and one stale tail
  slot remains, exactly the single-entry remove shape applied to a row.

The port's writers do not reproduce either remove: `delete_task_row` shrinks
the array physically (measured 132 mismatches at VarMeta offset 396, port
`0x00` vs Project `0x03`) and `clear_field_value` keeps the entry and appends
an empty block (measured 52 mismatches at offset 8, port `0x2b` vs Project
`0x2a`). Both are recorded as NAMED expected failures in
`tests/roundtrip_add_delete.cpp` and `tests/roundtrip_field_values.cpp`; the
clear shape contradicts the W52 decode and is queued for the writer groups
rather than patched blind.

## W34-1.1: the writer this wave enables

`core/mppwrite/varmeta_edit.{h,cpp}` is the first writer that owns the var
store as such: it appends, re-points or removes ONE record for ONE row, and
refuses every shape the pairs above do not measure.  Its rules, byte for
byte:

**The three enabled shapes.**

| shape | VarMeta | Var2Data | FixedMeta | `114/Props` |
|---|---|---|---|---|
| append (the row has no entry for the key) | new 12-byte entry at `var_entry_insert_offset` (uid group, key order); header +8 +1 | `[u32 len][payload]` at the old high-water mark; header +0x14 = new size | row entry +2 +1; the caller's presence bits ORed | size word = new size |
| re-point, larger payload | entry +4 = the old high-water mark; count unchanged | new block appended; header +0x14 = new size | untouched | size word = new size |
| remove | the 12-byte entry erased from the logical array; header +8 −1; the stream is NOT truncated (`merge_logical_var_meta` writes the new header + entries over the old bytes, so the freed last slot keeps the previous last entry) | untouched | row entry +2 −1; the caller's presence bits AND-ed out | freed word += the removed block's payload length |

The stale tail this reproduces is the measured one: wave31-fill (348 B,
27 → 26), wave34-note-clear (540 B, 43 → 42) and wave34-field-clear (540 B,
43 → 42) all keep the stream length and leave slot [count] as a byte-identical
duplicate of the new last entry.  `check_table` only ever sees the declared
entries, so the duplicate is invisible to it, exactly as it is to Project.

**The `114/Props` words.**  The directory-level `114/Props` holds, per table,
a block of 16-byte scalar properties
`[u32 LE 4][tag 4 bytes BIG-endian][u32 LE 4][u32 LE value]`.  The two words a
var edit moves are measured in every pair:

| table | size tag | freed tag | measured |
|---|---|---|---|
| Task | `01 00 01 00` (BE 0x01000100) | `01 00 00 02` (BE 0x01000002) | size 516 → 718 (notes), 725 → 938 (note-replace); freed 0 → 40 (field-clear), 0 → 188 (note-clear) |
| Resource | `02 00 01 00` (BE 0x02000100) | `02 00 00 02` (BE 0x02000002) | size 140 → 152 (resource-rename); freed unmoved |

The size word always equals the table's Var2Data byte length, and the freed
word increases by exactly the payload bytes the edit unlinks (a removed
block's length, or a shrink's `old - new`).  A missing or duplicated tag is
refused: the writer will not guess which word is the counter.  The sibling
tags in the same blocks (`0x01000001` = 7, `0x01008000` = 0, `0x01000004` =
305, `0x01004000` = 202 and the `0x0200` equivalents) are static and are
preserved byte for byte.

**FixedMeta.**  Entry +2 is the row's exact VarMeta entry count, counting the
plain uid and the `0x80000000|uid` family; the writer recomputes it after the
edit (measured: notes 11 → 12, note-clear and field-clear 12 → 11).  The
presence bits are NOT derivable from the var store: they are the field's
class-slot bitmap (`field_values.h` measures seven of them), so the caller
passes the measured set/clear operations and the writer only applies and
verifies them (notes ORs +14/0x10 and +44/0x10; note-clear clears the same
two; field-clear clears +21/0x20).

**Everything else.**  The Props14 edit marker (record `0x3540000a` payload
`01 00`) is written on every save, as `edit_marker.h` does for the other
writers.  `214/CEdl` and `214/Props` are NOT touched: their pair differences
are save-path noise (effects 0 in every var-store pair), and the writer's
proof fails if any stream outside
`{table VarMeta, table Var2Data, table FixedMeta, 114/Props, Props14}` moves.

**The capability matrix (rule 1).**  A command is enabled only where a pair
measures the shape end to end, with the null save separating noise from edit:

| table | append | re-point (larger) | remove |
|---|---|---|---|
| Task (`114/TBkndTask`), uid ≠ 0 | enabled (wave30-notes) | enabled (wave34-note-replace) | enabled (wave34-field-clear, wave34-note-clear, wave31-fill) |
| Resource (`114/TBkndRsc`), uid ≠ 0 | refused: no pair adds one record to an existing resource row | enabled (wave30-resource-rename) | refused: no pair removes a resource value |
| Assignment (`114/TBkndAssn`) | refused: its measured add shape belongs to `save_assignment_field_values` (append after the group, not key-sorted) | refused | refused |
| project row (uid 0) | refused: the project row's var entries are not a task/resource field edit | refused | refused |

**Refused shapes and why** (each has a test in
`tests/roundtrip_varmeta.cpp`):

- **shrink** (`new <= old`): the rule is measured (`wave28-outline-indent` /
  `wave28-outline-summary`: in place, freed += delta), but no committed pair
  isolates it as the only edit, so this writer refuses it rather than enable
  a shape its own pair test cannot prove.  Equal-size payloads are refused
  for the same reason (append vs in-place is unmeasured at equal length).
- **zero-length payload**: no pair stores a zero-length block; a zero-length
  source block is refused too.
- **multi-row / multi-record**: one record per save; zero or more than one
  edit is refused.
- **a key already present when appending**: both the plain uid and the
  `0x80000000|uid` family count as present; an append that collides is
  refused (the measured add has no such entry).
- **two entries for the same (uid, key)**: no measured file has a duplicate;
  a re-point or remove that would have to choose is refused.
- **`0xFFFFFFFF` offset**: static only, refused (see Limits).
- **a table whose VarMeta has stale tails**: `declared != physical` is
  refused, so a file Project has already shrunk once is not edited blind.
- **an existing orphan block**: not refused directly, but it is exactly why
  the append point is the high-water mark (the current Var2Data length),
  never the end of the last *live* block.
- **an unknown table path**, a damaged table geometry, a mis-declared
  header word, a missing `114/Props` tag, an unusable output path, a uid with
  no live FixedMeta entry, and a presence operation outside the entry or with
  more than one bit are all refused with named messages.

**Evidence trail.**  The three shapes' pair tests are
`wave30-notes` (append), `wave30-resource-rename` and `wave34-note-replace`
(re-point, Task and Resource), `wave34-field-clear` (remove, custom field)
and `wave34-note-clear` (remove, free text).  The removal brief asked for
("one new pair for a removal") is the W34-0.3/W34-0.5 pair set: the notes
field's removal, the Text1 clear and the second removal in `wave31-fill`.
Every save behind those pairs is an ordinary editor save through
`scripts/oracle_lock.py`; no COM call writes VarMeta, Var2Data or the
`114/Props` counters directly.

## Limits

- The editor-save VarMeta/Var2Data writer is not statically isolated. The one
  compacting routine in the model (`FUN_1408b05ac`) contradicts the measured
  bytes, so its loop cannot be used as the rule; the in-memory store's
  insert/remove path lives behind vtable calls.
- Removal is measured for one entry at a time (wave31-fill, wave34-note-clear,
  wave34-note-two, wave34-field-clear) and per row (the W34-0.5 delete pair).
  A shrink is measured only inside the wave-28 outline edits, where it is one
  of several changes in the same save (`wave28-outline-indent`,
  `wave28-outline-summary`): in place, entry unmoved, freed `old - new`.
  Whether two removals in one save keep both stale slots is unmeasured, and
  no pair isolates a shrink as the only edit.
- The stale bytes are input-dependent: the port must reproduce them by
  starting from the opened streams and writing over them, not by rebuilding a
  minimal stream. A file that already contains orphan blocks (a second edit
  save) is unmeasured.
- `0xFFFFFFFF` as the "no data" offset is static only; no committed pair
  contains such an entry.
- The load-time cap `entry_count < 0xFF000001` is static; no pair approaches
  it.
- The front-end view stream `214/CV_iew/Var2Data` does NOT follow the append
  rule: the textstyle pair grows its block in place and shifts every later
  block (+56). The append-only rule measured here is for the `114/TBknd*`
  table var stores.

## Pairs still needed

1. **An isolated shrink** (replace a long note with a short one and change
   nothing else): confirms the in-place rule of the wave-28 outline pairs with
   a null save of its own, which is what the W34-1.1 writer needs before it
   can enable the shape.  The in-place bytes and the freed delta themselves
   are already measured.
2. **Two removals in one save** (clear two notes): do both entries vanish and
   does the freed tail keep one or two stale slots?
3. **A file with an existing orphan**: open the wave31-fill edit save and edit
   it again. Confirms the append point is the high-water mark and that orphan
   bytes persist across saves.
4. **A block of zero payload** if any editor command produces one; this is the
   only way to see a zero-length block's length word.

Rule 1 note: the port may only enable a var-store write shape that one of
these pairs covers; anything else is refused up front.

No dynamic trace this wave (W34-0.4 decision). Every open rule here is
pair-measurable: shrink, two removals, an existing orphan and a zero-payload
block are all reachable through the editor. A live trace (a break on
`WriteFile` at save, or on `FUN_1408b05ac`) could attribute the on-disk write
to the in-memory store's incremental path rather than to the compacting
serialiser the static model shows, but that identifies a route, not a shape,
and no writer waits on it.
