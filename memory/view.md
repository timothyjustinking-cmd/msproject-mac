# memory: view / interchange stream

_Updated 2026-10-22 (W11). Successor: read this, then `docs/handoff/view.md`
(still the import-era handoff) and `spec/mspdi.md` §8 for the writer._

## State: implemented and verified

| Area | Files | State |
|---|---|---|
| Value model | `core/view/value.h/.cpp` | done; 7 kinds, 6-second duration ticks, civil dates, ISO-8601 parse/format, comparison/coercion |
| RecordSet | `core/view/record_set.h/.cpp` | done; columnar, field-id keyed, UniqueID identity |
| JSON | `core/view/json.h/.cpp` | done; dependency-free, ordered objects, duplicate-key/trailing-comma errors, line numbers |
| Table | `core/view/table.h/.cpp` | done; columns field/title/width/align; JSON; unnamed fields by id |
| Filter | `core/view/filter.h/.cpp` | done; criteria tree and/or/not; 12 operators; JSON; evaluation |
| Group | `core/view/group.h/.cpp` | done; key tree, 7 intervals, buckets/labels, JSON |
| Sort | `core/view/sort.h/.cpp` | done; stable multi-key, nulls last; JSON |
| ViewSet | `core/view/view_set.h/.cpp` | done; versioned JSON container, lookups |
| Default tables | `core/view/defaults.h/.cpp` | one evidenced (`recovered-column-map`), three marked inferred (Entry/Summary/Cost) |
| XML parser | `core/io/xml.h/.cpp` | done; entities, CDATA, prefixes, comments; line-numbered errors |
| MSPDI import | `core/io/mspdi.h/.cpp` | done for the curated subset; three RecordSets + links + assignment refs + warnings |
| **MSPDI export** | `core/io/mspdi_writer.h/.cpp` | **done**; `core::io::mspdi::write` / `write_file`; emits all 63 Task / 17 Resource / 20 Assignment mapped elements, links, refs, header; round-trips the three samples; element order inferred (no oracle run) |
| Reader bridge | `core/io/mpp_bridge.h` | done, header-only, guarded by `__has_include("reader.h")`; tested in memory |
| CSV export | `core/io/csv.h/.cpp` | done; table order, RFC4180 quoting, filter/order overloads |

## Verification (last run, 2026-10-22)

MSVC 19.44, `/W4 /permissive- /utf-8 /WX`, "Visual Studio 17 2022". Top-level
build (`build\vs`, Release) because the top-level CMake now wires `core/io` in:

- `ctest --test-dir build\vs -C Release`: **14/14 passed** (the 12 pre-existing,
  plus `io_mspdi_writer` from this stream, plus `core_sched_cpm` added
  concurrently by another stream).
- `io_mspdi_writer_tests: 852 checks passed` (round-trip of all three samples,
  escaping, UTF-8, empty values, durations, dates, root/namespace, one probe per
  mapped field, file helper, unrepresentable records).
- Existing: `io_mspdi_tests` 116, `io_csv_tests` 25, `view_model_tests` 270 —
  unchanged and green.

Commands (copy-paste; `cmake`/`ctest` are **not** on PATH, use the full path):

```powershell
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$cmake='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest=Join-Path (Split-Path $cmake) 'ctest.exe'
cmd /c "call `"$vc`" >nul && `"$cmake`" -S . -B build\vs -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && `"$cmake`" --build build\vs --config Release"   # never --parallel
& $ctest --test-dir build\vs -C Release --output-on-failure
```

## Evidence anchors

- Field ids/names: `analysis/field_metadata_all.tsv` (3,207 rows; 1,763 named),
  joined with `analysis/field_definitive_map.tsv` through
  `core/model/field_table.cpp`. The writer resolves element names through
  `core::view::field_id_by_name`, exactly like the importer.
- Recovered column catalogue: `analysis/column_field_map.tsv`, produced by
  `scripts/phase2d_column_map.py` from `WINPROJ.EXE` VA `0x1413b0440`.
  **41 rows, 10 named, ordinal 19 absent. NOT the Entry table.** Full write-up in
  `docs/analysis/view/README.md`.
