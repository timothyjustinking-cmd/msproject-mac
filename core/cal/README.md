# core/cal — base calendars from `TBkndCal` (MPP12 and MPP14)

Decodes the base calendars of an MPP file — work-week day patterns, dated
exceptions, dated work weeks, names — and builds a `core::sched::Calendar` from
one of them.  **Both storage layouts are supported**: MPP14 (Project 2010+) under
`114/` and MPP12 (Project 2007) under `112/`.  The four-part table framing is
identical; only the table root and the column order of a FixedData row differ,
and `decode_compound_file()` detects the layout by itself, so callers do not pass
a version.  `core/plan` consumes it through `core::cal::load_project_calendar()`;
`core/sched` is not modified by this stream, so dated exceptions are installed as
per-week `set_work_week` overrides.

Everything below is **verified** against files on disk unless it says
**inference**.  The end-to-end MPP14 proof is `tests/cal_tests.cpp`: it rebuilds
the three task dates MS Project calculated for `corpus/probe/holiday1.mpp` (the
oracle is `docs/analysis/oracle/holiday-probe.json`).  The MPP12 proof is the
same test binary on `corpus/probe/mpp12calendar.mpp`: the decoded identities are
cross-checked against the independent `core/mpp` reader, and a real schedule is
run through `core::sched::schedule_tasks` with the decoded calendar.

## MPP12 vs MPP14 (measured)

| | MPP14 (Project 2010+) | MPP12 (Project 2007) |
|---|---|---|
| table root | `114/` | `112/` |
| project props | `114/Props` | `112/Props` |
| FixedData 12-byte row | `+0` base uid, `+4` resource uid, `+8` calendar uid | `+0` calendar uid, `+4` base uid, `+8` resource uid |
| FixedMeta | 16-byte header + 10-byte entries, 4 descriptors | identical |
| VarMeta / Var2Data | 24-byte header + `(uid, offset, key)`; length-prefixed blocks | identical |
| calendar data block, 60-byte day block | below | identical |
| `DEFAULT_CALENDAR_HOURS` / `DEFAULT_CALENDAR_NAME` keys | `0x02401388` / `0x0240000E` | identical |

**Detection** (`core::cal::detect_calendar_layout`, and the default
`CalendarLayout::automatic`) mirrors `core/mpp/reader.cpp`'s "Version detection"
rule, so the two libraries cannot classify one file differently:

1. a top-level `Props12` stream means MPP12, `Props14` means MPP14;
2. when both exist, the `112/` vs `114/` `TBkndTask` storage decides;
3. when neither exists, the `TBkndTask` storage decides; if there is no task
   table at all (a synthetic container), core/cal falls back to which
   `TBkndCal/FixedData` exists;
4. finally the table that is actually present wins: if the chosen root has no
   `TBkndCal/FixedData` but the other root does, the other root is read.

A caller can force `CalendarLayout::mpp12` / `mpp14`; that also forces the table
root, so forcing the wrong layout either reads the other tree or throws.  A
forced layout is reported as `CalendarTable::layout_detected == false`.

Measured on `corpus/probe/mpp12calendar.mpp` (MPXJ's public MPP12 test file;
synthetic names): `112/TBkndCal` = FixedData **160** (`64 + 12*8`), FixedMeta
**136** (`16 + 10*12`), VarMeta **144** (`24 + 12*10`), Var2Data **2484**;
`112/Props` is 84 106 bytes.  The eight rows are
`(uid, base, resource)` = `(1,-1,-1) (2,1,0) (4,1,2) (5,-1,-1) (6,-1,-1)
(7,-1,-1) (8,1,3) (9,-1,-1)`; reading the same bytes with the MPP14 order gives
`(base, resource, uid)` = `(1,-1,-1) (2,1,0) ...`, i.e. the wrong identities.
Five calendars have names (uids 1, 5, 6, 7, 9); five have a var2 data block
(uids 4, 6, 7, 8, 9).  The reader's format rule decides MPP12 from the top-level
`Props12` stream and its `Document::calendars` lists the same eight
`uid/base/resource/name` rows.

The default week is the MPP12 point that was verified rather than assumed:
`112/Props` key `0x02401388` holds the same 420-byte stock week as MPP14
(Mon–Fri 08:00–12:00 + 13:00–17:00, weekend non-working) and key `0x0240000E`
holds `Standard`, so a `TBkndCal`-only decoder is not needed and the two formats
agree on the default calendar.

