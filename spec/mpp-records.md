# `.mpp` record layout — Task table, byte level (MPP14)

> **Correction (2026-10-22, W5).** The VarMeta framing in §5.1, the field-key
> attribution in §9 and the `Task.Name` key in §11 are **withdrawn**. VarMeta is
> a **24-byte header + n × 12-byte entries `(uid, offset, key)`**; the keys are
> ordinary PjField ids (*Name* = `0x0b40000e`, *Notes* = `0x0b40000f`), so
> `analysis/field_id_map.tsv` applies. An earlier revision parsed a 32-byte
> header and started the entry array one entry late, which paired every key with
> the *next* entry's uid/offset. §0 item 4, §5.1, §8's PROJECT_A entry count, §9 and
> §11 have been corrected inline. Evidence: [`mpp-field-keys.md`](mpp-field-keys.md)
> and `scripts/fk_*.py`. The FixedData, date/duration and Var2Data framing
> findings are unaffected.

Second pass over the container, 2026-10-22. Companion to
[`mpp-container.md`](mpp-container.md), which covers the OLE2 wrapper and the
per-table stream set. This file answers the six questions in the W1 brief, with
the command and byte offsets for every claim.

Everything marked **verified** was reproduced by a script committed next to this
document. Anything marked **inference** says what would prove it.

## Evidence base

| file | rows (`TBkndTask/FixedData`) | note |
|---|---|---|
| `corpus/probe/base.mpp` | 4 (project + 3 tasks) | reference; A 5d / B 3d / C 2d, start 2026-01-05 |
| `corpus/probe/var-*.mpp` | 4 | one-property deltas from `base.mpp` |
| `corpus/probe/n1..n6.mpp` | 1..6 | one project row + N identical 2d tasks |
| `corpus/project_a.mpp` | 85 | 84 tasks, heavily edited, saved 2026-07-22 |
| `corpus/project_b.mpp` | 86 | 85 tasks, saved 2026-03-10 |

Re-run everything:

```
python scripts/probe_fixedmeta.py corpus/probe/base.mpp corpus/project_a.mpp
python scripts/probe_varmeta.py  corpus/probe/base.mpp corpus/project_a.mpp   # old framing; mid=uid result unaffected
python scripts/probe_fixtures.py                 # base vs each var-*.mpp
python scripts/probe_names.py corpus/project_a.mpp   # superseded, see banner
```

`scripts/probe_lib.py` holds the shared parsing (stream loader, FixedMeta/VarMeta
parsers, Var2Data block reader, packed-date decoder). **Its `parse_var_meta()`
uses the old 32-byte-header framing** and is superseded by `scripts/fk_lib.py`
for the Task table; the corrected Task probes are the `scripts/fk_*.py` set
(see the correction banner).

## 0. Corrections to `mpp-container.md` — read these first

1. **FixedMeta is per-row bookkeeping, not a field map.** It is
   `16 + 47 * (rows + 3)` bytes, and it contains **zero** field ids. The earlier
   note that it "describes the set of fields stored in the fixed area" is wrong:
   its size tracks row count exactly (`probe_fixedmeta.py`).
2. **`mid` in VarMeta is the row's UniqueID**, not a row index and not an
   offset. Fixtures have uids 1..N so the two coincided; the real files do not
   (uids 8141, 10805, ...). Verified by matching every `mid` to the u32 at
   `FixedData + 48 + 202*row + 4`.
3. **The VarMeta "trailer"/"split entry" does not exist.** It was an artefact of
   the old 32-byte-header parse (§5.1): the header's last two words are entry
   0's uid and offset, and the trailing word is the last entry's key. The
   stream is a flat list of `n` 12-byte entries.
4. **VarMeta entries are `(uid, offset, key)` after a 24-byte header** — see
   §5.1. The file keys **are** the COM PjField ids: Task *Name* is key
   `0x0b40000e` and *Notes* is `0x0b40000f`, so `analysis/field_id_map.tsv`
   applies. The earlier claim in this document that Name is `0x0b400564` came
   from an off-by-one entry shift; see `spec/mpp-field-keys.md` §0.
5. **`Fixed2Data` is not baseline-vs-current.** It is a 64-byte per-row identity
   record (GUID-like id, level double, parent/previous id) and it is sized
   `64 * (rows + 3)`, with a `16 + 96 * (rows + 3)` Fixed2Meta. See §7.

