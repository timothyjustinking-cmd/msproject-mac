# Scheduling semantics — behaviour spec for the C++ engine

Stream W3, 2026-10-22. This is the contract that `core/sched/` implements:
calendar arithmetic, duration/work/units, CPM, constraints, links and what
recalculates. It is written to be implementable and falsifiable, not to be a
description of the UI.

**Status of verification.** Everything tagged **[B]** was read out of the
decompiled binary at the cited address. **[C]** is the recovered field model in
`analysis/*.tsv`, which was COM-validated in the earlier pass. **[I]** is
inference from standard MS Project behaviour plus the structure of the binary;
it needs an oracle fixture. **[O]** is genuinely open: do not hard-code a choice
without the named experiment.

The oracle is coordinator-only in this environment (`docs/coordination.md`
Revision 3): COM to Project hangs in a confined shell. `scripts/sched_oracle_recipes.py`
therefore prints the exact recipes to hand to the coordinator; it never launches
Project.

**Oracle run 1 (2026-10-22).** W3-O1 and W3-O4 have been executed:
`python scripts/oracle_w3.py o1|o4|o4b`, results in `docs/analysis/oracle/
w3-o1.json`, `w3-o4.json`, `w3-o4b.json`; Project's own saved fixture is
`corpus/probe/w3-o1.mpp`. Where a tag has been replaced by a measurement, the
text cites the JSON file. Still **[I]**: work/units triangle (W3-O2), link lag
anchors (W3-O3), elapsed durations and options (W3-O5), summary rollup (W3-O6),
free slack and its clamp (W3-O7), recalc convergence (W3-O8).

Reproduce the static evidence with:

```
python scripts/sched_index.py func 1402b5360     # recalc orchestration
python scripts/sched_index.py func 1402b570c     # per-element pass loop
python scripts/sched_index.py func 1402bb2b0     # outline rollup
python scripts/sched_index.py vt CalendarReader  # calendar virtual interface
python scripts/sched_index.py refs 0xb400011     # constraint field users
python scripts/sched_analyze.py                  # whole evidence report
```

---

## 0. Primitives: dates, durations, units, enumerations

### 0.1 `DATETYPE` / "date32"

MS Project's engine date is a packed **32-bit** value (type `date32` in the
field metadata, `DATETYPE` in RTTI):

```
bits 31..16 : day number, 1-based, epoch 1984-01-01 (day 1 = 1984-01-01)
bits 15..0  : time of day, in TENTHS OF A MINUTE, 0 .. 14390 (23:59)
```

* **[B]** `FUN_140200b10` splits a date with `param_2 >> 16` and
  `param_2 & 0xffff` and positions a time inside a day's intervals
  (`decompiled/part_0000.c:67381`). `FUN_14024e7c0` does the same
  (`decompiled/part_0001.c:35481`). `FUN_14038bdb0` (backward calendar walk)
  uses `param_2 >> 0x10` for the day (`decompiled/part_0003.c:58667`).
* **[C]** `analysis/flat_buffer_field_map.tsv:21`: a confirmed COM correlation
  `0x3d122bf2 = Oct 20 2026 18:45`, decoded as `(day<<16)|(min*10)`.
  `analysis/com_validation.tsv` types Start/Finish/Early*/Late*/ConstraintDate
  as `date32`.
* Weekday: `FUN_140246be8(date, weekStart)` returns `(day - 1) % 7` for
  `weekStart = 0`, and `6` for day 0 (`decompiled/part_0001.c:29423`).
  1984-01-01 was a Sunday, so **weekday 0 = Sunday … 6 = Saturday**.
* `0xFFFFFFFF` = NA / not set. The walker's overflow sentinel is `0xECD73836`
  (day 0xECD7 = 60631, time 23:59); it is returned when a walk leaves the
  calendar domain (`FUN_140200b10`, `FUN_14038c410`).
* Resolution is one tenth of a minute (6 s). Every duration/date computation is
  integer tenths; never use floating point for stored values.

Helpers the engine needs:

```text
pack(day, tenths)  -> u32
day(d)             -> d >> 16
time(d)            -> d & 0xffff
weekday(d)         -> (day(d) - 1) % 7          # 0 = Sunday
add_days(d, n)     -> ((day(d) + n) << 16) | time(d)
date_only(d)       -> day(d) << 16
```

**Falsification W3-D1.** A task created on 2026-10-20 08:00 must store
`(15634 << 16) | (480*10) = 0x3D1212C0`; a task on Sunday 2026-10-18 must have
weekday 0. Recipe `W3-D1` in `scripts/sched_oracle_recipes.py`.

### 0.2 Durations and work

Durations and work are integers in **tenths of a minute** (1 minute = 10).
COM's `Duration`/`Work` properties return minutes as a `double`; the raw stored
`5 days` is 24 000 (= 5 × 480 min × 10).

* **[B]** `FUN_140200b10` compares a day's remaining duration against interval
  lengths stored in tenths of minutes. The overflow sentinel above is
  `0xECD73836` = 60631 days 23:59, i.e. the same time scale.
* **[C]** `analysis/com_validation.tsv` lists Duration/Work as `i32`/`i64` with
  COM values in minutes. `scripts/phase2d_task_record.py:134` records the
  differential-scan assumption "5 days = 24000 min".
* Negative values are legal (leads, negative slack). `-1` (`0xFFFFFFFF`) means
  "not set" in a number of stored fields; the raw field must not be treated as
  a duration when it is -1.

**Falsification W3-D2.** `Task.Duration = "4h"` must store 2 400 raw units;
`"1d"` with HoursPerDay=8 must store 4 800.

### 0.3 Units

`Assignment.Units` is a floating-point ratio at the COM boundary (1.0 = 100 %)
but a fixed-point integer internally. The scale is **not yet verified**; the
descriptor types it as an 8-byte "Work"-shaped value (`0x0f400007`,
`analysis/field_metadata_assignment.tsv:9`). Until `W3-U1` pins it, keep units
as an exact rational internally and convert at the COM boundary. Do not assume
100 % = 100; the engine's `GINT` type and the fact that work is in tenths
suggest a hundredth-of-a-percent or thousandth scale.

### 0.4 Enumerations

From the binary's own type library (`analysis/typelib.tsv`, **[B]**):

| enum | values |
|---|---|
| `PjConstraint` (line 39) | 0 `pjASAP`, 1 `pjALAP`, 2 `pjMSO`, 3 `pjMFO`, 4 `pjSNET`, 5 `pjSNLT`, 6 `pjFNET`, 7 `pjFNLT` |
| `PjTaskFixedType` (line 4398) | 0 `pjFixedUnits`, 1 `pjFixedDuration`, 2 `pjFixedWork` |
| `PjTaskLinkType` (line 4394) | 0 `pjFinishToFinish`, 1 `pjFinishToStart`, 2 `pjStartToFinish`, 3 `pjStartToStart` |
| `PjFormatUnit` (line 3194) | 3 min, 5 hour, 7 day, 9 week, 11 month; **+1 = elapsed** (4,6,8,10,12); **+32 = estimated** (35…44) |
| `PjCalendarType` (line 11386) | 1 Gregorian, 6 Hijri, 7 Thai |
| `PjExceptionType` (line 11343) | 2 yearly-month-day, 3 yearly-positional, 4 monthly-month-day, 5 monthly-positional, 6 weekly, 7 day-count |

The constraint order is independently confirmed by the counting function
`FUN_140406fdc` (`decompiled/part_0004.c:6984`): it buckets tasks as
`Constraint_ASAP` (0), `Constraint_ALAP` (1), `Constraint_MSO` (2),
`Constraint_MFO` (3), `Constraint_SNET` (4), … (strings
`analysis/meta/strings.tsv:6436-6446`).

**Link types are numbered 0 = FF, 1 = FS, 2 = SF, 3 = SS.** Do not use the
spoken order — the enum is not in "FS/SS/FF/SF" order.