- Reader v0 model: `core/mpp/reader.h`; duration = 6-second ticks, packed date =
  `(day<<16)|sec/6`, day 0 = 1983-12-31 (`spec/mpp-records.md`).
- MSPDI namespace `http://schemas.microsoft.com/project`; no local schema file,
  no real export used. `analysis/meta/strings.tsv` contains bare element-name
  strings (`PredecessorLink` at row 141140fd0) but no ordered schema table, so
  it is not evidence for element order.
- Writer element order evidence = the three `tests/io_sample_*.xml` files only.

## Known-open (ranked)

1. **Default table layouts are unevidenced.** Only `recovered-column-map` is
   evidence-backed. Entry/Summary/Cost are inference and say so. Oracle recipe:
   `spec/view-model.md` §9.3 (`TaskTables("Entry").TableFields`).
2. **Writer element order is unverified against MS Project's importer.** The
   samples are hand-written and contradict each other in two places; the writer
   follows `io_sample_tasks.xml` + the importer's table order. Next evidence
   step (coordinator-only oracle): write a sample, `FileOpen` it in Project, and
   re-export; compare order / check Project accepts. Also unverified: whether
   Project requires `<ID>` (writer synthesises the row ordinal when absent) and
   whether omitting `<ScheduleFromStart>` (when the model never recorded it) is
   accepted.
3. **Filter/group/sort semantics unverified** against the oracle: case
   sensitivity, null ordering, coercion, fiscal/custom group intervals.
4. **Reader model needs to grow** before views reach parity: arbitrary field
   values by PjField id, OutlineLevel, Summary/Milestone flags, structured
   links/assignments. Today's adapter handles only the reader's eight members.
5. **MSPDI gaps**: project header field ids (no named Project rows), derived
   `Predecessors`/`ResourceNames`, calendar/ExtendedAttribute/outline-code
   import *and export* (the model cannot hold them), `Type`/`LagFormat` enum
   meanings, plain `Assignment.ID` (no named field, so it is neither imported nor
   written). Re-importing Project's own export warns once for it.

## Traps hit

- **The test process cannot write outside the workspace.** `std::ofstream` to
  `%TEMP%` fails with "cannot open" in the confined test process even though
  `pwsh` can write there. The writer's file-helper test therefore writes its
  temporary file under `IO_SAMPLE_DIR` (inside `tests/`) and removes it. An
  uncaught `MspdiError` terminates the process as `0xC0000409` (no message) —
  wrap `write_file` in try/catch in tests if a failure needs to be reported.
- `core::model::record_type_name` collides with a same-named wrapper in
  `core::view` through ADL. Do not re-export it; qualify calls.
- MSVC's `/WX` is clean, but `/W4` flags `const auto added = add_field(...)` if the
  return value is not used in a check.
- Dropbox rewrites file mtimes constantly; edit tools frequently report "file
  changed since it was read" on this tree. Re-read immediately before **every**
  edit, one edit per read.
- `cmake` and `ctest` are not on PATH in a bare `pwsh`; use the BuildTools
  `...\CMake\bin\cmake.exe` and `ctest.exe` next to it.
- Building in `$env:TEMP` triggers MSBuild warning MSB8029 (intermediate dir under
  temp). It is locational, not a code warning, and does not fail `/WX`.
- Do not use `cmake --build --parallel` (project rule); MSBuild picks its own
  parallelism.
- Concurrent builds by other streams share `build\vs`; a first `--build` can hit
  transient `C1083 ... Permission denied` on `.obj` files. Re-run the same build
  before treating it as a real failure.

## Wave 9b (W42) — measured tables, list filters, grouping, outline sort

Done and green on `build\vs-w42` (MSVC 19.44, Release, `/W4 /permissive- /utf-8 /WX`):

