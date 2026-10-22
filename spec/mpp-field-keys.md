# `.mpp` field keys — the Task VarMeta key table (W5)

Status: wave 2, 2026-10-22. Companion to [`mpp-records.md`](mpp-records.md)
(byte layout of the Task table) and [`mpp-container.md`](mpp-container.md)
(OLE2 wrapper). Deliverables: `data/field_keys.tsv`, `scripts/fk_*.py`.

Everything below is either **verified** (re-runnable command, byte offsets
given) or marked **inference** with the test that would confirm it.

## 0. Headline: the premise was wrong — the file keys *are* PjField ids

The W1 brief said `Task.Name` is stored under key `0x0b400564` while the COM
`PjFieldTaskName` `0x0b40000e` holds the RTF notes, and concluded that
`analysis/field_id_map.tsv` cannot decode the file.

That conclusion came from an **off-by-one in the VarMeta framing**. The VarMeta
stream has a **24-byte header and 12-byte entries ordered `uid, offset, key`**;
wave 1 parsed a 32-byte header and started the entry array 12 bytes late, which
paired every key with the *next* entry's `uid`/`offset`. Re-parsing correctly:

* the key that actually carries the task name is **`0x0b40000e`** — exactly
  `PjFieldTaskName` in `analysis/field_id_map.tsv`;
* the key that carries the RTF notes is **`0x0b40000f`** — exactly
  `PjFieldTaskNotes`;
* `0x0b400564` is an unrelated field that happens to hold `-1` in every row.

So the repo's PjField tables **do** decode the file. This document is the
evidence, the coverage that follows from it, and the list of keys that remain
unnamed.

### 0.1 Raw layout (verified)

`python scripts/fk_vmraw.py corpus/probe/base.mpp`:

```
VarMeta len 528
   0: ba ad df fa  magic 0xBAADFADF
   4: 00 00 00 00
   8: 2a 00 00 00  entry count 42
  12: 00 00 00 00
  16: 00 00 00 00
  20: d0 01 00 00  Var2Data size 464
  24: 00 00 00 00  } entry 0: uid 0
  28: 4c 00 00 00  }          offset 76  (the project name block)
  32: 0e 00 40 0b  }          key 0x0b40000e = Name
  36: 00 00 00 00  entry 1: uid 0 ...
```

```
VarMeta = 24-byte header + n * 12-byte entry
header  = magic | 0 | n | 0 | 0 | Var2Data byte size
entry   = u32 uid | u32 Var2Data offset | u32 field key
```

`n` from the stream length matches the header in every file except PROJECT_A:

| file | stream bytes | 24+12n | header n | n |
|---|---|---|---|---|
| base | 528 | 528 | 42 | 42 |
| var-notes | 540 | 540 | 43 | 43 |
| PROJECT_A | 12,996 | 12,996 | 1,076 | **1,081** |
| PROJECT_B | 17,268 | 17,268 | 1,437 | 1,437 |

PROJECT_A is internally inconsistent (as `mpp-records.md` §8 already noted for
FixedMeta): trust the stream length. The "1,080 entries" in the W1 brief is the
old parser's count, one short of the 1,081 entries actually present.

### 0.2 What the correction does and does not change

**Unchanged:** every Var2Data byte and its decoding. A block is still
`u32 byte_length | payload`, addressed directly by the entry's offset; task
names are still UTF-16LE + NUL, notes are still 8-bit RTF, integers are still
LE, and the packed-date/duration rules in `mpp-records.md` §3–§4 are untouched.
The PROJECT_A worked example's offsets and hex (`mpp-records.md` §9) are byte-for-byte
the same after the fix; only the entry index and key attributed to each block
were wrong.

**Changed:** the entry count (`n` is 42/1,081/1,437, not 41/1,080/1,436 + a
trailer), which uid/offset belongs to which key, the claim that the project
title block is unreferenced (it is entry 0), and every field-name attribution
that depended on the key. The VarMeta keys are PjField ids, so the existing
`analysis/field_id_map.tsv` name table is usable.

### 0.3 Why the old parse produced 0x564

With `W[i]` the i-th u32, true entry *i* is `(W[6+3i], W[7+3i], W[8+3i])`.
Wave 1's parser read `(W[8+3i], W[9+3i], W[10+3i])`, i.e. **the right key with
the next entry's uid and offset**. Its "split entry" — the last key paired with
the header's last two words — was an artefact of the same shift: the trailing
key belongs to the last entry, and the two "extra header words" are entry 0's
uid/offset. There is no split entry, no `+4` trailer and no `count+1`; the
stream is a flat list of `n` triples.

