# Trace of Calculate Project (target T1), Project 2021 perpetual: capture done, gate PASSED, round 2 added rows 1 and 2, value sources and a hash-checked call table

_Run Tue 10 Nov 2026 in the isolated trace VM (network disconnected). Written by the TRACE agent; not scanned or committed._

_Hash checking: the gate block below was re-typed on the host and its SHA-256 equals the one the VM printed.  The analysis tables were read off the VM screen (cdb output, zoomed screenshots) and are NOT hash-checked; they are internally consistent (every value matches the pair's changed bytes) but the coordinator should treat single hex digits as read-by-eye.  The JavaScript helper that produced them is `build\trace-2021\chain.js`; no memory dump and no name string is recorded._

| item | value |
|---|---|
| VM build line (`build.txt`, path omitted) | WINPROJ.EXE 16.0.20430.20182 sha256 F1A7E52729C01C115535B1277C5553302D15F996FCE8D433DFCE11E0FD7F5F6B |
| Office product id (VM registry) | ProjectPro2021Retail, updates off, 5-day pass ("Activation Required", full features until Sun 15 Nov 2026) |
| Input | `wave30-calculate-control.mpp` (copy hash 8E4E81D849DF, matches the repo file) |
| Edit | calculation manual, user name `trace`, open, `CalculateProject()`, `FileSaveAs`; the recorder is attached before the edit (see "Method change") |
| Saved file SHA-256 (final capture) | 7E7AF08203373196 6A82B121BE4C4329 B3F392C0D7C5931A 48A0556334C10A39 (spaces added) |
| Saved file size | 236544 bytes, 96 streams |
| Trace | `WINPROJ01.run` 812 MB, positions B8:0 to D1F2:4C, one process, 10 threads; stays in the VM |
| WINPROJ base in this trace | 0x7ff6'04ee0000 (RVA = address - base; the model's address = 0x140000000 + RVA) |

## Method change (for the coordinator): `trace-save.ps1` records almost nothing

1. `tools/trace/trace-save.ps1` waits for the `.run` file to appear, then saves. The file appears at "Initializing", before "Recording has started" is printed, so the save ran before the recording began and the trace was 0 ms long ("Recording stopped after 0ms", lifetime `[A3:0, A3:0]`). My first capture of every target had this fault; all were redone.
2. `build\trace-2021\trace-save2.ps1` waits for "Recording has started" in the attach log, then 4 s. It still records only the save.
3. `build\trace-2021\trace-save3.ps1` (used for this final capture) also starts recording BEFORE the edit, so the engine's write-back (CalculateProject), the baseline copy and the inserted rows are inside the trace. This is the only way to answer "which routine wrote the calculated word".
4. coml2 writes the compound file through memory-mapped views (4 `NtCreateSection`, 6 `NtMapViewOfSection`, no `WriteFile`). The writer chase is therefore: file bytes in a mapped view <- `ucrtbase!memcpy` <- Project's staging buffer <- the in-memory table row <- the engine's store instruction.

## Gate: traced calculate against `wave30-calculate.mpp`

`trace_gate.py calculate.mpp wave30-calculate.mpp wave30-calculate-control.mpp wave30-calculate-control-null.mpp`: **96 streams, 93 checked, 14 problem lines**, block SHA-256 3622bf8acf223df714c4865a2ef75feed797dbc6c0577f20a5bfb58a23d2702e (re-computed on the host from the copied text: equal).

```
96 streams, 93 checked, 14 problems
bytes-differ ???114/Props 24
bytes-differ ???114/TBkndAssn/Fixed2Data 40
bytes-differ ???114/TBkndCal/Fixed2Data 10
bytes-differ ???114/TBkndCons/Fixed2Data 30
bytes-differ ???114/TBkndTask/Fixed2Data 50
bytes-differ ???214/CEdl/FixedData 10
noise-whole ???214/CEdl/Var2Data 590->510
bytes-differ ???214/CEdl/VarMeta 1
bytes-differ ???214/CFilter/FixedData 20
bytes-differ ???214/CGrouping/FixedData 20
bytes-differ ???214/CTable/FixedData 10
bytes-differ ???214/CV_iew/FixedData 30
bytes-differ ???214/CV_iew/Var2Data 4
bytes-differ ???214/Props 18
size-differs ?SummaryInformation 292->284
noise-whole Props14 556->474
```

