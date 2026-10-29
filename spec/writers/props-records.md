# `114/Props` and `214/Props` record insertion

_W34-0.2 decode note, 30 October 2026. Companion to the pair reports under
[`evidence/`](evidence/). Static evidence is from the git-ignored Ghidra
model. No dynamic trace this wave (W34-0.4 decision)._

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

- a changed record keeps its id and position; its payload length may grow or
  shrink, and every later record and the tail move by that delta (there is no
  padding to absorb it);
- both size words and the tail offset move with the stream;
- the typed property key `0x00010001` in `114/Props` mirrors the owning Task
  table's `Var2Data` size and moves with it;
- the field-reference sequence is rewritten in place and, in the WBS edit, was
  identical before and after — insertion of a new record is the only case
  where the map's entry count can change, and none is measured.

For an insertion, the rule the port must follow is: append or splice whole
records only, keep the record order the file had, rewrite both size words to
`new_stream_size - 4`, and do not touch the tail's internal layout.

## Evidence

### Measured (pairs)

- `wave30-wbs` — `114/Props` 88,724 → 88,748 B (+24). Tail offset 3,316 →
  3,340; tail size 85,408 B on both sides (the tail moved, it did not grow).
  Four records change:
  - id `0x0240138b`, kind 0, payload 4 → 24 B:
    null `00 00 00 00`, edit
    `01 00 00 00 00 00 02 00 2e 00 00 00 00 00 00 00 07 00 00 00 57 00 00 00`;
  - id `0x024013be`, kind 0, payload 2 → 6 B (text-like, not shown);
  - id `0x024013b1`, kind 4, payload 4 → 4 B: `32 2d 18 3d` →
    `28 2d 18 3d`;
  - id `0x024013c2`, kind 0, payload 16 → 16 B: first four bytes
    `a1 08 e1 13` → `8b ee 55 3e` (the rest of the 16-byte GUID identical).
  Typed property `0x00010001` 512 → 552 (the Task `Var2Data` size);
  field-reference sequence identical (1,312 entries). Two further records
  change only at masked positions (the save-path bytes).
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

## Limits

- The routine that emits the `length/id/kind/payload` record framing was not
  isolated within the time box. It is reached only through container-specific
  vtable calls, and the decompiled model exposes the props blobs as opaque
  buffers. The record layout is therefore measured (`core/props`) plus the
  pair evidence, not statically proven.
- `declared entries` at header +12 is not the record count. Its meaning is
  undecoded; it was identical on both sides of every measured edit.
- The typed-property prefix's other keys and the meaning of the field
  reference's four trailing words are undecoded. Only `0x00010001` is tied to
  a measured quantity (the Task Var2Data size).
- Record insertion is unmeasured: the WBS edit grows four existing records and
  inserts none. The count of top-level records was 161 on both sides.
- Whether a 28-byte tail field-reference entry and the 28-byte in-memory
  property map are the same record at different offsets is inferred from the
  shape, not proven.

## Pairs still needed

1. **Add a custom field** through COM on the synthetic base: does Project
   append a new record (and a field-reference entry), or extend an existing
   one? One factor: insertion.
2. **Delete a custom field**: does the record disappear, and does the
   field-reference map lose its entry?
3. **A WBS scheme with a different separator and length** (e.g. Level=2,
   Length=3): confirms whether `0x0240138b` grows by (length + fixed) and
   whether `0x024013be` tracks it.

No dynamic trace this wave (W34-0.4 decision). The record-framing writer is a
live-trace candidate: a breakpoint on the record-length write would settle
the framing routine without a new pair.
