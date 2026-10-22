# memory/viewstate.md -- `214/` view-state streams (F1, 2026-10-23)

Read this, then `docs/handoff/f1.md` (derived records, wave 17), then
`docs/handoff/w62.md` (append writer + CV_iew decode, wave 15), then
`docs/handoff/w58.md` (encoder, wave 14), then `docs/handoff/w56.md` (decoder
evidence, wave 13), then `docs/analysis/oracle/w14-oracle.md` section 1 (column
layout) and `w10-oracle.md` section 4 **including the W50 correction**.

State: **decoder + targeted writer + append + CV_iew edits + CEdl regeneration +
CFilter/CGrouping framing**. Still not located: row height, splitter position;
still opaque: CFilter criteria, CGrouping payload after +0, CEdl host path /
other records, CV_iew display name <-> caption relation.

## What is on disk

| path | what |
|---|---|
| `core/viewstate/viewstate.h`, `viewstate.cpp` | framing, CTable/column decode, CV_iew view+item decode, CEdl typed record set, CFilter/CGrouping row+block decode; encoder; `set_table_date_format`, `set_table_column`, `append_table_column`, `set_view_name`, `set_view_table`, `set_view_page_margins`, `regenerate_edl`, `regenerate_view_state`, `apply_table_edits`, `parse_view_records`, `parse_row_record_set` |
| `core/viewstate/report_main.cpp` | `viewstate_report` CLI: `--roundtrip`, `--regenerate-edl` (no file writes), `--dump`, `--class` |
| `core/viewstate/CMakeLists.txt` | ctest `viewstate` (#44), `viewstate_writer` (#45), `viewstate_append` (#46). **No new entries in wave 17**: the new cases run inside the existing two |
| `tests/viewstate_tests.cpp` | 681 checks, no corpus dependency |
| `tests/data/synthetic-viewstate-ctable-{base,col,date}.bin` | raw `214/CTable` Var2Data from the synthetic wave-10 prefs fixtures (1051 B each) |
| `docs/handoff/f1.md` | wave 17: what is regenerated / round-tripped / opaque, byte offsets, commands, traps |

Blob SHA256 (unchanged):
`80573060bbacc24b656f9a6ff70bf907a421bee0677c2f99a8f7d3ab5993f4b9` (base),
`7b753dfe1c930cf95c6c2e3d75a821505a2b6f43c35fb42c30979d824e5ff52e` (col),
`e106e86452c37f4ea73263f043ae5ef1280beef46999c62271933960705eb3cb` (date).

Not committed and never to commit: extracted `214/CEdl` blobs (they carry a
host path).

## The row record set (wave 17; corrects W56)

Every 230-byte FixedData row of `CTable`, `CFilter`, `CGrouping`:

```
row + 0x6e (110) u32 set size (0 = no set; built-in rows)
row + 0x72 (114) u32 count
row + 0x76 (118) count * { u32 id, u32 payload_offset }   payloads follow
```

`...0009` = display name (UTF-16, no accelerator); `...0007` = criteria
(2 bytes in the stock rows, opaque).  Key families:
`0x20400007`/`0x20400009` (CTable, CFilter), `0x2d400007`/`0x2d400009`
(CGrouping).  Measured: CTable Entry 38 B (name at rel 26 = row+136), CFilter
46 B, CGrouping 54 B and 52 B (count 1, name at rel 16 = row+126).

**W56's "+136 second caption" is this name payload.**  Reading +136 for a
count-1 set gives tails like `"esources"` / `"sk Group"` on corpus rows.
`FixedRow::display_caption` now prefers the record-set name, falls back to the
+136 read only when the row has no set.  `CDrawing` has 230-byte rows but is
not a record set; it is excluded (its +0x6e bytes are garbage).

## Framing (measured, unchanged from W56)

- FixedData: rows of a class-specific size; `u32 ordinal` + UTF-16 caption at +4;
  16-byte identity/GUID at the end.
- FixedMeta: header `magic 0xfadfadba, flags, declared_count, fixed_data_size`;
  entries `+0 state, +2 var_count, +4 fixed_offset, rest opaque`.
- VarMeta: 24-byte header, entries `{uid, offset, key}`; offset points at a block's
  length word; **uid is the FixedData ordinal, not the row index**.
- Var2Data: back-to-back `u32 payload_length` + payload.

Row sizes: CTable/CFilter/CGrouping 230, CV_iew 138, CEdl 160.

## CTable -- the table definition

Payload header 12 bytes: `+0 u32 ordinal, +4 u16 last_column_index, +6 u8 format
word (1), +7 u8 date format (0xff default), +8 u32 flags, +12 columns`.

Column record 115 bytes: `+0 u32 field id`, `+4` nine bytes whose first is very
likely the width, `+13` UTF-16 title (51 units).  Stock Entry fields:
`0x0b400017, 0x0b4000cd, 0x0b400500, 0x0b40000e, 0x0b400507, 0x0b400505,
0x0b400506, 0x0b40002f, 0x0b400031`; Number1 `0x0b400057`, Text1 `0x0b400033`.

## Writer API (waves 14/15/17)

```cpp
struct WriteError { std::string message; };
struct EncodedParts { std::vector<uint8_t> fixed_data, fixed_meta, var_meta, var2_data;
                      PartSet parts() const; };
bool encode_view_state(const ViewState&, EncodedParts&, WriteError* = nullptr);
bool set_table_date_format(ViewState&, table_index, uint8_t date_format, WriteError* = nullptr);
bool set_table_column(ViewState&, table_index, column_index, uint32_t field_id,
                      std::string_view title, WriteError* = nullptr);
bool append_table_column(ViewState&, table_index, uint32_t field_id,
                         std::string_view title, WriteError* = nullptr);
// wave 17:
bool set_view_name(ViewState&, view_index, std::string_view name, WriteError* = nullptr);
bool set_view_table(ViewState&, view_index, std::string_view table_name, WriteError* = nullptr);
bool set_view_page_margins(ViewState&, view_index, const std::array<uint32_t,4>&, WriteError* = nullptr);
bool regenerate_edl(ViewState& edl, const ViewState& table, WriteError* = nullptr);
using TableEdits = std::span<const TableEdit>;
bool regenerate_view_state(ViewState& table, const TableEdits&, WriteError* = nullptr);
// CTable-edit seam (wave 15):
struct TableEdit { enum class Kind { date_format, replace_column, append_column }; ... };
TableEdit table_date_format_edit(...), table_column_replace_edit(...),
          table_column_append_edit(...);
bool apply_table_edits(const ViewState&, std::span<const TableEdit>, EncodedParts&,
                       WriteError* = nullptr);
```

- `ViewState::parsed` is set only by a successful `parse_view_state`; encode and
  all mutators refuse an unparsed state.
- Encode re-serialises all four parts: FixedMeta header (`fixed_data_size`),
  VarMeta header (`declared_count`, `var2_size`) and every VarMeta offset are
  recomputed from the block walk; FixedData and every 16-byte identity are
  preserved verbatim; VarMeta reserved header words and trailing bytes are
  preserved.
- No-op: parse -> encode byte-identical (test + `--roundtrip`: 96/96 over the
  12 synthetic trees; corpus CTable/CV_iew/CFilter/CGrouping too, CEdl excluded
  -- see below).
- `set_table_date_format` writes CTable block payload +7 = stream
  `block_offset + 11`.  Test proof: committed `col` (0xff) + set 0x05 encodes to
  the committed `date` blob byte-for-byte.
- `set_table_column` replaces the 115-byte record at an existing index: field id
  at +0, title UTF-16 at +13 (zero-filled tail, 51-code-unit capacity), nine bytes
  at +4 preserved. Absolute record: `block_offset + 4 + 12 + index*115`.
- Refusals: bad index, field id 0, title >51 units, non-ASCII, undecoded table,
  unparsed state.  Refusals leave the state untouched.

## Append -- OURS, not Project

Project's automation surface cannot append (`TableEdit(ColumnPosition=0)`
replaces index 1; 8/9 rejected; `ColumnInsert`/`Tables()` hang).  The encoder is
the only append implementation; the wave-15 coordinator gate proved Project
accepts a file with our appended column.

