# Engine and backend parity — differential testing against MS Project

**Requirement (2026-10-23).** The engine and backend, like the UI, must be
*identical to MS Project on this machine*. That is a behavioural claim, so it has
to be measured by running the same inputs through both and comparing every
observable — not by "we implemented the algorithm".

MS Project is installed here and is the only authority. Where our specification
and Project disagree, Project wins (`docs/coordination.md`).

## What "identical" means, observably

For a scenario (a base file plus an ordered list of operations), compare a
**canonical state dump** produced from the file Project saved and from the file
our engine wrote. The dump is numbers and ids only, never names:

| family | fields compared |
|---|---|
| task | uid, id, outline level, summary/milestone/active/manual flags, duration ticks, start, finish, early/late start and finish, free/start/total slack, critical, percent complete, constraint type + date, deadline, task type, effort-driven, work, cost |
| assignment | uid, task uid, resource uid, units, work, start, finish, contour/step count |
| resource | uid, type, max units, standard/overtime rates, calendar uid, cost |
| calendar | base calendar, work week, every exception (type, dates, occurrences, period, weekdays, month position/item/day) |
| project | start/finish, calendar, critical slack limit, currency, default task type |
| file | which streams changed, and stream sizes (not bytes — identity values churn) |

A scenario **passes only if every compared field matches exactly**. A tolerance
would hide the class of bug this exists to catch (off-by-one ticks, wrong
calendar boundary, missed rounding).

## How it runs

* `tests/scenarios/*.json` — the scenarios: base fixture name, operations
  (edit/insert/delete/link/constrain/recalculate/save), and the fields to compare.
  Committed; ids and numbers only.
* `scripts/engine_diff.py` — two halves:
  1. **Oracle half** (coordinator only, `--record`): opens the scenario in MS
     Project through `scripts/oracle_lock.py`, applies the operations with COM,
     saves, and dumps the canonical state of Project's own result to
     `tests/data/scenarios/<name>.oracle.json`.
  2. **Ours half** (`--check`, CI-safe): applies the same operations through our
     engine, writes the file, dumps the same state, and diffs it against the
     recorded oracle state. Exit non-zero on any difference.
* `docs/engine-parity.md` (this file) carries the current score: scenarios
  passing / total, and the open differences with the instruction that reproduces
  each.

Scenarios are grouped by family so a wave can close one family at a time:
scheduling, calendars, fields/values, links, resources/assignments, file format.

## Where we already stand

The existing gates are single-scenario versions of this idea and must keep
passing: **M2 332/332** (83 tasks × start/finish/slack/critical against Project's
own recalculated file), schedin 20/20 + 10/10, `plan_recalc` 291, `cal` 553,
reader 685, assignments 679, links 773, add_delete 760, field_values 802,
formula 1,249, viewstate 361. The writer is already byte-identical to Project's
own rows for task/link/resource/assignment adds and deletes, and Project displays
our custom-field values.

What those do **not** cover, and this harness exists to find: task-type and
effort-driven interactions, constraint semantics (all eight, with deadlines and
ALAP), elapsed durations mixed with calendars, task and resource calendars,
splits and contour shaping, cost accrual and earned value, material resources,
multiple baselines, summary rollup corners, inactive tasks, and MPP12/14
variants.

## Honest scale

MS Project's scheduling engine is twenty years of accumulated behaviour,
including leveling, contouring, cost accrual and earned value. "Identical on
everything" is a large programme: the realistic path is a growing scenario corpus
whose score is published, closing one family per wave, with the gaps named rather
than assumed away. A family counts as done only when its scenarios pass and the
families it touches stay green.

## Rules this harness must keep

1. **Project wins.** A difference is a bug in our engine until shown otherwise;
   then the specification is wrong and gets corrected with the evidence.
2. **No tolerance.** Exact equality per field, or the scenario fails with the
   field, the two values and the reproduction command.
3. **No client data.** Scenarios use synthetic fixtures; recorded oracle states
   carry ids, dates and numbers only.
4. **The oracle half stays coordinator-only** (COM); the recorded JSON is what CI
   compares against, so CI needs no Project.

## Measured baseline (wave 17, 2026-10-23)

The harness now exists as code. **No oracle state has been recorded yet**, so the
score below is a baseline of the machinery, not a parity claim. Every scenario
is `UNRECORDED`; turning one into a scored result needs one coordinator run of
`--record` (the COM half cannot run from a stream: `docs/coordination.md`
Revision 3).

### Scenario corpus

`tests/scenarios/*.json`, all on the one committed synthetic fixture
`tests/data/synthetic-base.mpp` (3 tasks, 1 resource, empty link table). Ids and
numbers only; no names.

