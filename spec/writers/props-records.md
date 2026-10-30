# `114/Props` and `214/Props`: record updates and typed-property scalars

_W34-0.2 decode note, 30 October 2026, extended by W34-2.2 (the writer's
decode confirmation, the record-update/scalar rules it implements, and the
insert refusal). Companion to the pair reports under [`evidence/`](evidence/).
Static evidence is from the git-ignored Ghidra model. No dynamic trace this
wave (W34-0.4 decision)._

## Rule

`114/Props` (back-end document) and `214/Props` (front-end document) are
property streams in the same format as the per-container `Props` streams. The
decoder in `core/props` is the reference for the layout; this note adds what
the save side does on edit.

```
header, 16 bytes
  +0   u32  stream_size - 4      // 114/Props and 214/Props
  +4   u32  stream_size - 4
  +8   u32  format word          // 0x00010000 measured on 114/214
  +12  u32  declared entries     // NOT the top-level record count
records, back to back, no padding
  u32 payload_length
  u32 id                         // byte 2 is 0x40 on every measured id
  u32 kind                       // 0, 2, 4, 9 measured
  byte[payload_length] payload
tail
  begins at the first offset that is not a record header; the rest of the
  stream is the tail
```

The tail begins with a typed-property prefix: 16-byte entries `(lead 4, key,
lead 2, value)`. `114/Props` also carries a field-reference map: 28-byte
entries found by scanning the tail every 2 bytes for a `0x0b40xxxx` id at
`entry + 8`, with the index word at +0, a length word at +4 constrained to
4..4096, the field id at +8, and four more u32 words at +12..+24. The
reference 114/Props fixture has 161 records, a tail from 3,316 or 3,320
depending on the file, 6 typed properties and 1,312 field references; 214/Props
has 47 records, 6 typed properties and no field references.

On edit Project rewrites the whole stream:

- a changed record keeps its id, kind and position; its payload keeps its
  length or grows (4 -> 24 and 2 -> 6 in the WBS pair; 2 -> 2 and 8 -> 16 in
  the field-set pair), and every later record and the tail move by the growth
  (there is no padding to absorb it). No measured pair shrinks a payload, and
  no measured update changes a record's kind;
- both size words and the tail offset move with the stream;
- the typed property key `0x00010001` in `114/Props` mirrors the owning Task
  table's `Var2Data` size and moves with it;
- the field-reference sequence is rewritten in place and, in the WBS edit, was
  identical before and after.

That is the whole measured save-side shape: **update a record that already
exists**. No measured pair inserts a directory-level record: the WBS and
field-set pairs keep 161 `114/Props` records on all three sides, and the one
pair whose stream grows (`wave30-baseline`, +110 B) grows its undecoded tail,
not its record area. The custom-field SET's insertion is a record inside the
per-table `114/TBkndTask/Props` and `114/TBkndAssn/Props` streams, which this
format does not cover. The writer therefore refuses insertion on both
directory streams (W34-2.2, below).

## Evidence

### Measured (pairs)

- `wave30-wbs` — `114/Props` 88,724 → 88,748 B (+24). Tail offset 3,316 →
  3,340; tail size 85,408 B on both sides (the tail moved, it did not grow).
  Two records are the edit's effects:
  - id `0x0240138b`, kind 0, payload 4 → 24 B:
    null `00 00 00 00`, edit
    `01 00 00 00 00 00 02 00 2e 00 00 00 00 00 00 00 07 00 00 00 57 00 00 00`;
  - id `0x024013be`, kind 0, payload 2 → 6 B: `00 00` →
    `57 00 00 00 00 00` (record 2,908 → 2,928, moved by the +20 growth of
    the record before it).
  Two more records differ from the NULL save only at masked positions and are
  per-save noise, not effects:
  - id `0x024013b1`, kind 4, payload 4 → 4 B: control and edit both
    `28 2d 18 3d`; the null's first byte is `32` at stream offset 2,626,
    inside the masked run [2626:2627);
  - id `0x024013c2`, kind 0, payload 16 → 16 B: control `8b ee 55 2b …`,
    null `a1 08 e1 13 …`, edit `8b ee 55 3e …`; every differing byte is in
    the masked run [2836:2840).
  The pair report prints all four as `EFFECT: CHANGED` because, after the +20
  growth, every later record's byte span contains a raw effect position from
  the shifted suffix; its span test fires even though the payload difference
  itself is masked. The mask is authoritative: the writer need reproduce only
  `0x0240138b` and `0x024013be`, and no writer reproduces a masked position.
  The report's own "noise: 2" line counts the two further records
  (`0x02400007`, `0x02400029`) whose payload differs only in the masked
  save value at stream offsets 434 and 898. The measured writer consequence is
  exact: a whole-stream positional compare flags ONE byte — 2,859, our `2b`
  (the control-copied per-save byte of `0x024013c2`) vs Project's `3e`, at a
  position the shift moved out of the masked run — while the field-wise
  compare below has zero mismatches.
  Typed property `0x00010001` 512 → 552 (the Task `Var2Data` size);
  field-reference sequence identical (1,312 entries).
  The two size words move 0x…90 → 0x…a8, i.e. by the same +24 as the stream,
  confirming `stream_size - 4`.