## 1. FixedData (Task)

```
FixedData = 48-byte header + rows * 202 bytes
rows      = tasks + 1          (row 0 is the project row, §6)
```

`base.mpp`: `856 = 48 + 4*202`; PROJECT_A: `17,218 = 48 + 85*202`; PROJECT_B:
`17,420 = 48 + 86*202`.

The 48-byte header is three 16-byte records, identical in every file examined:

```
00 00 ff ff 00 00 00 00 00 00 00 00 00 00 00 00
01 00 ff ff 00 00 00 00 00 00 00 00 00 00 00 00
02 00 ff ff 00 00 00 00 00 00 00 00 00 00 00 00
```

(`probe_fixedmeta.py` prints it.) The three FixedMeta entries 0..2 point at
offsets 0, 16, 32, so these header slots are described by the same mechanism as
the rows.

### 1.1 FixedMeta layout (brief item (a))

```
FixedMeta = 16-byte header + 47-byte entries, entries = rows + 3
header:  ba ad df fa | u32 4 or 6 | u32 entry_count | u32 FixedData size
```

Verified sizes: 251 = 16+5*47 (n1), 298 = 16+6*47 (n2), 345 = 16+7*47 (base/n3),
4,152 = 16+88*47 (PROJECT_A), 4,199 = 16+89*47 (PROJECT_B). The header's third word is
`rows + 3` in every file except PROJECT_A, which says 87 while 88 entries are present
(see §8).

Entry (`probe_fixedmeta.py` prints these):

| off | size | meaning |
|---|---|---|
| +0 | u16 | zero in every entry examined |
| +2 | u16 | small counter (9..145). Correlates loosely with the number of stored fields but is **not** equal to the VarMeta entry count (PROJECT_A row 1: 16 vs 18). Meaning open. |
| +4 | u32 | byte offset into FixedData: 0/16/32 for entries 0..2, then `48 + 202*row` |
| +8 | 16 B | opaque per-row blob; changes when the row content changes, looks like a save/row identity (see §2) |
| +24 | 23 B | mostly zero; byte +44 flips on some edits (0x08 on 50 % complete, 0x10 with notes) — flags, meaning open |

**Answer to the brief's central question: FixedMeta is purely positional
bookkeeping; it carries no field ids and no per-field (offset, size) list.**
`probe_fixedmeta.py` scans the whole stream for the `40 0b` byte pair and finds
0 hits in every file; the same scan finds 42/1,081 hits in VarMeta, which is the
only Task stream that stores field ids. Consequently a field id is located in
the 202-byte row **by a compiled schema, not by anything stored in the file**.
The positions that schema uses are recovered empirically in §2.

The first three entries are the FixedData header descriptors:

```
entry0 [0] off=0   entry1 [1] off=16   entry2 [2] off=32
```

They are the only entries whose +4 is not a row offset, which is why
`rows + 3` is the entry count.

## 2. Field positions inside the 202-byte row (verified by one-change fixtures)

`scripts/probe_fixtures.py` diffs `base.mpp` against each `var-*.mpp` and maps
every changed byte to `(row, intra-row offset)`. All offsets below are relative
to the start of the 202-byte row; absolute file offset is `48 + 202*row + off`.

