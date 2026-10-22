# `core/sched` — date32, durations, calendar walks, CPM, units, rollup

The proven part of `spec/scheduling.md`, as tested C++20, plus the CPM engine
(W12), the work/units triangle (W15), the summary rollup (W15), the wave-23
measured rules for 0-duration start bounds and FF leads (`milestone-probe.json`,
`lagff-probe.json`) and the wave-30 measured stored-slack words and started-task
rule (`corpus/probe/project_a-recalc.mpp`). `[B]` items are read out of the binary;
**oracle** items are measured MS Project results in `docs/analysis/oracle/` or
Project's own saves in `corpus/probe/`; everything else is explicitly marked
inference, and behaviour that would be a guess is refused or left as a
`TODO(open: <recipe>)` instead of hard-coded.

```
core/sched/date.h  date.cpp        §0.1  date32 packing, epoch, weekday, civil + seconds
core/sched/duration.h  duration.cpp §0.2 §2.2  ticks, the [B] unit table, gates
core/sched/calendar.h  calendar.cpp §1.1-§1.4  60-byte day record, effective-week cache,
                                                WorkTimeBetween, AddWork/SubWork, align_back
core/sched/cpm.h  cpm.cpp           §3-§4  task graph, forward/backward passes, slack,
                                            criticality, FS/SS/FF/SF + lag, the 8 constraints,
                                            deadlines, cycle detection, CpmTask::actual_start
core/sched/units.h  units.cpp       §2.3  task types, the measured 3x3 edit matrix, units/work
                                          conversions, effort-driven add/remove ([I])
core/sched/rollup.h  rollup.cpp     §3.7  bottom-up summary rollup over explicit parent ids
tests/sched_date_tests.cpp         | 43 checks
tests/sched_duration_tests.cpp     | 125 checks
tests/sched_calendar_tests.cpp     | 117 checks
tests/sched_cpm_tests.cpp          | 1282 checks
tests/sched_units_tests.cpp        | 201 checks
tests/sched_rollup_tests.cpp       | 155 checks
tests/sched_test_support.h         the shared assertion harness (no framework)
```

## Build and test

Self-contained subdirectory; CI adds it (the top-level `CMakeLists.txt` is not
edited here):

```cmake
enable_testing()          # already done by the top-level under CORE_BUILD_TESTS
add_subdirectory(core/sched)
```

Registers `core_sched` (alias `core::sched`) and
`core_sched_{date,duration,calendar,cpm,units,rollup}_tests`, with CTest tests
of the same names. `CORE_SCHED_BUILD_TESTS=OFF` skips the tests. The warning
policy is reused from the parent's `core_set_warnings()` when it exists,
otherwise applied locally (`/W4 /permissive- /utf-8`, honouring
`CORE_WARNINGS_AS_ERRORS`).

MSVC is not on PATH. Re-runnable, from the repository root. W15 uses its own
build directory because four agents build concurrently in this checkout (the
shared `build\vs` produced transient C1083 errors):

```
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul && cmake -S . -B build\vs-sched -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-sched --config Release"     # never --parallel
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build\vs-sched -C Release -R "sched" --output-on-failure
build\vs-sched\core\sched\Release\core_sched_cpm_tests.exe
```

Last run (2026-10-22 14:46:56 +11:00, Release, `/WX`, build tree `build\vs-w30`:
`core/sched/cpm.cpp` 14:38:22, `core/sched/cpm.h` 14:38:06,
`core/sched/rollup.cpp` 14:45:36, `core/sched/rollup.h` 14:45:38,
`tests/sched_cpm_tests.cpp` 14:56 and `tests/sched_rollup_tests.cpp`
14:45:41): the six sched binaries report
`43 / 125 / 117 / 1282 / 201 / 155 = 1923 checks passed`; the `-R sched` CTest
run is `100% tests passed, 0 tests failed out of 6`.  The whole-suite count in
that tree is not meaningful while other streams are mid-landing (their
`CMakeLists.txt` can reference files that do not exist yet).

## Verified against the oracle (W12)

Inputs are the raw oracle rows. Durations and slack are converted openly:
the oracle reports **minutes**, the engine stores **tenths of a minute** (§0.2),
so `480 min -> 4800 ticks`. The tests never mix the two.

