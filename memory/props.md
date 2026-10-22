# memory/props.md -- `114/Props`, `214/Props`, `Props14` (W50, 2026-10-22)

Read this, then `docs/handoff/w50.md`, then `docs/analysis/oracle/w10-oracle.md`
section 2 (formulas) and section 4 (preferences). The decoder is read-only;
there is deliberately no Props writer.

## What is on disk

| path | what |
|---|---|
| `core/props/props.h`, `props.cpp` | `PropsDocument` parser, formula extraction, tail scan |
| `core/props/report_main.cpp` | `props_report` CLI for an already-extracted stream |
| `core/props/CMakeLists.txt` | `core_props`, `props_tests`, `props_report` |
| `tests/props_tests.cpp` | 520 checks, no corpus dependency |
| `tests/data/synthetic-props.bin` | raw `114/Props` from `corpus/probe/w10-formulas.mpp` (SHA256 `8739cdb83d96210ed3874ad14055f02c88b97f9c5ff74c343ad04d548b8e0334`, 89,266 B) |

The fixture is synthetic: its only UTF-16 strings are the project name
`w10-formulas`, the generic `TBknd...` table-name list, `Standard` and the six
formulas. No client task/calendar name is in it.

## Verification (exact, 2026-10-22)

```
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake -S . -B build\vs-w50 -G "Visual Studio 17 2022" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON'
cmd /c '"...\vcvars64.bat" >nul && cmake --build build\vs-w50 --config Release --target props_tests props_report'
.\build\vs-w50\core\props\Release\props_tests.exe          -> PASS: 520 checks, 0 failures
ctest --test-dir build\vs-w50 -C Release --output-on-failure -R "props"   -> 1/1
```

The optional corpus-blob case is guarded and not required. With
`PROPS_CORPUS_BLOB=<extracted 114/Props>` it adds 2 checks (522 total, 0
failures); with `MPP_NO_CORPUS` set it skips.

## Measured layout

Header, 16 bytes: `u32 size_word`, `u32 size_repeat`, `u32 format word`,
`u32 declared_entries`.
- `114/Props` and `214/Props`: `size_word == size_repeat == stream_size - 4`
  (6 probe streams and 4 corpus streams measured).
- `Props14` does NOT follow that: deltas were 32 (probe), 56 (corpus A) and 80
  (corpus B). The parser therefore only requires the two size words to agree,
  be >= 16 and be <= stream size. `PropsHeader::size_word` reports the value.
- `declared_entries` is not a record count. Probe `114/Props`: 262 declared vs
  161 parsed records. Corpus: 290 vs 175/174. `Props14` corpus B even read
  `0x0001006f` / `6488086`. Do not use it as a loop bound.

Records, back to back, no padding: `u32 payload_length`, `u32 id`, `u32 kind`,
then `payload_length` bytes.
- `id` has byte 2 = `0x40` on every measured id (`0x0240xxxx`, `0x0340xxxx`,
  `0x0440xxxx`, `0x0b40xxxx`, `0x3540xxxx`). That byte is the parser's
  record-boundary test.
- Measured kinds: 0 = blob/string, 2 = 16-bit, 4 = 32-bit, 9 = 64-bit (8 zero
  bytes in the two probe records; the numeric meaning is inference). Unknown
  kinds are preserved, not rejected.
- After the last valid record the remainder is an opaque `tail`; its size is
  fixed per stream variant (114/Props probe: 85,408; 214/Props: 3,200;
  Props14: 263/301/325).

Record meanings measured:
- `0x024003e8` kind 0: comma-separated `TBkndTask,TBkndRsc,...` table-name list.
- `0x02400008` kind 0: project name (payload length changes with the name).
- `0x02401390` kind 0: custom-field formula table (below).
- `0x02400029` / `0x024013c2`: 16-byte identity/GUID-like values, churn on save.

## Formula recovery (the product result)

The wave-10 oracle script `scripts/oracle_w10.py` sets six formulas on six known
field ids. `extract_formulas()` recovers all six, in the stored order, by field
id (not by order):

