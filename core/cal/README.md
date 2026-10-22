# core/cal — base calendars from `114/TBkndCal`

Decodes the base calendars of an MPP14 file (work-week day patterns, dated
exceptions, dated work weeks, names) and builds a `core::sched::Calendar` from
one of them.  `core/plan` consumes it through
`core::cal::load_project_calendar()`; `core/sched` is not modified by this
stream, so dated exceptions are installed as per-week `set_work_week` overrides.

Everything below is **verified** against files on disk unless it says
**inference**.  The end-to-end proof is `tests/cal_tests.cpp`: it rebuilds the
three task dates MS Project calculated for `corpus/probe/holiday1.mpp` (the
oracle is `docs/analysis/oracle/holiday-probe.json`).

## Stream inventory (measured)

`TBkndCal` is a fixed-row table plus Var2Data payloads; since MPP14 stores the
whole file in one `114` storage, the paths are `114/TBkndCal/<stream>`.

| stream | base.mpp | holiday1.mpp | w3-o1.mpp | project_a.mpp |
|---|---|---|---|---|
| `FixedData` | 100 | 100 | 88 | 460 |
| `FixedMeta` | 86 | 86 | 76 | 386 |
| `VarMeta` | 36 | 60 | 36 | 372 |
| `Var2Data` | 22 | 566 | 22 | 154 292 |
| `Fixed2Data` | 336 | 336 | – | – |
| `Fixed2Meta` | 86 | 86 | – | – |

The project defaults are in the sibling stream `114/Props` (88 690 bytes for
`base.mpp`).

Identities that hold in every file measured:

```
rows          = (len(FixedData) - 64) / 12
FixedMeta len = 16 + 10 * (rows + 4)
VarMeta  len  = 24 + 12 * entries
```

## Byte layout

### `114/TBkndCal/FixedMeta` — 16-byte header + 10-byte entries

```
+0   u32 magic 0xFADFADBA (bytes ba ad df fa)
+4   u32 version 4
+8   u32 entry count
+12  u32 FixedData byte size
entry (10 bytes): +0 u16 tag, +2 u16 field count, +4 u32 byte offset into
                  FixedData, +8 u16 block length
```

`base.mpp` (86 bytes, 7 entries):

```
0000  ba ad df fa 04 00 00 00 07 00 00 00 64 00 00 00
0010  04 00 00 00 00 00 00 00 10 00     tag 4, offset 0,  length 16
001a  04 00 00 00 10 00 00 00 10 00     tag 4, offset 16, length 16
0024  04 00 00 00 20 00 00 00 10 00     tag 4, offset 32, length 16
002e  04 00 00 00 30 00 00 00 10 00     tag 4, offset 48, length 16
0038  00 00 01 00 40 00 00 00 8f 00     row 0, offset 64
0042  00 00 00 00 4c 00 00 00 0e 00     row 1, offset 76
004c  00 00 00 00 58 00 00 00 0e 00     row 2, offset 88
```

The first four entries describe the four 16-byte header descriptors at
FixedData 0/16/32/48; the rest are rows.  This decoder reads rows from
`FixedData`'s own size and uses FixedMeta only as a cross-check
(`CalendarTable::metadata_anomalies`), so a damaged metadata stream cannot shift
the rows.

### `114/TBkndCal/FixedData` — 64-byte header + 12-byte row per calendar

```
+0  i32 base calendar uid    <= 0 means "this row is a base calendar"
+4  i32 resource uid         -1 (0xFFFFFFFF) means "not a resource calendar"
+8  i32 calendar uid         > 0, the key into VarMeta / Var2Data
```

`base.mpp` (100 bytes; 1 base + 2 resource calendars):

```
0040  00 00 00 00 ff ff ff ff 01 00 00 00   base 0, res -1, uid 1 = Standard
004c  01 00 00 00 00 00 00 00 02 00 00 00   base 1, res  0, uid 2
0058  01 00 00 00 01 00 00 00 03 00 00 00   base 1, res  1, uid 3
```

`holiday1.mpp` row 2 is `00 00 00 00 ff ff ff ff 03 00 00 00`: the synthetic
SiteCal is stored as a **root** calendar (base uid 0), and its days that do not
override anything inherit the project default week (see `114/Props`).

### `114/TBkndCal/VarMeta` — 24-byte header + 12-byte entry

```
+0  u32 magic 0xFADFADBA      +8  u32 entry count
+4  u32 0                     +12 u32 0
+16 u32 0                     +20 u32 Var2Data size
entry: u32 uid | u32 Var2Data offset | u32 field key
```