- `wave30-notes` — `114/Props` unchanged in size; typed property
  `0x00010001` 516 → 718; two records changed only at masked positions. The
  notes edit adds no record: it is a var-store edit whose size is mirrored in
  the tail property.
- `wave30-baseline` — `114/Props` 88,734 → 88,844 B (+110): 34 effect bytes
  plus 110 tail-effect bytes. A baseline set writes project-level state, not
  just the task row.
- `wave30-calculate` — `114/Props` 88,736 B on all three sides, 6 effect
  bytes, 10 noise bytes: a recalculation rewrites values in records without
  changing the stream size.
- `wave30-respect-links-*` — `114/Props` unchanged except masked noise; the
  edit's only effect in the whole file is one byte in `Props14`.
- `214/Props` — 13,102 B and 47 records in every measured pair; the only
  measured differences are at masked positions (8–9 noise bytes per pair).
  No edit in the corpus inserts a front-end record.

### Static (Ghidra model)

- `FUN_140a495f0` @ `0x140a495f0` — logged as "Writing project custom props
  for helem"; this is the project-element property writer, the candidate for
  the document-level `Props` content (records and their ids).
- `FUN_140a448ac` @ `0x140a448ac` (3,009 B) — writes a container `Props`
  stream: a 2-byte marker `0xee`, then a props blob sized by
  `FUN_140a5e85c`, with "Container, prop size" logging. It also handles the
  nested-storage case (`StgCreateDocfileOnILockBytes` + `CopyTo`), which is
  why a Props stream can contain a whole storage.
- `FUN_140a45900` @ `0x140a45900` (728 B) — per-container dispatch on save:
  container type 1 → `FUN_140a49e1c`, 2 → `FUN_140a4a194`, 3 → `FUN_140a4b65c`,
  4 → `FUN_140a495f0` ("Writing custom props failed" on each branch), then a
  storage flush. `FUN_140a50264` is its caller for the table container
  classes.