| measured behaviour | oracle | where it is pinned |
|---|---|---|
| finish instant = `AddWork(start, duration)`: 1 d from Mon 08:00 finishes Mon 17:00 | `w3-o1.json` T1 | `O1/T1` |
| weekend skip: 5 d from Tue 08:00 finishes the following Mon 17:00 | `w3-o1.json` T2 | `O1/T2` |
| mid-day finish: 4 h from Fri 15:00 finishes Mon 10:00 | `w3-o1.json` T3 | `O1/T3` |
| a 0-duration milestone has `start == finish` and `late == project finish` | `w3-o1.json` T4 | `O1/T4` |
| the project start is midnight and the task starts at the first working instant | `w3-o1.json` project_start | `ASAP starts at the first working instant` |
| SNET is a floor and never pulls a task earlier | `w3-o4.json` 4/6 | `O4/*/type4`, `/type6` |
| SNET with a later date moves the task to it | `w3-o1.json` T2/T3/T4 | `O1/*` |
| MSO pins the start exactly, including a mid-day instant, even against a link | `w3-o4.json` 2 | `O4/*/type2` |
| MFO pins the finish exactly and derives the start with `SubWork` | `w3-o4.json` 3 | `O4/*/type3` |
| MSO/MFO violations: slack **-2400 / -2880**, start slack carries it, finish slack 0 | `w3-o4.json` 10 d rows | `O4/infeasible/type2,3` |
| SNLT clamps in the forward pass and gives -2400; FNLT likewise with -2880 | `w3-o4.json` 10 d rows | `O4/infeasible/type5,7` |
| SNLT defines the late dates (`LF = AddWork(date, dur)`), even beyond the project finish | `w3-o4.json` 1 d SNLT | `O4/feasible/type5` |
| *assigning ConstraintDate to an ASAP task silently makes it SNET(4)* | `w3-o4b.json` asap probe | `ASAP+date becomes SNET(4)` |
| the FS+0 forward bound is the **next working instant** after the predecessor's finish | `w3-o4.json` baseline | `O4/feasible/type0`, `O3/type1/lag0` |
| a **start bound is always a working instant**: FS+1d's bound Tue 20 17:00 is displayed as Start Wed 21 08:00 | `w3-o3.json` FS+1d | `O3/type1/lag4800` |
| an SS+1d bound Mon 19 17:00 is displayed as Start Tue 20 08:00; SS+2h stays Mon 10:00 | `w3-o3.json` SS+1d, SS+2h | `O3/type3/lag4800`, `O3/type3/lag1200` |
| a **finish is NOT snapped as a start**: FS+1d finishes Wed 21 17:00 and FS+2h finishes Wed 21 10:00 | `w3-o3.json` FS+1d, FS+2h | `O3/type1/lag4800`, `O3/type1/lag1200` (finish) |
| **an SS lead is not floored at the project start**: SS-1d starts Fri 2026-10-16 08:00, before the Mon 19 project start, and the row carries 480 min of slack | `w3-o3.json` SS-1d | `O3/type3/lag-4800` (both task rows) |
| an **FF bound is anchored on the predecessor's finish instant**: FF+0 finishes Mon 17:00, FF+1d Tue 17:00, FF+2h gives Mon 10:00 -> Tue 10:00 | `w3-o3.json` FF rows | `O3/type0/*` |
| an **SF bound is a placed finish**: SF+0 finishes Mon 19 08:00 (mid-day) with Start Fri 16 08:00; SF+1d Mon 17:00; SF-1d Fri 08:00 with Start Thu 15; SF+2h Mon 10:00 with Start Fri 10:00 | `w3-o3.json` SF rows | `O3/type2/*` |
| a **finish-side late bound is lagged then aligned back**: FS+1d successor LS Wed 08:00 -> T1 LF Mon 17:00 (not Tue 08:00); FF+1d T2 LF Tue 17:00 -> T1 LF Mon 17:00 | `w3-o3.json` FS+1d, FF+1d | `O3/type1/lag4800`, `O3/type0/lag4800` (T1 late finish) |
| a **start-side successor link does not lift the predecessor's late finish past the project finish**: SS-1d's T1 LF is Mon 17:00 with 0 slack (the start bound alone would give Tue 17:00/480) | `w3-o3.json` SS and SF rows | `O3/type3/*`, `O3/type2/*` (T1 row) |
| the FS backward bound aligns down: successor LS Fri 23 08:00 -> predecessor LF Thu 22 17:00 | `w3-o4.json` MFO | `O4/feasible/type3` (T1 LF) |
| TotalSlack/StartSlack/FinishSlack are working minutes; mid-day finishes make them differ | `w3-o1.json` T3 (420 vs 360) | `O1/T3` |
| FreeSlack = TotalSlack when the task has no successors; 0 when the link leaves no gap | `w3-o1.json`, `w3-o4.json` | `O1/*`, `O4/feasible/type5` (T1) |
| a deadline never moves a date; it clamps the late finish (aligned) and makes slack -480 | `w3-o4b.json` deadlines | `O4b/deadline/*` |
| a later deadline leaves slack 0 | `w3-o4b.json` deadline 10/21 | `O4b/deadline/*` row 3 |
| Critical = `TotalSlack <= CriticalSlackLimit(days) * minutes_per_day`; 480 min is not critical at 0 and is at 1 and 2 | `w3-o4b.json` critical_slack_limit | `O4b/critical-limit/*` |
| the baseline with no link (A 1 d, B SNET Tue 20 08:00) | `w3-o4b.json` slack_baseline | `O4b/slack-baseline` |
| a **0-duration task keeps a start bound's instant**: FS+0 on a task finishing 17:00 gives start == finish 17:00 (the same bound on a 1 d successor is Tue 08:00) | `milestone-probe.json` IDs 2/7 vs `w3-o3.json` FS+0 | `MS/ID2`, `MS/ID7`, `MS/contrast` |
| a milestone's FS+50d bound also keeps its 17:00 instant; snapping the *result* would give Tue 12/29 08:00 | `milestone-probe.json` ID 3 | `MS/ID3` |
| a milestone's late dates are the project finish, and a 0-d SS+0 keeps 08:00 | `milestone-probe.json` IDs 4, 5 | `MS/ID4`, `MS/ID5` |
| **FF lag is applied on the raw finish instant**: FF-1d from Fri 23 17:00 -> Thu 22 17:00, FF-3d -> Tue 20, FF+0 -> Fri 23 | `lagff-probe.json` T2/T3/T5 | `LAGFF/T2`, `/T3`, `/T5` |
| **an FF placement is floored at the project start**: FF-6d computes Thu 10/15 17:00 and is placed Mon 19 08:00-17:00 | `lagff-probe.json` T4 | `LAGFF/T4` |
| the old w3-o3 FF-1d row is the same rule: the applied bound Fri 16 17:00 is floored to Mon 17:00, so the row coincides with zero lag | `w3-o3.json` FF-1d | `O3/type0/lag-4800` (T2 row) |
| **an FF lead does not push the predecessor's late finish past the successor's**: FF-1d T1 LF is Mon 17:00 with 0 slack, where the plain inverse gives Tue 17:00 and 480 | `w3-o3.json` FF-1d | `O3/type0/lag-4800` (T1 row); consistent with `LAGFF/T1` |
| **free slack never exceeds total slack**: FF-1d's lead-inflated raw slip (480) is reported as 0, matching its total slack | `w3-o3.json` FF-1d | `O3/type0/lag-4800` (T1 free slack) |
| SF+1d is not anomalous: `AddWork(pred.ES 08:00, 1 d)` is Mon 17:00, so the placed finish is the measured Mon 08:00-17:00 | `w3-o3.json` SF+1d | `O3/type2/lag4800` |

