# MSPDI (Project XML) import

_Stream W-view, 2026-10-22. Owner: `core/io/`, `spec/mspdi.md`._

MSPDI is Microsoft Project's published XML interchange format
(`http://schemas.microsoft.com/project`). It is the interchange path that works
before the `.mpp` writer exists, and it lands in the same field-keyed model the
views read. This spec covers what is imported, how values convert, what is
warned about, and the contract with the `.mpp` reader's model.

Status: import and export implemented and tested. Minimal sample XML is committed
under `tests/`; no real export was used (the oracle is coordinator-only), so the
writer's element order is *inferred* from the samples, not validated against MS
Project's importer (§8).

## 1. Files

| File | Role |
|---|---|
| `core/io/xml.h/.cpp` | dependency-free XML reader (declaration, comments, CDATA, entities, prefixes, self-closing) |
| `core/io/mspdi.h/.cpp` | import into `MspdiDocument` with three RecordSets, links, assignment refs, warnings |
| `core/io/mspdi_writer.h/.cpp` | export: `core::io::mspdi::write` / `write_file` from `MspdiDocument` back to MSPDI (§8) |
| `core/io/mpp_bridge.h` | optional adapter from `mpp::Document` (reader v0) to the same RecordSet |
| `tests/io_sample_minimal.xml` | one task, project header |
| `tests/io_sample_tasks.xml` | summary/child/milestone, link, CDATA/entities, custom fields, one unsupported element |
| `tests/io_sample_full.xml` | namespaced document, resources, assignments, unsupported `EmailAddress` |
| `tests/io_mspdi_tests.cpp` | parser, import, warnings, errors, reader bridge |
| `tests/io_mspdi_writer_tests.cpp` | round-trip of the three samples, escaping, UTF-8, empty values, durations, dates, root/namespace, per-field coverage |

## 2. Result model

```cpp
struct MspdiDocument {
    MspdiProject project;
    RecordSet tasks;        // RecordType::Task
    RecordSet resources;    // RecordType::Resource
    RecordSet assignments;  // RecordType::Assignment
    std::vector<PredecessorLink> links;
    std::vector<MspdiAssignmentRef> assignment_refs;  // uid, task_uid, resource_uid
    std::vector<std::string> warnings;
};
```

This is the same `core::view::RecordSet` used by tables, filters, groups, sorts,
CSV export and the reader bridge. A table can name a Task field and read the
import without knowing the source was XML.

The **project header** is a struct, not a RecordSet: the descriptor table's nine
Project rows (`0x07400000`–`0x07400008`) have no recovered property names in
`analysis/field_metadata_all.tsv`, so project-level values cannot be field-mapped
without inventing ids. `MspdiProject` holds `name`, `title`, `start`, `finish`,
`schedule_from_start`, `minutes_per_day/week`, `days_per_month`,
`calendar_uid`, `default_start_time`, `default_finish_time`. Closing this gap
needs named Project fields from the analysis or an oracle capture (see §10).

`Assignment.TaskUID`/`ResourceUID` have the same problem: they are not named
Assignment descriptor fields. They are preserved in `assignment_refs` (same order
as assignment rows) so usage views can join without guessing an id.

## 3. XML subset

The parser accepts one document with:

- UTF-8 BOM, XML declaration, processing instructions, comments, DOCTYPE
  (skipped, not resolved; external entities are therefore not expanded);
- CDATA;
- the five predefined entities and numeric character references (`&#65;`,
  `&#x41;`); an unknown entity is an error;
- namespace prefixes, which are **stripped** (`mspdi:Task` ≡ `Task`);
- self-closing elements and quoted attributes.

It rejects mismatched end tags, duplicate attributes, unterminated constructs,
unknown entities, multiple roots and trailing content, and reports a line
number. It is not a schema validator.

The tree keeps direct text separate from child elements, so a mixed-content
element cannot smuggle text into a child. `text_of()` trims direct text.

A full DOM is built. That is fine for the project sizes in `corpus/` (85 and 84
tasks) and for test files; it is not a streaming parser for 100 MB exports.

## 4. Element → field mapping

Field names are resolved through `core/model/field_table.h`
(`analysis/field_metadata_all.tsv`, joined with `field_definitive_map.tsv`). A
name that does not resolve becomes **one** warning and that element is skipped;
nothing is written to a guessed id. That is why the map below is data, not a
list of hardcoded hex ids: regenerating the descriptor table cannot silently
mis-point a mapping.

`UID` is special: it becomes `RecordSet` UniqueID, not a field.

### Task (`<Tasks><Task>`)