Two keys are used: **`0x0D400001`** (low word 1) = calendar name, and
**`0x0D400008`** (low word 8) = calendar data block.  The decoder compares the
low 16 bits, which is what the stored keys and mpxj's `VarMeta12` agree on.

`holiday1.mpp` (60 bytes, 3 entries):

```
0018  01 00 00 00 00 00 00 00 01 00 40 0d   uid 1, off 0,   name "Standard"
0024  03 00 00 00 22 02 00 00 01 00 40 0d   uid 3, off 546, name "SiteCal"
0030  03 00 00 00 16 00 00 00 08 00 40 0d   uid 3, off 22,  data (520 bytes)
```

A Var2Data block is `u32 byte length` followed by that many bytes.  Names are
UTF-16LE NUL-terminated; the length includes the terminator.

`base.mpp` has **one** VarMeta entry (`uid 1, offset 0, key 0x0D400001`), i.e.
the Standard calendar has a name and **no data block at all**.

### `114/Props` — 16-byte header + records, and where Standard really lives

```
+0 16-byte header, record count as u16 at +12
record: u32 data length | u32 key | u32 unused | data[length]  (+1 pad if odd)
```

Two keys matter:

| key | name | value |
|---|---|---|
| `37753736` (`0x02401388`) | `DEFAULT_CALENDAR_HOURS` | 420 bytes = seven 60-byte day blocks |
| `37748750` (`0x0240000E`) | `DEFAULT_CALENDAR_NAME` | UTF-16LE project default calendar name |

**The Standard base calendar's week is not in `TBkndCal`.**  Because it has no
Var2Data block, its seven day patterns are the project's
`DEFAULT_CALENDAR_HOURS` record.  `holiday1.mpp`, `base.mpp` and PROJECT_A all carry
the same 420 bytes there:

```
day0 (Sun) day type 0, count 0                       non-working
day1..day5  type 0, count 2, starts 4800 / 7800,     Mon-Fri 08:00-12:00 +
            lengths 2400 / 2400, total 4800           13:00-17:00
day6 (Sat) day type 0, count 0                       non-working
```

