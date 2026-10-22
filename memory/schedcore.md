# memory — scheduling core (W7 primitives, W12 CPM, W15 units/rollup, W23 milestones/FF, W30 slack words)

_2026-10-22 14:58. Successor: read this, then `core/sched/README.md`, then
`spec/scheduling.md` §0-§4. W7 built the primitives, W12 added the CPM engine,
W15 added the work/units triangle (§2.3) and the summary rollup (§3.7), W23
measured the last two CPM rows (0-duration start bounds, FF leads), and W30
(2026-10-22) settled Project's stored slack words and the W3-P7 summary rule,
and added `CpmTask::actual_start`/`actual_finish` and the
completed-successor rule._

## State (W30, 2026-10-22 14:56)

`core/sched/` is implemented, built and tested on disk in the main checkout.
No git operations were run (coordination.md rev 2: the coordinator commits), and
MS Project was not driven (rev 3; the oracle is coordinator-only).

- Files: `date`, `duration`, `calendar`, `cpm`, `units`, `rollup` as before.
  W30 touched **`cpm.h`/`cpm.cpp`** (TotalSlack rule, `CpmTask::actual_start`,
  `CpmTask::actual_finish` + the completed-successor rule),
  **`rollup.h`/`rollup.cpp`** (summary TotalSlack = finish slack, summary
  FreeSlack = TotalSlack, default mode recompute) and, under the coordinator's
  explicit authorisation, **`core/plan/plan.h`/`plan.cpp`** (PlanInput default
  summary mode recompute; a started task is mapped to
  `CpmTask::actual_start`, not to an MSO pin; a completed task is marked on
  `CpmTask::actual_finish`).
- Tests: `tests/sched_{date,duration,calendar,cpm,units,rollup}_tests.cpp` +
  `sched_test_support.h`. **Counts 43 / 125 / 117 / 1282 / 201 / 155 = 1923**
  checks, all passing; `date`/`duration`/`calendar`/`units` unchanged, cpm
  1261 -> 1282 (13 ACTUAL/MSO-contrast + 8 COMPLETED checks), rollup 137 -> 155
  (18 W3-P7 PROJECT_A-shape checks). Warning-clean at `/W4 /WX`
  (`CORE_WARNINGS_AS_ERRORS=ON`).
- Verification (W30, own build dir `build\vs-w30`; exact commands below).
  Last run 2026-10-22 14:56 +11:00. `ctest -R sched` = 6/6 passed.
