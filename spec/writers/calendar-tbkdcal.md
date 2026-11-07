# The `TBkndCal` resource-calendar row writer

_W34-0.2 decode note, 30 October 2026; corrected and extended with the
W34-0.3 measurements. Companion to the pair reports under
[`evidence/`](evidence/). Static evidence is from the git-ignored Ghidra
model. No dynamic trace this wave (W34-0.4 decision)._

## Rule

`114/TBkndCal` is the calendar table. With `n` live calendar rows it is
`FixedData` = 64 + 12·n, `FixedMeta` = 16 + 10·(n+4), `Fixed2Data` =
48·(n+4), `Fixed2Meta` = 16 + 10·(n+4), plus `VarMeta`/`Var2Data`. The `+4`
is the same spare-slot allowance the other tables carry (see
[`table-growth.md`](table-growth.md)). In the measured files the four spare
Fixed2Data slots are at indices 0..3 and the live calendar records follow at
4..4+n-1.

Adding a calendar appends every part; nothing is spliced:

- **FixedData**: one 12-byte row at `64 + 12·n`. Two producers write
  different rows — Set Baseline writes `00 00 00 00 ff ff ff ff 04 00 00 00`
  and Add Resource writes `01 00 00 00 <resource uid> <calendar uid> 00 00 00`
  (`01 00 00 00 02 00 00 00 04 00 00 00` for the first added resource,
  `01 00 00 00 03 00 00 00 05 00 00 00` for the second). The field mapping of
  the other words is not decoded (see Limits), so the port copies the measured
  rows only for the two measured producers.
- **FixedMeta**: one 10-byte entry at `16 + 10·(n+4)`, count word +1. The
  measured baseline entry is `00 00 01 00 64 00 00 00 af 00`: a `0x0001`
  lead word, the row's FixedData offset (`0x64` = 100 = 64 + 3·12), and a
  `0x00af` trailer. Resource entries have a zero lead word and a `0x000e`
  trailer: the first added resource is `00 00 00 00 4c 00 00 00 0e 00`, the
  second is `00 00 00 00 58 00 00 00 0e 00`, and the third (measured in
  `wave34-resource-add-third`) is `00 00 00 00 70 00 00 00 0e 00` — the
  FixedData offset is `0x40 + 0x0c·index`.
- **Fixed2Data**: one 48-byte slot at `48·(n+4)`, count in Fixed2Meta +1.
  Three shapes are measured:
  - Set Baseline's new slot is ONE 16-byte identity then 32 zero bytes:
    `8b ee 54 c4 d1 15 11 f1 9f 64 1c c1 0c 05 4c 03` + zeros.
  - An existing resource-calendar slot is THREE 16-byte GUIDs. The first is
    stable across saves; the second and third are save-generated v1-style
    GUIDs whose timestamps advance on every save (control-vs-null noise).
  - Add Resource's new slot is also three 16-byte GUIDs: the first two are the
    same fresh save-time identity and the third is the shared save-time GUID
    also written into the other resource slots. Example, the third resource's
    slot 8: `5e b1 4c 15 d3 f4 11 f1 9f 66 1c c1 0c 05 4c 03` twice, then
    `5e b1 4c 12 d3 f4 11 f1 9f 66 1c c1 0c 05 4c 03`.
  The new slot lies in the unmasked tail of the save-pair comparison, so a
  port save can only match if the comparison normalises the identity or the
  writer reproduces Project's time-based value (see Limits and the G3.2
  hand-off in the brief).
- **Fixed2Meta**: one 10-byte entry, measured
  `00 00 00 00 50 01 00 00 0e 00` for the first added resource — the slot's
  Fixed2Data offset (`0x150` = 336 = 48·7) and a `0x000e` trailer. The third
  resource's entry is `00 00 00 00 80 01 00 00 0e 00` (offset 384 = 48·8).
- **VarMeta**: Set Baseline adds one 12-byte entry at its sorted `(uid, key)`
  position, key `0x0d400001` (Calendar/Name), offset = the previous Var2Data
  length; count word +1 and `var2_bytes` updated. **Add Resource adds no
  Cal VarMeta entry and no Var2Data block** — the new resource calendar has
  no stored name block in the measured pairs (the calendar's display name
  comes from the resource row).
