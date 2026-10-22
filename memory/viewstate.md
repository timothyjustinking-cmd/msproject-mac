# memory/viewstate.md -- `214/` view-state streams (W58, 2026-10-22)

Read this, then `docs/handoff/w58.md` (writer, wave 14), then
`docs/handoff/w56.md` (decoder evidence, wave 13), then
`docs/analysis/oracle/w10-oracle.md` section 4 **including the W50 correction**
and `docs/analysis/oracle/w14-oracle.md` section 1 (the measured write layouts).

State: **decoder + targeted writer**. There is still no writer for appending a
column, for CV_iew item bodies, or for CEdl/Props records.

## What is on disk

| path | what |
|---|---|
| `core/viewstate/viewstate.h`, `viewstate.cpp` | collection framing, CTable table/column decode, CV_iew view/block decode, CEdl Props reuse; `encode_view_state`, `set_table_date_format`, `set_table_column` |
| `core/viewstate/report_main.cpp` | `viewstate_report` CLI; `--roundtrip` re-encodes every class and requires byte-identical parts |
| `core/viewstate/CMakeLists.txt` | `core_viewstate`, `viewstate_tests`, `viewstate_report`; ctest `viewstate` (#42) and `viewstate_writer` (#43) |
| `tests/viewstate_tests.cpp` | 212 checks (113 decoder + 99 writer), no corpus dependency |
| `tests/data/synthetic-viewstate-ctable-{base,col,date}.bin` | raw `214/CTable` Var2Data from the synthetic wave-10 prefs fixtures (1051 B each) |
| `docs/handoff/w56.md` | decoder evidence |
| `docs/handoff/w58.md` | writer evidence, API, proved/inferred, traps |

Blob SHA256 (unchanged):
`80573060bbacc24b656f9a6ff70bf907a421bee0677c2f99a8f7d3ab5993f4b9` (base),
`7b753dfe1c930cf95c6c2e3d75a821505a2b6f43c35fb42c30979d824e5ff52e` (col),
`e106e86452c37f4ea73263f043ae5ef1280beef46999c62271933960705eb3cb` (date).

## Writer (wave 14, proved scope)

```cpp
struct WriteError { std::string message; };
struct EncodedParts { std::vector<uint8_t> fixed_data, fixed_meta, var_meta, var2_data;
                      PartSet parts() const; };
bool encode_view_state(const ViewState&, EncodedParts&, WriteError* = nullptr);
bool set_table_date_format(ViewState&, table_index, uint8_t date_format, WriteError* = nullptr);
bool set_table_column(ViewState&, table_index, column_index, uint32_t field_id,
                      std::string_view title, WriteError* = nullptr);
```

- `ViewState::parsed` is set only by a successful `parse_view_state`; encode and
  both mutators refuse an unparsed state.
- Encode re-serialises all four parts, not a blind copy: FixedMeta header
  (`fixed_data_size`), VarMeta header (`declared_count`, `var2_size`) and every
  VarMeta offset are recomputed from the block walk; FixedData and every 16-byte
  identity are preserved verbatim; VarMeta reserved header words and trailing
  bytes are preserved.
- No-op: parse -> encode is byte-identical (test + `viewstate_report --roundtrip`
  on 72 extracted collections from 9 synthetic fixture trees, 0 differing).
- `set_table_date_format` writes CTable block payload +7 = stream
  `block_offset + 11`. Test proof: committed `col` (0xff) + set 0x05 encodes to
  the committed `date` blob byte-for-byte.
- `set_table_column` replaces the 115-byte record at an existing index: field id
  at +0, title UTF-16 at +13 (zero-filled tail, 51-code-unit capacity), 9 opaque
  bytes at +4 preserved. Absolute record in a block: `block_offset + 4 + 12 +
  index*115`. **Replace only; append is not implemented or claimed** (Project
  rejects ColumnPosition 8/9; append layout unmeasured).
- Refusals: bad table/column index, field id 0, title >51 code units,
  non-ASCII title, undecoded table, unparsed state. Refusals leave the
  `ViewState` untouched.

## Verification (exact, 2026-10-22)

```
cmd /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake -S . -B build\w58 -G "Visual Studio 17 2022" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON'
cmd /c 'call "...\vcvars64.bat" >nul && cmake --build build\w58 --config Release'
build\w58\core\viewstate\Release\viewstate_tests.exe        -> PASS: 212 checks, 0 failures
cmd /c 'call "...\vcvars64.bat" >nul && ctest --test-dir build\w58 -C Release --output-on-failure'
                                                            -> 48 tests configured; 47/48 passed on
                                                               2026-10-22 at 22:01. The one failure
                                                               is mppwrite_roundtrip_field_values
                                                               in core/mppwrite (W60 in-flight edits,
                                                               not this stream). Excluding it: 47/47.
                                                               viewstate #42 and viewstate_writer #43
                                                               both pass.
```

Per-case: real 51, ctable 10, views 13, edl 7, empty 5, errors 24, text 3
(=113); writer 99 (roundtrip 32, date-format 21, column 26, refusals 20).
`ctest` is not on the bare PATH here; call vcvars first.

No-op proof on real extracted bytes:
`build\w58\core\viewstate\Release\viewstate_report.exe --roundtrip <extracted-root>`
prints `roundtrip: byte-identical ...` per class and exits non-zero on a
difference. 9 dumps x 8 classes = 72 collections, all identical, 0 differing
(includes a 2-table CTable). Extract with olefile into `<root>/214/<Class>/<Part>`;
the CFB names carry three leading spaces, trim them consistently.

## Framing (measured, unchanged from W56)

Four parts, same shape as the `114/` table streams:

- FixedData: rows of a class-specific size. Row starts `u32 ordinal` (1-based,
  equals the VarMeta uid) and a NUL-terminated UTF-16 caption at +4. Every row
  ends in a 16-byte identity/GUID-like value that churns on every save. 230-byte
  classes (CTable/CFilter/CGrouping) also carry a display caption at +136.
- FixedMeta: 16-byte header `magic 0xfadfadba, flags(4), declared_count,
  fixed_data_size`, entries of `entry_size = (size-16)/declared_count`:
  `+0 u16 state(0), +2 u16 var_count, +4 u32 fixed offset`, rest opaque.
- VarMeta: 24-byte header `magic, reserved(0), declared_count, reserved(0),
  reserved(0), var2_size`, entries `{uid, offset, key}`. Offsets point at a
  block's length word and are **not** in block order (CV_iew groups by uid).
- Var2Data: back-to-back blocks `u32 payload_length` + payload. Payload starts
  4 bytes after the block offset.

Row sizes: CTable 230, CFilter 230, CGrouping 230 (2 rows), CV_iew 138 (3 rows),
CEdl 160 (1 row). Inventory per prefs fixture: 96 CFB streams, 33 under `214/`
(8 classes x 4 parts + `214/Props`); `CDrawing`/`CMap`/`CReport` are empty
collections.

## CTable -- the table definition (proved)

One fixed row per table; the block keyed `0x20400006`. Payload header 12 bytes:

| off | field | measured |
|---|---|---|
| +0 | u32 table ordinal | equals FixedData +0 and the uid |
| +4 | u16 last column index | `column_count - 1` (8 for the 9-column Entry) |
| +6 | u8 format word | 1 in every measured table |
| +7 | u8 date-format override | 0xff default, **0x05** after `DateFormat=5` |
| +8 | u32 table flags | 6 stock, 2 after the wave-10 TableEdit save; semantics open |
| +12 | columns | 115-byte records |

Column record: `+0 u32 field id` (task 0x0b40xxxx, resource 0xc40xxxx
family), `+4` 9 opaque bytes, `+13` UTF-16 NUL-terminated title (51 units). Stock
Entry fields: `0x0b400017, 0x0b4000cd, 0x0b400500, 0x0b40000e, 0x0b400507,
0x0b400505, 0x0b400506, 0x0b40002f, 0x0b400031`; only Name (0x0b40000e) has a
title (`Task Name`). Number1 is `0x0b400057`, Text1 `0x0b400033`.

Wave-14 write measurement (w14-oracle.md section 1): `TableEdit` with
`ColumnPosition=0` replaced the record at index 1 (Number1 then Text1), leaving
9 columns and the 1047-byte block unchanged. `ColumnPosition` 8/9 rejected; the
accepted range is 0..7 and 0 means "replace index 1". The 9 opaque bytes were
identical for the Number1 and Text1 replacements, which is why the writer
preserves them (not proof that Project would, see w58 handoff section 7).

## CV_iew -- view definitions (partly proved, no writer)

3 rows of 138 bytes: ordinal at +0, UTF-16 name at +4, identity at +122. Seven
Var2Data blocks grouped by uid: `0x20400006` main view state (42572 / 42692 /
8272 B), `0x22400014` notes/RTF (381 / 409 / 230 B), `0x2240002f` timeline view
data (5594 B, `<TLViewData ...>`, view 3 only). The main block starts with a
Props-like 16-byte prefix but its item encoding is **not** Props record framing;
payloads stay opaque with a preview. No view edit is written.

## CEdl / 214/Props -- cache, not table state (proved as churn)

`214/CEdl`: one 160-byte row and one block that is a complete Props document
(14 records in base, 15 in col/date; the extra record is a UTF-16 file path).
`214/Props`: an ordinary Props stream (13,102 B, 47 records, 3,200 B tail). The
CEdl path growth (+210 B, then +2 B) is the W46-era "table state" red herring.
**Do not commit an extracted CEdl blob: it contains a host path.** The writer
re-frames these collections but does not interpret or edit their records.

## Differential decode of the prefs fixtures (W56, still valid)

| comparison | 214 changes |
|---|---|
| base -> col | CTable Var2Data stream byte 12 (payload +8, flags 6->2); CTable FixedMeta last byte (df->d7); CV_iew 96 one-byte colour changes in view 3; CEdl +210 B path record; every FixedData identity + 214/Props identity |
| col -> date | **only** CTable Var2Data stream byte 11 (payload +7, 0xff -> 0x05); plus identity/path churn (CEdl path +2 B, size words, identities) |

The wave-10 "add Number1" call persisted no column: no Number1 field id or title
under `214/` in any of the three fixtures. The real `TableEdit` argument order
(typelib line 9354) is `Name, TaskTable, Create, OverwriteExisting, NewName,
FieldName, NewFieldName, Title, Width, Align, ShowInMenu, LockFirstColumn,
DateFormat, ...`.

## Measured vs inferred

Measured: inventory; four-part framing/magic/headers/entry sizes/row sizes/
ordinals/captions/identity churn; CTable block header, column count rule, column
field ids, title offset/capacity; date-format byte 0xff -> 5; the wave-14
column-record replacement and that the block size/count did not change; CV_iew
row/block structure; CEdl as a Props document; every diff byte listed above; the
absence of Number1 in the wave-10 214 tree.

Inferred: 0xff means "application default" and the other date codes; CTable
flags +8 and the FixedMeta tail word are table options; CV_iew colour change is
style churn; the `0x22400014`/`0x2240002f` key meanings generalise; the 9 opaque
column bytes are width/align/format fields; preserving those 9 bytes on replace
is consistent with the I/J pair but not a controlled test; zero-filling the
title tail matches I/J but title growth is unmeasured.

## Open / next three actions

1. **Append a column**: unmeasured. Needs an oracle case that grows the column
   count (different API route or a table with spare columns); only then implement
   a growing writer (`last_column_index`, block length, FixedMeta/VarMeta growth).
2. **CV_iew item encoding**: one oracle view-property save (zoom/column width)
   and a diff of the `0x20400006` block. No view write exists.
3. **CEdl/Props record semantics**: what a view/table selection should persist
   there is unmeasured; today only framing round-trips.

## Traps

- Two offsets per block: `block_offset` points at the length word; payload starts
  +4. Payload +7 = stream +11; record i = `block_offset + 4 + 12 + i*115`.
- base (flags 6) and date (flags 2) are not a single-variable pair; reproduce the
  date save from **col** (flags 2, 0xff), not base.
- VarMeta entry order is not block order (CV_iew); match `(uid, key)` and prefer
  the entry's old offset when keys repeat.
- Identities churn per save; never synthesise or diff them as semantics.
- `std::fill(vec<uint8_t>..., 0)` under `/W4 /WX` is C4244; use `uint8_t{0}`.
- `ctest` is not on the bare PATH in this environment; run it after vcvars64.
- CV_iew payloads look like Props streams but are not; only the class name
  decides (CEdl).
- Never write or patch a `214/` stream in a real `.mpp` blindly: Project rewrites
  most of it on save and churns identities; the writer's output has not been
  opened in Project by this stream.
- The test blobs are raw Var2Data only; the other parts are generated from the
  framing constants, so keep constants and the test generator in sync.