Independent confirmation of the correct layout: mpxj's `VarMeta12` reader
(`https://github.com/joniles/mpxj`, `src/main/java/org/mpxj/mpp/VarMeta12.java`)
reads a 24-byte header and 12-byte entries as `uid (u32), offset (u32), type
(u16) + 16 unknown bits` — the low half of our key word. mpxj's
`MPPTaskField.FIELD_ARRAY` puts `NAME` at index 14 and `NOTES` at 15, which is
what the fixtures independently show.

## 1. Method

```
python scripts/fk_vmraw.py corpus/probe/base.mpp      # raw framing
python scripts/fk_diff.py                             # one-change fixtures
python scripts/fk_validate.py                          # framing on all files
python scripts/fk_stats.py                             # keys per file
python scripts/fk_compare.py                           # repo names vs mpxj
python scripts/fk_build.py                             # -> data/field_keys.tsv
python scripts/fk_coverage.py                          # coverage numbers
python scripts/fk_sample.py 0x0b4004e3                 # values of one key
```

`scripts/fk_lib.py` holds the corrected parser; it reuses `probe_lib.py` for the
OLE stream loader, FixedData rows, Var2Data blocks and text decoding.

Name sources, in order:

1. **Fixtures.** `fk_diff.py` compares every `(uid, key)` payload against
   `base.mpp`. A key whose payload changes for exactly the row the fixture
   changed is proven.
2. **The repo's own tables.** `analysis/field_catalog_task.tsv` and
   `analysis/field_id_map.tsv`, 1,444 Task ids from the typelib. Their id space
   is the file's id space — proven by 14/15 below and checked against mpxj.
3. **mpxj `MPPTaskField.FIELD_ARRAY`** (external, cited above): cross-check and
   filler for 11 baseline-duration-unit ids the repo tables leave blank.

## 2. What is proven (fixtures)

`python scripts/fk_diff.py`. Every fixture also renames the project row to the
fixture's file name, so key 14 changes in all of them; that is only listed where
it is the actual proof.

| fixture | delta on the changed row | proves |
|---|---|---|
| `var-name` | uid 1, key `0x0b40000e`: `text 'Task A'` -> `text 'Renamed A'` | key 14 = **Name** |
| `var-title` | uid 0, key `0x0b40000e`: `'base'` -> `'Probe Title'` | row 0 is the project; its title uses the **same key as Name** |
| `var-notes` | uid 2, key `0x0b40000f`: block **added**, 180 B `{\rtf1\ansi\ansicpg1252...` | key 15 = **Notes** (RTF) |
| `var-pct` | uid 1: `0x0b40015e` added (76 B blob), `0x0b4004e5` removed, `0x0b400505`/`06` (Start/FinishText) removed | unknown keys 350/1253 are touched by %complete; actual dates replace the text caches |
| `var-duration`, `var-priority`, `var-projstart`, `var-rsc2`, `var-baseline` | only the project-row Name changes | these properties are fixed-row only (or, for `var-rsc2`/`var-baseline`, other tables) |

That is the honest extent of *direct* proof: **two keys**. Everything else is
schema cross-reference, graded below.

## 3. What is inferred (schema cross-reference)

`data/field_keys.tsv` has 185 rows, one per Task key observed in any corpus
file. `confidence` means:

| confidence | rule | rows |
|---|---|---|
| `proven` | fixture delta above | 2 |
| `high` | named by the repo PjField tables **and** mpxj agrees or is silent, **and** the observed payload shape fits the field family (text, units, date, work/cost) | 138 |
| `medium` | named only by mpxj (the 11 `BaselineN_DurationUnits` ids), or named but no block is ever present so the shape cannot be checked (`EnterpriseProjectText40`) | 12 |
| `none` | neither table names the id | 33 |

Cross-check (`python scripts/fk_compare.py`): of the 185 keys, 140 get the same
name from the repo tables and mpxj (after case/underscore normalisation), 1 is a
spelling alias (`CalendarGuid` vs `TASK_CALENDAR_GUID`), 11 are mpxj-only, 33
are unnamed by both, 0 are repo-only. The fixture-proven keys 14 and 15 land on
`NAME`/`NOTES` in both sources.

## 4. Coverage of PROJECT_A (the number the brief asks for)

`python scripts/fk_coverage.py`:

| file | entries | absent blocks | distinct keys | named keys | **named entries** | addressed bytes | **named bytes** |
|---|---|---|---|---|---|---|---|
| base | 42 | 0 | 12 | 8 | 30 (71.4 %) | 296 | 224 (75.7 %) |
| **PROJECT_A** | **1,081** | 101 | 181 | 149 (82.3 %) | **665 (61.5 %)** | 11,978 | **8,590 (71.7 %)** |
| PROJECT_B | 1,437 | 0 | 156 | 147 (94.2 %) | 1,004 (69.9 %) | 28,596 | 26,204 (91.6 %) |

