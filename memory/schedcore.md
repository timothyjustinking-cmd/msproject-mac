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

- CPM: ~~ALAP scheduling itself~~ **MEASURED in wave 9b (W41, see the last
  section: the displayed dates ARE the late dates, confirmed by COM)**;
  schedule-from-finish (W3-P5); negative free slack clamp (W3-P3); free slack
  with several measured-different successors; per-task calendars inside a link
  bound; ~~elapsed lag~~ **MEASURED in wave 9b (W41)** — elapsed DURATION is still
  open and is a `core/mpp` format-word gap; non-default HoursPerDay for
  criticality; a start-side link's late finish when a later constraint coexists
  with it; an ALAP task that HAS successors (only the no-successor row is
  measured); the FF floor for a 0-d successor (W41 item 4).
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

## W41 (wave 9b, 2026-10-22 16:55) — ALAP closed, elapsed lag measured

Stream: `core/sched` + `core/plan` + `core/cal`; own build dir `build\vs-w41`.
Nothing was run through git, MS Project was not driven, no corpus name appears
below (uids/dates/counts only). Files touched by this stream:
`core/sched/cpm.h`, `core/sched/cpm.cpp`, `core/plan/plan.cpp`,
`core/plan/CMakeLists.txt`, `core/cal/cal.h`, `core/cal/cal.cpp`,
`tests/sched_cpm_tests.cpp`, `tests/plan_recalc_tests.cpp`, `tests/cal_tests.cpp`,
`memory/schedcore.md`, `memory/plan.md`, `memory/cal.md`.

### (1) ALAP display dates — CLOSED; the engine was already right

**Reader-facing finding (the coordinator asked for this sentence verbatim):**
the reader's +96/+100 calculated pair and +104/+108 effective pair are NOT
interchangeable on an ALAP row.

Measured from the stored row of `corpus/probe/schedin.mpp` uid 2 (dump:
`python build\vs-w30\rows.py corpus\probe\schedin.mpp`, offsets from
`spec/mpp-records.md`):

| word | value | meaning on an ALAP row |
|---|---|---|
| +96/+100 (calculated) | **2026-11-03 08:00 / 17:00** | the ALAP placement |
| +104/+108 (effective) | **2026-10-19 08:00 / 17:00** | the ASAP early dates |
| +112/+116 (late) | 2026-11-03 08:00 / 17:00 | the same late dates |
| +74 FreeSlack | 0 ticks | |
| +186 StartSlack | 0 ticks | |
| +190 TotalSlack | 52800 ticks = 5280 min | LF - EF on the early dates |

`docs/analysis/oracle/schedin-probe.json` ID 2 reports COM Start
2026-11-03 08:00 and Finish 2026-11-03 17:00 — i.e. Project's own displayed
dates for the ALAP task ARE the late placement, which is the CALCULATED pair, and
the effective pair still holds the early dates.  So:

* the engine's ALAP display rule (`display_late` -> late dates) was **already
  correct**; the old "18/20 dates, ALAP is the known divergence" pin was
  comparing it with the wrong stored word — the same class of bug as W30's
  +74-as-TotalSlack misread, not an engine defect;
* **new engine rule (measured, one row)**: an ALAP task is placed at its late
  start, so `start_slack` and `free_slack` are **0** (row +186 and +74), while
  `total_slack` stays LF - EF (row +190 = 52800). `cpm.cpp` encodes it; the `[I]`
  scope is that it is measured on that single ALAP row (the only one in the
  corpus);
* `tests/sched_cpm_tests.cpp` now has a measured ALAP block (schedin's shape:
  1 d ASAP + 1 d ALAP + 2 d SNET 11-02 driving the project finish) pinning
  early = 10-19, late = 11-03, display = late, slack words 0/0/5280 and the ASAP
  control row's 5280/5280/5280;
* `tests/plan_recalc_tests.cpp` compares the ALAP row against the calculated
  pair and pins the two pairs as different: schedin is now **20/20 dates and
  10/10 slack**, no divergence.

### (2) Elapsed lag — measured and implemented (fixture A)

`corpus/probe/w9-elapsedlag.mpp` + `docs/analysis/oracle/w9-elapsedlag.json`
(built by the coordinator, recipe `python scripts/oracle_w9.py elapsedlag`).
Link rows decoded with `python build\vs-w41\links.py <file>` (TBkndCons
FixedData is 20 bytes/row: +12 u16 type, +14 i32 lag ticks, **+18 u16
LagFormat**):

