# The trace gate's named noise fields (wave 36 G0b)

Decoded from the 99 committed control / control-null pairs in `tests/data` (same machine, same
session, so a field that differs between a control and its null save is per-save noise) and from
the earlier trace notes named in the last column.  `trace_gate.py` cites a row of this table by
line number for every mask it keeps.  A mask names a field, never a region.

A version-1 GUID (UuidCreateSequential) is stored in RFC byte order: byte 6 >> 4 == 1 and byte 8 >> 6 == 2.  A GUID field is masked only when both sides hold a version-1 GUID; all zeros against a real GUID FAILs (W36-0c.2, R36G0b-4: in the 196 control/null pairs no GUID is zero on one side only).

| Field | Where | Width | Noise because | Earlier note |
|---|---|---|---|---|
| Assn Fixed2Data GUIDs | 114/TBkndAssn/Fixed2Data row (48 bytes) +0 and +16 | 16 each | version-1 GUIDs, regenerated at every save; +32..+47 is equal in the VM file and the pair, so it is data | trace-insert-milestone-r4.md:9, :11-14 |
| Task Fixed2Data GUIDs | 114/TBkndTask/Fixed2Data row (64 bytes) +0 and +24 (some rows hold zeros at +24) | 16 each | version-1 GUIDs; +42 and +46..49 are a flags byte and the Calculate save stamp, data, not masked | trace-insert-summary.md:42 |
| Rsc Fixed2Data GUIDs | 114/TBkndRsc/Fixed2Data row (40 bytes) +0 and +24 | 16 each | version-1 GUIDs | trace-insert-summary.md:42 |
| Cal Fixed2Data GUID | 114/TBkndCal/Fixed2Data row (16 bytes) +0 | 16 | version-1 GUID | trace-insert-summary.md:42 |
| Cons Fixed2Data GUID | 114/TBkndCons/Fixed2Data row (16 bytes) +0 | 16 | version-1 GUID | trace-insert-summary.md:42 |
| View stamps | 214/CEdl/FixedData +144; CFilter and CGrouping FixedData +214 and +444; CTable FixedData +214; CV_iew FixedData +122, +260 and +398 | 16 each | version-1 GUIDs saved with the view | trace-null.md:27-33 |
| Props save words | 114/Props records 0x02400007 and 0x024013b1, payload +0, 4 bytes | 4 | the save date-time word; equal to each other in a file | trace-props-framing.md:13 |
| Props save GUIDs | 114/Props records 0x02400029 and 0x024013c2, payload +0, 16 bytes | 16 | version-1 GUIDs, per save | trace-props-framing.md:15-16 |
| Props file identity | 114/Props record 0x02401394 | 16 | NOT noise: per file, unchanged between a control and its null save; not masked | trace-props-framing.md:17 |
| View GUID string | 214/Props record 0x03400001, UTF-16 `{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}` at payload +0 (76 bytes; the rest of the record is data) | the 32 hex digits | the view GUID, per save; the braces, dashes and high bytes are data | trace-null.md:35 |
| New Task row creation stamp | 114/TBkndTask/FixedData row (202 bytes from 48) +128, 4 bytes | 4 | the creation date-time of a row; only a row whose uid (row +0) is not in the control; an existing row's is stable between a control and its null save | trace-insert-summary.md:42 |
| New assignment creation stamp | 114/TBkndAssn/Var2Data, the key 0x0f40027a block (4 bytes) located through VarMeta | 4 | the creation date-time of an assignment; only a block whose VarMeta uid is not in the control | trace-insert-milestone-r4.md:16-20 |
| Font word top byte | 214/CV_iew/Var2Data font item (0x2240000c) payload +43 of each 44-byte row | 1 | uninitialised allocator byte | trace-font-item-r6.md |

## Row identity of the fixed-stride streams (W36-0b.8, R36G0-9)

The control mask is moved row by row onto the reference row that carries the same identity
(`IDENT`, `row_ids`).  Decoded from all 332 committed files; no row count or layout was guessed.

| Stream | Identity of a row | Layout |
|---|---|---|
| Task FixedData | uid word at row +0 | 48-byte header, then 202-byte rows |
| Task Fixed2Data | uid of the Task FixedData row it follows | 3 header rows, then one 64-byte row per FixedData row (row count checked against FixedData) |
| Assn FixedData | uid word at row +0 | 110-byte rows |
| Assn Fixed2Data | uid of the Assn FixedData row | 48-byte rows, one per FixedData row |
| Rsc Fixed2Data | uid word at +0 of the Rsc FixedData row | Rsc FixedData: 48-byte header, then 172-byte rows; Fixed2Data 3 header rows, then 40-byte rows |
| Cal Fixed2Data | uid word at +8 of the Cal FixedData item, and the row's place (0..2) within it | Cal FixedData: 64 bytes of header items, then 12-byte items; Fixed2Data 12 header rows, then 3 16-byte rows per item |
| Cons Fixed2Data | uid word at +0 of the Cons FixedData item, and the row's place (0..2) | Cons FixedData 20-byte items; Fixed2Data 3 16-byte rows per item |

The Fixed2Meta/FixedMeta streams are not used: the Assn meta lists only some rows, so its item
count is not the row count.  A stream whose Fixed2Data row count does not fit its FixedData is
not named (no mask).  A uid that two rows carry (a deleted Task row that stays, two Rsc rows in
two files) leaves those rows unmasked rather than guessed.