- M2 (with W31's reader fix, which landed at 14:44:50 and credits W30):
  **start 83/83, finish 83/83, slack 83/83, critical 83/83 = 332/332**.
  No DIFF lines; the attribution matrix is all zero.

## W30: Project's three stored slack words at +74 / +186 / +190 — MEASURED

The task row (202-byte `TBkndTask/FixedData`) stores THREE slack values, not one
(`scripts/mpp_rows.py`-style dump of the raw row, offsets from
`spec/mpp-records.md` §2):

| offset | type | field | proof |
|---|---|---|---|
| **+74** | i32 | **FreeSlack** | `corpus/probe/milestone1.mpp` ID6 (=1 d SNET then a 0-d FS successor): +74 = 0 while COM TotalSlack = 22080 and FreeSlack = 0 (`milestone-probe.json`). PROJECT_A uid 10849 +74 = 72000 ticks (7200 min), +190 = 163200 (16320) and free <= total holds. |
| **+186** | i32 | **StartSlack** | `outline1.mpp`'s summary stores +186 = 19200 ticks = COM StartSlack 1920 min while +74 = +190 = 0; w3-o1 T1 +186 = 24000 = 2400 min = COM StartSlack. Agreement with the engine's StartSlack on **83/83** PROJECT_A rows. |
| **+190** | i32 | **TotalSlack** | w3-o1 (+190 = 24000/0/3600/19200 = COM TotalSlack 2400/0/360/1920), outline1, lagff1, milestone; schedin's ALAP task +190 = 52800 ticks = 5280 min, which is the engine's ALAP TotalSlack. +190 = LF-EF on **all 83** PROJECT_A rows. |

**The old reading was wrong.** W18/mpp-records.md §2 read **+74 as TotalSlack**
because its two witnesses (w3-o1, outline1) have TotalSlack == StartSlack ==
FreeSlack on every row. milestone1 ID6 separates them, and **28 of the 83 PROJECT_A
rows have +74 != +190**.

Consequences (all measured, not inferred):

1. **The 45 link-family + 5 milestone M2 mismatches were an identification bug,
   not a backward-pass bug.** Those rows' TotalSlack already equalled +190; the
   comparison was reading +74 (FreeSlack). With the reader mapped to +190 both
   families are 0 mismatches.
2. **The "ALAP open divergence" was the same misread**: `schedin.mpp` uid 2
   stores +74 = 0 (FreeSlack) and +190 = 52800 ticks; the engine's 5280 min is
   Project's stored TotalSlack. `plan_recalc_tests.cpp`'s ALAP assertion should
   become 10/10 matching slack values, not 9/10 with ALAP the exception.
3. **TotalSlack is the FINISH slack, LF - EF, not min(LS-ES, LF-EF).** It goes
   below LF-EF only for a NEGATIVE start-side conflict (measured w3-o4 MSO/SNLT:
   TotalSlack -2400 while LF-EF = 0; the engine's `constraint_slack` covers
   that). A positive start slack never lowers it: PROJECT_A summary 10798 stores
   StartSlack 100800 and TotalSlack 38400 = LF-EF; summary 8141 stores
   StartSlack 0 and TotalSlack 6240.
4. **A started task pins only its START.** PROJECT_A uid 10849 (60 d, 40 % complete)
   and 10851: row +112 LateStart == the actual start and +186 StartSlack = 0,
   but row +116 LateFinish is successor-derived and +190 = 16320 / 39360 min =
   LF-EF. Encoding this as MSO (the old `plan.cpp` mapping) pins late = early and
   reported TotalSlack 0 for both. `CpmTask::actual_start` (new) pins ES to the
   actual start and LS to the actual start, and leaves LF successor-derived.
5. **W3-P7 is settled: a summary's TotalSlack is the rolled-up FINISH slack.**
   16 PROJECT_A summaries: 8141 start 0 / finish 6240 / total 6240 / Critical false
   while its children are critical; 10720 0/10080/10080; 10761 0/28320/28320;
   10798 start 100800 / total 38400; W3-O6 and outline1 (start 1920, finish 0,
   total 0). The inherited rule (min child TotalSlack, critical if any child is
   critical) is **refuted** by 8141. `rollup.cpp` now computes
   `total_slack_recomputed = finish_slack`; `RollupOptions::critical_mode`
   defaults to `recompute`, and `PlanInput::summary_critical_mode` (plan.h,
   W30-authorized) too. The inherit candidate is still recorded.
6. **A summary's FreeSlack equals its TotalSlack** (it has no successors, like
   a leaf with no outgoing link). All 16 PROJECT_A summaries have row +74 == +190.
   `rollup.cpp` sets it after the total is selected. W3-O6's summary is 0/0 and
   agrees. Caveat: `corpus/project_b.mpp` (a template that was never
   recalculated — task dates 2020, project start 2026) has +74 = 0 on three
   summaries with +190 > 0; treat PROJECT_B's slack words as stale, not as a
   counter-row.

### The last M2 row: uid 10851 (completed successor)

