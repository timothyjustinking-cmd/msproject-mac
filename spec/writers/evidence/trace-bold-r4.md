# Font Bold (round 4): the item writer, the font word bytes, the VarMeta offsets and the 214/Props size word

_Wed 11 Nov 2026, round 4.  Trace `bold` (`C:\trace\runs\bold`, recorder attached before the edit).  Written to `build\trace-2021\` only; the coordinator merges into `spec/writers/view-stream-214.md`._

**Hash-checked.**  Every VM output came through the KVP channel with `sha OK` (first 12 hex digits): r4g1 `2C3AB899803A` (the one call of the set-by-id with id 0x2240000c), r4g2 `D924FDF334D9` (registers, stack and disassembly of the caller), r4g3 `6810CA07EB50` (blob header and item bytes), r4g4 `3D481765A84B` and r4g5 `29F9838CE402` (reads of the item), r4g6 `07324E77F66B` (the save-time copy), r4g7 `2DD78AA9E1B7` (the four header stores and the payload copy), r4g9 `C5875E9B389D` (writes to the 44-byte source buffer), r4g10 `EDAF55E338DA` (font word bytes and the two store instructions), r4g11 `CF914E7E5CFE`, r4g12 `A01D3F015C79`, r4g13 `16D25E029D30` (view table size word).  Static host check of the item bytes: `r4_bold.py`, `r4_bold2.py` (public fixtures).  Interpretation is mine.

## 1. What the 56 bytes are

`214/CV_iew/Var2Data` of `wave30-textstyle.mpp` (100234 bytes; control 100178) has, at offset 85272, a 56-byte item and, after it, the previous content shifted by 56:
`2c 00 00 00 | 0c 00 40 22 | 01 00 00 00 | <44-byte payload>`: **length 44, id 0x2240000c, kind 1**, then the payload `01 00 00 00 | 0e 00 40 0b | 03 00 00 01 | 00 00 00 ff | ... | 01 00 00 00 | 01 00 00 90` (count, field key 0x0b40000e = Task Name, the style cells, and the font word at payload +40).  It is a record of the **same `length / id / kind / payload` framing as the Props records**, inside the view's property bag (a blob of 0xa6f8 = 42744 bytes in memory), appended to the end of the bag.

## 2. The writer at edit time: the same set-by-id as Props

At position **63a0:1f08** (the FontBold edit) `FUN_140253fe4` (RVA 0x253fe4, see `trace-props-framing-r4.md`) is called with id **0x2240000c**, kind 1, no payload pointer and byte size 0x2c, from RVA **0x230092** (a "get or create the property buffer" routine; chain 0x22eff5 <- 0x229e32 <- 0x253c7d <- 0x22c338 <- 0x23e207 <- 0x23e079 <- 0x450448 <- 0x450354).  The id is not in the record list, so the not-found branch appends the record at the old end of the bag blob (bag base `…c11c9040`, old size 0xa6c0): header stores **0x254240 (id), 0x254244 (kind word), 0x254248 (length), 0x25424a (flags word)** at positions 63a1:25ac..25af (hash-checked write list, r4g7), blob size and record count updated as for Props.  The payload is then copied in by the memcpy at 0x22f988 (positions 63a2:206 to 208, two overlapping 32-byte vector copies) from a 44-byte source buffer built just before (positions 639f:1c7a to 1cfc).  After 63a8 nothing writes the item again; it is read many times (view redraws) and copied at save (below).

## 3. The font word (payload +40): bit 0 = Bold, bytes 1 and 2 zero, byte 3 never stored

Writes to the 44-byte source buffer (r4g9): the font word at `+0x28` is written by **two instructions only**:

- **0xe3884b `mov word [rbx+28h], r8w` and 0xe38850 `mov byte [rbx+2Ah], r8b`** (r8 = 0): bytes `+0x28..+0x2a` are zeroed.  `+0x2b` is **never written** by any instruction in the recording: it keeps whatever the buffer held (allocation garbage): **0x90 in the pair's session, 0x00 in this one, 0x6b in the other pairs**.  This settles the "uninitialised byte" reading of RW35-W10: the top byte is stack/heap garbage, not computed.
- **0xe3a027 `mov word [rdi+28h], cx`** with `cx |= 1` at 0xe3a019 when the called predicate (call 0x838d21d, `test al,1`) is true, `cx &= 0xFFFE` otherwise: **bit 0 of the word is Bold** (and the routine clears it for Bold off, with the same store).  Position 639f:1cfc; the buffer then reads `01 00 00 00`.
- The routines are in the 0xe37..0xe3a range (`FUN_140e37bfa` <- 0xe39e56 <- 0xe3087a <- 0xb60264 <- 0x2a335d <- 0x29f5d5, the FontBold command chain).

**For the port:** write payload +40 as `01 00 00 xx`, `xx` any byte (the gate must mask it); Bold off clears bit 0 of the existing word (the bold-off effect on the item was not captured).

## 4. The save: VarMeta offsets and the `214/Props` size word

- At save the bag blob is copied out byte by byte (`rep movs`, position e6b0:23a4, reading the item) by `0x26e28e` <- `0x3a4cfc` <- `0x429be0` <- `0x42923f` <- `0x377cbe` (the stream flush that also drives the 114 tables), and handed on through the vtable call at 0x3a4d32 (type word 0x1d, the blob var field) to the **generic setter `FUN_1403dd790` (0x3dd282) and the var-block append 0x3dc7d0**: the writes to the view table's running Var2Data end `[table+0x344]` are `W+0x3dcf70` at positions e6ab:2fd, e6b6, e6b9, e6ba, e6bc, e6be, e6c0 (r4g13).  So the view stream's blocks are re-appended at save through the same machinery as the 114 tables (append 0x3dc7d0 -> inserter 0x3de484 -> merge 0x376b10, `trace-calculate-r3.md` section 1), which is why round 3 saw the keys 0x20400006 / 0x22400014 / 0x2240002f there and not 0x2240000c: **0x2240000c is an item inside a bag blob, not a VarMeta key**.  The VarMeta offsets (0, 0xa650, 0x14d50, 0x16da4, 0x16f25, 0x170c2, 0x171ac in round 3's list) are the running ends after each re-append, in call order; the 56 bytes of the new item enlarge the bag and shift every later block, which is the "offset rewrite" the pair shows.
- The **`214/Props` size word** is `[table+0x344]` of the view table (0x1878a = 100234 = the new Var2Data size), published by the per-table function at 0x397980 (store at **0x397993**, position **e6c2:261B**, `eax = 0x1878a`; the same function as the 114 tables' `0x0001000N` words in `trace-baseline-r4.md`).  The other 15 tables at that point give 0xe0, 0x41b, 0, 0x1de, 0, 0, 0xae, 0, 0x20e, 0x56, 0x16, 0x18c, 0, 0xc, 0 (the 114 values 0x20e, 0x56, 0x16, 0x18c are the Task, Rsc, Cal and Assn sizes).

## 5. Not done

Bold off and other font bits (italic, underline) were not captured (one `bold` recording only); the dependence of the bag's size on the number of items (the 56-byte growth is item 12 + 44); the predicate at 0x838d21d (`bold` getter) was not traced.