| intra | type | field | fixture proof |
|---|---|---|---|
| 0 | u32 | row id (dense, in table order) | base rows 0,1,2,3; PROJECT_A 0,13,5,12,... |
| 4 | u32 | **UniqueID** | base 0,1,2,3; PROJECT_A 0,8141,8142,8166,10720,... |
| 78 | u16 | **Priority** | `var-priority`: row 3, `f4 01`→`84 03` (500→900) |
| 80 | u32 | ActualDuration (ticks) | `var-pct`: row 1 0→12000 (2.5d), row 0 0→5400 |
| 84 | u32 | **Duration** (ticks) | `var-duration`: row 2 `40 38`→`c0 5d` (14400→24000) |
| 88 | u32 | RemainingDuration | `var-duration` 14400→24000; `var-pct` 24000→12000 |
| 92 | **u16** | **PercentComplete** | `var-pct`: row 1 0→50, project row 0→25. Only the low two bytes are used; `var-pct` row 1 holds `32 00 00 00` at +92, so a u32 read yields `0x00000032`. W5 corrected §5.1 and the reader already reads u16; this row was the last u32 statement. |
| 96 | u32 | **calculated Start** (packed date, §3) | `var-projstart`: row 0 `78 1e f1 3b`→`d0 20 84 3c`. **Corrected 2026-10-22:** this is the CPM-calculated copy, not always the effective date — see +104 below |
| 100 | u32 | **calculated Finish** | same fixture, bytes 100..103 |
| 104/108 | u32 pair | **effective Start/Finish** (what the UI reports) | manual-start-edit (Project's own save after setting a manual task's Start): +104/+108 move to the new dates while +96/+100 stay at the old calculated ones; on auto-scheduled files the pairs are equal (w3-o1) |
| 112/116 | u32 pair | **LateStart/LateFinish** | w3-o1 uid 1: +112/+116 = 10-26 08:00/17:00, exactly COM's LateStart/LateFinish; manual-start-edit moves them with Start/Finish when slack is 0 |
| 174 | u32 | **start anchor** (packed date) | manual-start-edit: Project wrote the new manual Start here; PROJECT_A auto tasks store the unsnapped predecessor-finish bound (uid 10759 = 08-10 17:00 while Start is 08-11 08:00; 10760 = 08-14 17:00 while Start is 08-17) |
| 178 | i32 | **task calendar uid** (-1 = NA) | holiday1.mpp T1/T2/T3 = 3 (SiteCal); PROJECT_A/PROJECT_B = -1 on every row; `core/cal::find_calendar_by_uid` consumes it |
| 182 | u32 | **Deadline** (packed date, NA) | W19: schedin DL stores 2026-10-21 17:00 = COM's deadline; every row without one stores 0xFFFFFFFF |
| 120..139 | 5 × u32 | actual/stop/resume dates | `var-pct` fills them with `0x3bf412c0` (2026-01-07 08:00) etc. |
| 128 | u32 | save/current date | base row 0 `0x3d141482` = 2026-10-22 08:45, same as the file mtime |
| 74 | i32 | **FreeSlack** (6-second ticks, signed) | **Corrected 2026-10-22 (W30):** `milestone1.mpp` ID6 stores +74 = 0 while COM reports FreeSlack 0 and TotalSlack 22080; on w3-o1/outline1/lagff +74 equals COM's FreeSlack. The old "TotalSlack at +74" reading came from `w3-o1`, where all four slack fields are equal. |
| 186 | i32 | **StartSlack** | `outline1.mpp`'s summary stores +186 = 19200 ticks = 1920 min = COM StartSlack; equals TotalSlack on rows where the fields coincide |
| 190 | i32 | **TotalSlack** | w3-o1/outline1/lagff/milestone +190 equals COM TotalSlack; `schedin`'s ALAP task stores +190 = 52800 ticks = the engine's 5280 min (the old "ALAP divergence" was this +74 misread); 67/67 PROJECT_A leaves match after the correction. `Critical` derives from +190 <= 0 (and !manually_scheduled). |

`var-name`, `var-notes` and `var-title` change **no bytes at all** in FixedData
(probe output "FixedData: no bytes changed"), which is the positive control:
string properties live entirely in Var2Data, and the fixed row layout does not
shift when they change.

Fields with the same value in every row (48-byte header, id/uid, priority
default 500, date copies) are laid out identically in all five files: the row
schema is fixed per table, not per file.

## 3. Packed date/time (verified)

A date is one u32:

```
value = (day << 16) | (seconds_since_midnight // 6)
```

* tick = 6 seconds (1/14400 day);
* day 15346 = 2026-01-05 (epoch: day 0 = 1983-12-31);
* examples: `0x3bf212c0` = day 15346, 4800 ticks = **2026-01-05 08:00**;
  `0x3bf327d8` = day 15347, 10200 ticks = **2026-01-06 17:00** (a working-day
  boundary, from `var-pct`); `0x3d141482` = day 15636, 5250 ticks =
  **2026-10-22 08:45**, equal to the file's save time.