| Kind | MSPDI elements |
|---|---|
| Integer | `ID`, `Type`, `OutlineLevel`, `Priority`, `PercentComplete`, `PercentWorkComplete`, `FixedCostAccrual` |
| Text | `Name`, `WBS`, `OutlineNumber`, `Notes`, `Hyperlink`, `HyperlinkAddress`, `Text1` |
| Date | `Start`, `Finish`, `ActualStart`, `ActualFinish`, `ConstraintDate`, `Deadline`, `EarlyStart`, `EarlyFinish`, `LateStart`, `LateFinish`, `BaselineStart`, `BaselineFinish`, `Resume`, `Date1` |
| Duration | `Duration`, `RemainingDuration`, `ActualDuration`, `BaselineDuration`, `LevelingDelay`, `TotalSlack`, `FreeSlack`, `Work`, `RemainingWork`, `ActualWork`, `BaselineWork`, `OvertimeWork`, `ActualOvertimeWork`, `RemainingOvertimeWork`, `RegularWork` |
| Number | `Cost`, `FixedCost`, `ActualCost`, `RemainingCost`, `BaselineCost`, `OvertimeCost`, `ActualOvertimeCost`, `RemainingOvertimeCost`, `Cost1`, `Number1` |
| Bool | `Milestone`, `Summary`, `Critical`, `Estimated`, `EffortDriven`, `IgnoreResourceCalendar`, `LevelingCanSplit`, `Active`, `LinkedFields`, `Flag1` |

`PredecessorLink` is preserved structurally: `PredecessorUID` (→ predecessor),
the containing task UID (→ successor), `Type`, `LinkLag`, `LagFormat`. **Measured
2026-10-22 (W27) against Project's own export:** PROJECT_A's `LinkLag` is a **decimal
count of tenths of a minute** (`0`, `96000`, `139200`, `240000`), not ISO; the
importer now accepts both spellings (tenths of a minute == the reader's
6-second tick) and the writer still emits ISO (pinned by its tests). `Type` is
stored raw (0=FF 1=FS 2=SF 3=SS, measured W3-O3); `LagFormat` is 7 for working
days and 8 for elapsed days (lagff-probe). **`ConstraintType` was missing from
the importer until W27** (only `ConstraintDate` was read); it is now imported as
an Integer Task field. Every other Task field the cross-check compares
(Start/Finish/Duration/Milestone/Summary/ConstraintDate/Deadline/ActualStart/
ActualFinish) agrees on PROJECT_A: 830 comparisons, 0 mismatches.

### Resource (`<Resources><Resource>`)

| Kind | MSPDI elements |
|---|---|
| Integer | `ID`, `Type`, `AccrueAt` |
| Text | `Name`, `Initials`, `Group`, `Notes`, `MaterialLabel`, `Text1` |
| Number | `MaxUnits`, `StandardRate`, `OvertimeRate`, `CostPerUse`, `Cost1`, `Number1` |
| Bool | `Flag1` |
| Date | `Date1` |

### Assignment (`<Assignments><Assignment>`)

| Kind | MSPDI elements |
|---|---|
| Integer | `TaskID`, `ResourceID`, `PercentWorkComplete` |
| Text | `TaskName`, `ResourceName`, `Notes`, `Text1` |
| Number | `Units`, `Cost`, `ActualCost`, `RemainingCost`, `Cost1`, `Number1` |
| Duration | `Work`, `ActualWork`, `RemainingWork` |
| Date | `Start`, `Finish`, `ActualStart`, `ActualFinish` |

`TaskUID`, `ResourceUID` and assignment `UID` go to `assignment_refs` / row uid as
described in §2.

## 5. Value conversion

| XML text | Field kind | Conversion |
|---|---|---|
| `PT16H0M0S`, `P2D` | Duration | ISO-8601 → 6-second ticks. `P2D` is 48 h = 28,800 ticks (calendar days), not two 8-hour working days |
| `2024-01-01T08:00:00` | Date | civil date/time; `Z` and `±HH:MM` are accepted and ignored (§value.h: no timezone conversion) |
| `1`/`0`/`true`/`false` | Bool | bool |
| `0x…` not accepted | Integer | decimal only, `int64` range |
| `1250.5` | Number | shortest double parse; no locale dependence |
| anything | Text | UTF-8, entities already decoded |

An empty element is `Value::Null`. A malformed value (e.g. `PercentComplete=x`)
is a warning and that value is skipped; it does not abort the import. A
malformed document (bad XML, root not `<Project>`) throws `MspdiError` with a
line number.

## 6. Diagnostics