### 0.5 Field ids at the COM boundary

From `analysis/com_validation.tsv` (COM-validated **[C]**):

| field | id | storage |
|---|---|---|
| Work | `0x0b400000` | i64, tenths of min |
| ConstraintType | `0x0b400011` | i16 (`PjConstraint`) |
| ConstraintDate | `0x0b400012` | date32 |
| Critical | `0x0b400013` | bool |
| FreeSlack | `0x0b400015` | i32 |
| TotalSlack | `0x0b400016` | **computed, not stored** |
| Duration | `0x0b40001d` | i32, tenths |
| FixedDuration | `0x0b400022` | bool |
| Start / Finish | `0x0b400023` / `0x0b400024` | date32 |
| EarlyStart / EarlyFinish | `0x0b400025` / `0x0b400026` | date32 |
| LateStart / LateFinish | `0x0b400027` / `0x0b400028` | date32 |
| ActualStart / ActualFinish | `0x0b400029` / `0x0b40002a` | date32 |
| Deadline | `0x0b4001b5` | date32 |
| StartSlack / FinishSlack | `0x0b4001b6` / `0x0b4001b7` | i32 |
| DurationText | `0x0b400507` | text |
| Task type | `0x0b400080` | i16 (`PjTaskFixedType`) |

`phase2d_slack_verify.py:110-114` records the important asymmetry:
StartSlack/FinishSlack/FreeSlack live in the record; **TotalSlack has
dispatch_type 10 (computed) and is not in the flat record**. It must be derived
as `LateStart - EarlyStart` (equivalently `LateFinish - EarlyFinish`, which can
differ for elapsed/contoured tasks).

---

## 1. Calendar arithmetic

### 1.1 The data model

A calendar is a **base-calendar chain plus week patterns plus dated
exceptions**. The engine caches, for a day, the effective week containing it:

```text
EffectiveWeek {
    weekStartDay    # day number of the first day
    weekFinishDay   # normally weekStartDay + 6
    totalTenths     # working time in the seven days
    day[7]          # interval list per day, Sunday..Saturday
}
DayPattern {
    count           # working intervals, 0..8
    start[8]        # interval start, minutes from midnight
    length[8]       # interval length, minutes
}
```

* **[B]** `FUN_140203ccc` (`CalendarReader::GetEffectiveWeekExtents`,
  `decompiled/part_0000.c:69674`) returns week start, week finish and weekly
  minutes from cached fields at calendar `+0x1c0`, `+0x1c2`, `+0x1c4`.
* **[B]** `FUN_14085ae80` (`decompiled/part_0016.c:68658`) fills a
  7 × `0x3c`-byte day-pattern block at `+0x1c`, sums the seven days into
  `+0x1c4`, and marks the cache valid; the invalid path zeroes exactly `0x1a4`
  bytes = 7 × 60.
* **[B]** Day-pattern layout: `FUN_140200b10`/`FUN_140200cac`
  (`decompiled/part_0000.c:67381,67449`) read `count` at `+2`, interval start at
  `+8 + 2*i`, interval length at `+0x1c + 4*i`. The record is 60 bytes and holds
  at most **8** intervals.
* **[B]** Weekday index is Sunday-first (§0.1); `FUN_14085ae80` copies seven
  day patterns with a `0..6` loop.
* **[B]/[I]** Week sources: the calendar keeps a list at `+0x1d0` with count at
  `+0x1c8`; a week is materialised by `FUN_14085ced0`, and if no source covers
  the week the cache is marked `0xFFFF`. Dated exceptions replace the pattern of
  the week they fall in; a non-working exception has `count = 0`, a custom
  working day has its own interval list, "use base times" resolves through the
  chain, "use default times" resolves to the calendar's default week.

Implementation model:

```text
Calendar {
  base: Calendar*                     # null for a base calendar
  defaultWeek: DayPattern[7]          # default work week
  workWeeks: [(weekStartDay, DayPattern[7])]  # explicit work-week overrides
  exceptions: [Exception]             # dated, possibly recurring
  hoursPerDayOverride: optional       # not used by scheduling maths (§1.5)
}
Exception {
  startDay, endDay                    # inclusive day numbers
  recurrence                          # PjExceptionType + parameters
  mode: nonworking | custom | useBase | useDefault
  pattern: DayPattern
}
```

`effective_pattern(cal, day)`:

1. expand and apply any exception covering `day`; `useBase` continues into the
   base chain, `useDefault` substitutes `defaultWeek[weekday]`;
2. else, if a `workWeeks` entry starts in `[day - weekday(day), +6]`, use that
   week's pattern;
3. else `defaultWeek[weekday(day)]`;
4. if the result is "no working time" and a base exists, consult the base for
   that day and merge. The exact merge rule is **[O]** (`W3-C3`).

### 1.2 Working-time predicates

```text
is_working_instant(cal, t)   # t inside some interval: start <= t < start+len
is_working_day(cal, day)     # sum of the day's interval lengths > 0
next_start(cal, t)           # t if inside working time, else start of next interval
prev_finish(cal, t)          # t if inside working time, else end of previous interval
```

Every entry into or exit from the calendar goes through these; no code may add
"one day" or "08:00" directly.

### 1.3 `WorkTimeBetween(cal, a, b)`

Verified by `FUN_140305e00` (`CalendarReader::WorkTimeBetween`,
`decompiled/part_0002.c:65390`):

```text
if b <= a: return 0
total = 0
for day in day(a) .. day(b):
    intervals = effective_pattern(cal, day)
    lo = time(a) if day == day(a) else 0
    hi = time(b) if day == day(b) else 14400
    for (s, len) in intervals:
        total += max(0, min(hi, s+len) - max(lo, s))
return total
```

The binary has a fast path for spans under eight days and a whole-week shortcut
for longer spans: align to a week boundary, count full weeks using
`GetEffectiveWeekExtents(...).totalTenths`, then sum the partial weeks
(`FUN_14024ed00`, `FUN_14085ad58`). Results must be identical; implement the day
loop first and add the week cache only with a differential test.

The interval is half-open at the start and closed at the end: a 08:00–17:00 day
has one interval of length 540 and `WorkTimeBetween(08:00, 17:00) = 540`.
The first/last-day partial sums in `FUN_14024ed00`
(`decompiled/part_0001.c:35689`) subtract exactly the time outside `[a,b]`.

**Falsification W3-C1.** Mon–Fri 08:00–17:00, weekend non-working:
`WorkTimeBetween(Fri 16:00, Mon 09:00) = 60 + 60 = 120` minutes.

### 1.4 `AddWork(cal, t, d)` — adding a duration to a date

This is the single most important function in the engine. The binary implements
it in two layers:

* outer: whole weeks, then whole days — `FUN_14024e7c0`
  (`CalendarReader::GetEndDate` / `FastDateEnd`,
  `decompiled/part_0001.c:35481`);
* inner: position inside one day — `FUN_140200b10`, with
  `FUN_140200cac` walking the interval list (`decompiled/part_0000.c:67381,67449`).

Specification:

```text
AddWork(cal, t, d):                 # d >= 0, tenths of a minute
    if d == 0: return t
    t = next_start(cal, t)          # a non-working start snaps forward first
    loop:
        (s, len) = interval containing t       # s <= t < s+len
        avail = (s + len) - t
        if d <= avail: return t + d            # may equal s+len (interval end)
        d -= avail
        t = next_start(cal, s + len)           # next interval, next day if needed
        if no next interval: return OVERFLOW   # 0xECD73836
```

Consequences, all pinned by tests:

1. A 1-day task (480 min) starting Mon 08:00 **finishes Mon 17:00**, not 16:59:
   finish is the instant after the last working minute is consumed.
2. A 1-day task starting Mon 14:00 finishes Tue 13:00 (3 h Mon + 5 h Tue).
3. `AddWork(Wed 17:00, 480) = Thu 17:00`, not Thu 08:00 — landing exactly on an
   interval end does not snap.