For PROJECT_A that is:

* **61.5 % of entries** (665 / 1,081) and **82.3 % of distinct keys** named;
* **71.7 % of the addressed Var2Data bytes** (8,590 / 11,978); measured against
  the whole 17,300-byte stream it is 49.7 %, because 5,322 bytes are not
  reachable from any entry offset (unreferenced/deduplicated blocks);
* the 101 "absent" entries (offset `0xFFFFFFFF`) hold no value bytes and are
  concentrated in the `0x80000000 | uid` second family.

An entry counts as named when its key has a non-empty `candidate_name` in
`data/field_keys.tsv`.

## 5. The unnamed keys — do not invent names

416 PROJECT_A entries (32 keys) stay unnamed. The big ones, with the observed value
structure from `scripts/fk_sample.py`:

| key | PROJECT_A n | observed | note |
|---|---|---|---|
| `0x0b400564` | 86 | `i32 -1` in every row | sentinel; unassigned in both tables |
| `0x0b400563` | 85 | `i32 -1` in every row | sentinel; unassigned in both tables |
| `0x0b4004e3` | 61 | `u32 count` + `count × u32 uid` (8/12/16 B), or absent | PROJECT_B: `01 00 00 00` + one task uid |
| `0x0b4004e4` | 43 | `u32 count` + uid list, or absent | PROJECT_A row 0: `01 00 00 00` + uid 8166 |
| `0x0b4004e5` | 41 | 8 B: `01 00 00 00 01 00 ff ff` | constant where present; removed by `var-pct` |
| `0x0b4005a1` | 33 | always absent | only in the `0x80000000 \| uid` family |
| `0x0b40015e` | 21 | 76-byte blob, prefix `03 00 04 00 10 00` | added by `var-pct` (first seen with actuals) |
| `0x0b40007c` | 16 | 12-byte record, e.g. `00 96 00 00 15 00 01 00 00 00 00 00` | |
| `0x0b400512` | 3 (73 in PROJECT_B) | `i32 0` | |
| `0x0b4000f4` | 0 (73 in PROJECT_B) | `i32 0` | |

**Inference (unconfirmed):** 0x4e3/0x4e4 look like task-uid lists (a count
followed by uids), which is what a "start driver"/dependency cache would look
like, and 0x4e5/0x563/0x564 sit in the same cluster. This is a shape argument
only. A fixture that sets a predecessor, clears the Task Path view, or saves a
baseline with float would identify them; see §7.

## 6. Traps for the reader

1. **Header count is not authoritative.** Use `(len(VarMeta) - 24) / 12`; PROJECT_A's
   header says 1,076 while 1,081 entries are present.
2. **`mid` is the row's UniqueID**, and the second family is
   `0x80000000 | UniqueID`. `fk_validate.py` confirms every `mid` resolves to a
   row uid in all 14 files.
3. **`offset == 0xFFFFFFFF` means "no block"**, not an error. 101 PROJECT_A entries
   are like this. `probe_lib.read_block` raises on it; check first.
4. **Row 0 is the project row**, and its title is stored under the *same key as
   task Name* (`0x0b40000e`). Do not treat key 14 on row 0 as a task name.
5. **A key can be absent on one row and present on another** (VarMeta is sparse
   per row). Never index VarMeta positionally.
6. **Do not sum block lengths to get the stream size**: entries can share blocks
   and some blocks are unreferenced (PROJECT_A: 11,978 addressed vs 17,300 bytes).
7. **`mpp-records.md` §0 item 4, §5.1 and §9 predate this correction.** The
   uid/offset columns in its VarMeta examples are shifted by one entry, its
   "split entry" does not exist, and its field-key attribution (`0x0b400564`
   for Name) is wrong. Its `mid` = UniqueID finding (§5.2) and the FixedData
   findings are unaffected.

## 7. Open / next actions

1. Name 0x4e3/0x4e4/0x4e5/0x563/0x564 with a fixture that adds a predecessor
   and toggles the Task Path/float columns, then re-run `fk_diff.py`.
2. Decode `TBkndTask/Props` (`PropsKey.CUSTOM_FIELDS`) to map the local custom
   field definitions to their user names; this is the most likely source for
   0x5a1 and the singleton project-row ids.
3. Regenerate the readme/report tables once another stream re-derives the
   FixedMeta picture; nothing here depends on FixedMeta.
