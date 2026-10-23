# memory/viewstate.md -- `214/` view-state streams (F2, wave 18, 2026-10-23)

Read this, then `docs/handoff/f2.md` (wave 18: CEdl cache rule, CFilter/CGrouping
payloads, drawing inventory, row-height/splitter oracle recipe), then
`docs/handoff/f1.md` (derived records, wave 17), `docs/handoff/w62.md` (append
writer + CV_iew decode, wave 15), `docs/handoff/w58.md` (encoder, wave 14),
`docs/handoff/w56.md` (decoder evidence, wave 13), then
`docs/analysis/oracle/w14-oracle.md` §1 and `w10-oracle.md` §4 including the W50
correction.

State: **decoder + targeted writer + append + CV_iew edits + CEdl regeneration +
corpus CEdl cache parsing + CFilter criterion framing + drawing inventory.**
Still not located: row height, splitter (recipe in `docs/handoff/f2.md` §6).
Still opaque: CFilter criterion semantics, CGrouping 71-byte body, CEdl residue
bytes, CDrawing bodies, `0x22400007` / most of `0x22400009`.

## What is on disk

| path | what |
|---|---|
| `core/viewstate/viewstate.h`, `viewstate.cpp` | framing, CTable/column decode, CV_iew view+item decode, CEdl typed record set + cache-tolerant Var2Data walk, CFilter/CGrouping row+block decode, encoder, all mutators, `parse_var2_edl_cache`, `view_item_kind` |
| `core/viewstate/report_main.cpp` | `viewstate_report`: `--roundtrip`, `--regenerate-edl`, `--diff OTHER`, `--dump`, `--inventory`, `--class` |
| `core/viewstate/CMakeLists.txt` | ctest `viewstate` (#44), `viewstate_writer` (#45), `viewstate_append` (#46). No new entries in waves 17/18 |
| `tests/viewstate_tests.cpp` | 791 checks, no corpus dependency |
| `tests/data/synthetic-viewstate-ctable-{base,col,date}.bin` | 1051-byte CTable Var2Data blobs |
| `docs/handoff/f2.md` | wave 18: cache rule, CFilter/CGrouping layouts, drawing inventory, row-height/splitter recipes |

Blob SHA256 (unchanged): `80573060...f4b9` (base), `7b753dfe...f52e` (col),
`e106e864...b3cb` (date).

Never commit extracted `214/CEdl` blobs: they carry a client host path.

## Framing (unchanged from W56)

- FixedData: class-sized rows; `u32 ordinal` + UTF-16 caption at +4; 16-byte
  identity at the end.
- FixedMeta: `magic 0xfadfadba, flags, declared_count, fixed_data_size`; entries
  `+0 state, +2 var_count, +4 fixed_offset, rest opaque`.
- VarMeta: 24-byte header, entries `{uid, offset, key}`; **uid is the FixedData
  ordinal, not the row index**; offset points at a block's length word.
- Var2Data: `u32 payload_length` + payload, normally back-to-back.
- Row sizes: CTable/CFilter/CGrouping/CDrawing 230, CV_iew 138, CEdl 160.

## The row record set (wave 17; corrects W56)

Every 230-byte `CTable`/`CFilter`/`CGrouping` row:

```
row + 0x6e u32 set size (0 = no set) ; +0x72 u32 count ;
+0x76 count * {u32 id, u32 payload_offset} ; payloads follow
```

`...0009` = display name (UTF-16, no accelerator); `...0007` = criteria record.
Keys `0x20400007/9` (CTable, CFilter), `0x2d400007/9` (CGrouping). `CDrawing` is
230-byte-row but **not** a record set (its +0x6e bytes are garbage).

## CTable -- the table definition

Payload header 12 B: `+0 u32 ordinal, +4 u16 last_column_index, +6 u8 format (1),
+7 u8 date format (0xff default), +8 u32 flags, +12 columns`. Column record 115 B:
`+0 u32 field id`, `+4` nine bytes (first very likely width), `+13` UTF-16 title
(51 units). Stock Entry fields: `0x0b400017, 0x0b4000cd, 0x0b400500,
0x0b40000e Task Name, 0x0b400507, 0x0b400505, 0x0b400506, 0x0b40002f Pre,
0x0b400031 Suc`; Number1 `0x0b400057`, Text1 `0x0b400033`.

## Writer API (waves 14/15/17/18)

```cpp
bool encode_view_state(const ViewState&, EncodedParts&, WriteError* = nullptr);
bool set_table_date_format / set_table_column / append_table_column(...);
bool set_view_name / set_view_table / set_view_page_margins(...);
bool regenerate_edl(ViewState& edl, const ViewState& table, WriteError* = nullptr);
bool regenerate_view_state(ViewState& table, const TableEdits&, WriteError* = nullptr);
bool apply_table_edits(const ViewState&, std::span<const TableEdit>, EncodedParts&, ...);
// wave 18:
bool parse_var2_edl_cache(std::span<const uint8_t>, std::span<const VarMetaEntry>,
                          Var2Stream&, ParseError* = nullptr);
std::string_view view_item_kind(uint32_t id);
```

- `ViewState::parsed` gates encode and every mutator.
- Encode recomputes FixedMeta/VarMeta headers, `var2_size`, every VarMeta offset;
  FixedData and identities preserved. **Var2 gaps are written back in stream
  order** (wave 18).
- No-op: parse -> encode byte-identical. Proof: tests + `--roundtrip` 96/96 over
  the 12 synthetic trees **and both corpus trees, all classes, CEdl included**.
- `append_table_column` inserts 115 B: field id at +0, `0c 10 08 00 08 01 00 00 00`
  at +4, UTF-16 title at +13. Block length +115, payload +4 = count-1; later
  blocks/VarMeta offsets/decoded offsets shift. Append is OURS, not Project's
  (wave-15 gate proved Project accepts the result).
- `set_view_name` writes FixedData row +4 (118 B / 59 units); does **not** touch
  `0x2240002a` (caption != display name; measured counter-example).
- `set_view_table` re-frames the `0x2240000a` item (pad `12+len` to even, two
  block size words, block length, later offsets).
- `set_view_page_margins` writes four u32 at `0x22400009` payload start
  (structural, no oracle save).
- `regenerate_edl` refreshes `0x26400018/19/17`; rebuilds the Props document;
  preserves every other record, including `0x26400031` host path.

## CV_iew -- view definitions

Rows 138 B: ordinal at +0 (NOT row index: corpus skips 13 and 22), UTF-16 caption
at +4, identity at +122. Main block (`0x20400006`) is item-framed:

```
+0 u32 size-4; +4 u32 same; +8 u16 opaque; +10 u16 1; +12 u16 item count;
+14 u16 opaque; +16 items {u32 len, u32 id 0x2240xxxx, u32 kind, payload}, one
pad byte after an odd total (12+len).  Walk must end exactly at the payload end.
```

Decoded/known item ids:

| id | meaning |
|---|---|
| `0x2240002a` | display name |
| `0x2240000a` | single view: table name; combination view: top pane view |
| `0x2240000b` | single view: filter; combination: bottom pane view |
| `0x22400018` | grouping name |
| `0x22400014` | notes: 9 u32 chunk lengths + chunks |
| `0x22400009` | 970-byte view-format template; first 4 u32 are margins (1/1000") + fifth word; rest opaque |
| `0x22400008` / `0x22400030` | Gantt bar styles; opaque |
| `0x2240002f` | timeline XML; opaque |
| `0x22400007` | 37 B per-view flags; only trailing handle varies |
| `0x2240000f` / `0x22400010` | calendar view only (6 B / 4 B) |
| `0x22400015` / `0x22400037` | 20 B window state; contains a per-save timestamp |
| `0x2240001d` | applied-grouping cache; byte-equal to CGrouping blocks (12/83 B) |
| `0x2240001f` | 2 B criteria word (0 in every view) |
| `0x22400038` | 12 B applied-definition cache |

Combination classification is inference (`0x2240000a` matching another view's
name). Column widths are NOT in CV_iew; row height/splitter NOT located.

## CEdl -- the current-view cache (wave 18: corpus cache rule)

Block payload = complete Props document; typed records `0x26400018` view,
`0x26400019` table, `0x26400017` field list (+ optional `0xffffffff`), `0x26400031`
host path (opaque). Synthetic family: 470/474 B, 14 records.

**Corpus `project_a`:** 9,932 B, **10 documents** at offsets 0, 968, 1480, 2512,
3552, 4600, 5652, 6712, **7784, 8856**; VarMeta uid 6 -> 968, uid 1 -> 8856
(live). Strict walk stops at 7780 (`10846` with 2,148 B left); the "trailer" is:

```
4 residue bytes (7780..7784) + 1,072-byte document (7784) +
1,072-byte live document (8856) + 4 residue bytes (9928..9932)
```

`project_b`: 2,326 B, 3 documents (0, 786, 1296), no residue.

Rule (`parse_var2_edl_cache`, CEdl only, used only when the strict walk fails):
walk; on an invalid length skip 4 B (max 8) to a candidate whose payload parses
as a complete Props document; keep skipped bytes as `Var2Stream::gaps`; allow up
to 8 trailing bytes; **every VarMeta offset must be a block start**. Encoder
writes gaps back in order. Failure messages name the offset/declared
length/remainder or the bad VarMeta entry.

`regenerate_edl` on the corpus **refuses**: table `"&Entry"` is ambiguous (task
Entry uid 1 and resource Entry uid 4). Not a parse problem; do not relax the
resolver.

## CFilter / CGrouping (wave 18)

CFilter block = `20-byte header + N*36-byte criterion records + UTF-16 string
table`; `S == 20 + 36*N` on **53/53** blocks (12 synthetic fixtures + 2 corpus).

```
+0 u32 ordinal; +4 u32 flags (0x100 synthetic / corpus rows 18/19; 0/1 others,
meaning unknown); +8/+10/+12 u16 0; +14 u16 N; +16 u32 S;
+20 N records: +0 u32 tag, +4 u32 operator, +8 u32 field id or 0; 24 B opaque
+S  strings: "tasks"/"resources" first, then prompts
```

Tags measured `0x0b, 6, 7, 1, 0x19, 0x1a, 0x1c, 0x27, 0x3ed, 0x3ee, 0x3ef`.
Tag 7 = field test; 40/40 `+8` ids are plausible property ids. Operator/operand
semantics opaque; copied verbatim.

`...0007` row record = `u16` (0 in all 30 measured rows) + optional UTF-16 label;
20/20 labels equal the `...0009` display name. **No criteria are stored there.**

CGrouping block: 12-byte header (+0 ordinal, +4 flags, +8 u16, +10 u16) or 83 B
with a 71-byte body. Measured: synthetic 83/0x100/0/0; corpus uid 9,10 = 12 B;
corpus uid 11 "Status" = 83 B, flags 0x100, word8 0, word10 1, body first u32
`0x0b40021f` (a field id). Body opaque.

## CDrawing / CMap / CReport (wave 18 inventory)

| class | synthetic | corpus |
|---|---|---|
| CDrawing | empty | 3 rows x 230 B, 3 blocks 19,420 / 10,982 / 12,062 B at 0 / 19,424 / 30,410 |
| CMap | header-only | header-only |
| CReport | header-only | header-only |

CDrawing rows are not record sets; all bodies opaque; all round-trip.

## Row height / splitter -- NOT located

Ruled out: CTable (block exactly `12 + 115*columns`), CEdl (all records decoded),
CV_iew small items (inventory above), 214/Props. Remaining lead: the 970-byte
`0x22400009` template, whose first four u32 vary per view (500/500/500/500 Gantt,
750/750/1000/1000 Task Sheet, 1910/1910/2540/2540 Resource Sheet). Oracle recipe
(row height + splitter, control-save churn subtraction, `--diff`, item mapping)
is `docs/handoff/f2.md` §6. Do not guess a field.

## Verification (exact, 2026-10-23)

```
cmd /c 'call "C:\...\vcvars64.bat" >nul && cmake -S . -B build\f2 -G "Visual Studio 17 2022" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON'
cmd /c 'call "C:\...\vcvars64.bat" >nul && cmake --build build\f2 --config Release --target viewstate_tests viewstate_report'
build\f2\core\viewstate\Release\viewstate_tests.exe            -> PASS: 791 checks, 0 failures
build\f2\core\viewstate\Release\viewstate_tests.exe writer     -> PASS: 640 checks, 0 failures
cmd /c 'call "...\vcvars64.bat" >nul && ctest --test-dir build\f2 -C Release -R viewstate --output-on-failure'
                                                               -> 3/3 passed (#44/#45/#46)
```

Wave-18 cases: `edl-cache` 40, `filter-payload` 24, `grouping-payload` 18,
`drawing-inventory` 28 (110 new checks). ctest configured total is 51 (two fuzz
entries from another stream); this stream adds none.

A full-tree build is currently red in other streams' files
(`core/mpp/reader.cpp:1549` C2562, `tests/roundtrip_row_write.cpp:571/576` C4456
under `/WX`); build the viewstate targets.

No-op proof: 96/96 over the 12 synthetic trees; both corpus trees all classes
byte-identical (CV_iew 1,022,179 B; CEdl 9,932 / 2,326 B).

`--diff` smoke test: `--diff build\dump-base build\dump-base` -> 0 differing
parts; `--diff build\dump-base build\dump-w15proj` -> 13 differing parts.

## Open / next three actions

1. **Run the row-height recipe** (`docs/handoff/f2.md` §6.2): two control saves +
  `Format > Row Height = 30`, diff, map the changed range to a `0x22400009` or
  `0x22400007` offset. Then the splitter recipe (§6.3, combination view).
2. **Corpus CEdl regeneration**: needs a task/resource discriminator for the
  ambiguous `"&Entry"` table name. Until then regeneration stays synthetic-only.
3. **CFilter criterion semantics**: pair tag-7 records with operand records and
  the operator word using one custom filter created through the oracle.

## Traps

- Do not apply the CEdl cache rule to other classes; do not "fix" `parse_var2`.
- CEdl residue is part of the stream: encoder and `replace_block_payload` must
  carry `Var2Stream::gaps` (offsets shift on edit).
- The corpus CEdl live document is the block VarMeta names, not the first block.
- `CEdl applied table "&Entry"` is ambiguous in the corpus; do not pick the first.
- CFilter count is the u16 at +14, not the word at +4; S must equal 20+36N.
- `...0007` holds no criteria (word 0 + optional label).
- `0x2240001d`/`0x22400038` are definition caches, not geometry.
- `CDrawing` is 230-byte-row but not a record set.
- CV_iew prefix is not Props: count is u16 at +12. Items are 2-byte padded.
- Ordinal is not row index (corpus skips 13 and 22).
- Caption != display name (measured counter-example).
- CEdl record ids are unordered; header size words are `stream_size - 4`.
- `replace_block_payload` must shift blocks, VarMeta offsets, decoded block
  offsets **and gaps**.
- A member-function parameter named `klass` in `ViewState` is C4458 under
  `/W4 /WX`; a shadowed `residue` in tests is C4456. Rename.
- `ctest` needs vcvars; build dir is `build\f2`; never `--parallel`.
