# memory/viewstate.md -- `214/` view-state streams (W56, 2026-10-22)

Read this, then `docs/handoff/w56.md`, then `docs/analysis/oracle/w10-oracle.md`
section 4 **including the W50 correction**. The decoder is read-only; there is
deliberately no writer and no write-layout claim.

## What is on disk

| path | what |
|---|---|
| `core/viewstate/viewstate.h`, `viewstate.cpp` | collection framing, CTable table/column decode, CV_iew view/block decode, CEdl Props reuse |
| `core/viewstate/report_main.cpp` | `viewstate_report` CLI for an extracted `214/` tree |
| `core/viewstate/CMakeLists.txt` | `core_viewstate`, `viewstate_tests`, `viewstate_report` |
| `tests/viewstate_tests.cpp` | 113 checks, no corpus dependency |
| `tests/data/synthetic-viewstate-ctable-{base,col,date}.bin` | raw `214/CTable/Var2Data` from the synthetic prefs fixtures (1051 B each) |
| `docs/handoff/w56.md` | the full evidence write-up |

The three blobs contain only the stock Entry table's column field ids and the
generic `Task Name` title. SHA256:
`80573060bbacc24b656f9a6ff70bf907a421bee0677c2f99a8f7d3ab5993f4b9` (base),
`7b753dfe1c930cf95c6c2e3d75a821505a2b6f43c35fb42c30979d824e5ff52e` (col),
`e106e86452c37f4ea73263f043ae5ef1280beef46999c62271933960705eb3cb` (date).

## Verification (exact, 2026-10-22)

```
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake -S . -B build\vs-w56 -G "Visual Studio 17 2022" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON'
cmd /c '"...\vcvars64.bat" >nul && cmake --build build\vs-w56 --config Release --target viewstate_tests viewstate_report'
.\build\vs-w56\core\viewstate\Release\viewstate_tests.exe          -> PASS: 113 checks, 0 failures
ctest --test-dir build\vs-w56 -C Release --output-on-failure -R "viewstate"  -> 1/1 (test #42)
```

`viewstate_report` needs an extracted tree, not an `.mpp`. Extract with
olefile/python into `<root>/214/<Class>/<Part>`; the CLI then prints the
inventory (33 streams) and the decoded blocks. Example command used for the
evidence (output in `docs/handoff/w56.md`; the dump lives under `build/`):

```
.\build\vs-w56\core\viewstate\Release\viewstate_report.exe --dump <root>
```

## Stream inventory (measured)

Every prefs fixture has 96 CFB streams, 33 of them under `214/`:
`214/Props` plus 8 classes x 4 parts: `CTable`, `CV_iew`, `CEdl`, `CFilter`,
`CGrouping`, `CDrawing`, `CMap`, `CReport`. `CDrawing`/`CMap`/`CReport` are
empty collections in these files (FixedData 0, FixedMeta 16 with count 0,
VarMeta 24 with count 0, Var2Data 0).

The CFB directory names carry **three leading spaces**: `"   214/CTable/Var2Data"`,
`"   114/Props"`. `core/mpp/cfb.cpp` trims spaces before comparing, which is why
reader paths without spaces work. Any extractor must preserve or trim them
consistently.

## Framing (measured on all 33 streams x 3 fixtures)

Four parts, same shape as the `114/` table streams:

- FixedData: rows of a class-specific size. A row starts `u32 ordinal`
  (1-based, equals the VarMeta uid) and a NUL-terminated UTF-16 caption at +4.
  Every row ends in a 16-byte identity/GUID-like value that changes on every
  Project save. The 230-byte classes (CTable/CFilter/CGrouping) also carry a
  second UTF-16 caption at +136 (display name without `&`).
- FixedMeta: 16-byte header `magic 0xfadfadba, flags(4), declared_count,
  fixed_data_size`, then entries of `entry_size = (size-16)/declared_count`:
  `+0 u16 state(0), +2 u16 var_count, +4 u32 fixed row offset`, rest opaque.
  `var_count` equals the number of VarMeta entries for that uid (verified on
  CTable 1/1, CV_iew 2/2/3 vs 7, CGrouping 1/1, CEdl 1/1).
