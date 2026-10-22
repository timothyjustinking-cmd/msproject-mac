# memory/plan.md -- core/plan: the M2 comparison, its attribution, and what is left

Status: 2026-10-22 14:10, W28 run (stream: M2 attribution in core/plan + tools/recalc).
Owned paths: `core/plan/` (plan.h, plan.cpp, attribution.h, attribution.cpp,
CMakeLists.txt), `tools/recalc/`, `tests/plan_*`, this file. `tests/data/
project_a_recalc_golden.tsv` is PINNED by the coordinator after the other streams land;
this stream must not rewrite it (see "Trap: the golden file" below).

## Read this first: the numbers moved during this run

W23 (`core/sched/cpm.cpp`, 14:03, written again at 14:06) and W24
(`core/mpp/reader.cpp`, 14:02) landed WHILE this stream was running. The tree
therefore has two snapshots in this file:

| snapshot | start | finish | slack | critical | overall | links (mapped/skipped) | fully matching uids |
|---|---|---|---|---|---|---|---|
| pre-landing, 13:35 tree (the brief's "before") | 73/83 | 69/83 | 34/83 | 52/83 | 228/332 | 92 / 5 | 23 |
| **post-landing, current tree** (14:09 rebuilt run) | **83/83** | **83/83** | **47/83** | **53/83** | **266/332** | **88 / 0** | **47** |

The cpm.cpp rewrites at 14:06/14:07/14:09 were each compiled and the comparison
re-run; the counts and the whole matrix below never changed.

Everything below is for the post-landing tree unless the line says pre-landing.
Observed delta (not a causal claim beyond the counts): `document.links` went
97 -> 88, mapped links 92 -> 88, skipped links 5 -> 0, project finish
2026-11-16 08:00 -> 2026-10-30 17:00, and every start and finish now matches
Project's own recalculation. The families W20 listed as (1) milestones at an
interval end, (2) the ignored 10859 link and (3) the 10846 cascade no longer
produce a DATE divergence. W25's calendar decoder (`core/cal`) was still absent
at 14:07, so the calendar is still the hard-coded standard one.

## The W28 deliverable: attribution of every mismatch

`core/plan/attribution.h/.cpp` classify ONE mismatching (uid, field) into exactly
one family. `tools/recalc --compare` prints the family x field matrix after the
existing tables; `--attrib-json <path>` writes the same counts plus one object
per mismatching (uid, field): `{uid, field, family, ours, stored}`. Uids, dates
and values only -- no task name or exception name is ever written.

### Family rules (precedence = the first row that applies wins)

1. **actual** -- `start` field with `start_is_actual`, or `finish` with
   `finish_is_actual`. A kept actual date is a stored fact (§2.4), not a
   forecast. Date fields only: a slack/critical mismatch on a started row is a
   network consequence, not a recorded instant.
2. **summary** -- `ours.summary || stored.summary`. A rolled-up row's values are
   aggregates of its children.
3. **milestone** -- `duration == 0`. core/sched's interval-end placement rule
   could not reproduce a 0-duration task at an end instant; start == finish, so
   one placement question shows in both date fields.
4. **constraint** -- `has_date_constraint`: a date-bearing PjConstraint
   (MSO/MFO/SNET/SNLT/FNET/FNLT) WITH its date stored, or a deadline. A
   downgraded (date-less) constraint does not count.
5. **link** -- `has_predecessor`: at least one mapped, schedulable link names the
   row as successor (links to deleted/unknown/summary rows are dropped first).
6. **calendar_or_start** -- a leaf, not a milestone, with no date constraint/
   deadline, no predecessor and no actual. Its only inputs left are the calendar
   and the project start.
7. **unexplained** -- the fallback, justified in attribution.h: reachable only by
   a leaf that KEEPS an actual but whose mismatching field is not that actual
   (slack/critical on a started row, or the finish forecast of an in-progress
   row). It is not called `actual` because the field is not a date, and not
   `calendar_or_start` because neither the calendar nor the project start moved
   the row.

`has_date_constraint` and `has_predecessor` are new MEASUREMENT-ONLY fields on
`PlanTaskResult`, filled by `recalculate()`; they do not reach the CPM engine and
change no scheduling semantics.

## Attribution matrix, post-landing tree (the "before" for the next merge)

`recalc corpus\project_a.mpp --compare corpus\probe\project_a-recalc.mpp`
(83 tasks, 16 summaries, 88 links, 0 skipped, 13 stored constraints all with a
date, 0 deadlines, 21 actual starts / 18 actual finishes)

| family | start | finish | slack | critical | total |
|---|---|---|---|---|---|
| actual | 0 | 0 | 0 | 0 | 0 |
| summary | 0 | 0 | 8 | 8 | 16 |
| milestone | 0 | 0 | 3 | 2 | 5 |
| constraint | 0 | 0 | 0 | 0 | 0 |
| link | 0 | 0 | 25 | 20 | 45 |
| calendar_or_start | 0 | 0 | 0 | 0 | 0 |
| unexplained | 0 | 0 | 0 | 0 | 0 |
| **mismatch** | **0** | **0** | **36** | **30** | **66** |
| **match** | **83/83** | **83/83** | **47/83** | **53/83** | 266/332 fields |

Every remaining mismatch is a slack/critical field. There is no date mismatch
left on PROJECT_A, so the three date families are empty for a structural reason, not a
classifier bug: all 21 actual-start and 18 actual-finish rows match, and the
milestone/link/constraint date rows now match too. `actual` is unit-tested on
synthetic rows even though PROJECT_A does not exercise it.

First mismatches per family (uids/values only):

* summary: 8141 slack 0.0 vs 6240.0; 8141 critical yes vs no; 10720 slack 0.0 vs
  10080.0; 10720 critical yes vs no.
* milestone: 10742 slack 18240.0 vs 8160.0; 10743 slack 10080.0 vs 0.0; 10743
  critical no vs yes; 10876 slack 15360.0 vs 0.0.
* link: 10763 slack 28320.0 vs 0.0; 10763 critical no vs yes; 10768 slack 28320.0
  vs 0.0; 10768 critical no vs yes.
* constraint / actual / calendar_or_start / unexplained: none.

## Attribution matrix, pre-landing 13:35 tree (for the delta)

Derived with a Python replica of the SHIPPED rules from the saved pre-landing
`--compare`, `--fields` and `--links` output (the C++ tool was built after the
landing; the replica and the C++ tool then agreed exactly on the post-landing
run, which is the cross-check).

| family | start | finish | slack | critical | total |
|---|---|---|---|---|---|
| actual | 0 | 0 | 0 | 0 | 0 |
| summary | 0 | 4 | 10 | 8 | 22 |
| milestone | 3 | 3 | 4 | 2 | 12 |
| constraint | 1 | 1 | 1 | 0 | 3 |
| link | 6 | 6 | 34 | 21 | 67 |
| calendar_or_start | 0 | 0 | 0 | 0 | 0 |
| unexplained | 0 | 0 | 0 | 0 | 0 |
| **mismatch** | **10** | **14** | **49** | **31** | **104** |
| **match** | **73/83** | **69/83** | **34/83** | **52/83** | 228/332 |

So the landing removed 38 mismatches: 10 start + 14 finish (all date
mismatches), plus 15 slack + 1 critical. The remaining 66 fields are slack (36)
and critical (30). On the pre-landing tree, the constraint family was exactly
the 10859 row (start, finish, slack); it is gone now.

## Verification commands (from the repo root)

```
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul && cmake -S . -B build\vs-w28 -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w28 --config Release --target recalc plan_recalc_tests plan_attribution_tests"

# attribution + JSON (the deliverable; prints the matrix and writes the rows)
build\vs-w28\tools\recalc\Release\recalc.exe corpus\project_a.mpp --compare corpus\probe\project_a-recalc.mpp --attrib-json build\vs-w28\attrib.json

# classifier unit test: 54 checks
build\vs-w28\core\plan\Release\plan_attribution_tests.exe
cmd /c "call `"$vc`" >nul && ctest --test-dir build\vs-w28 -C Release -R plan_attribution --output-on-failure"

# existing M2 test -- see the warning below before running it
build\vs-w28\core\plan\Release\plan_recalc_tests.exe
```

Measured 2026-10-22 14:04-14:12 on the post-landing tree: `/W4 /WX` clean (MSVC,
binary built against `core/sched/cpm.cpp` 14:09:05 -- W23 was still editing it);
`plan_attribution_tests` 54 checks passed; `plan_recalc_tests` **6 of its 186
checks FAIL** (see below); `recalc --compare` prints the matrix and writes 66
JSON rows; a validation script checked every JSON `ours`/`stored` value against
date/number/yes/no/NA patterns (no names).

Build notes for the next stream:

* W23 wrote `cpm.cpp` at 14:03, 14:06, 14:07:40 and 14:09:05. After each visible
  change this stream rebuilt and re-ran; the counts and the full matrix above
  never moved. That is the evidence that the numbers are not an artefact of one
  intermediate cpm state.
* W25's `core/cal/` appeared at 14:07 with a `CMakeLists.txt` that references
  `tests/cal_tests.cpp` before that file existed, so the SHARED top-level
  configure failed for ~2 minutes (14:07:18-14:09). It recovered by 14:09:03;
  nothing was changed outside this stream's lane. If it recurs, the standalone
  fallback that does not configure `core/cal` is:
  `cmake -S core\plan -B build\vs-w28-plan -G "Visual Studio 17 2022" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON`
  then build `recalc plan_recalc_tests plan_attribution_tests` there (binaries
  land in `build\vs-w28-plan\Release\` and `build\vs-w28-plan\tools\recalc\Release\`).
  The current-source standalone run also measured 266/332 and the same matrix.

### plan_recalc_tests currently FAILS 6/186 checks -- do NOT "fix" it here

The pinned constants in `tests/plan_recalc_tests.cpp` predate W23/W24. On the
current tree the 6 failures are:

* "92 of PROJECT_A's 97 links have two live endpoints" -- actual 88;
* "all 5 links that name the deleted task 10856 are skipped and counted" --
  actual 0;
* the four recorded counts: start 83 (pinned 73), finish 83 (69), slack 47 (34),
  critical 53 (52).

The coordinator updates these constants (and the link assertions) after the
other streams land; W28 must not. The total check count is still 186.

### Trap: the golden file

`plan_recalc_tests` rewrites `tests/data/project_a_recalc_golden.tsv` on every run
when the corpus is present. On this tree it emits 47 fully matching uids, which
is NOT the pinned file. W28 backed the file up before running the test and
restored it; its pinned content is 23 uids + header (sha256 of the file
`58214E1A0CE3AFAA3A997F74FC8DC9A8212B7CA27C72DFC9A6E3028DF2A9C2E0`). Check and
restore it after any test run if you are not the coordinator. The file was in
fact rewritten again at 14:07:01 by a CONCURRENT process in this shared tree
(not by this stream) while W28 was verifying -- so the coordinator should
expect to re-pin it once, after W23/W24/W25 are done, and not treat any 47-row
content as a W28 edit.

## Files touched by W28

* `core/plan/attribution.h`, `core/plan/attribution.cpp` -- NEW: the classifier,
  `StoredRow`, the family/field enums and the stable names used by the table and
  the JSON.
* `core/plan/plan.h` -- `PlanTaskResult` gains `has_date_constraint` and
  `has_predecessor` (attribution inputs only; documented as no scheduling
  effect).
* `core/plan/plan.cpp` -- fills those two fields from the mapped constraint/
  deadline and from the links that actually reach the CPM graph. The only other
  change is a `has_predecessor` vector while mapping links. No CPM/rollup
  behaviour changed.
* `core/plan/CMakeLists.txt` -- builds `attribution.cpp`; adds
  `plan_attribution_tests` and its ctest entry (`plan_attribution`).
* `tools/recalc/main.cpp` -- `--attrib-json <path>`; the attribution matrix and
  first-uid examples after the existing tables; the four comparisons now call
  `core::plan::field_matches` (identical exact equality). The existing DIFF
  lines and the three all/leaves/summaries tables are unchanged in order and
  meaning.
* `tests/plan_attribution_tests.cpp` -- NEW: 54 checks, synthetic
  `PlanTaskResult`/`StoredRow` pairs for every family incl. the fallback, the
  precedence claims (actual>link, summary>milestone, constraint>link,
  milestone>link), the fallback boundary (same row with a predecessor becomes
  link) and the enum/name contract. No PROJECT_A counts.
* `tests/plan_recalc_tests.cpp` -- NOT touched; `tests/data/project_a_recalc_golden.tsv`
  restored to its pinned 23-row content.

## What is still open (slack/critical only)

1. **Link-family slack is the largest bucket: 45 fields (25 slack + 20 critical)
   on rows that now have correct dates.** Ours is usually much larger than
   Project's stored TotalSlack (10763: 28320 min vs 0; 10768 the same; 10774:
   33120 vs 28320). This is now a pure `core/sched` backward-pass / TotalSlack
   question, not a bridge or link-liveness question. W23's lane.
2. **Summary slack: 16 fields (8 slack + 8 critical).** W3-P7 (inherit vs
   recompute) is still selected by `PlanInput::summary_critical_mode` (default
   `inherit`); the oracle still cannot distinguish them. A one-line experiment
   (`recompute`) is cheap and should be run when the coordinator can use COM.
3. **Milestone slack: 5 fields (3 slack + 2 critical)** now that milestone DATES
   match (e.g. 10742 slack 18240 vs 8160, 10743 10080 vs 0). A 0-duration task's
   slack rule is `core/sched`, open.
4. **W25 calendar**: `114/TBkndCal` is still undecoded at 14:06 (`core/cal`
   absent). When it lands, re-run the matrix: dates may move, and that is the
   first run where `calendar_or_start` can legitimately become non-zero.
5. **ALAP TotalSlack** (schedin uid 2: stored 0, engine 5280) is still open in
   `core/sched`; see the pinned assertion in plan_recalc_tests.
6. The M2 date fields are now 83/83 on PROJECT_A with the current merge; if CI's
   whole-suite run disagrees, check the reader/cpm mtimes first -- other streams
   were writing during this measurement.

## Traps (W28)

* Two other streams wrote `core/mpp/reader.cpp` and `core/sched/cpm.cpp` at
  14:02-14:03 while this stream was building; the pre/post-landing split above
  is deliberate. Always record the tree time (or file mtimes) with a count.
* Running `plan_recalc_tests` rewrites the pinned golden file (above).
* `/W4 /WX` clean here; the new file-scope arrays are all used, so no
  `[[maybe_unused]]` was needed. CI clang/GCC strictness: no unused constants.
* The attribution JSON is written with escaped backslashes/quotes; all per-row
  values are dates, numbers, yes/no or NA by construction (validated).
* `--attrib-json` without `--compare` returns 2; an unwritable path returns 1.
