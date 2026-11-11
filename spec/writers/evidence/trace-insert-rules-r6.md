# Insert rules (round 6, item 3): the FixedMeta entry is fully predicted, the FixedData position rule, what stays open

_Wed 11 Nov 2026, round 6b.  Recordings `wave35-g3-insert-milestone-{first,last,nested}` and `wave35-g3-insert-summary-nested` (setter calls of FUN_1403dd790 on the Task table, one `fc6` run each), public fixtures `tests/data/wave35-g3-insert-*` and `wave30-insert-*`, `wave34-insert-task`.  Written to `build\trace-2021\` only.  PARTIAL: FixedMeta and the FixedData position rule are decoded; Props14, CEdl, Fixed2Data and the date words' sources are not._

**Hash-checked.**  The setter-call lists came through the KVP channel (`r6-i1.ps1`, saved as `r6i1-wave35-g3-insert-*.txt`, whose first line is the check): milestone-first `6C754B39009B`, milestone-last `ABAAE2F25118`, milestone-nested `135AA24A7705`, summary-nested `4C1F7F6D903E` (all `sha OK`).  Static checks: `r6_ins.py` (row diffs per pair), `r6_ins2.py`, `r6_ins3.py` (per-row setter values), `r6_ins4.py` (the FixedMeta prediction), `r6_meta.py`, `r6_calc.py`.  Interpretation is mine.

## 1. FixedMeta: the new row's whole entry is predicted from the setter calls (4 of 4 pairs, byte-exact)

Rule found in item 4 (`trace-calculate-words-r6.md`): the 39 bytes at entry `+8..+46` are a bitmap indexed by the setter's field number; bit `fld` is set when the row's setter call for that field is a value field (descriptor dword 0 = 0) with `fld < 305`, or a flag field (descriptor dword 0 non-zero) whose value is 1; `+2` is the row's count of VarMeta entries; `+4` is the row's FixedData offset; fields 305 and above belong to Fixed2Data's own bitmap.  `r6_ins4.py` applies exactly this to the setter calls on the new row of each recording and compares with the saved FixedMeta entry:

| pair | predicted = saved |
|---|---|
| milestone first, last, nested (all three equal) | `e3 bf ff fe 71 c1 67 01 07 48 ef 1f 10 00 ...` (0x0a entries) MATCH |
| summary nested | `e3 ff fd fe 7d c1 67 01 17 49 ef 1f 10 00 00 00 00 00 80 00 ...` MATCH |

So the milestone/summary difference is just the flag fields: the summary entry has bit 14 (flag 0x0b400013 = 1), bits 34 and 35 (flags 0x0b400052 and 0x0b40005c, value 1, byte `+12`: 0x71 -> 0x7d), bits 68 and 72 (flags 0x0b400179, 0x0b40017d: `+17` 0x48 -> 0x49 ... and 17 = 0 so `+10` stays `fd`), bit 17 clear (flag 0x0b400018 = 0), and byte `+26` bit 7 (0x80 = fld 151 key 0x0b40007c, the value 0x12c0).  The milestone entry has flag 0x0b400018 (bit 17) = 1 and 0x0b400013 = 0.  Position does not enter: first, last and nested give the same entry.  The "12-byte flag mask" the insert spec calls a mask is this bitmap; the bits "summary `b4 & 0x08`, project-row `b5 & 0x10`, milestone `b2 & 0x02`" are fld 35 / fld 14 / fld 17 flags (entry offsets +12, +9, +10).

## 2. FixedData: the position rule (measured, explains the 3, 15 and 1 bytes)

The save routine rewrites every row of the Task table through the setter (`row` = table row, uid + 3; 18 to 41 calls on each row, 121 on the new one).  Compared with the control:

- **IDs.**  Key 0x0b400017 (fld 0, `+0`) is the row's ID.  The new row takes the insertion position (first 1, last 6, nested 4) and **every existing row whose ID is at or after that position gets ID + 1** (setter values: first 0,2,3,4,5,6,7 plus the new row's 1; last 0..5,7 plus 6; nested 0..3,5,6,7 plus 4).  Bytes changed in FixedData: 6 (first), 1 (last), 3 (nested) from this rule alone.
- **Project row.**  Its start words (key 0x0b400025 fld 27 at `+104` and the neighbours at `+96` and `+112`, three 3-byte date words, 9 bytes) take the new task's start when that is earlier: only in "first" (3bf13138, against 3bf212c0), which makes 6 + 9 = **15** bytes there; none in last and nested.  The finish words (fld 28) stay 3bfd27d8.
- **Everything else in the existing rows is unchanged**: so 15 / 1 / 3 bytes of FixedData change inside the common length for first, last, nested.  The nested summary pair has the old row at the insert position change `+142` (2 -> 7, fld 46 key 0x0b4000a0) and `+172` (3 -> 4, fld 64 key 0x0b4000f9) because it becomes a child of the new summary (7 = the last renumbered ID, 4 = its new sibling index; two data points).

## 3. The new FixedData row (setter values on table row 10, 121 calls; 97 are the same in all three milestone pairs)

Differ with the position (milestone pairs `first | last | nested`):

| field | offset | first | last | nested | reading |
|---|---|---|---|---|---|
| fld 0 key 0x0b400017 | +0 | 1 | 6 | 4 | ID = insertion position |
| fld 46 key 0x0b4000a0 | +142 | 0 | 1 | 2 | outline level (0-based) of the position |
| fld 64 key 0x0b4000f9 | +172 | 1 | 2 | 3 | sibling index at that position |
| fld 27, 28, 75, 313, 314 (keys 0x0b400025, 26, 183, 503, 504) | +104, +108, +174, Fixed2 +50/+54 | 3bf13138 | 3bf212c0 | 3bf212c0 | start = finish of the milestone: the project start (3bf212c0), except 3bf13138 when inserted before the first task |
| fld 36 key 0x0b40005d | +128 | 3d280b04 | 3d280b18 | 3d280afa | creation stamp (day 3d28 + time), from the clock (item `GetLocalTime`, 0x1db4db) |
| fld 305, 307 (keys 0x0b400477, 0x0b40047f) | Fixed2 +0, +24 | a time-based GUID pair | | | per-save noise (UuidCreateSequential, 0x22cdb0) |

(Upper halves of some 2-byte fields in the table read as stale garbage because the logged `v` is a 4-byte read; they do not match the saved row.)  The summary-nested new row differs from the milestone-nested one in the flag fields above, fld 50 and 316 (the wbs/level bytes `+164` = 0x35 against 0x15), fld 40, and fld 151 (0x12c0); and has a 1004 var block (0x0b4004e4 = 1) where the milestone has none.

## 4. What the 3 / 15 / 1 bytes were not: Props14 and CEdl

Not traced in this round.  The earlier rounds' note (`trace-insert-milestone-r4.md`) already attributes the Assn rows (GUID pair, creation stamp) to UuidCreateSequential and GetLocalTime; `Props14` and `214/CEdl/Var2Data` were not revisited.

## For the port:

Insert Milestone / Summary: write the new FixedMeta entry from the set of fields the row's own setter calls write (value fields below 305 plus flag fields whose value is 1; milestone and summary differ only in the flags 0x0b400018 / 0x0b400013, 0x0b400052, 0x0b40005c, 0x0b400179, 0x0b40017d and fld 151), `+2` = the row's VarMeta entry count (10 here), `+4` = 48 + 202 * uid; renumber the IDs of rows at or after the insertion point by +1 and the project row's start words if the new start is earlier; the new row's ID is the position, `+142` the outline level (0-based), `+172` the sibling index.  Dates, the creation stamp and the GUIDs are per-save noise (masked), except the start/finish constants above.  The CEdl and Props14 rewrites stay unwritten.

## Spec lines answered and what remains open

`spec/writers/insert.md` about line 69 ("items 3 and 5 are not decoded to a rule that predicts both held-out pairs"): item 3 (FixedMeta mask bits) is decoded and predicts all four recorded pairs exactly; the FixedData positional bytes (3, 15, 1) are decoded from the ID renumber and the project start words.  Item 5 (CEdl and Props14) is open.  Also open: the source of 3bf13138 for "first" (the day before the project start at 21:00 in the date format, not a working time), the meaning of `+142` and `+172` for a summary row, and the round 1 wave 30 pairs (not re-read: their dates (3bf11e78) come from another session).