4. On a 24-hour calendar, 24:00 is normalised to next day 00:00
   (`FUN_140200b10`: `*puVar2 == 0x3840` → day+1, time 0).
5. A start on a non-working instant moves forward before any duration is
   consumed. This is **[O]** for the exact alignment (`W3-C2`): the binary's
   `GetEndDate` calls `FUN_14038c2f0` before the walk, which suggests forward
   alignment, but a task explicitly pinned to Saturday 10:00 must be measured.

`SubWork(cal, t, d)` — the backward pass, symmetric:

```text
SubWork(cal, t, d):                 # d >= 0
    if d == 0: return t
    t = prev_finish(cal, t)         # if t is at/inside a gap, previous interval end
    loop:
        (s, len) = interval ending at t        # t <= s+len
        avail = t - s
        if d <= avail: return t - d
        d -= avail
        t = prev_finish(cal, s)                # previous interval/day
```

**[B]** `FUN_14038bdb0` is `CalendarReader` vfunc 3, the backward counterpart of
`GetEndDate`: it consumes the remainder of the current week, loops full weeks
using `GetEffectiveWeekExtents`, then calls `FUN_140200b10` for a negative
remainder and `FUN_14038c410` to land at the end of the previous working day
(`decompiled/part_0003.c:58667`). `FUN_14038c410` returns the *end* of the
previous working day; `FUN_14038c530` returns the start time when subtracting
within a day; `FUN_1403da710` subtracts within a day
(`decompiled/part_0003.c:58990,59035,112304`). `FUN_14038c590` is the
"late start = finish - duration" helper.

**Falsification W3-C3.** Task 5 days starting Tue 08:00 (Mon–Fri 08:00–17:00)
finishes the following **Monday 17:00**: Tue/Wed/Thu/Fri are 4 days, the weekend
is skipped, the fifth day is Monday and the finish is its interval end.

**Falsification W3-C4.** A 4-hour task starting Fri 15:00 finishes Mon 10:00:
2 h Friday + 2 h Monday.

**Measured against MS Project (W3-O1, 2026-10-22).** Run once by the coordinator:
`python scripts/oracle_w3.py o1` → `docs/analysis/oracle/w3-o1.json`; Project's own
saved file `corpus/probe/w3-o1.mpp` is then read with
`python scripts/mpp_rows.py corpus/probe/w3-o1.mpp`. Blank project, project start
2026-10-19 (Mon), standard calendar, each task's start pinned by an SNET
constraint (`Task.Start` setter):

| task | duration | start | Project's finish | raw finish word in the file |
|---|---|---|---|---|
| T1 | 1 d | Mon 10/19 08:00 | **Mon 10/19 17:00** | `0x3d1127d8` = day 15633, 10200 ticks |
| T2 | 5 d | Tue 10/20 08:00 | **Mon 10/26 17:00** (weekend skipped) | `0x3d1827d8` |
| T3 | 4 h | Fri 10/23 **15:00** | **Mon 10/26 10:00** | start `0x3d152328` (9000 ticks = 15:00), finish `0x3d181770` (6000 = 10:00) |
| T4 | 0 d | Wed 10/21 08:00 | Wed 10/21 08:00 (milestone, start = finish) | `0x3d1312c0` both |

So consequences 1 and 2 and both falsifications are **confirmed**, and the same
run pins the storage scale: duration in the row is **tenths of a minute**
(1 d = 4800), date is `(day<<16)|seconds/6`, and 2026-10-19 is day **15633**.
The project summary row stores the *span*, not the sum: 6 working days
(28800 ticks) from 10/19 08:00 to 10/26 17:00.

**Measured directly (not yet explained).** Assigning `Task.Start` normalises the
value to the project's default start time when the value carries a different
time of day *and* the COM caller passes a naive `datetime`: pywin32 converts a
naive datetime to UTC before handing it to Project, so 15:00 Sydney arrives as
04:00 and Project snaps it to 08:00. Pass `datetime(...).replace(tzinfo=utc)`
(or Project will silently shift every time you write). With UTC-aware values the
15:00 start is accepted and T3 finishes Mon 10:00 as predicted.

### 1.5 Hours per day/week/month are conversion constants only

`HoursPerDay`, `HoursPerWeek`, `DaysPerMonth` are project options, exposed by
`Application.OptionsCalendar` (`analysis/typelib.tsv:9359`). They define how many
minutes a *displayed* day/week/month is worth:

```
minutesPerDay   = HoursPerDay * 60                 # default 480
minutesPerWeek  = HoursPerWeek * 60                # default 2400
minutesPerMonth = DaysPerMonth * minutesPerDay     # default 20 * 480 = 9600
```

**Measured** in the blank-project oracle: `HoursPerDay = 8`, `HoursPerWeek = 40`,
`DaysPerMonth = 20` (`w3-o1.json`), and every duration behaved as 480 min/day
(1 d = 4800 ticks, a 4 h task = 2400 ticks). The distinction matters: a calendar whose working
day is 10 hours still schedules 600 minutes per day even when `HoursPerDay = 8`;
the option only changes how a duration typed as "1 day" is converted.

**Falsification W3-C5.** With `HoursPerDay = 10` and an eight-hour calendar (08:00–12:00,
13:00–17:00), a task entered as `1d` must store 6 000 (not 4 800) and span 600
working minutes: Mon 08:00 → Tue 11:00 (8 h Mon + 2 h Tue). The two facts — what
"1 day" converts to, and how much working time the calendar has — must be
reported separately by the fixture.

### 1.6 Which calendar a task uses

1. Task calendar, if `Task.Calendar` is set and the task is not a summary;
2. project calendar otherwise.

Resource calendars participate at the assignment level (work distribution), not
in the task's CPM duration **[I]**. The task span is computed on the task/project
calendar; a resource calendar carves up the assignment work. Multiple resources
are scheduled on the **union** of their calendars for the task span, each
assignment's work falling inside its own resource calendar. The exact
union/intersection rule is **[O]** (`W3-C3`). `IgnoreResourceCalendar`
(`0x0b40018f`) disables the resource calendar for that task. Summary tasks use
the project calendar; task calendars on summaries are ignored **[I]**.

---

## 2. Durations, work and units

### 2.1 The three quantities

For a leaf task with assignments `a`:

```
Work(task)     = Σ_a Work(a)                       # tenths of min
Duration(task) = AddWork(cal, Start, Duration) - Start   # elapsed working time
Units(a)       = Work(a) / Duration                # ratio for that assignment
```

For one assignment at constant units (no contour, no split):

```
Work = Duration × Units
```

All three are stored; the task type fixes one and an edit recomputes the other
two.

**[B]** The engine has a dedicated virtual
`TTaskTable::SetDependantFields(unsigned long, unsigned long, unsigned long, bool)`
(RTTI symbols at `analysis/meta/strings.tsv:13015-13016`, `16227-16230`), whose
lambdas mark tasks dirty and propagate to assignments (`FUN_1404ddcf0`,
`FUN_1404ddd80`, `decompiled/part_0007.c:10676,10703`). The fixed-point
arithmetic inside it is not yet decoded; §2.3 is the required behaviour and is
**[I]** until `W3-U1` exercises all nine cells.

### 2.2 Duration formats and text

`PjFormatUnit` (§0.4) is the display encoding. `DurationText`/`WorkText` are
generated from the number plus the format. Parsing accepts the abbreviation
table baked into the binary (resource 537,
`analysis/res/pjintl_strings.tsv:17377-17384`):

```
h, hr, hrs, hour, hours        eh, ehr, ehrs, ehour, ehours
d, dy, dys, day, days          ed, edy, edys, eday, edays
w, wk, wks, week, weeks        ew, ewk, ewks, eweek, eweeks
mo, mon, mons, month, months   emo, emon, emons, emonth, emonths
```