| pred -> succ | lag ticks | LagFormat | spelling |
|---|---|---|---|
| 1 -> 2 | 28800 (=48 h) | **8** | "2 edays" |
| 1 -> 3 | 43200 (=72 h) | **8** | "3 edays" |
| 1 -> 4 | 9600 (=16 h) | **7** | "2 days" |
| 5 -> 6 | 28800 | **8** | "2 edays" |
| 5 -> 7 | 9600 | **7** | "2 days" |

So **LagFormat 7 = working days, 8 = elapsed days**, Project did NOT silently
convert an elapsed lag to working days, and an elapsed lag's stored tick value is
already wall-clock time (2 edays = 28800, 3 edays = 43200).  Two single-variable
pairs prove it (same predecessor, same "2"-day spelling, only the unit differs).

Two measured scheduling rules, both now in `cpm.cpp`:

1. **An elapsed lag is plain packed-date arithmetic** — the calendar is not
   walked (`add_elapsed`), forward and backward.
2. **An elapsed lag anchors on the predecessor's RAW finish instant**, not on its
   aligned next start.  Discriminators (do not lose these):
   * FS+3 edays from Wed 10-21 17:00 = Sat 10-24 17:00 -> Start **Mon 10-26
     08:00** (Project).  Three WORKING days would start **Tue 10-27 08:00**, so
     the dates do separate the two readings — on this row, not on the 2-eday
     rows, where 48 h from a 17:00 finish and 2 working days coincide.
   * FS+2 edays from **Fri** 10-23 17:00 = Sun 17:00 -> Start Mon 10-26 08:00
     (Project).  Aligning first (Mon 08:00 + 48 h = Wed 10-28 08:00) is exactly
     the WORKING-day answer of the sibling row, so this row is what fixed the
     anchor rule after the first engine cut got it wrong.