| field id | custom field | stored formula (Project's rewrite) | block | text | format words | trailer |
|---|---|---|---|---|---|---|
| `0x0b400033` | Text1 | `[MSPJ188743694] & "-" & [MSPJ188744967]` | 2786 | 2822 | 7 x `0x74` | none |
| `0x0b400048` | Flag1 | `IIf([MSPJ188743712]>50,1,0)` | 2902 | 2938 | 7 x `0x5c` | none |
| `0x0b400057` | Number1 | `[MSPJ188744967]*2` | 2994 | 3030 | 7 x `0x4c` | `0x67` |
| `0x0b400058` | Number2 | `[MSPJ188744967]/480` | 3070 | 3106 | 7 x `0x4c` | none |
| `0x0b400067` | Duration1 | `[MSPJ188744967]/2` | 3146 | 3182 | 7 x `0x4c` | `0x72` |
| `0x0b400109` | Date1 | `[MSPJ188744965]+7` | 3222 | 3258 | 7 x `0x4c` | `0x30` |

The oracle source formulas are `[Duration]*2`, `[Duration]/480`, `[Start]+7`,
`IIf([% Complete]>50,1,0)`, `[Name] & "-" & [Duration]`, `[Duration]/2`; the
field-id association above is the `CUSTOM` list in `scripts/oracle_w10.py`, so
it is measured, not inferred. `[MSPJ<decimal>]` is the decimal field id:
`188743694 = 0x0b40000e` (Name), `188744967 = 0x0b400507` (Duration text),
`188743712 = 0x0b400020` (% Complete), `188744965 = 0x0b400505` (Start).

Formula record payload layout (measured): `u32 inner_size` (== payload length),
`u32 count`, `count * {u32 field_id, u32 relative_block_offset}`, then blocks at
`payload_start + relative_block_offset`. Block: `u32 13`, `u32 0xffffffff`,
7 x `u32 format words`, UTF-16LE text, `u16 0`, optional `u32 trailer` (present
when the next word is not the next block's 13). The same six formulas and the
same offsets (+2 in `formulas2`/`formulas3`, only because the project name is
2 bytes longer) were measured in all three formula fixtures.

## Table / view findings (honest version)

- `114/Props` contains **no table names and no column-list text**. The prefs
  fixtures have only three UTF-16 runs: the table-name list, the project name,
  `Standard`.
- Correction to `w10-oracle.md` section 4: the `114/Props` -2 B delta for
  "base -> column added" is the project-name record length (32 -> 30,
  `w10-prefs-base` -> `w10-prefs-col`), not a table definition. The +2 B back
  is the longer `-date` project name.
- The actual table/date-format change in `114/Props` is confined to the opaque
  tail plus identity/name churn. Measured aligned diffs (same-size section):
  - column added: tail typed property key `0x02000001` value 0 -> 2 (tail+28),
    and one word at tail+60,176: 168 -> 208. Meaning inferred.
  - `DateFormat=5`: only tail typed property key `0x00010001` value 298 -> 332
    (tail+60) changes, plus name/identity churn. 332 is not the literal 5, so
    this is an offset/size-like value, not a proven format code.
- `Props14` is where W46 measured +192 B, but that growth is a recent-file
  **path string** (18 B -> 196/198 B in one record), not table state. Do not
  build a table writer on that delta.
- `214/Props` keeps its size across the prefs fixtures (13,102 B); its content
  was not decoded beyond header/records/tail (tail 3,200 B, same typed-property
  prefix as `114/Props`). The real view/table state is likely under `214/`
  (`CTable`, `CV_iew`, ...), which this stream does not own.

The tail's first 112 bytes are six typed `[4][key][4][value]` groups; then a
7-word/28-byte field-reference array starts at tail+112. The probe has 1,312
entries, first id `0x0b400017`, then `0x0b400056/62/9f/f7`, then a sorted
catalog `0x0b400000..0x0b40059d`; one 40-byte gap was measured. The entry
semantics are NOT proven, so the API calls them field references, not columns.

## Measured vs inferred

Measured: header/record framing, ids/kinds, record payloads named above, the
formula table and all six formulas/offsets/formats/trailers, tail sizes and the
typed-property prefixes, the field-reference array stride and ids, the prefs
diff bytes and their exact offsets.

Inferred: that kinds map to integer widths (payload sizes support it), that the
tail typed keys mean anything semantic, that the field-reference array is a
column/field property list, and that the `0x00010001` value is the date-format
control rather than a dependent size/offset.

## Open / next three actions

1. If a table/view persistence path is wanted, decode `214/CTable` and
   `214/CV_iew` streams, not `114/Props`; the brief's table-definition premise
   is not supported by bytes.
2. If the tail semantics matter, build a fixture that changes exactly one
   property and diff the tail beyond the first 112 bytes; the current prefs
   fixtures only isolate three words.
3. If formula coverage needs widening, build a synthetic fixture with formulas
   on Number3/Text2 and confirm the same `0x02401390` record and directory
   layout; no extra fixture is needed for the six existing associations.

## Traps

- `declared_entries` is not a record count (see above). Do not loop on it.
- `Props14` is not `size-4`; corpus B's `declared_entries`/format word are junk.
  Parse records, do not trust header fields you have not measured per variant.
- Formula blocks are not all the same length: the optional trailer means you
  must trust the UTF-16 NUL, not a fixed stride.
- The formula record id `0x02401390` is stable in the three probe files, but
  the decoder finds it by payload content, not by hard-coding the id.
- A damaged formula block leaves five formulas; the test covers that.
- Do not write `114/Props`: a stray u32 at +510 makes Project refuse the file
  (`spec/mpp-writer.md` section 10.4).
