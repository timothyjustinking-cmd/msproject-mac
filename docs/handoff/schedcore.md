# Handoff — scheduling core (W7)

Date: 2026-10-22. Wave 2, stream W7. Branch: none — single tree, coordinator
commits (coordination.md rev 2). No git commands were run.

## Done

- `core/sched/date.{h,cpp}` — date32 per spec §0.1 [B]: packed day/time, epoch
  1984-01-01 = day 1 = Sunday, weekday 0 = Sunday, NA `0xFFFFFFFF`, overflow
  sentinel `0xECD73836`, 24:00 → next day 00:00, civil and seconds conversion.
- `core/sched/duration.{h,cpp}` — ticks in tenths of a minute (4800 = one 8 h
  day), the [B] `h/d/w/mo` + elapsed-`e` + estimated-`?` table, formatting.
  Elapsed conversion and non-default HoursPerDay are **gated** (`nullopt`) —
  `TODO(open: W3-E1/E2/O5/O2)`.
- `core/sched/calendar.{h,cpp}` — 60-byte day record (count `+2`, starts
  `+8+2i`, lengths `+0x1c+4i`, ≤ 8 intervals, layout enforced by
  `static_assert`), effective-week cache with invalidation, `WorkTimeBetween`
  (§1.3 verbatim), `AddWork`/`SubWork` as whole weeks → whole days → interval
  walk (§1.4). Base calendars, exceptions and resource calendars are not
  implemented (`TODO(open: W3-C3)`), pinned non-working starts are
  `TODO(open: W3-C2)`.
- `core/sched/CMakeLists.txt` — self-contained subdirectory, `core_sched`
  target + alias `core::sched`, three CTest tests. Top-level `CMakeLists.txt`
  untouched.
- `tests/sched_{date,duration,calendar}_tests.cpp` + `tests/sched_test_support.h`
  — 285 hand-computed checks, all passing, including the brief's cases:
  Mon 08:00 + 1 d → Mon 17:00, weekends, lunch boundaries, noon starts, a
  4-day work week, W3-C1/C3/C4, both walk directions, the cache, and the
  60-byte layout.
- `core/sched/README.md` — evidence map, gates, findings.

## Verification (re-runnable)

```
cmd /c "logs\w7\build.bat"      # configure (VS 17 2022, x64) + build + ctest
cmd /c "logs\w7\werror.bat"     # same with /WX; warning-clean
```

Result: `100% tests passed, 0 tests failed out of 3`; binaries report
`43 / 125 / 117 checks passed`. `logs/w7/` is git-ignored: it is a throw-away
probe project that does exactly what CI will do (`enable_testing()` +
`add_subdirectory(core/sched)`), not part of the deliverable.

CI integration, when the coordinator is ready:

```cmake
# after enable_testing() (already called by the top-level under CORE_BUILD_TESTS)
add_subdirectory(core/sched)
```

## Not done

- No CPM: no tasks, links, constraints, slack or rollup. That is spec §3-§5 and
  was outside this stream's brief (date32, duration, calendar).
- No `.mpp`/task-record wiring: `calendar.h` takes day numbers and patterns, it
  does not read the container (W5) or the task model (W6).
- Every `[I]`/`[O]` item listed in `core/sched/README.md` remains a gate. None
  was guessed to make a test pass.

## Next three actions

1. Add `add_subdirectory(core/sched)` to the top-level build and confirm the
   three test names pass in CI.
2. Route the four spec findings (see below) to W3 so §1.1/§1.4 can be amended.
3. When `W3-O4`/`W3-C2` results exist, replace the two TODO comments in
   `calendar.cpp` (`add_work` alignment, `sub_work` interval-start anchor) and
   add the pinned-Saturday fixture.

## Findings for W3 (`spec/scheduling.md`)

1. **§1.4 consequences 1 and 3 assume an 8-working-hour calendar.** On §1.3's
   08:00-17:00 day, `AddWork(Mon 08:00, 480 min)` is Mon 16:00 and
   `AddWork(Wed 17:00, 480)` is Thu 16:00 — not 17:00. The tests use the 8 h
   calendar (08:00-12:00 + 13:00-17:00) for both, which is where the stated
   results come from. The spec should name the calendar.
2. **§1.4 `SubWork` does not terminate when `t` is an interval start.**
   `prev_finish(t) == t` there (§1.2), so `avail == 0` and the loop cannot
   progress. Implemented as: anchor on the previous interval's finish. Then
   `SubWork(Mon 08:00, 1 d) = Fri 08:00`, while `AddWork(Fri 08:00, 1 d) = Fri
   17:00` — the two walks are inverse only at real finish instants. Tagged
   `TODO(open: W3-O4)`; needs a late-date fixture.
3. **§1.1 does not say which weekday `weekStartDay` is.** Weekday 0 = Sunday and
   the Sunday..Saturday day block imply the week runs Sunday..Saturday; the cache
   keys on that Sunday (the week holding Mon 2026-10-19 starts on day 15632).
   Default-week arithmetic is unaffected; a work-week override key would be.
4. `weekday()` is undefined for NA in §0.1; `weekday(kDateNa) == -1` here.

## Traps

- Week key = Sunday, not Monday (a Monday key makes `set_work_week` a silent
  no-op).
- Day/week skip conditions are `d > total`, never `>=`; `>=` moves a one-day
  task to the next morning.
- `pack()` normalises 14400 tenths to the next day — keep internal walk
  positions in `DayTime` (`tenths` may equal 14400) and convert only at the API.
- Do not run git; do not edit the top-level `CMakeLists.txt`, `core/mpp/`,
  `data/`, `.github/` or shared docs. `spec/scheduling.md` is W3's; findings go
  through the coordinator.