- `FUN_140a3a900` @ `0x140a3a900` — reads a props blob ("ReadPropsFromBLob
  failed", "ReadPropsFromBlob incorrect PSIProp count");
  `FUN_140a3a244` @ `0x140a3a244` — "PID in Container Props Stream:" and
  "Loading Props from Stream for Container:". These are the read counterparts
  and confirm that a props stream is a counted set of (pid, value) blobs.
- `FUN_140a37390` @ `0x140a37390` (2,194 B) — validates the fixed-property
  map on load. It walks 28-byte entries, logs "found new fixed property which
  serializer doesn't know about" and "found inconsistent fixed property" with
  named fields (`loadedProperty`, `loadedOffset`, `loadedVisible`,
  `projectProperty`, `projectOffset`, `projectVisible`, `container`). This is
  static proof that a 28-byte property-map entry couples a field id, an offset
  and a visibility flag — the same shape as the `114/Props` field-reference
  entries. The in-memory field positions differ from the on-disk entry (the
  disk id is at +8, the in-memory property id at +0xc), so the two are
  related, not byte-identical.
- `FUN_140a3f8ac` — "ReadVarDataFromBin detected corrupt dwPropSize";
  `FUN_140a38ca0` — "ReadFixedprops failed". Both are read-side guards that
  show Project treats these sizes as authoritative.

### COM facts

The WBS pair applies
`WBSCodeMaskEdit("W", Level=1, Sequence=ordered numbers(0), Length=2,
Separator=".", CodeGenerate=True, VerifyUniqueness=True)` then
`WBSCodeRenumber(All=True)` through `scripts/oracle_lock.py`. The records the
edit changes are the WBS code-definition records; no COM call wrote
`114/Props` directly.

### W34-0.5: the custom-field SET and CLEAR pairs

The synthetic field pairs ([set](evidence/wave34-field-set.md),
[clear](evidence/wave34-field-clear.md)) answer the add/delete record
questions below:

- **SET** (`Task.Text1` on a task with no value): Project appends ONE 88-byte
  registration record to BOTH `114/TBkndTask/Props` (86 -> 174) and
  `114/TBkndAssn/Props` (88 -> 176), and edits the directory-level
  `114/Props` (record `0x02401392` kind 2 keeps its 2-byte payload,
  `00 00` -> `01 00`; record `0x024013c5` kind 0 8 -> 16 bytes,
  `00 00 00 00` -> `00 00 00 00 01 00 00 00 33 00 40 0b 64 40 40 0f`;
  typed-property key `0x00010001` 530 -> 574). The top-level record count is
  161 on both sides: the registration is a record INSIDE an existing Props
  record, not a new top-level record. (The report also prints record
  `0x024013c2` as `EFFECT: CHANGED`; its differing bytes sit at stream
  offsets 2,854..2,857 inside the masked run [2854:2858), so it is per-save
  noise exactly like the WBS pair's. Its stream-level consequence: the
  whole-stream positional compare flags byte 2,865, our `c6` vs Project's
  `d9`, at the same shifted-out-of-the-mask seam; the field-wise compare has
  zero mismatches.)
- **CLEAR** (`Task.Text1 = ""`): both Props streams are BYTE-IDENTICAL to the
  control. Clearing a value does not remove the registration, so there is no
  measured "delete a custom field" record shape; the editor's clear is a
  VarMeta removal (see [`var-meta.md`](var-meta.md)).

The port's SET writes the 88-byte record to `114/Props` too, where Project
writes 8 bytes (measured: 51662 mismatches from byte 0, port `0xa2` vs
Project `0xaa`), and this pair's effects are recorded as a named expected
failure in `tests/roundtrip_field_values.cpp`.

## W34-2.2: the writer

`core/mppwrite/props_edit.{h,cpp}` (`save_props_edit`) implements the two
measured shapes and refuses everything else up front, in the edit model,
before the document or the output path is touched.

**Record update, `114/Props`.** The id must already exist; its kind must
match; the new payload must be equal to or longer than the stored one. The
record keeps its position, every later record and the tail move by the
growth, both size words become `new_size - 4`, and the header's +8 and +12
words are preserved verbatim. Measured: `0x0240138b` 4 -> 24 and
`0x024013be` 2 -> 6 (wave30-wbs-code), `0x02401392` 2 -> 2 and
`0x024013c5` 8 -> 16 (wave34-field-set).

**Scalar update.** One 16-byte `[LE u32 4][BE tag][LE u32 4][LE u32 value]`
entry in the tail: only the value word changes, the stream length does not.
The whitelist is per stream:

| stream | tags (BIG-endian as stored) | measured pairs |
|---|---|---|
| `114/Props` | `0x01000100` Task size, `0x01000002` Task freed, `0x02000001` Resource count, `0x02000100` Resource size, `0x03000001` Calendar count | wbs 512 -> 552, field-set 530 -> 574, Add Resources 5 -> 6 / 86 -> 140 / 7 -> 8 |
| `214/Props` | `0x01000100` only | textstyle 100,178 -> 100,234 |

The scan requires exactly one hit in the tail, so an ambiguous tag is refused
rather than guessed. A record edit and a scalar edit may be combined in one
save, as the WBS and field-set pairs do.

**The Props14 mirror.** Every save writes the Props14 edit marker
(edit_marker.h), as every measured editor save does; the writer proves from
disk that no stream outside {the edited Props stream, Props14} changed.

**Refused, with the evidence of the attempt.**

- **Insert** (a target id absent from the stream), on both streams. The WBS
  and field-set pairs keep 161 records on all three sides; the one pair whose
  `114/Props` grows is `wave30-baseline` (+110 B), where the count stays 161,
  the +12 word moves 262 -> 263 and the growth sits in the undecoded tail —
  a section this note cannot rebuild. The field-set 88-byte registration is
  an insert inside the per-table `114/TBkndTask/Props` and
  `114/TBkndAssn/Props` streams, not here.
- **Record edits on `214/Props`.** Every measured pair has 47 records and
  13,102 B there; the only differences are masked noise. No pair changes a
  front-end record.
- **Shrink, kind change, no-op.** No pair shrinks a payload, every measured
  update keeps the kind, and an equal payload or value is not an edit.
- **An unlisted scalar tag**, a duplicate request, a tag absent or present
  twice, a hit outside the tail, a stream other than the two, a damaged
  header or record walk, and an unusable output path are all refused.

**How the tests compare a growing stream.** Both measured record pairs also
change table streams this writer does not own, so the tests are stream-level
on `114/Props`: the header words, the record list (id, kind, offset), every
record payload and the tail are each compared with Project's edit save under
the control-vs-null mask (`save_pair::compare`, effects assertion on for the
fields the edit changes). A single whole-stream positional compare is NOT
used: when a record grows mid-stream, every later byte moves, and the
positional mask no longer covers the per-save identity bytes those records
carry. Measured: it flags exactly one byte per pair — byte 2,859 (our `2b`
vs Project's `3e`, record `0x024013c2`) and byte 2,865 (our `c6` vs `d9`,
the same record) — both control-vs-null noise at the record's own offsets
(masked runs [2836:2840) and [2854:2858)). The field-wise compare has zero
mismatches and accounts for every byte: 16 header bytes, the 161 record
headers and payloads, and the tail.

## Limits

- The routine that emits the `length/id/kind/payload` record framing was not
  isolated within the time box. It is reached only through container-specific
  vtable calls, and the decompiled model exposes the props blobs as opaque
  buffers. The record layout is therefore measured (`core/props`) plus the
  pair evidence, not statically proven.
- `declared entries` at header +12 is not the record count. Its meaning is
  undecoded. It was preserved verbatim through every measured record update
  (262 in the reference `114/Props`) and moved only in `wave30-baseline`
  (262 -> 263), the one pair whose directory stream grows, whose +110 B sit
  in a tail section this note cannot rebuild.
- The typed-property prefix's other keys and the meaning of the field
  reference's four trailing words are undecoded. Only `0x00010001` is tied to
  a measured quantity (the Task Var2Data size).
- Record insertion is measured (W34-0.5, SET): one 88-byte record appended to
  each of the two per-table Props streams. In the directory streams the WBS
  edit grows two existing records and inserts none, the field-set edit grows
  one and re-values one, and the top-level count is 161 on all three sides of
  both pairs. No measured pair inserts a directory record; the writer refuses
  insertion there (W34-2.2).
- Whether a 28-byte tail field-reference entry and the 28-byte in-memory
  property map are the same record at different offsets is inferred from the
  shape, not proven.

## Pairs still needed

1. ~~**Add a custom field**~~ measured by W34-0.5 (the SET pair): Project
   appends one 88-byte record to each per-table Props stream.
2. ~~**Delete a custom field**~~ the editor's "delete" is a value clear, and
   the CLEAR pair shows both Props streams byte-identical. No measured
   editor command removes a registration record; a file whose record another
   tool removed is unmeasured.
3. **A WBS scheme with a different separator and length** (e.g. Level=2,
   Length=3): confirms whether `0x0240138b` grows by (length + fixed) and
   whether `0x024013be` tracks it.

No dynamic trace this wave (W34-0.4 decision). The record-framing writer is a
live-trace candidate: a breakpoint on the record-length write would settle
the framing routine without a new pair.
