# View model: tables, filters, groups, sorts

_Stream W-view, 2026-10-22. Owner: `core/view/`, `spec/view-model.md`._

MS Project is table- and descriptor-driven: one field model, and the sheet,
Gantt, usage and export all read through it. This spec describes the middle
layer that makes the views configuration rather than four implementations. It
is **plain data plus evaluation**, no UI and no widgets.

Status: implemented and tested in C++20. Verification at the end of this file.

## 1. What exists and why

| Layer | File | Role |
|---|---|---|
| Value | `core/view/value.h` | seven value kinds; date/duration encodings; formatting; ordering/coercion |
| Record set | `core/view/record_set.h` | columnar rows keyed by PjField id; UniqueID identity |
| JSON | `core/view/json.h` | dependency-free reader/writer for the definitions |
| Field references | `core/view/field_ref.h` | name ⇄ id through `core/model/field_table.h` |
| Table | `core/view/table.h` | ordered columns (field, title, width, alignment) |
| Filter | `core/view/filter.h` | criteria tree; evaluation against a RecordSet |
| Group | `core/view/group.h` | key tree with intervals; bucketing and labels |
| Sort | `core/view/sort.h` | key list; stable row order |
| View set | `core/view/view_set.h` | the four collections plus versioned JSON container |
| Defaults | `core/view/defaults.h` | the one evidenced table + inferred defaults |

The single rule the design enforces: **there is no per-view field logic.** A
surface names field ids in a `TableDefinition`; it does not know whether a value
came from `.mpp`, MSPDI or an edit.

## 2. Values

`core::view::Value` is a tagged value with seven kinds. Only the member matching
`kind` is meaningful.

| Kind | Storage | Notes |
|---|---|---|
| `Null` | — | "no value" is not the same as empty text or zero |
| `Bool` | `bool` | MSPDI renders `1`/`0` |
| `Integer` | `int64` | includes enum-valued fields (`Type`, `ConstraintType`, …) |
| `Number` | `double` | cost, rate, units, custom number fields |
| `Text` | `std::string` | UTF-8 |
| `Duration` | `int64` | **6-second ticks**, `4800` = one 8-hour day |
| `Date` | `DateValue` | civil `y/m/d h:m:s`, no timezone |

The duration tick is the same unit as the reader (`mpp::kTicksPerDay`, see
`core/mpp/reader.h` and `spec/mpp-records.md` §4), so `.mpp` and MSPDI values
meet without conversion. MSPDI durations are ISO-8601 (`PT16H0M0S`) and convert
exactly; `P2D` means two 24-hour days (28,800 ticks), not two 8-hour working
days — the schema's calendar-day unit is preserved rather than reinterpreted.

Dates are timezone-naive on purpose. Both file formats store civil time with no
usable zone; converting here would invent one. Any zone policy belongs to the
calendar engine (`core/sched/`).

`format_duration` has three styles: ISO-8601 (lossless, default for CSV),
working days (`2d`), and ticks. ISO output always uses the `PT#H#M#S` form, which
is what MSPDI writes (`PT40H0M0S` for a five-day task), never `P5D`.

`compare_values` defines a total order used by sort and group: same-kind
comparison first; text is coerced to the other operand's kind when possible
(`"8"` vs `8`); otherwise kinds rank Null < Bool < Integer < Number < Duration <
Date < Text. Null placement is the caller's decision (sort/group put it last).

## 3. RecordSet

`core::view::RecordSet` is **columnar**: one `std::vector<Value>` per field, plus
a parallel `UID` vector. This is deliberate:

- table rendering and CSV export walk a column at a time;
- MSPDI import appends one value per column while walking a `<Task>`;
- the schema is the field ids the producer actually supplied, in first-seen
  order, and a field that is absent reads as `Value::Null`.

```cpp
RecordSet records{core::model::RecordType::Task};
records.add_field(task_name_id);            // column 0
std::size_t row = records.add_row(unique_id); // UID identity, not a field
records.set_field(row, task_name_id, Value::of_text("Design"));
const Value* v = records.get(row, task_duration_id); // nullptr if not in schema
```

UniqueID is kept out of the value columns because it is identity: links,
assignments and grouping join on it. `ID`, by contrast, is an ordinary field.