Code: `CpmLink::elapsed_lag` is honoured; `CpmError::elapsed_lag_unsupported` is
**gone** (two tests that pinned the refusal were corrected, with a comment saying
the old assertion encoded an incomplete rule).  `core/plan` now maps the field:
`lag_is_elapsed(lag_units)` = true for 8 (measured) and for every even value
4..12 (the enum's +1 = elapsed rule, spec §0.4 [B]) — PROJECT_A/PROJECT_B store 7, so M2
is untouched.

### (3) Elapsed DURATION — measured, NOT implemented; it is a `core/mpp` gap

T8 in the same fixture is "2 edays" of DURATION: the row stores **28800 ticks**
(= 48 h) and Project schedules it Mon 2026-10-19 08:00 -> **Wed 2026-10-21
08:00**, with its slack words computed in elapsed time (+190 = 106200 ticks =
10620 min).  The engine cannot reproduce it: **stored 28800 ticks is
indistinguishable from a six-day working duration because `core/mpp` exposes no
duration-format word**.  Project's answer is measured and in
`docs/analysis/oracle/w9-elapsedlag.json`; no new fixture is needed, only the
reader field (PjFormatUnit for the task duration row word) — wave 10 with the
reader.  `tests/plan_recalc_tests.cpp` pins the divergence explicitly (T8 start
matches, finish is asserted to differ today, so a fix shows up as that assertion
failing).

### (4) FF floor for a 0-duration successor — still inference, still flagged

Unchanged and deliberately not touched (no new oracle row; the brief says do not
silently change FF behaviour).  The code path is `has_ff_link`:
`finish_bound = max(finish_bound, add_work(floor, duration))` with
`floor = next_start(project_start)`; for `duration == 0` that floors a milestone
to the project's first working instant.  The reasoning from the two measured
rules (FF placement floored at the project start for a 1 d successor; a 0-d
successor keeps a PLACED instant and never snaps) is consistent with that, but
no fixture separates "floor to the first working instant" from "floor to the raw
project-start instant" for a milestone.  A fixture would be: 5 d task + 0 d
successor with FF-6d, plus the same with FF+0.

### Counts (wave 9b, build\vs-w41, all passing)

| binary | wave-8 | now |
|---|---|---|
| `core_sched_date_tests` | 43 | 43 |
| `core_sched_duration_tests` | 125 | 125 |
| `core_sched_calendar_tests` | 117 | 117 |
| `core_sched_cpm_tests` | 1282 | **1308** |
| `core_sched_units_tests` | 201 | 201 |
| `core_sched_rollup_tests` | 155 | 155 |
| `cal_tests` | 165 | **217** |
| `plan_attribution_tests` | 54 | 54 |
| `plan_recalc_tests` | 185 | **231** |

`ctest -R "sched|plan|flip"` = 8/8 passed.  M2 re-run on the final tree:
**start 83/83, finish 83/83, slack 83/83, critical 83/83 = 332/332**, no DIFF
lines, attribution matrix all zero.  `tests/data/project_a_recalc_golden.tsv` was
restored to its pinned content after every `plan_recalc_tests` run (sha256
`DB81218ECCC9219B2DA9864D5F3A0387FBB16BC5D8F027089E826B7BEFC46A0A`, 83 uids).

### Traps added by this stream

* **+104/+108 is not "the displayed date" on an ALAP row.** The calculated
  +96/+100 and the late +112/+116 hold the ALAP placement; the effective pair
  holds the early dates.  Any comparison of an ALAP task's dates must pick the
  pair deliberately (plan_recalc_tests does, per row).
* **An ALAP task's stored StartSlack and FreeSlack are 0** while its TotalSlack
  is LF - EF.  Do not derive StartSlack from LS - ES for an ALAP row.
* **LagFormat lives at link row +18**: 7 working, 8 elapsed.  An elapsed lag is
  plain wall-clock arithmetic AND anchors on the raw predecessor finish.
* `CpmError` no longer has `elapsed_lag_unsupported`; the numeric values after it
  shifted, so nothing may persist those integers.
* The elapsed-duration control (28800 ticks) is NOT six working days; nothing in
  `core/sched` can tell the difference until the reader exposes the format word.

## Next three actions (wave 10, from the W41 state)

1. **Elapsed duration** (the only measured row the engine still misses): get a
   task duration-format word (PjFormatUnit) exposed from `core/mpp`, set a
   `CpmTask::elapsed_duration` flag in the bridge, and schedule it as
   `add_elapsed` forward / `add_elapsed` backward, with slack measured in elapsed
   time.  The expected values are already measured: T8 of
   `corpus/probe/w9-elapsedlag.mpp` = Mon 2026-10-19 08:00 -> Wed 2026-10-21
   08:00 with +190 = 106200 ticks.  `tests/plan_recalc_tests.cpp` already asserts
   that this row's finish diverges, so the fix flips one assertion.
2. **ALAP with successors / ALAP start-slack wider check**: the W41 rule is
   measured on one no-successor row.  A fixture with an ALAP task that HAS
   successors, and one where the ALAP task is also started, would settle whether
   StartSlack/FreeSlack stay 0 and where the late dates come from.
3. **FF floor for a 0-duration successor**: one fixture (5 d task + a 0 d
   successor with FF-6d, and the same with FF+0) separates "floor to the first
   working instant" from "floor to the raw project start".  Until then the
   behaviour in `cpm.cpp` (`has_ff_link` block) stays flagged as inference.

## W49 addendum (wave 11, 2026-10-22 18:55) — elapsed DURATION closed; task calendars reach CPM

Owned files touched by this stream: `core/sched/cpm.h`, `core/sched/cpm.cpp`,
`tests/sched_cpm_tests.cpp`, `core/plan/plan.{h,cpp}`, `core/plan/CMakeLists.txt`,
`tests/plan_recalc_tests.cpp`, `core/mpp/reader.{h,cpp}` (W49 fields only; W48's
`field_value.{h,cpp}` untouched), `tools/recalc/main.cpp`, plus memory/handoff.
No client name is used below (uids/dates/counts only).

### (1) Elapsed DURATION is now reproduced — the W41 open item is CLOSED

The reader now exposes the task row's **+164 (0xa4) DurationFormat** (see
`memory/reader.md`): 8 = elapsed days.  `CpmTask::elapsed_duration` is set by
`core::plan::from_mpp` from `Task::duration_is_elapsed()`, and `cpm.cpp` routes
all task-duration arithmetic through three helpers:

* `add_duration` / `sub_duration` — `add_elapsed` for an elapsed task, otherwise
  `Calendar::add_work` / `sub_work`;
* `duration_delta` — `elapsed_delta` (wall clock, seconds/6) for an elapsed task,
  otherwise `work_delta` (working time).

