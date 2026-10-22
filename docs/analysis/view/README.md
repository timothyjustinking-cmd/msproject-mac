# Analysis: the recovered UI column catalogue

_Stream W-view, 2026-10-22._

This note records what the repository's only column-order evidence actually is,
so that no one mistakes it for MS Project's default table layout. It supports
`core/view/defaults.cpp` and `spec/view-model.md` §9.

## Source

`analysis/column_field_map.tsv` (41 data rows) was produced by
`scripts/phase2d_column_map.py`, which reads a table at `WINPROJ.EXE` VA
`0x1413b0440` and treats it as `(column ordinal, PjField id)` pairs. The script's
own summary calls it "42 UI column mappings"; that counts the header line. The
data has **41** mappings, and ordinal **19 is missing** in the source sequence
(18 → 20).

Every mapped id is a **Task** field (`0x0b4xxxxx`). The first two u32 at the VA
are header field ids per the script; the committed TSV starts at ordinal 0.

## What the catalogue contains

| ordinal | field id | name in descriptor table |
|---:|---|---|
| 0 | 0x0b40000e | Name |
| 1 | 0x0b400056 | — |
| 2 | 0x0b400017 | ID |
| 3 | 0x0b40009f | — |
| 4 | 0x0b4000c7 | — |
| 5 | 0x0b40000f | Notes |
| 6 | 0x0b4000c2 | — |
| 7 | 0x0b400055 | OutlineLevel |
| 8 | 0x0b40005c | Summary |
| 9 | 0x0b4000e4 | — |
| 10 | 0x0b4000c6 | — |
| 11 | 0x0b4000e0 | — |
| 12 | 0x0b400087 | — |
| 13 | 0x0b4000d2 | — |
| 14 | 0x0b4000d3 | — |
| 15 | 0x0b4000d4 | — |
| 16 | 0x0b400062 | LinkedFields |
| 17 | 0x0b400090 | — |
| 18 | 0x0b4000e3 | — |
| 20 | 0x0b4000f1 | — |
| 21 | 0x0b4000f7 | — |
| 22 | 0x0b4000d6 | Application |
| 23 | 0x0b4000d5 | Parent |
| 24 | 0x0b4000ed | — |
| 25 | 0x0b4000ec | — |
| 26 | 0x0b4000f5 | — |
| 27 | 0x0b4000e5 | — |
| 28 | 0x0b4000e6 | — |
| 29 | 0x0b4000d1 | — |
| 30 | 0x0b400083 | — |
| 31 | 0x0b40017f | — |
| 32 | 0x0b400181 | — |
| 33 | 0x0b4001be | GroupBySummary |
| 34 | 0x0b4001bf | — |
| 35 | 0x0b4001c0 | — |
| 36 | 0x0b4001ba | — |
| 37 | 0x0b4001bb | — |
| 38 | 0x0b4001bc | — |
| 39 | 0x0b4001c6 | — |
| 40 | 0x0b400464 | — |
| 41 | 0x0b4004ff | Active |

10 of 41 are named. The other 31 have no `property_name` in
`analysis/field_metadata_all.tsv`; that is the source of the `—` entries, not an
omission here.

## Why it is not the Entry/Task Sheet table

1. **Order.** The name column is ordinal 0 and `ID` is ordinal 2. The documented
   Entry sheet starts with ID (and Indicators) before Task Name.
2. **Content.** It includes `Application`, `Parent`, `LinkedFields`,
   `GroupBySummary` and `Active` — object-model properties, not sheet columns.
3. **Shape.** A default table carries per-column width and alignment; this table
   is two u32 per entry and the script reads only ordinal/id.
4. **Coverage.** It has no `Duration`, `Start`, `Finish`, `Predecessors` or
   `ResourceNames`, which every Task Sheet variant shows, and which are all named
   in the descriptor table.

The most defensible reading is a compiled UI property/column index, not a saved
table definition. **No default-table layout is evidenced anywhere in the
repository.** The canonical Entry/Summary/Cost tables in `defaults.cpp` are
therefore marked `source="inferred: ... NOT oracle-verified"`.

## Re-runnable check

The table in `core/view/defaults.cpp` is checked against the TSV by the test, and
the same join can be reproduced without building:

```powershell
@'
import csv
meta = {}
for r in csv.DictReader(open('analysis/field_metadata_all.tsv', encoding='utf-8'), delimiter='\t'):
    meta[(r['record_type'], r['field_id_hex'])] = r['property_name']
named = 0
rows = 0
for r in csv.DictReader(open('analysis/column_field_map.tsv', encoding='utf-8'), delimiter='\t'):
    name = meta.get(('Task', r['field_id_hex']), '')
    named += bool(name)
    rows += 1
    print(r['column_index'], r['field_id_hex'], name or '-')
print('rows:', rows, 'named:', named)
'@ | python -
```

Expected: 41 rows, `named: 10`, ordinals 0–18 then 20–41.

## What would replace it

An oracle capture of `project.TaskTables("Entry").TableFields` with
`Field`, `Width`, `Align` (recipe in `spec/view-model.md` §9.3). That capture
would supersede both this catalogue and the inferred tables.
