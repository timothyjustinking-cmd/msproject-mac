# The `TBkndCal` resource-calendar row writer

_W34-0.2 decode note, 30 October 2026. Companion to the pair reports under
[`evidence/`](evidence/). Static evidence is from the git-ignored Ghidra
model. No dynamic trace this wave (W34-0.4 decision)._

## Rule

`114/TBkndCal` is the calendar table. With `n` live calendar rows it is
`FixedData` = 64 + 12·n, `FixedMeta` = 16 + 10·(n+4), `Fixed2Data` =
48·(n+4), `Fixed2Meta` = 16 + 10·(n+4), plus `VarMeta`/`Var2Data`. The `+4`
is the same spare-slot allowance the other tables carry (see
[`table-growth.md`](table-growth.md)).

Adding a calendar appends every part; nothing is spliced:

- **FixedData**: one 12-byte row at `64 + 12·n`. Two producers write
  different rows — Set Baseline writes `00 00 00 00 ff ff ff ff 04 00 00 00`
  and Add Resource writes `01 00 00 00 02 00 00 00 04 00 00 00`. The second
  and third words track the owning resource and the new calendar's uid; the
  field mapping is not decoded (see Limits), so the port copies the measured
  rows only for the two measured producers.
- **FixedMeta**: one 10-byte entry at `16 + 10·(n+4)`, count word +1. The
  measured baseline entry is `00 00 01 00 64 00 00 00 af 00`: a `0x0001`
  lead word, the row's FixedData offset (`0x64` = 100 = 64 + 3·12), and a
  `0x00af` trailer.
- **Fixed2Data**: one 48-byte slot at `48·(n+4)`, count in Fixed2Meta +1.
  The baseline slot is a fresh 16-byte identity plus 32 zero bytes:
  `8b ee 54 c4 d1 15 11 f1 9f 64 1c c1 0c 05 4c 03` then zeros. The same
  save also rewrote the first dword of an existing slot's identity
  (`a1 08 e0 e5` → `8b ee 54 c3`); the two new identities differ by one in
  their first dword (`…54 c3`, `…54 c4`), so identities are generated per save
  and the existing ones are part of the file's masked noise.
- **Fixed2Meta**: one 10-byte entry, measured
  `00 00 00 00 50 01 00 00 0e 00` — the slot's Fixed2Data offset
  (`0x150` = 336 = 48·7) and a `0x000e` trailer.
- **VarMeta**: one 12-byte entry at its sorted `(uid, key)` position, key
  `0x0d400001` (Calendar/Name), offset = the previous Var2Data length; count
  word +1 and `var2_bytes` updated.
- **Var2Data**: one appended block, `4 + payload`, whose payload is the
  calendar name as UTF-16LE plus a NUL. Measured: the existing `Standard`
  block is 18 B (`S\0t\0a\0n\0d\0a\0r\0d\0\0\0`); the baseline writer's new
  calendar carries the 96-byte name `Used for Microsoft Project 98 Baseline
  Calendar\0`; Add Resource's new block is also 96 B.

Measured deltas, identical in both producer pairs:

| stream | delta |
|---|---|
| FixedData | +12 |
| FixedMeta | +10 |
| Fixed2Data | +48 |
| Fixed2Meta | +10 |
| VarMeta | +12 |
| Var2Data | +4 + name payload |

A no-edit save changes none of the calendar streams. A calendar deletion or
change is unmeasured.

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
- `wave30-resource-add` (one work resource added) — the same six deltas:
  FixedData 100 → 112 with the new row
  `01 00 00 00 02 00 00 00 04 00 00 00`, FixedMeta 86 → 96, Fixed2Data
  336 → 384, Fixed2Meta 86 → 96, VarMeta 36 → 48, Var2Data 22 → 122. The
  `Fixed2Data` noise and effect pattern is identical to the baseline pair.
  The same save also grows the Resource table and rewrites `114/Props` (see
  [`table-growth.md`](table-growth.md)).
- The FixedMeta and Fixed2Meta effect runs are `[8:9)` `07 → 08` (count) and
  `[12:13)` `0x50 → 0x80` (the declared FixedData/Fixed2Data size), plus the
  10 tail bytes that are the new entry. So both meta headers track the new
  high-water mark, not the live row count alone.
- Control vs null: every calendar stream is byte-identical except the masked
  identity dwords above, so the per-save noise is confined to those 12 bytes.

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
  create the 12-byte row, the 48-byte slot and the name block — was not
  isolated within the two-hour box. The row's field words are read by the
  table layer, not by a calendar-specific function in the decompiled model.

### COM facts

The baseline pair uses `Application.BaselineSave` (baseline 0, entire
project); the resource pair uses `Resources.Add` for one work resource. Both
are single editor commands through `scripts/oracle_lock.py`; no COM call
touched a calendar stream directly.

## Limits

- The 12-byte row's field mapping is not decoded. Only two producers are
  measured and their rows differ in the first two words; the port must not
  generalise a row from one sample.
- The new 16-byte identity is generated at save time and is not derivable
  from the input. Existing identity dwords are masked noise; the appended
  slot's identity falls in the unmasked tail, so the save-pair comparison
  will only match if the writer reproduces Project's value or the comparison
  masks it. This must be settled before G3.2 enables Add Resources.
- Only calendar addition is measured. Deletion, rename, base-calendar change
  and a second resource's calendar are unmeasured.
- The name payload is measured for three names (18 B and two 96 B); the
  length rule (2 × (len + 1)) is an inference, not tested with an odd-length
  name.
- The `+4` slack means a new slot can occupy an existing spare rather than
  extend the stream; no measured pair exercises the boundary.

## Pairs still needed

1. **A second resource added after the first** (W34-3.2): confirms the uid
   allocation (4 → 5) and whether the row's resource word tracks the new
   resource.
2. **Rename a resource's calendar** (Resource Information, calendar name):
   does the name block grow in place (view-style) or append (table-style),
   and does the VarMeta offset move?
3. **Delete the resource**: does the calendar row/slot disappear or become a
   tombstone, as assignments do?
4. **A calendar name of odd length** to confirm the UTF-16 + NUL length rule.

No dynamic trace this wave (W34-0.4 decision).