- **`core/view/table_registry.h/.cpp`** — the real Task/Resource table set, decoded
  from PJINTL.DLL **RT_RCDATA 330 (15 Task tables)** and **311 (9 Resource
  tables)**. The resource records are `(PjField id, width, align code)`; the
  resource's six-byte block header is `01 01 <count> <flag> ff 00`, each column is
  8 bytes, custom captions follow the zero terminator. Export tables (84 Task /
  24 Resource columns) are deliberately excluded: they are data-export layouts,
  not sheets.
  - **Measured**: column field ids, order, widths, alignment codes (0 left,
    1 right, 2 centre). Entry = 8 columns including Indicators (the old inferred
    table omitted it): ID 6R, Indicators 6L, Task Name 24L, Duration 9R, Start
    12R, Finish 12R, Predecessors 14L, Resource Names 17L. Schedule and Tracking
    are pinned in `tests/view_model_tests.cpp` too.
  - **Decision / inference**: the resource emits custom captions as a string list
    without a per-caption field id; the caption→field assignment in
    `table_registry.cpp` is by field meaning ("Task Name"→Name, "Start Var."→
    StartVariance, …). The captions and fields are measured, the pairing is
    inferred. Unnamed PjField ids (0x0b4000cd Indicators, 0x0b40002f
    Predecessors, 0x0b400030 Successors, 0x0b400031 Resource Names,
    0x0b400056 UniqueID, and the resource counterparts) get their title from
    `analysis/typelib.tsv`'s PjField enum name — also a decision.
  - Alignment code 2 appears exactly once in 330 (Tracking → PhysicalPercentComplete)
    and is mapped to `ColumnAlign::Center`; treated as a decision until a second
    observation.
  - The old `defaults::inferred_entry()` / `builtin_tables()` API is **unchanged**
    (still 4 tables, Entry still 7 columns); the registry is additive so the
    macOS shell keeps compiling. `tests/view_model_tests.cpp` pins that.
  - Every column is checked against `core::model::find_field_by_id` and its record
    type; every column title is non-empty and not a hex id.

- **Filter breadth** (`filter.h/.cpp`): `FilterOp::In/NotIn` with
  `FilterCriterion::values`, `IsNull/IsNotNull`, `distinct_values()` (sorted,
  nulls omitted), `value_in_list()`, `auto_filter_criterion()` and
  `auto_filter()`. JSON uses `"values": [...]` for in/not_in (a `"value"` array
  is accepted too). Membership uses the same coercion as Equals; null only
  matches a null member; an empty list matches nothing.

- **Grouping breadth** (`group.h/.cpp`): `build_groups()` overload taking a row
  order (sort first, group second); `GroupBucket::row_count/total_row_count/
  summary_line` (`"3 tasks"`, singular/plural by record type); nested buckets
  report `row_count == 0` and the sum of their children; `group_display_lines()`
  flattens a bucket tree into heading/summary/row lines. **Decision,
  unmeasured**: the summary line is emitted *after* the heading and *before* the
  rows.

- **Sort breadth** (`sort.h/.cpp`): `SortOptions { keep_outline; outline_level_field }`
  and an `apply_sort(..., options)` overload. `keep_outline` builds the
  OutlineLevel tree, sorts siblings (including top-level parents) with the same
  stable multi-key comparator and emits a pre-order walk, so children stay under
  their parent. Grouping over that order is a flat partition, which is the
  documented "grouping flattens" rule. The flat sort stays stable and keeps file
  order for equal keys.

Verification (last run 2026-10-22, `build\vs-w42`):

```powershell
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\vs-w42 --config Release --target view_model_tests'
.\build\vs-w42\core\view\Release\view_model_tests.exe
# -> "view_model_tests: 1339 checks passed"   (wave-8 baseline was 270; +1069)
$ctest='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
& $ctest --test-dir build\vs-w42 -C Release --output-on-failure -R "view|gantt"
# -> 2/2 passed (view_model, gantt)
```

Open after W42: the Export tables, a second observation for alignment code 2,
the caption→field pairing (coordinator oracle could confirm through
`TaskTables(...).TableFields` per `spec/view-model.md` §9.3), group summary
placement, and whether Project sorts top-level parents in outline sort the same
way. No UI was added.