- **Var2Data**: Set Baseline appends one block, `4 + payload`, whose payload
  is the calendar name as UTF-16LE plus a NUL: the 96-byte name
  `Used for Microsoft Project 98 Baseline Calendar\0`. The existing
  `Standard` block is 18 B (`S\0t\0a\0n\0d\0a\0r\0d\0\0\0`).

Measured deltas:

| stream | Set Baseline | Add Resource |
|---|---|---|
| FixedData | +12 | +12 |
| FixedMeta | +10 | +10 |
| Fixed2Data | +48 | +48 |
| Fixed2Meta | +10 | +10 |
| VarMeta | +12 | 0 |
| Var2Data | +4 + name payload (100 B) | 0 |

A no-edit save changes none of the calendar streams; the only noise is the
save-generated identity dwords inside the existing slots.

## Evidence

### Measured (pairs)

- `wave30-baseline` (Set Baseline 0 for the project) — Cal FixedData
  100 → 112, FixedMeta 86 → 96, Fixed2Data 336 → 384, Fixed2Meta 86 → 96,
  VarMeta 36 → 48, Var2Data 22 → 122. The FixedData walk reports
  "rows control 3, null 3, edit 4; row 3 uid 4 is NEW:
  `00 00 00 00 ff ff ff ff 04 00 00 00`"; the new FixedMeta entry is
  `00 00 01 00 64 00 00 00 af 00`. Fixed2Data noise is 12 B at [272:276),
  [304:308) and [320:324), the three per-save identity dwords; every other
  calendar stream has zero noise. The VarMeta header goes
  `(0xFADFADBA, 0, 1, 0, 0, 22)` → `(0xFADFADBA, 0, 2, 0, 0, 122)` and the
  new entry is `[1] uid 4 key 0x0d400001 off 22`.
- `wave30-resource-add` (one work resource added) — FixedData 100 → 112 with
  the new row `01 00 00 00 02 00 00 00 04 00 00 00`, FixedMeta 86 → 96,
  Fixed2Data 336 → 384, Fixed2Meta 86 → 96. The new Fixed2Meta entry is
  `00 00 00 00 50 01 00 00 0e 00`, and the new slot 7 is
  `89 b9 c4 21 d1 0a 11 f1 9f 64 1c c1 0c 05 4c 03` twice then
  `89 b9 c4 1f d1 0a 11 f1 9f 64 1c c1 0c 05 4c 03`. **Cal VarMeta and
  Var2Data are byte-identical in this pair** (36 B / 22 B); the earlier
  draft of this note wrongly attributed the baseline pair's name-block growth
  to Add Resource. The same save also grows the Resource table and rewrites
  `114/Props` (see [`table-growth.md`](table-growth.md)).
- `wave34-resource-add-third` (a SECOND added resource, on top of
  `wave30-resource-add.mpp`) — FixedData 112 → 124, FixedMeta 96 → 106,
  Fixed2Data 384 → 432, Fixed2Meta 96 → 106; Cal VarMeta and Var2Data
  unchanged (36 B / 22 B). COM: `Resources.Count` 2 → 3, new resource
  ID/UniqueID 3, Work, MaxUnits 1.0. The new row is
  `01 00 00 00 03 00 00 00 05 00 00 00` (uid 5, so the calendar uid allocation
  is 4 → 5 as the wave-30 pair predicted), the new FixedMeta entry is
  `00 00 00 00 70 00 00 00 0e 00`, the new Fixed2Meta entry is
  `00 00 00 00 80 01 00 00 0e 00`, and the new slot 8 is the three-GUID
  identity quoted above. The existing slots 6 and 7 keep their stable first
  GUID and have their second and third GUIDs replaced by the save
  (`…4b fd` → `…4b 13`, then `…4c 12`), which is the masked noise.
- Control vs null: every calendar stream is byte-identical except the masked
  identity dwords, so the per-save noise is confined to those slots.

### Static (Ghidra model)