The two resource calendars (uids 4 and 8) store a week whose seven days are all
day type 1 ("inherit") plus one non-working exception on day 8339
(2006-10-31).  The 420-byte week area is stale buffer text (fragments such as
`ResourceCalendarEditDays`, i.e. an old "Change Working Time" field list) and
must **not** be read as intervals — which the day-type rule already guarantees.
Both blocks are 520 bytes = 420 + 4 + 92 + 4 and consume exactly, so they are
valid data blocks, not damage.

`metadata_anomalies` is 0 for `mpp12calendar.mpp`.  A data block whose exception
count cannot fit in the remaining bytes is reported as `truncated` and its
exception list is refused rather than decoded out of stale text
(`decode_calendar_data`); that guard is format-independent.

## Stream inventory (measured)

`TBkndCal` is a fixed-row table plus Var2Data payloads; MPP14 stores the whole
file in one `114` storage and MPP12 in one `112` storage, so the paths below are
`<root>/TBkndCal/<stream>`.  The MPP14 sizes are the original table; the MPP12
sizes are from `mpp12calendar.mpp`.

| stream | base.mpp | holiday1.mpp | w3-o1.mpp | project_a.mpp |
|---|---|---|---|---|
| `FixedData` | 100 | 100 | 88 | 460 |
| `FixedMeta` | 86 | 86 | 76 | 386 |
| `VarMeta` | 36 | 60 | 36 | 372 |
| `Var2Data` | 22 | 566 | 22 | 154 292 |
| `Fixed2Data` | 336 | 336 | – | – |
| `Fixed2Meta` | 86 | 86 | – | – |

The project defaults are in the sibling stream `<root>/Props` (88 690 bytes for
`base.mpp`, 84 106 for `mpp12calendar.mpp`).

Identities that hold in every file measured, both formats:

```
rows          = (len(FixedData) - 64) / 12
FixedMeta len = 16 + 10 * (rows + 4)
VarMeta  len  = 24 + 12 * entries
```

## Byte layout

### `TBkndCal/FixedMeta` — 16-byte header + 10-byte entries

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

### `TBkndCal/FixedData` — 64-byte header + 12-byte row per calendar

MPP14 row (the order every example below uses):

```
+0  i32 base calendar uid    <= 0 means "this row is a base calendar"
+4  i32 resource uid         -1 (0xFFFFFFFF) means "not a resource calendar"
+8  i32 calendar uid         > 0, the key into VarMeta / Var2Data
```

MPP12 row (`112/TBkndCal/FixedData`; the same three i32s, different order --
MPXJ's MPP12CalendarFactory declares the calendar id first, the MPP14 factory
the reverse):

```
+0  i32 calendar uid         > 0, the key into VarMeta / Var2Data
+4  i32 base calendar uid    -1 or 0 means "this row is a base calendar"
+8  i32 resource uid         -1 (0xFFFFFFFF) means "not a resource calendar"
```

`decode_calendar_rows(fixed_data, layout)` is the public row decoder; the
CI-safe test builds the same three logical rows in both orders and checks that
reading MPP12 bytes under the MPP14 order changes the identity columns.

`base.mpp` (MPP14, 100 bytes; 1 base + 2 resource calendars):

```
0040  00 00 00 00 ff ff ff ff 01 00 00 00   base 0, res -1, uid 1 = Standard
004c  01 00 00 00 00 00 00 00 02 00 00 00   base 1, res  0, uid 2
0058  01 00 00 00 01 00 00 00 03 00 00 00   base 1, res  1, uid 3
```

`holiday1.mpp` row 2 is `00 00 00 00 ff ff ff ff 03 00 00 00`: the synthetic
SiteCal is stored as a **root** calendar (base uid 0), and its days that do not
override anything inherit the project default week (see `<root>/Props`).

### `TBkndCal/VarMeta` — 24-byte header + 12-byte entry (identical under 112/ and 114/)

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

### `<root>/Props` — 16-byte header + records, and where Standard really lives

```
+0 16-byte header, record count as u16 at +12
record: u32 data length | u32 key | u32 unused | data[length]  (+1 pad if odd)
```

The layout and both keys are identical in `114/Props` (MPP14) and `112/Props`
(MPP12); the MPP12 defaults were decoded on `mpp12calendar.mpp` and hold exactly
the stock week and the name `Standard` shown below.

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