A leading `+` and the `*` are UI accelerators, not part of the accepted text.
The suffix `?` marks estimated durations (`analysis/com_validation.tsv` shows
`0 days?` and `Duration14Estimated` exists); it must round-trip.

Conversion to internal tenths:

```
to_internal(value, unit, elapsed, project):
    working = { min: 1, hour: 60, day: HoursPerDay*60,
                week: HoursPerWeek*60, month: DaysPerMonth*HoursPerDay*60 }
    elaps   = { min: 1, hour: 60, day: 1440, week: 10080, month: 43200 }
    return round(value * (elapsed ? elaps[unit] : working[unit]) * 10)
```

Elapsed constants are **[I]**: 24 h/day, 7 d/week, 30 d/month. The binary has a
distinct `ElapsedDayReader` class (`.?AVElapsedDayReader@@`,
`analysis/meta/strings.tsv:14680`) and `ElapsedDayInfoSource` (`1414ac148`), and
`GetEndDate` is paired with a "Fast" variant, so elapsed time is a first-class
calendar; the month length is `W3-E2`.

**Falsification W3-E1.** With HoursPerDay=8, `1d` stores 4 800 and `1ed` stores
14 400. With HoursPerDay=10, `1d` stores 6 000.

### 2.3 Task types and effort-driven scheduling

`PjTaskFixedType` (§0.4): 0 fixed units (default), 1 fixed duration, 2 fixed
work. The task type is a `short` at TASKNODE `+0xf2`; the effort-driven flag is
bit 11 (`0x800`) of TASKNODE `+0x1c`. **[B]**

`FUN_1404e9400` (`TaskElement::vfunc_38`, `decompiled/part_0007.c:21184`)
converts `(type, effortDriven)` into the small calculation mode the dependency
solver uses. Its own assertions are evidence for the mapping:

```
type = *(short *)(node + 0xf2);            # 0 units, 1 duration, 2 work
effort = (*(uint *)(node + 0x1c) >> 11) & 1;
if type == 1: mode = effort ? 4 : 1;       # fixed duration
if type == 0: mode = effort ? 3 : 0;       # fixed units
if type == 2:                              # fixed work
    assert effort, "expect effort driven"
    mode = 2;
```

(`FUN_1404e9400`: strings "expect work" and "expect effort driven" at
`part_0007.c:21204,21207`.) The mode codes are consumed by the solver; the
exact arithmetic lives in `SetDependantFields` and is not yet read out.

Required edit matrix — the **fixed** quantity is never changed by the edit; the
other two are recomputed from `Work = Duration × Units`:

| task type | edit | Duration | Work | Units |
|---|---|---|---|---|
| **Fixed Units (0)** | Duration | changed | `Dur × Units` | fixed |
| | Work | `Work / Units` | changed | fixed |
| | Units | `Work / Units` | fixed | changed |
| **Fixed Duration (1)** | Duration | changed | `Dur × Units` | fixed |
| | Work | fixed | changed | `Work / Dur` |
| | Units | fixed | `Dur × Units` | changed |
| **Fixed Work (2)** | Duration | changed | fixed | `Work / Dur` |
| | Work | `Work / Units` | changed | fixed |
| | Units | `Work / Units` | fixed | changed |

**Measured (W3-O2/O2b, 2026-10-22).** `python scripts/oracle_w3.py o2` and
`o2b` -> `docs/analysis/oracle/w3-o2.json`, `w3-o2b.json`. One task typed "3d"
from Mon 2026-10-19 08:00, one resource at MaxUnits 100 %, one assignment, a
`CalculateProject` after each edit. Units are changed on the **assignment**
(`Task.Units` does not exist); `Assignment.Units` takes and returns a fraction
(0.5 = 50 %, 2.0 = 200 %), and the task/work values are **minutes** on the COM
surface (3 d = 1440) while the file stores tenths (14400).

Changing the assignment's Units:

| task type | initial | Units = 50 % | Units = 200 % |
|---|---|---|---|
| fixed units (0) | dur 1440, work 1440 | **dur 2880**, work 1440 | **dur 720**, work 1440 |
| fixed duration (1) | dur 1440, work 1440 | dur 1440, **work 720** | dur 1440, **work 2880** |
| fixed work (2) | dur 1440, work 1440 | **dur 2880**, work 1440 | **dur 720**, work 1440 |

Setting the task's Work / Duration instead:

* fixed units, `Work = 4 h` -> duration 240 (finish Mon 12:00), work 240,
  units stay 1.0;
* fixed duration, `Work = 4 h` -> **duration stays 1440, work becomes 240, units
  stay 1.0** — so Project does *not* recompute units here; a fixed-duration task
  may be under-allocated (`work < duration x units`). The table above says
  `Units = Work / Dur` for that cell; **that cell is wrong** and is corrected to
  "units unchanged, work changed".
* fixed work, `Duration = 5 d` -> duration 2400, **work stays 240**.

Two resources at 100 % on a 3 d task: work = 2880 (sum over assignments); setting
the task's `Work = 4 h` gives duration **120 min** and 120 min on each
assignment, i.e. `duration = total_work / total_units` with
`total_units = sum of assignment units`. EffortDriven true and false made **no
difference** to that edit, so effort-driven behaviour below stays **[I]** until
a recipe that *adds* a resource to an existing task is run.

**Effort-driven [I]**

* One resource: no difference.
* Add a resource to an effort-driven task: total Work stays constant; the new
  assignment takes a share, so Duration (fixed work) or Units redistribute.
* Add a resource to a non-effort-driven task: Duration stays; Work grows by the
  new assignment's `Dur × Units`.
* Remove a resource: effort-driven keeps Work and rebalances; non-effort-driven
  keeps Duration and reduces Work.

**Summary tasks** do not obey the triangle: Duration is the working span of the
children, Work is their sum, Units is derived (`Work / Duration`). Editing a
summary duration scales children; that is a deliberate v1 non-goal.

### 2.4 Progress fields

`ActualDuration`, `RemainingDuration`, `ActualWork`, `RemainingWork`,
`PercentComplete`, `PercentWorkComplete` are stored once a task starts; recalc
must respect them, not overwrite them:

* `RemainingDuration = Duration - ActualDuration` for a started task (working
  minutes, not wall clock).
* Completing a task sets `RemainingDuration = 0` and `Finish = ActualFinish`.
* `% Complete` and `% Work Complete` are different when assignment units differ;
  do not alias them.
* A 100 % complete fixed-duration task stays 100 % when duration changes
  (string "Maintaining 100% complete on fixedDuration task",
  `analysis/meta/strings.tsv:4328`, in `FUN_140423a58`).

**[I]** arithmetic; `W3-U3` records 0/50/100 % cases.

---

## 3. CPM

### 3.1 Recalc orchestration in the binary

`TBkndRecalc` is the engine object. Its vtable has 94 slots
(`analysis/meta/vtables.tsv`; vtable `140fcb5e8` at runtime). Slot 11
(`FUN_1402b5360`, `decompiled/part_0002.c:955`) is the public recalc:

```text
FUN_1402b5360(rec, full?, reset?, notify?):
  if rec->busy: return 0
  if reset: rec->vfunc_0x50(); rec->vfunc_0x40()
  rec->vfunc_0x88(full?)                 # set mode
  schedule = rec->vfunc_0x30()           # project/schedule object
  from_finish = schedule->vfunc_0x1a0()  # scheduling direction flag
  Recalc(rec, forward = !from_finish, 0)
  Recalc(rec, forward =  from_finish, 0) # the opposite pass
  if NeedsAnotherPass(rec):
      Recalc(rec, forward = !from_finish, 1)
  XP_RecomputeOutline(rec)               # summary rollup
  result = Validate(rec, ...)
  ...
```

