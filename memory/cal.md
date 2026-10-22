# memory/cal.md — W25: 114/TBkndCal base calendars

Status: 2026-10-22 (W25 run). Owned paths: `core/cal/` (cal.h, cal.cpp,
CMakeLists.txt, README.md), `tests/cal_tests.cpp`, this file.  No other stream's
files were touched, `core/sched` was not modified, and no git command was run.

**One line:** the calendar table is decoded, the builder reproduces all three MS
Project dates of the holiday1 fixture with `core/sched` arithmetic, and PROJECT_A's
calendar structure is counted (10 base calendars, 0 work weeks, 1331 dated
exceptions).  `core::cal::load_project_calendar(path[, name])` is ready for
`core/plan` to consume.

## What is done

| deliverable | state |
|---|---|
| decode `114/TBkndCal` (FixedMeta/FixedData/VarMeta/Var2Data) + `114/Props` | done, `core/cal/cal.cpp` |
| work-week day patterns (start/length per weekday) | done (60-byte day blocks, index 0 = Sunday) |
| dated exceptions, non-working **and** special working | done |
| dated work-week overrides ("Calendar > Work Weeks") | decoder implemented, **0 in every file on disk**, exercised only by the synthetic test (layout from mpxj, marked inference) |
| builder -> `core::sched::Calendar` | done: dated changes become per-week `set_work_week` overrides (no `core/sched` API added) |
| prove against the oracle | `tests/cal_tests.cpp`: T1/T2/T3 reproduced exactly |
| Standard calendar from a synthetic probe | done on `corpus/probe/base.mpp` (+ a cheap `w3-o1.mpp` check) |
| PROJECT_A counts | done (below) |
| API for the plan bridge | done |

## Verification (exact, re-runnable from the repo root)

```
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul && cmake -S . -B build\vs-w25 -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w25 --config Release --target cal_tests"
build\vs-w25\core\cal\Release\cal_tests.exe                     # 165 checks passed
$env:CAL_NO_CORPUS='1'; build\vs-w25\core\cal\Release\cal_tests.exe   # 38 checks, 4 SKIP lines
cmd /c "call `"$vc`" >nul && ctest --test-dir build\vs-w25 -C Release -R `"^cal$`""   # 1/1 passed
```

* `/W4 /WX` clean (configured with `-DCORE_WARNINGS_AS_ERRORS=ON`).
* Standalone mode also verified: `cmake -S core\cal -B <dir> -G "Visual Studio 17
  2022" -A x64` + build + run -> 165 checks passed.
* `ctest -R cal` (unanchored) also matches `core_sched_calendar` and
  `plan_recalc`, which are "Not Run" in my tree because those targets were not
  built there; the anchored filter is the meaningful one.

## Verified byte facts (evidence in core/cal/README.md)

1. `114/TBkndCal/FixedMeta` = 16-byte header (`ba ad df fa`, version 4, entry
   count, FixedData size) + **10-byte** entries; the first four describe the
   four 16-byte FixedData header descriptors (offsets 0/16/32/48), the rest are
   rows.  `rows = (FixedData - 64) / 12`; `FixedMeta = 16 + 10*(rows+4)`.
2. `114/TBkndCal/FixedData` row = 12 bytes: `i32 base_uid | i32 resource_uid |
   i32 calendar_uid`.  base_uid <= 0 means "base calendar"; resource_uid -1
   means "not a resource calendar".
3. `114/TBkndCal/VarMeta` = 24-byte header + 12-byte `(uid, offset, key)`;
   key `0x0D400001` = name (UTF-16LE), key `0x0D400008` = calendar data block.
4. Calendar data block: 7 x 60-byte day blocks at 0..419 (index 0 = Sunday),
   `u16` exception count at 420, then 92-byte exception records + padded names,
   then a `u32` work-week header and work-week records.  The decoder consumes
   **all 21** data blocks on disk exactly (`end == length`), which validates the
   framing.
5. 60-byte day block: `+0 u16 day type (1 = inherit)`, `+2 u16 count`,
   `+4 u32 total tenths`, `+8 u16 start[i]` tenths, `+20 u32 length[i]` tenths.
   Arithmetic cross-check: PROJECT_A uid 102 day 2 lengths 1800+2400+600 = 4800 = the
   stored total.
6. **The Standard base calendar has no Var2Data block.**  In `base.mpp` its row
   exists (uid 1) but VarMeta has only its name.  Its week is
   `114/Props` key **37753736** (`0x02401388`, `DEFAULT_CALENDAR_HOURS`,
   420 bytes): day 0 and day 6 custom non-working, days 1..5 custom with
   starts 4800/7800 and lengths 2400/2400 tenths = Mon-Fri 08:00-12:00 +
   13:00-17:00.  `holiday1.mpp` and PROJECT_A carry the identical 420 bytes.  A
   decoder that reads only TBkndCal gets a non-working Standard week.