The problem set is identical to the null save's (trace-null.md), apart from the byte counts of the noise streams: every calculate stream the pair changes (Task FixedData, FixedMeta, Fixed2Data not counted below, VarMeta, Var2Data, DocumentSummaryInformation) equals the committed file. The Fixed2Data lines are the per-record 12-byte stamp words the null save also changes; `SummaryInformation` is the user-name-sized property set. Verdict: PASS.

## Analysis: the engine's write-back goes through one setter, FUN_1403dd790

Method (TTD data model in cdb, all inside the VM): execute-watch on the entry of `FUN_1403dd790` (RVA 0x3dd790, absolute 0x7ff6052bd790 in this trace) lists every call; at each entry the arguments are read (rcx = table object, edx = row, r8d = field index, r9 = pointer to the new value, [rsp+0x28] = size) and the field descriptor is read from the table object (`*(table+0x3e0) + fld*0x1c`, seven dwords).  The decompiled body (`decompiled/part_0003.c`, FUN_1403dd790) confirms it is the generic "set field value in a record store" routine: for a fixed field it stores `*(uint *)(row_base + desc.offset) = *pValue` (the store at RVA 0x3dd9f4); for a var field it goes through the var store.

Descriptor dwords, as read: `d0` (kind: 0 fixed, 1 or 2 var), `d1` (offset of the field in the 202-byte task row, 0xffff for a var field, or 0x20n for a var-store slot), `d2`, `d3` (the field key, `0x0b4000nn`, names from `analysis/field_catalog_task.tsv`), `d4`, `d5`, `d6`.  In the stream, the task FixedData offset = 48 + row offset (the 48-byte stream header), so row offset 0x54 = stream +132.