The expected values are transcribed mechanically from the JSON (a script wrote
the tables, not hand arithmetic), and `tests/sched_cpm_tests.cpp` asserts every
field the oracle reports: Start, Finish, Early\*, Late\*, StartSlack,
FinishSlack, FreeSlack, TotalSlack, Critical and the **stored** constraint type.
Coverage is all 24 W3-O1/O4/O4b cases (50 task rows), all **16** measured W3-O3
link rows (32 task rows: both tasks of every FS/SS/FF/SF run, lags
0/+1d/-1d/+2h -- `FF-1d` and `SF+1d` are no longer excluded), the 7
`milestone-probe` rows and the 5 working-lag `lagff-probe` rows (12 more task
rows).  The two ALAP rows of `w3-o4.json` are reproduced as what they actually
measured: MSP stored the type as SNET(4) once a date was assigned, and the
engine's `normalise_constraint` does the same. They are therefore a test of the
migration, *not* of ALAP scheduling — see the inference table.

**W3-O3 `FF-1d` is explained (wave 23).** `lagff-probe.json` shows that an FF
lead IS applied -- from a 5 d predecessor finishing Fri 10/23 17:00, FF-1d gives
Thu 10/22 17:00, FF-3d Tue 10/20, FF-6d Thu 10/15 -- and that the resulting
placement is floored at the project start (FF-6d becomes Mon 10/19 08:00-17:00).
The old w3-o3 row has a 1 d predecessor, so its applied bound (Fri 10/16 17:00)
sits before the project start, the floor lifts it to Mon 10/19 17:00 and the row
lands on the zero-lag dates. That is why it looked as if the lag were dropped;
there is no second FF formula.  On the backward pass the measured rows force one
more rule: a lead does not push the predecessor's late finish past the
successor's late finish (`O3/type0/lag-4800` T1: LF Mon 17:00, 0 slack).