| file | operations | family it starts to measure |
|---|---|---|
| `01-recalculate.json` | `recalculate`, `save` | scheduling: project rollup and CPM dates |
| `02-duration-change.json` | `set_task_field` uid 1 Duration = 14400 ticks (3 d), `recalculate`, `save` | duration edit and its effect on finish/slack |
| `03-link-fs-lag.json` | `add_link` 1→2 FS +4800 ticks (1 d), `recalculate`, `save` | links: FS with lag |
| `04-constraint-snet.json` | `set_task_field` uid 2 ConstraintType = 4 (SNET), ConstraintDate = 2026-02-02 08:00, `recalculate`, `save` | constraints: SNET |
| `05-constraint-mso.json` | `set_task_field` uid 3 ConstraintType = 2 (MSO), ConstraintDate = 2026-01-20 08:00, `recalculate`, `save` | constraints: MSO |
| `06-assignment-50pct.json` | `add_assignment` task 1 / resource 1 at 50 %, `recalculate`, `save` | resources/assignments: units and work |
| `07-task-calendar.json` | `set_calendar` task 1 → calendar uid 2, `recalculate`, `save` | calendars: task calendar selection |
| `08-delete-task-with-successor.json` | `add_link` 1→2, `add_link` 2→3 FS, `delete_task` uid 2, `recalculate`, `save` | delete cascade with a successor |

### Current score

| scenario | status | compared fields |
|---|---|---|
| recalculate | UNRECORDED | 0 |
| duration-change | UNRECORDED | 0 |
| link-fs-lag | UNRECORDED | 0 |
| constraint-snet | UNRECORDED | 0 |
| constraint-mso | UNRECORDED | 0 |
| assignment-50pct | UNRECORDED | 0 |
| task-calendar | UNRECORDED | 0 |
| delete-task-with-successor | UNRECORDED | 0 |

**0 of 8 scored. 8 need a coordinator run.** A `--check` on an unrecorded
scenario exits 3 and says so; `--suite` exits 3 while any scenario is
unrecorded (1 if a recorded scenario differs). The our-side machinery is green:
`engine_diff_apply` (159 checks) and `engine_diff_harness` (41 checks).

### What the first recording is expected to expose (named, not assumed away)

These are visible in the our-side dumps today and follow from code already in
the tree, so a first recording should show them rather than discover them:

1. **Manual scheduling.** `synthetic-base.mpp`'s three tasks are stored
   Manually Scheduled (`Fixed2Meta` bit 0x80 set; the reader reports
   `manually_scheduled = true`, and task 1 with zero slack is not Critical).
   MS Project's `CalculateProject` does not reschedule a manual task; our engine
   maps every row onto CPM and does. `core/mppwrite/save.h` records the measured
   W37 result that a manual row ignores an SNET pin unless the mode bit is
   cleared. Expect task start/finish/early/late and summary differences until
   manual scheduling (or a deliberate "convert to auto" operation) exists.
   `set_calendar` has the same dependency: a manual task keeps its date, so the
   calendar change is stored but not rescheduled by Project.
2. **File stream sizes.** `--record` dumps Project's own saved file, whose save
   churns `Var2Data`/`214/` (Project compacts and re-keys; our writer appends and
   preserves `214/`). The `file.streams` sizes are compared exactly, by design;
   operation scenarios that grow or tombstone rows should therefore differ there
   until the writer mirrors Project's save churn (or the specification decides
   the file family is not compared — it currently is).
3. **Free vs total slack.** `mppwrite::write_task_row` writes TotalSlack to
   `+74` and `+190`; the reader maps `+74` to FreeSlack (W30). A first dump of
   our own output therefore shows `free_slack_ticks == total_slack_ticks`; a
   Project save need not. This is a writer/reader disagreement to settle with
   the oracle, not a dump bug.

### Dump schema (`engine-diff/1`)