7. `114/Props` = 16-byte header (record count u16 at +12) + records
   `(u32 len, u32 key, u32 unused, data)`.  Key **37748750** (`0x0240000E`) is
   `DEFAULT_CALENDAR_NAME`, UTF-16LE.

## The holiday1 fixture, row by row

`corpus/probe/holiday1.mpp` (synthetic, built over COM by the coordinator):
2 base calendars + 1 resource calendar, 1 dated exception.

Decoded SiteCal: root calendar (`base_uid = 0`, so unoverridden days inherit the
project default week); day 5 (Friday) custom with one interval 4800..7200
(08:00-12:00, 240 min); the other six days are day type 1 (inherit); one
exception, `start = end = 15635 = 2026-10-21`, interval count 0 (non-working),
`PjExceptionType = 1` (plain range).

Built calendar (`core::sched`):

| day | total ticks | note |
|---|---|---|
| Mon 10-19 | 4800 | 8 h |
| Tue 10-20 | 4800 | 8 h |
| Wed 10-21 | 0 | the dated exception |
| Thu 10-22 | 4800 | 8 h |
| Fri 10-23 | 2400 | 4 h |
| Sat/Sun | 0 | non-working |

One work-week override exists (the week 15632..15638); `work_week_count() == 1`.

The three task dates, `add_work` / `next_start` on that calendar vs
`docs/analysis/oracle/holiday-probe.json`:

| task | computation | result | Project |
|---|---|---|---|
| T1 | `add_work(Mon 10-19 08:00, 9600)` | **Tue 10-20 17:00** | Tue 10-20 17:00 |
| T2 start | `next_start(T1 finish)` | **Thu 10-22 08:00** (Wed skipped) | Thu 10-22 08:00 |
| T2 finish | `add_work(Thu 10-22 08:00, 24000)` | **Thu 10-29 12:00** | Thu 10-29 12:00 |
| T3 | `add_work(Fri 10-23 08:00, 4800)` | **Mon 10-26 12:00** | Mon 10-26 12:00 |

Also pinned: `work_time_between` = 9600 for T1 and 24000 for T2; T3's 4800 =
4 h Friday + 4 h Monday.

## Which calendar the fixture actually proves (correction to the JSON)