- VarMeta: 24-byte header `magic, 0, declared_count, 0, 0, var2_size`, then
  `{uid, offset, key}`. `var2_size` equals the Var2Data stream size. Offsets are
  block-start positions and are **not** in block order (CV_iew groups by uid).
- Var2Data: back-to-back blocks `u32 payload_length` + payload; every block is
  named by exactly one VarMeta entry in the measured files.

Derived row sizes: CTable 230, CFilter 230, CGrouping 230 (2 rows), CV_iew 138
(3 rows), CEdl 160 (1 row). Row size is derived from the first two FixedMeta
offsets (single-entry streams: the whole FixedData), then every entry offset is
validated.

## CTable -- the table definition (proved)

One fixed row per table; the VarMeta block with key `0x20400006` is the column
list. Payload header (12 bytes):

| off | field | measured |
|---|---|---|
| +0 | `u32` table ordinal | equals FixedData +0 and the uid |
| +4 | `u16` last column index | `column_count - 1` (8 for the 9-column Entry) |
| +6 | `u8` format word | 1 in every measured table |
| +7 | `u8` date-format override | `0xff` default, **`0x05`** after `DateFormat=5` |
| +8 | `u32` table flags | 6 stock, 2 after the wave-10 TableEdit save; semantics open |
| +12 | columns | 115-byte records |

Column record: `+0 u32 field id` (0x0b40xxxx task / 0xc40xxxx resource),
`+4` 9 opaque bytes, `+13` UTF-16 NUL-terminated title (51 code units). The
stock Entry table has 9 columns and the fields
`0x0b400017, 0x0b4000cd, 0x0b400500, 0x0b40000e, 0x0b400507, 0x0b400505,
0x0b400506, 0x0b40002f, 0x0b400031`; only the Name column (0x0b40000e) has a
stored title (`Task Name`).

A probe file with a multi-table CTable collection (8 tables, 4/7/9/10/14
columns) confirms `+4 = n-1` for all eight; run the scan in the handoff, which
prints the file's own name (it is corpus-derived, so it is not written here).

## CV_iew -- view definitions (partly proved)

3 rows of 138 bytes: ordinal at +0, UTF-16 name at +4, identity at +122. Seven
Var2Data blocks, grouped by uid:

- `0x20400006` main view state (42572 / 42692 / 8272 B in the prefs fixture)
- `0x22400014` notes/RTF payload (381 / 409 / 230 B; starts with 16-24 zero
  bytes then `{\rtf1...`)
- `0x2240002f` timeline view data (5594 B, UTF-16 `<TLViewData ...>`), view 3
  only.

The main block starts with a Props-like 16-byte prefix (`payload-4` twice,
`0x00010000`, declared item count 7/10/7) but its item encoding is **not** the
Props record framing (record walk parses 1 record and then stops). The payloads
are exposed as opaque blocks with a preview. Nothing in CV_iew proved a column
list: no `Number1` field id or title exists in any 214 stream.

## CEdl -- last view/table names plus a file path (proved as churn)

One 160-byte row; one block whose payload is a complete Props document, parsed
with `core/props`: 14 records in base, 15 in col/date. Records include the last
view name (`&Gantt Chart`), the table name (`Entry`) and a UTF-16 file path
(198 B in col, 200 B in date). The path growth is the whole `CEdl Var2Data`
+210/+2 delta W46-era notes attributed to table state. **Do not commit an
extracted CEdl blob: it contains a host path.**

`214/Props` is an ordinary Props stream (13,102 B, 47 records, 3,200 B tail) and
is reported via `core/props`; it churns on save (identity + the CEdl size word at
11402) but carries no table definition.

## Differential decode of the three prefs fixtures

All 96 streams hashed; only these 214 streams differ:

