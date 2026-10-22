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

## W49 addendum (wave 11, 2026-10-22 18:55) — task calendars reach the CPM bridge

`core/cal` itself did not need a decoder change for W49.  The pieces the earlier
addendum asked for are now consumed:

* `core::plan::attach_task_calendars(PlanInput&, CalendarTable const&)` builds
  exactly ONE `core::sched::Calendar` per distinct `Task::calendar_uid` (task
  row +178) with `find_calendar_by_uid` + `build_calendar`, and appends one note
  with the counts (named rows / built / missing / failed).  It never throws; a
  missing uid or a build failure leaves those tasks on the project calendar.
* `PlanInput::task_calendars` owns the Calendar objects and `recalculate()`
  points each CPM node at its task calendar; entries with uid <= 0 keep the
  project calendar.

Proof through the bridge: `corpus/probe/holiday1.mpp` (project calendar
Standard, all three tasks store +178 = 3 = SiteCal).  With the task calendars
T2 is Thu 2026-10-22 08:00 -> Thu 10-29 12:00 and T3 is Fri 10-23 08:00 ->
Mon 10-26 12:00; with every +178 reset to -1 the same rows are T2 Wed 10-21
08:00 -> Tue 10-27 17:00 and T3 Fri 10-23 08:00 -> Fri 17:00.
`tests/plan_recalc_tests.cpp` `test_task_calendars` pins both runs (277 checks
total).  PROJECT_A and PROJECT_B store -1 on every task row, so M2 is unchanged
(332/332) and `PlanInput::task_calendars` is empty there.

Counts unchanged: `cal_tests` 217 with corpus / 90 with `CAL_NO_CORPUS=1`
(the difference from the W25 note is the W41 recurring-exception checks).
Recurring-exception PARAMETERS were not attempted in W49: still no fixture, and
the brief made item 4 conditional.  The existing expansion remains the
shortest-period inference documented above; do not claim the weekday mask,
every-N, position or month/day words are decoded.

Client-data rule: no calendar or exception name from a client file appears in
the new code/tests/docs.  The only name added is the synthetic fixture label
"metre", which is in `corpus/probe` (COM-built), not client data.

## W53 addendum (wave 12, 2026-10-22 19:55) — recurring exception PARAMETERS decoded and applied

Status: done for every PjExceptionType that stores its parameters.  One measured
gap remains (monthly-positional weekday), stated below and in `core/cal/cal.h`.
Owned paths touched: `core/cal/cal.h`, `core/cal/cal.cpp`, `core/cal/README.md`,
`tests/cal_tests.cpp`, `core/plan/CMakeLists.txt` (fixture path),
`tests/plan_recalc_tests.cpp`, `tools/cal_exception_dump.py`, this file.  No
client name is used below.

### The measurement

Fixture: `corpus/probe/w12-exceptions.mpp` (coordinator-built over COM), one
exception of every `PjExceptionType` on the project calendar.  Ground truth:
`docs/analysis/oracle/w12-exceptions.json` — COM read-back before the save and
after the reopen, identical in both.  Second opinion: mpxj
`AbstractCalendarAndExceptionFactory.readRecurringData` (fetched from GitHub,
read-only) reads the same words with the same meanings.

Dump command (read-only; prints no client names):

```
python tools/cal_exception_dump.py corpus/probe/w12-exceptions.mpp --words
```

Output for the seven records (type, start/end day, occurrence word, parameters):

```
type 1 start 15647 end 15675 occ 29  | no parameters (plain range)
type 6 start 15647 end 15706 occ 18  | days_of_week 0x0a period 1
type 5 start 15647 end 15858 occ  7  | position 0 month_item 0 period 1
type 4 start 15660 end 15780 occ  5  | month_day 15 period 1
type 3 start 15672 end 17133 occ  4  | month 11 position 4 month_item 8
type 7 start 15696 end 15700 occ  5  | period 1
type 2 start 15780 end 16876 occ  4  | month 3 month_day 15
```

**The layout (measured).**  Seven `u16` words at **+74..+86** of the 92-byte
record.  Byte views of those words: `+76` (low byte and high byte), `+77`,
`+78` low byte and `+78` as a word:

| type | +76 low | +77 | +78 |
|---|---|---|---|
| 1 daily | 0 | 0 | 0 |
| 2 yearly month+day | Month-1 (2 -> March) | MonthDay (15) | 0 |
| 3 yearly positional | Month-1 (10 -> November) | MonthPosition (4 = last) | MonthItem (8 = Friday) |
| 4 monthly day | MonthDay (15) | 0 | Period (1) |
| 5 monthly positional | MonthPosition (0 = first) | MonthItem (0 here) | Period (1) |
| 6 weekly | DaysOfWeek bitmap (10 = Mon\|Wed) | 0 | Period (1) |
| 7 every N days | Period (u16) | — | — |

`MonthItem` = PjMonthItem, Sunday-first 3..9 (3 Sunday, 4 Monday, 8 Friday, 9
Saturday).  `MonthPosition` = PjMonthPosition 0 first .. 3 fourth, 4 last.
`DaysOfWeek` bit 0 = Sunday .. bit 6 = Saturday.  The `+80` word is **not** a
parameter: it was 1..7 across the seven records in creation order (a recorded
ordinal).  `+74`, `+82`, `+84`, `+86` are 0 in all seven.

