# Calculate save-path writers (round 3): VarMeta offsets, FixedMeta, Fixed2Data, the Props rewrites

_Wed 11 Nov 2026, round 3 of the TRACE agent.  Same VM build, same `calculate` trace (`C:\trace\runs\calculate`, wave 30 pair).  Merge into `spec/writers/evidence/trace-calculate.md` (the coordinator merges; this file is a copy under `build\trace-2021\`)._

**Hash-checking.**  Every table below was read from the VM through the new Hyper-V KVP channel (`kvp\agent.ps1` + `kvp\host.ps1`): the guest computes SHA-256 of the output text (CR removed, trailing space trimmed), the host decodes it and re-computes; every call below printed `sha OK`.  The check is on the transported text (no transcription by eye).  First 12 hex digits per call: vm5 `B6943FE21913` (disassembly of the merge routine), vm9 `1F59CEE1C319` (writes to the pending array), vm8 `AD6004EBC8E4` (the pending list dump), vm13 `E294C3C98A74` (the 15 insert calls), vm14 `2AF19291611A` (FixedMeta bytes), vm25 `439510143124` (Props blob write), vm26 `E1A5011C8D51`, vm27 `2A440FB38DBB`, vm28 `5E2BE94CC779` (Props stores).  The old rows of `trace-calculate.md` stay as read by eye.  Interpretation (what a row means for the port) is mine and is not hash-checked.

Function names: `analysis/meta/functions.tsv` boundaries are unreliable here (several RVAs fall inside a listed function that starts earlier or later); RVAs are from the trace (`uf` flow analysis in cdb), the `FUN_` is the nearest listed entry.

## 1. The nine new Task VarMeta entries and their offsets

Pair (host, `dstream.py`): `114/TBkndTask/VarMeta` 252 -> 360 bytes: header count word `0x13 -> 0x1c` (+9) and header size word `0x108 -> 0x174` (= 0x108 + 9 * 12); every entry is `uid(4) offset(4) key(4)`, **sorted by (uid, key)**, 12 bytes.  The nine new entries (uid, key, offset): uid 0: `0x0b4000a4` @0x108, `a5` @0x114, `a8` @0x120, `a9` @0x12c, `aa` @0x138, `0x0b4004e4` @0x144; uid 1: `0x0b4004e5` @0x150; uid 2: `0x0b4004e3` @0x15c, `0x0b4004e5` @0x168.  The existing entries keep their offsets (uid 0's `0e` @0x10, `0x0b400563` @0, `64` @8; uid 2's `a4..aa` @0xaa..0xda).

**Writer of the offsets: `FUN_1403dc7e0` area, RVA 0x3dc7d0 (the var-block append, caller of the insert), line `mov edi,[r14+0x344]` at RVA 0x3dcea6.**  `[table+0x344]` is the running end of the table's Var2Data (0x108 at the first call, then +12 per new block: 0x114, 0x120 ...).  The routine seeks the in-memory Var2Data stream to that offset (stream vtable +0x28), writes the 4-byte length and the payload (vtable +0x20 twice), then passes the offset (`r9d = edi`) to the entry inserter.  The header size word 0x174 is the final `[table+0x344]`.  The block bytes themselves (`01 00 00 00` / `01 00 ff ff`) are the payloads of those writes (existing note: round 2 slot calls).

**Writer of the entries: the inserter at RVA 0x3de484 (cdb flow start; listed as `FUN_1403ddd7c` +0x708 / `FUN_1403de494`)**, signature `(table rcx, row edx, key r8d, offset r9d, flags [rsp+28h])`.  It builds the 12-byte entry `{uid word of the row, offset, key}` at `[rsp+20h]` and binary-inserts it into the table's *pending list* at `table+0xa8` (12-byte elements, array pointer at list+0x18, count at list+0xc) at the position ordered by (uid, key); flag bit 1 (value 2) means "new entry", which also does `add word [rowmeta+2], 1` (see section 2).  The 15 calls (hash-checked, `vm13`; table `0x..8a3cc0`, row = uid + 3, caller 0x3dcff4):

| position | row (uid) | key | offset | flags |
|---|---|---|---|---|
| 99b9:305 | 3 (0) | 0x0b4000a4 | 0x108 | 2 |
| 99bf:37e | 3 | 0x0b4000a5 | 0x114 | 2 |
| 99bf:a53 | 3 | 0x0b4000a8 | 0x120 | 2 |
| 99c3:6df | 3 | 0x0b4000a9 | 0x12c | 2 |
| 99c5:6df | 3 | 0x0b4000aa | 0x138 | 2 |
| 99fe:650 | 3 | 0x0b40000e | 0x10 | 0 (touch existing) |
| 9a0c:191 | 3 | 0x0b4004e4 | 0x144 | 2 |
| 9a6f:d51 | 4 (1) | 0x0b4004e5 | 0x150 | 2 |
| 9ab7:63b | 5 (2) | 0x0b4000a4 | 0xaa | 0 (existing) |
| 9ab9:d22 | 5 | 0x0b4000a5 | 0xb6 | 0 |
| 9abd:ee9 | 5 | 0x0b4000a8 | 0xc2 | 0 |
| 9ac1:ad0 | 5 | 0x0b4000a9 | 0xce | 0 |
| 9ac5:b79 | 5 | 0x0b4000aa | 0xda | 0 |
| 9af2:12c9 | 5 | 0x0b4004e3 | 0x15c | 2 |
| 9af6:370 | 5 | 0x0b4004e5 | 0x168 | 2 |

This answers the round-2 open point "why row 5's overtime calls (value 0) create no block while row 3's do": uid 2 already has blocks for keys a4..aa (offsets 0xaa..0xda in the control); the calls only *touch* them (flags 0, existing offset reused); uid 0 has none, so they are created.  The rule for the port: **create a block when the (uid, key) entry does not exist, otherwise reuse its offset; new blocks go at the Var2Data end in call order**.

**Writer of the VarMeta stream at save: the merge routine at RVA 0x376b10 (listed `FUN_140376a50` +0xc0 / `FUN_140376b20`)**, called through `0x377808` (vtable +0x400 of the table's stream object, inside the flush function at RVA 0x377760) at position 9b10:77f for the Task table.  It merges the pending list (15 entries above, already in (uid, key) order, dump `vm8`) into the table's sorted entry array (`table+0x380` pointer array, count `table+0x338`): compare uid (bit 31 cleared), then key; equal -> the pending entry replaces the stored one; the merged array is built in a heap block (`FUN_14024a20c`/`0x24a360` realloc, the 16-byte `memmove` at 9b1a:bf is that realloc) and then written as the stream.  At 9b10:77f the pending array holds, in memory order (uid, offset, key): `0:0x10:0e, 0:0x108:a4, 0:0x114:a5, 0:0x120:a8, 0:0x12c:a9, 0:0x138:aa, 0:0x144:4e4, 1:0x150:4e5, 2:0xaa:a4, 2:0xb6:a5, 2:0xc2:a8, 2:0xce:a9, 2:0xda:aa, 2:0x15c:4e3, 2:0x168:4e5`.  Sorting is by uid then key (the stream order of uid 0 is `0e, a4, a5, a8, a9, aa, 4e4, 563, 564`: ascending key).

## 2. Task FixedMeta changes (7 bytes)

Pair: `+159 03->09`, `+166 bf->ff`, `+175 af00->ef1f` (uid 0, entry base 157); `+206 08->09`, `+213 bf->ff` (uid 1, base 204); `+253 08->0a`, `+260 bf->ff` (uid 2, base 251).  Entry stride 47; entry+2 is a word, entry+9 and entry+18/19 are bitmap bytes.

| stream offset | old -> new | writer (RVA) | position | note |
|---|---|---|---|---|
| +159 / +206 / +253 (entry+2) | 3->9, 8->9, 8->10 | `add word [rax+2], 1` at **0x3dce8b** (inside the var-block append 0x3dc7d0); `-1` at 0x3dce13 on removal; rax = `0x4ebccc(table, row)` (row meta pointer) | with each flag-2 insert above | the word is the row's exact var-entry count (uid 0: +6, uid 1: +1, uid 2: +2) |
| +166 (uid 0 entry+9) | bf -> ff | `mov [r8+r9+8], al` at **0x43535b** (`FUN_140435230` +0x12b), called from 0x3ddd28 (`FUN_1403dd790` +0x598, the generic setter) | 9990:101 | bitmap-bit set per stored field; hash-checked (`vm14`, `Q 0 STORE`) |
| +175 (uid 0 entry+18) | af -> ef | same store | 99ab:12e3 (during the setter call fld 86, key 0x0b4001df) | |
| +176 (uid 0 entry+19) | 00 -> 1f | same store (caller chain 3dd282, 3aa9df, 3a1286) | 99c5:b4d (setter call fld 92, key 0x0b4000aa) | |
| uid 1 +213, uid 2 +260 | bf -> ff | same store | not traced (inferred: same helper, row 4 and 5 setter calls) | |

So FixedMeta = per-row "field present / dirty" bitmap set **by the generic setter FUN_1403dd790 itself** through the bit-set helper 0x43535b, plus the var-entry counter word in the var-block append.  The staging is then copied (32-byte copies) to the stream at 9bb9:2ea (round 1 table).

## 3. Task Fixed2Data changes (not fully written, partial)

Pair: `+195 50->61`, `+259 51->62`, `+283 50->61`, `+323 52->63`, `+347 50->61` (single bytes inside what look like record GUID/serial bytes, `... 85 ac a9 61 d1 15 11 f1 9f 64 ...`: +0x11 each; save stamps, inside the gate's noise set), `+234 00->40`, `+298 00->30`, `+362 00->28`, `+302 c012f2->3831f1`, `+366 c012f2->d827fd`, `+250 ffffffff->00000000` (uid 0).

Record layout inferred from offsets: row base 192 + 64 * uid; `+42` is a 2-byte field, `+46` a 4-byte date word.  These equal the setter calls of the round-2 table **fld 309, key 0x0b4004e2, row offset 0x2a** (values 0x40 for uid 0 at 99ad:2ce, 0x30 for uid 1 at 9a36:2ce, 0x28 for uid 2 at 9aab:2ce) and **fld 310, key 0x0b4004e7, row offset 0x2e** (date words 0x3bf13138 for uid 1 at 9a38:2ce and 0x3bfd27d8 for uid 2 at 9aaf:2ce; the +302/+366 runs are the low three bytes).  So the Fixed2Data record fields written by Calculate are the setter's fields 309 and 310; the writer is the same `FUN_1403dd790` (RVA 0x3dd790, store through the second table's descriptor).  **Not traced:** the exact store instruction, `+250` (uid 0), the +0x11 bytes, and Fixed2Meta `+399 00->08`, `+495 00->10`, `+591 00->14` (same bitmap helper 0x43535b by analogy, not traced).  The docfile view search (`findtrace` on view 2) traced these Fixed2 bytes through copies to the open-time read only (the 32-byte `memcpy` stage copies obscure the byte; srcfind is unreliable across overlapping vector copies): a TTD write watch on the in-memory Fixed2 row would settle it (see next steps).

## 4. The 114/Props 0x02400003 and 0x02400024 rewrites

Pair, stream `114/Props` (88736 bytes both): `+388 c0 12 18 3d -> d8 27 02 3c` (record `0x02400003` payload, the project Finish word) and `+840 01 00 -> 00 00` (record `0x02400024`, the pending flag).  The stream is written by `FUN_14038b6dc` (RVA 0x38b6cc: create stream flags 0x1012, write 4-byte length, then the blob) -> the blob (88732 bytes) at call a184:de6 (`rdx` = 0x26f40b916f0) is the in-memory Props blob; stream offset = blob offset + 4.

**Writer: a single save-time routine, `FUN_140397c00` (RVA 0x397c69 / 0x397cea)**, called from `FUN_14044fdc0` (0x44fe3e) <- `FUN_140407d10` (0x407d2e, 10,770 bytes) <- 0x490c1e <- 0x43f780 <- 0x47f308 <- 0x454205 (the save chain).  It does two things:
1. `0x397c69` calls `FUN_1403694d0` (RVA 0x36957f): this computes the project Finish word (`FUN_140369170`, `mov [rdi+8],eax` at 0x3691cb, position 92cf:186, value 0x3c0227d8) and the cleared flag (`FUN_140369430`, `mov [r14+8],di` at 0x22708e inside `FUN_140226f40`, position 931f:230c; di = 0).
2. `0x397cea` calls `FUN_14038b50c` (RVA 0x38b631, +0x125: "set record by id") which finds the record by id and stores the payload with the copy helper `FUN_140254050` (RVA 0x2540a2): for `0x02400003` a 4-byte store `mov [rax],ecx` (VCRUNTIME small copy) at position **92cf:4cc** (value `3c0227d8` to blob+0x180), for `0x02400024` a 2-byte store `mov [rax],cx` at position **9321:12c** (blob+0x344).

Then the blob is reallocated and moved at 9b30:a7 / 9b30:fb (RtlReAllocateHeap via `FUN_14024a20c`), and written at a184:de6.  Hash-checked: `vm26`, `vm27`, `vm28`.  So the record is updated *in place by id*, no framing is written for an existing record (consistent with round 2).  For the port: the Calculate Props rewrite = look the record up by id and overwrite its payload (4 bytes for `0x02400003` with the new project Finish word; 2 bytes for `0x02400024` set to 0); the value comes from `FUN_140369170` (the same engine value as the project row's Finish in the setter table).

## Next steps for the next agent

- Fixed2Data/Fixed2Meta: TTD write watch on the in-memory Fixed2 row for uid 0 (the object holding fld 309 and 310); the setter call at 99ad:2ce stores 0x40.
- The `0x43535b` bit index per field (descriptor word -> byte/bit) for the FixedMeta rows of uid 1 and 2.
- Bold (view table): in `bold` the view table (`0x..1f980`) has var-block appends (0x3dc7d0) and insert calls (0x3de484, keys 0x20400006, 0x22400014, 0x2240002f) only at positions e69e to e6c0; none after the edit's setter calls, and no key 0x2240000c: the edit's item append is not through this machinery inside the recording window, or happens before it.  See `trace-bold-r3.md`.