`tests/engine_diff_tests.cpp --dump <file.mpp> <out.json>` writes it with the
`core/mpp` reader (the same code `tools/reader_dump` links) and `core/cal`.
Both halves call that one executable, so the two sides cannot drift. Every
value is a number, id, date string, boolean or a format path; **no name is
ever emitted** (text custom fields are omitted for that reason, and the
self-test asserts the fixture's task/resource names are absent). Dates are
`YYYY-MM-DD HH:MM:SS+00:00`, the stored wall-clock value with no timezone
conversion; `""` is "not stored".

```
{
  "schema": "engine-diff/1",
  "project": {uid, id, start, finish, duration_ticks, priority,
              percent_complete},
  "tasks": [ {uid, id, outline_number, outline_level, summary, milestone,
              milestone_stored, manually_scheduled, critical, duration_ticks,
              duration_format, start, finish, calculated_start,
              calculated_finish, priority, percent_complete,
              free_slack_ticks, start_slack_ticks, total_slack_ticks,
              start_anchor, calendar_uid, constraint_type, constraint_date,
              deadline, actual_start, actual_finish, actual_duration_ticks,
              child_count, custom: {"0x0b400057": {kind, present, measured,
              value|ticks}}} ],
  "links": [ {uid, predecessor_uid, successor_uid, type, lag_ticks, lag_units,
              predecessor_known, successor_known} ],
  "assignments": [ {uid, task_uid, resource_uid, units_x100, work_ticks,
                    start, finish, resume} ],
  "resources": [ {uid, id, type, type_stored, max_units_x100} ],
  "calendar_table": {decoded, default_week, metadata_anomalies,
                     recurring_exceptions, sub_minute_intervals,
                     calendars: [{uid, base_uid, resource_uid,
                     has_week_data, week, exceptions: [...],
                     work_weeks: [...]}]},
  "file": {stream_count, streams: [{path, size}]}
}
```

`week` is seven day objects `{use_base, intervals: [[start_ticks,
length_ticks], ...]}` (index 0 = Sunday). An exception carries
`start_day, end_day, working, intervals, recurrence_type, occurrences, period,
days_of_week, month, month_day, month_item, month_position,
raw_recurrence_words[7]`. Rows are sorted by uid and streams by path, so the
comparison is structural rather than row-order dependent. `compare: "all"`
means every leaf in this object is compared exactly.

**Not yet in the dump** (a reader gap, named rather than silently skipped):
task work/cost and early/late start/finish (COM-derived or not exposed by
`core/mpp`), resource rates/costs/calendar uid, project options (currency,
default task type, critical slack limit), and calendar/exception names (names
are deliberately excluded). These need reader/API work before the harness can
compare the corresponding rows of the table at the top of this document.

### Operation grammar (scenario JSON)

```json
{"name": "duration-change", "base": "synthetic-base",
 "operations": [
   {"op": "set_task_field", "uid": 1, "field": 188743709, "value": 14400},
   {"op": "recalculate"},
   {"op": "save"}],
 "compare": "all"}
```

| op | keys | meaning |
|---|---|---|
| `set_task_field` | `uid`, `field`, `value` | write one PjField; the supported ids and value units are in `tests/engine_diff_tests.cpp` |
| `add_link` | `predecessor_uid`, `successor_uid`, `type` (`FF`/`FS`/`SF`/`SS`), `lag_ticks` | one link; lag is 6-second ticks, a whole number of minutes |
| `delete_task` | `uid` | task delete (mppwrite cascades links/assignments) |
| `add_assignment` | `task_uid`, `resource_uid`, `units_percent` | one assignment; COM `Units` is the fraction, the JSON is percent |
| `set_calendar` | `task_uid`, `calendar_uid` | task `+178` calendar uid |
| `recalculate` | — | `CalculateProject` on the Project side; CPM + rollup + write on ours |
| `save` | — | terminal; the compared state. Must be last and appear once |

Field ids are the decimal PjField ids from `analysis/field_id_map.tsv`
(`188743709` = `0x0b40001d` Task.Duration, `188743697` ConstraintType,
`188743698` ConstraintDate, `188743715` Start, `188743712` PercentComplete,
`188743705` Priority) plus the PjTask custom Number/Duration/Date/Cost/Flag1
ranges from `core/mpp/field_value.h`. Text custom fields are refused so a
committed scenario cannot carry a name. `--check` compiles each scenario to a
TSV operation list for the C++ helper; that TSV is internal and documented in
`tests/engine_diff_tests.cpp`.

### Exact commands

Build and the two ctest lanes (no COM, no tolerance):

```bat
cmake -S . -B build\e1 -G "Visual Studio 17 2022" -A x64 -DCORE_BUILD_TESTS=ON
cmake --build build\e1 --config Release --target engine_diff_tests
ctest --test-dir build\e1 -C Release -R "engine_diff" --output-on-failure
```

The C++ self-test and the Python self-test, run directly:

```bat
build\e1\Release\engine_diff_tests.exe
python scripts/engine_diff.py --selftest --apply-bin build\e1\Release\engine_diff_tests.exe
```

Check and suite (CI-safe; a recorded oracle is the only input CI needs):

```bat
python scripts/engine_diff.py --check tests/scenarios/02-duration-change.json
python scripts/engine_diff.py --suite
```

Record one oracle (COORDINATOR ONLY, COM; this path was written but **not
exercised** by stream E1 — Project cannot be driven from a stream):

```bat
python scripts/engine_diff.py --record tests/scenarios/01-recalculate.json
```

To turn the whole corpus from unrecorded into scored, the coordinator runs, in
this order (each is one Project session; `oracle_lock` serialises them):

```bat
python scripts/engine_diff.py --record tests/scenarios/01-recalculate.json
python scripts/engine_diff.py --record tests/scenarios/02-duration-change.json
python scripts/engine_diff.py --record tests/scenarios/03-link-fs-lag.json
python scripts/engine_diff.py --record tests/scenarios/04-constraint-snet.json
python scripts/engine_diff.py --record tests/scenarios/05-constraint-mso.json
python scripts/engine_diff.py --record tests/scenarios/06-assignment-50pct.json
python scripts/engine_diff.py --record tests/scenarios/07-task-calendar.json
python scripts/engine_diff.py --record tests/scenarios/08-delete-task-with-successor.json
python scripts/engine_diff.py --suite
```

`--record` writes `tests/data/scenarios/<name>.oracle.json` (the dump exactly,
numbers only) and the Project-saved `.mpp` under `build/e1/engine-diff/oracle/`
(git-ignored). It prints the base fixture's full SHA-256 and refuses to run if
the fixture does not match the pinned hash, so the recording is tied to the
exact base it was made from.

### Fixture integrity guard

`tests/data/synthetic-base.mpp` is pinned in `scripts/engine_diff.py`
(`BASE_FILES["synthetic-base"]["sha256"]`) to
`55c109f2f62fd6b9f05789ab002c35991a424aab18c78f92e4200df7b3fc6069`,
237,568 B. This is a measurement prerequisite, not boilerplate: the fixture was
corrupted in place twice during wave 17 by a Dropbox sync trap (an earlier copy
hashed to `ec5c0443…642c` while reporting the right size and tasks), and an
oracle recorded against a changed base is not a comparison. The guard now runs
in both halves:

* every mode (`--record`, `--check`, `--suite`, `--selftest`) hashes the base
  before doing anything and **refuses on a mismatch**; `--record` says
  explicitly that the oracle would be invalid;
* `engine_diff_tests --apply` takes the expected SHA-256, hashes its input
  before and after the operation list, refuses a mismatch before writing, and
  refuses to write its output over its input;
* the Python half hashes the original fixture and the build copy before and
  after every scenario run, so a write that lands on the fixture is a loud
  failure;
* all generated `.mpp`, dump and `.ops` files are asserted to be under
  `build/`. The only `tests/data/` write in the harness is the oracle JSON that
  `--record` creates there by design, and `--record` is coordinator-only.

To add a fixture (for example the auto-scheduled base the coordinator will build
for the first scoring run), add its relative path and SHA-256 to `BASE_FILES`,
add its name to a scenario's `"base"`, and keep the scenario corpus ids/numbers
only. The README's provenance paragraph says the fixture has "No resources";
the committed file actually contains one Work resource (`Crew`) and three
placeholder assignment rows — `06-assignment-50pct.json` relies on that
resource. The coordinator should fix the README wording; the harness records
the base hash on every recording so this can never pass unnoticed.


## First recording (2026-10-23) — 7 of 8, 0 pass

The coordinator ran `--record` for all eight scenarios against MS Project on this
machine. Two harness bugs surfaced and are fixed:

1. **The recorder opened the committed fixture in place.** MS Project rewrites a
   file it opens, and the fixture hash changed mid-run; the harness refused the
   measurement on its own hash guard. Recording now copies the base into
   `build/` first and only ever opens that copy. `check_fixtures.py` verified
   `55C109F2…6069` after every run.
2. **`Project.Calendars` does not exist in this build** (and neither does
   `Project.BaseCalendars`), so `07-task-calendar` cannot be recorded through a
   uid-to-calendar lookup. `Task.Calendar` is a *name* get/put. That scenario
   needs a fixture that already carries a second named base calendar (or a
   scenario op that creates one), which is a coordinator fixture job.

**Score: 0 passed, 7 recorded, 1 unrecorded.** The failures group into three
causes, not seven:

| group | example | kind |
|---|---|---|
| manual scheduling | `task 1 start: 2026-01-05 08:00 vs 2026-01-04 13:00`; project finish `01-09 vs 01-16` | family gap: Project does not reschedule manual tasks, our planner applies CPM to every row |
| slack word | `task 1 free/total_slack: 0 vs 24000`; `task 3: 14400 vs 0` | probable bug: free-vs-total slack, the same `+74` disagreement W60 noted |
| start anchor | `task 3 start_anchor: 2026-01-04 13:00 vs 2026-01-14 13:00` | manual pin + calendar boundary (time-of-day anchor) |

M2 remains 332/332 against the auto-scheduled corpus file, so the CPM path is
sound; this fixture's manual tasks are what expose the family gap. The next
recording should use an **auto-scheduled** base so the score measures the engine
rather than manual-mode semantics.
