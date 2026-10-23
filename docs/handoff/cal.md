# Wave 19 — stream CAL: MPP12 calendars are real

Status: **done** (2026-10-23).  Wave 19 / stream CAL.  Owned paths only:
`core/cal/**`, `tests/cal_tests.cpp`, `build/cal-w19/**`, `docs/handoff/cal.md`,
`memory/cal.md`.  `core/mpp` was read, never edited; no git command was run (the
coordinator commits).  The tree builds (`/W4 /WX` clean) and `cal_tests` is green.

## Problem

`core/cal` hard-coded MPP14: it read `114/TBkndCal/*` and `114/Props`, and its
row decoder assumed the MPP14 column order (`base_uid +0, resource_uid +4,
uid +8`).  An MPP12 (Project 2007) file stores the same table under `112/` and
its props under `112/Props`, and its 12-byte row is `uid +0, base_uid +4,
resource_uid +8`.  Decoding an MPP12 file with the old code either threw (no
`114/TBkndCal`) or mis-routed identities if the streams were renamed.  The
`core/mpp` reader already exposed `Document::version/table_root/calendars`; this
wave makes `core/cal` consume the same information without a public `core/mpp`
API change and without editing `core/mpp`.

## What changed

### `core/cal/cal.h`

* `enum class CalendarLayout { automatic, mpp14, mpp12 }` and
  `struct CalendarFormat { layout, table_root, props_stream }`.
* `CalendarFormat detect_calendar_layout(mpp::CompoundFile const&)` — the
  detection rule as a public, documented call.
* `struct CalendarRow { uid, base_uid, resource_uid }` and
  `std::vector<CalendarRow> decode_calendar_rows(fixed_data, layout)` — the
  public byte-level row decoder used for the row-order proof.  It requires an
  explicit layout (a raw buffer carries no version) and throws `mpp::Error`
  otherwise.
* `CalendarTable` gained `layout` (what was decoded), `table_root` (`"114"` or
  `"112"`) and `layout_detected` (false when the caller forced the layout).
* Layout parameters, **defaulted to `automatic`**, on `decode_compound_file`,
  `decode_file` and both `load_project_calendar` overloads.  All existing
  one-argument call sites compile unchanged.
* Top-of-file layout documentation now covers both formats; the
  `CalendarDefinition` field comments name the MPP14 offset and the MPP12
  meaning.

### `core/cal/cal.cpp`

* `choose_format()` mirrors `core/mpp/reader.cpp`'s "Version detection" rule
  exactly: `Props12` / `Props14` presence first; when both/neither exist the
  `112/` vs `114/` `TBkndTask` storage decides.  One extra fallback for
  calendar-only/synthetic containers (which `TBkndCal/FixedData` exists), and a
  final "the tree decides" correction: if the chosen root has no
  `TBkndCal/FixedData` but the other does, the other root is read.
* `decode_compound_file()` derives the table root/props stream from that choice,
  decodes rows with `decode_calendar_rows`, and fills
  `CalendarTable::{layout, table_root, layout_detected}`.  The "default calendar
  is not in ..." error names the actual root.
* `decode_calendar_data()` now refuses an exception count that cannot fit in the
  remaining bytes (`truncated`) instead of decoding one garbage record out of
  stale text.  No measured file changes behaviour (all previous tests pass).

### `tests/cal_tests.cpp`

* Includes `cpm.h`, `reader.h` and the reader tests' header-only
  `tests/mpp_fixture_builder.h` (read only, not edited).  `cfb_writer` was not
  used: `mppwrite::CfbDocument` deliberately cannot create a stream (it only
  replaces payloads of an existing container), while the fixture builder can
  assemble a container from scratch in memory.
* `test_calendar_row_layouts` (**CI-safe**): builds the same three logical
  calendars in both layouts (raw FixedData + a whole in-memory CFB) and asserts
  identical routing, plus the negative control that the MPP12 bytes under the
  MPP14 order produce different uid/base/resource.  Also covers all detection
  rules, the forced-layout root rule and the no-table refusal.
* `test_mpp12_identity` (corpus-gated): `mpp12calendar.mpp` decodes as MPP12
  under `112/`; every row's `uid/base/resource/name` equals
  `mpp::load_file()`'s `Document::calendars` (independent code).
* `test_mpp12_task_table` (corpus-gated): `mpp12task.mpp` 3 rows + reader
  agreement.
* `test_mpp12_agreement` (corpus-gated): the `Standard` week built from
  `mpp12calendar.mpp` and from MPP14 `base.mpp` is identical; a 2 d task from
  Fri 08:00 finishes Mon 17:00 in both (checked with `add_work` and with
  `schedule_tasks`).
