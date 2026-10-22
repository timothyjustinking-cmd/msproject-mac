# `.mpp` container format — observed structure (MPP14)

First pass over the real corpus, 2026-10-22. Everything below is either **verified**
against the bytes or marked **hypothesis**. Tool: `scripts/mpp_dump.py`.

## Evidence base

| File | Size | Last saved | Build |
|---|---|---|---|
| `corpus/project_a.mpp` | 1,567,744 B | 2026-07-22 | 16.0.19725 |
| `corpus/project_b.mpp` | 1,601,536 B | 2026-03-10 | 16.0.19725 |

Both descend from the same 2016-08-09 template lineage, both carry `Props14`.

## Version

- OLE2 / Compound File Binary **v3**: 512-byte sectors, 64-byte mini sectors, little-endian.
- `Props14` stream present → the **MPP14** family (Project 2010 and later). There is no
  `Props9`/`Props12` stream, so this corpus gives no evidence for older formats.
- `Props14` carries the writing version as UTF-16: `16,0,19725,20152` → Project 16.0,
  build 19725 (2024-era). A reader must therefore cope with MPP14 written by 2010–2024,
  where sub-version differences are not yet characterised.

## Top-level layout

Root streams: `\x05SummaryInformation`, `\x05DocumentSummaryInformation`, `\x01CompObj`,
`Props14`, `114/Props`, `214/Props`.

Two sibling storages appear in every file, with identical names:

| Storage | Contents |
|---|---|
| `114/` | **engine tables** — `TBknd*` record tables |
| `214/` | **UI documents** — `CDrawing`, `CEdl`, `CFilter`, `CGrouping`, `CMap`, `CReport`, `CTable`, `CV_iew` (sic), `CVba` |

The names `114` and `214` are identical across both unrelated files, so they are
structural constants rather than counters. Their meaning is **hypothesis**: numeric
ids of the two document namespaces. They may be written as decimal strings of
`0x72`/`0xD6` — unconfirmed.

## Table inventory (observed)

`114/`: `TBkndTask`, `TBkndRsc`, `TBkndAssn`, `TBkndCal`, `TBkndCons`,
`TBkndOutlCode`, `TBkndAttachment`, `TBkndChecklistItem`, `TBkndConversation`,
`TBkndLabel`, `TBkndLabelAssociation`; plus `TBkndSprint` and `TBkndBoardColumn` in
the project_b file only (agile/board feature → per-project feature flags).

These map onto the recovered record types: Task, Resource, Assignment, Calendar,
Cons (= Links/dependencies), OutlineCode, plus the agile tables. `Task34` and
`Project` have no table of their own here.

## Per-table stream quartet

Every `TBknd*` table carries the same small set of streams:

```
FixedData      fixed-size rows          FixedMeta     layout map for FixedData
Var2Data       variable-length values   VarMeta       index into Var2Data
Props          table-level properties
Fixed2Data/Fixed2Meta   second fixed table (present for Task, Rsc, Assn, Cal, Cons, OutlCode)
```

So the file mirrors the in-memory architecture: one fixed row per record, one
variable store for the rest of the fields, and a metadata map for each.

## Verified encodings

**`*Meta` magic.** `ba ad df fa` (0xBAADFADF) at offset 0 of `VarMeta` and `FixedMeta`.

**VarMeta header** — 32 bytes, 8 little-endian `u32`s:

| Offset | Value (PROJECT_A / PROJECT_B) | Meaning |
|---|---|---|
| 0x00 | 0xBAADFADF | magic |
| 0x04 | 0 | unknown |
| 0x08 | 1076 / 1437 | entry count candidate (**not** equal to parsed entries) |
| 0x0C, 0x10 | 0 | unknown |
| 0x14 | 17300 / 36204 | **size of Var2Data — verified equal to the actual stream length** |
| 0x18, 0x1C | 0 | unknown |

**VarMeta entries** — 12 bytes each, starting at 0x20, followed by a 4-byte trailer:

```
u32 field_id     u32 mid     u32 offset
```

`field_id` is a **PjField id**: all 1080 (PROJECT_A) / 1436 (PROJECT_B) entries have the
`0x0b40xxxx` prefix, i.e. every one is a Task field. Resolved against
`analysis/field_id_map.tsv`:

| field_id | name | mid | offset |
|---|---|---|---|
| 0x0b400001 | BaselineWork | 0 | 12 |
| 0x0b400006 | BaselineCost | 0 | 24 |
| 0x0b40000e | Name | 0 | 90 |
| 0x0b40001b | BaselineDuration | 0 | 98 |
| 0x0b40002b | BaselineStart | 0 | 106 |
| 0x0b40002c | BaselineFinish | 0 | 114 |

