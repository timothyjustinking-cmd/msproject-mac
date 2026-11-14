# The trace gate's named noise fields (wave 36 G0b)

Decoded from the 99 committed control / control-null pairs in `tests/data` (same machine, same
session, so a field that differs between a control and its null save is per-save noise) and from
the earlier trace notes named in the last column.  `trace_gate.py` cites a row of this table by
line number for every mask it keeps.  A mask names a field, never a region.

A version-1 GUID (UuidCreateSequential) is stored in RFC byte order: byte 6 >> 4 == 1 and byte 8 >> 6 == 2.  A GUID field is masked only when both sides hold a version-1 GUID; all zeros against a real GUID FAILs (W36-0c.2, R36G0b-4: in the 196 control/null pairs no GUID is zero on one side only).

| Field | Where | Width | Noise because | Earlier note |
|---|---|---|---|---|
| Assn Fixed2Data GUIDs | 114/TBkndAssn/Fixed2Data row (48 bytes) +0 and +16 | 16 each | version-1 GUIDs, regenerated at every save; +32..+37 and +41 differ between a control and its null with no edit (per-save values nobody decoded: not masked, so the pairs that show them are OPEN in the test; masking them would also pass an edit of the real field) | trace-insert-milestone-r4.md:9, :11-14 |
| Task Fixed2Data GUIDs | 114/TBkndTask/Fixed2Data row (64 bytes) +0 and +24 (some rows hold zeros at +24) | 16 each | version-1 GUIDs; +42 and +46..49 differ between a control and its null with no edit (wave30-baseline: +42 00 -> 20, +46..48 c0 12 f2 -> 78 1e f1): per-save values nobody decoded, not masked, so the pairs that show them are OPEN in the test | trace-insert-summary.md:42 |
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
| CEdl path record | 214/CEdl/Var2Data record 0x26400031, the last record: the length word and the payload plus its pad | 4 + payload | the file path, UTF-16 text ending `!` and NUL; the machine's, never the document's; noise only as printable text with that ending | trace-null.md:28 (602 -> 500) |
| CEdl size words | 214/CEdl/Var2Data +0, +4, +8 | 4 each | derived from the stream length: +0 = length - 4, +4 and +8 = length - 8; noise only when they say so | trace-null.md:28 |
| CEdl slot table | 214/CEdl/Var2Data record 0x26400024: the length word and the 0xffffffff words after the last uid (W36-0f.2) | 4 + the fill | the task uids in view order, then 0xffffffff up to an allocation capacity: 64 bytes in 280 committed files, 128 in 97 files with the same uids, 72 in the VM's saves, 36 to 144 in all (DOSSIER, CEdl slot-table capacity; a host control and its null differ in it in 6 of 223 pairs); the uids are data and are compared one by one; noise only for a whole number of words up to 1024 bytes (the bound is this note's, about 7 times the largest committed table) | DOSSIER, CEdl slot-table capacity |
| CEdl row record | 214/CEdl/Var2Data record 0x2640003d, the whole record (W36-0f.2) | 12 + 14 + 4 n | the view row positions in the form `[n][0][22][44]..[22 n][22][u16 0]` (459 of 592 committed records; the rest are longer and hold date words): derived from the slot table, whose uid count is n; present in one save only of 2 of the 223 control/null pairs (DOSSIER, CEdl record 0x2640003d); noise only in that form and with n equal to the uid count, anything else is compared | DOSSIER, CEdl record 0x2640003d |
| CEdl record count | 214/CEdl/Var2Data +16 | 4 | the number of records (hdr+12 of the DOSSIER's record list), derived: it differs only with the row record; noise only when it equals the number of records, and then the canonical form holds the number of records kept (W36-0f.2) | DOSSIER, CEdl record 0x2640003d |
| CEdl length word | 214/CEdl/VarMeta +20 | 4 | the length of 214/CEdl/Var2Data; noise only when it equals it | trace-null.md:29 |
| View CEdl length | 214/Props typed record 0x00010007 (4 bytes), payload +0 | 4 | the length of 214/CEdl/Var2Data again; noise only when it equals it | trace-null.md:28 |
| Props14 blob size words | Props14 +0 and +4 | 4 each | the blob size, derived (`size` = blob end - 4, both words, the blob header `0x10000` at +8); noise only when framed so | trace-null.md:37 (530 -> 464) |
| Props14 path record | Props14 record 0x35400008: the length word and the payload plus its pad | 4 + payload | a file path, UTF-16 text; noise only as printable text | trace-null.md:37 |
| Props14 tail names | Props14 tail `[count]` then entries `[id][byte length][UTF-16 name]`, ids 0xffffffff and 0x3540000b: the length word and the name bytes (W36-0f.1) | 4 + the name | a user name and its length, one machine field: the VM's name is 10 bytes, the host's 30, the committed 8-character author 16 (DOSSIER, size-differs Props14); the entry id is data; the entries chain by their length words exactly to the end of the tail; noise only as non-empty printable text of even length.  The count word is noise only when the 0xffffffff entry is absent on one side and the other tail is exactly one 0x3540000b entry with a text name (W36-0c.4) | trace-null.md:37 |
| Props14 record 0x3540000a | Props14 record 0x3540000a (2 bytes, the last record), payload +0; absent on one side, the record count word at +12 is 20 and not 21 (W36-0f.1) | 2 | a session flag: the committed control and its null differ in 9 of 223 pairs and every VM save holds 0 where the host edits hold 1; noise only as a u16 of 0 or 1 | DOSSIER, Props14 rec 0x3540000a |
| SummaryInformation strings | ?SummaryInformation properties 4 (Author), 8 (LastAuthor), 9 (RevNumber): the length word and the string | 4 + string | the user name; the slot is exactly type 0x1e, the length, the string, zero padding to 4 (W36-0c.3); the type word is data | trace-null.md:36, :46 |
| SummaryInformation LastSave | ?SummaryInformation property 13: the 8 bytes after the type word 0x40 of a 12-byte slot | 8 | the save FILETIME; property 12 (CreateTime) and every other property are data | trace-null.md:48 |
| SummaryInformation offsets | ?SummaryInformation section size word (section +0) and each table entry's offset word (+4 of the 8-byte entry) | 4 each | derived from the values laid back to back | trace-null.md:36 |

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

## Value bounds of the masked fields (W36-0d.6, R36G0c-6; W36-0e.2, R36G0d-2)

A named field is noise only when it holds a value a save writes, on both sides.  Decoded from the
624 committed .mpp files of tests/data (probes `build/w36/g0d_probe.py`, `g0d_probe2.py`, `g0e_probe.py`; the pairs, controls and nulls alike):

| Field | Bound in the gate | Committed values |
|---|---|---|
| Task new-row stamp, Assn new-block stamp, Props save words (`_stamp_ok`, `STAMP_SPAN`) | 32 bits inside 0x3d141482 to 0x3d282db4 (W36-0d.6 refused only 0 and all-ones, so a stamp of 1 or 0x7fffffff was noise; W36-0e.2) | 5835 values (Task +128 2700, Assn block 1887, Props words 1248), 0x3d141482 to 0x3d282db4 (Task and Assn to 0x3d282daa, the Props words to 0x3d282db4); none 0 or all-ones |
| Props save words 0x02400007 and 0x024013b1 (`_save_words_equal`) | equal to each other, on both sides (W36-0e.2) | equal in all 624 files (0 unequal) |
| SummaryInformation LastSave (`_filetime_ok`, `LASTSAVE_SPAN`) | a FILETIME inside 0x01dd62019f6a0e00 to 0x01dd72130aac6c00 (W36-0d.6 took any date after 2000, so 2001 and 2600 were noise; W36-0e.2) | 624 values, 0x01dd62019f6a0e00 to 0x01dd72130aac6c00 (22 October to 11 November 2026) |
| SummaryInformation Author, LastAuthor, RevNumber (`_lpstr_text_ok`) | a length of at least 1, ending in a NUL, a Windows-1252 text with no control character | 1872 strings, 4 to 16 bytes; none breaks the rule.  33 bytes of 0xff and a NUL is a valid Windows-1252 text and stays noise |
| Props14 record 0x3540000a (`_saved_flag_ok`) | a u16 of 0 or 1 | 621 records (the 3 saves with no such record excepted): 411 of 0, 210 of 1 (W36-0f.1) |
| Font word top byte | none | 24 bytes, 8 distinct values (0, 52, 96, 107, 110, 144, 192, 215): an allocator byte with no pattern to test |

The span is the range the committed saves show, not a law of the writer: a stamp grows with time
and LastSave is the time of the save, so a capture made after the newest committed save lies outside
both spans, and its stamps and LastSave then FAIL as data.  That is the point of the bound (a
value no committed save wrote is not noise), and the cost: when new saves are committed, the probes
above are re-run and `STAMP_SPAN` and `LASTSAVE_SPAN` in tools/trace/trace_gate.py, the two
`span_lo/span_hi` and `ls_lo/ls_hi` pairs in tools/trace/test_trace_gate.py and this table are widened
together, as one evidence row.  The re-gate shows it: a pair of the new saves fails on the stamp or
the LastSave until that is done.
