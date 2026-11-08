# Set Baseline 0 for the entire project: what Project writes, and why the port does not

Status: W35-3.2 decode and measurement; **deferred with evidence**.  The finding
is new in wave 35: the file holds process addresses.

## 1. The pairs

Two Project 2021 sessions, two shapes, the same call
(`Application.BaselineSave(All=True, Copy=pjCopyCurrent, Into=pjIntoBaseline)`,
the Set Baseline dialog's defaults):

| pair | session | shape | new var blocks | anatomy |
|---|---|---|---|---|
| `wave30-baseline` | wave 30 | three tasks, no links, one placeholder assignment per row | 94 | `evidence/wave30-baseline.md` |
| `wave35-g3-baseline-set-nested` | wave 35 (`scripts/oracle_w35_g3.py`, log `build/w35/g3-run2.log`) | six automatic tasks on a three-level outline, no links, one placeholder assignment per row | 103 (60 Task, 36 Assignment, 6 Resource, 1 Calendar) | `evidence/wave35-g3-baseline-set-nested.md` |

Both change the same 20-odd streams (all of Task, Assignment, Resource and
Calendar plus `114/Props`, whose tail grows 110 bytes and whose +12 word steps
262 to 263).

## 2. What is decoded

Per task (and the project row) the new Task var blocks are:

| key | field | payload | rule |
|---|---|---|---|
| `0x0b400001` / `0x0b400006` | BaselineWork / BaselineCost | 8 bytes | zero on both pairs (zero work and cost) |
| `0x0b40001b` | BaselineDuration | 4 bytes | the row's duration word (row +84), checked on all four rows of the first pair and all seven of the second |
| `0x0b40002b` / `0x0b40002c` | BaselineStart / BaselineFinish | 4 bytes | the row's Start and Finish words (row +96 and +100), checked on the same eleven rows |
| `0x0b400496` / `0x0b400497` | BaselineDeliverableStart / Finish | `ff ff ff ff` | constant |
| `0x0b400498` / `0x0b400499` | BaselineBudgetWork / Cost | `8d ed b5 a0 f7 c6 b0 be` | constant |
| `0x0b400513` / `0x0b400514` | BaselineStartText / FinishText | `00 00` | constant, task rows only |
| `0x0b4000b3` | undecoded | 2 bytes | `0x35` on every row of the second pair; `0x15` on the project row and `0x07` on the three tasks of the first |
| `0x0b4000ad` | undecoded | 148 bytes on the project row; 108, 108, 108 (first pair) and 128, 108, 88, 88, 88, 88 (second) on the tasks | the dword at offset 20 equals the row's duration word on the second pair and differs from it on the first (`0x5460`, `0x2ee0`, `0x1c20` against 24000, 14400, 9600); the length follows something not yet identified |
| `0x0b4000ae` | undecoded | 76 bytes | **holds process addresses; see section 3** |

## 3. Why it cannot be enabled

The 76-byte block `0x0b4000ae` is written once per task row.  Its bytes, from
the two sessions:

```
wave30 : 0300 0400 1000 0000 0000 0000 0000 0000 | 0100 3e71 f67f 0000 | double | b8d22b0d 0000 3e71 f67f 0000 ...
wave35 : 0300 0400 1000 0000 0000 0000 0000 0000 | 0100 4410 f77f 0000 | double | 40ff2b0d 0000 4410 f77f 0000 ...
```

* The 8-byte little-endian value at offset 16 is `0x00007ff6713e0001` in the
  first session and `0x00007ff710440001` in the second; the 8-byte value at
  offset 36 is the same address without the low bit (`0x00007ff6713e0000`,
  `0x00007ff710440000`).  Both are 64 KiB-aligned user-mode addresses, the shape
  of an image's load address under randomised layout.  They differ by session
  and the file's contents do not determine them.
* The dword at offset 32 (`0x0d2bd2b8` in the first session, `0x0d2bff40` in the
  second) is constant across the rows of one file and differs between the
  sessions.  The dword at offset 52 takes several values inside one file
  (`0x0d2ce9a0`, `0x0d2c7920`, `0x0d2c40e0` in the first; `0x0d2e7f18`,
  `0x0d2c4c98`, `0x0d2cbd18`, `0x0d2c84d8` in the second, with `0x0d2e7f18`
  repeated on the project row and three tasks): values of the same form as the
  one at offset 32, which is what 32-bit in-process addresses or handles would
  look like.  This note calls them that on the strength of the pattern only; no
  trace of the saving process was taken.
* The word at offset 58 is `0x0060` in the first session and `0x0020` in the
  second.

None of these is a function of the project, so a port cannot predict them, and a
port save that wrote other values would not match Project's (Standing Rule 1).
A writer that copied one session's values would be a guess presented as a
decode.  `0x0b4000ad` is not closed either (its dword at offset 20 and its
length follow different things on the two pairs) and neither are the Assignment
blobs of section 4.

## 4. The rest

The undecoded structured blocks also appear for Assignment (`0x0f400034`,
`0x0f400035`, `0x0f40010b`), Resource (`0x0c400044`, `0x0c400045`) and the
calendar (`0x0d400001`), and the new calendar row (TBkndCal FixedData grows by
12 bytes, 100 to 112 in the first pair and 88 to 100 in the second; Fixed2Data
+48), the presence bits in four tables' FixedMeta and
Fixed2Meta, and the `114/Props` tail are rewrites the port has no writer for
(`spec/writers/props-records.md`).  The retained var writer handles the Task and
Resource tables only, one record per call, and refuses the project row and
every Assignment or Calendar table.

State left: Project > Schedule > Set Baseline stays disabled with its tooltip;
no source file changed.