`append_table_column` inserts a 115-byte record after the last column:

| record offset | contents |
|---|---|
| +0 | u32 field id |
| +4 | `0c 10 08 00 08 01 00 00 00` (nine bytes Project wrote for a fresh column) |
| +13 | UTF-16 title (51 units, zero-filled tail) |

Then: block length word +115; payload +4 (u16 `last_column_index`) = new count-1;
later Var2 blocks, VarMeta offsets, decoded table/view/CEdl block offsets shift
by 115.  On the 1051-byte base: Var2Data 1051 -> 1166, block 1047 -> 1162,
payload+4 `08` -> `09`; FixedData/FixedMeta unchanged; VarMeta only `var2_size`.
Decode-back: 10 columns, last index 9.  Refusals: bad index, undecoded table,
field id 0, >51 units, non-ASCII, 0x10000-column table.  A refusal leaves every
part unchanged.

## CV_iew -- view definitions

Rows 138 B: ordinal at +0 (NOT the row index: corpus skips ordinals 13 and 22),
UTF-16 caption at +4, identity at +122.  Blocks keyed `0x20400006` (main),
`0x22400014` (notes), `0x2240002f` (timeline).

Main block framing (NOT a Props header):

```
+0 u32 size-4; +4 u32 same; +8 u16 opaque; +10 u16 1; +12 u16 item count;
+14 u16 opaque; +16 items {u32 len, u32 id 0x2240xxxx, u32 kind, payload[len]},
one pad byte after an odd total (12+len).
```

