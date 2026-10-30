# The `214` view-stream writer

_W34-0.2 decode note, 30 October 2026, extended by W34-2.1 (the writer's
decode confirmation and the font-word limit). Companion to the pair reports
under [`evidence/`](evidence/). Static evidence is from the git-ignored Ghidra
model. No dynamic trace this wave (W34-0.4 decision)._

## Rule

`214` is the front-end (UI) document: one stream set per live container class
plus the document-level `214/Props`. The classes present in every measured
file are CDrawing, CEdl, CFilter, CGrouping, CMap, CReport, CTable and CV_iew,
each with `FixedMeta`, `FixedData`, `VarMeta`, `Var2Data` (empty classes carry
only a 16-byte fixed header and a 24-byte var header). `props` for the
front-end document lives in `214/Props`.

The view state is held in `214/CV_iew/Var2Data` as whole Var2Data blocks,
framed exactly like every other table stream: `u32 length` + payload, no
alignment. A block payload is a view-state document:

```
+0   u32  payload_length - 4
+4   u32  payload_length - 4
+8   u32  constant 0x00010000
+12  u16  item_count (the u16 at +14 is preserved)
+16  item records, back to back:
        u32 length       // payload length of this item
        u32 id           // 0x2240000c is the cell-format item
        u32 kind
        byte[length] payload
        byte pad         // one zero byte only when length is odd, so the
                         // next item starts on an even offset
```

The walk is exact: the items and their pad bytes consume the payload from +16
to `payload_length` with no trailing byte. `wave30-textstyle`'s control block
shows it directly: item 0 has length 37, the pad byte at payload +65 is
`00`, and item 1 starts at payload +66. (W34-2.1 confirmed the framing on the
committed pair; `core/viewstate/viewstate.cpp` decodes it the same way.)

The cell-format item id `0x2240000c` carries a 44-byte payload (item length
word 44, kind 1). A cell text-style change appends one such item to the end of
its block, then rewrites both size words to the new `payload_length - 4` and
increments the count at +12. Nothing else in the block moves. Every later
block in the same stream moves by the appended item's size (4 + 4 + 4 + 44 =
56 B measured), and every `VarMeta` entry that points at a later block moves
by the same delta; the grown block's own entry keeps its offset. The stream
header's `var2_bytes` (VarMeta +20) is updated to the new stream length.

Saving an unmodified view rewrites the same bytes: the 23 control-vs-null
comparisons show no re-framing of `214/CV_iew/Var2Data` (and no `VarMeta`
vector changes) when the edit does not touch the view.

## Evidence

### Measured (pairs)

- `wave30-textstyle` — `214/CV_iew/Var2Data` block at 42,576 (uid 2,
  key `0x20400006`) grows from a 42,692-byte to a 42,748-byte payload.
  Payload head null `c0 a6 00 00 c0 a6 00 00 00 00 01 00 0a 00 00 00`,
  edit `f8 a6 00 00 f8 a6 00 00 00 00 01 00 0b 00 00 00`: the two size
  words go 0x0000a6c0 → 0x0000a6f8 (42,688 → 42,744 = payload − 4), the
  word at +8 is unchanged and the count at +12 goes 10 → 11.
  The appended 56 B are `2c 00 00 00` (length 44) `0c 00 40 22` (id
  0x2240000c) `01 00 00 00` (kind 1) plus 44 payload bytes beginning
  `01 00 00 00 0e 00 40 0b ...`. `214/CV_iew/VarMeta` 100,178 → 100,234 B
  (header size word), five entries re-pointed +56 (offsets 85,272, 93,548,
  93,933, 94,346, 94,580).
- The same sample's seven blocks sit at 0, 42,576, 85,272, 93,548, 93,933,
  94,346, 94,580 with payload lengths 42,572, 42,692, 8,272, 381, 409, 230,
  5,594 — each next block starts at `offset + 4 + payload`, confirming the
  framing above with no padding.
- `wave30-notes` — `214/CV_iew` FixedData unchanged; `214/CV_iew/Var2Data`
  not listed among changing streams.
- `wave30-task-mode-*` — `214/CV_iew/Var2Data` shows 2 B of masked noise and
  no effects: the task-mode edit does not touch the view stream, yet the
  buffer is still rewritten byte-identically.
- `wave30-calculate` — `214/CEdl/Var2Data` 606 → 590 B with no unmasked
  effect: the EDL (edit-list) stream is save-path state, and its shrink is
  entirely in the save-path record that the CEDL normalisation replaces with
  the marker path. This is a measured limitation of the CEdl mask, not the
  edit's work.

### Static (Ghidra model)

- `FUN_140a4fa18` @ `0x140a4fa18` (640 B) — the save loop, logged as
  "Saving BackEnd Container Props:" / "Saving FrontEnd Container Props:".
  Back-end (`*(int *)(param_1 + 0xdc) == 1`) iterates 8 `TBkndCal` class
  entries; front-end iterates 11 `CV_iew` entries. For each entry that is
  present (`FUN_140a4dc44`) and not deleted (byte at entry +0x6a), it resolves
  the props object (`FUN_140a4d640`) and writes it with `FUN_140a448ac`.
- `FUN_140a4dcf8` @ `0x140a4dcf8` (1,014 B) — the load counterpart,
  "Loading BackEnd/FrontEnd Container Props". Class entry stride is `0x6c`
  bytes and the match key is the 32-bit id at entry +200 (`0xc8`); an
  unknown container logs "Skipping Props from Stream for unknown container".