Warnings are in document order and **deduplicated per element name** for
unsupported elements, so a file with 1,000 tasks does not produce 1,000 copies:

- `unsupported Task element <DurationFormat> (no named Task field; ignored)`
- `unsupported Resource element <EmailAddress> ...`
- `Task[UID=2]/PercentComplete: expected an integer, got "x"`
- `Task: no <UID>; using 1`
- `Assignment[UID=1]: no <TaskUID>`

Boilerplate/derived elements are listed as deliberately ignored in
`core/io/mspdi.cpp` (`IsNull`, `CreateDate`, `Manual*`, `IsSubproject*`,
`ExternalTask*`, `Subproject*`, variance/slack fields). They do not warn.
Everything else that is unmapped does, because "unmapped" may mean "relevant".

## 7. Contract with the `.mpp` reader

The reader (`core/mpp/reader.h`, other stream) currently returns
`mpp::Document` with eight fixed Task members. `core/io/mpp_bridge.h` converts it
to the same RecordSet:

| mpp::Task | PjField |
|---|---|
| `name` | `Task.Name` (0x0b40000e) |
| `duration_ticks` | `Task.Duration` (0x0b40001d) |
| `priority` | `Task.Priority` (0x0b400019) |
| `percent_complete` | `Task.PercentComplete` (0x0b400020) |
| `start` | `Task.Start` (0x0b400023) |
| `finish` | `Task.Finish` (0x0b400024) |
| `uid` | RecordSet UniqueID |

Packed dates convert as `(day << 16) | seconds/6` with day 0 = 1983-12-31,
preserving civil time. Field ids are resolved by name, not hardcoded.

The include is guarded by `__has_include("reader.h")` and CMake only adds
`core/mpp` to the include path when that file exists, so `core/io` builds without
the reader stream. The test constructs an `mpp::Document` in memory and checks
the conversion; it links no reader symbol.

**What the reader needs to grow into.** The bridge is intentionally a stopgap.
For views to reach parity, the reader should emit one of:

1. `core::view::RecordSet` directly (preferred: one model, no adapter), or
2. the same eight members plus per-field values keyed by PjField id, outline
   level, and structured links/assignments.

The minimum additions are: arbitrary field values by id, `OutlineLevel` (for
outline-aware tables/groups), `Summary`/`Milestone` flags (filters use them), and
links/assignments as structured records. The MSPDI importer already shows the
shape those can take.

## 8. Writer (export)

_Added 2026-10-22. Status: implemented and tested; **not** run against MS
Project's importer (the oracle is coordinator-only), so everything about what
Project accepts is marked inferred._

### 8.1 API

```cpp
namespace core::io::mspdi {
std::string write(const MspdiDocument& document);
void write_file(const MspdiDocument& document, const std::string& path);
}
```

`core/io/mspdi_writer.h/.cpp`. `write` is the whole export path; `write_file`
writes exactly `write()`'s bytes (binary, LF) and throws `MspdiError` when the
file cannot be opened or written — the same error type the import path throws.
The importer keeps its public name (`core::io::import_mspdi`); the round-trip
pair is `import_mspdi` ⇄ `core::io::mspdi::write`.

What goes out: the project header, then `<Tasks>`, `<Resources>`,
`<Assignments>` in that order. Each record is `<UID>`, `<ID>`, the mapped
elements, and `<PredecessorLink>` children under the successor task (last child).
The writer covers **all** elements the importer maps — 63 Task, 17 Resource, 20
Assignment — resolved by name through the same descriptor table
(`field_id_by_name`), never by hardcoded id. Only mapped elements can be
written: anything the importer dropped (`DurationFormat`, `EmailAddress`,
calendars, timephased data, ...) is not in the model. Consequently writer output
re-imports with **zero warnings**, which the tests assert for all three samples.

### 8.2 Element order — inferred, not verified

The only MSPDI in this repository is the three committed hand-written samples;
there is no local schema file and no real Project export. The order below keeps
what the samples agree on and fills the rest from the importer's curated table
order:

- `Task`: `UID`, `ID`, `Name`, `Type`, `WBS`, `OutlineNumber`, `OutlineLevel`,
  `Priority`, `Summary`, `Milestone`, `Duration`, `RemainingDuration`,
  `ActualDuration`, `BaselineDuration`, `Start`, `Finish`, `ActualStart`,
  `ActualFinish`, `Resume`, `ConstraintDate`, `Deadline`, the early/late and
  baseline dates, `Date1`, `PercentComplete`, `PercentWorkComplete`, the flags,
  the work fields, the cost fields, `FixedCostAccrual`, `LevelingDelay`,
  `TotalSlack`, `FreeSlack`, `Notes`, `Hyperlink`, `HyperlinkAddress`, `Text1`,
  then `PredecessorLink`.
