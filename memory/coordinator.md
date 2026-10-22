# memory — coordinator (wave 7 complete, 2026-10-22)

The long version is `docs/handoff/coordinator.md`; this is the short version.

## Main

`8f6f590` or later, pushed; full suite **24/24** in a clean MSVC `/W4 /WX`
build (`build\vs-cal`), `plan_recalc` 186 checks. Wave-6's tip was red on
clang/gcc (unused `get_u16`, fixed in `d1981cb`) — always run `gh run list`.

## The number

M2: **start 83/83, finish 83/83, slack 47/83, critical 53/83 = 266/332**
(was 228/332), 47 golden uids. Remaining mismatches are slack/critical only:
45 link-family, 16 summary, 5 milestone, 0 unexplained
(`recalc --attrib-json`).

## What wave 7 established

- PROJECT_A MSPDI ground truth: 88 links (== 88 FixedMeta rows), 13 constraints, 34
  actual rows, 67 assignments. Reader now returns exactly those 88.
- Link liveness = FixedMeta coverage (99 rows / 91 entries / 88 covered).
  Phantom rows 10760->10846 and 10758->10859 lag 6d are gone.
- 0-d link bounds are not snapped; FF leads are applied and floored at the
  project start; backward lead clamped to succ.LF; free slack min(slip,total).
- `core/cal`: TBkndCal + `114/Props` decoded; Standard week lives in Props;
  holiday proof exact; PROJECT_A 228 week overrides; M2 unchanged because PROJECT_A's 1331
  exceptions are all 2002..2024.
- COM dates are wall clock; `+104/+108` are effective Start/Finish, `+96/+100`
  a calculated copy, `+112/+116` Late. Writer authority measured; manual Start
  store still open (`manual-start-edited.mpp` is the reference to diff).
- Independent MSPDI cross-check: 830 fields, 88/88 links, 0 mismatches; two
  import bugs fixed (ConstraintType, decimal LinkLag).

## Next three actions (same as the handoff)

1. One oracle session: diff `manual-start-edited.mpp` vs `base.mpp` stream by
   stream, then a fixture that writes exactly the changed set; if dates survive,
   wire `recalculate_write_path`, else test the SNET-constraint path.
2. Slack/critical: the 45 link-family rows (uid 8141, 10763 first), then W3-P7
   with a separating summary fixture.
3. Reader `+178` task-calendar uid; then per-task calendars via
   `find_calendar_by_uid`.

## Traps added this wave

Stale oracle fixture (check mtimes), `ctest -R cal` over-matches, target names
`core_sched_<module>_tests`, `FormatID="MSProject.xml"`, `BaseCalendars` is a
method, wall-clock COM getters, PROJECT_A's default calendar name is client data.
See the handoff's Traps section for the full list.
