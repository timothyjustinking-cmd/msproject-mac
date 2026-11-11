# Calculate's undecoded words (round 6, item 4): the FixedMeta and Fixed2Meta bits, the count word, the nine var blocks

_Wed 11 Nov 2026, round 6b.  Recordings `calculate` (the Calculate Project pair from wave 30, editing and saving inside the recording) and, host-side, the public fixtures `wave30-calculate`, `wave34-calculate-second`, `wave35-calculate-{lag,linked,snet,unlinked}`, `wave30-inactivate-second`.  Written to `build\trace-2021\` only._

**Hash-checked.**  KVP `sha OK` prefixes: setter calls with descriptors on the `calculate` recording (r6c1) `59226FAFB406`; the same log re-filtered for the low fields (r6c2) `245A3CC6FA1F`; value pointers and callers of the three 0x4e3/0x4e4/0x4e5 blocks (r6c3) `E22FF30CEF42`, `8A16469F9A67`.  Host-side scripts on public fixtures: `r6_calc.py` (byte diffs of FixedMeta/Fixed2Meta), `r6_meta.py` (entry dumps), `r6_cnt.py` (count word against VarMeta), `r6_vm.py` (new var blocks per pair).  Interpretation is mine.

## 1. The FixedMeta entry is a presence bitmap indexed by the setter's field number (measured)

Geometry (derived, all fixtures): the Task FixedMeta stream is a 16-byte header plus **47-byte entries**; entries 0..2 are the three header rows and entry 3+k describes table row k (project row = k 0); `+4` (u32) = the row's byte offset in Task FixedData (48 + 202 * uid); `+2` (u16) = see section 2; **`+8 .. +46` (39 bytes, 312 bits) is a bitmap in which bit `i` is byte `8 + i/8`, bit `i%8`, and `i` is the field number (`fld`, the descriptor index) of the generic setter FUN_1403dd790**.

Fit against the recording (setter calls on rows 3, 4, 5 = uids 0, 1, 2, `r6c2`):

| meta change (pair) | bit | setter call that sets it |
|---|---|---|
| `+9 0xbf -> 0xff`, all three rows | bit 14 | **fld 14**, key 0x0b400013, a flag descriptor (descriptor dword 0 = 2, last dword 1), value 1, called on rows 3, 4 and 5 (positions 998e:15c1, 9a18:572, 9a8b:d8c) |
| `+18 0xaf -> 0xef`, project row | bit 150 | **fld 86**, key 0x0b4001df, flag descriptor (dword 0 = 0x10), value 1, row 3 (99ab:119f); rows 4 and 5 also call it but their bit was already set in the control |
| `+19 0x00 -> 0x1f`, project row | bits 152..156 | **flds 88..92** = the five overtime keys 0x0b4000a4, a5, a8, a9, aa (value 0, var descriptors 0x201ffff..0x205ffff), row 3 positions 99b7:490 .. 99c5:31e; row 5 also calls them (9ab5:48c .. 9ac3:1ba) but its bits were already set |

So `+9 0xbf -> 0xff` is bit 14 (field 0x0b400013) and the `+18/+19` change is bit 86 and bits 88..92: the five overtime fields written for the first time on the project row.  Why the neighbours (+10 `fd`, +11 `fe`, the rest) are what they are: every other byte of the bitmap is the set of fields written earlier in the row's life.  A flag-type descriptor (descriptor dword 0 non-zero) sets its bit only when the flag value is 1 (fld 3 and fld 17, value 0, leave bits 3 and 17 clear in `e3 bf fd fe`), a value field sets its bit when it is written (fld 37 and fld 38, value 0xffffffff, set bits 37 and 38 = byte +12 bits 5 and 6 of 0x71/0x79).

The `wave35-calculate-{lag,linked,snet,unlinked}` fixtures change only entry 3 (`+2 0x04 -> 0x09`, `+175 af -> ef`, `+176 00 -> 1f`) because their controls are already post-calculate for the task rows; the project row is the only one that gets the five overtime bits.  `wave34-calculate-second` changes no meta byte (already calculated).

## 2. The `+2` word is the number of Task VarMeta entries of the row (measured, 1037 of 1045 rows)

Project row 3 -> 9 (3 + the 5 overtime blocks + 0x4e4), leaf uid 1 8 -> 9 (+0x4e5), leaf uid 2 8 -> 10 (+0x4e3, +0x4e5): exactly the nine new VarMeta entries of the pair.  `r6_cnt.py` over the `wave3*` fixtures: 1037 rows agree, 8 differ by one in a swapped pair of rows (`wave31-fill`, `wave34-field-clear`, `wave34-note-clear`, `wave34-note-two`: a row-to-uid mapping slip in my script when rows are reordered, not a different rule; the counts are 9/8 and 8/9 swapped).  The insert pairs agree too: the new row has `+2 = 0x0a` and 120 more VarMeta bytes = 10 entries.

## 3. Fixed2Meta `+399/+495/+591` (and `+95` of entries 3, 4, 5): the var-key bits beyond the bitmap

Entry layout (derived): 16-byte header, **96-byte entries**, `+4` = the row's offset in Task Fixed2Data (64 * uid), `+8 ..` a bitmap.  Entries 3, 4, 5 = uids 0, 1, 2 (592 = 16 + 6*96), so `+399 = 16 + 96*3 + 95`, `+495`, `+591` are byte `+95` of those entries.  Values `0x08, 0x10, 0x14` = bit 3 for the project row, bit 4 for leaf uid 1, bits 2 and 4 for leaf uid 2, which is the set of new var blocks per row: project row 0x4e4, A 0x4e5, B 0x4e3 + 0x4e5.  The recording shows the setter keys for them are **flds 1003, 1004, 1005** (0x4e3, 0x4e4, 0x4e5: calls 9af2:71f, 9a08:dac, 9a6f:99e / 9af4:454), so the bit is `fld - 1003 + 2` in byte +95 (a fit of 3 rows to 3 keys, not a general proof).  It is the same second bitmap as the 1 to 2 byte Fixed2Meta/FixedMeta differences of `wave30-inactivate-second` (Fixed2Meta `+408 0xff -> 0xbf` = entry 4 byte +8, bit 6 cleared; FixedMeta `+222 0xcf -> 0x8f` = entry 4 byte +9 ... bit 14 area): a command that changes a stored flag clears or sets the one presence bit of that field.

## 4. What the Fixed2Data words are (the `+42` and `+46` bytes)

The setter calls on `fld 309` (key 0x0b4004e2, descriptor offset 0x2a = +42, 4-byte value 0x40, 0x30, 0x28 on rows 3, 4, 5: positions 99ad:2ce, 9a36:2ce, 9aab:2ce) and `fld 310` (key 0x0b4004e7, offset 0x2e = +46, values 0xffffffff, 0x3bf13138, 0x3bfd27d8: 99af:2ce, 9a38:2ce, 9aaf:2ce) are the Fixed2Data words the pair shows at `+42` (0x40, 0x30, 0x28) and `+46..+48` (a date for rows 1 and 2, no-date for the project row).  They are fixed fields of the same 0x4e2..0x4e7 key family as the three var blocks.  Row 4's value 0x3bf13138 and row 5's 0x3bfd27d8 equal the dates the same recording writes to fld 30 (key 0x0b400028, +116) and fld 75 (0x0b400183, +174): the task's early start / finish copy.

## 5. The `ff ff` tail and the payloads of 0x4e3/0x4e4/0x4e5 (partial)

Payloads (8 bytes): 0x4e4 `01 00 00 00 01 00 00 00`, 0x4e5 `01 00 00 00 01 00 ff ff`, 0x4e3 `01 00 00 00 01 00 00 00`.  At the setter entry for each (r6c3) the value pointer `r9` points at exactly these 8 bytes and the call comes from the save routine: stack `3aa9df <- 3a1286 <- 377cbe <- 43f8d6 <- 47f308 <- 454205 <- 411dea <- 410f00 <- 410c76 <- 3e0d8b` (the same flush as the 114 tables).  The bytes are read from the engine's in-memory task object at save; the last 600 writes to them before the call are all from outside Project's own code (the CRT copy), so the originating assignment (which would say what `ff ff` means) was not reached.  The `ff ff` appears on 0x4e5 in both tasks and never on 0x4e3/0x4e4; read as the second u16 pair of a (count, flag, 0xffff = none) record that only 0x4e5 leaves unset.

## 6. What decides the nine-block set

- The five overtime blocks (flds 88..92, zero payload) go to the project row only, and only when that row's bits 88..92 are clear in the control's FixedMeta (wave 30 control: clear; wave 35 controls: set on tasks, clear on the project row: the four variants add exactly these five and nothing else, `r6_vm.py`).  The recording calls the same five fields on leaf uid 2 (row 5) too, and no block is added, because the bits there were already set.
- The 0x4e3/0x4e4/0x4e5 blocks (flds 1003..1005) are added where the row has no such VarMeta entry in the control (project row: 0x4e4; A: 0x4e5; B: 0x4e3 and 0x4e5); in the four wave 35 variants the controls already hold them, so none are added.  Which key a row gets (project 0x4e4, task 0x4e5, B additionally 0x4e3) follows the row kind and its link (B is the linked successor); the one fixture with a fresh set (`wave30-calculate`) is the only evidence, so this is a fit to 3 rows.

## For the port:

When Calculate is applied to a file whose control lacks them, for each row set bit `fld` in the FixedMeta bitmap (`+8 + fld/8`, bit `fld%8`) for every field the engine writes (flag descriptors only when the flag is 1): at least bit 14 and bit 86 on all rows and bits 88..92 on the project row; set `+2` to the row's new VarMeta entry count; in Fixed2Meta set `+95` bits (0x08 project row 0x4e4, 0x10 task 0x4e5, 0x04 for 0x4e3); add the five zero overtime blocks to the project row and the 0x4e3/0x4e4/0x4e5 blocks only where they are absent.  The 8-byte payload of 0x4e5 ends `ff ff`; the others end `00 00`.

## Spec lines answered and what remains open

`spec/writers/calculate.md` lines about 26, 76 and 108: `+9 0xbf -> 0xff` = bit 14 (field 0x0b400013); `+18/+19` = bit 86 and bits 88..92; `+2` = VarMeta entry count; Fixed2Meta `+399/+495/+591` = `+95` bits for flds 1003..1005; the `ff ff` tail is read from the engine's task record at save.  Open: the engine's origin of the two payload words and of the Fixed2Data `+42` values (0x40/0x30/0x28), what field 0x0b400013 and 0x0b4001df are called in the UI, the rule for which row gets 0x4e3 (one pair).