* `test_mpp12_schedules` (corpus-gated): `Night Shift` (uid 7) installed in
  `core::sched::schedule_tasks`; 2 d from the Fri 2026-10-23 08:00 project start
  runs Fri 23:00 -> Tue 2026-10-27 08:00 (across the weekend), while the same
  task on `Standard` runs Fri 08:00 -> Mon 17:00.  uid 9's short-day week runs
  1 d from Mon 08:00 to Tue 09:00.

### Docs

`core/cal/README.md` (format doc now covers MPP12, detection, byte evidence,
API and test coverage), this file, `memory/cal.md` (wave-19 section + current
state).

## MPP12 measured facts (bytes)

`corpus/probe/mpp12calendar.mpp` (MPXJ's public fixture; synthetic names):

| stream | size | derivation |
|---|---|---|
| `112/TBkndCal/FixedData` | 160 | 64 + 12*8 (8 rows) |
| `112/TBkndCal/FixedMeta` | 136 | 16 + 10*(8+4) |
| `112/TBkndCal/VarMeta` | 144 | 24 + 12*10 |
| `112/TBkndCal/Var2Data` | 2484 | 10 length-prefixed blocks |
| `112/Props` | 84106 | 212 records |

* Rows `(uid, base, resource)` = `(1,-1,-1) (2,1,0) (4,1,2) (5,-1,-1)
  (6,-1,-1) (7,-1,-1) (8,1,3) (9,-1,-1)`.  Reading these bytes as MPP14 gives
  `(base,resource,uid)`, e.g. row 0 becomes uid -1 (skipped) and row 2 becomes
  uid 3 / base 12 / resource 10 — the mis-route the test pins.
* Names on uids 1,5,6,7,9; week/data blocks on uids 4,6,7,8,9.
* `112/Props` `DEFAULT_CALENDAR_HOURS` (0x02401388, 420 bytes) is the stock
  Mon-Fri 08:00-12:00 + 13:00-17:00 week; `DEFAULT_CALENDAR_NAME` = `Standard`.
  **Verified in bytes, not assumed.**
* `Night Shift` uid 7: Mon 23:00-24:00; Tue-Fri 00:00-03:00 + 04:00-08:00 +
  23:00-24:00; Sat 00:00-03:00 + 04:00-08:00; Sunday inherits the default week.
* `Test Calendar 2` uid 9: Mon 09:00-12:00 + 13:00-17:00 (4200 tenths), Tue
  4800, Wed 4700, Thu 4600, Fri 4500, weekend inherits.
* uids 4 and 8 (resource calendars) store 520-byte blocks: all seven day types
  are 1 (inherit) plus one non-working exception on day 8339 = 2006-10-31.  The
  420-byte day area is stale buffer text (fragments such as
  `ResourceCalendarEditDays`).  The blocks consume exactly, so they are valid,
  not damage; the day-type rule already stops the decoder from reading the text.
* `mpp12task.mpp`: 3 rows `(1,-1,-1) (2,1,0) (3,1,1)`, only uid 1 named.
* `mpp12resource.mpp` (checked with a throwaway dump, not committed): 7 rows,
  uid 6 `Night Shift` with the same week; reader agrees.  `mpp12duration.mpp`
  and `mpp12recurring.mpp` were not needed (1 calendar each).

Reader agreement on both files: `mpp::Document::version == mpp12`,
`table_root == "112"`, and every `core/cal` row matches the reader's
`uid/base_uid/resource_uid/name`.

## Commands run (exact) and result tails

`cmake`/`ctest` are not on `PATH` in this shell; the VS 2022 BuildTools copies
were used.

```
$cmake='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake -S . -B build\cal-w19 -G "Visual Studio 17 2022" -A x64 -DCORE_BUILD_TESTS=ON -DCORE_WARNINGS_AS_ERRORS=ON
& $cmake --build build\cal-w19 --config Release --target cal_tests
  -> core_cal.lib, core_sched.lib, core_mpp.lib, cal_tests.exe; no warnings/errors

build\cal-w19\core\cal\Release\cal_tests.exe
  -> cal_tests: 785 checks passed

$env:CAL_NO_CORPUS='1'; build\cal-w19\core\cal\Release\cal_tests.exe
  -> cal_tests: 323 checks passed   (8 SKIP lines)

ctest --test-dir build\cal-w19 -C Release -R "^cal$" --output-on-failure
  -> 1/1 Test #34: cal ... Passed     (100% tests passed, 0 failed)

& $cmake --build build\cal-w19 --config Release        # every target
ctest --test-dir build\cal-w19 -C Release -E plan_recalc --output-on-failure
  -> 100% tests passed, 0 tests failed out of 50
     Total Test time (real) = 35.31 s cold / 4.69 s warm
     (no pre-existing failures; no corpus-dependent failures surfaced, and the
     two regression tests + five fuzz targets also pass)
```

Before/after check counts: **553 -> 785** with the corpus, **245 -> 323** with
`CAL_NO_CORPUS=1`.  All pre-existing checks still pass; the wave-19 checks are
additive.

Full-suite note: the 50-test run above excludes `plan_recalc` on purpose (it
rewrites a golden and must never run concurrently with anything).  Nothing in
this wave touches `document.xml`/goldens, so `plan_recalc` is not expected to
change; the coordinator should run it alone in the final gate.

## Verified vs inferred

**Verified in bytes and/or against independent code**

* MPP12 table root, streams, sizes and 12-byte row order; identity/name match
  with `core/mpp`'s independent `Document::calendars` on `mpp12calendar.mpp` and
  `mpp12task.mpp` (8/8 and 3/3 rows).
* `112/Props` stock default week and `Standard` default name.
* Same calendar-data/day-block framing as MPP14: five MPP12 blocks consume
  exactly; uid 6/7/9 values are plausible and internally consistent (totals =
  interval sums; day types resolve through the base chain).
* The scheduling assertions: the dates are this engine's output from the
  decoded MPP12 bytes (deterministic; `schedule_tasks` is the production path),
  and the Standard-calendar control differs, proving the MPP12 calendar drove
  the result.
* MPP14 is unchanged: all previous `cal_tests` checks pass, `base.mpp` still
  decodes MPP14/`114` with the same week.

**Inference / no oracle**

* No MS Project (COM) read-back exists for MPP12 (the oracle is
  coordinator-only).  The calendar semantics of the MPP12 fixture are inferred
  from the same layout as MPP14 and from the independent reader's agreement;
  the *scheduling dates* are our engine's, not Project's.
* The `ResourceCalendarEditDays` text in uids 4/8 is interpreted as stale
  "Change Working Time" field-list text (string content); the functional claim
  (all days inherit, one exception on day 8339) is structural.
* MPP12 files with a per-calendar data block of a *different* framing were not
  seen; the decoder treats every `0x0D400008` block as the MPP14-shaped block.

## Traps for the successor

* `core::sched::Calendar::day_total_ticks(day)` takes a **date day number**
  (e.g. 15633 = 2026-10-19), not a weekday index.  Passing 1..5 silently asks
  about January 1984.
* The two MPP12 resource-calendar blocks in `mpp12calendar.mpp` look like text
  and are valid: seven "inherit" day types + one exception.  Do not add a
  heuristic that rejects them; the day-type rule is the correct guard.
* `decode_calendar_rows` needs an explicit layout; `CalendarLayout::automatic`
  throws there (a byte buffer has no version).
* A forced layout also forces its table root: `decode_file(MPP14file, mpp12)`
  throws.  The cross-order experiment belongs in `decode_calendar_rows`.
* MPP12 base rows store `base_uid = -1`, MPP14 stores `0`; user code must treat
  `<= 0` as "base calendar" (as `CalendarDefinition` documents).
* `cal_tests` now includes `tests/mpp_fixture_builder.h` and `reader.h`; the
  `core_mpp`/`core_sched` include directories must stay `PUBLIC` on `core_cal`
  (they are).  If the reader stream changes that header, rebuild `cal_tests`.
* This Dropbox folder updates file mtimes asynchronously, so the edit tool can
  report "file changed since it was read"; batch a read and the edit in one
  message (or re-read immediately before editing).

## Open items

1. No COM oracle for MPP12 (dates, exception semantics).  The next best check
   would be a Project 2007/2010 save of the same synthetic project and a
   compare of the schedule.
2. `core/mpp` reads only MPP12 calendar identity/name, so there is no second
   decoder for MPP12 week data; the evidence is structural + plausible values.
3. `core/plan` now gets MPP12 calendars for free through
   `load_project_calendar`, but its task/row path for MPP12 is a different
   stream's scope.

## Next three concrete actions

1. Coordinator: commit/merge this wave; run the full serial gate (excluding
   `plan_recalc` unless run alone) on a machine with the corpus.
2. On a machine with Project 2007 (COM, coordinator-only): open
   `mpp12calendar.mpp`, read the Night Shift week and schedule a 2 d task from
   Fri 2026-10-23 08:00; compare with the test's Fri 23:00 -> Tue 08:00.
3. If another stream starts consuming MPP12 calendars, add a small
   `tests/cal_tests.cpp` case for its path; do not change `core/mpp`.