Calibration evidence: `var-projstart` moves the project from 2026-01-05 to
2026-06-01, exactly +147 days, and the stored day moves +147 (15345→15492)
while the time tick moves +600 (13:00→14:00). That one-hour shift is the
Sydney DST change, which fixes the low half as clock time and the high half as
a day index. **Inference:** date-only project fields are stored as UTC (project
start reads 2026-01-04 13:00 = 2026-01-05 00:00 Sydney) while task working times
are local wall time (08:00/17:00). A pair of fixtures on either side of a DST
boundary would settle it.

## 4. Duration (verified)

Durations use the same 6-second tick: **4800 ticks = one 8-hour working day**.

* task B 3d: 14,400; after the fixture changes it to 5d: 24,000;
* n1's 2d task: 9,600; C 2d: 9,600; A 5d: 24,000;
* project summary: 21,600. **Inference:** that is 4.5 working days because the
  project row's start is midnight and the tasks start 08:00; the value itself
  is measured, the explanation is not.

This is why `Duration` in the fixed row is in ticks, not minutes. A reader must
divide by 4800 for days at the default calendar.

## 5. VarMeta (brief item (b)) and Var2Data framing (item (e))

### 5.1 VarMeta layout (corrected 2026-10-22)

```
24-byte header:
  +0x00 ba ad df fa
  +0x04 0
  +0x08 entry count n              (base 42, PROJECT_B 1437; PROJECT_A's header says
                                    1076 while 1,081 entries are present, §8)
  +0x0C 0
  +0x10 0
  +0x14 Var2Data size              (equals the real stream length exactly)
then n x 12 bytes:  u32 uid | u32 Var2Data offset | u32 field key
```

Base: `528 = 24 + 42*12`; PROJECT_A: `12,996 = 24 + 1,081*12`; PROJECT_B:
`17,268 = 24 + 1,437*12`.

Entry 0 of `base.mpp` is `uid 0 | offset 76 | key 0x0b40000e`, and the block at
76 is `b'b\x00a\x00s\x00e\x00\x00\x00'` ("base") — the project name/title.
`var-title.mpp` puts "Probe Title" in the same place. PROJECT_A and PROJECT_B store
their title in a normal entry with `uid 0` and key `0x0b40000e`: PROJECT_A entry 2
(offset 24) = `"Project A"`, PROJECT_B entry 2 (offset 3,250)
= `"Project B"` — the offset differs per file, the key does not.

The key is a full PjField id (`0x0b40xxxx`). mpxj's `VarMeta12` reader consumes
only its low 16 bits (`readShort` after the offset) and its field map is keyed
on those (`NAME` = 14, `NOTES` = 15); that truncation is an implementation
detail, not evidence that the file stores 16-bit keys.

**There is no "split entry".** The previous revision of this section described a
32-byte header with a `+0x18/+0x1C` mid/offset pair, a trailing field id and a
"count+1" — all an artefact of starting the entry array 12 bytes late. The two
"extra header" words are entry 0's uid and offset, and the trailing word is the
last entry's key. For `base.mpp` that last entry is
`(uid 3, offset 414, key 0x0b400564)` and its payload is `ff ff ff ff` (-1),
not the project name. Full evidence and the resulting key table:
[`mpp-field-keys.md`](mpp-field-keys.md).

### 5.2 `mid` = row UniqueID (verified)

`probe_varmeta.py` compares every `mid` with the u32 at `row + 4`:

* `base.mpp`: uids 0,1,2,3; mids 0,1,2,3; every group matches a row.
* PROJECT_A: uids 0, 8141, 8142, 8166, 10720, ...; mids are exactly those values,
  plus a second family `0x80000000 | uid` (49 groups; e.g. `0x80001fcd` for uid
  8141). No mid is unexplained.
* Fixtures use uids 1..N, which is why the first pass mistook `mid` for an index.

`0x80000000 | uid` groups hold 1–3 entries each and appear only in the real
files (49 groups in PROJECT_A, 0 in PROJECT_B). **Inference:** they are a second value
family for the same row (baseline/actual). A baseline fixture (BaselineSave is
not exposed by this build) would confirm.

### 5.3 Var2Data block framing

This framing is independent of the VarMeta entry layout and is **unchanged** by
the 2026-10-22 correction: the offsets, length prefixes and payload decodings
are exactly as first measured; only which `(uid, key)` owns each offset changed.

```
block at offset X = u32 byte_length | payload[byte_length]
```