`114/Props` `DEFAULT_CALENDAR_NAME` in `holiday1.mpp` is **"Standard"**, not
"SiteCal".  What makes the oracle dates work is the **task** calendar:
`114/TBkndTask/FixedData` row offset **+178** is an `i32` task-calendar uid
(`-1` = NA).  In `holiday1.mpp` it is `3` (= SiteCal's uid) for T1/T2/T3 and
`-1` for the project row; in `base.mpp`, `w3-o1.mpp`, `outline1.mpp`,
`schedin.mpp`, **PROJECT_A (85/85 rows) and PROJECT_B (86/86 rows)** it is `-1`
everywhere.  The coordinator confirmed this independently and is correcting
`docs/analysis/oracle/holiday-probe.json`.

Consequence for the port:

* **Today the bridge is correct**: PROJECT_A/PROJECT_B use the project calendar for every
  task, so `load_project_calendar(path)` (empty name = project default) is what
  `tools/recalc` should call.
* Later, per-task calendars are `find_calendar_by_uid(table, task_uid)` +
  `build_calendar(table, *def)`; the table lookup by uid already exists.  The
  `+178` field belongs to `core/mpp` and is next wave — **do not add it to the
  reader as part of this stream**.
* The PROJECT_A project-default calendar name is client data (it contains a
  company/project name).  It is never printed, never written to a committed
  file.  This file, `core/cal/README.md` and `cal_tests` mention no name from a
  client file; the only names committed anywhere are the synthetic
  "Standard"/"SiteCal"/"Synthetic" of the probes.

## PROJECT_A / PROJECT_B structural counts (counts and dates only, no names)

The project default calendar of PROJECT_A is **name withheld: client data** (its name
carries a company/project identifier).  Only counts and dates appear below.

`corpus/project_a.mpp` (85 task rows, file saved 2026-07-22):

* 33 calendar rows = **10 base calendars** + **23 resource calendars**;
* **10 data blocks** (one per base calendar), **17 custom day records** (16 with
  working time) in those blocks; the other 53 weekday slots inherit;
* **0 dated work-week overrides**;
* **1331 dated exceptions** = 1322 non-working + 9 special working days, all
  `PjExceptionType = 1` (one plain range each), so **0 recurring**;
* exception days **6603..14976** = 2002-01-28 .. 2024-12-31, **614 distinct start
  days**;
* per-calendar exception counts: 1, 173, 179, 316, 382, 23, 23, 234, 0, 0
  (2 of the 10 base calendars have none);
* `metadata_anomalies = 0` (unlike PROJECT_A's `TBkndTask`, this table is consistent);
  `sub_minute_intervals = 0`;
* `load_project_calendar(project_a)` — the exact call the bridge makes — resolves the
  default by name and builds, installing **228 per-week overrides** (234 of the
  1331 exceptions are on that default calendar; exceptions that share a week
  merge into one override).  No fallback is needed for PROJECT_A.

`corpus/project_b.mpp` (86 task rows): the same 33/10/23 shape, 10 data
blocks, 17 custom days, 0 work weeks, **1333** exceptions (1323 non-working, 10
working), days 6603..18351.  The two files share 9 calendar uids but are not the
same file.

## API contract (one paragraph)

`core::cal::decode_file(path)` / `decode_compound_file(mpp::CompoundFile const&)`
return a `CalendarTable`: every calendar row (`uid`, `base_uid`, `resource_uid`,
`name`, raw seven-day `week`, `exceptions`, `work_weeks`), the project default
week from `114/Props`, the project default calendar name, and counters
(`metadata_anomalies`, `recurring_exceptions`, `sub_minute_intervals`).
`build_calendar(table, definition)` turns one definition into a
`core::sched::Calendar` by resolving "inherit" days through the base chain and
the project default week, then merging every dated exception and dated work week
into the `Week` of the week it falls in and installing it with
`set_work_week` (so `core/sched` needs no dated-exception API).
`build_project_calendar(table[, name])` / `load_project_calendar(path-or-file[,
name])` do the name resolution and the decode: an empty `name` means the
project default from `114/Props`; an explicit name that is absent **throws
`mpp::Error`**; a missing default name falls back only when the file has exactly
one base calendar, otherwise it throws; a missing `114/TBkndCal/FixedData`, a
`FixedData` that is not `64 + 12*n` bytes, or a short Var2Data block also throws
`mpp::Error`.  Damaged-but-readable metadata never throws: it increments
`metadata_anomalies` and rows are still read from `FixedData`'s own size.
Recurring exceptions (`PjExceptionType` 2..7) are counted and **not** applied.

## Open items / next actions

1. **Coordinator**: wire `core::cal::load_project_calendar(path)` into
   `tools/recalc` + `core/plan` (link `core_cal`; it links `core_sched` and
   `core_mpp` PUBLIC).  Catch `mpp::Error` and keep the hard-coded Standard
   calendar as the fallback.  PROJECT_A should move the holiday-crossing dates.
2. **Coordinator/next wave**: expose TBkndTask row `+178` (i32 task-calendar uid,
   `-1` = NA) in `core/mpp`; then the bridge can do
   `find_calendar_by_uid(table, uid)` + `build_calendar`.  No reader change was
   made by this stream.
3. Re-run the M2 comparison with the real PROJECT_A calendar and see how many of the
   24 remaining date diffs go away.  The 1331 exceptions include holidays that
   the hard-coded calendar cannot cross; expect movement on the 2026 links that
   jump a public holiday.
4. ~~Recurring exceptions are skipped~~ **DONE in wave 9b (W41, see the addendum
   at the end of this file)**: types 2..6 are expanded and applied when the
   record implies the recurrence, type 7 is refused and counted.  The remaining
   gap is the recurrence PARAMETER words (weekday mask, every-N, position,
   month/day); the fixture set `w9-r-*` named in the addendum is the wave-10
   oracle work.
5. `Fixed2Data`/`Fixed2Meta` (per-calendar GUIDs, 48/10-byte records) are
   present but not decoded — calendar arithmetic does not need them.  A writer
   would.

## Traps hit this run

* The Standard calendar is **not** in `TBkndCal` (no Var2Data block); it is
  `114/Props` `DEFAULT_CALENDAR_HOURS`.  A TBkndCal-only decoder yields an empty
  week and every task becomes zero-duration.  This is the single most likely
  way to get this wrong.
* `day type == 1` means "inherit", and the **other 58 bytes of that block are
  stale buffer content** in real files (PROJECT_A's contain fragments of unrelated
  strings).  Never read `count`/`starts`/`lengths` for an inherited day.
* The day-block length array starts at **+20**, not at +0x1c: `start[6]` and
  `start[7]` overlap `length[0]`, so the format supports at most 6 intervals.
  Reading lengths at +0x1c (the in-memory `DayRecord` offset) gives wrong times.
* The exception `from`/`to` are `u16` day numbers, not packed date32; the date
  is the day component only (15635 = 2026-10-21).
* Calendar `FixedMeta` entries are **10** bytes, not the Task table's 47.  The
  four 16-byte FixedData header descriptors are why rows start at 64.
* `ctest -R cal` matches `core_sched_calendar` and `plan_recalc` too; use
  `-R "^cal$"` when only this stream's target is built.
* MSVC's CRT deprecates `std::getenv` (C4996 fatal under `/WX`); the test uses
  `_dupenv_s` on `_MSC_VER` and `std::getenv` elsewhere, copied from
  `tests/mpp_reader_tests.cpp`.
* `corpus/` is read-only and git-ignored: `holiday1.mpp`/`base.mpp` were only
  read.  No client name (calendar, exception or work-week) is in any file this
  stream wrote.

## Files touched

* `core/cal/cal.h`, `core/cal/cal.cpp` — decoder, builder, API.
* `core/cal/CMakeLists.txt` — `core_cal` + `cal_tests` + `add_test(cal)`,
  standalone mode.
* `core/cal/README.md` — byte evidence, counts, API, limits.
* `tests/cal_tests.cpp` — 165 checks with corpus / 38 without.
* `memory/cal.md` — this file.

## W41 addendum (wave 9b, 2026-10-22 16:55) — recurring exceptions: decoded, expanded, applied when derivable

Owned paths touched: `core/cal/cal.h`, `core/cal/cal.cpp`,
`tests/cal_tests.cpp`, this file.  `core/sched` was edited by this stream for
other items, not for this one (the builder still installs per-week overrides).
No client name is used below (counts/dates only).

### What changed

1. **The +4 word is now decoded** as `DatedException::occurrences` (u16, the word
   the 92-byte record keeps for recurring exceptions; mpxj calls it the
   occurrence count).  It is a **check, never a source of truth** ([I] identity:
   no file on disk has a recurring exception).
2. **`core::cal::expand_recurring_exception()`** expands one PjExceptionType 2..7
   record into the concrete days it covers:
   * type 2 yearly month+day -> the start day's (month, day) once a year;
   * type 3 yearly positional -> the same nth weekday of the start day's month;
   * type 4 monthly month+day -> the start day's day-of-month every month;
   * type 5 monthly positional -> the same nth weekday every month;
   * type 6 weekly -> the start day's weekday every week;
   * type 7 day-count -> **refused** ("the 'every N days' period is not
     decoded");
   * the positional "nth" is `(day-1)/7 + 1`, or "last" when no further
     same-weekday day fits in that month;
   * a refusal (`why`) is returned instead of a guess when: the type is not
     recurring, the type is outside 0..7, the expansion is empty, it exceeds the
     caller's day cap (`kMaxExpandedExceptionDays` = 3660), or the occurrence
     word disagrees with the expanded count.
3. **`build_calendar()` applies the expansion** exactly like a plain dated range,
   and takes an optional `RecurrenceReport*` (`applied` / `not_applied`).
   `build_project_calendar()` forwards it.  An exception that cannot be expanded
   is counted and skipped, never guessed.  `CalendarTable::recurring_exceptions`
   still counts every PjExceptionType 2..7 row.

### The inference, named exactly (wave-10 oracle agenda)

The recurrence **parameters are not decoded**.  The record carries the range
(+0/+2), the occurrence word (+4), the interval list (+14/+20/+32) and the type
(+72) — but not the **weekday mask** (type 6), the **"every N" period** (types
4/5/6/7), the **position** (types 3/5), or the **month/day** parameter words
(types 2/3).  The expansion therefore assumes the shortest period (every
week/month/year) and derives weekday / day-of-month / position from the range's
first day.  The occurrence word catches a wrong period (it refuses when the
expanded count differs), which is why type 7 is refused rather than expanded at
period 1.

Fixture set that would turn this into a measurement (one file per type, one
exception each, `RecurCal` = the Standard week, one 10 d task using it, project
start Mon 2026-10-12, save + record Start/Finish):
`w9-r-weekly` (every Monday, 2026-10-19..2026-11-02), `w9-r-monthly` (day 15,
2026-10..2027-01), `w9-r-yearly` (every 21 October, 2026..2028), `w9-r-relative`
(second Tuesday, 2026-10..2027-01), `w9-r-daycount` (every 3 days,
2026-10-19..2026-10-31).  The coordinator's probe shows `Project.Calendar`
exposes `Exceptions`, `Years`, `WeekDays`, `WorkWeeks`, `Period`, `Reset`, so
the object model can probably build them; the coordinator owns that oracle work
(wave-10 agenda) and this stream did not spend any more on it.

### Counts

`cal_tests` **165 -> 217 checks** (52 new: the expansion and refusal rules, the +4
decoder word, and the builder application), all passing with corpus
and under `CAL_NO_CORPUS=1` (the new tests are CI-safe; no fixture needed).
`ctest -R "^cal$"` = 1/1.

### Traps added

* A recurring exception must never be applied without the count check: the
  period is an assumption.
* `build_calendar(table, calendar, &report)` — the report is optional and
  per-call; the table's `recurring_exceptions` is a decode-time count, not an
  applied count.
* The exception's name bytes are still skipped (client-data rule); nothing here
  prints an exception name.
