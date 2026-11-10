# Insert Milestone (round 4): the 32-byte Assn Fixed2Data run and the 3-byte Assn Var2Data run

_Wed 11 Nov 2026, round 4.  Trace `insert-milestone` (trace-save3: edit and save inside the recording), public fixtures, VM saved file text excerpts (hex of 32 bytes; no names).  Written to `build\trace-2021\` only._

**Hash-checked.**  KVP `sha OK` (first 12 hex digits): UuidCreate list `6EAC65DDB610` (r4c2), saved-file excerpts `D9B0C947C330`, VarMeta/Var2Data listing `82F83082EBF3`, setter excerpts `9C40AC4FA8B5` and `7DEE6B127047`, GetLocalTime callers `A895954A648B`, value chain `D4944CC19D84`, `trace-fc4-insert-milestone.txt` `B8BD1EB8AB41`.  Interpretation is mine.

## 1b-1. `TBkndAssn/Fixed2Data` +144, 32 bytes: two time-based GUIDs (writer: `FUN_1403dd790` fields 205 and 206; value: `UuidCreateSequential` through 0x22cdb0)

Geometry (derived): the stream is **48 bytes per assignment row** (192 = 4 rows): `+0` 16 bytes = field **0x0f40027c** (a GUID, flag word 0x1020), `+0x10` 16 bytes = field **0x0f40027d** (a GUID), `+0x20` 16 bytes = field **0x0f40027e** (8 bytes `78 8b cb a0 8c 2a 6d 43`-style value, zeros, `ff`; it is equal in the VM file and the pair).  The new row (row 3) starts at +144.

- The VM file has at +144 `ef eb 87 99 dc fa 11 f1 b9 5b 00 15 5d xx xx xx` and at +160 `ef eb 87 93 dc fa 11 f1 ...`: version-1 (time-based) GUIDs whose node is the VM's Hyper-V MAC; the pair has GUIDs of another machine (`... 9f 64 1c c1 0c 05 4c 03`).  That is the whole difference: **per-save noise**, not data; the pair's two GUIDs and the VM's differ in every byte that depends on time and node.
- Writer: the setter calls on row 3: `152b71:2125` fld 205 key 0x0f40027c value dword `9987ebef` and `152b79:878` fld 206 key 0x0f40027d value `9387ebef` (rows 0 to 2 got `9687ebef`/`9087ebef`, `9787ebef`/`9187ebef`, `9887ebef`/`9287ebef`, i.e. the same two fields on the existing rows: these are the other 12-byte stamp runs of the gate).
- Value source: `rpcrt4!UuidCreateSequential` called from **WINPROJ+0x22cdb0** (one call per GUID; 23 calls in the recording, results `8c87ebef...` to `a287ebef...` counting up by one in the first byte, consistent with the "+0x11" steps of the Calculate stamps: single bytes of such a counter differ between the control and the pair; not individually matched).  The calls for this row are at positions 13bd02:1f2 (GUID `...93`) and 13c480:1f7 (GUID `...99`).
- For the port: write two fresh version-1 GUIDs (any value) at the new row's +0 and +16; the existing rows' GUIDs are regenerated at every save too (the 12-byte runs in the gates).

## 1b-2. `TBkndAssn/Var2Data` +400, 3 bytes: the assignment's creation date-time word (var key 0x0f40027a)

VarMeta of the VM file (listed by `vm_im2.py`): block uid 4 key **0x0f40027a** at offset 396 (length 4) holds `f8 34 27 3d` (VM) against `92 22 18 3d` (pair); the other assignments' blocks of the same key hold `82 14 14 3d`.  The word is `(day number << 16) | time`, the date half `0x3d27` and the time half `0x34f8`: the creation date and time of the row in Project's date format (time half in tenths of a minute: 0x34f8 = 1356 min = 22:36, the wall-clock time of the capture on 10 Nov 2026).  It differs per session, so it is per-save noise again.

Writer: the setter call `152b80:1607` (table row 3, fld 425, key 0x0f40027a, value `3d2734f8`, var slot, flags 0x80ea), which appends the var block (0x3dc7d0 path of trace-calculate-r3.md).  Value source: `GetLocalTime` (`kernelbase`) called from **WINPROJ+0x1db4db** (calls at 13b7af:4f and four more, the nearest before the row's creation is 13b7af:4f), converted to the day/tenth-minute word; the memcpy chain to the setter's value pointer runs through 0x221e25 <- 0x22fecd <- 0x22eff5 <- 0x229e32 <- 0x38bb70 <- 0x4308f9.  The Task FixedData creation stamp of insert-summary (+984, 3 bytes) is the same kind of word (not separately traced).

## 1c. Per-stream-offset rows

`trace-rowmap-insert-milestone.txt` and `trace-rowmap-insert-summary.txt` (host, `mapdiff.py`): for each changed run of Task FixedData (`+452` and `+654` are the ID renumber, key 0x0b400017, setter positions 1529c6:823 and 1529e7:24c6; insert-summary also `+594` key 0x0b4000a0 `0 -> 4` and `+624` key 0x0b4000f9 `1 -> 2`), and the 21 / 4 / 1 / 8 non-zero fields of the new Task FixedData row (+856..1058), Task Fixed2Data row (+448..512), Assn Fixed2Data row (+144..192) and Assn FixedData row (+330..440) whose saved first dword equals the setter call's value.  The Fixed2Data single-byte differences at `rel 3`, `rel 19`, `rel 27` of each row are the low byte of the GUID counter (above).