Blocks are addressed directly by VarMeta offsets; a block may be shared by
several entries (PROJECT_A has 9 offsets referenced by more than one entry) and some
bytes are unreferenced by any entry (PROJECT_A: 5,322 of 17,300 bytes). Payloads
observed:

| payload | encoding | example |
|---|---|---|
| task name | UTF-16LE, NUL-terminated; length = bytes incl. NUL (always even) | "CONSTRUCTION" = 12 chars + NUL = 26 B → `1a 00 00 00` |
| notes | 8-bit RTF (`{\rtf1\ansi...`), not UTF-16 | `var-notes` block `7b 5c 72 74 66 31 ...` |
| integer | u32/i32 LE | `ff ff ff ff` = -1; `03 00 00 00` = 3 |
| pair/quad | 8 B = two u32; 16 B = four u32 | `01 00 00 00 01 00 ff ff` |
| empty | `00 00 00 00` length 0 is not used; empty values are 4/8 zero bytes | `08 00 00 00` + 8 zeros |

**Not observed in Var2Data:** dates and durations. Every date and duration in
the fixtures is in FixedData (§2/§3/§4). A variable-stored date would need a
fixture that forces one (e.g. a custom date field) before this can be stated as
a rule.

## 6. Row 0 is the project row (brief item (c)) — confirmed

Evidence:

1. `base.mpp` row 0 has id 0 and UniqueID 0; its VarMeta group `mid=0` has 9
   entries (146 in PROJECT_A) and its fixed row carries the project-level defaults
   (Priority 500 at +78) and the save time at +128. Its key `0x0b40000e` block
   is the document title, not a task name.
2. `var-projstart` (project start 2026-01-05 → 2026-06-01) changes row 0's
   dates at +96..+119 (`78 1e f1 3b`→`d0 20 84 3c`), and each task row's dates
   only as derived values. `var-title` changes no fixed bytes at all.
3. The project row is the only row whose `mid` group contains project
   properties (title/name), and its key `0x0b40000e` block is the title.

So: **rows = tasks + 1, and row 0 is the project.** A reader should skip it
when enumerating tasks, but must read it for project start/finish/status.

## 7. Fixed2Data / Fixed2Meta (brief item (d)) — not baseline

Sizes, all files:

```
Fixed2Data = 64 * (rows + 3)
Fixed2Meta = 16 + 96 * (rows + 3)
```

base: 448 = 64*7, 688 = 16+96*7; PROJECT_A: 5,632 = 64*88, 8,464 = 16+96*88;
PROJECT_B: 5,696 = 64*89, 8,560 = 16+96*89.

Fixed2Meta mirrors FixedMeta: 16-byte header `ba ad df fa | 4 | count | Fixed2Data
size`, entries of 96 bytes whose +4 word is 0, 64, 128, ... (the Fixed2Data
offsets); entry 3 points at 192, i.e. the first row record.

A Fixed2Data row record (`probe_fixtures.py`/§2 output, base offset 192):

```
+0  16 B  id, looks like a v1 UUID: ba cb 09 a1 cd 98 11 f1 9f 64 1c c1 0c 05 4c 03
+16  8 B  double: 1.0 for the project row, 2.0 for task rows
+24 16 B  another id; in the fixtures this equals the previous row's +0 id
+40  6 B  03 00 40 00 00 00
+46 18 B  ff ff ... (project row), tail ends 07 00
```

Why it is not baseline-vs-current:

* its size is a function of row count only (`rows + 3`), never of a baseline;
* the three special records are zero in the fixtures and hold the Fixed2Data
  offsets in Fixed2Meta, exactly like the FixedData header slots;
* the double at +16 is 1.0 for the project row and 2.0 for every task row;
* changing one property (`var-name`, `var-duration`, ...) changes single bytes
  in the +0/+24 ids of affected records, not a parallel value table.

**Inference:** Fixed2Data is a per-row identity/outline table (row GUID,
level/type double, parent or previous-row id). Confirming the id semantics needs
a fixture that inserts/deletes/reparents rows.

## 8. Anomalies in the real PROJECT_A file (do not generalise from it)

`project_a.mpp` is internally inconsistent in ways the fixtures are not:

* `TBkndTask/VarMeta` has 1,081 entries (by stream length) but its header says
  1,076.