PROJECT_A uid 10851 is 40 % complete (start 04-07, finish 04-27), successors
**10757** (FS+0, 100 % complete: 05-05 -> 06-15) and **10870** (FS+0, not
started: 07-21 -> 07-29). Project stores +74 = 288000 ticks (28800 min),
+186 = 0, +190 = 393600 (39360 min) and +116 = 08-19 17:00. Every one of those
values is exactly the bound from **10870 alone** (10870 LS 08-20 08:00 ->
align_back 08-19 17:00; gap to 10870's ES = 28800 min); the 10757 bound would be
05-04 17:00 / 2400 min. The link row is live (`TBkndCons` row 27 = 10851 ->
10757, type 1 FS, lag 0, FixedMeta entry 25 covers offset 540) and 10757 really
is a successor (its ES 05-05 is also driven by 10807's SS link).
**Encoded rule (measured on this one row): a link whose SUCCESSOR is 100 %
complete does not bound the predecessor's late finish and is excluded from its
free slack** — a completed successor has no remaining work and cannot be
delayed. `CpmTask::actual_finish` (new) marks it; the backward pass and the free
slack loop skip those links; `plan.cpp` sets the field for a completed task.
Pinned by `tests/sched_cpm_tests.cpp` `COMPLETED/*` (8 checks: the rule value and
a contrast where the same MFO successor without an actual finish does bound the
predecessor). This made the PROJECT_A M2 **332/332**.
The row is the only PROJECT_A link whose predecessor is not itself complete (15 links
end at a complete task; the other 14 predecessors are 100 % complete, so they
cannot discriminate), and the [I] extension is that the engine applies the rule
to every link type while only FS is measured. The alternative explanation is
still worth a COM check (it would not change the encoded rule's fit, only its
justification): the row's slack could be **stale from before the client's
realign** — its three words are identical in `project_a.mpp` and
`project_a-recalc.mpp`, while 19 other rows changed on the recalc. Cheap confirmation:
open `corpus/probe/project_a-recalc.mpp`, read `Task.TotalSlack`/`FreeSlack`/
`Successors` for uid 10851 (values only), and build A in progress -> B complete
-> C not started as in the test.

### M2 numbers around W30

| tree | start | finish | slack | critical | overall |
|---|---|---|---|---|---|
| W28 tree, reader on +74 | 83/83 | 83/83 | 47/83 | 53/83 | 266/332 |
| **current tree (W30 + W31 + plan wiring)** | **83/83** | **83/83** | **83/83** | **83/83** | **332/332** |

Attribution now: every family 0 mismatches.

## Verified against the oracle (W12 CPM)

`docs/analysis/oracle/w3-o1.json`, `w3-o3.json`, `w3-o3b.json`, `w3-o4.json`,
`w3-o4b.json`; all 24 oracle cases (50 task rows) reproduced exactly — all date
and slack fields, plus the stored constraint type. The two ALAP rows are
reproduced as the type migration MSP actually recorded (stored SNET(4) after a
date was assigned); ALAP scheduling itself is still unverified (the schedin ALAP
slack is now verified through +190, but not ALAP's displayed dates). Highlights:
finish instant + weekend skip + mid-day finish; milestone; SNET/ASAP
normalisation; MSO/MFO pins; negative slack -2400/-2880; deadline against
TotalSlack; Critical = `slack <= limit_days * 480`; late FS bound via
`align_back`.

## Verified against the oracle (W15 units/work)

`docs/analysis/oracle/w3-o2.json`, `w3-o2b.json` (single 3 d task, one resource
100 %: Duration 1440 min, Work 1440 min, Units 1.0). All nine cells of the edit
matrix are measured (details in `core/sched/README.md`). The important
measurement is that **Units is never derived from Duration and Work**:

- fixed duration + `Work=4h` -> Duration 1440, Work 240, Units 1.0
  (under-allocated, Work != Duration x Units);
- fixed work + `Duration=5d` -> Work 240, Units 1.0 (under-allocated);
- every other cell recomputes Work or Duration only.

Two resources at 100 % give total units 2.0 and Work 2880 min; task `Work=4h`
gives Duration 120 min and 120 min on each assignment. The engine's triangle is
`work = duration x total_units / 10000`; the COM surface reports units as a
0..1 ratio and the engine stores 10000 = 100 %. Work is ticks internally
(minutes x10); the brief gives the `.mpp` Work scale as minutes x1000, i.e. 100
file units per tick.

## Verified against the oracle (W15 rollup) and W30's W3-P7 settlement

`docs/analysis/oracle/w3-o6.json`: summary S1 over C1 (2 d from Mon 19 08:00)
and C2 (1 d from Mon 26 08:00). Measured summary: start Mon 19 08:00, finish
Mon 26 17:00, **duration 2880 min = the working span**, not the 1440 min sum;
late start Fri 23 08:00 (min child LS), late finish Mon 26 17:00 (max child LF);
start/finish/total slack 1920 / 0 / 0; Critical true. C1 slack 1920 not
critical, C2 slack 0 critical. After OutlineOutdent S1 is a leaf with its stored
960 min — the rolled-up duration is not stored back. W30 adds: the total is the
**finish** slack (W3-P7 settled, see above), and a 16-row PROJECT_A check.

## W15 follow-up: start-bound alignment (W3-O3)

`docs/analysis/oracle/w3-o3.json` measured FS/SS/FF/SF with lags 0/+1d/-1d/+2h.
`cpm.cpp` now snaps every start lower bound (FS, SS, SNET/SNLT/MSO) with
`next_start()` when it is not a working instant; finish bounds are never snapped.
Measured and pinned in `tests/sched_cpm_tests.cpp`: FS+1d Start Wed 21 08:00
(bound Tue 20 17:00), SS+1d Start Tue 20 08:00 (bound Mon 19 17:00), FS+2h
Start Tue 10:00 / Finish Wed 10:00. Reproduced on the real file through the plan
stream's rebuilt `recalc.exe` (uids 10757 and 10807 both 2026-05-05 08:00).

## W22 follow-up: the four remaining W3-O3 defects (done 2026-10-22)

All four are fixed in `core/sched/cpm.cpp` and pinned by the 16 measured
W3-O3 link rows in `tests/sched_cpm_tests.cpp` (`O3/type0..3/lag*`, both tasks,
all date/slack/critical fields):

1. **FF** now anchors on the predecessor's **finish instant** (FF+0: T2
   Mon 08:00-17:00, no `next_start`). FF+1d/FF+2h/T1 late dates also match.
2. **SF** is a **placed finish** (SF+0: finish Mon 08:00 mid-day, start Fri 16
   08:00; SF-1d Thu 15 -> Fri 08:00; SF+2h). `SF+1d` also matches — see below.
3. **A start-side link is not floored at the project start**: SS-1d starts
   Fri 16 08:00 with 480 min slack. The floor stays for ASAP/no-predecessor.
   A start back-computed from an SF finish is also unfloored.
4. **The FS backward inverse** is `align_back(sub_lag(succ.LS, lag))` (lag first,
   then align back to a finish instant): FS+1d T1 LF Mon 19 17:00. The same
   order fixes FF backward (FF+1d T1 LF Mon 17:00).

Two extra measured rules: an SS/SF (start-side) successor link does **not** lift
the predecessor's late finish past the project finish (SS-1d T1 LF Mon 17:00,
slack 0), and `AddWork(08:00, 480) = 17:00 the same day`, so **`SF+1d` is not
anomalous**.

## W23 (wave 23): the two remaining CPM rows are measured — both fixed

New oracle data, read only (no COM here):

- `docs/analysis/oracle/milestone-probe.json` (Project's save
  `corpus/probe/milestone1.mpp`): ID1 1 d anchor; ID2 0 d FS+0 -> start ==
  finish 17:00; ID3 0 d FS+50d -> 12/28 17:00; ID4 0 d whose Start was assigned
  17:00 -> MSP **stored SNET 10/22 08:00**; ID5 0 d SS+0 -> 08:00; ID6 1 d SNET
  10/23 08:00; ID7 0 d FS+0 after ID6 -> 17:00. **ID6 is also W30's discriminator
  for +74: row +74 = 0, +186 = +190 = 220800 ticks.**
- `docs/analysis/oracle/lagff-probe.json`: T1 5 d Mon 10/19 -> Fri 10/23;
  T2 FF-1d -> Thu 22; T3 FF-3d -> Tue 20; T4 FF-6d -> Mon 19 08:00-17:00
  (floored at the project start); T5 FF+0 -> Fri 23; T6 FF-1ed -> Thu 22.

Rules in `cpm.cpp` and pinned in `tests/sched_cpm_tests.cpp`:

1. **A 0-duration task does not snap a start bound** (`snap = duration > 0`).
2. **FF applies its lag on the raw finish instant** (`add_finish_lag`); SF does
   not use it (SF-1d lands on an interval start).
3. **The FF placement is floored at the project start** (FF-only).
4. **A backward FF lead is clamped to the successor's late finish.**
5. **Free slack never exceeds total slack** (`max(min(raw_slip, total), 0)`).
6. **`SF+1d` is not anomalous** (AddWork(Mon 08:00, 1 d) = Mon 17:00).

## Still inference (do not claim)

- CPM: ALAP scheduling itself (its stored +190 slack is now verified; its
  displayed dates are not); schedule-from-finish (W3-P5); negative free slack
  clamp (W3-P3); free slack with several measured-different successors; per-task
  calendars inside a link bound; elapsed durations/lag (refused by code;
  FF-1ed separates nothing); non-default HoursPerDay for criticality; a
  start-side link's late finish when a later constraint coexists with it.
- **A completed successor's link** — the rule is encoded and reproduces the one
  measured row (uid 10851, three stored words) and is pinned by `COMPLETED/*`,
  but it is measured on ONE row (FS) and extended to the other link types by
  inference. The alternative reading (that row's stored slack is stale) is not
  formally excluded; a COM read of uid 10851 or a fresh fixture would confirm.
- **A started task with its own real constraint**, and a **0-duration started
  task** — no oracle row; `CpmTask::actual_start` overrides links and
  constraints in the forward pass ([I]).
- W15: effort-driven add/remove (`W3-U2`); the `.mpp` units/work scales;
  summary Work/ActualWork sums and %complete; blank-summary rule; proportional
  per-assignment distribution; progress beyond the rollup sums (`W3-U3`).
- The summary FreeSlack = TotalSlack rule rests on the 16 PROJECT_A rows (+74 == +190)
  and the no-successor CPM identity; the stale PROJECT_B template writes 0 there.
  A COM read of a summary's FreeSlack on a recalculated file with total > 0
  would turn it from measured-on-PROJECT_A to measured-on-COM.

## Findings the coordinator should route

1. **Reader (W31, already landed 14:44):** `+74` FreeSlack, `+186` StartSlack,
   `+190` TotalSlack, Critical from +190. W30 measured it; W31 implemented it
   and added `free_slack_ticks`/`start_slack_ticks`.
2. **Spec §3.5 is wrong** where it says `TotalSlack = LS-ES` / "== LF-EF when
   duration is fixed": measured TotalSlack = LF-EF, with negative start-side
   conflicts only (w3-o4). §3.5/§2 also say +74 is TotalSlack (wrong). The
   coordinator owns the spec text.
3. **Spec §3.7/W3-P7:** the summary rule is recompute from the rolled-up late
   dates with TotalSlack = LF-EF (finish slack); "inherit any critical child" is
   refuted by PROJECT_A 8141. The text should also record summary FreeSlack = total.
4. **plan_recalc_tests.cpp (plan stream):** with the W31 reader the pinned PROJECT_A
   counts move again — start 83, finish 83, slack **83**, critical **83**,
   overall **332/332** (all four fields); the ALAP assertion should become 10/10
   matching (the "Project 0 vs engine 5280" divergence did not exist; it was
   +74). Do not hand this stream the golden file; the coordinator re-pins; the
   current `tests/data/project_a_recalc_golden.tsv` was not touched by W30.
5. §4.2's `SF+1d` prose is wrong (W23 item 7); the six measured link rules stand.
6. **The W3-P7 fixture the brief asked for is the PROJECT_A file itself**: summaries
   8141/10720/10761 have a critical child AND project slack, and all store
   Critical false. No new fixture is needed for the summary rule. A *new*
   fixture is only a nice-to-have for the 10851 completed-successor rule (the
   row already settles the values; a fresh fixture would exclude the stale-data
   alternative).

## Next three actions

1. **Confirm uid 10851 (nice-to-have)**: COM read of
   TotalSlack/FreeSlack/Successors for uid 10851 on `corpus/probe/project_a-recalc.mpp`
   (values only), or the synthetic A-in-progress -> B-complete -> C fixture in
   `COMPLETED/*`. The rule is already encoded and the M2 is 332/332; this only
   removes the "stale row" alternative.
2. **Re-pin `plan_recalc_tests.cpp`** (plan stream/coordinator): new counts
   83/83/83/83 = 332/332; ALAP 10/10; regenerate the golden TSV after W31.
3. **Spec update** (coordinator): §2/§3.5 stored-slack words + TotalSlack =
   LF-EF; §3.7/W3-P7 recompute + summary free = total; note that the 45
   link-family + 5 milestone mismatches were a field-identification bug.

## Exact verification commands (W30)

```
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul && cmake -S . -B build\vs-w30 -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w30 --config Release --target core_sched_date_tests core_sched_duration_tests core_sched_calendar_tests core_sched_cpm_tests core_sched_units_tests core_sched_rollup_tests recalc"
cmd /c "call `"$vc`" >nul && ctest --test-dir build\vs-w30 -C Release -R sched --output-on-failure"

# per-binary check counts: 43 125 117 1282 201 155
foreach($t in 'date','duration','calendar','cpm','units','rollup'){ & "build\vs-w30\core\sched\Release\core_sched_${t}_tests.exe" }

# M2 (needs W31's reader and W30's plan wiring; 332/332 today, no DIFF)
build\vs-w30\tools\recalc\Release\recalc.exe corpus\project_a.mpp --compare corpus\probe\project_a-recalc.mpp
```

Raw-word evidence (scratch, not committed): `build\vs-w30\rows.py <file.mpp>`
dumps +74/+186/+190/+112/+116 per row; `build\vs-w30\slack_convention.py`
compares them with the stored late dates; `build\vs-w30\plan_dump2.cpp` prints
core::plan's four slack columns (recompute mode), which `tools/recalc` does not.

## Traps

- **The three stored slack words.** +74 = FreeSlack, +186 = StartSlack,
  +190 = TotalSlack. w3-o1/outline1/lagff have all three equal, so any fixture
  built on them cannot tell them apart; milestone1 ID6 and the PROJECT_A rows can.
  Never call +74 TotalSlack again.
- **TotalSlack = LF - EF**, not min(LS-ES, LF-EF). A started task's late start
  is its actual start (StartSlack 0) while its late finish is successor-derived;
  min() gives 0 instead of the measured 16320/39360. Only a NEGATIVE
  start/constraint conflict lowers the total (MSO/SNLT -2400, MFO/FNLT -2880).
- **`CpmTask::actual_start` pins the start only.** Do not map it to MSO in
  plan.cpp: MSO pins late = early and loses the successor-derived late finish
  (that was worth 2 PROJECT_A rows).
- **Summary TotalSlack = LF-EF, summary FreeSlack = TotalSlack.** min(LS-ES,
  LF-EF) gives 0 where Project stores 6240 (8141); inheriting the min child
  TotalSlack gives 0 too. `RollupOptions::critical_mode` default is recompute.
- Units: fixed point 10000 = 100 % internally, COM 0..1; work is ticks
  internally, `.mpp` minutes x1000, COM minutes.
- Do not "repair" the under-allocation cells: Work != Duration x Units is
  measured for fixed-duration + Work and fixed-work + Duration edits.
- Summary duration is `work_time_between(start, finish)`, never the sum of child
  durations; never write it back onto the summary row.
- The rollup tree is explicit parent ids (1-based, 0 = top level); a node is a
  summary iff it has children. Do not infer it from an outline level here.
- The deadline is aligned with `align_back` before it clamps LF.
- The project start in the fixture is midnight; the forward pass aligns it. Do
  not align a constraint pin (MSO/MFO keep exact mid-day instants).
- A task with an FS/FF successor must NOT be min'ed with the project finish; an
  SS/SF-only predecessor's late finish IS capped there.
- A link bound is start-side (FS, SS) or finish-side (FF, SF); FF/SF bounds are
  PLACED finishes, never `start + duration`.
- 0-d milestones: pass `snap = (duration > 0)`; use `pred.ef` (not
  `next_start(pred.ef)`) as the FS anchor for a milestone.
- FF uses `add_finish_lag` (raw finish, lead lands on the previous interval
  END); SF must not. FF is the only floored placement. A backward FF lead is
  clamped to `succ.LF`. Free slack = `max(min(raw_slip, total), 0)`.
- `AddWork(08:00, 480)` is 17:00 the SAME day; do not read "1 day" as the next
  calendar day.
- Backward finish-side bound: `align_back(sub_lag(succ.LS/LF, lag))`; lag first.
- The week key is the **Sunday** that starts the week.
- MSVC is not on PATH: source
  `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat`,
  never build with `--parallel`. Targets are `core_sched_<module>_tests`.
  Quote the `ctest.exe` path in PowerShell. `MSB6003` is Dropbox/AV: retry once.
- A concurrent stream landing `core/cal`/`core/io` can break the CMake configure
  of every tree for a few minutes; re-run the identical build.
- clang/GCC reject unused file-scope constants/functions and missing
  initializers; `[[maybe_unused]]` any doc-only constant, value-init every
  member. MSVC only is not the CI gate.