- `Resource`: `UID`, `ID`, `Name`, `Type`, `Initials`, `Group`, `MaxUnits`,
  `StandardRate`, `OvertimeRate`, `CostPerUse`, `MaterialLabel`, `AccrueAt`,
  flags/custom fields, `Notes`.
- `Assignment`: `UID`, `TaskUID`, `ResourceUID`, `TaskID`, `ResourceID`,
  `TaskName`, `ResourceName`, `Units`, the work/cost fields, `Start`/`Finish`,
  `ActualStart`/`ActualFinish`, `PercentWorkComplete`, custom fields, `Notes`.
- `PredecessorLink`: `PredecessorUID`, `Type`, `CrossProject` (constant 0, as
  the committed sample writes it), `LinkLag`, `LagFormat`.

The samples contradict each other on two points: `io_sample_minimal.xml` puts
`PercentComplete` before `Milestone`/`Summary`/`Critical`, while
`io_sample_tasks.xml` puts `Summary` before `Duration`. The writer follows the
richer `tasks` file and then the importer's table order. **No evidence in this
repository proves this order is what MS Project's importer requires**; Project's
schema is a sequence, and a real `FileOpen` of a writer-produced file (oracle,
coordinator-only) is the test that would settle it. Recorded as an open item.

Verified mechanically: the tests parse writer output and assert `UID` is the
first child of a task, `ID` the second, and `PredecessorLink` the last child of
its successor (the shape the samples have).

### 8.3 Value rules — verified by round-trip

| Value | Written as |
|---|---|
| `Null` | no element at all |
| empty Text | no element at all |
| Bool | `1` / `0` |
| Integer | decimal |
| Number | shortest round-trip decimal; NaN/inf omitted |
| Text | UTF-8 verbatim, escaped `& < > " '` |
| Duration | ISO `PT#H#M#S` from 6-second ticks (`4800` ticks = `PT8H0M0S` = one 480-minute day); zero is `PT0H0M0S`; negative is `-PT…` |
| Date | `YYYY-MM-DDTHH:MM:SS`, seconds always present, no timezone; invalid dates and years outside 1..9999 omitted |

`<x/>`, `<x></x>` and an absent element all import as `Value::Null`, so
"empty value ⇒ no element" is the consistent spelling and the writer never emits
`<x/>` for a value. A zero integer, `false` flag or zero duration is a *value*
and is written. Dates are formatted by the writer
(`%04d-%02d-%02dT%02d:%02d:%02d`) because `core::view::format_date` is the
display form (`"YYYY-MM-DD HH:MM"`, space, no seconds); durations reuse
`core::view::format_duration`, whose ISO form is already `PT#H#M#S`.

Header elements are written only when the model has them:
`Name`/`Title` when non-empty, `StartDate`/`FinishDate` when Date,
`ScheduleFromStart` only when `has_schedule_from_start` (writing it always would
change that flag on re-import), the three minute/day counts and `CalendarUID`
when `>= 0` (the `-1` sentinel means absent), `DefaultStartTime`/
`DefaultFinishTime` when non-empty. `<Tasks>`, `<Resources>` and `<Assignments>`
are always present; an empty one is `<Tasks/>`.

### 8.4 Inferred choices that are not round-trip-visible

- **`<ID>` fallback.** `ID` is written from the model; when a row has no ID
  value the writer writes the row's 1-based ordinal. Project's exports always
  carry `ID`, and ID is a display sequence number, not project data. Inference:
  no oracle capture shows whether Project requires it or recomputes it.
- **`CrossProject` 0** on every link: in the committed sample, and the importer
  ignores it. A link in this model is by definition in this project.
- **`ScheduleFromStart` omitted** when the model never recorded it (see above);
  a real export always has it, so a writer-produced file for a project imported
  from such a document lacks it. Project may default it on import — unverified.
- **Dangling records are dropped**: an assignment row with no
  `MspdiAssignmentRef` cannot name a task and a resource, and a link whose
  successor UID is not a task row has nowhere to go. Both are skipped rather
  than written as an invalid reference. Every other mapped, non-null value is
  written.

### 8.5 What the round-trip proves

`parse → write → parse` over `io_sample_minimal.xml`, `io_sample_tasks.xml` and
`io_sample_full.xml`, comparing project header, all three RecordSets (schema,
UIDs, every cell), links, and assignment refs. Plus a per-field probe: for every
field in each imported schema, the test finds a value kind that survives
write→parse with an exactly equal `Value`. That coverage test is what catches a
writer element list that has drifted from the importer's.