- `core/mpp/reader.cpp` lines 259–290 state the same geometry independently:
  Cal FixedData 64 + 12·rows, FixedMeta 16 + 10·(rows + 4), Fixed2 48·(rows +
  4), Fixed2Meta 16 + 10·(rows + 4).
- `FUN_140a4fa18` @ `0x140a4fa18` — the container save loop; `TBkndCal` is the
  back-end class with 8 entries. `FUN_140a50264` @ `0x140a50264` enumerates
  TBkndCal as class 8 and calls `FUN_140a45900` to save it. These are the
  plumbing that reaches the table writer, not the row producer.
- `FUN_1408ad2d4` @ `0x1408ad2d4` builds the six table streams and binds them
  to the table object; `FUN_1408b05ac` @ `0x1408b05ac` is the model's
  compacting table serialiser (see the mismatch recorded in
  [`var-meta.md`](var-meta.md)).
- The calendar-row producer — the Set-Baseline and Add-Resource handlers that
  create the 12-byte row, the 48-byte slot and (for baseline) the name block —
  was not isolated within the two-hour box. The row's field words are read by
  the table layer, not by a calendar-specific function in the decompiled
  model.

### COM facts

The baseline pair uses `Application.BaselineSave` (baseline 0, entire
project); the resource pairs use `Resources.Add` (one work resource, then a
second). All are single editor commands through `scripts/oracle_lock.py`; no
COM call touched a calendar stream directly.

## Writer (W34-3.2)

`core/mppwrite/resource_growth.{h,cpp}` (`add_work_resource`) writes the Add
Resources shape for the two measured counts (resource table rows 2 and 3) and
refuses every other count: the Rsc FixedMeta `+13` and Fixed2Meta `+8` bytes
are measured per count, not decoded. The identity question under Limits is
settled by `save_pair::TailNormalisation`, which masks only the first dword of
each identity in the appended Cal slot (three) and Rsc Fixed2Data record
(two); every other appended byte is compared against Project's own save. The
writer derives the family counter as the greatest counter in the family + 1,
and both pairs agree with that outside the masked dwords. The test is
`tests/roundtrip_resource_growth.cpp`.

## Limits

- The 12-byte row's field mapping is not decoded. Three producers are
  measured; the Add Resource rows differ from the baseline row in the first
  three words.
- The new 16-byte identity is generated at save time and is not derivable
  from the input. Existing identity dwords are masked noise; the appended
  slot's identity falls in the unmasked tail, so the save-pair comparison
  will only match if the writer reproduces Project's value or the comparison
  normalises it. Settled in W34-3.2 by normalising the appended identities'
  first dwords (see Writer above).
- Only calendar addition is measured. Deletion, rename, base-calendar change
  and a second resource's calendar are still partly open: the second added
  resource is measured (`wave34-resource-add-third`), the third is not.
- The name payload is measured for two names (18 B and 96 B), both from the
  baseline producer; the length rule (2 × (len + 1)) is an inference, not
  tested with an odd-length name, and Add Resource writes no name block at
  all.
- The `+4` slack means a new slot can occupy an existing spare rather than
  extend the stream; no measured pair exercises the boundary.
- Why the existing resource slot's second and third GUIDs change on every
  save is not decoded (the values look like v1 UUID timestamps); the port
  only knows they are masked noise.

## Pairs still needed

1. **Rename a resource's calendar** (Resource Information, calendar name):
   does a name block appear or grow in place, and does a Cal VarMeta entry
   move?
2. **Delete the resource**: does the calendar row/slot disappear or become a
   tombstone, as assignments do?
3. **A calendar name of odd length** to confirm the UTF-16 + NUL length rule.
4. **A fourth resource** to confirm the FixedData row and Fixed2Meta entry
   arithmetic beyond index 8.

No dynamic trace this wave (W34-0.4 decision). The open rules here are all
pair-measurable (rename, delete, an odd-length name, a fourth resource). The
save-time identity generator is not a shape question a file-write trace can
settle — the GUIDs exist before serialisation — and the G3.2 blocker is the
comparison's identity masking, not the decode.