## 4. Table

```cpp
struct Column { FieldId field; std::string title; int width = 0; ColumnAlign align = Auto; };
struct TableDefinition {
    std::string id, name, source;
    core::model::RecordType record_type;
    std::vector<Column> columns;
};
```

- `title` empty means "derive from the descriptor table"; if the analysis has no
  name for the field, the title is the hex id (`0x0b4000c7`). This is what makes
  the 31 unnamed recovered columns renderable without inventing labels.
- `width` is characters, `0` = auto. Alignment `Auto` means "by value kind"
  (numeric/duration right, date/text left). Stored, never interpreted in
  `core/view`; the UI applies it.
- JSON allows `{"field":"Name"}` or `{"field_id":188743879}` and optional
  `title`/`width`/`align`. A name that is unknown, or a field id belonging to
  another record type, is a hard parse error — a typo must not become a blank
  column.
- Duplicate fields in one table are rejected.

```json
{"id":"entry","name":"Entry","record_type":"Task",
 "columns":[{"field":"ID","width":6,"align":"right"},
            {"field":"Name"},
            {"field":"Duration","align":"right"}]}
```

## 5. Filter

Criteria tree over field ids:

```json
{"id":"critical","name":"Critical tasks","record_type":"Task",
 "criteria":{"and":[
   {"field":"Summary","op":"equals","value":false},
   {"or":[{"field":"Duration","op":"greater_than","value":"PT0H0M0S"},
          {"field":"Name","op":"contains","value":"milestone"}]}]}}
```

Kinds: comparison, `and`, `or`, `not` (exactly one operand). Operators:
`equals`, `not_equals`, `less_than`, `less_or_equal`, `greater_than`,
`greater_or_equal`, `contains`, `not_contains`, `starts_with`, `ends_with`,
`is_true`, `is_false`.

Semantics that are **implementation choices** (no oracle evidence yet):

- comparisons are case-sensitive;
- a constant is coerced to the field value's kind when possible. Write durations
  and dates as JSON strings (`"PT16H0M0S"`, `"2024-02-01"`); write numbers and
  booleans as JSON numbers/booleans;
- a Null field value equals a Null constant and nothing else; ordering
  comparisons against Null are false;
- text predicates operate on the value's display text (dates/durations are
  formatted first) and are case-sensitive.

`matching_rows()` returns row indices; it does not reorder the RecordSet.

## 6. Group

```cpp
struct GroupKey { FieldId field; GroupInterval interval = None; bool ascending = true; };
struct GroupDefinition { std::string id, name; RecordType record_type;
                         std::vector<GroupKey> keys; bool show_summary = true; };
```

`build_groups()` returns `GroupBucket { Value key; std::string label;
std::vector<size_t> rows; std::vector<GroupBucket> children; }` recursively.

- Intervals: `none`, `day`, `week`, `month`, `quarter`, `year`, `percent`.
  Only Date fields use date intervals; Percent uses numeric values in ten-point
  bands. Any other combination falls back to the raw value rather than inventing
  a bucket.
- Labels: date → `2024-01`, `2024-Q1`, `2024`, `week of 2024-01-01` (the week's
  Monday, avoiding ISO week-year edge cases), none → `2024-01-01 08:00`;
  percent → `0-9`, …, `90-99`, `100`; no value → `(none)`; empty text →
  `(blank)`.
- Buckets are ordered by key, ascending/descending as authored; the Null bucket
  is always last. Rows inside a bucket keep the input order, so the caller sorts
  first and groups second.
- No rollup values are computed: summing Work/Cost needs the scheduling layer.

## 7. Sort

`SortDefinition { id, name, record_type, keys: [{field, ascending}] }`.
`apply_sort()` returns row indices, stable, with Null keys last in both
directions. The source order is the file order and is never mutated.

## 8. View-set JSON

```json
{
  "version": 1,
  "tables": [...], "filters": [...], "groups": [...], "sorts": [...]
}
```

Arrays are optional. `version` must be 1 when present. Unknown keys are ignored
(forward compatibility), but malformed types, unknown field names, duplicate
table columns and duplicate JSON object keys are errors. The writer is
deterministic: insertion order, two-space indent, no trailing newline.