**0-duration tasks are a measured case of their own (wave 23).**
`milestone-probe.json` measured a 0-d task's FS+0 and FS+50d bounds landing on
17:00: a milestone keeps the bound instant, while the identical FS+0 bound on a
1 d successor is displayed at the next working morning. The engine therefore
applies `align_start_bound` only when `duration > 0`.  Note which rows
discriminate what: ID2/ID7 (FS+0, d == 0) discriminate the *anchor* (the
predecessor's finish instant, not the next working instant) and the missing
snap; ID3 does not discriminate the anchor (AddWork already moves off 17:00 for
a 50 d lag) but rules out snapping the *result*, which would give Tue 12/29
08:00.  Not measured, and therefore not claimed: the FF project-start floor for
a **0-duration** successor (the fixture's successors are all 1 d), and a raw
17:00 SNET date on a milestone (ID 4's stored date is already 08:00 because MSP
normalised the assigned start).  Both are listed as open in the inference table.

## W30: Project's stored slack words, TotalSlack = LF-EF, started tasks, W3-P7

Measured on Project's own saves (`corpus/probe/project_a-recalc.mpp`, the
`milestone1.mpp`/`outline1.mpp`/`w3-o1.mpp` fixtures and `schedin.mpp`). Raw row
offsets are `spec/mpp-records.md` §2; the evidence is `docs/analysis/oracle/`
plus a byte-level dump (`build\vs-w30\rows.py`, scratch).

**The three stored slack words** (a 202-byte `TBkndTask/FixedData` row):

| offset | field | discriminator |
|---|---|---|
| **+74** | **FreeSlack** | `milestone1.mpp` ID6 stores +74 = 0 while COM TotalSlack = 22080 and FreeSlack = 0; PROJECT_A uid 10849 +74 = 7200 min, +190 = 16320. |
| **+186** | **StartSlack** | `outline1.mpp`'s summary stores +186 = 1920 min (COM StartSlack) with +74 = +190 = 0; the engine's StartSlack matches it on 83/83 PROJECT_A rows. |
| **+190** | **TotalSlack** | COM TotalSlack on w3-o1/outline1/lagff/milestone; `schedin.mpp`'s ALAP task +190 = 5280 min = the engine's total; +190 = LF-EF on all 83 PROJECT_A rows. |

W18/mpp-records.md read **+74 as TotalSlack** because its witnesses (w3-o1,
outline1) have all three slacks equal; milestone ID6 and 28 PROJECT_A rows separate
them. Consequences: the M2's 45 link-family + 5 milestone mismatches were a
field-identification bug (the engine's TotalSlack already equalled +190), and
the "ALAP Project 0 vs engine 5280" divergence was +74 too. The reader fix is
W31's (`core/mpp` now exposes all three; Critical derives from +190).

**TotalSlack is the FINISH slack, LF - EF**, not `min(LS-ES, LF-EF)`. It goes
below LF-EF only for a NEGATIVE start-side conflict (measured w3-o4 MSO/SNLT
-2400, MFO/FNLT -2880). Measured rows: PROJECT_A summary 10798 (StartSlack 100800,
TotalSlack 38400) and 8141 (StartSlack 0, TotalSlack 6240) prove the start side
is not min'ed in; uid 10849 (started) stores StartSlack 0 and TotalSlack
16320 = LF-EF. `cpm.cpp` now computes `total = finish_raw`, min'ed only with a
negative start slack or a negative constraint slack; `StartSlack` stays
`min(LS-ES, TotalSlack)` (the w3-o4 MSO rows require -2400 there).

**A started task pins only its START** (`CpmTask::actual_start`): the forward
pass places ES at the actual start and EF = ES + duration; the backward pass
pins **LS** to the actual start but leaves **LF** successor-derived. The old
plan mapping (MSO on the actual start) pinned late = early and reported 0.
Test: `tests/sched_cpm_tests.cpp` `ACTUAL/*` (13 checks; the MSO contrast row
still pins TotalSlack 0, the w3-o4 shape).

**W3-P7 is settled: a summary's TotalSlack is the rolled-up FINISH slack.**
16 PROJECT_A summaries: 8141 StartSlack 0 / TotalSlack 6240 / Critical false while its
children are critical; 10720 (10080), 10761 (28320), 10798 (start 100800 /
total 38400); W3-O6 and outline1 (start 1920 / total 0). "Inherit any critical
child" is refuted by 8141. `rollup.cpp` computes `total_slack_recomputed =
finish_slack`; the default mode is recompute (both in `RollupOptions` and, under
the coordinator's authorisation, `PlanInput::summary_critical_mode` in
`core/plan/plan.h`), while the inherited candidate is still recorded. A summary
has no successors, so its **FreeSlack equals its TotalSlack** (all 16 PROJECT_A
summaries have row +74 == +190). Test: `W3-P7 PROJECT_A shape` in
`tests/sched_rollup_tests.cpp` (18 checks).

**M2 result on PROJECT_A after all of the above** (with W31's reader):
start 83/83, finish 83/83, slack **83/83**, critical **83/83** = **332/332**
(no DIFF lines; every attribution family is 0). The last row was uid 10851
(40 % complete) with successors 10757 (FS+0, 100 % complete) and 10870 (FS+0,
not started). Every stored slack word equalled the bound from **10870 alone**
(+74 28800, +116 08-19 17:00, +190 39360); the 10757 bound would be 2400 min /
05-04. MEASURED RULE (one row): **a link whose SUCCESSOR is 100 % complete does
not bound the predecessor's late finish and is excluded from its free slack** --
the completed successor has no remaining work and cannot be delayed.
`CpmTask::actual_finish` marks it, `cpm.cpp` skips it in the backward and free
slack loops, and `plan.cpp` sets it for a completed task. Pinned by
`tests/sched_cpm_tests.cpp` `COMPLETED/*` (rule value + a contrast where the same
MFO successor without an actual finish does bound). [I] scope: the row is FS and
the engine extends the rule to the other link types; only 1 PROJECT_A link has an
in-progress predecessor and a complete successor (the other 14 complete-task
predecessors are complete themselves), and the alternative "stale saved row"
reading is not formally excluded. `memory/schedcore.md` keeps the cheap COM
confirmation (read uid 10851's TotalSlack/FreeSlack/Successors).

## Verified against W3-O2/O2b (work/units) and W3-O6 (rollup)

W3-O2/O2b measured one 3 d task (Mon 2026-10-19 08:00, one resource at 100 %:
Duration 1440 min, Work 1440 min, Units 1.0) and then each edit in turn. W3-O6
measured a summary S1 over C1 (2 d) and C2 (1 d). All nine cells of the edit
matrix are measured; the effort-driven add/remove transition is not.

| measured behaviour | oracle | where pinned |
|---|---|---|
| COM `Assignment.Units` is a fraction: 0.5 / 1.0 / 2.0 round-trips | `w3-o2b.json` | `ORACLE units 0.5 -> 5000`, ... |
| fixed units: `Duration=5d` -> Work 2400 min (Work = Dur x U) | `w3-o2.json` | `fixed units Duration=5d` |
| fixed units: `Work=4h` -> Duration 240 min (Dur = Work / U) | `w3-o2.json` | `fixed units Work=4h` |
| fixed units: `Units=50%` -> Duration 2880, Work 1440; `200%` -> 720 | `w3-o2b.json` | `fixed units Units=50%/200%` |
| fixed duration: `Units=50%` -> Work 720; `200%` -> Work 2880 | `w3-o2b.json` | `fixed duration Units=50%/200%` |
| fixed duration: `Work=4h` -> Duration stays 1440, Work 240, **Units stays 1.0** (under-allocated) | `w3-o2.json` | `fixed duration Work=4h (under-allocated)` |
| fixed duration: `Duration=5d` -> Work 2400 | `w3-o2.json` | `fixed duration Duration=5d` |
| fixed work: `Units=50%` -> Duration 2880; `200%` -> 720 | `w3-o2b.json` | `fixed work Units=50%/200%` |
| fixed work: `Work=4h` -> Duration 240 | `w3-o2.json` | `fixed work Work=4h` |
| fixed work: `Duration=5d` -> Work stays 240, **Units stays 1.0** (under-allocated) | `w3-o2.json` | `fixed work Duration=5d (under-allocated)` |
| two resources at 100 % -> total units 2.0, Work 2880 min; task `Work=4h` -> Duration 120 min, 120 min per assignment | `w3-o2.json` | `two resources*` |
| summary duration is the working span: 2880 min, not 960 + 480 = 1440 | `w3-o6.json` | `ORACLE W3-O6: S1 duration 2880 min` |
| summary late dates roll up: LS = min child LS (Fri 23 08:00), LF = max child LF (Mon 26 17:00) | `w3-o6.json` | `ORACLE W3-O6: S1 late start/finish` |
| summary StartSlack 1920, FinishSlack 0, TotalSlack 0, Critical true | `w3-o6.json` | `ORACLE W3-O6: S1 ... slack` |
| C1 960 min with slack 1920, not critical; C2 480 min with slack 0, critical | `w3-o6.json` | `ORACLE W3-O6 C1/C2` |
| after OutlineOutdent S1 is a leaf again and reads its stored 960 min — the rolled-up duration is never stored | `w3-o6.json` | `ORACLE W3-O6 outdented: S1 stored duration 960` |

## Still inference — do not treat as MS Project behaviour

| what | why | recipe |
|---|---|---|
| the FF **project-start floor for a 0-duration successor** | the floor is measured for 1 d successors (`lagff-probe` T4); `milestone1.mpp` has no FF milestone, so `add_work(floor, 0) == floor` placing a milestone at the project start is an assumption, not a measurement | fixture: 0-d successor with FF-6d (or any FF bound before the project start) |
| a raw SNET/MSO date at 17:00 on a milestone | `milestone-probe` ID 4 assigned 17:00 and MSP stored SNET at Thu 08:00, so the engine only ever sees the normalised date; whether the scheduler would snap a stored 17:00 milestone start is unmeasured. The engine keeps its unconditional constraint snap | MSPDI fixture with `ConstraintType=4` and a raw 17:00 `ConstraintDate` on a 0-d task |
| the **FF backward lead clamp** beyond the measured shape | `min(succ.LF, align_back(sub_lag(...)))` is pinned by `w3-o3` FF-1d (T1 LF Mon 17:00, 0 slack); in the fixture the successor's LF equals the project finish, so "clamp to succ.LF" and "clamp to the project finish" cannot be distinguished | FF-1d whose successor has an SNLT date beyond the project finish |
| a lead's effect on free slack beyond FF-1d | `free_slack = max(min(raw slip, total slack), 0)` reproduces FF-1d (0, not the 480 the raw formula gives); the raw slip is <= total in every other measured row, so the two rules coincide there | a lead link with a successor that has slack of its own |
| **mixed FF and SF bounds** on one task | the floor is applied to the FF part only; no fixture mixes the two finish-side types on one successor | fixture with one FF and one SF predecessor |
| negative free slack clamp at 0 | every measured free slack is >= 0 | `W3-P3` |
| free slack with several successors / non-FS links | the FF and SF rows exist, but no measured row has two successors with different slack | `W3-P3` |
| ALAP scheduling itself (display the late dates; slack convention) | the oracle's ALAP rows came back stored as SNET(4), so the schedule shown was SNET's; only the type migration is measured, and ALAP+date is normalised the same way **[I]** | clean ALAP re-test |
| schedule-from-finish | no fixture; the flag changes the backward anchor and the displayed dates only | `W3-P5` |
| a constraint's slack when the task has no links (the "link-driven" anchor is the project start) | no measured row | needs a fixture |
| per-task calendars inside a link bound (the align_back uses the successor's calendar) | single-calendar fixtures only | `W3-L1` |
| elapsed durations and elapsed lag (refused, not guessed) | elapsed constants are [I]; `lagff-probe` FF-1ed lands on the same instant as FF-1d because Thursday is a working day, so it separates nothing | `W3-E1`, `W3-E2` |
| non-default HoursPerDay for criticality | only 8 h/day measured | `W3-O5` |
| effort-driven add/remove (total work held vs duration held) | W3-O2 recorded identical two-resource states with the flag false and true, but did not isolate the add/remove step; the code implements the spec's candidate | `W3-U2`: add a resource to an existing task and compare Duration with EffortDriven on/off |
| the .mpp units double and Work field scales (units x100 -> 10000 = 100 %, Work minutes x1000) | the stream brief gives them; no oracle row in W3 measured the raw bytes | read the raw Work/Units cells back from a saved `.mpp` |
| summary Work / ActualWork sums and %complete = sum(actual duration) / sum(duration) | W3-O6 assigned no resources, so only 0 / 0 / 0 was observed | `W3-U6` with resources under a summary |
| blank summaries stay blank and blank children are ignored for dates | no blank-row fixture | fixture with an empty child |
| a completed-successor link for **SS/FF/SF** | the rule is encoded and measured on PROJECT_A uid 10851, but that row is FS; the type-independent reason ("no remaining work") extends it to the other types without a row | fixture with an in-progress predecessor and a completed SS/FF/SF successor |
| the 10851 row is **stale stored data** rather than a completed-successor rule | the rule reproduces all three stored words, but the row did not change on the recalc while 19 others did; a stale row would look the same | COM read of uid 10851 TotalSlack/FreeSlack/Successors on `project_a-recalc.mpp` (values only) |
| proportional per-assignment work splitting and rounding | measured only with equal units (120 / 120 in W3-O2) | unequal-units fixture |
| progress fields (`% Work Complete`, actual/remaining work) beyond the rollup sums | not in the measured W3-U3 set | `W3-U3` |

## Findings against the spec (for W3; `spec/scheduling.md` is not mine to edit)

These are places where the measurements contradict or refine the written rules.

1. **§3.4's `LF = project_finish` is only an anchor for unconstrained tasks.**
   A predecessor's late finish can exceed the project finish: in the feasible
   SNLT row T1's LF is Fri 23 17:00 while the project finishes Tue 20 17:00.
   The engine uses `min` over successor bounds, and the project finish only when
   a task has no bound.
2. **§3.4's SNLT clamp `LF = min(LF, AddWork(date, dur))` is wrong.** Measured
   LF is Mon 26 17:00, i.e. exactly `AddWork(date, dur)`, *not* the smaller
   project finish. SNLT defines the late dates; it is not min'ed with the
   project finish.
3. **§3.4/§3.6 say pinned tasks have late == early, which implies zero slack.**
   Measured: TotalSlack and StartSlack are -2400 (MSO/SNLT) and -2880
   (MFO/FNLT), and FinishSlack is 0. The slack of a constrained task is the
   working-time distance between where the constraint puts it and where its links
   want it.
4. **§3.5 `FreeSlack = 0 if no successors` is wrong.** Measured free slack for a
   no-successor task equals its total slack (2400, 1920, 360, 1920 in
   `w3-o1.json`), clamped at 0 when the total slack is negative (-2400 -> 0).
5. **§3.5's `TotalSlack = LS - ES` is wrong; it is `LF - EF`.** W3-O6's T3
   (`w3-o1.json`) has LS-ES = 420 min but TotalSlack = FinishSlack = 360 min.
   The wave-30 PROJECT_A rows settle the general form: a started task stores
   StartSlack 0 and TotalSlack 16320 = LF-EF, and a summary stores TotalSlack
   6240 with StartSlack 0. StartSlack is `min(LS-ES, TotalSlack)` (a hard
   constraint conflict makes it negative), TotalSlack is LF-EF min'ed only with
   a NEGATIVE start/constraint conflict. The row also misses the stored-slack
   word map: **+74 FreeSlack, +186 StartSlack, +190 TotalSlack** (W30).
6. **§3.6 "SNLT/FNLT: conflict if violated" understates it.** The forward pass
   *places* the task at the constraint (SNLT: Mon 26 08:00 instead of Mon 2 Nov;
   FNLT: finish clamped to Mon 26 08:00), and the negative slack is measured
   against the unconstrained link-driven dates.
7. **§3.6 ASAP: assigning ConstraintDate changes the type to SNET(4)**, measured
   in `w3-o4b.json` (`0 -> 4`). The engine normalises this on input and reports
   the effective type per task.
8. **§1.4 `SubWork` at an interval start**: resolved as `align_back` (the
   previous interval's finish). The MFO fixture confirms it - a finish pinned to
   Mon 26 08:00 yields a start of Fri 23 08:00, which is what the calendar
   tests already asserted.
9. **§4.2's backward FS is not `SubWork(succ.LS, 0)`.** With a successor late
   start of Fri 08:00 the predecessor's late finish is Thu 22 17:00, not
   Fri 08:00; the late bound needs the backward alignment (`Calendar::align_back`).
10. **§1.1 never says which weekday `weekStartDay` is.** Weekday 0 = Sunday plus
    the Sunday..Saturday day block implies the effective week runs
    Sunday..Saturday, which is what `effective_week()` does.
11. `weekday(d)` is undefined for NA in §0.1; `weekday(kDateNa) == -1` here.
12. **§2.3's edit matrix derives Units from Work and Duration; the measurement
    says Units is never derived.** On a fixed-duration task setting `Work=4h`
    leaves Duration 1440, Work 240 and Units 1.0; on a fixed-work task setting
    `Duration=5d` leaves Work 240 and Units 1.0. Both are under-allocated
    (Work != Duration x Units). The engine's dependency table therefore maps
    those two cells to "recompute nothing"; the other seven cells follow the
    spec. `tests/sched_units_tests.cpp` pins all nine.
13. **§3.7's "summary duration is the working span" is confirmed and matters:**
    W3-O6's summary is 2880 min (6 working days) while its children are
    960 + 480 = 1440 min. The rollup uses `work_time_between(start, finish)` and
    never writes the span back onto the summary row.
14. **§3.7 leaves summary late dates/slack unspecified; W30 settles W3-P7.**
    Summary LS = min child LS (Fri 23 08:00), LF = max child LF (Mon 26 17:00),
    StartSlack = 1920 (= LS-ES, not the minimum child StartSlack), FinishSlack =
    0, and **TotalSlack = FinishSlack = 0**, not min(start, finish). The
    16 PROJECT_A summaries settle it: 8141 stores StartSlack 0 / TotalSlack 6240 /
    Critical false while its children are critical, which refutes "inherit any
    critical child"; 10798 stores StartSlack 100800 / TotalSlack 38400. A
    summary's FreeSlack equals its TotalSlack (no successors). Both candidates
    are still recorded, but the default is recompute.
15. **§4.2's link formulas need five measured corrections** (W3-O3 and the
    wave-23 `lagff-probe`, pinned in `O3/type*` and `LAGFF/*`):
    * the **FF anchor is the predecessor's finish instant**, not
      `AlignForward(pred.EF)`: FF+0 finishes Mon 17:00, and `AlignForward` gave
      Tue 08:00;
    * an **SF/FF bound is a PLACED finish**: SF+0's finish is Mon 08:00 (a
      mid-day instant) while its start is Fri 16 08:00, so the finish must not
      be reconstructed as start + duration;
    * the **project-start floor is FF-only**, not for every start-side link and
      not for SF: SS-1d starts Fri 16 08:00 and SF+0 starts Fri 16 08:00, before
      the project start, while FF-6d is floored to Mon 10/19 08:00-17:00;
    * **an FF lead is applied on the raw finish instant and lands on the
      previous interval end**: FF-1d -> Thu 22 17:00, FF-3d -> Tue 20, FF-6d ->
      Thu 10/15 (not the same-day 08:00 a plain SubWork gives);
    * **a start bound is snapped only for `duration > 0`**: FS+0 at 17:00 stays
      17:00 on a milestone and becomes Tue 08:00 on a 1 d successor.
    Also, on the backward pass a finish bound is **lagged first, then aligned
    back** (`align_back(sub_lag(succ.LS, lag))`), so FS+1d's T1 LF is Mon 17:00;
    item 9 above only fixed the zero-lag case.  A backward FF bound with a lead
    is additionally **clamped to the successor's late finish** (FF-1d T1 LF
    Mon 17:00, not Tue 17:00 with 480 slack), and free slack is
    **`max(min(raw slip, total slack), 0)`** (FF-1d's raw slip is 480, reported
    0).
16. **§4.2's `FF-1d` row is explained, not anomalous (wave 23).**
    `lagff-probe.json` shows the lead is applied (FF-1d -> Thu 22 17:00 from a
    Friday 17:00 finish) and that the placement is floored at the project start
    (FF-6d -> Mon 10/19 08:00-17:00).  In the w3-o3 row the 1 d predecessor's
    applied bound (Fri 10/16 17:00) is before the project start, so the floor
    lifts it to Mon 17:00 and the row coincides with zero lag.  The backward
    pass adds a clamp the spec does not have: a lead does not push the
    predecessor's late finish past the successor's (`O3/type0/lag-4800` T1).
    `SF+1d` is arithmetic, not an anomaly: `AddWork(Mon 08:00, 480)` is Mon
    17:00 (one working day ends the same day), so its placed finish is the
    measured Mon 19 08:00-17:00.
17. **§4.2's "a start bound is aligned to a working instant" needs the
    duration exception.** `milestone-probe.json` measures a 0-d task with FS+0
    and FS+50d bounds on 17:00 keeping the 17:00 instant (start == finish),
    while the same FS+0 on a 1 d successor is displayed at the next 08:00.  The
    engine applies the start alignment only when `duration > 0`; the FF
    project-start floor is applied only to FF links, and its 0-d case is
    unmeasured (see the inference table).
18. **The plan stream's pinned PROJECT_A counts move with these fixes.** The wave-22
    engine gave start 73/83, finish 69/83, slack 34/83, critical 52/83, overall
    228/332 against `corpus/probe/project_a-recalc.mpp`.  Wave 23 changed start
    bounds for 0-d tasks and FF leads; W24 fixed the reader's link count
    (97 -> 88); W30 fixed the slack field mapping (+190), the TotalSlack rule,
    the started-task pin, the completed-successor rule and the summary rule.
    The current tree is **83/83 start, 83/83 finish, 83/83 slack, 83/83
    critical = 332/332**.
    `tests/plan_recalc_tests.cpp` and `tests/data/project_a_recalc_golden.tsv` must be
    regenerated by their owner; do not trust the old 26/26/5/29 or 73/69/34/52
    or 83/83/47/53 constants.  The schedin ALAP slack assertion should become
    10/10 matching: the old "Project 0" was row +74 (FreeSlack), not TotalSlack.
19. **The stored slack words are three, not one** (W30): +74 FreeSlack,
    +186 StartSlack, +190 TotalSlack. W18/`spec/mpp-records.md` read +74 as
    TotalSlack because w3-o1 and outline1 have all slacks equal; milestone1
    ID6 and 28 PROJECT_A rows separate them. This was an identification bug in the
    comparison, not a backward-pass defect.

## Deliberately not implemented (gates / TODOs)

| what | why | recipe |
|---|---|---|
| elapsed duration semantics (`1ed`, `3ew`, `1emon`) — parsed, but `duration_to_ticks`/`format_elapsed` return `nullopt`; an elapsed link lag is a `CpmError` | elapsed tick constants (1440/10080/43200 min) are [I] | `W3-E1`, `W3-E2` |
| non-default HoursPerDay/Week/DaysPerMonth for duration text | "conversion constants only" is [I] | `W3-O5` |
| duration text display policy | DurationFormat is a user setting | `W3-O2` |
| a pinned start on a non-working instant | exact forward alignment is [O]; the walk snaps forward | `W3-C2` |
| base-calendar chain, dated exceptions, resource calendars | merge rules are [O] | `W3-C3` |
| progress fields (`% Work Complete`, actual/remaining work) beyond the rollup sums | not in the measured set | `W3-U3` |
| effort-driven add/remove semantics | [I] candidate only | `W3-U2` |
| the calendar domain end used by the binary | not measured; `set_day_limit()` defaults to day `0xFFFE` | `W3-O1` |

`core/sched` depends on nothing else in the repo, so it cannot drift with the
task model (W6) or the container (W5).

## Traps

- **Three stored slack words, not one.** A task row stores +74 = **FreeSlack**,
  +186 = **StartSlack**, +190 = **TotalSlack** (6-second ticks). w3-o1 and
  outline1 have all three equal, so a fixture built on them cannot tell them
  apart; milestone1 ID6 and the PROJECT_A rows can. Never read +74 as TotalSlack.
- **TotalSlack = LF - EF, not min(LS-ES, LF-EF).** A started task's late start
  is its actual start (StartSlack 0) while its late finish is
  successor-derived; min() reports 0 instead of the measured 16320/39360.
  Only a NEGATIVE start/constraint conflict lowers the total.
- **`CpmTask::actual_start` pins the START only.** Do not map a started task to
  MSO (late = early): the late finish must stay successor-derived.
- **Summary TotalSlack = LF-EF; summary FreeSlack = TotalSlack.** min(LS-ES,
  LF-EF) gives 0 where Project stores 6240 (PROJECT_A 8141); inheriting the minimum
  child TotalSlack gives 0 too.
- **A completed successor does not bound its predecessor.** A link into a 100 %
  task is skipped in the backward pass and free slack (`CpmTask::actual_finish`).
  PROJECT_A uid 10851 needs it: the completed 10757's bound (2400 min) is not used,
  the not-started 10870's (39360) is.
- **Units.** Raw `.mpp` durations are tenths of a minute (4800 = 1 day); COM and
  the oracle report minutes (480). Convert once, at the boundary. `units32` is
  fixed point with 10000 = 100 %; the COM ratio is `units / 10000`. The `.mpp`
  Work field is minutes x1000, i.e. 100 file units per engine tick
  (`units.h` is the only place that knows both scales).
- **Units is an input, not an output.** The edit triangle never derives Units
  from Duration and Work; the two under-allocation cells deliberately leave
  Work != Duration x Units. Do not "fix" that invariant in `set_work` /
  `set_duration`.
- **Summary duration is a span, not a sum.** Roll it up with
  `work_time_between(start, finish)`; summing child durations gives the wrong
  answer whenever children have gaps (W3-O6: 2880 vs 1440).
- **Late dates roll up too.** Summary LS = min child LS, LF = max child LF;
  StartSlack is then LS-ES, not the minimum child StartSlack.
- **Deadlines align.** A deadline at an interval start clamps the late finish to
  the previous interval's end (`align_back`), not to the deadline itself.
- **Project start is midnight** in the oracle fixture; the forward pass aligns it
  with `next_start` before consuming any duration. A constraint pin is *not*
  aligned (MSO/MFO keep the exact mid-day instant).
- **A link bound is start-side or finish-side, and they differ.** FS/SS bounds
  go through `align_start_bound` and may precede the project start; FF/SF bounds
  are finish instants and are *placed*, never snapped or rebuilt as
  start + duration (SF+0's finish is Mon 08:00 with start Fri 08:00). On the
  backward pass a finish-side lag is `align_back(sub_lag(...))`, not
  `sub_lag(align_back(...))` and not a start alignment.
- **A milestone does not snap a start bound.** `align_start_bound(..., snap)` is
  called with `snap = (duration > 0)`; a 0-d task keeps a 17:00 bound (start ==
  finish), while the same FS+0 on a 1 d successor displays 08:00. Do not "fix"
  the milestone to the next morning.
- **FF is not FS with a different anchor.** FF's forward lag goes through
  `add_finish_lag` (raw finish instant; a lead subtracts and then `align_back`s
  to the previous interval end), and the resulting placement is floored at the
  project start -- for FF links only. SF keeps plain `add_lag` and is not
  floored (SF-1d finishes Fri 08:00, an interval *start*).
- **The FF backward bound with a lead is clamped** to the successor's late
  finish (`min(succ.LF, align_back(sub_lag(...)))`). The plain inverse would push
  the predecessor's LF past the project finish and invent 480 of slack.
- **Free slack is `max(min(raw slip, total slack), 0)`.** A lead inflates the
  raw slip (FF-1d gives 480) and MSP reports 0; every other measured row has
  raw slip <= total, so the clamp is invisible there.
- **`AddWork(t, 480)` from 08:00 is 17:00 the same day**, not the next morning.
  This is why SF+1d's placed finish is Mon 17:00 and why reading "one day" as
  the next calendar day gives a phantom anomaly.
- **Week key = Sunday**, not Monday (a Monday key makes `set_work_week` a silent
  no-op).
- Day/week skip conditions are `d > total`, never `>=`; `>=` moves a one-day task
  to the next morning.
- **MSVC on a Dropbox path**: a build can fail with `MSB6003` because another
  process (the sync client) briefly holds an `.obj`. Re-running the same build
  command fixes it; it is not a code error. The same applies to a CMake
  reconfigure failing on *another stream's* `CMakeLists.txt` while it is
  mid-landing (`Cannot find source file ...`): wait, then re-run the identical
  `cmake --build`.
- **Do not reconfigure in another stream's tree.** Each stream has its own
  `build\vs-*`; a concurrent landing in `core/cal` or `core/io` can break the
  configure step of *every* tree for a few minutes.

