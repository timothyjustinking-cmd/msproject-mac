# Font item (round 6, items 1 and 2): every field of the 44-byte payload and its writing instruction

_Wed 11 Nov 2026, round 6b.  Recordings `wave35-g3-font-{italic,bold-off,underline,size,name,colour,bold-row2,bold-start,bold-range}` (C:\trace\runs, the VM msp-trace).  Written to `build\trace-2021\` only.  This replaces the static-only note the earlier round 6 agent wrote from the public fixtures (`r6_font.py`); its readings are confirmed or corrected below._

**Hash-checked.**  KVP `sha OK` prefixes (first 12 hex digits).  Calls of the set-by-id `FUN_140253fe4` (RVA 0x253fe4), one run each: italic `E6F0B5C7F6BE`, bold-off `6F3947786E8A`, underline `1CB15714576F`, size `32217E9B704C`, name `DF9BF0416493`, colour `F3787936669D`, bold-row2 `385C0B3471D2`, bold-start `D3D0F6AEA74C`, bold-range `3A182495ECD8`.  Call arguments and first copy (r6f3): italic `4A259FD1E228`, bold-range `5A390A3F3709`, underline `29E06A82BBD9`.  Source-buffer bytes and writers (r6f4): italic `24922CBA027F`, bold-range `105467F4FA03`, underline `A0006AB588F6`; (r6f5, with disassembly of 0xe389c0, 0xe39fa0, 0xece769): size `0D5F04C4DC4D`, name `80D8B8ED3755`, colour `723368F2C293`, bold-row2 `15C500D8133B`, bold-start `03B7E3FD3EC2`.  Bold off: copy `ABF6C1050956`, writers `9A4780BFFA55`.  Saved-file item list of the traced bold-range file (`r6_vitems.py`, in the VM): `AFE02747A94F`.  Helper scripts: `chain6.js` (ab, bufw, sz, mcp, m44), `r6-f1.ps1` to `r6-f9.ps1`.  Interpretation is mine.

## 1. The shape (same in all nine runs)

Each edit makes ONE call of the set-by-id with id **0x2240000c**, kind 1, no payload pointer and a byte size on the stack (arg 5 at rsp+0x28): **0x2c** (44) for one row, **0x58** (88) for two.  Positions of the call: italic 13e580:3c4, underline 1462bf:3ca, size 1324c9:3ca, name 14ac51:3c4, colour 1568fd:3ca, bold-row2 1403b8:3ca, bold-start 1d841c:3ca, bold-range 1478b1:3c4.  Bold off makes NO call (the item exists; see section 4).  The payload is copied in a few hundred steps later (first execution of RVA 0x22f988 after the call, e.g. 13e5dc:43b for italic: dest = record +12 in the bag, src = a 44-byte buffer on the stack heap, r8 = 0x2c or 0x58).  The payload is built into that buffer by **one routine, entry about 0xe389c0, run once per selected row** (callers e37bfa <- e39e56 <- e3087a <- b60264 for the toolbar commands, b6037d or b60c0f for Font32Ex).

## 2. Payload words and the instruction that writes each (measured, `r6f4` and `r6f5`)

| offset | word | writing instruction | value |
|---|---|---|---|
| +0 | w0 | **0xe389d0** `mov [rbx], r14d` | the row number of this payload within the pair of calls: 1, 2, 1 (row 1), 2 (row 2 pair); in the two-row call the second payload has w0 = 2 (written at buffer +44). It is the task row, not a counter: bold-row2 stores 2 with a single payload. |
| +4 | w1 | **0xe389db** `mov [rbx+4], r12d` | the field key: 0x0b40000e (Task Name) or 0x0b400505 (Start). Caller's argument. |
| +8 | low word | **0xe389e3** `mov [rcx], ax` (ax = word at caller's [rbp-60h]) | 3 for the toolbar commands, Underline and Colour; **6 for size and name**. Re-stored as 3 at 0xece769 on the toolbar path. |
| +10 | word | the 16-byte store **0xe389d7** `movups [rcx+2], xmm0` (copies the caller's style struct to +10..+25), then 0xece7d3, 0xece7fd, 0xece827, 0xece851, 0xece879 re-store the same word | the attribute-changed mask: **0x0100 Bold, 0x0200 Italic, 0x0400 Underline** (bold-off 0, colour/size/name 0). This is flags word byte 3 (0x01/0x02/0x04) of the pair tables. |
| +12 | w3 | toolbar path: 0xece77a (qword store, 8 bytes at +12); Font32Ex: it arrives in the 16-byte copy at 0xe389d7 | toolbar: 0xff000000 (automatic colour). Colour pair: **0x000000ff** (the Font32Ex colour 255, BGR) copied from the style struct. Underline, size, name: 0. |
| +20 | w5 | 0xece782 (4-byte zero) | 0 |
| +24 | w6 | toolbar: 0xece793 (qword); Font32Ex: zeroed by 0xe389df `movups [rcx+10h], xmm7` | toolbar 0xff000000, else 0 |
| +32 | w8 | 0xece79b (4-byte zero) | 0 |
| +36 | w9 | toolbar: **0xece7aa** `mov byte [..], 1` | 1 on the toolbar path; 0 on Font32Ex |
| +40..+42 | font word low 3 bytes | zeroed first by **0xe3884b** `mov word [rbx+28h], r8w` and **0xe38850** `mov byte [rbx+2Ah], r8b` (r8 = 0), then the builder below | see section 3 |
| +43 | font word top byte | **never written** by any instruction in the run (confirmed again in all nine runs: 0x90, 0x6b, 0x00, 0x34, 0x32, 0x49, 0x02, 0x10, 0x4a in the nine buffers); it is what the buffer held | garbage |

Corrections to the static note: w2 byte 0 (the +8 word) is not "bit 2 = face/size change": it is a caller-supplied word, 3 or 6, stored whole at 0xe389e3. w3..w8 are copied from a style struct, not "two triples": +12 and +24 are two colours (text colour and a second colour), both automatic (0xff000000) on the toolbar path, +36 byte is 1 on that path only.

## 3. The font word (+40): a diff of the new style against the reference style, one bit per attribute group

The word is built by **FUN_ at 0xe39f80** (called at 0xe38a21 from the builder, `rcx` = the payload buffer, `rdx` = r11+4 = the new style, `r8d` = the mask of attribute groups NOT to compare, `r10` = the reference style qword).  For each group the routine sets or clears one bit, comparing the new style with the reference; a group whose bit in `r8d` is set is skipped, which is why a toolbar command (one group) touches only one bit and a Font32Ex call (all groups) touches all of them.  Each instruction below is `or cx, bit` on the set path and `and cx, ~bit` on the clear path, ending in `mov word [rdi+28h], cx` (the position of the store is the one seen in the recordings):

| bit | meaning (from the comparison) | set at, store at |
|---|---|---|
| 0x01 | **Bold**: bit 0 of byte 3 of the style differs | or at 0xe3a019, clear at 0xe3a024, store **0xe3a027** (skipped if r8d bit 8) |
| 0x04 | **Italic**: bit 9 of the style word differs | or 0xe3a051, store **0xe3a05f** (skipped if r8d bit 9) |
| 0x02 | **Underline**: bit 10 differs | or 0xe3a089, store **0xe3a097** (skipped if r8d bit 10) |
| 0x100 | group of bit 11 (a fourth attribute; not exercised by a pair, so unnamed here) | or 0xe3a0c1 (`or cx, r9w`, r9w = 0x100), store 0xe3a0cf |
| 0x10 | **font face or size differs**: `(byte0 of reference xor byte0 of new) & 0x3f` is non-zero, tested only if r9w != 0xffff | or 0xe39fbb, store 0xe39fc9 |
| 0x08 | the high dword of the reference differs from the dword at [rdx+4] | or 0xe39fe6, store 0xe39ff4 (skipped if r8d & 0x1f0000) |
| 0x40 | the colour group differs (dword at [r11+0x10] against the reference) | or 0xe3a0eb, store 0xe3a0f9 (skipped if r8d & 0x3e00000) |
| 0x80 | a last group differs (result of 0x2bc60c on the two styles) | or 0xe3a126, store 0xe3a135 (skipped if r8d & 0xfc000000) |

Seen in the runs: Bold on, bold-row2 and bold-start execute only 0xe3a027 (word 1); italic only 0xe3a05f (word 4); Bold off only 0xe3a027 with the clear path (word 0).  Font32Ex underline runs every store: 0 -> 8 (e39ff4) -> 8 -> 8 -> 0xa (e3a097, underline) -> 0xa -> 0x4a (e3a0f9) -> **0xca**; colour: 0 -> 8 -> ... -> 0x48 -> **0xc8**; size and name: 0x10 (e39fc9) -> 0x18 -> ... -> 0x58 -> **0xd8**.  So bits 3, 6 and 7 are always set on the Font32Ex path (the all-zero style a Font32Ex call builds differs from the reference in those groups regardless of the edit), bit 1 is Underline and bit 4 is "face or size changed".  The earlier static reading (bits 0, 1, 2, 4) is confirmed; bits 3, 6, 7 and 8 are new.

**Where font size and name are stored: not in this item.**  For Font32Ex('Calibri', 14) and Font32Ex('Arial', 11) the whole 44-byte buffer at the copy is `01 00 00 00 | 0e 00 40 0b | 06 00 00 00 | zeros | d8 00 00 xx`: the only trace of the size or the name is bit 0x10 of the font word and the 6 in the +8 word.  No run writes a name or a size into this buffer, and the pair files agree (the item and nothing else changes in `214/CV_iew/Var2Data`).  So the item records that the face or size group changed; the face and size values themselves are not in what these recordings save.  Not found elsewhere (the other changed streams are VarMeta offsets and the Props size word, round 4).

## 4. Bold off edits the existing item through the same builder

`wave35-g3-font-bold-off` makes no 0x2240000c call.  Position **1dab9b:96f to 1dab9b:9da** runs the same builder (e389d0, e389d7, e389db, e389df, e389e3, ece769.., then **0xe3a027 with the clear path**, value 0): the buffer is `01 00 00 00 | 0e 00 40 0b | 03 00 00 00 | 00 00 00 ff | .. | 01 00 00 00 | 00 00 00 90`, and the copy at **1dabdb:976** (r8 = 0x2c, only one 44-byte copy in the whole run) overwrites the existing record's payload in place.  The +10 word is 0 (the 0xece7fd..879 stores write 0 because the mask is clear), so flags go 0x01000003 -> 0x00000003 and the font word 0x…01 -> 0x…00; the top byte 0x90 is the stack leftover, the same in both buffers.

## 5. Item 2: wave35-g3-font-bold-range (the one capture FAIL)

- **The extra 44 bytes are a second payload inside the same item.**  The traced run makes one set-by-id call (1478b1:3c4) with byte size **0x58**, and the copy at 14791a:43d moves 0x58 bytes: the builder runs twice (writes at buffer +0 and +44: w0 = 1 at 147854:45c, w0 = 2 at 147861:47d), once per selected row.  The saved file of the traced run (`C:\trace\work\wave35-g3-font-bold-range.mpp`, 100,278 bytes, 44 more than the pair's 100,234) holds ONE item at offset 85,272 with **length 88**: payload 0 = `00000001 0b40000e 01000003 ff000000 0 0 ff000000 0 0 1 00000001` and payload 1 = the same with w0 = `00000002`.  The pair holds the same item with length 44 and only the row-2 payload (w0 = 2).
- **Which call did what.**  A Bold command over a selection makes one item whose payload array has one 44-byte entry per selected row, in row order; the item's length is 44 times the number of rows.  Round 5b's trace run selected two rows (two builder runs); the oracle's selection call, when the committed pair was made, covered only one row (the pair has the row-2 payload alone, i.e. the extended selection replaced the first one, as the spec suspected).
- **Item list comparison.**  Trace file: one 0x2240000c item, 88 bytes, rows 1 and 2.  Pair: one item, 44 bytes, row 2.  Control: none.  The FAIL is therefore a different (two-row) edit, not a writer fault.

## For the port:

Build each style item as 12 header bytes (length = 44 per row, id 0x2240000c, kind 1) plus one 44-byte payload per selected row: `row, field key, word(+8) = 3, word(+10) = 0x100 Bold / 0x200 Italic / 0x400 Underline, colours at +12 and +24 = 0xff000000, byte +36 = 1, font word = 0x00000001 (Bold), 0x4 (Italic), 0x2 (Underline)`; pin the top byte at 0x90 as the writer already does.  Bold off rewrites the same payload with +10 = 0 and font bit 0 clear.  A Font32Ex face/size/colour edit is not reproducible from the item (word(+8) = 6, font word 0xd8 low byte, zero colours, but the face and size are not in the file part we can see), so keep it unenabled.

## Spec lines answered and what remains open

`spec/writers/view-stream-214.md` lines about 132 and 283 ("font word is not decoded"): answered for bits 0x01, 0x02, 0x04, 0x08, 0x10, 0x40, 0x80, 0x100 and for every payload word (section 2).  Open: what attribute group bit 0x100 (fourth group) and bit 0x08 and 0x80 compare in plain terms; where Font32Ex's face and size values persist (nothing in the item); the meaning of the +8 word (3 versus 6) beyond "caller-supplied, 6 when face or size is the edit"; the toolbar Underline command (not recorded; Font32Ex underline is the 0x2 bit of the font word and 0x400 mask).  Item 2 (bold-range): answered.
