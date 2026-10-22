# memory: gantt stream

_Updated 2026-10-22 (W35: Gantt + real calendar). One session, no workers. No git
commands were run: the files are on disk for the coordinator to commit._

## State: implemented, built with MSVC /W4 /WX, tested

| File | Contents |
|---|---|
| `core/gantt/gantt.h` | `Timescale` (unit, origin, px/unit, header cells, snap, week anchor), `RowLayout`, `Bar`, `LinkRoute` + `ArrowHead`, `GanttStyle`, `WorkingWeek` (legacy), `WorkingDayPredicate`, `GanttSource` (+ calendar / predicate), `GanttLayout` + `hit_test`, `is_working_day`, `nonworking_spans`, status marker, `build_gantt()` |
| `core/gantt/gantt.cpp` | all of the above; calendar day-pattern -> non-working x spans; no drawing, no windowing system, no external dependency |
| `core/gantt/CMakeLists.txt` | target `core_gantt` (links `core_sched`); `gantt_tests` links `core_cal` **when that target exists** (root build) and is then compiled with `GANTT_HAS_CAL` + `GANTT_PROBE_DIR`; standalone build omits both |
| `tests/gantt_tests.cpp` | **644 checks** in the root build (597 without `core_cal`); fixed day scale 20 px/day, row height 22 |

The model depends only on `core/sched` (`date.h` for `date32`, `calendar.h` for
`Calendar`/`DayPattern`). It does **not** depend on `core/view`; the shell maps a
`RecordSet` row to `TaskBarInput` (uid, start, finish, percent, kind, critical,
label) and a link record to `LinkInput`. That mapping is deliberately the
caller's job: the Gantt must not learn field ids.

The only corpus contact is the gated holiday probe at the bottom of the test
(`corpus/probe/holiday1.mpp`, a synthetic COM-built fixture). When the file is
absent the test prints `SKIP` and counts nothing, so a corpus-less checkout still
passes. `corpus/probe/holiday1.mpp` was present this session (237,568 bytes).

## Additive API added this session (W32: nothing removed or renamed)

```cpp
using WorkingDayPredicate = std::function<bool(date32)>;   // day has any working time

struct GanttSource {
    // ... existing members unchanged ...
    std::shared_ptr<const core::sched::Calendar> calendar;  // shading, not geometry
    WorkingDayPredicate working_day;                        // whole-day fallback
    void set_calendar(const core::sched::Calendar&);        // copies; layout owns it
};

struct NonworkingSpan {
    date32 date; double x; double width;   // unchanged
    bool partial = false;                  // NEW: intra-day non-working stretch
};

struct GanttLayout {
    // ... existing members unchanged ...
    std::shared_ptr<const core::sched::Calendar> calendar;  // copied from source
    WorkingDayPredicate working_day;
    bool is_working_day(date32 day) const;                   // NEW
};

struct Timescale {
    // ... existing members unchanged ...
    bool adopt_calendar_week_start = true;   // NEW: week columns follow the calendar
};
```

New members are **appended** (`GanttSource` after `status_date`, `GanttLayout`
after `has_status`, `Timescale` after `week_start`, `NonworkingSpan` after
`width`), so positional aggregate initialisation of the old fields still
compiles for W32.

Precedence for shading (documented at the top of `GanttSource` in `gantt.h`):
calendar > `working_day` predicate > legacy `working_week`; `nonworking_days` is
always applied on top and always shades the whole day. `working_week` is ignored
when a calendar or predicate is set.

## Verification (W35, last run 2026-10-22)

MSVC 19.44.35229.0, generator "Visual Studio 17 2022", x64, Release,
`/W4 /permissive- /utf-8 /WX` via `-DCORE_WARNINGS_AS_ERRORS=ON`. Build clean,
no warnings. Never `--parallel`.

```powershell
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul && cmake -S . -B build\vs-w35 -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w35 --config Release --target gantt_tests"
.\build\vs-w35\core\gantt\Release\gantt_tests.exe
# -> "gantt_tests: 644 checks passed"   (380 before this session; +264)

$ctest='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
& $ctest --test-dir build\vs-w35 -C Release -R "^gantt$" --output-on-failure
# -> 1/1 Test #25: gantt ... Passed; 100% tests passed
```