**[B]** `FUN_1402b5360` calls `FUN_1402b558c` twice with inverted flags, then
`FUN_1402b558c(..., 1)` if `FUN_1402afbe4` returns true, then
`FUN_1402bb2b0` (outline rollup), then `FUN_1402bc5d4`. The scheduling
direction comes from `vfunc 0x1a0` of the schedule object; `param_2` (full vs
incremental) is passed through.

`FUN_1402b558c` (`TBkndRecalc::Recalc`, `decompiled/part_0002.c:1022`) is the
per-mode pass. It labels itself `TBkndRecalc::Recalc-ForwardRecalc` /
`-BackwardRecalc` (strings `analysis/meta/strings.tsv:4337-4338`), runs a
"set up all elements" loop when `full`, then `FUN_1402afd7c` (pre-pass) and
finally `FUN_1402b570c` (the iterative pass).

`FUN_1402b570c` (`decompiled/part_0002.c:1098`) is the actual fixpoint loop over
the task elements:

```text
do {
  changed = false
  for each element:
      if element->vfunc_0x170(mode) == 0:            # not skipped/manual?
          element->vfunc_0x208(worklist, mode)       # recalculate element
          if element->vfunc_0x128():                 # reports a change
              changed = true
} while (changed)
# then element->vfunc_0x1b8 / 0x1c8: project finish date update
```

**[B]** `FUN_1402b570c` calls offsets `+0x170`, `+0x208` and `+0x128`
(`decompiled/part_0002.c:1148-1158`), loops `while (bVar1)` and finishes with
`+0x1b8`/`+0x1c8`. The element list comes from `vfunc 0x38` on the recalc
object.

`FUN_1402afbe4` (`decompiled/part_0001.c:112316`) is the "needs another pass"
test: it returns 1 if any element's `+0x130` getter is non-zero. Slot 38 of
`TaskElement` is `FUN_1404e9400`, the task-type/effort mode getter; the
`TBkndTaskElem` slot-38 implementation is `FUN_14083fbd0`. So the repeat is
driven by a per-element flag the pass sets, not by a slack heuristic. The
semantics of that flag are **[O]** (`W3-P1`): candidate causes are a constraint
conflict, a summary being recomputed after children, or an assignment change.

`FUN_1402bb2b0` (`TBkndRecalc::XP_RecomputeOutline`, named at confidence 80)
runs after the passes: it first prepares every element (`vfunc 0xc0(0)`), then
walks the outline hierarchy and recomputes each summary (`+0x108`, `+0x110`,
`+0x118`, `+0x250`), then resets the project-level object
(`vfunc 0x220(0)`) (`decompiled/part_0002.c:4329`). This confirms that summary
rollup is a separate bottom-up phase after both CPM passes.

The graph is validated before the passes: `FUN_1404ac7b0`
(`decompiled/part_0005.c:9970`) walks every element's predecessor and successor
lists and reports "constraint not on pred list", "constraint not on succ list",
"converging linked lists" (strings `141133180-1411331f8`). Cyclic links must be
rejected, not iterated.

### 3.2 Inputs and normalisation

* Project start/finish, and the scheduling direction
  (`Project.ScheduleFromStart`); the direction chooses which pass runs first,
  but both always run.
* Every task: duration (tenths), calendar, constraint type/date, deadline,
  predecessors/successors with lag, type/effort flags, actuals.
* Manual and inactive tasks are skipped by the per-element `+0x170` test and
  keep their stored dates.
* All slack/date arithmetic is in working tenths on the task's calendar.

### 3.3 Forward pass (early dates)

Process tasks so every predecessor is done first; iterate the whole list if
needed (`FUN_1402b570c` already does).

```text
ES = project_start
for each predecessor link L:             # §4
    ES = max_date(ES, link_lower_bound(L))

switch constraint:
    SNET: ES = max(ES, constraintDate)
    MSO:  ES = constraintDate
    FNET: EF = max(AddWork(cal, ES, dur), constraintDate); ES = SubWork(cal, EF, dur)
    MFO:  EF = constraintDate; ES = SubWork(cal, EF, dur)
    otherwise: no lower bound

if actualStart: ES = max(ES, actualStart)
EF = AddWork(cal, ES, duration)
if actualFinish: EF = actualFinish
```

**Measured (W3-O4, 2026-10-22).** `python scripts/oracle_w3.py o4` ->
`docs/analysis/oracle/w3-o4.json`. Two tasks, T1 (1 d or 10 d) and T2 (1 d, FS
from T1), then T2's constraint type set 0..7 with the date Mon 2026-10-26 08:00
and `CalculateProject` after each. The unconstrained FS result is T2 =
Tue 10/20 08:00-17:00.

| constraint | T1 = 1 d (T2 early 10/20) | T1 = 10 d (T2 early 11/02) |
|---|---|---|
| MSO 2 | Start **10/26 08:00** exactly | Start **10/26 08:00**, before its predecessor finishes; slack **-2400** |
| MFO 3 | Finish **10/26 08:00** exactly (a mid-day instant, not a working boundary); Start 10/23 08:00 | Finish 10/26 08:00, Start 10/23 08:00; slack **-2880** |
| SNET 4 | Start **10/26 08:00** (floor) | Start **11/02** - a floor never pulls a task earlier |
| SNLT 5 | Start 10/20 (early), LateStart **10/26**, slack **+1920** (the bound relaxes the late dates) | Start clamped to **10/26** (early dates moved); slack **-2400** |
| FNET 6 | Finish **10/26 08:00** exactly | Finish 11/02 (floor, no effect) |
| FNLT 7 | Finish 10/20 (early), no effect | Finish clamped to **10/26 08:00**, Start 10/23; slack **-2880** |

So the **upper bounds (SNLT/FNLT) do clamp the early dates and can place a task
before its predecessor's finish, producing negative slack** - they are not
backward-pass-only. The clamp belongs in the forward pass as
`ES = min(ES, SNLT)` / `EF = min(EF, FNLT)`, with the late dates then derived
from the clamped early dates. The `[O]` tag `W3-P2` is settled.

Two further measured facts:

