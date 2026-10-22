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

## Wave 10 (W43) — resource table builder + time-phased usage

Owned paths this wave: `core/usage/**`, `core/view/resource_tables.*`,
`core/view/CMakeLists.txt`, `tests/usage_tests.cpp`,
`tests/resource_view_tests.cpp`, this file, `docs/handoff/w43.md`. No other
stream file was edited and no existing `core/view`/`core/sheet` API changed.

| path | what |
|---|---|
| `core/usage/usage.h` | `Period{day,week,month}`, `PeriodSpan`, `enumerate_periods` (day numbers and date32 overloads), `AssignmentInput`, `UsageInput`, `UsageRow`, `UsageTable`, `AssignmentUsage`, `UsageProjection`, `project_usage`, `render_usage_table` |
| `core/usage/usage.cpp` | the flat spread (cumulative rounding over the calendar's working ticks), period enumeration, table projections, name-free rendering |
| `core/usage/usage_mpp.h` | header-only `usage_input_from_mpp(mpp::Document)`: tasks/resources uids and assignment uid/task/resource/start/finish/work; keeps `core_usage` free of a link dependency on `core_mpp` |
| `core/usage/CMakeLists.txt` | `core_usage`, ctest `usage`, standalone branch (adds sched/mpp/cal with tests off) |
| `core/view/resource_tables.h/.cpp` | neutral `ResourceRecord` + `build_resource_record_set`, resource field-id constants, `resource_field_kind`, `find_resource_table`, `resource_table_definitions`, `resource_tables_resolve` |
| `tests/usage_tests.cpp` | 164 checks with corpus, **149** with `MPP_NO_CORPUS=1` |
| `tests/resource_view_tests.cpp` | **386 checks**, no corpus |

### What is verified (MSVC 19.44, Release, `/W4 /permissive- /utf-8 /WX`)

```powershell
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake -S . -B build\vs-w43 -G "Visual Studio 17 2022" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON'
cmd /c '"C:\...\vcvars64.bat" >nul && cmake --build build\vs-w43 --config Release --target usage_tests resource_view_tests'
.\build\vs-w43\core\usage\Release\usage_tests.exe
# -> CORPUS: project_a: 303 day periods, 67 assignments, 22 resources, 83 tasks,
#            322080 work minutes (task rows 322080, resource rows 0)
# -> usage_tests: 164 checks passed
.\build\vs-w43\core\view\Release\resource_view_tests.exe
# -> resource_view_tests: 386 checks passed
$env:MPP_NO_CORPUS='1'; .\build\vs-w43\core\usage\Release\usage_tests.exe
# -> SKIP: PROJECT_A usage checks (...)   usage_tests: 149 checks passed
cmd /c '"C:\...\vcvars64.bat" >nul && ctest --test-dir build\vs-w43 -C Release --output-on-failure -R "usage|resource|view"'
# -> 3/3 passed (21 view_model, 22 resource_view, 35 usage); view_model 1339
```

Synthetic coverage: period enumeration Friday→Monday (4 day rows, weekend
present), Sunday-start weeks (clipped first/last), civil months (Feb 28 in
2026), empty/reversed/NA ranges; flat spread cells sum exactly to Work
(full days, partial day, Work smaller than the span); a Friday→Monday
assignment puts 480/0/0/480 and a Wednesday calendar holiday puts 480/480/0/
480/480; task-usage equals the sum of its assignments; resource-usage equals
the sum of its assignments (one resource, two tasks/assignments, plus a
resource with no assignments); empty project; task/resource rows with no
assignments; unplaceable Work (NA dates, finish == start) stays in the totals
and is counted; the `mpp::Document` bridge and the null-resource mapping.

Resource view: builder schema order ID/Name/Type/Initials/MaxUnits, kinds
Integer/Text/Integer/Text/Number, MaxUnits in percent, Type 0/1/2
Work/Material/Cost, rows keep uid identity, empty input keeps the schema; nine
resource tables (86 columns) all resolve to Resource fields through
`core::model::find_field_by_id`, Entry column data pinned (ID 6 R, Name 20 L
"Resource Name", Type 11 L, MaxUnits 10 R "Max. Units"), `find_resource_table`
by id and by name, task tables untouched (15).

### Measured vs inferred

- **Measured (inherited)**: the storage layout that motivates this module —
  assignment totals at `TBkndAssn/FixedData` +12 Units, +20 Work minutes x1000,
  +52/+56/+60 Start/Finish/Resume; the contour step list is VarMeta
  `0x0f400031`/`0x0f400032`; there is **no** time-phased stream
  (`docs/analysis/oracle/w10-oracle.md` §3). Field ids/kinds follow the MSPDI
  resource importer and the descriptor table (W42's RT_RCDATA 311).
- **Inference/decision (ours, no oracle)**: week periods start Sunday; months
  are civil months; minutes are integers with cumulative rounding; flat spread
  is proportional to the calendar's working ticks; unplaceable Work stays in
  totals without buckets; resource `Type` uses Project's PjResourceTypes order
  (0 Work / 1 Material / 2 Cost), not MSPDI's export spelling.
- **Not decoded (optional, open)**: the `0x0f400031` contour step list. Flat is
  exact for a contour-less assignment and is the documented fallback.

### Open / next

1. **PROJECT_A cannot exercise resource usage.** All 67 live PROJECT_A assignment rows
   store `kNullResourceUid` at +8 (pinned by `tests/mpp_reader_tests.cpp`), so
   the PROJECT_A resource-usage table is 22 present-but-zero rows and the test asserts
   that measured shape. A corpus file with real +8 resource ids is the missing
   case; `corpus/probe/assign1.mpp` would do it, but the probe fixtures are not
   in every checkout (they are 1-byte stubs in this tree), so the resource path
   is covered with a hand-built `mpp::Document`.
2. **Contour shaping.** Decode assignment VarMeta `0x0f400031` (step list) to
   reproduce back-loaded/front-loaded contours; mark inference until an oracle
   pairs the step bytes with TimeScaleData buckets.
3. **Per-resource calendars.** `core::sched` models one calendar; resource-usage
   respects the project calendar only. A resource calendar/units cap is not
   applied (`max_units` is exposed but not used to clip bucket work).
4. **Week boundary oracle.** One `TimeScaleData`/timescale observation would
   turn Sunday-start weeks from a decision into a measurement.

### Traps hit

- `core/mppwrite/CMakeLists.txt` briefly named `tests/roundtrip_add_delete.cpp`
  before W45 created it; the root configure failed with "Cannot find source
  file". Retrying the identical configure once the file landed was the fix —
  do not edit another stream's file.
- MSB6003 on a `.tlog` (Dropbox/AV churn) appeared once; the identical build
  retried cleanly.
- `ctest` and `cmake` are not on a bare `pwsh` PATH; run them after `vcvars64`
  (or full paths).
- The test binary lands under `build\vs-w43\core\usage\Release\` and
  `build\vs-w43\core\view\Release\`, not `build\vs-w43\Release\`.
- Do not print client names: this module only ever holds uids/dates/minutes, and
  `render_usage_table` is name-free by construction.