**The validation is the +4 occurrence word.**  Project recomputes it from the
range and the parameters, so "our expansion reproduces Project's own count" is a
measurement of the decode, not of our assumptions.  All six recurring records
agree: 18 (weekly), 7 (monthly positional), 5 (monthly day 15), 4 (yearly
positional), 5 (every-N-days), 4 (yearly month+day), plus 29 days for the type-1
plain range.  The sharpest case is the yearly positional record: last Friday of
November 2026..2030 = 2026-11-27, 2027-11-26, 2028-11-24, **2029-11-30**
(excluding 2030-11-29, which is past the stored Finish) — exactly 4 occurrences,
and only a correct Month=11/MonthItem=8/Position=4 decode produces that set.

### What changed

1. `DatedException` gained `period`, `days_of_week`, `month`, `month_day`,
   `month_item`, `month_position` and `raw_recurrence_words` (the seven words
   verbatim).  `core::cal::weekday_of_month_item()` maps PjMonthItem -> 0..6/-1.
2. `decode_calendar_data()` reads the seven words and calls
   `decode_recurrence_parameters()`, which switches on the stored type exactly as
   the measured table says (byte at +76/+77, word at +78; type 4's period is a
   byte, mirroring mpxj).
3. `expand_recurring_exception()` is parameter-driven for types 2..7:
   * 2: month+day every year; a 29 February or a 31st in a short month is
     skipped (the month has no such day), never clamped;
   * 3: the position-th weekday of the month every year (position 4 = last);
   * 4/5: every `period` months, day-of-month / position-th weekday (position 0 =
     first);
   * 6: every `period`th week, every weekday in the bitmap (bit 0 = Sunday, the
     week starts on the Sunday on or before the start day);
   * 7: every `period` days from the start day (type 7 is how Project stores
     "daily every N days": the period is at +76; type 1 is every day and stays a
     plain range).
   Parameters are never derived except when the type does not store them (0):
   the weekday falls back to the start day's, the day-of-month to the start
   day's, the month to the start day's, the period to 1.  **Position is never
   derived** — 0 is a stored value meaning "first", which changed one W41 test.
   Refusals are unchanged (not recurring, empty range, day cap, occurrence-word
   disagreement) plus two new ones: a MonthItem that is not a weekday (1, 2, 0 or
   >9 when non-zero) and a MonthPosition outside 0..4.
4. Every loop is bounded by the day range (a 16-bit word: <= 65535 daily steps,
   <= 9363 weeks, <= 2340 months, <= 180 years) and the day cap stops the
   accumulation the moment it is exceeded, so there is no unbounded loop even
   with the cap disabled.

### The one gap (measured, not inferred)

For the **monthly positional** record Project stores 0 in the MonthItem byte and
COM reads `MonthItem 0` / `MonthPosition 0` back (`FirstMonday` in the JSON).
The record has no weekday, so the expansion derives it from the start day
(2026-11-02 is a Monday) — and the stored occurrence word (7) still agrees, which
proves the derivation for this record.  A different monthly-positional exception
whose start day is not its weekday cannot be distinguished from one that is; the
occurrence word catches it (count disagreement -> refusal).  Everything else is
read from the file.

### Counts (build\vs-w53, Release, /W4 /WX)

| binary | before | now |
|---|---|---|
| `cal_tests` (corpus) | 217 | **509** |
| `cal_tests` (`CAL_NO_CORPUS=1`) | 90 | **245** |
| `plan_recalc_tests` | 277 | **291** |
| `ctest -R "cal\|sched\|plan\|mpp"` | 21/21 | **24/24** |

New tests: `test_recurrence_parameters` (CI-safe: decode round trip for all seven
types in one hand-built block, every type's expansion reproduced, PjMonthItem
mapping, leap day 2024-02-29..2028-02-29 = 2 days, month-end 31st Jan/Mar/May
2027, absent fifth Monday Feb/Mar 2027, weekly period 2, monthly period 2 across
a year boundary, zero-occurrence one-day range, empty-mask fallback, parameter
guards) and `test_w12_exceptions` (corpus: the seven records' parameters equal
the JSON, 72 expanded days, builder applied 6 / refused 0, eleven spot dates from
2026-11-02 to 2030-11-29, `add_work` Mon 11-02 + 2 d = Thu 12-03 17:00, and the
derived calendar uid 2 inheriting all six).

### Traps added

* `cal_tests`' `CAL_HOLIDAY` default name check is "Standard" — the w12 fixture's
  project default is also "Standard" (synthetic), so the two do not collide.
* The convention change: PjMonthPosition 0 is "first", so the old
  derive-the-position-from-the-start-day behaviour is gone.  A hand-built
  `DatedException` with position 0 expands to the FIRST such weekday, not to the
  start day's ordinal; set `month_position` when the rule is "second".
* The +80 word is an ordinal, not a parameter.  It matched the fixture's creation
  order for all seven records; do not decode anything from it.
* Type 7 is no longer refused.  A type-7 record with period 0 falls back to every
  day, which is exactly what the builder already did for a plain range.

### Open

1. A monthly-positional fixture whose start day is not the first matching
   weekday would settle whether Project ever stores MonthItem for type 5 (it did
   not here).  Coordinator-only (COM).
2. Yearly recurrences store no period word, so "every N years" is unmeasurable
   from this fixture; the decoder assumes every year, which is what all six
   records show.