* `TBkndTask/FixedMeta` has 88 entries but its header says 87.
* Row 67 (id 70, uid 10856) has **no** FixedMeta entry and **no** VarMeta group;
  later rows shift down one slot and the last entry duplicates row 84's offset
  (17,016 appears twice). `probe_fixedmeta.py` reports the duplicate.
* 133 `mid` groups for 85 rows because of the `0x80000000|uid` family.

A reader must tolerate missing/duplicated metadata records rather than assuming
`entry[i]` is row `i`. PROJECT_B (86 rows) has none of these problems, so they are
damage/history in PROJECT_A, not format features. Whether they come from deleted rows
or from a specific edit operation is open.

## 9. Worked example: Task.Name of the first three PROJECT_A tasks (brief item (f))

Re-derived 2026-10-22 with the corrected entry framing (the payload offsets and
text are the same as the first pass; the entry index and key were shifted by
one entry).

`python scripts/fk_dump.py corpus/project_a.mpp --limit=3`:

```
row 1: FixedData[250:258] = id 13 uid 8141
  VarMeta entry 168: uid 8141 offset 1456 key 0x0b40000e
  Var2Data[1456:1500] = 28 00 00 00 53 00 69 00 74 00 65 00 20 00 ... 20 00 00 00
  prefix 28 00 00 00 = 40 bytes; name = 'Site establishment '

row 2: FixedData[452:460] = id 5 uid 8142
  VarMeta entry 189: uid 8142 offset 1660 key 0x0b40000e
  Var2Data[1660:1734] = 46 00 00 00 52 00 65 00 63 00 65 00 69 00 70 00 ...
  prefix 46 00 00 00 = 70 bytes; name = 'client-text-025'

row 3: FixedData[654:662] = id 12 uid 8166
  VarMeta entry 208: uid 8166 offset 1866 key 0x0b40000e
  Var2Data[1866:1896] = 1a 00 00 00 43 00 4f 00 4e 00 53 00 54 00 52 00 ...
  prefix 1a 00 00 00 = 26 bytes; name = 'CONSTRUCTION'
```

The chain, end to end: `row = (FixedData - 48) / 202`; `uid = u32(row, +4)`;
find the VarMeta entry with `(key 0x0b40000e, uid == row uid)`; read the block
at its `offset`; decode the payload as UTF-16LE and strip the trailing NUL.
Note the first name has a trailing space in the file — do not trim.

(First-pass numbers for comparison: it reported entries 167/188/207 and key
`0x0b400564` for the same three blocks. The blocks and offsets were right; the
entry index and key belonged to the neighbouring entry.)

## 10. Open / needs a fixture

| question | what would settle it |
|---|---|
| FixedMeta entry +2 meaning, and the 16-byte +8 blob | compare files saved by different Project builds, or a file with a deleted/restored row |
| `0x80000000 \| uid` VarMeta family | save a baseline (BaselineSave is not exposed by this build) — ask the coordinator for one |
| Fixed2Data id semantics | insert, delete and re-parent rows, then diff |
| the +74 / +186 words | a duration-variance or leveling fixture |
| date UTC-vs-local rule | fixtures either side of a DST boundary |
| full fixed schema (all ~200 bytes) | one fixture per field group; the recovered descriptor table gives candidate fields but not file offsets |

## 11. Reader algorithm (what the port needs)

1. Open the OLE2 file, find the storage whose name ends in `114`; take
   `TBkndTask`.
2. `rows = (len(FixedData) - 48) / 202`; row 0 = project, rows 1.. = tasks in
   table order; `uid = u32(row, 4)`, `id = u32(row, 0)`.
3. Parse FixedMeta only for validation (size identity, row offsets); it does
   not describe fields.
4. For each row, collect VarMeta entries with `mid == uid`, plus the
   `0x80000000 | uid` family if present; read each block as
   `u32 len + payload`.
5. Task.Name = block for key `0x0b40000e` (UTF-16LE). Keys are PjField ids, so
   `analysis/field_id_map.tsv` applies; see `spec/mpp-field-keys.md` for the
   per-key table and the remaining unnamed keys.
6. Dates = packed u32 as in §3; durations = 6-second ticks (§4); priority =
   u16 at +78; percent complete = u32 at +92.