MEASURED, `corpus/probe/w9-elapsedlag.mpp` T8 (oracle
`docs/analysis/oracle/w9-elapsedlag.json`): duration 28800 ticks, duration
format 8, scheduled **Mon 2026-10-19 08:00 -> Wed 2026-10-21 08:00** (48 wall
clock hours, not six working days), LateFinish Wed 10-28 17:00, LateStart Mon
10-26 17:00, and Start/Finish/Total/Free slack all **106200 ticks = 10620
min** (wall-clock LF-EF).  All 16 date fields and all 10 slack values of the
fixture now match; `plan_recalc_tests` changed from 15/16 to 16/16 dates and the
T8-diverges assertion is gone.  `tests/sched_cpm_tests.cpp` adds the same row
plus a no-flag control (28800 ticks = six working days -> Mon 10-26 17:00).

[I] scope: the measured row has no links and no constraints.  The engine applies
the elapsed rule to MFO/FNET/FNLT constraint arithmetic and to the task's own slack;
a link that mixes an elapsed-duration task with a working-time successor, and
an elapsed task with a START/FINISH constraint, are unmeasured.

### (2) Per-task calendars in CPM — engine side was already present, bridge wired

`CpmTask::calendar` existed since W12; W49's new work is
`core::plan::attach_task_calendars` (one `Calendar` per distinct
`Task::calendar_uid`, built by `core::cal`) and `recalculate()` selecting
`task.calendar_uid`'s calendar or the project calendar.  `PlanInput::calendar`
is unchanged as the project/default calendar; `PlanInput::task_calendars` is the
new owned vector.

MEASURED (wave 11, `corpus/probe/holiday1.mpp`, oracle
`docs/analysis/oracle/holiday-probe.json`): all three tasks store +178 = 3
(SiteCal with a Wednesday exception and a 4 h Friday).  The engine now produces
T1 Mon 10-19 08:00 -> Tue 10-20 17:00, T2 Thu 10-22 08:00 -> Thu 10-29 12:00,
T3 Fri 10-23 08:00 -> Mon 10-26 12:00, project finish Thu 10-29 12:00, with
T3 total slack 1440 min.  The control run (same rows, every +178 reset to -1)
gives T2 Wed 10-21 08:00 -> Tue 10-27 17:00 and T3 Fri 10-23 08:00 -> Fri
10-23 17:00, proving the calendar (not the dates) moved the two tasks.
`test_task_calendars` in `tests/plan_recalc_tests.cpp` pins both runs.

PROJECT_A and PROJECT_B store +178 = -1 on every task row, so `input.task_calendars` is
empty and M2 remains **332/332** (83/83 start, finish, slack, critical; no DIFF
lines; attribution matrix all zero).  Summary rollup still uses the project
calendar (spec §1.6 [I]); no measured summary has a task calendar.

### Counts (W49, build\vs-w49, Release)

| binary | before W49 | now |
|---|---|---|
| `core_sched_cpm_tests` | 1308 | **1321** |
| `plan_recalc_tests` | 231 | **277** |
| `mpp_reader_tests` (corpus / no corpus) | 663 / 316 | **685 / 324** |
| `cal_tests` (corpus / no corpus) | 217 / 90 | 217 / 90 |
| `ctest -R "mpp|cal|sched|plan"` | 21/21 | **23/23** |

M2 command: `build\vs-w49\tools\recalc\Release\recalc.exe corpus\project_a.mpp --compare corpus\probe\project_a-recalc.mpp`
-> **332/332**, `tests/data/project_a_recalc_golden.tsv` SHA256 unchanged
`DB81218ECCC9219B2DA9864D5F3A0387FBB16BC5D8F027089E826B7BEFC46A0A`.
Schedin: **20/20 dates, 10/10 slack**.  Reader: **PROJECT_A 83 tasks, PROJECT_B 73**.
`mspdi_cmp` PROJECT_A and PROJECT_B still **MATCH** (resources 22/22 each).

### Traps added

* Elapsed duration is a FORMAT flag, not a different tick scale: 2 edays is
  stored as 28800 ticks and is scheduled with `add_elapsed`, not `add_work`.
  A 28800-tick task without the flag is six working days.
* Do not put core_cal behind core_plan only for tests: `core_plan` now links
  `core_cal` PUBLIC because plan.cpp calls `build_calendar`.
* `Task::calendar_uid` is an i32; -1 must keep the project calendar.  Do not
  build a calendar for uid <= 0.
* `attach_task_calendars` is a separate call: `from_mpp` has no path and cannot
  decode the calendar.  A caller that forgets it silently gets the old
  project-calendar behaviour (tools/recalc and plan_recalc_tests call it).
* Recurring exception parameters are still NOT decoded; W49 did not change
  `core/cal`'s expansion.  Do not claim the weekday mask / every-N / position /
  month-day words are measured.