* **Assigning `ConstraintDate` to an ASAP task silently changes its type to
  SNET (4)** and the task moves to that date (`w3-o4b.json`, "ASAP probe:
  type 0 -> 0 -> 4"). A fixture builder that sets the date before the type
  contaminates an ASAP test.
* ALAP (1) with no successors lands on the project finish (measured 10/26 in the
  feasible case), but that run was state-dependent; treat ALAP's exact anchor as
  **[I]** until re-run from a fresh project with an independent project finish.

The deadline never moves the task.

### 3.4 Backward pass (late dates)

```text
LF = project_finish        # max EF, or the scheduling-from-finish date
for each task in reverse topological order:
    for each successor link L:
        LF = min_date(LF, link_upper_bound(L))

    switch constraint:
        SNLT: LF = min(LF, AddWork(cal, constraintDate, duration))
        FNLT: LF = min(LF, constraintDate)
        MSO/MFO: LF = EF; LS = ES            # pinned
        ALAP: LF = max(LF, EF); LS = LF      # as late as possible
        otherwise: pass

    if actualFinish: LF = min(LF, actualFinish)
    LS = SubWork(cal, LF, duration)
```

For ALAP tasks the *displayed* Start/Finish are `LS`/`LF`; their early dates
remain for slack computation. Pinned tasks have late = early.

**Measured (2026-10-22, W23).** For an **FF lead** the backward bound is also
clamped to the successor's late finish: `LF_pred = min(LF_pred,
align_back(sub_lag(...)))`. Without the clamp, w3-o3's FF-1d gives T1 LF
Tue 17:00 / slack 480, while Project reports Mon 17:00 / 0. The clamp is inert
for a non-negative lag (sub_lag already returns an earlier instant). The
clamp-to-`succ.LF` and clamp-to-project-finish readings are indistinguishable in
the measured rows (both have `succ.LF` = project finish); that shape is
inference, flagged in core/sched/README.md.

**Measured (`W3-P2` settled).** When the early dates already violate an upper
bound, Project **moves the task**: SNLT 10/26 on a task whose predecessor
finishes 10/30 gives Start 10/26 and TotalSlack -2400; FNLT 10/26 gives Finish
10/26 08:00 and -2880. The clamp is therefore in the forward pass, and the late
dates follow the clamped early dates.

### 3.5 Slack and criticality

All in working tenths on the task's calendar:

```
TotalSlack  = LS - ES            # == LF - EF when duration is fixed
StartSlack  = LS - ES
FinishSlack = LF - EF
FreeSlack   = min over successors of allowed slip, or 0 if no successors
```

`TotalSlack` **is** stored: the task row carries it at +74 as a signed i32 in
6-second ticks, and it is recomputed on every pass (`mpp-records.md` §2;
measured 2026-10-22 — `outline1.mpp`'s summary row stores 0 where StartSlack is
1920, so +74 is TotalSlack, not StartSlack). §0.5's "computed, not stored" is
wrong. `StartSlack`, `FinishSlack` and `FreeSlack` are also stored; `FreeSlack`
is the only one that depends on the successors' early dates.

**Measured (2026-10-22, W23).** Free slack is clamped to total slack:
`FreeSlack = max(min(raw_slip, TotalSlack), 0)`. The raw slip formula counts an
FF lead twice (FF-1d gives 480 where Project reports 0, its TotalSlack); no
other measured W3-O3/O4 row changes. The general negative-free-slack clamp
(W3-P3) is still untested.

Free slack per link type (the amount the task can slip without moving that
successor's early date):

```
FS: ES_succ - EF - lag
SS: ES_succ - ES - lag
FF: EF_succ - EF - lag
SF: EF_succ - ES - lag
take the minimum over successors
```

Whether negative free slack is clamped to 0 is **[O]** (`W3-P3`).

Critical: `TotalSlack <= CriticalSlackLimit * minutesPerDay`, the option being
in **days** (default 0). **Measured** (`w3-o4b.json`): a task with TotalSlack =
480 min is not critical at limit 0 and becomes critical at limit 1 and at
limit 2, so the comparison is `slack <= limit * 480` (inclusive) and the option
converts with `minutesPerDay` (§1.5). `Task.Critical` is stored (`0x0b400013`)
and recomputed every pass. Milestones are zero-duration tasks with `ES == EF`.

**Falsification W3-P5.** Chain A(2 d) → B(3 d), no constraints: TotalSlack A =
B = 0, FreeSlack A = 0. Add C(1 d) starting two days after A finishes: A's
TotalSlack stays 0 but FreeSlack becomes 2 days if C is the later successor.

### 3.6 Constraints, deadlines, ALAP

| constraint | forward effect | backward effect | notes |
|---|---|---|---|
| ASAP (0) | none | none | default |
| ALAP (1) | none | `LS = LF = project finish` | consumes all slack |
| MSO (2) | `ES = date` | pinned | |
| MFO (3) | `EF = date`, `ES = SubWork(EF,dur)` | pinned | |
| SNET (4) | `ES >= date` | none | what typing a start date creates |
| SNLT (5) | none | `LF <= AddWork(date,dur)` | conflict if violated |
| FNET (6) | `EF >= date` | none | |
| FNLT (7) | none | `LF <= date` | conflict if violated |

The setter path confirms the storage and defaults **[B]**
(`FUN_1401f2570`, `decompiled/part_0000.c:58748-58905`):

* Constraint type is at node `+0x84` (short), the date at `+0x78` (date32,
  `-1` = NA).
* Changing the type migrates the existing date between the start/finish-side
  fields (`+0x48`/`+0x4c` vs `+0x68`/`+0x6c`, depending on the scheduling
  direction), then calls `FUN_1406bbfbc(task, type)` to write the new type and
  `FUN_1406bbf0c(task, date)` to write the date
  (`decompiled/part_0012.c:42541,42515`).
* Setting either marks recalc (`node+0x140 |= 0x40000`) and calls
  `FUN_14083b3ac(task, 0)` which writes the "system constraint"
  (`0xb400467`) via a separate path (`decompiled/part_0016.c:41990`).
* The default date for a new date-bearing constraint is the task's start or
  finish on the active scheduling side; `FUN_140449494` is called when the
  stored date is NA.

Deadline (`0xb4001b5`): stored date32, **does not move the task**.
**Measured** (`w3-o4b.json`): deadline 10/20 08:00 against a 10/20 17:00 finish
gives TotalSlack **-480** (the 8 h of working time between them), FreeSlack 0,
dates unchanged; a deadline *after* the finish leaves slack at 0 and adds
nothing. So the deadline only ever lowers TotalSlack, in working time, and it
*is* reported as `TotalSlack` rather than only as a flag. `W3-P6` settled.

### 3.7 Summary rollup

After the two passes, `XP_RecomputeOutline` rolls up bottom-up by outline level
(§3.1). For each summary, over its non-blank children:

```
summary.ES   = min(child.ES)
summary.EF   = max(child.EF)
summary.Start = summary.ES
summary.Finish = summary.EF
summary.Duration = WorkTimeBetween(projectCal, summary.ES, summary.EF)
summary.Work = Σ child.Work
summary.ActualWork = Σ child.ActualWork
summary.RemainingWork = Σ child.RemainingWork
summary.Cost = Σ child.Cost + fixed cost
summary.%Complete = Σ actual duration / Σ duration          [I]
summary.%WorkComplete = Σ actual work / Σ work              [I]
```

**Measured (W3-O6, 2026-10-22).** `python scripts/oracle_w3.py o6` ->
`docs/analysis/oracle/w3-o6.json`. S1 typed 2 d, children C1 2 d from Mon
2026-10-19 08:00 and C2 1 d from Mon 2026-10-26 08:00 (a five-day gap), both
indented under S1:

| task | duration | start | finish | slack | critical |
|---|---|---|---|---|---|
| S1 summary | **2880 min = 6 working days** | 10/19 08:00 | 10/26 17:00 | 0 | True |
| C1 | 960 | 10/19 08:00 | 10/20 17:00 | 1920 | False |
| C2 | 480 | 10/26 08:00 | 10/26 17:00 | 0 | True |

So a summary's duration is the **working span** between min start and max finish,
not the sum of child durations (which would be 3 d). Outdenting both children
turns S1 back into a leaf with its stored 960 min — the rolled-up value is not
written back to the task's own duration. Work could not be distinguished (no
resources were assigned), so "Work = sum of children" remains **[I]**.
`Task.OutlineLevel`/`Task.Summary` are readable over COM (1/2 and True/False
above), which is how the fixture's tree was checked.

* A summary with all children blank stays blank; a summary with some blank
  children ignores the blanks for dates (strings "summary task with all blank
  children getting a date, making first child non blank" and "This is too late
  to change summary blank flag since we've already computed dates based on
  blank state", `analysis/meta/strings.tsv:4369,4329`).
* Summary constraints are not applied to the rollup.
* Summary criticality and slack are **[O]** (`W3-P7`): either recomputed for the
  summary or inherited ("critical if any child critical"). Record both.
* Summary duration is the working span on the project calendar, not the sum of
  child durations.

**Falsification W3-P8.** Children 2 d and 1 d, the second starting two days after
the first finishes: summary duration is the 5-day working span; work is 3 days.

### 3.8 Convergence

Both passes repeat until no element reports a change (`FUN_1402b570c`), and the
whole sequence repeats once more if `FUN_1402afbe4` says so, then the outline
rolls up. Implement:

```
repeat up to N (e.g. 50):
    changed = forward_pass()
    changed |= backward_pass()
    if not changed: break