## 10. Link and assignment rows (measured 2026-10-22)

Rebuild the fixtures with the coordinator's script (they are git-ignored, like
everything under `corpus/`):

```
python scripts/oracle_fixtures.py assign    # -> corpus/probe/assign1.mpp
python scripts/oracle_fixtures.py link      # -> corpus/probe/link1.mpp
```

Both files are written by MS Project itself over COM; what COM reported for each
is in `docs/analysis/oracle/assign-probe.json` and `link-probe.json`. Unlike the
Task table these two have **no 48-byte header**: `FixedData` is a bare array of
records, so the row count is `len / row_size` with no offset.

### 10.1 `114/TBkndCons` (links) — 20-byte rows

`link1.mpp` has `FixedData` 100 B = 5 x 20. Every field was cross-checked against
the links created over COM (`T2 FS<-T1`, `T3 SS+1d<-T1`, `T4 FF-1d<-T2`,
`T4 SF+2h<-T3`):

| off | type | field | evidence |
|---|---|---|---|
| 0 | u32 | link uid (dense, 1..N) | 1..5 in `link1.mpp` |
| 4 | u32 | **predecessor task UniqueID** | (1,2,3,1,2) |
| 8 | u32 | **successor task UniqueID** | (2,3,4,3,4) |
| 12 | u16 | **link type**: 0=FF, 1=FS, 2=SF, 3=SS | row 0 type 1 (FS), row 2 type 2 (SF), row 3 type 3 (SS) |
| 14 | i32 | **lag in tenths of a minute** | SS +1 d = 4800, FF -1 d = 0xFFFFED40 (-4800), SF +2 h = 1200 |
| 18 | u16 | 7 in every row; `LagFormat` is the obvious candidate, unproven | 5/5 rows |

Note the left-to-right order is **(predecessor, successor)** even though the
record is held on the successor's list: `T4 SF+2h<-T3` is stored as pred 3,
succ 4.

### 10.2 `114/TBkndAssn` (assignments) — 110-byte rows

`assign1.mpp` has `FixedData` 550 B = 5 x 110: two all-zero placeholder rows
(uids 1, 2) and three real assignments, `T1/R1`, `T1/R2`, `T2/R1` with
`Units = 100 %` and `Work` 1440/1440/960 minutes.

| off | type | field | evidence |
|---|---|---|---|
| 0 | u32 | assignment uid | 1..5 |
| 4 | u32 | **task UniqueID** | 1, 1, 2 |
| 8 | u32 | **resource UniqueID** | 1, 2, 1 |
| 12 | f64 | **Units x 10000** (double, unaligned) | 10000.0 in all three |
| 20 | f64 | **Work, minutes x 1000** | 1,440,000 = 3 d; 960,000 = 2 d |
| 28 | f64 | RemainingWork, same scale | equals Work when nothing is done |
| 52 / 56 / 60 | u32 | start / finish / start (packed dates) | 0x3d1112c0 / 0x3d1327d8 / 0x3d1112c0 for T1 |

**Correction to a claim in the field-key work:** an unassigned placeholder row in
PROJECT_A/base carries `0xFFFF0001` at +8, which earlier looked like "the resource id
is never in the fixed row". A real assignment stores the resource UniqueID at +8;
`0xFFFF0001` is the null-resource sentinel of a placeholder. Likewise
`docs/coordination.md`'s statement that `probe-v1.mpp` has two FS links and an
assignment is wrong: that file has `TBkndCons` with **zero** rows and its three
assignment rows are placeholders. `probe-v1.mpp` is not a link fixture.

### 10.3 The COM calls that create these records

Measured while building the fixtures (they are what any test fixture generator
needs):

* assignment: `Task.Assignments.Add(ResourceID=<resource ID>)`. Passing the
  resource object, or no argument, raises "The argument value is not valid."
* link: `Task.TaskDependencies.Add(predecessor, type, lag)` or
  `Task.LinkPredecessors(predecessor, type, lag)`; `Project.TaskDependencies`
  does not exist. `lag` accepts `"1d"`, `"-1d"`, `"2h"` or an integer number of
  minutes.
* `Task.GetField(id)` returns Project's **formatted text** ("1 day",
  "Mon 19/10/26"), not a raw value; raw encodings must come from a saved file.
