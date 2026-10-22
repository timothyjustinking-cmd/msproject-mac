# memory/viewstate.md -- `214/` view-state streams (W62, 2026-10-23)

Read this, then `docs/handoff/w62.md` (append writer + CV_iew decode, wave 15),
then `docs/handoff/w58.md` (encoder, wave 14), then `docs/handoff/w56.md`
(decoder evidence, wave 13), then `docs/analysis/oracle/w14-oracle.md` section 1
(column layout) and `w10-oracle.md` section 4 **including the W50 correction**.

State: **decoder + targeted writer + append**. There is still no writer for
`CV_iew` item bodies or for CEdl/Props records, and row height / splitter were
not located.

## What is on disk

| path | what |
|---|---|
| `core/viewstate/viewstate.h`, `viewstate.cpp` | collection framing, CTable table/column decode (width byte), CV_iew view + main-block/notes/options decode, CEdl Props reuse; `encode_view_state`, `set_table_date_format`, `set_table_column`, `append_table_column`, `apply_table_edits`, `parse_view_records` |
| `core/viewstate/report_main.cpp` | `viewstate_report` CLI; `--roundtrip` re-encodes every class and requires byte-identical parts |
| `core/viewstate/CMakeLists.txt` | ctest `viewstate` (#42), `viewstate_writer` (#43), `viewstate_append` (#44) |
| `tests/viewstate_tests.cpp` | 361 checks, no corpus dependency |
| `tests/data/synthetic-viewstate-ctable-{base,col,date}.bin` | raw `214/CTable` Var2Data from the synthetic wave-10 prefs fixtures (1051 B each) |
| `docs/handoff/w62.md` | this wave: append + CV_iew evidence, byte tables, traps |

Blob SHA256 (unchanged):
`80573060bbacc24b656f9a6ff70bf907a421bee0677c2f99a8f7d3ab5993f4b9` (base),
`7b753dfe1c930cf95c6c2e3d75a821505a2b6f43c35fb42c30979d824e5ff52e` (col),
`e106e86452c37f4ea73263f043ae5ef1280beef46999c62271933960705eb3cb` (date).

## Writer API (wave 14 + wave 15)

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
// seam for app/document.cpp:
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
- No-op: parse -> encode is byte-identical (test + `--roundtrip`).
- `set_table_date_format` writes CTable block payload +7 = stream
  `block_offset + 11`. Test proof: committed `col` (0xff) + set 0x05 encodes to
  the committed `date` blob byte-for-byte.
- `set_table_column` replaces the 115-byte record at an existing index: field id
  at +0, title UTF-16 at +13 (zero-filled tail, 51-code-unit capacity), nine bytes
  at +4 preserved. Absolute record in a block: `block_offset + 4 + 12 + index*115`.
- Refusals: bad table/column index, field id 0, title >51 units, non-ASCII,
  undecoded table, unparsed state. Refusals leave the state untouched.

## Append (wave 15, W62) -- OURS, not Project

Project's automation surface **cannot append**: `TableEdit(..., ColumnPosition=0)`
replaces index 1 (w14), `ColumnPosition` 8/9 is rejected, `Application.ColumnInsert()`
hangs this build, `Application.Tables()` wedges. The encoder is the only append
implementation, proved structurally plus a coordinator Project-resave gate
(pending, see `docs/handoff/w62.md` §8).

`append_table_column` inserts a 115-byte record after the last column:

| record offset | contents |
|---|---|
| +0 | u32 field id |
| +4 | `0c 10 08 00 08 01 00 00 00` (nine bytes Project wrote for a fresh column) |
| +13 | UTF-16 title (51 units, zero-filled tail) |

Then: block length word +115; payload +4 (u16 `last_column_index`) = new count-1;
later Var2 blocks, VarMeta offsets, decoded table/view block offsets shift by 115.
On the 1051-byte base: Var2Data 1051 -> 1166, length 1047 -> 1162, payload+4
08 -> 09; FixedData/FixedMeta unchanged; VarMeta only `var2_size`. Decode-back:
10 columns, last index 9. Twice: 11 columns. Two-table shift is tested too.

Refusals: bad table index, undecoded table, field id 0, >51 units, non-ASCII,
0x10000-column table (u16 index cannot grow), position out of range. A refusal
leaves every part unchanged.

`apply_table_edits` copies the state, applies edits in order, encodes; on failure
`out` is empty and the input state is untouched. W64 wires it.

## CV_iew -- view definitions (wave 15: main block decoded)

Rows: 138 bytes, ordinal at +0 (NOT the row index: corpus files skip ordinals 13
and 22), UTF-16 caption at +4, identity at +122. Blocks are keyed
`0x20400006` (main), `0x22400014` (notes), `0x2240002f` (timeline).

Main block framing (NOT a Props header):

```
+0 u32 size-4; +4 u32 same; +8 u16 opaque; +10 u16 1; +12 u16 item count;
+14 u16 opaque; +16 items {u32 len, u32 id 0x2240xxxx, u32 kind, payload[len]},
one pad byte after an odd total (12+len).
```

Count must be read as u16 at +12: 3 corpus blocks have a non-zero word at +14.
The walk must end exactly at the payload end, else the block stays opaque.

Decoded item ids:

| id | meaning |
|---|---|
| `0x2240002a` | view display name |
| `0x2240000a` | single view: table name; combination view: top view name |
| `0x2240000b` | single view: filter name; combination view: bottom view name |
| `0x22400018` | group name |
| `0x22400014` | notes: 9 u32 chunk lengths + chunks (3x3), verified sum |
| `0x22400009` | 970-byte view-format template: 4 page margins (1/1000 inch, 500 = 0.5") + fifth word; rest opaque |
| `0x22400008` | Gantt bar-style blob (bar names inside); opaque |
| `0x2240002f` | timeline XML; opaque |

Combination classification is inference: `0x2240000a` matching another view's name.
Selected-table proof: `dump-w14-tablecol-C` view 2 reads "W14 Entry C" after the
oracle created and applied that table (base: "&Entry"). Corpus check: 22/22 main
blocks decode, 0 opaque; names match row captions.

**Column widths are NOT in CV_iew.** They are the first of the nine bytes at
`214/CTable` column record +4: in `corpus/project_a.mpp` the same Task
Name field reads 0x46 / 0x2a / 0x18 in three tables; the wave-14 Width=12 record
has 0x0c but that byte was already 0x0c, so the mapping is inference.

**Row height and splitter position were not found** in the stream; no measured
save changes them. Do not invent a layout.

## CEdl / 214/Props -- cache, not table state

`214/CEdl`: one 160-byte row and one block that is a Props document. `214/Props`
is an ordinary Props stream. The writer re-frames but does not interpret them.
**Never commit an extracted CEdl blob: it contains a host path.**

## Framing (measured, unchanged from W56)

- FixedData: rows of a class-specific size; `u32 ordinal` + UTF-16 caption at +4;
  16-byte identity/GUID at the end; 230-byte classes carry a second caption at +136.
- FixedMeta: header `magic 0xfadfadba, flags, declared_count, fixed_data_size`;
  entries `+0 state, +2 var_count, +4 fixed_offset, rest opaque`.
- VarMeta: 24-byte header, entries `{uid, offset, key}`; offset points at a block's
  length word; **uid is the FixedData ordinal, not the row index**.
- Var2Data: back-to-back `u32 payload_length` + payload.

Row sizes: CTable/CFilter/CGrouping 230, CV_iew 138 (3 synthetic rows, 22 corpus),
CEdl 160.

## CTable -- the table definition

Payload header 12 bytes: `+0 u32 ordinal, +4 u16 last_column_index, +6 u8 format
word (1), +7 u8 date format (0xff default), +8 u32 flags, +12 columns`.

Column record 115 bytes: `+0 u32 field id`, `+4` nine bytes whose first is very
likely the width (see above), `+13` UTF-16 title (51 units). Stock Entry fields:
`0x0b400017, 0x0b4000cd, 0x0b400500, 0x0b40000e, 0x0b400507, 0x0b400505,
0x0b400506, 0x0b40002f, 0x0b400031`; Number1 `0x0b400057`, Text1 `0x0b400033`.

## Verification (exact, 2026-10-23)

```
cmd /c 'call "C:\...\vcvars64.bat" >nul && cmake -S . -B build\w62 -G "Visual Studio 17 2022" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON'
cmd /c 'call "C:\...\vcvars64.bat" >nul && cmake --build build\w62 --config Release'
build\w62\core\viewstate\Release\viewstate_tests.exe    -> PASS: 361 checks, 0 failures
cmd /c 'call "C:\...\vcvars64.bat" >nul && ctest --test-dir build\w62 -C Release --output-on-failure'
                                                       -> 49/49 passed (48 pre-existing + viewstate_append)
```

Per-case: real 51, ctable 10, views 13, views-main 38, edl 7, empty 5, errors 24,
text 3; writer-roundtrip 32, date 21, column 26, refusals 20, append 54,
append-refusals 29, apply-edits 28. `ctest` needs vcvars.

No-op proof on extracted bytes: `viewstate_report --roundtrip <tree>` over the 9
non-empty synthetic `214/` trees = **72/72 byte-identical, 0 differing**. Extra: a
real corpus tree (`project_a`, `214/CV_iew/Var2Data` 1,022,179 B) round-
trips all 8 classes byte-identically.

## Open / next three actions

1. **Coordinator gate for append**: write the four parts into a copied
   `synthetic-base.mpp` through the CFB writer, have Project save it, decode the
   re-saved `214/CTable`. Needs W64's document wiring; COM is coordinator-only.
2. **CV_iew editing**: decode exists, no mutator. A measured save that changes the
   table name/filter/group of a view would prove item edits and whether the
   `0x22400036`/`0x22400030` twins must be updated too.
3. **Row height / splitter**: not located. One oracle save that changes row height
   and one that moves the split, diffed against a control, is the way in.

## Traps

- CV_iew prefix is not Props: count is u16 at +12, not u32 at +8.
- Items are 2-byte padded after an odd payload length.
- Ordinal is not row index (corpus CV_iew/CTable skip ordinals).
- Append needs three writes: length word, payload +4 (stream `block_offset + 8`),
  and in-memory `VarBlock::payload_length`; miss the last and encode drops the
  record silently.
- Inserting into a non-last block moves later blocks, VarMeta offsets and decoded
  `TableDef::block_offset` / `ViewDef::main_block_offset`.
- Two offsets per block: `block_offset` points at the length word; payload +4 =
  stream `block_offset + 8`.
- base (flags 6) and date (flags 2) are not a single-variable pair; reproduce the
  date save from **col**.
- VarMeta entry order is not block order (CV_iew); match `(uid, key)` and prefer
  the entry's old offset when keys repeat.
- Identities churn per save; never synthesise or diff them as semantics.
- `std::fill(vec<uint8_t>..., 0)` under `/W4 /WX` is C4244; use `uint8_t{0}`.
- `ctest` is not on the bare PATH here; run it after vcvars64.
- The test blobs are raw Var2Data; the other parts come from the test generator,
  so keep the framing constants and generator in sync.