| comparison | 214 changes |
|---|---|
| base -> col (the "add column" save) | CTable Var2Data byte at stream 12 (payload +8, 6->2); CTable FixedMeta last byte (df->d7); CV_iew Var2Data 96 one-byte colour changes in view 3 (`<fmt ... Lclr/Dclr>` values `FFFFFFFF/FFBCD5EE` -> `FF151515/FF000000`); CEdl +210 B path record; every 214 FixedData identity and 214/Props identity |
| col -> date (`DateFormat=5`) | **only** CTable Var2Data byte at stream 11 (payload +7, `0xff` -> `0x05`); plus identity/path churn (CEdl path +2 B, CEdl size word in 214/Props, 214 FixedData identities) |

**No column was added.** The CTable block still holds 9 column records in all
three fixtures (bytes identical apart from the flag byte), and byte scans for
the Number1 field id `57 00 40 0b` and for UTF-16 `Number1` over every stream of
all three files find **zero** hits under `214/` (the only field-id hits are the
two catalog bytes in `114/Props`, in every fixture). Why: `analysis/typelib.tsv`
line 9354 gives the real `TableEdit` order
`Name, TaskTable, Create, OverwriteExisting, NewName, FieldName, NewFieldName,
Title, Width, Align, ShowInMenu, LockFirstColumn, DateFormat, ...`. The oracle's
"add" call passed `("Entry", True, False, False, None, "Number1", ...)`, i.e.
Create=False and OverwriteExisting=False, and it returned True without adding a
column. The second call set OverwriteExisting=True and DateFormat=5, which is
why the date-format byte is the only semantic 214 change.

The W50 `114/Props` tail word key `0x00010001` moves 298 -> 332, exactly equal
to `114/TBkndTask/Var2Data` (298 -> 332). That Var2Data change is Project's own
save compaction/churn (a stale 32-byte project-name block appears at offset 76;
the real name block moves to the end), so the Props word is size/offset-like,
not the date-format code. The only date-format correlate is CTable payload +7.

## Measured vs inferred

Measured: inventory; the four-part framing, magic, entry layouts, entry sizes,
row sizes, ordinals, captions, identity churn; the CTable block header fields,
column count rule, column field ids and title offset; the date-format byte
(0xff -> 5 in a single-variable pair); the CV_iew row/block structure and key
families; CEdl as a Props document; every diff byte listed above; the absence of
Number1 in 214.

Inferred: that 0xff means "use the application default"; that CTable flags +8
and the FixedMeta entry trailing word encode table options; that the CV_iew
colour change is view-style churn rather than a table edit; that the
`0x22400014`/`0x2240002f` key meanings generalise beyond these fixtures; that
the 9 opaque column bytes are width/align/format fields.

## Open / next three actions

1. **A real "add a column" fixture**: rerun the oracle TableEdit with
   `Create=True` or `OverwriteExisting=True` and no DateFormat change, then diff
   `214/CTable`. That would show whether a column is an appended 115-byte record
   and whether the block's `+4`/`+8` words change.
2. **Column-record header semantics**: one fixture per column property (width,
   align, title) and diff the 9 opaque bytes; today only field id and title are
   proved.
3. **Table options**: isolate ShowInMenu / LockFirstColumn / Width to identify
   `CTable block +8` and the FixedMeta entry trailing word; both changed on the
   wave-10 save and neither is named.

## Traps

- `TableEdit`'s Boolean return is not evidence that an edit happened; the arg
  order above is the typelib's, not the oracle prose's.
- Do not trust sizes: the "add" save changed no 214 stream size except the CEdl
  path. Column count is in the block header, not in the stream size.
- Identities churn on every save. Never treat a 16-byte tail difference as a
  semantic change.
- CV_iew main-block payloads look like Props streams (same 16-byte prefix) but
  are not; `props::parse_props` accepts one record and a huge tail, so only the
  class name decides whether a block is a Props document (CEdl).
- Never write or patch `214/`; Project rewrites most of it on save and the
  fixture pair shows cache churn independent of the edit.
- The test blobs are raw Var2Data only; the other three parts are generated from
  the measured framing, so a change to the framing constants must update the
  test generator too.