Consumption is exact on all 21 MPP14 calendar data blocks on disk (`end ==
length`), and on the five MPP12 blocks in `mpp12calendar.mpp` / `mpp12resource.mpp`
(`end == length` there too), which is what validates the identical framing.  An
exception count that cannot fit in the remaining bytes is refused as `truncated`
(the count word is then stale text, not a count).

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
+4  u16 occurrences                  +32 u32 length[i], tenths
+72 u16 PjExceptionType              +74 u16 recurrence parameter words 0..6
+88 u32 name byte length (4-byte aligned)
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
reports them (`CalendarTable::recurring_exceptions`) and expands them (wave 9b
`expand_recurring_exception`, parameters measured in wave 12 -- next section).
Exception names are **skipped on purpose**: only the padded length is used, so
no client label can reach a caller, a tool or a test.

### Recurrence parameter words (wave 12, measured)

The seven `u16` words at **+74 .. +86** carry the recurrence parameters.  The
fixture is `corpus/probe/w12-exceptions.mpp` (synthetic, one exception of every
`PjExceptionType` on the project calendar) and the COM read-back of the same
seven records is `docs/analysis/oracle/w12-exceptions.json`.  Dump them with

```
python tools/cal_exception_dump.py corpus/probe/w12-exceptions.mpp --words
```

Layout, per type (the fixture's value in brackets; `+76`/`+77`/`+78` are byte
views of those words):

| type | meaning | +76 | +77 | +78 |
|---|---|---|---|---|
| 1 | daily, every day | 0 | 0 | 0 |
| 2 | yearly, month + day | Month-1 [2 -> March] | MonthDay [15] | 0 |
| 3 | yearly, positional | Month-1 [10 -> November] | MonthPosition [4 = last] | MonthItem [8 = Friday] |
| 4 | monthly, day | MonthDay [15] | 0 | Period [1] |
| 5 | monthly, positional | MonthPosition [0 = first] | MonthItem [0 or 4] | Period [1] |
| 6 | weekly | DaysOfWeek bitmap [10 = Mon\|Wed] | 0 | Period [1] |
| 7 | every N days | Period (u16) [1] | - | - |

`MonthItem` is PjMonthItem, Sunday-first (`3` = Sunday .. `9` = Saturday, so
`8` = Friday); `MonthPosition` is PjMonthPosition (`0` = first .. `3` = fourth,
`4` = last); `DaysOfWeek` is a bitmap with bit 0 = Sunday.  The `+80` word is
**not** a parameter: it is a per-record ordinal and was 1..7 across the seven
records in creation order; `+74`, `+82`, `+84` and `+86` are 0 in all seven.
`DatedException::raw_recurrence_words` keeps all seven words verbatim.