The Calculate Project call is positions 997a:6f3 to 9a08:dac in this trace.  Every call in it has table object `...8a3cc0` and row index 3 (the in-memory index of the project summary row, uid 0: the offsets match the pair's row 0 words).  **Corrected in round 2:** rows 4 and 5 (uids 1 and 2) DO go through this setter; round 1's range ended at 9a08:dac, before them (see the round-2 section at the end).

**Round 2 replaced round 1's table here** (re-checked: the project-row calls are the same, six trace positions were mis-read by eye, and rows 1 and 2 were missing because round 1 stopped at 9a08:dac).  The table below is `trace-calc-table.txt`, written by `tab.ps1` in the VM from the cdb log of `fcr("3dd790","997a:0","9d00:0",500)`, 101 calls, **SHA-256 `A07BAE1285C41154B0753F1A588935C8D48F8C55BA3C245CE940C53FE955329B`**, transcribed on the host and re-hashed: equal (LF line endings).  Columns: in-memory row (3 = project row uid 0, 4 = leaf uid 1 uid 1, 5 = leaf uid 2 uid 2), TTD position of the setter entry, field index `fld`, field key (`0x0b4000nn`, shown without the `0x0`), the descriptor's row offset (`ffff` = var field, `20nffff` = var-store slot n), descriptor kind word `d0`, and the first 4 bytes at the value pointer (the Name field's text is masked as `TEXT`; the `fld 305` and `fld 307` values are heap addresses and vary from run to run).  Stream offset in `114/TBkndTask/FixedData` = 48 + 202 * uid + row offset (uid 0 base 48, uid 1 base 250, uid 2 base 452).

```
3 997a:6f3 1 b400056 4 0 0
3 9981:2dc 0 b400017 0 0 0
3 9983:d7e 3 b40009f ffff 1 0
3 9988:2ce 305 b400477 0 0 9b304a2
3 9988:def 5 b400000 8 0 0
3 998a:7f2 6 b400002 10 0 0
3 998a:1027 7 b400004 18 0 0
3 998c:2dc 8 b400005 20 0 0
3 998c:b11 9 b400007 28 0 0
3 998c:1346 10 b400008 30 0 0
3 998e:7f2 11 b40000a 38 0 0
3 998e:15c1 14 b400013 ffff 2 1
3 9990:b5a 16 b400015 4a 0 0
3 9990:13f5 17 b400018 ffff 1 0
3 9992:c63 20 b40001d 54 0 f3c0
3 9994:6b3 21 b40001f 58 0 f3c0
3 9994:eda 22 b400020 5c 0 570000
3 9996:6b3 23 b400021 5e 0 ffff0000
3 9996:116b 25 b400023 60 0 3bf212c0
3 9998:7e4 26 b400024 64 0 3c0227d8
3 9998:1199 27 b400025 68 0 3bf212c0
3 999a:2dc 28 b400026 6c 0 3c0227d8
3 999a:c91 29 b400027 70 0 3bf212c0
3 999c:2dc 30 b400028 74 0 3c0227d8
3 999e:2dc 37 b400063 84 0 ffffffff
3 999e:b03 38 b400064 88 0 ffffffff
3 99a2:2dc 54 b4000c9 a8 0 80
3 99a4:2dc 64 b4000f9 ac 0 ffff0000
3 99a4:242e 74 b400182 ffff 400 0
3 99a7:8cb 75 b400183 ae 0 ffffffff
3 99a9:2dc 81 b4001b6 ba 0 0
3 99a9:bfb 82 b4001b7 be 0 0
3 99ab:119f 86 b4001df ffff 10 1
3 99ad:2ce 309 b4004e2 2a 0 40
3 99af:2ce 310 b4004e7 2e 0 ffffffff
3 99b1:2ce 315 b400508 3a 0 0
3 99b7:490 88 b4000a4 201ffff 0 0
3 99bd:345 89 b4000a5 202ffff 0 0
3 99bf:692 90 b4000a8 203ffff 0 0
3 99c3:31e 91 b4000a9 204ffff 0 0
3 99c5:31e 92 b4000aa 205ffff 0 0
3 99ee:3c8 100 b40000e 20dffff 0 TEXT
3 9a08:dac 1004 b4004e4 201ffff 0 1
4 9a12:c20 1 b400056 4 0 1
4 9a14:687 0 b400017 0 0 1
4 9a14:1448 3 b40009f ffff 1 0
4 9a16:9c2 305 b400477 0 0 ab304a2
4 9a18:572 14 b400013 ffff 2 1
4 9a1a:b5a 16 b400015 4a 0 0
4 9a1e:2dc 27 b400025 68 0 3bf212c0
4 9a1e:c91 28 b400026 6c 0 3bfd27d8
4 9a20:2dc 29 b400027 70 0 3bf212c0
4 9a20:c91 30 b400028 74 0 3bfd27d8
4 9a26:d86 64 b4000f9 ac 0 3d270001
4 9a28:210f 74 b400182 ffff 400 0
4 9a2c:2dc 75 b400183 ae 0 ffffffff
4 9a2c:18da 81 b4001b6 ba 0 0
4 9a2c:21f9 82 b4001b7 be 0 0
4 9a30:106a 86 b4001df ffff 10 1
4 9a34:889 307 b40047f 18 0 9b304a2
4 9a36:2ce 309 b4004e2 2a 0 30
4 9a38:2ce 310 b4004e7 2e 0 3bf13138
4 9a6f:99e 1005 b4004e5 202ffff 0 1
5 9a77:c20 1 b400056 4 0 2
5 9a79:687 0 b400017 0 0 2
5 9a79:1448 3 b40009f ffff 1 0
5 9a79:1e4d 305 b400477 0 0 bb304a2
5 9a7b:2dc 5 b400000 8 0 0
5 9a7b:b11 6 b400002 10 0 0
5 9a7b:1346 7 b400004 18 0 0
5 9a7d:7f2 8 b400005 20 0 0
5 9a81:2dc 9 b400007 28 0 0
5 9a85:2dc 10 b400008 30 0 0
5 9a89:2dc 11 b40000a 38 0 0
5 9a8b:d8c 14 b400013 ffff 2 1
5 9a8d:b5a 16 b400015 4a 0 0
5 9a8f:12c1 22 b400020 5c 0 40d20000
5 9a93:2dc 23 b400021 5e 0 0
5 9a93:d94 25 b400023 60 0 3c0012c0
5 9a95:2dc 26 b400024 64 0 3c0227d8
5 9a97:972 27 b400025 68 0 3c0012c0
5 9a97:1327 28 b400026 6c 0 3c0227d8
5 9a97:1cdc 29 b400027 70 0 3c0012c0
5 9a97:2691 30 b400028 74 0 3c0227d8
5 9a9a:e7b 64 b4000f9 ac 0 3c020001
5 9a9b:209 72 b40017d ffff 20000 0
5 9a9b:e1e 74 b400182 ffff 400 0
5 9a9f:2dc 75 b400183 ae 0 3bfd27d8
5 9aa3:2dc 81 b4001b6 ba 0 0
5 9aa5:8dc 82 b4001b7 be 0 0
5 9aa5:1aba 86 b4001df ffff 10 1
5 9aa7:9be 307 b40047f 18 0 9b304a2
5 9aab:2ce 309 b4004e2 2a 0 28
5 9aaf:2ce 310 b4004e7 2e 0 3bfd27d8
5 9ab5:48c 88 b4000a4 201ffff 0 0
5 9ab9:1ba 89 b4000a5 202ffff 0 0
5 9abd:32d 90 b4000a8 203ffff 0 0
5 9abf:1ba 91 b4000a9 204ffff 0 0
5 9ac3:1ba 92 b4000aa 205ffff 0 0
5 9af2:71f 1003 b4004e3 200ffff 0 1
5 9af4:454 1005 b4004e5 202ffff 0 1
```

Field names (`analysis/field_catalog_task.tsv`): b400056 UniqueID, b400017 ID, b400477 Guid, b400000 Work, b400002 ActualWork, b400004 RemainingWork, b400005 Cost, b400007 ActualCost, b400008 FixedCost, b40000a RemainingCost, b400013 Critical, b400015 FreeSlack, b400018 Milestone, b40001d Duration, b40001f RemainingDuration, b400020 PercentComplete, b400021 PercentWorkComplete, b400023 Start, b400024 Finish, b400025..28 EarlyStart, EarlyFinish, LateStart, LateFinish, b400063 Resume, b400064 Stop, b4001b6 StartSlack, b4001b7 FinishSlack, b4004e2 RecalcFlags, b4000a4/a5/a8/a9/aa the five overtime fields.  Unnamed: b40009f, b4000c9, b4000f9, b400182, b400183, b4001df, b40047f, b4004e3..e7, b400508, b40017d.

Offsets for the fixed fields are the descriptor's row offset plus 48; `fld` is the engine's field index (descriptor index), distinct from the key.  The pair's measured words (`scheduler-save.md`) are exactly these: Duration and RemainingDuration (+132, +136), and Start..LateFinish (+144 to +167: the note's "+98..+119" are the same words as row offsets 0x60 to 0x77).  The five zero overtime blocks and the 0x4e4 block are the var-slot calls at 99b7 to 99c5 and 9a08.  The other four of the nine new blocks (uid 1 0x4e5, uid 2 0x4e3 and 0x4e5, and the uid 0 count) are for rows 1 and 2, which have no call through this setter.

### Call chain of the row-0 stores (the 4-byte store at FUN_1403dd790 +0x264, RVA 0x3dd9f4)

Stack at the store, innermost first: 3dd282 (FUN_1403dd1b0 +0xd2), 3a17e2 (FUN_14039ed80 +0x2a62, a 13915-byte function), 377cbe (FUN_140377ba0 +0x11e), 43f8d6 (FUN_14043f6f0 +0x1e6), 47f308 (FUN_14047ead0 +0x838), 454205 (FUN_1404541f0 +0x15), 411dea (FUN_140411dc0 +0x2a), 410f00 (FUN_140410e40), 410c76 (FUN_140410c40), 3e0d8b (FUN_1403e0350), 4149f4 (FUN_14041262c), 3b4e4f (FUN_1403b2f6c), 2a0589 (FUN_1402a0110), 29f5d5 (FUN_14029f4e0).  The value is passed by pointer from FUN_1403dd1b0's caller; the computation of the date and duration values (eax = 0x3bf212c0 and so on) is upstream of FUN_14039ed80 and was not traced further.

## Save-time chain: who writes each stream

The compound file is written through `coml2` (memory-mapped views).  For every table stream the Project side of the `IStream::Write` call is the same chain: 427e2e (FUN_140427d70 +0xbe) <- 440c73 (FUN_140440bdc +0x97) <- 440ad5 (FUN_140440ac0 +0x15) <- **FUN_140377770** (+0x14c for FixedMeta, +0x1a6 for FixedData and the assignment FixedData, +0x1e7 for Var2Data) <- 377ce7 (FUN_140377ba0 +0x147) <- 43f8d6 (FUN_14043f6f0 +0x1e6) <- 47f308 (FUN_14047ead0 +0x838) <- 454205 (FUN_1404541f0 +0x15) <- 411dea (FUN_140411dc0).  The staging buffer for a FixedData stream is filled by a `memcpy` in FUN_140427584 (+0x199, called through 42752b FUN_1404274d0, 3dea2f FUN_1403de850, 3dde07 FUN_1403ddd7c, 3dd939 FUN_1403dd790 +0x1a9, 3dd282 FUN_1403dd1b0).  The Task Var2Data blocks are written from FUN_140427584 +0x562 on a second path (427ae6, 46cf0c FUN_14046cd30, 3777eb FUN_140377770 +0x7b).

| stream | pattern located in the docfile cache at | final writer position | Project caller of IStream::Write |
|---|---|---|---|
| Task FixedData (+144 run) | view 2 +0x17090 | 9bbd:1824 (`ucrtbase!memcpy`, 32 bytes) | FUN_140377770 +0x1a6 (377916) |
| Task Var2Data (the `01 00 ff ff` blocks) | view 2 +0x12150 | 9bd2:7de | FUN_140377770 +0x1e7 (377957) |
| Task Var2Data (the `01 00 00 00` block) | view 2 +0x12144 | 9bd2:7dc | FUN_140377770 +0x1e7 (377957) |
| Task FixedMeta (entry 3 +2/+9 run) | view 2 +0x1309f | 9bb9:2ea | FUN_140377770 +0x14c (3778bc) |
| Assn FixedData (+164 run) | view 2 +0x260a0, +0x2c0a0 | a0fa:ec4 and a10f:1696 | FUN_140377770 +0x1a6 (377916) and a second path via 427ae6 |

## Not done in round 1 (superseded: see the round-2 section below)

- Task rows 1 and 2 (uid 1 and uid 2: stream +424 onward, the `ffffffff` word at +424, the date words at +550 to +570, FixedMeta entries 3 to 5, Fixed2Data records and Fixed2Meta): no call through FUN_1403dd790 in the Calculate range, so they are written by another routine; the pattern search over the heap range around the row-0 block did not find their in-memory images at the staging position.
- The origin of the computed values (the date arithmetic upstream of FUN_14039ed80).
- The nine VarMeta entries' offset words (264, 276, ...): the append routine was not located.  The var slot calls above are where the blocks come from.
- DocumentSummaryInformation (175 bytes).

## Round 2 results (Wed 11 Nov 2026, same trace, same VM build)

Method: the same execute-watch on `FUN_1403dd790`, now over the whole Calculate range (positions 997a:6f3 to 9af4:454, 101 calls; table above), then two hops back from each value pointer with TTD.Memory write queries (`chain.js`: `vsrc`, `srcw`, `hops`).  Function names from `analysis/meta/functions.tsv` (`fnmap.py`).  The hop output was read off the VM screen and is NOT hash-checked; the 101-call table is.

### Answers

1. **Task rows 1 and 2 (uids 1 and 2, in-memory rows 4 and 5) are written by the same setter, `FUN_1403dd790`.**  Round 1's range ended at 9a08:dac, inside the project row's last calls.  Row 4 (leaf uid 1) is set at 9a12:c20 to 9a6f:99e (20 calls), row 5 (leaf uid 2) at 9a77:c20 to 9af4:454 (38 calls).  The order is row 3, then 4, then 5 (the in-memory index is uid + 3: three rows of the project, A and B).
2. **The engine rewrites a fixed set of fields on every row and the stream changes only where the value differs.**  Row 4 writes EarlyStart, EarlyFinish, LateStart and LateFinish with their old values (the pair shows no change there), so those calls change no byte; its one byte change is the `0xae` word (key `0x0b400183`, `c012f23b -> ffffffff`).  Row 5 writes Start to LateFinish (`0x60`..`0x77`), PercentComplete (`0x5c`, 2 bytes, no byte change), `0xac` and `0xae`.  For a port: the write-back is "set this field list on every row, compare, store", not a list of known-moving words.
3. **The nine new var blocks are all var-slot calls of this setter** (field index, key, slot):
   - uid 0 (row 3): fld 88..92 (`0x0b4000a4`, `a5`, `a8`, `a9`, `aa`, slots `0x201`..`0x205`) value 0, and fld 1004 (`0x0b4004e4`, slot `0x201`) value 1;
   - uid 1 (row 4): fld 1005 (`0x0b4004e5`, slot `0x202`) value 1;
   - uid 2 (row 5): fld 1003 (`0x0b4004e3`, slot `0x200`) value 1 and fld 1005 (`0x0b4004e5`, slot `0x202`) value 1.
   - Row 5 also calls fld 88..92 with value 0 and creates NO block for them; row 3's identical calls create blocks.  What differs (the project row versus a task row, or an existing zero) is open.
   - A decision input for the port: the set of blocks depends on the row, and the engine calls `0x4e4` only for the project row, `0x4e3` only for leaf uid 2, `0x4e5` for both tasks.
4. **Where the values come from.**  Every value in the three rows is passed by pointer to a 4-byte slot that `FUN_14039ed80` (return address `0x3a16b2`, +0x2932) filled with a `memcpy` from an engine result buffer.  The buffer's own contents were written earlier (positions 3d2e to 3d97, before the setter calls) by a second `memcpy` and, one hop further back, by a register store inside the engine's field computers:

| value | setter entry | slot copy (memcpy) | result buffer written (memcpy) | the store, FUN_ and offset |
|---|---|---|---|---|
| project Duration (+132) | 9992:c63 | 9992:b21 | 3d97:1180 | 3d97:c43 `mov [rsp+10h], edx` in `FUN_14028e298` +0x1598 (edx = 0xf3c0) |
| project Start (+144) | 9996:116b | 9996:1029 | 3d8a:1e65 | 3d8a:18e1 `mov [rsp+18h], r8d` in `FUN_140290dec` +0x13f9 |
| project Finish (+148) | 9998:7e4 | 9998:6a2 | 3d8c:197a | 3d8c:1439 same instruction form in `FUN_140290dec` +0x1439 |
| project LateFinish (+164) | 999c:2dc | 999c:19a | 3d6c:1d9f | copy made inside `FUN_14028e298` +0x75c (not hopped further) |
| project RecalcFlags (+90) | 99ad:2ce | 99ad:19a | 3d35:b3f | 3d34:2355 in `FUN_140290dec` +0x1309 |
| leaf uid 1 EarlyStart (row 4 +0x68) | 9a1e:2dc | 9a1e:19a | 3d32:1c9f | 3d32:1762 `mov [rsp+10h], edx` in `FUN_14028e298` +0x778 |
| leaf uid 1 word 0xae | 9a2c:2dc | 9a2c:19a | 3d2e:7e9 | 3d2e:274 `mov [rsp+58h], edx` in `FUN_14047ada0` +0x38 (a 48-byte helper) |
| leaf uid 2 Start (row 5 +0x60) | 9a93:d94 | 9a93:c52 | 3d73:31a | 3d72:24a6 in `FUN_140290dec` +0x13f9 (the same instruction as the project's Start) |

   The routines that compute the dates are `FUN_140290dec` (10,621 bytes: Start, Finish, flags) and `FUN_14028e298` (11,042 bytes: Duration, early and late dates); both are called from `FUN_1402985a0` (18,193 bytes) through `FUN_140293a34` / `FUN_14056c3b8` / `FUN_1402974c0`.  Each stores a value that arrives in a register (`edx` or `r8d`); the arithmetic itself (calendar, working-time, successor) is upstream of those stores and was not decomposed.  The `0xae` word of leaf uid 1 is set by the small helper `FUN_14047ada0` called from `FUN_14038e4c8` +0x38c (callers `FUN_140402600`, `FUN_140402030`, `FUN_1402985a0` +0x3d6a).  The project row's Start and leaf uid 2's Start use the same store instruction, so the same routine computes both.

### Per-stream-offset rows (runbook section 6 step 7), Task FixedData

| stream offset | old -> new | writing routine (RVA) | call stack of the final store | value source | TTD |
|---|---|---|---|---|---|
| +132 (uid 0) | 0000 -> f3c0 | `FUN_1403dd790` 0x3dd9f4 | 3dd282 `FUN_1403dd1b0`, 3a17e2 `FUN_14039ed80` +0x2a62, 377cbe `FUN_140377ba0`, 43f8d6, 47f308, 454205 | `FUN_14028e298` +0x1598 (table above) | 9992:c63 |
| +136 (uid 0) | 0000 -> f3c0 | same | same | `FUN_14028e298` family (slot copy 9994:571; not hopped) | 9994:6b3 |
| +144..+167 (uid 0) | three runs `18 3d c0 12 18 3d -> f2 3b d8 27 02 3c` | same | same | Start `FUN_140290dec` +0x13f9, Finish +0x1439, early and late copies as in the table | 9996:116b to 999c:2dc |
| +424 (uid 1 base 250 + 174) | `c0 12 f2 3b -> ff ff ff ff` | same | same | `FUN_14047ada0` +0x38 | 9a2c:2dc |
| +550..+571 (uid 2 base 452 + 98..119) | six 2-byte runs (Start to LateFinish) | same | same | Start `FUN_140290dec` +0x13f9; Finish etc. copies | 9a93:d94 to 9a97:2691 |
| +626 (uid 2 + 174) | `c0 12 f2 -> d8 27 fd` | same | same | the same `0xae` word, value 3bfd27d8 | 9a9f:2dc |

The per-field values for every call are in the table above; the stream offset of any call is 48 + 202 * uid + its row offset.

### Still open

- The date arithmetic upstream of the stores in `FUN_140290dec` and `FUN_14028e298`.
- FixedMeta (entries 3 to 5), Fixed2Data records and Fixed2Meta `+95`: not decomposed.  The setter calls `*(vtable + 0x68)` (a dirty or changed notification) after each store, which is the probable writer of the per-row presence bytes; not traced.
- The nine VarMeta entries' offset words: the var-slot calls above create the blocks; the routine that appends the offset word was not isolated.
- DocumentSummaryInformation (175 bytes).
- Why row 5's overtime calls (value 0) create no block while row 3's do.