Count must be read as u16 at +12.  The walk must end exactly at the payload end,
else the block stays opaque.

Decoded item ids:

| id | meaning |
|---|---|
| `0x2240002a` | view display name |
| `0x2240000a` | single view: table name; combination view: top view name |
| `0x2240000b` | single view: filter name; combination view: bottom view name |
| `0x22400018` | group name |
| `0x22400014` | notes: 9 u32 chunk lengths + chunks (3x3), verified sum |
| `0x22400009` | 970-byte template: 4 page margins (1/1000 inch, 500 = 0.5") + fifth word; rest opaque |
| `0x22400008` | Gantt bar-style blob; opaque |
| `0x2240002f` | timeline XML; opaque |

Combination classification is inference: `0x2240000a` matching another view's
name.  Column widths are NOT in CV_iew (they are the first of the nine CTable
column bytes).  Row height / splitter not found.

### Editing (wave 17)

- `set_view_name`: FixedData row +4, 118 B = 59 UTF-16 units, zero-filled.
  Does **not** touch `0x2240002a`: caption and display name are not a
  one-to-one relation (measured counter-example "Gantt &with Timeline" /
  "Gantt Chart with Timeline").
- `set_view_table`: replaces the `0x2240000a` item text; re-frames item length +
  pad, the block's two size words, the block length word, later block/VarMeta
  offsets; re-decodes.  On `dump-base`: view uid 1 item header Var2Data 70,
  text 82; view uid 2 (block 42576) item header 42646, text 42658.
- `set_view_page_margins`: four u32 at `0x22400009` payload start; view uid 1
  margins at 152..167, uid 2 at 42748..42763 on `dump-base`.
- Refusals leave the state untouched.

## CEdl -- the current-view cache (wave 17: typed + regenerated)

`214/CEdl`'s block payload is a complete Props document.  Synthetic family
(`dump-base`): Var2Data 474 B = u32 470 + 470-byte doc, 14 records, no tail.
Corpus family: Props format `0x00014258`, 2 fixed rows, 9 blocks, and a
Var2Data trailer that is not a block walk (see below).

Decoded records (search by id; Project reorders):

| id | meaning | offset in `dump-base` |
|---|---|---|
| `0x26400018` | applied view's FixedData caption ("&Gantt Chart") | header +52, payload +64, 26 B |
| `0x26400019` | applied table's display name (`display_caption` else `caption`) | header +90, payload +102, 12 B |
| `0x26400017` | table columns 1..n-1 field ids, optional `0xffffffff` terminator | header +114, payload +126, 36 B (8 ids + terminator) |
| `0x26400031` | host path (opaque; never regenerate/commit) | header +436 in Project's re-save, payload +448 |

`regenerate_edl(edl, table)` refreshes those three records from the edited
table and its linked CV_iew (resolution: CEdl view name -> view -> view's
`0x2240000a` table name -> CTable row), rebuilds the Props document (order,
kinds, header format/declared, tail preserved; size words = `built - (old_size -
old_size_word)`, delta 4 on both variants), re-frames the block and shifts later
offsets.  Terminator is preserved iff the existing record had it.  All-or-
nothing; a no-op when already consistent.

Proof: `dump-w15gate` (our 10-column gate file, stale CEdl 470 B, 8 ids +
terminator) -> regenerate -> 474 B, 9 ids + terminator, the exact 40-byte
`0x26400017` payload Project wrote in `dump-w15proj`; on `dump-w15proj` itself
the regeneration is a no-op.  Commands in `docs/handoff/f1.md` section 8.

**Page margins:** no CEdl record holds them in any measured file.  The only
margin values are CV_iew `0x22400009`; `regenerate_edl` leaves them untouched.
`set_view_page_margins` writes them explicitly (structural, no oracle save).

**Corpus CEdl limit:** `build/w62/scratch/dump-corpus/214/CEdl/Var2Data` is
9,932 B but only 8 framed blocks walk cleanly (offsets 0..6712); at 7780 the
u32 is 10,846 with 2,148 B left, so `parse_var2` refuses.  The W62 binary
refuses too.  No corpus CEdl round-trip/regeneration; the 2,152-byte trailer
layout is unknown.  Corpus CTable/CV_iew/CFilter/CGrouping still round-trip.

## CFilter / CGrouping -- framing level (wave 17)

`ViewState::filters` / `groupings`; each has row index/ordinal, caption,
`display_name` (row record `...0009`), the `RowRecordSet`, and the resolved
`0x20400006` block (offset, length, payload +0 ordinal).  CFilter additionally
decodes payload +16 as the offset of a UTF-16 applies-to string ("tasks" /
"resources"; 92 in `dump-base`, 236/488/524 on corpus rows).

Understood: the row record set framing + names, block ordinal, CFilter
applies-to.  Opaque: CFilter criteria (`...0007`), all other CFilter header
words, all CGrouping payload bytes after +0 (synthetic blocks 83 B, corpus
blocks 12 B and 83 B).  Copied verbatim by the encoder.

## The seam (wave 17)

`regenerate_view_state(ViewState& table, const TableEdits&, WriteError*)`:
copies the CTable, applies the edits through the mutators, calls
`regenerate_edl` for a linked `"CEdl"`, validates and leaves linked `"CV_iew"` /
`"CFilter"` / `"CGrouping"` byte-identical, then commits.  Links live on
`ViewState::related` (non-owning `std::vector<ViewState*>`; never parsed or
encoded).  Failure leaves every caller state untouched.  Unlinked = in-place
table edit.

## Verification (exact, 2026-10-23)

```
cmd /c 'call "C:\...\vcvars64.bat" >nul && cmake -S . -B build\f1 -G "Visual Studio 17 2022" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON'
cmd /c 'call "C:\...\vcvars64.bat" >nul && cmake --build build\f1 --config Release'
build\f1\core\viewstate\Release\viewstate_tests.exe    -> PASS: 681 checks, 0 failures
build\f1\core\viewstate\Release\viewstate_tests.exe writer -> PASS: 530 checks, 0 failures
cmd /c 'call "C:\...\vcvars64.bat" >nul && ctest --test-dir build\f1 -C Release -R viewstate --output-on-failure'
                                                       -> 3/3 passed (#44/#45/#46)
```

ctest configured total in this checkout is **51**, not 49: two
`fuzz_deep_nesting_guard_*` entries were added by another stream under
`tests/fuzz/CMakeLists.txt`.  F1 added no ctest entries.

Per-case: real 51, ctable 10, views 13, views-main 38, edl 7, empty 5, errors
24, text 3; writer-roundtrip 32, date 21, column 26, refusals 20, append 54,
append-refusals 29, apply-edits 28; wave 17 row-records 25, filters 24,
groupings 17, edl-typed 30, noop-five 34, view-name 30, view-table 40,
view-margins 17, edl-regenerate 63, regenerate 40.  `ctest` needs vcvars.

No-op proof on extracted bytes: `viewstate_report --roundtrip <tree>` over the
12 non-empty synthetic `214/` trees = **96/96 byte-identical, 0 differing**.

## Open / next three actions

1. **Corpus CEdl trailer**: measure the 2,152-byte structure after the 8th
   block (offset 7780) before touching `parse_var2`; until then corpus CEdl
   stays unparsed and no regeneration is claimed for it.
2. **Coordinator gate for regeneration**: write the regenerated CEdl back into
   a copy of `synthetic-base.mpp` via the CFB writer, have Project save it, and
   confirm our `0x26400017`/`18`/`19` survive unchanged (the evidence here is
   structural + the gate-file comparison, not a re-save of our own output).
3. **Row height / splitter**: still not located; one oracle save each, diffed
   against a control, is the way in.

## Traps

- Row record set: name at row + 136 (task-shaped, rel 26) or row + 126
  (name-only, rel 16); do not read +136 unconditionally.
- `CDrawing` is 230-byte-row but not a record set; gate by class.
- CEdl record ids are not ordered; search by id.  Header size words are
  `stream_size - 4` on both measured variants.
- `replace_block_payload` must shift later `VarBlock::offset`, VarMeta offsets
  and decoded `TableDef`/`ViewDef`/`EmbeddedProps` block offsets.
- `set_view_table` must pad `12 + payload` to even and update both block size
  words, the block length word and the VarBlock payload length.
- Caption != display name (measured counter-example).
- A member-function parameter named `klass` inside `ViewState` is C4458 under
  `/W4 /WX`; rename it.
- CV_iew prefix is not Props: count is u16 at +12, not u32 at +8.
- Items are 2-byte padded after an odd payload length.
- Ordinal is not row index (corpus CV_iew/CTable skip ordinals).
- Append needs three writes: length word, payload +4 (stream `block_offset + 8`),
  and in-memory `VarBlock::payload_length`.
- Two offsets per block: `block_offset` points at the length word; payload +4 =
  stream `block_offset + 8`.
- base (flags 6) and date (flags 2) are not a single-variable pair; reproduce the
  date save from **col**.
- VarMeta entry order is not block order (CV_iew); match `(uid, key)` and prefer
  the entry's old offset when keys repeat.
- Identities churn per save; never synthesise or diff them as semantics.
- `std::fill(vec<uint8_t>..., 0)` under `/W4 /WX` is C4244; use `uint8_t{0}`.
- `ctest` is not on the bare PATH here; run it after vcvars64 (and only this
  stream's targets -- the coordinator asked streams not to run the full suite
  concurrently).
- Corpus CEdl is not a block walk; do not "fix" `parse_var2` without measuring
  the trailer.