- `FUN_140a4e6ec` @ `0x140a4e6ec` (939 B) — loads container data and custom
  props ("Loading data for", "Loading custom prop data for"); `FUN_140a50264`
  @ `0x140a50264` (406 B) enumerates the container classes (TBkndAssn 6,
  TBkndTask 3, TBkndCal 8, CV_iew 11) and calls `FUN_140a45900` to save each.
- `FUN_140a51ec0` @ `0x140a51ec0` (328 B) — dispatches a container name
  (TBkndRsc, TBkndCal, TBkndAssn) to the per-class writer stored at
  `param_1 + 0x1a8` / `+0x1b0` / `+0x1b8`; this is the vtable path by which a
  container's table streams are written.
- `FUN_14043e960` @ `0x14043e960` (3,461 B) — the generic container
  serialiser. It creates containers in the serializer ("Unable to create
  containers in serializer!"), names them from the class name plus an index
  ("TBkndAssn" + number), deletes elements no longer present ("Unable to
  delete elem from serializer!"), and copies the 11-qword container flag
  block from the old serializer to the new one. This is where the container
  set — and therefore the 214 stream set — is diffed against the in-memory
  document on save.
- `FUN_1408b05ac` @ `0x1408b05ac` — a table serialiser in the model that
  compacts VarMeta/Var2Data in entry order (see [`var-meta.md`](var-meta.md)).
  The measured editor save does not use that loop, so it is not the view-block
  writer either. The view-state block framing above is the payload format of
  one `214/CV_iew/Var2Data` block; the routine that writes it was not isolated
  within the time box.

### W34-2.1: the font word (decode confirmation and its limit)

The writer needs to know which bits of the 44-byte entry's font word at +40
mean Bold. The W34-0.2 static pass did not settle any of them, and a second
targeted pass for W34-2.1 did not either:

- the literal `0x90000001` does not appear anywhere in the Ghidra model;
- the `0x2240000c` references the model exposes are property get/set calls —
  `FUN_140395150` reads the record, `FUN_1402a007c` and `FUN_1403a3910` fetch
  it by id — with no bit manipulation of the font word;
- the only `bold`/`italic` strings are the HTML export serialiser
  (`0x140efbe00`), which reads styles for export and does not write the item.

The word is therefore NOT decoded, and the writer claims no bit. It accepts the
measured word exactly (`0x90000001`) and only for Bold (set): the capability
matrix in `core/mppwrite/viewstream_edit.h` enables that one append and
refuses italic, underline, strikethrough, both colours, Bold-off, whole-row
selection, every view uid but the measured 2, and the project summary row
(uid 0). A future pair that varies exactly one of those factors is what would
extend it.

### COM facts

The textstyle pair is a Project 2021 editor save after a cell text-style
change on one view. The view stream bytes were not touched by the oracle
script; the appended item is Project's own serialisation.

### W34-0.5: the five task-op pairs

All five synthetic task-op pairs change `214/CEdl/Var2Data` beyond the
save-path record ([insert](evidence/wave34-insert-task.md) 600 -> 592 with
118 effect bytes, [delete](evidence/wave34-delete-task.md) 600 -> 576 with
125, [link-add](evidence/wave34-link-add.md) 594 -> 608 with 2 + 30 tail,
[field-set](evidence/wave34-field-set.md) and
[field-clear](evidence/wave34-field-clear.md) 596 -> 580 with none). The
column is the CEdl view's cached task state: it follows any task edit, and
the port copies the control's stream. The comparison therefore records a
named expected failure per pair with the exact signature (e.g. insert: first
of 126 at byte 166, port `0x18` vs Project `0x1c`); the port has no CEdl
writer and none is written blind.

## Limits

- The static model shows the container-level save ordering but not the
  view-state item writer: the block payload is produced inside an opaque
  props/container object reached only through vtable calls, and the writer of
  id `0x2240000c` was not isolated within the time box. The item rule is
  therefore measured (one id, one operation) plus the port's
  `core/viewstate` model, not statically proven.
- The 11 `CV_iew` class entries are an in-memory class table, not 11
  storages: the measured file has one `214/CV_iew` stream set. The index is
  not visible in the file layout.
- Replace and remove are unmeasured. Only append has been observed; whether a
  second change to the same cell appends a second item or replaces the first
  is open.
- The font word is not decoded (W34-2.1 above): the writer stores the measured
  `0x90000001` for Bold and refuses every other attribute and every unset.
- The writer does not claim a second item, an item in a block that already
  carries one, a row-default entry, or any view but the measured uid 2.
- `214/CEdl/Var2Data` is a save-path buffer whose size changes on every save
  (518 → 502 in `wave30-notes`, 606 → 590 in `wave30-calculate`) with all of
  the movement inside the normalised save-path record. The port must keep
  reproducing the normalised form only; the record's raw per-save bytes are a
  mask limitation.

## Pairs still needed

1. **A view edit that must change an existing item** (rename a view, change a
   column width): does Project append a second `0x2240000c` item, replace the
   old one in place, or rewrite the block? One factor: replace vs append.
2. **Delete a view** (or hide one): the container deletion branch of
   `FUN_14043e960` is otherwise unmeasured.
3. **A second cell-style change on a different cell of the same view**: tests
   whether one item is appended per change and whether the count is a true
   item count.

No dynamic trace this wave (W34-0.4 decision). The item writer is the first
candidate for a live trace if a future wave needs the replace rule.
