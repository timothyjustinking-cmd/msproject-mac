# Calculate: variant value sources (item 1d) and the remaining Fixed2Data stores (item 1e), round 4

_Wed 11 Nov 2026.  Traces `w35-{unlinked,linked,lag,snet}-calc` and `calculate`.  Written to `build\trace-2021\` only._

**Hash-checked.**  KVP `sha OK` (first 12 hex digits): unlinked `8F2E3D3F1592`, linked `AA07382D168D`, snet `0A5FC8BA4BE0`, lag `739B5D18FD3F` (value chains), setter positions `CB5BF73A6730`; calculate `D183391DDBCF` (instruction path of one setter call), `2C41D4E8A84B` (the store at 0x3dd9f4), `19D8A633D7F1` (the bitmap stores).  Interpretation is mine.

## 1d. Value sources of the variants' changed words (`vchain`: the last write to the value the setter is given, followed through memcpy hops to the first non-copy store)

| variant | word | setter position | final store (RVA) | what it is |
|---|---|---|---|---|
| unlinked | project Finish `3bfd27d8` (row 3, key 0x0b400024) | 30383:2dc | **0x292225** `mov [rsp+18h], r8d` | the Finish store of the scheduler (FUN_140290dec +0x1439, round 2) |
| unlinked | leaf uid 2 Finish `3bf427d8` (row 5, key 0x26) | 305c7:7bf | **0x28ecc0** `mov [rsp+10h], edx` (FUN_14028e298 region, +0xa28) | date arithmetic for an unlinked task |
| unlinked | leaf uid 2 Fixed2 `0x0b4004e7` `3bf13138` | 305d4:d58 | **0x486720** `mov [rsp+10h], edx` (via 0x486764 <- 0x38f366 <- 0x403457 <- 0x402085) | the 4e7 date word |
| linked | project Finish `3c0227d8` | 7b43:198e | 0x292225 | as above |
| linked | leaf uid 2 Finish `3c0227d8` (row 5, key 0x24) | 7b89:1cc0 | 0x292225 | as above |
| linked | leaf uid 2 4e7 `3bfd27d8` | 7b90:fb9 | 0x486720 | as above |
| lag | project Finish `3c0327d8` | 4169c:2ec | 0x292225 | as above |
| lag | leaf uid 2 Finish `3c0327d8` | 41890:2ec | 0x292225 | as above |
| lag | leaf uid 2 4e7 `3c0027d8` | 418ec:2de | 0x486720 | as above |
| snet | leaf uid 2 Start `3c0712c0` (key 0x25) | 4cdb8:2dc | **0x28ea10** `mov [rsp+10h], edx` (FUN_14028e298 region) | the SNET constraint date |
| snet | leaf uid 2 Finish `3c0927d8` | 4cdc4:2dc | 0x28ecc0 | as above |
| snet | leaf uid 2 4e7 `3c063138` | 4ce89:2ce | 0x486720 | as above |

In every variant the setter gets its value through a copy loop (stores `0x23bb2a <- 0x3a16b2 <- 0x377cbe`, memcpy chain `0x22f988 <- 0x229e32 <- 0x56c7ac / 0x28ed04 / 0x486764`); the **final stores are four instructions**: 0x292225 (Finish, linked and lag chains and the project row), 0x28ecc0 and 0x28ea10 (the earlier/later date computations in FUN_14028e298's neighbourhood), and 0x486720 (the 0x0b4004e7 word, a separate routine reached through 0x403457).  The arithmetic that produces these dates (a 480-minute working day, calendar skipping, lag and SNET handling) is upstream of those stores and was not unrolled; the calls above are the place to start for each rule.

## 1e. The remaining Fixed2Data stores (calculate trace)

- **The store instruction**: Fixed2 fields go through the same generic setter as every field: for the call fld 309 (key 0x0b4004e2, row 3, position 99ad:2ce) the stored value 0x40 is written at **RVA 0x3dd9f4 `mov [r14], eax`** (position 99ad:3b7, r14 = the row staging buffer + the descriptor offset 0x2a), and the presence bit set by **0x43535b `mov [r8+r9+8], al`** (position 99ad:3fe: `al = old | (1 << cl)`, cl = 4, byte index r9 = 0).  Fld 310 (key 0x4e7, 99af:2ce) uses cl = 5 and fld 315 (key 0x0b400508, 99b1:2ce) byte 1, cl = 2; in this trace the bits were already set (the old byte equals the new), so these three calls do not themselves change the Fixed2Meta bytes.  The Fixed2Meta `+399 00 -> 08`, `+495 00 -> 10`, `+591 00 -> 14` are therefore **not** from the presence bit of these three fields (bits 4/5 and byte 1 bit 2 do not give 0x08, 0x10, 0x14 per row); they live in a second bitmap that was not located (open).
- **The +250 word** (uid 0, `ffffffff -> 00000000`): row 3, **fld 315, key 0x0b400508, row offset 0x3a (58 = 250 - 192), value 0**, setter position **99b1:2ce** (calc-table line 37); stored by the same 0x3dd9f4.  The value 0 comes from the engine copy chain (not hopped).
- **The bytes stepping by 0x11** (+195, +259, +283, +323, +347): single bytes of the 16-byte `UuidCreateSequential` GUIDs (`rel 3` and `rel 19` of each 64-byte Task Fixed2 row etc.): the counter of `rpcrt4!UuidCreateSequential` (called from WINPROJ+0x22cdb0) moves by one per GUID, so single bytes differ between files saved at different counter values (the exact step of 0x11 was not matched to call numbers).  Not data; see `trace-insert-milestone-r4.md`.