`io_mspdi_writer_tests: 852 checks passed` (Release, MSVC 19.44,
`/W4 /permissive- /utf-8 /WX`, command in §9).

## 9. Verification

What was actually run for the writer (2026-10-22, MSVC 19.44,
`/W4 /permissive- /utf-8 /WX`) is the **top-level** build, which now includes
`core/io`:

```powershell
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$cmake='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest=Join-Path (Split-Path $cmake) 'ctest.exe'
cmd /c "call `"$vc`" >nul && `"$cmake`" -S . -B build\vs -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && `"$cmake`" --build build\vs --config Release"   # never --parallel
& $ctest --test-dir build\vs -C Release --output-on-failure
```

Result: clean build (no warnings under `/WX`); `ctest` **14/14 passed**. That is
the 12 tests that existed before this session, plus `io_mspdi_writer` from this
stream, plus `core_sched_cpm` added concurrently by another stream. Run directly,
the two io binaries report `io_mspdi_tests: 116 checks passed` and
`io_mspdi_writer_tests: 852 checks passed`. The executables resolve sample files
via the `IO_SAMPLE_DIR` compile definition, so they must be run from the build
tree or with that path.

The historical standalone configure of `core/io` (previous session, Debug) uses
the same source set and is the faster loop while editing only this directory. It
was **not** re-run this session, and note that the confined test process cannot
write to `$env:TEMP`, so a build tree outside the workspace may not work for the
writer's file test (that test writes under `tests/` for this reason):

```powershell
$src='core\io'
cmd /c "call `"$vc`" >nul && `"$cmake`" -S `"$src`" -B `"$env:TEMP\msproj-io-wx`" -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && `"$cmake`" --build `"$env:TEMP\msproj-io-wx`" --config Debug"
& "$env:TEMP\msproj-io-wx\Debug\io_mspdi_tests.exe"
& "$env:TEMP\msproj-io-wx\Debug\io_mspdi_writer_tests.exe"
```

Coverage: parser success/failure cases; project header; durations/dates/bools/
numbers; summary/milestone/critical; CDATA and entities; custom fields; links;
namespaced documents; resources/assignments/joins; option to skip resources;
warning attribution and dedup; wrong root/missing file/bad value behaviour;
reader bridge date and value conversion; and, for the writer, the three sample
round-trips, escaping, UTF-8, empty values, durations, dates, root/namespace,
per-field coverage, the file helper and the unrepresentable-record rules.

## 10. Limits and open items

- **Not imported**: calendars, extended attributes (custom field definitions),
  outline codes beyond the three named fields, task dependencies as a separate
  table, project-level fields as field ids, baselines beyond the named fields,
  recurring-task structure, subprojects, timephased data. Each shows up as a
  warning unless listed as boilerplate.
- **Derived display fields** `Predecessors` and `ResourceNames` are not
  synthesized from links/assignments. Need the scheduling layer's formatting and
  the oracle's exact text (separator, lag units, resource separator).
- **Type/LagFormat enums** are stored raw. The mapping from MSPDI `Type` to
  FS/SS/FF/SF must be evidenced before use.
- **Timezone**: no conversion; `Z` is ignored. If Project exports local times
  with an offset, the port currently keeps the wall time.
- **Project header field ids** are the known gap; the analysis has no names for
  `0x07400000`–`0x07400008`.
- **Writer element order is unverified** (§8.2). The next evidence step is a
  coordinator-only oracle run: write a sample, `FileOpen` it in Project, and
  re-export; compare the element order and check Project does not reorder or
  reject it. Until then, treat "MS Project accepts this order" as inference.
- **Writer does not restore what the model cannot hold**: calendars (Project
  regenerates a default calendar), `ExtendedAttributes`, outline codes,
  timephased data, project GUID/creation metadata, `Assignment.ID` (the plain
  assignment ID has no named Assignment descriptor field, so it is neither
  imported nor written; re-importing a Project export that has it warns once).
- **Text with leading/trailing whitespace does not round-trip**: the importer
  trims direct text (`text_of`), so the writer's verbatim text comes back
  trimmed. Models produced by `import_mspdi` never have such values, so the
  round-trip guarantee holds for them.
- **Writer normalises `ID`** by writing the row ordinal when the model has none
  (§8.4), so a model that deliberately leaves `ID` null re-imports with an ID.
- **No `.mpp` writer integration yet**: the writer is MSPDI only; the `.mpp`
  writer stream owns the binary path.