## 9. Default tables — what is evidenced

### 9.1 The recovered column map is a catalogue, not the Entry table

`analysis/column_field_map.tsv` contains 41 `(column ordinal, PjField id)` pairs
for Task fields. `analysis/phase2d_summary.txt` calls it "42 UI column mappings";
the file has 42 lines including the header, i.e. **41 mappings**, and ordinal
**19 is absent** from the source (it jumps 18 → 20).

Evidence: the data and `scripts/phase2d_column_map.py`, which reads the table at
`WINPROJ.EXE` VA `0x1413b0440` and writes the TSV. It is the only recovered
column-order evidence in the repository.

It is **not** MS Project's Entry/Task Sheet layout:

- the order is `Name` (0), …, `ID` (2), …, `Notes` (5), `OutlineLevel` (7),
  `Summary` (8);
- it includes fields no sheet column would show: `Application` (22), `Parent`
  (23), `GUID`-ish unnamed fields, `GroupBySummary` (33), `Active` (41);
- 31 of the 41 field ids have no recovered property name in
  `analysis/field_metadata_all.tsv` (only `Name`, `ID`, `Notes`, `OutlineLevel`,
  `Summary`, `LinkedFields`, `Application`, `Parent`, `GroupBySummary`, `Active`
  are named).

So `defaults.cpp` exposes it as a table with its real provenance,
`recovered-column-map`, with **no invented titles** and no invented widths. Test
`view_model_tests` re-reads the TSV and fails if the 41 ids or their order drift
from the evidence.

### 9.2 The canonical tables are marked inferred

`Entry`, `Summary` and `Cost` are present because the sheet needs a starting
configuration, but the repository contains no evidence of their actual column
layout. Each carries a `source` string starting `inferred:` and stating that it
is **not oracle-verified**; `Indicators` is omitted from Entry because the
descriptor table has no named `Indicators` field. These are data the coordinator
can replace with oracle-captured definitions without touching code.

### 9.3 Oracle recipe that would settle them (coordinator-only)

The oracle is coordinator-only (`docs/coordination.md` Revision 3). Proposed
capture, per table:

```python
from oracle_lock import oracle_session
with oracle_session() as app:
    app.FileNew()
    project = app.ActiveProject
    table = project.TaskTables("Entry")
    for index in range(1, table.TableFields.Count + 1):
        field = table.TableFields(index)
        print(index, field.Field, field.Width, field.Align)
```

Write the result to `tests/fixtures/tables/<name>.json` in this file's shape.
Until that exists, any claim that Entry has particular columns is product
documentation, not this project's evidence.

## 10. Verification

Standalone (top-level CMake does not yet add these directories; see the
handoff):

```powershell
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$cmake='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$src='D:\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile\core\io'
cmd /c "call `"$vc`" >nul && `"$cmake`" -S `"$src`" -B `"$env:TEMP\msproj-io-wx`" -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && `"$cmake`" --build `"$env:TEMP\msproj-io-wx`" --config Debug"
& "$env:TEMP\msproj-io-wx\view\Debug\view_model_tests.exe"
```

Result on 2026-10-22 (MSVC 19.44, /W4 /permissive- /utf-8 /WX): build clean;
`view_model_tests: 270 checks passed`. `core/io` builds `core/view` as a
subdirectory, so one configure/build covers both.

The test covers: value parsing/formatting/comparison; JSON exact output, escapes
and error cases; RecordSet semantics; table/filter/group/sort evaluation and
JSON round-trip; defaults against `analysis/column_field_map.tsv`; view-set
round-trip and versioning.

## 11. Limits and open questions

- **No UI semantics.** Widths are characters, not points; alignment `Auto` is not
  a Project rule yet. If Project stores widths in points, the conversion belongs
  at the surface, not here.
- **Filter/group/sort semantics are unverified** against the oracle: case
  sensitivity, null ordering, and text-vs-number coercion are documented choices.
- **Group labels and intervals** are our encoding; Project's fiscal intervals,
  custom intervals and "group on" variants are not modelled yet.
- **No summary rollups** in group buckets (needs `core/sched`).
- The default-table gap is the largest evidence hole; see §9.3.