The executable is under `build\vs-w35\core\gantt\Release\`, **not**
`build\vs-w35\Release\` (an earlier memory entry had that wrong).

Standalone (the no-`core_cal` compile path; proves the `#ifdef` and the CMake
conditional, and gives the corpus-free check count):

```powershell
cmd /c "call `"$vc`" >nul && cmake -S core\gantt -B build\vs-w35\standalone -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w35\standalone --config Release"
.\build\vs-w35\standalone\Release\gantt_tests.exe    # "gantt_tests: 597 checks passed"
```

644 - 597 = **47 checks come from the holiday probe**. The runtime SKIP branch
(file absent) is not exercised this session; it is three lines around an
`ifstream` test.

## Measured vs decision

### Measured (reused from the calendar stream; this stream asserts it end to end)

`holiday1.mpp`'s task calendar (the synthetic fixture's calendar, uid 3; name
`SiteCal` is synthetic, not client data) has exactly one dated exception --
Wed 2026-10-21 non-working, `PjExceptionType` 1 -- and a 4-hour Friday
(08:00-12:00; every other day inherits the 8-hour default week). `tests/cal_tests.cpp`
measured this; `core::cal::build_calendar` turns the exception into a work-week
override for the week of 18-24 Oct. The new `test_holiday_probe` asserts:

- `day_total_ticks`: Wed 21 = 0, Thu 22 = 4800 (8 h), Fri 23 = 2400 (4 h).
- `nonworking_spans(0, 180)` on a timescale whose origin is Mon 19 Oct:
  whole-day spans = Wed 21 (x 40, w 20), Sat 24 (x 100), Sun 25 (x 120);
  partial spans on Fri 23 = 00:00-08:00 (x 80, w 20*8/24) and 12:00-24:00
  (x 90, w 10).
- Bar rects are identical with and without the calendar (all bars compared;
  the nine-day bar is x 0 w 180, the short-Friday task x 80 w 20).
- **No calendar name is printed**, and only counts/geometry derived from the
  synthetic fixture are committed.

### Decisions (ours; no oracle)

1. **Nonworking spans from a calendar are the complement of the day's working
   intervals**, not just whole non-working days. A normal 08:00-17:00 day
   therefore yields three intra-day spans (00:00-08:00, 12:00-13:00,
   17:00-24:00) marked `partial == true`; a fully non-working day is one span
   with `partial == false`. Rationale: at an hour-granularity renderer Project
   does shade lunch and evenings, so the model keeps the data complete and lets
   the view decide. **A day-granularity renderer must draw only
   `partial == false` spans** (that is the weekend/holiday look). This is a
   decision, not a measurement of Project's per-zoom behaviour.
2. **Week start is data-driven when a calendar is present**: `build_gantt`
   adopts `week_start` from `Calendar::effective_week(...).week_start_day`.
   `core::sched` anchors its effective week on Sunday (0), which is also MS
   Project's stock work week, so a calendar-driven week starts Sunday. Without
   a calendar the default stays ISO Monday (`week_start = 1`). Opt out with
   `Timescale::adopt_calendar_week_start = false`. Caveat: the .mpp work-week
   record stores a start day, but `core::sched::Calendar` does not expose it, so
   the "data" here is the engine's Sunday anchor, not a decoded per-project
   value. Tests pin Sunday 18 Oct (calendar) vs Monday 19 Oct (no calendar).
3. **Milestone centring: settled as column-centred** (`x = day_start +
   (day_width - 12)/2`), not date-line-centred. It stays inside the day cell,
   never clips at x = 0, and never spills into the previous day; a date-line
   centre would be half a diamond left and is unmeasured. Pinned by
   `test_milestone_centring` (4 px clearance each side).
4. **Summary bar end caps are left OPEN and not modelled.** Their shape/size is
   unmeasured, so `GanttStyle` says so and `Bar::rect` is the plain rectangle;
   the shell may derive decorative caps from it. No guessed geometry was pinned.
5. **Backward FS and SF share one six-point lane shape** (exit, stub on the exit
   side, lane, approach, entry), and the lane is the midpoint of the two row
   centre lines however far apart the rows are. Pinned for adjacent rows
   (existing test) and now for rows two apart (lane y = 33, arrow tip on the
   successor's left edge). Unmeasured against the product: whether Project
   routes at the midpoint or below both rows.
6. **`nonworking_days` still forces a whole-day span when a calendar is set**
   (explicit holiday wins over the calendar for that day).

## Geometry decisions carried over (unchanged unless noted above)

The real product's Gantt geometry is not in the resources at all
(`spec/ui-inventory.md` §8), and the oracle is coordinator-only. Everything
below is a decision pinned by `tests/gantt_tests.cpp`, not evidence.

1. **Calendar-day x axis, not working days.** Sat/Sun occupy a full day column
   each; a 5-working-day task Fri-Thu is 7 columns. Non-working time is data for
   shading, not compression.
2. **Whole-day bars.** Bar left = 00:00 of the start day, right = 00:00 of the
   day after the finish day. 1-day task = exactly one column.
3. **Timescale is piecewise linear for months** (equal-width month columns);
   `pixels_per_day()` is NaN for the month unit.
4. **Origin snapping** moves back to the unit start containing the earliest
   task start, never forward. Week columns: calendar anchor when a calendar is
   supplied, Monday otherwise (decision 2 above).
5. **Rows**: `row_height = 22`, `y_for_row = row * row_height`,
   `row_at_y` floors and returns -1 above the first row. `visible_rows` includes
   partial rows and clamps to the row count.
6. **Bars**: `bar_height = 12`, `summary_height = 8`, vertically centred.
   Fill = left-aligned `w * percent_complete`, clamped 0..1.
7. **Milestone**: 12x12 diamond bounding box centred in its own day column
   (settled; decision 3 above).
8. **Labels**: `label_gap = 4`; inside (centred) when it fits and the bar is not
   a milestone; otherwise right of the bar. Milestones always label right.
9. **Links**: FS right->left, SS left->left, FF right->right, SF left->right at
   bar centre lines. Normal route is a Z; a "behind" target detours through the
   inter-row lane; SF always detours. Arrow tip on the successor edge, wings
   `arrow_length = 6`, `arrow_width = 4`.
10. **Hit testing**: bars first (rect + `hit_tolerance = 3`), then links
    (point-to-polyline distance); first match wins.
11. **Critical** is caller data; nothing here computes CPM.
12. **Status line** is a vertical marker at `x_for_date(status_date)`; time of
    day honoured. The stepped progress line is not modelled.
13. **Legacy non-working shading** (no calendar, no predicate): weekend mask
    (default Mon-Fri) plus `nonworking_days`; unchanged, and the 380 checks from
    before this session still pass.

## Coordinates the shell must understand

- Bar/row space: x = 0 at the snapped origin, y = 0 at the top of row 0. Header
  height is excluded; the shell adds/subtracts it.
- Header cells are in the same x units; tier `major` on top, tier `minor` below.
- y grows downwards (Core Graphics default); no points/DPI conversion.

## What the macOS shell still has to provide

1. The view: scroll/clipping/header heights; call `header(tier, x0, x1)` and
   `nonworking_spans(x0, x1)` for the visible x window.
2. Drawing for normal/summary/milestone bars, the fill, the label, and
   `LinkRoute::points` + `ArrowHead`. Summary end caps are the shell's own
   decoration for now.
3. Colours/styles, including critical/baseline look (the model carries only
   `critical`).
4. A real font metric via `GanttStyle::measure_text`.
5. Row-to-task binding: `GanttSource::tasks` index = row; keep sheet order.
6. Scroll sync: `layout.visible_rows(scroll_y, height)`; content height
   `row_count * row_height`, content width `layout.content_width`.
7. Shading policy: draw whole-day spans always; draw `partial` spans only when
   the zoom resolves sub-day time (decision 1).
8. Interaction (drag/resize) is not modelled; convert pixels through
   `Timescale::date_at_x` and re-run `build_gantt`.

## Open items, ranked

1. **Everything numeric is still unmeasured** (row height, bar height, milestone
   size, link gap, arrow size, status x). Needs a coordinator oracle session;
   pin the new numbers in the tests and delete "decision:" notes.
2. **Link shapes need a picture-level check**, especially SS/SF exit sides and
   whether the backward-FS lane is the midpoint (our pin) or below both rows.
3. **Summary bar end caps** (decision 4 above): unmodelled, needs a screenshot.
4. **No hour timescale unit.** `TimescaleUnit` is day/week/month, so partial
   shading can be *drawn* at day zoom but not *labelled* at hour zoom. Add an
   `hour` unit if the shell needs the product's per-hour Gantt.
5. **Per-task and resource calendars.** `GanttSource` takes one calendar; the
   caller must choose it. The reader does not yet expose the task calendar uid at
   task row `+178` (coordinator open item 3), so per-task selection cannot be
   wired end to end yet.
6. **Week anchor source.** `core::sched::Calendar` always anchors on Sunday;
   the .mpp work-week start-day field is decoded by `core/cal` but not surfaced
   through `Calendar`. A project whose week genuinely starts on another day
   cannot be expressed yet.
7. **Split tasks and elapsed/estimated markers** are out of scope by the brief
   (`BarKind` has no split member deliberately).
8. **Progress line** (stepped slip line) is not modelled; only the vertical
   status marker.

## Traps hit

- **Calendar shading is the full complement, not one span per day.** Every
  working day contributes intra-day gaps; tests must filter `partial` when they
  mean the day-granularity look. The first version of the W35 tests failed
  exactly this way.
- **Do not store a raw pointer to a `Calendar` in the source.** The layout is
  used after `build_gantt` returns (drawing), so `GanttSource::set_calendar`
  copies the calendar into a `shared_ptr<const Calendar>` owned by the layout.
- **Span generation drops spans that end at or before x0** but returns the span
  containing x0 whole (same as header cells); do not assume exactly one span per
  day index.
- **Week-anchor adoption must run before `Timescale::snap_origin`**, because the
  anchor chooses the week column. Done in `build_gantt`; keep that order.
- **date32 day 1 is 1984-01-01 (a Sunday)**; a week unit snapping back from it
  would ask for 1983-12-26, which packs into a wrapped 16-bit day. The default
  origin is Monday 1984-01-02, `snap_origin` refuses a snap below `kMinDay`, and
  `date_at_x` returns NA before the domain.
- **Equal-width month columns** mean `pixels_per_day()` cannot exist for the
  month unit; partial spans are positioned through `x_for_date(pack(day, tenths))`,
  never by adding px/day.
- Bar geometry is whole-day; `x_for_date` (instant) and `x_for_day_start` (grid)
  are intentionally different.
- `context.equal` must not stream values (several comparisons are `enum class`;
  MSVC instantiates the failure branch even when it never runs).
- MSVC `/W4 /WX` rejects a loop variable that shadows an outer local (`C4456`);
  rename rather than suppress. Also avoid partially initialised aggregates
  (`-Wmissing-field-initializers` on clang/GCC): `whole_day_span()` builds one.
- The `gantt_tests` executable lives in `build\vs-w35\core\gantt\Release\`.
- Dropbox rewrites mtimes: `edit` may report "file changed since read"; re-read
  immediately before each edit.
- clang/GCC are not installed on this machine; the CI-only warning risk was
  handled by review (no unused file-scope symbols, every member value-init,
  `-Wmissing-field-initializers` avoided), not by a local run.

## Next three concrete actions

1. Have the coordinator run one oracle session for the Gantt's numbers (item 1)
   and for one summary-bar screenshot (item 3); pin them and drop the
   "decision:" notes that become evidence.
2. Add an `hour` timescale unit (item 4) so partial shading and the product's
   hour-level Gantt can be labelled; reuse `Timescale::x_for_date` with a
   time-of-day fraction rather than a new axis.
3. When the reader exposes the task-calendar uid at `+178` (item 5), build one
   `Calendar` per task calendar and pass the right one per row; that needs a
   per-row shading source (either a windowed union or a `TaskBarInput` field),
   which is a bigger API step than this session's single-layout calendar.

## Wave 9b (W42) — zoom breadth, bar labels, shapes, zoom hit testing

Additive only: no existing function, struct field or enum value changed.

- **Zoom layer**: `GanttZoom { day, week, month }`, `ZoomSpec` (unit, default
  px/unit, name, header vocabulary), `zoom_spec()`, `zoom_name()`,
  `timescale_for_zoom()` and `zoom_for_timescale()`. Defaults are **decision**:
  day 20 px/day, week 140 px/week, month 300 px/month (the same numbers the old
  tests used). Header text is the existing `Timescale::header()`: day "22" /
  "October 2026"; week "19 Oct" / "October 2026"; month "October" / "2026".
- **Bar labels**: `BarLabelContent` + `GanttStyle::bar_label` (default `name`,
  so the old API is unchanged) and `bar_label_text()`. Modes: none, name,
  resource, dates, percent, name+percent, name+resource, name+dates.
  `TaskBarInput::resource` is appended for resource labels. **Decision,
  unmeasured**: the set of modes (the product's BarLabelDialog is a UIFILE class
  but its defaults are not in the resources) and the date spelling
  (`YYYY-MM-DD`, or `start - finish` when they differ). The percent label is
  clamped and rounded to a whole number.
- **Shapes**: `BarShape { rectangle, diamond, summary_bracket }` and
  `Bar::diamond[4]`, `Bar::cap_left[3]`, `Bar::cap_right[3]`. The summary bar
  gets two downward end-cap triangles (clamped to half the bar width);
  `GanttStyle::summary_cap_width/height` = 4 px. **Decision, unmeasured**: the
  cap shape. The milestone diamond is 12 px at day/week; at the month zoom a
  31-day column is ~9.7 px, so the diamond shrinks to the day column and stays
  inside it (the old "never spills into the previous day" rule now holds at
  every zoom). `Bar::rect` is unchanged in meaning.
- **Zoom-aware hit testing**: `GanttStyle::hit_tolerance_days = 0.15`,
  `min_hit_tolerance = 2`, `max_hit_tolerance = 8`; `hit_test_tolerance()` uses
  the day-column width (for month, the first day of the origin month). Day/week
  20 px × 0.15 = 3 px (the old fixed value), month ~9.68 px × 0.15 → the 2 px
  floor. Setting `hit_tolerance_days = 0` keeps the old fixed
  `hit_tolerance`. `GanttLayout::hit_test()` now calls it, so the old 3/4 px
  pins at the day zoom still pass.
- **Routing at zoom**: geometry is unchanged and remains screen-space (the
  `link_gap` is a pixel clearance, not a date distance). New tests assert at the
  month zoom that the route still leaves/enters the correct bar edges, the arrow
  direction is a unit vector, the bounds are finite and the route is
  hit-testable. **Open**: no per-zoom link-gap scaling; the product's zoom
  routing is unmeasured.
- **Calendar shading** is asserted at the week and month zooms as well as the
  existing day zoom: standard weekends are whole-day spans, a 4-hour Friday's
  gaps are partial, and with the calendar-driven week anchor disabled the bars
  are identical with and without a calendar. The calendar still never moves a
  bar; the week **anchor** adoption legitimately moves columns, so the shading
  comparison pins `adopt_calendar_week_start = false`.

Verification (last run 2026-10-22, `build\vs-w42`):

```powershell
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\vs-w42 --config Release --target gantt_tests'
.\build\vs-w42\core\gantt\Release\gantt_tests.exe
# -> "gantt_tests: 733 checks passed"   (wave-8 baseline 644; +89)
$ctest='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
& $ctest --test-dir build\vs-w42 -C Release --output-on-failure -R "view|gantt"
# -> 2/2 passed (view_model, gantt)
```

Open after W42: bar-label content/format and summary cap shape need an oracle
screenshot; link-gap scaling per zoom is not modelled; no `hour` timescale unit;
per-task calendars still blocked on the reader's task-calendar uid.