A decoder that only reads `TBkndCal` therefore returns a **non-working**
Standard week.  `stock_default_week()` is the fallback used only when
`DEFAULT_CALENDAR_HOURS` is absent; every file on disk has it (inference: the
fallback is MS Project's stock default, not a measured file behaviour).

### The calendar data block (`Var2Data` key `0x0D400008`)

```
  0 .. 419   seven 60-byte day blocks, index 0 = Sunday
 420         u16 exception count
 count == 0  -> offset becomes 424
 count != 0  -> offset becomes 424, then per exception:
                92-byte record + 4-byte-aligned UTF-16 name
 then        u32 work-week header
 then        work-week records until the block runs out:
                7 x 60-byte day blocks, u16 start day, u16 finish day,
                8 unknown bytes, u32 name length (+ padded name)
```

Consumption is exact on all 21 calendar data blocks on disk (`end == length`),
which is what validates the framing.

### 60-byte day block

```
+0  u16 day type: 1 = "use the inherited times", anything else = custom
+2  u16 interval count (only meaningful for a custom day)
+4  u32 total working tenths of a minute
+8  u16 start[i],  tenths of a minute from midnight      (i = 0..count-1)
+20 u32 length[i], tenths of a minute                    (i = 0..count-1)
```

`holiday1.mpp`'s SiteCal data block, Friday (block offset `0x12c`, 60 bytes):

```
012c  00 00 01 00 60 09 00 00 c0 12 00 00 00 00 00 00
013c  00 00 00 00 60 09 00 00 00 00 00 00 00 00 00 00
014c  00 00 00 00 00 00 00 00 60 09 00 00 00 00 00 00
015c  00 00 00 00 00 00 00 00 00 00 00 00
```

* `+0 = 0` custom; `+2 = 1` one interval; `+4 = 0x0960 = 2400` tenths = 240 min;
* `+8 = 0x12c0 = 4800` tenths = 480 min = **08:00**;
* `+20 = 0x0960 = 2400` = 240 min = **4 h**, so Friday is 08:00-12:00.

The other six blocks start with `01 00 00 00` (day type 1) and are *not* read
further: in real files the rest of an inherited block is stale buffer content
(PROJECT_A's blocks contain fragments of unrelated strings there).  The tail of a
custom block can also hold stale copies of lengths — only `[0, count)` is
meaningful.  Arithmetic cross-check on `project_a.mpp` uid 102 day 2
(three intervals): lengths 1800+2400+600 = 4800 = the total at `+4`.

### Exception record (92 bytes + name)

```
+0  u16 start day (inclusive)        +14 u16 interval count
+2  u16 end day (inclusive)          +20 u16 start[i], tenths
+4  u16 occurrences (recurring)      +32 u32 length[i], tenths
+72 u16 PjExceptionType              +88 u32 name byte length (4-byte aligned)
+92     UTF-16LE name
```

`holiday1.mpp`'s one exception starts at block offset `0x1a8`:

```
01a4  01 00 00 00                    exception count = 1
01a8  13 3d 13 3d 01 00 00 00 ...    start = end = 0x3d13 = 15635 = 2026-10-21
```

`+14` (interval count) is 0, so the day is **non-working**.  A special working
day carries intervals there and is stored the same way.  `PjExceptionType`
**0 and 1 are a plain dated range** (measured: all 1331 exceptions in PROJECT_A and the
one in holiday1 are type 1); 2..7 are genuinely recurring and this decoder
reports them (`CalendarTable::recurring_exceptions`) and expands the derivable
ones (wave 9b: `expand_recurring_exception`, see the W41 note at the end).
Exception names are **skipped on purpose**: only the padded length is used, so
no client label can reach a caller, a tool or a test.

### Work-week record

`7 x 60` day blocks + `u16 start day, u16 finish day, 8 unknown bytes, u32 name
length (+ padded name)`.  **No file on disk contains one** (`0` in every probe
and in both real files), so this part of the layout is [inference]: it follows
the same reader in mpxj (`AbstractCalendarAndExceptionFactory.processWorkWeeks`)
and is exercised by a hand-built block in `cal_tests`.  Re-run the whole
inventory with `cal_tests` and read `note:` lines, or with the snippet below.

### `Fixed2Data` / `Fixed2Meta`

Present for this table (`holiday1`: `Fixed2Data` 336 = 7 x 48,
`Fixed2Meta` 86 = 16 + 7 x 10) and not decoded: they hold per-row GUIDs and
identities, which calendar arithmetic does not need.

## The fixture, end to end

`holiday1.mpp` has three calendars (2 base + 1 resource).  Both base names are
synthetic — the coordinator built the file over COM with T1..T3.

| task | duration | calendar behaviour | Project's dates (oracle) |
|---|---|---|---|
| T1 | 2 d | none of the changes matter | Mon 10-19 08:00 → Tue 10-20 17:00 |
| T2 | 5 d | FS after T1; Wed 10-21 skipped; Friday 4 h | Thu 10-22 08:00 → Thu 10-29 12:00 |
| T3 | 1 d | Fri 4 h, the day continues Monday | Fri 10-23 08:00 → Mon 10-26 12:00 |

`cal_tests` decodes SiteCal, builds the `core::sched::Calendar`, and reproduces
all three with `add_work` / `next_start`.  Note what the file says about *which*
calendar: `114/Props` still names **Standard** as the project default, while
each task row carries **task-calendar uid 3 (SiteCal)** at `TBkndTask/FixedData`
row offset **+178** (i32, `-1` = NA).  The fixture's dates are therefore proof
of the task calendar; the oracle JSON's `project_calendar: "SiteCal"` is not
corroborated by the saved file.  PROJECT_A (85 rows) and PROJECT_B (86 rows) have `-1`
in every row, so the real programme needs only the project calendar.

## PROJECT_A / PROJECT_B counts (no names)

The project default calendar of `corpus/project_a.mpp` is **name
withheld: client data** (the name carries a company/project identifier).  Only
its structure is reported, and nothing in this directory, in `cal_tests` or in
`memory/cal.md` contains a client calendar, exception or work-week name.

`corpus/project_a.mpp`:

* 33 calendar rows = **10 base calendars + 23 resource calendars**;
* 10 data blocks (one per base calendar), **17 custom day records** (16 with
  working time) across those blocks — the other 53 days inherit;
* **0 dated work-week overrides**;
* **1331 dated exceptions**: 1322 non-working + 9 special working days, all
  stored as one plain range each; days **6603..14976** (2002-01-28 .. 2024-12-31),
  614 distinct start days;
* recurring exceptions: 0; intervals that are not whole minutes: 0;
* the project default calendar resolves by name and builds, installing **228
  per-week overrides** (234 of the 1331 exceptions are on that one calendar;
  exceptions sharing a week merge into one override).

`corpus/project_b.mpp`: same 33 / 10 / 23 shape, 10 data blocks,
17 custom days, 0 work weeks, **1333** exceptions (1323 non-working, 10 working),
days 6603..18351.  The two files share 9 calendar uids and their exception sets
overlap; they are not the same file.

## API, defaults and failure behaviour

```cpp
namespace core::cal {
    // all declarations of the table, plus the project default week/name
    CalendarTable decode_file(std::string const& mpp_path);
    CalendarTable decode_compound_file(mpp::CompoundFile const& file);

    // one calendar -> the engine's model
    core::sched::Calendar build_calendar(CalendarTable const&, CalendarDefinition const&);
    core::sched::Calendar build_project_calendar(CalendarTable const&, std::string const& name = {});
    core::sched::Calendar load_project_calendar(std::string const& mpp_path, std::string const& name = {});
    core::sched::Calendar load_project_calendar(mpp::CompoundFile const&, std::string const& name = {});
}
```

* `name` empty = the project's default calendar: `114/Props`
  `DEFAULT_CALENDAR_NAME`, matched exactly then ASCII case-insensitively.  In a
  client file that name is itself client data (it can carry a company/project
  identifier): it is decoded only so lookup works, and is never printed by this
  stream's test or written to any committed file.
* `name` given but absent -> **throws `mpp::Error`** (no silent fallback).
* the default name absent from the table -> falls back only when the file has
  exactly one base calendar, otherwise **throws `mpp::Error`**.
* a missing `114/TBkndCal/FixedData`, a `FixedData` that is not `64 + 12*n`
  bytes, or a short Var2Data block -> **throws `mpp::Error`**.
* damaged-but-readable metadata (FixedMeta/VarMeta counts or sizes that
  disagree with FixedData) does **not** throw: it increments
  `CalendarTable::metadata_anomalies` and rows are still read from `FixedData`.

Exception mapping: every dated exception (and dated work week) is merged into
the seven `DayPattern`s of the week it falls in — `Calendar::set_work_week` — so
`core/sched` needs no new API.  Two exceptions in the same week merge into that
week; an exception wins over a dated work week for the same day; a derived
calendar's exceptions win over its base's.  This was checked against the oracle
above; it cannot represent a *recurring* exception, which is why those are
counted and skipped rather than guessed.

## Re-running the evidence

```
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul && cmake -S . -B build\vs-w25 -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w25 --config Release --target cal_tests"
build\vs-w25\core\cal\Release\cal_tests.exe
```

Standalone: `cmake -S core\cal -B <dir> -G "Visual Studio 17 2022" -A x64`
builds `core_cal` and `cal_tests` after adding `core/mpp` and `core/sched` with
their tests off.

`CAL_NO_CORPUS=1` forces the corpus-gated cases to skip (38 checks instead of
165), the same convention as `MPP_NO_CORPUS` in the reader tests.

To dump the raw streams without this code (read-only; prints no client names):

```
python -c "import olefile,struct;ole=olefile.OleFileIO('corpus/probe/holiday1.mpp');\
[print(k,len(ole.openstream(p).read())) for p in ole.listdir(streams=True) \
 if (k:='/'.join(x.strip() for x in p)).startswith('114/TBkndCal')]"
```

## Limits

* MPP14 only (this is the format of every file in the corpus).  MPP12 uses
  different FixedMeta entry sizes and a different id-offset order.
* Recurring exceptions (types 2..7): the recurrence PARAMETER words are not
decoded, so a derivable type (2..6) is expanded at period 1 and checked against
the +4 occurrence word, while type 7 is refused.  See the W41 note at the end.
* `core::sched::DayPattern` stores minutes, so a sub-minute interval would be
  rounded; `CalendarTable::sub_minute_intervals` counts any (0 in every file).
* Calendar names are decoded because lookup needs them; nothing here writes a
  name to a file, and no test or tool prints one from a corpus file.

## W41 note (wave 9b): recurring exceptions

The record's recurrence PARAMETERS are still not decoded (weekday mask, "every
N", position, month/day); what is now implemented is the derivable half:

* `DatedException::occurrences` decodes the +4 word (a check, never a source of
  truth — its identity is [I]: no file on disk has a recurring exception).
* `expand_recurring_exception()` expands PjExceptionType 2..6 from the range's
  first day at the shortest period (every week/month/year): yearly month+day,
  yearly positional, monthly month+day, monthly positional, weekly.  Type 7
  (every N days) is refused because N is not derivable.  The expansion is refused
  (with a stable `why`) when the type is not recurring, the range is empty, the
  expansion exceeds the caller's day cap, or the +4 occurrence word disagrees
  with the expanded count.
* `build_calendar(table, calendar, RecurrenceReport*)` applies an expansion
  exactly like a plain dated range and counts `applied` / `not_applied`.

`tests/cal_tests.cpp` pins the expansion (weekly/monthly/yearly/positional/
month-existence), every refusal, and the builder application on synthetic
records — no file on disk has a recurring exception, so the parameter words are
the named wave-10 inference (`memory/cal.md`: the `w9-r-*` fixture set).