if needs_another_pass(): forward_pass()      # binary does exactly one extra
rollup()
```

A hard iteration cap is mandatory: SF links plus constraints can fail to
converge, and MS Project stops with a circular-relationship error instead of
looping. The binary's graph check (`FUN_1404ac7b0`) is the guard.

---

## 4. Links: FS / SS / FF / SF with lag and lead

### 4.1 Record

Link type uses `PjTaskLinkType` numbering: **0 = FF, 1 = FS, 2 = SF, 3 = SS**.
The link table has 30 fields (`analysis/field_metadata_links.tsv`): four
Duration-typed fields (lag/lead, tenths), several date fields, booleans
(driving/critical), GUID/type fields. `TaskDependency.Lag` and `.LagType` exist
(`analysis/typelib.tsv:8952,8959`), so lag carries its own unit/elapsed flag.
Negative lag = lead.

### 4.2 Link bounds

Let `lag` be converted to working tenths on the successor's calendar (working
lag) or absolute tenths (elapsed lag). The lag calendar is **[I]**; `W3-L1`
decides it with different predecessor/successor calendars.

```text
AlignForward(cal, t) = t if t is inside a working interval, else next_start(cal, t)

# lower bounds for the forward pass
FS: bound = AddWork(succCal, AlignForward(predCal, pred.EF), lag)
SS: bound = AddWork(succCal, pred.ES, lag)
FF: finishBound = AddWork(succCal, AlignForward(predCal, pred.EF), lag)
    bound = SubWork(succCal, finishBound, succ.Duration)
SF: finishBound = AddWork(succCal, pred.ES, lag)
    bound = SubWork(succCal, finishBound, succ.Duration)

succ.ES = max(succ.ES, bound)