Cross-check, not source: mpxj's `AbstractCalendarAndExceptionFactory.
readRecurringData` reads the same words with the same meanings (`+76`/`+77`/
`+78`, `getInstance(value - 2)` for the Sunday-first `MonthItem`, `+78` for the
weekly frequency).  Fetched 2026-10-22 from
<https://raw.githubusercontent.com/joniles/mpxj/master/src/main/java/org/mpxj/mpp/AbstractCalendarAndExceptionFactory.java>
(the file was read, not vendored; nothing from it is compiled here).
The measurement that makes this the format, not a guess, is
the occurrence word at `+4`: Project recomputes it, and **all seven recurring
records across the two fixtures agree with the expansion** built from these words
(18/7/5/4/5/4 days plus 8 in the second fixture; the type-1 plain range is 29).
The yearly positional record is the sharpest: last Friday of November 2026..2030
gives 2026-11-27, 2027-11-26, 2028-11-24, **2029-11-30**, and excludes 2030-11-29
(past the stored Finish).

**Monthly positional has two measured shapes.**  The first fixture's record
(`FirstMonday`) stores 0 in both the MonthPosition and MonthItem bytes, and COM
reads `MonthPosition 0` / `MonthItem 0` back: that record's start day
(2026-11-02) is itself the first Monday, so only the weekday (from the start day,
which is a matching instance) is derived, and the stored count 7 agrees.  The
second fixture (`corpus/probe/w12-exceptions-monthpos.mpp`, COM JSON
`docs/analysis/oracle/w12-monthpos.json`) separates them: one type-5 record with
Start **2026-11-01 (a Sunday)**, Finish 2027-06-30, Occurrences **8**,
MonthPosition **2 (third)**, MonthItem **4 (Monday)**, Period 1.  Its words are
`+76 = 0x0402` (position 2, month item 4) and `+78 = 1`; the expansion is the
third Monday of each month (2026-11-16, 12-21, 2027-01-18, 02-15, 03-15, 04-19,
05-17, 06-21) -- 8 days, which is Project's own count.  So MonthItem/MonthPosition
are **stored and used as stored** whenever they are non-zero; only a zero
MonthItem falls back to the start day's weekday.

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
    // all declarations of the table, plus the project default week/name.
    // `layout` defaults to automatic detection; the existing one-argument
    // calls are unchanged.
    CalendarFormat detect_calendar_layout(mpp::CompoundFile const& file);
    std::vector<CalendarRow> decode_calendar_rows(std::vector<std::uint8_t> const& fixed_data,
                                                  CalendarLayout layout);
    CalendarTable decode_file(std::string const& mpp_path,
                              CalendarLayout layout = CalendarLayout::automatic);
    CalendarTable decode_compound_file(mpp::CompoundFile const& file,
                                       CalendarLayout layout = CalendarLayout::automatic);

    // one calendar -> the engine's model
    core::sched::Calendar build_calendar(CalendarTable const&, CalendarDefinition const&);
    core::sched::Calendar build_project_calendar(CalendarTable const&, std::string const& name = {});
    core::sched::Calendar load_project_calendar(std::string const& mpp_path,
                                                std::string const& name = {},
                                                CalendarLayout layout = CalendarLayout::automatic);
    core::sched::Calendar load_project_calendar(mpp::CompoundFile const&,
                                                std::string const& name = {},
                                                CalendarLayout layout = CalendarLayout::automatic);
}
```

`CalendarLayout` is `automatic` (default), `mpp12` or `mpp14`.  A decoded table
reports what was used in `CalendarTable::layout`, `table_root` and
`layout_detected`; a forced layout also forces its table root, so forcing the
wrong one either reads the other tree or throws.

* `name` empty = the project's default calendar: `<root>/Props`
  `DEFAULT_CALENDAR_NAME`, matched exactly then ASCII case-insensitively.  In a
  client file that name is itself client data (it can carry a company/project
  identifier): it is decoded only so lookup works, and is never printed by this
  stream's test or written to any committed file.
* `name` given but absent -> **throws `mpp::Error`** (no silent fallback).
* the default name absent from the table -> falls back only when the file has
  exactly one base calendar, otherwise **throws `mpp::Error`**.
* a missing `<root>/TBkndCal/FixedData`, a `FixedData` that is not `64 + 12*n`
  bytes, or a short Var2Data block -> **throws `mpp::Error`**.
* damaged-but-readable metadata (FixedMeta/VarMeta counts or sizes that
  disagree with FixedData) does **not** throw: it increments
  `CalendarTable::metadata_anomalies` and rows are still read from `FixedData`.
* a container with no calendar table at all is refused by
  `detect_calendar_layout()` and by the auto-detect path of the decoders.

Exception mapping: every dated exception (and dated work week) is merged into
the seven `DayPattern`s of the week it falls in — `Calendar::set_work_week` — so
`core/sched` needs no new API.  Two exceptions in the same week merge into that
week; an exception wins over a dated work week for the same day; a derived
calendar's exceptions win over its base's.  This was checked against the oracle
above; it cannot represent a *recurring* exception, which is why those are
counted and skipped rather than guessed.

## Re-running the evidence

```
$cmake='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake -S . -B build\cal-w19 -G "Visual Studio 17 2022" -A x64 -DCORE_BUILD_TESTS=ON -DCORE_WARNINGS_AS_ERRORS=ON
& $cmake --build build\cal-w19 --config Release --target cal_tests
build\cal-w19\core\cal\Release\cal_tests.exe
```

Current counts (wave 19): **785 checks** from `cal_tests` with the corpus, **323**
with `CAL_NO_CORPUS=1` (the corpus-gated cases print `SKIP` and still pass), the
same convention as `MPP_NO_CORPUS` in the reader tests.  Wave 19 added the
MPP12/MPP14 row-order (CI-safe), MPP12 table/reader agreement, MPP12-vs-MPP14
`Standard` agreement and the MPP12-driven schedule cases.

