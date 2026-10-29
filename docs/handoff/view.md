# Handoff: view / interchange stream

_2026-10-22. One session, no workers (delegation depth was 0 in this session, so
the brief's fallback "do the work yourself in order" applied). No git commands
were run; files are on disk for the coordinator to commit._

## (a) What is done

All three deliverables are implemented, built and tested with MSVC.

### 1. View model — `core/view/`

| File | Contents |
|---|---|
| `value.h/.cpp` | `Value` (Null/Bool/Integer/Number/Text/Duration/Date); Duration is 6-second ticks (4800/day); civil dates; ISO-8601 duration parse/format (`PT16H0M0S`, not `P2D`); `PT#H#M#S` output; comparison and coercion |
| `record_set.h/.cpp` | columnar `RecordSet` keyed by PjField id, UniqueID separate; add/set/get; null for absent fields |
| `json.h/.cpp` | dependency-free JSON: ordered objects, escapes/surrogates, duplicate-key and trailing-comma errors, line numbers, pretty/compact writer |
| `field_ref.h/.cpp` | field name ⇄ id via the generated descriptor table; unnamed fields use `field_id` and hex titles |
| `table.h/.cpp` | `TableDefinition` = ordered `Column{field,title,width,align}`; JSON; errors on unknown/duplicate fields |
| `filter.h/.cpp` | criteria tree `and`/`or`/`not` over comparisons; 12 operators; JSON; evaluation against a RecordSet |
| `group.h/.cpp` | key tree; intervals none/day/week/month/quarter/year/percent; buckets with labels and children; JSON |
| `sort.h/.cpp` | stable multi-key sort; nulls last; JSON |
| `view_set.h/.cpp` | `{version, tables, filters, groups, sorts}` JSON container; lookups; version check |
| `defaults.h/.cpp` | `recovered-column-map` (evidenced) + Entry/Summary/Cost (explicitly inferred) |

### 2. MSPDI import — `core/io/`

| File | Contents |
|---|---|
| `xml.h/.cpp` | XML reader: declaration/comments/CDATA/entities/prefix stripping; line-numbered rejections |
| `mspdi.h/.cpp` | `<Project>` → `MspdiDocument` (project header, Task/Resource/Assignment RecordSets, links, assignment refs, warnings). Curated mapping of ~66 Task, 17 Resource, 20 Assignment elements. Bad values warn, bad documents throw |
| `mpp_bridge.h` | optional bridge from `mpp::Document` (reader v0) into the same RecordSet; `__has_include` guarded |

### 3. CSV export — `core/io/csv.h/.cpp`

Table column order applied to a RecordSet, RFC 4180 quoting, CRLF/LF,
ISO/days/ticks durations, bool 1/0 or Yes/No, filter and explicit-order
overloads, streaming writer.

### Specs / analysis / memory

- `spec/view-model.md` — model, JSON schemas, semantics, default-table evidence,
  oracle recipe, verification, limits.
- `spec/mspdi.md` — mapped subset, conversions, diagnostics, reader contract.
- `docs/analysis/view/README.md` — the 41-row catalogue analysis and reproduction.
- `memory/view.md` — state, evidence anchors, ranked open items, traps.

## (b) What is not done

1. **Default table layouts are not evidenced.** `analysis/column_field_map.tsv`
   is a 41-entry Task column catalogue, not the Entry sheet (order starts Name,
   ID; contains Application/Parent/Active; 10 of 41 named; ordinal 19 missing).
   `recovered-column-map` exposes it verbatim. Entry/Summary/Cost are
   `source="inferred ... NOT oracle-verified"`. **No default-table layout exists
   anywhere in the repo.** The oracle recipe to fill this is in
   `spec/view-model.md` §9.3.
2. **Filter/group/sort semantics are our choices**, not oracle-verified: case
   sensitivity, null ordering, coercion, label forms, fiscal/custom intervals.
3. **MSPDI is a subset**: no calendars, ExtendedAttribute/outline codes,
   recurring-task structure, timephased data, project header field ids, derived
   `Predecessors`/`ResourceNames`, no writer. Unmapped elements warn once.
4. **The reader bridge covers only the reader's current eight fields.** The
   reader must grow (see (d)).
5. **No UI.** Widths/alignment are stored data; no rendering, no points/DPI
   conversion.
6. **Top-level CMake is not wired** (W8 owns it). See (c) action 1.

## (c) Next three concrete actions

1. **Wire the build.** In the top-level `CMakeLists.txt` (owned by W8), change
   `foreach(stream_subdir mpp sched)` to `foreach(stream_subdir mpp sched view io)`.
   `view` builds before `io`; both link the existing `core_model` target. If W8
   prefers a different order or target names, `core/view/CMakeLists.txt` and
   `core/io/CMakeLists.txt` are self-contained and need no edits. After wiring,
   `ctest` gains `view_model`, `io_mspdi`, `io_csv`.
2. **Capture the real default tables via the oracle** (coordinator-only) and
   replace the inferred `Entry`/`Summary`/`Cost` with fixtures in
   `core/view/defaults.cpp` (or load them from `tests/fixtures/tables/*.json`).
3. **Agree the reader contract.** Either have the reader emit
   `core::view::RecordSet` directly, or add per-field values by PjField id +
   OutlineLevel + Summary/Milestone + structured links/assignments to
   `mpp::Document`, and replace `core/io/mpp_bridge.h` with the real conversion.

## (d) What this stream needs from the reader's model

Current `mpp::Document` (`core/mpp/reader.h`) has `ProjectRow` + `Task{row,
row_id, uid, name, duration_ticks, priority, percent_complete, start, finish,
name_recovered}`. The bridge maps name/duration/priority/percent/start/finish/uid
to the right PjField ids and preserves civil time.

For the views to reach parity, the reader (or its successor) needs to supply:

- **values by PjField id** for arbitrary Task/Resource/Assignment fields (the
  RecordSet schema is the natural target);
- **OutlineLevel** and **OutlineNumber** for outline-aware tables/groups;
- **Summary/Milestone/Critical/Active** flags — filters and grouping depend on
  them;
- **links** as structured records (predecessor UID, successor UID, type, lag) and
  **assignments** with TaskUID/ResourceUID joins;
- **project-level fields** once the analysis names the nine Project descriptor
  rows (`0x07400000`–`0x07400008`), which currently have no `property_name`.

Until then, MSPDI import is the richer source and the reader can be bridged one
direction at a time.

## Verification commands (re-runnable)

```powershell
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$cmake='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$src='core\io'
$build="$env:TEMP\msproj-io-wx"

cmd /c "call `"$vc`" >nul && `"$cmake`" -S `"$src`" -B `"$build`" -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && `"$cmake`" --build `"$build`" --config Debug"
& $ctest --test-dir $build -C Debug --output-on-failure
```

Last result (2026-10-22, MSVC 19.44, `/W4 /permissive- /utf-8 /WX`): build
clean; 3/3 tests passed; `view_model_tests` 270 checks, `io_mspdi_tests` 116
checks (including the reader bridge), `io_csv_tests` 25 checks.

Configure `core/view` alone with `cmake -S core/view -B <dir> -G "Visual Studio
17 2022"` if only the view model is wanted.

## (e) Traps

- **Do not re-export `record_type_name`** from `core::view` (ADL collision with
  `core::model::record_type_name`); qualify calls instead.
- Duration formats: MSPDI writes `PT16H0M0S`, not `P2D`; `P2D` parses as 24-hour
  calendar days (28,800 ticks), which is *not* two working days.
- `cmake --build --parallel` is forbidden by the project rules; pass no
  parallelism flag.
- Building under `$env:TEMP` produces MSBuild warning MSB8029 (intermediate dir
  under temp); it is locational, not a compiler warning, and `/WX` still passes.
- Dropbox rewrites mtimes; edit tools may report "file changed since read" — read
  again immediately before editing.
- Test binaries resolve sample XML through the `IO_SAMPLE_DIR` compile
  definition; run them from the build tree (ctest does this) or the samples will
  not be found.