# upper bounds for the backward pass (invert the same formulas)
FS: LF_pred = min(LF_pred, SubWork(predCal, succ.LS, lag))
SS: LS_pred = min(LS_pred, SubWork(predCal, succ.LS, lag))
FF: LF_pred = min(LF_pred, SubWork(predCal, succ.LF, lag))
SF: LS_pred = min(LS_pred, SubWork(predCal, succ.LF, lag))
```

`AlignForward` is why FS with zero lag works: a predecessor finishing Tue 17:00
gives a successor starting Wed 08:00, not Tue 17:00. With a **lead** of one day,
the anchor is `Wed 08:00 - 480 = Tue 08:00`, not `Tue 17:00 - 480 = Mon 17:00`.
This "lag is counted from the next available working instant" rule is **[I]**;
`W3-L1` confirms or refutes it.

Special cases:

* `lag = 0` FS: successor starts at the next working instant after the
  predecessor's finish.
* Negative dates are allowed internally; clamp only for display.
* Elapsed lag ignores calendars (plain addition).
* A link to/from a summary uses the rolled-up start/finish.
* Cross-project links resolve the other project's task first; same formulas.

**Measured against MS Project (W3-O3, 2026-10-22).**
`python scripts/oracle_w3.py o3` -> `docs/analysis/oracle/w3-o3.json`. T1 = 1 d
Mon 2026-10-19 08:00-17:00, T2 = 1 d, one link T1->T2 per row,
`CalculateProject` after each:

| type | lag 0 | +1 d | -1 d | +2 h |
|---|---|---|---|---|
| FS (1) | T2 **10/20 08:00**-17:00 | 10/21 | **10/19** 08:00 (T2 occupies T1's own day) | 10/20 **10:00**-10/21 10:00 |
| SS (3) | 10/19 08:00 | 10/20 | **10/16 (Fri) 08:00** - a lead may precede the project start | 10/19 **10:00**-10/20 10:00 |
| FF (0) | T2 finish **10/19 17:00**, start 10/19 08:00 | finish 10/20 17:00 | 10/16 17:00 bound, **floored at the project start -> 10/19 08:00-17:00** | start 10/19 10:00, finish 10/20 10:00 |
| SF (2) | T2 finish **10/19 08:00** (= T1's start), start 10/16 08:00 | finish 10/19 **17:00**, start 10/19 08:00 (`AddWork(Mon 08:00, 1 d)`) | finish 10/16 08:00, start 10/15 | finish 10/19 **10:00**, start 10/16 10:00 |

This confirms the enum order **0 = FF, 1 = FS, 2 = SF, 3 = SS** behaviourally, and
confirms the §4.2 anchor rule: lag is counted from the **aligned** instant, so
FS + 2 h starts T2 at 10/20 10:00 (anchor = Tue 08:00, the next working instant
after T1's 17:00 finish, plus 2 h) rather than 10/19 17:00 + 2 h, and FS -1 d
gives 10/19 08:00. A lead may place a task before the project start: SS -1 d
starts T2 on Fri 10/16. **`W3-L1` is settled**: lag is working time anchored at
the aligned instant.

Both rows are now **measured and explained** by the `LagFormat` fixture
`scripts/oracle_wave7.py lagff` -> `docs/analysis/oracle/lagff-probe.json`
(`corpus/probe/lagff1.mpp`, with its own MSPDI export `corpus/probe/lagff1.xml`;
`LinkLag` is tenths of a minute, `LagFormat` 7 = working days, 8 = elapsed days).
T1 is 5 d Mon 10/19 08:00 - Fri 10/23 17:00:

* **FF negative lag is applied, and the placement is floored at the project
  start.** FF-1 d puts the successor Thu 10/22 08:00-17:00, FF-3 d Tue 10/20,
  FF+0 Fri 10/23. FF-6 d wants Thu 10/15 17:00, i.e. before the project start;
  Project places the successor Mon 10/19 08:00-17:00 (the project start). That
  floor is why the old 1-day `W3-O3` row looked like "lag dropped": the applied
  result was the previous Friday, before the project start, and clamping put it
  back on the predecessor's own day. Working-day and elapsed-day lags read the
  same on this pair (`LagFormat` 7 vs 8); the shifted day is a working day, so
  the fixture does not separate them.
* **SF +1 d is not anomalous.** `AddWork(Mon 08:00, 480) = Mon 17:00` the same
  working day, so the placed finish is Mon 10/19 17:00 and the start Mon 08:00 -
  exactly the old "zero-lag" row.

**Milestones (duration == 0).** A link start bound that lands on an interval end
is aligned forward for duration > 0 (§4.2 above), but **not** for a zero-duration
task. `scripts/oracle_wave7.py milestone` ->
`docs/analysis/oracle/milestone-probe.json` (`corpus/probe/milestone1.mpp`)
measures: 0 d FS+0 after a 1 d predecessor at 17:00 -> start == finish 17:00;
0 d FS+50 d -> 2026-12-28 17:00; 0 d SS+0 -> 08:00. Assigning `Start` =
Wed 17:00 to a 0 d task is a different path: Project normalises it to Thu 08:00
and stores SNET 2026-10-22 08:00. The measured exception is therefore specific to
link-derived start bounds, not to a `Start` assignment.

**Falsification W3-L2.** A 1 d Monday, B 1 d FS+1 d: B starts Wed 08:00 (not
Tue 17:00, not Thu). **Measured: 10/21 08:00 = Wed. Confirmed.**
**Falsification W3-L3.** Same with −1 d: B starts Mon 08:00.
**Falsification W3-L4.** SS+2 h: B starts 10:00 the same day.
**Falsification W3-L5.** FF+1 d: B's finish is one working day after A's; B's
start can precede A entirely.
**Falsification W3-L6.** SF 0: B cannot finish before A starts; B's start =
A.ES − B.duration.

### 4.3 Driving link and critical path

The driving predecessor is the link whose bound equals the successor's `ES`
(within a tenth); if several tie, the tie-break is **[O]** (`W3-L7`). The
critical path is `TotalSlack <= CriticalSlackLimit` (§3.5), not "longest path"
when constraints or deadlines are present.

---

## 5. What recalculates, and when

### 5.1 Triggers

| edit | scope |
|---|---|
| Task duration/work/units/type/effort | task + descendants (`SetDependantFields`) |
| Start/finish/duration text | task + connected component |
| Constraint type/date, deadline | task + connected component |
| Link add/remove/type/lag | both components |
| Calendar intervals/exceptions/base/project calendar | all tasks using it (whole project for the project calendar); invalidate week cache |
| Project start/finish, schedule-from-start, HoursPerDay/Week/Month, CriticalSlackLimit | whole project |
| Resource calendar/units/assignment add/remove | affected tasks, then project |
| Actuals/progress | task + successors |
| Manual/auto toggle, inactive | task + connected component |
| Calculate Now/F9, open, paste | whole project (full pass) |

### 5.2 Order

The binary fixes the order; follow it exactly:

1. Invalidate dependent caches (calendar weeks, rollups).
2. Pre-pass `FUN_1402afd7c` (collect elements, graph validation).
3. Pass in the scheduling direction (`Recalc(forward)` or `Recalc(backward)`).
4. Pass in the opposite direction.
5. If `FUN_1402afbe4` says a repeat is needed, one more pass in the scheduling
   direction with `full = 1`.
6. `XP_RecomputeOutline` — summary rollup bottom-up.
7. Validation/result propagation (`FUN_1402bc5d4`).
8. Notify the UI, clear dirty flags.

Steps 3-6 correspond one-to-one to the calls in `FUN_1402b5360`
(`decompiled/part_0002.c:974-991`).

### 5.3 Idempotence

A second recalculation with no input changes must produce identical dates. Call
`Calculate` twice in a fixture and diff every date field; any drift is a
convergence bug.

---

## 6. COM-facing numbers vs stored fields

| COM property | raw field | unit |
|---|---|---|
| Duration | `0x0b40001d` | minutes (double); internal tenths |
| DurationText | `0x0b400507` | formatted per `DurationFormat` |
| Work | `0x0b400000` | minutes |
| Start/Finish | `0x0b400023/24` | date32 |
| Early/Late | `0x0b400025-28` | date32 |
| TotalSlack | `0x0b400016` | **computed** (LS-ES), minutes |
| FreeSlack | `0x0b400015` | minutes, stored |
| StartSlack/FinishSlack | `0x0b4001b6/b7` | minutes, stored |
| Critical | `0x0b400013` | bool |
| ConstraintType/Date | `0x0b400011/12` | enum / date32 |
| Deadline | `0x0b4001b5` | date32 |

Slack is displayed in days by dividing by `minutesPerDay` (default 480) and
rounding only at the boundary. Whether COM rounds `TotalSlack` before returning
is **[O]** (`W3-P9`); compare raw stored StartSlack with a deliberately
non-integral slack (e.g. a 2-hour gap).

---

## 7. Out of scope for this spec

Resource leveling, splits, contours, EVM/cost, manual scheduling, master
projects, and `.mpp` serialisation. They consume the dates/work defined here; a
correct calendar + CPM base is the prerequisite. Leveling delay is a stored
duration added to a task's start; keep the hook but do not implement it in v1.

---

## 8. Oracle requests (coordinator runs; no COM here)

`scripts/sched_oracle_recipes.py` prints these as ready-to-run COM snippets. The
five that decide the most, in order:

**W3-O1 — finish instant and weekend skip.** New project, start Mon 2026-10-19,
standard calendar Mon–Fri 08:00–17:00. T1 duration 1 d starting 08:00; T2
duration 5 d starting Tue 08:00; T3 4 h starting Fri 15:00. Read `Start`,
`Finish`, `Duration`, raw `EarlyStart/EarlyFinish/LateStart/LateFinish`, and
`GetField(188743709)` (raw duration). Decides §1.4 and the tenths scale.

**W3-O2 — the duration/work/units triangle.** One task, standard calendar. For
each of the three task types, set `Duration = 3d`, then set `Work = 4h`, then
set `Units = 50%`; after each edit read `Duration`, `Work`, `Units`,
`DurationText`. Two runs: one assignment, then two assignments with
`EffortDriven` off/on. Decides §2.3 and the internal units scale (also dump the
assignment record if practical).

**W3-O3 — lag anchor.** T1 1 d Mon, T2 1 d. Set T2's predecessor T1 with each
link type and lag ∈ {0, +1 d, −1 d, +2 h}, all on the standard calendar. Read
T1/T2 Start/Finish after `Calculate`. Decides §4.2 and the enum ordering.

**W3-O4 — constraints and slack.** T1 1 d, T2 1 d FS from T1. For each
constraint type 0..7 on T2 with date 2026-10-26 08:00: read
Start/Finish/Early*/Late*/TotalSlack/FreeSlack/Critical and
`ConstraintType`/`ConstraintDate`. Repeat with a predecessor that makes the
constraint infeasible (T1 10 d). Add a deadline one day before T2's finish and
read slack again. Decides §3.3-§3.6 and negative-slack behaviour.

**W3-O5 — elapsed and options.** `1 eday` starting Fri 08:00; `1 eweek`;
`1 emon`; with `HoursPerDay = 10`, `1 d`. Read Start/Finish/Duration. Then set
a task calendar with a 4-day week (Mon–Thu) and confirm the weekend shift.
Decides §1.5, §2.2 elapsed constants and calendar resolution.

Secondary: **W3-O6** summary rollup with a gap and criticality; **W3-O7** free
slack with two successors and a negative slip; **W3-O8** `CriticalSlackLimit =
2d`; **W3-O9** 0/50/100 % complete; **W3-O10** two passes of `Calculate`
(idempotence).

---

## 9. Evidence index

| topic | evidence |
|---|---|
| date32 packing | `decompiled/part_0000.c:67381,67449`; `part_0001.c:35481`; `part_0003.c:58667`; `analysis/flat_buffer_field_map.tsv:21` |
| weekday / epoch | `decompiled/part_0001.c:29423`; `analysis/flat_buffer_field_map.tsv:21` |
| day-pattern layout | `decompiled/part_0000.c:67449`; `part_0016.c:68658` |
| effective week | `decompiled/part_0000.c:69674`; `part_0001.c:35481` |
| WorkTimeBetween | `decompiled/part_0002.c:65390`; `part_0001.c:35689`; `part_0016.c:68597` |
| AddWork (forward) | `decompiled/part_0001.c:35481`; `part_0000.c:67381` |
| SubWork (backward) | `decompiled/part_0003.c:58667,58928,58990,59035,112304` |
| recalc orchestration | `decompiled/part_0002.c:955` |
| recalc pass / fixpoint | `decompiled/part_0002.c:1022,1098`; `part_0001.c:112316` |
| outline rollup | `decompiled/part_0002.c:4329` |
| recalc labels | `analysis/meta/strings.tsv:4337-4338` |
| constraint enum | `analysis/typelib.tsv:39-46`; `analysis/meta/strings.tsv:6436-6446` |
| constraint setter/defaults | `decompiled/part_0000.c:58126,58748-58905`; `part_0012.c:42515,42541`; `part_0016.c:41990` |
| link enum | `analysis/typelib.tsv:4394-4397` |
| link fields | `analysis/field_metadata_links.tsv` |
| format units / parser | `analysis/typelib.tsv:3194-3213`; `analysis/res/pjintl_strings.tsv:17377-17384` |
| task types | `analysis/typelib.tsv:4398-4400`; `decompiled/part_0007.c:21184` |
| duration/work/units fields | `analysis/field_metadata_task.tsv`, `..._assignment.tsv`; `analysis/com_validation.tsv` |
| SetDependantFields | `analysis/meta/strings.tsv:13015-13016,16227-16230`; `decompiled/part_0007.c:10676,10703` |
| slack storage | `scripts/phase2d_slack_verify.py:110-114` |
| graph validation | `decompiled/part_0005.c:9970` |
| calendar virtual interface | `analysis/meta/vtables.tsv` vtable `140fffa18` (9 slots) |
| hours/week/month options | `analysis/typelib.tsv:9359` |
| elapsed classes | RTTI `.?AVElapsedDayReader@@`, `analysis/meta/strings.tsv:14680,15654` |