### What `cal_tests` checks for MPP12

No corpus is needed for the first group; `CAL_NO_CORPUS=1` skips the rest.

* **Row order / detection (CI-safe)**: `tests/mpp_fixture_builder.h` builds an
  in-memory CFB with the same three calendars written in both layouts; both
  decode to the same `uid/base/resource/name`, and the MPP12 bytes read under
  the MPP14 order are asserted to move the columns (the negative control).  The
  same test covers `Props12`/`Props14` detection, the `112`/`114` `TBkndTask`
  fallback, the calendar-only fallback and the "no table" refusal.
* **`mpp12calendar.mpp`**: auto-detected MPP12, 8 rows (5 base + 3 resource),
  0 metadata anomalies, `112/Props` default week and name; every row
  `uid/base/resource/name` equals `mpp::load_file()`'s `Document::calendars`;
  forcing MPP14 throws.
* **MPP12 vs MPP14**: `Standard` from `mpp12calendar.mpp` and from `base.mpp`
  build the identical week and both schedule 2 d from Fri 08:00 to Mon 17:00.
* **Real schedule driven by MPP12 data**: `Night Shift` (uid 7, its own
  Var2Data week) is installed in `core::sched::schedule_tasks`; a 2 d task from
  the Fri 08:00 project start runs Fri 23:00 -> Tue 08:00 across the weekend,
  while the same task on `Standard` runs Fri 08:00 -> Mon 17:00.  uid 9's
  short-day week schedules an 8 h task Mon 09:00 -> Tue 09:00.
* **`mpp12task.mpp`**: 3 rows with the expected `(uid, base, resource)` identity
  and reader agreement (second witness for the column order).

Standalone: `cmake -S core\cal -B <dir> -G "Visual Studio 17 2022" -A x64`
builds `core_cal` and `cal_tests` after adding `core/mpp` and `core/sched` with
their tests off.

To dump the raw streams without this code (read-only; prints no client names;
use `112/TBkndCal` instead of `114/TBkndCal` for an MPP12 file):

```
python -c "import olefile,struct;ole=olefile.OleFileIO('corpus/probe/holiday1.mpp');\
[print(k,len(ole.openstream(p).read())) for p in ole.listdir(streams=True) \
 if (k:='/'.join(x.strip() for x in p)).startswith('114/TBkndCal')]"
```

## Limits

* Both formats are supported: MPP14 (`114/`) and MPP12 (`112/`).  The MPP12
  week data is decoded with the same 60-byte day-block layout and was measured
  on `corpus/probe/mpp12calendar.mpp` (5 blocks) and `mpp12resource.mpp`;
  MPP12's two resource-calendar blocks in `mpp12calendar.mpp` are all-"inherit"
  weeks whose day area holds stale text, so only the day-type word is read there.
  MPP12 identity uses the reader's detection rule; there is no separate
  MPP12-only API.
* Recurring exceptions (types 2..7): every parameter word is decoded and applied
  (wave 12, "Recurrence parameter words" above).  The one gap is the weekday of a
  *monthly* positional record, which Project does not store; it is derived from
  the start day and checked against the +4 occurrence word.
* `core::sched::DayPattern` stores minutes, so a sub-minute interval would be
  rounded; `CalendarTable::sub_minute_intervals` counts any (0 in every file).
* Calendar names are decoded because lookup needs them; nothing here writes a
  name to a file, and no test or tool prints one from a corpus file.

## W41 note (wave 9b): recurring exceptions

Superseded by the wave-12 section above for everything except the history: W41
decoded the +4 occurrence word as a check and expanded types 2..6 from the
range's first day at the shortest period (`every week/month/year`), refusing type
7 because the "every N days" period was not decoded then.  The refusals it
introduced -- not recurring, empty range, day cap exceeded, occurrence word
disagreement -- are all still there; the parameter guards (a MonthItem that is
not a weekday, a MonthPosition outside 0..4) are new in wave 12.

`tests/cal_tests.cpp` pins the expansion (weekly/monthly/yearly/positional/
month-existence, plus leap day, month-end, an absent fifth weekday and a
zero-occurrence range in wave 12), every refusal, the builder application, and
the fixture `corpus/probe/w12-exceptions.mpp` against the COM values in
`docs/analysis/oracle/w12-exceptions.json`.