**This is the load-bearing finding:** the file keys its variable data by the same
field ids the COM surface and the recovered descriptor table use. The 3,207-field
model already in `analysis/` therefore decodes a large part of the file without any
new reversing.

**Var2Data** begins with 12-byte groups (`08 00 00 00` + 8 zero bytes) and then
length-prefixed blocks with UTF-16LE payloads — e.g. `3e 00 00 00` followed by
`ProjectA…` at offset 0x20. The first two VarMeta offsets (12, 24) land exactly on
those 12-byte boundaries, so offsets address blocks in this stream directly.

**FixedMeta contains no field ids.** A scan for `0x0b40xxxx` words over the whole
4152-byte stream found zero hits, so it is a positional layout map (offset/size per
slot), consistent with the in-memory `PropInfoData` being slot-indexed rather than
id-keyed.

## Not yet determined

- `FixedMeta` layout: entry stride, and how (offset, size, type) are encoded. 4152 B
  over an unknown entry count.
- `FixedData` row stride. 17,218 B = 16 × 1076 + 2, and the first three 16-byte groups
  begin `00 00 ff ff`, `01 00 ff ff`, `02 00 ff ff` — suggestive of a 2-byte prefix with
  16-byte rows, but not proven, and 16 bytes is far too small for a full task row, so
  the fixed section is probably a subset of fields with the rest in `Var2Data`.
- Meaning of `mid` in VarMeta entries (0 for the first block of entries, then values
  such as 8141, 10805, 10733 — plausible record offsets).
- `Fixed2Data` vs `FixedData`: candidate baseline-vs-current split.
- `114/Props` (90 KB, begins with table names as UTF-16) — looks like the table of
  contents; encoding unread.

## Next experiment

Do not keep guessing from hexdumps. Create a project through COM with a known, tiny
content (3 tasks, 2 resources, 1 link, fixed dates), save, and dump; then change one
field and diff the two saves. That resolves row stride, `FixedMeta` layout and `mid`
in a single pass, and gives a fixture with known-good expectations for the reader
tests.

## Verified against the oracle (2026-10-22)

Built minimal projects through COM (`scripts/oracle_probe.py`) and swept the task
count to measure the row stride instead of guessing it:

| tasks | `TBkndTask/FixedData` |
|---|---|
| 1 | 452 |
| 2 | 654 |
| 3 | 856 |
| 4 | 1,058 |
| 5 | 1,260 |
| 6 | 1,462 |

Delta is **+202 bytes per task at every step**, so:

```
FixedData = 48-byte header + 202 x rows,  rows = tasks + 1
```

Row 0 is the project row (it moved when a task's duration moved the project
finish date). Checked against the two real files — both divide exactly:

- PROJECT_A: (17,218 - 48) / 202 = **85 rows**
- PROJECT_B: (17,420 - 48) / 202 = **86 rows**

### Corrections to the first pass

- **Storage names carry leading spaces**: they are right-aligned in a 6-character
  field, e.g. `'   114'`, `'   214'`. Exact path matching fails; match on suffix or
  strip the name. This is also why the naive `114/...` lookups returned
  "file not found" earlier.
- `FixedMeta` is **345 B** in the minimal fixture but **4,152 B** in PROJECT_A, so it
  describes the *set of fields stored in the fixed area*, not the rows.
- Every table also has a second fixed table (`Fixed2Data`), so fixed storage is
  split in two.
- `214/CV_iew/Var2Data` is rewritten on save even when only a duration changes, so
  view state is **not inert** — carrying it through opaquely on a save path needs
  checking rather than assuming. Confirmed directly on 2026-10-22 by opening
  `base.mpp` and doing Save As with no edit: 25 streams change, 12 of them under
  `214/` (`CV_iew/Var2Data` 103 bytes at 42,613+, `CEdl/Var2Data` 474->702,
  `CFilter/FixedData` 230->460, plus `CGrouping`, `CTable`, `CV_iew/FixedData`
  and `214/Props`). The file grows 237,568 -> 239,104 B with the same 96 streams.
  Reproduce: `python scripts/oracle_writer.py base` then `... diff`
  (`docs/analysis/oracle/writer-diff.json`).

### COM recipe (expensive to rediscover)

- **Never pass `/automation`** (or any switch) to `WINPROJ.EXE`. Project treats an
  unknown switch as a file name, fails to open it, and shows a modal dialog; every
  COM call then fails with 0x80080005 / `RPC_E_CALL_REJECTED`, which reads as a
  permissions problem but is not.
- Launch with **no arguments**, then poll `Application.Projects` until it binds
  (20-30 s cold).
- `Application.Version` does not exist on Project's Application object.

### Still open

`FixedMeta` layout (now tractable: 345 B for a handful of fields), the meaning of
`mid` in VarMeta, and the role of `Fixed2Data` versus `FixedData`.
