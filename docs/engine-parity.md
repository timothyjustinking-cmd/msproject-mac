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

The wave-17 table below is superseded by the wave-18 measurement at the end of
this document; the score has not moved (0 passed, 7 recorded, 1 unrecorded), but
the causes are now known precisely and the planner side is closed.

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

## Wave 18 (E2): manual scheduling implemented — 5 of 7 scenarios down to `file.*`

Stream E2 closed the three causes above **in the planner** and then, under
coordinator exceptions, in the scenario applier (`tests/engine_diff_tests.cpp`)
and the writer (`core/mppwrite`). Full evidence, code map and verification
commands: `docs/handoff/e2.md`.

### Score (re-measured 2026-10-23, `build\e2`, Release, sizes still compared)

| scenario | before | after | what still differs |
|---|---|---|---|
| recalculate | FAIL 15/240 | FAIL 8/240 | `file.*` only |
| duration-change | FAIL 19/240 | FAIL 8/240 | `file.*` only |
| link-fs-lag | FAIL 25/247 | FAIL 10/247 | `file.*` only |
| constraint-snet | FAIL 24/240 | FAIL 8/240 | `file.*` only |
| constraint-mso | FAIL 25/240 | FAIL 9/240 | `file.*` + `task 3 duration_ticks` 9600 vs 12000 |
| assignment-50pct | FAIL 22/247 | FAIL 15/247 | `file.*` + the null-resource placeholder assignment Project tombstoned |
| task-calendar | UNRECORDED | UNRECORDED | needs the coordinator's fixture |
| delete-task-with-successor | FAIL 25/204 | FAIL 9/204 | `file.*` only |

**Suite: 0 passed, 7 recorded (7 fail), 1 unrecorded** — because the `file.*`
sizes are still compared. Five scenarios have *no* non-file difference left;
the coordinator's planned change from sizes to a change-set comparison is the
gate for them (note: our save of `recalculate` now changes no bytes at all,
while Project's save churns eight streams, so a change-set comparison still
needs a writer stream or a spec decision for that family). **M2 332/332**
(`recalc --compare`), cpm 1321, rollup 155, attribution 54, plan 206, writer
760/446/304/101.

### What was wrong, and is now fixed

1. **Manual scheduling** (the family gap). Project keeps a manual row's dates,
   ignores its own constraint for them, stores a second (calculated) date pair at
   +96/+100, reports one slack number (`+74 = +186 = +190`), never calls the row
   critical, and still drives its successors from the kept dates. Implemented as
   two CPM passes (`core/sched` `CpmTask::manual_start/manual_finish`,
   `link_anchor_*`; `core/plan` `manual_finish_of()` and the two-pass result),
   with the measured boundary rule that makes a Sunday 13:00 start finish at
   12:00 rather than 17:00.
2. **Manual successors move when a link is ADDED**, not on recalculation: the
   applier now recalcs-and-persists after structural edits, with
   `PlanInput::manual_rows_follow_links` for the link add. This is what makes
   `link-fs-lag` uid 2 (`01-12 13:00`) and `delete` uid 3 (`01-14 13:00`) match,
   and the calculated pair for a manual successor is now the bound of the
   predecessor's *kept* finish (`link-fs-lag` calculated `01-12 13:00`).
3. **Free vs total slack was a WRITER defect.** The reader's mapping is right
   (+74 FreeSlack, +186 StartSlack, +190 TotalSlack; milestone1.mpp ID 6 =
   0/220800/220800), but `TaskValues` had no free-slack field and
   `write_task_row` put TotalSlack at +74, so a saved file could never hold
   free < total. Fixed, with a writer regression that stores and reads back
   free 0 / start 4800 / total 4800 at the three offsets.
4. **Applier mapping.** It wrote one plan date pair into all six stored date
   words, which is why a manual scenario could not be saved either way. It now
   maps the calculated pair to +96/+100, the kept pair to +104/+108 and the
   start anchor to +174, writes the project row's duration
   (`work_time_between(project start, finish)`; measured
   21600/12000/40800/21600/21600/21600/45600), writes DurationFormat with a
   Duration edit (COM was given minutes -> 3), passes the minutes lag format
   (LagFormat 3), and maintains the assignment rows' work/dates from their tasks.
5. **Start anchor**: +174 is written from the plan for manual rows (anchor =
   kept start on every measured row), including the link-moved ones.

### Still open (each with its cause)

1. `file.*`: Project's save churn (8–10 streams per scenario), writer-owned;
   see the note above.
2. `constraint-mso` `task 3 duration_ticks` 9600 vs 12000: Project rewrote the
   stored Duration when MSO was set on the manual row while its calculated span
   stayed 2 d — an edit-time recompute, not a scheduling rule.
3. `assignment-50pct`: Project tombstones the null-resource placeholder of a
   task when an assignment is added (measured, `core/mppwrite/add_delete.h`); our
   `add_assignment_row` leaves it, so the placeholder row is still there.
4. `task-calendar` unrecorded (no `Project.Calendars` in this build).
5. Manual rows with actuals, an elapsed duration, a task calendar or a deadline
   have no oracle row; the bridge falls back to the CPM path and counts them in
   `PlanResult::manual_unpinned`.

## Wave 18 (E2): manual scheduling implemented — 5 of 7 scenarios field-exact

`file.*` (and `file stream ...`) are **excluded from scoring**: Project's save
churn is not a parity signal, and our recalculate-only save now changes no bytes
at all because the fixture already holds the correct state. The writer's own
suite proves the "only intended bytes move" invariant.

### Final score (re-measured 2026-10-23 against clean re-recorded oracles)

| scenario | result | what differs |
|---|---|---|
| recalculate | **PASS** (240 fields) | - |
| duration-change | **PASS** (240) | - |
| link-fs-lag | **PASS** (247) | - |
| constraint-snet | **PASS** (240) | - |
| delete-task-with-successor | **PASS** (204) | - |
| constraint-mso | **FAIL** (1/240) | `task 3 duration_ticks` 9600 vs 12000 |
| assignment-50pct | **FAIL** (7/247) | our assignment row vs `<missing>`: the placeholder tombstone matches the *previous* recording, not the clean one |
| task-calendar | UNRECORDED | needs a fixture with a named base calendar (`Project.Calendars` does not exist here) |

M2 **332/332** (268/268 leaves, 64/64 summaries) before and after; cpm 1321,
rollup 155, attribution 54, plan 206, writer 760/446/304/101.

### What was fixed

1. **Manual scheduling** — Project keeps a manual row's dates, ignores its own
   constraint for them, stores a second (calculated) date pair at +96/+100,
   reports one slack number, never marks it critical, and still drives its
   successors from the kept dates. Implemented as two CPM passes plus the
   measured boundary rule (Sunday 13:00 + 5 d = Friday 12:00, not 17:00).
2. **Free vs total slack was a WRITER defect** — the reader's mapping is right
   (+74 FreeSlack, +186 StartSlack, +190 TotalSlack), but `TaskValues` had no
   free-slack field and `write_task_row` put TotalSlack at +74, so a file could
   never hold free < total. Fixed, with a writer regression at the three offsets.
3. **Applier mapping** — the calculated pair to +96/+100, the kept pair to
   +104/+108, the anchor to +174; project-row duration, DurationFormat, minutes
   LagFormat, and assignment work/dates are written; a link ADD moves a manual
   successor (Project recalcs at add time).

### Still open, each with its cause

* **constraint-mso**: Project stored 12000 where both of its own date pairs span
  9600. The coordinator applied MSO twice to all three manual rows and Project
  left durations **unchanged** (24000/14400/9600), so 12000 is not a scheduling
  rule — the fresh oracle needs re-examination before anything is implemented.
  A formula fitted to one row would be invented behaviour.
* **assignment-50pct**: the placeholder tombstone matched the earlier recording;
  with clean oracles the row identity differs and needs a real fix.
* `task-calendar` stays unrecorded until the named-calendar fixture exists.

## Wave 19: clean re-record, 7 of 8 — and what the two open fields really are

### Score (2026-10-23, `build\vs-w19`, Release)

| scenario | result | what differs |
|---|---|---|
| recalculate | **PASS** (240 fields) | - |
| duration-change | **PASS** (240) | - |
| link-fs-lag | **PASS** (247) | - |
| constraint-snet | **PASS** (240) | - |
| constraint-mso | **FAIL** (1/240) | `task 3 duration_ticks` 9600 vs 12000 |
| assignment-50pct | **PASS** (240) | - (was 7 fields) |
| task-calendar | **PASS** (246) | recorded for the first time |
| delete-task-with-successor | **PASS** (204) | - |

`python scripts/engine_diff.py --suite --apply-bin build\vs-w19\Release\engine_diff_tests.exe`
prints **8 recorded (7 pass, 1 fail), 0 unrecorded** (wave 18: 5 pass, 1 fail, 1
unrecorded).  M2 stays 332/332.

### The clean re-record proved the old oracles were faithful

All eight scenarios were re-recorded from clean bases (the recorder copies the
base into `build/` first).  Diffing the new JSONs against the previous ones field
by field: **0 semantic fields changed in seven scenarios, 1 in `link-fs-lag`
(one `file.streams` size, a family that is excluded from scoring)**.  So
`constraint-mso`'s stored 12000 is reproducible Project output, not a recording
artefact, and no oracle needed hand-editing.

### `constraint-mso`: measured, reproducible, and NOT a scheduling rule

The stored `+84` duration disagrees with **both** of Project's own date pairs
(9600) while Project's COM surface reports `Task.Duration = 960` minutes
throughout -- before the constraint, after `ConstraintType=2`, after
`ConstraintDate`, after `CalculateProject`, and after `FileSaveAs`.  A probe
without the harness reproduces the same file word.  What changes it is the
constraint application on a **manual** row whose start is **off-calendar**.

Nine measurements (`synthetic-base`, task 3 = 2 d unless noted; MSO 2026-01-20
08:00 unless noted), kept start -> stored `+84`:

| kept start | stored duration | ticks | increment |
|---|---|---|---|
| Sun 13:00 (base row, 2 d) | 12000 | 12000 | +2400 (4 h) |
| Sun 13:00, 1 d | 7200 | 7200 | +2400 |
| Sun 08:00, 1 d | 9600 | 9600 | +4800 (8 h) |
| Sun 10:00, 1 d | 8400 | 8400 | +3600 (6 h) |
| Sun 15:00, 1 d | 6000 | 6000 | +1200 (2 h) |
| Sat 09:00, 1 d | 9000 | 9000 | +4200 (7 h) |
| Fri 13:00, 1 d (working day) | 4800 | 4800 | 0 |
| task 2 (3 d), Sun 13:00 | 16800 | 16800 | +2400 |
| task 1 (5 d), Sun 13:00 | 26400 | 26400 | +2400 |
| no constraint (control) | 9600 | 9600 | 0 |

The increment equals the **remaining working windows of the start day** when
that day is not a working day for the calendar (Sun 08:00 -> 08:00-12:00 +
13:00-17:00 = 8 h; Sun 10:00 -> 6 h; Sun 15:00 -> 2 h; Sat 09:00 -> 7 h; Fri
13:00, a working day -> 0).  It is independent of the task, its duration and the
distance to the MSO date, and it appears only when a constraint is applied.

Conclusion for the engine: this is a **Project save-time recomputation**, not
CPM.  Our writer stores the plan's duration (9600) and is one field away.  The
only writer that could carry the rule into a file is `mppwrite::save_plan`, and
it has **no production caller**: no app/, app_qt/ or tools/ code calls it, so it
is test-only until the app's save path is wired to it (wave-23 A-4).  The
scenario is deliberately left failing this wave: the rule above is measured on nine rows,
but its cause is not understood well enough to be sure it generalises to every
manual/constraint/calendar combination, and the brief's rule is "do not fit a
rule to one row".  The next wave can implement it in the applier with this table
as the regression, or decide the dump should compare a duration Project itself
does not report through COM.

### `assignment-50pct` was a writer bug, not an oracle artefact

A Project-written **auto** MPP14 row stores no Fixed2Data block at all:
`+46/+50 = 0xFFFFFFFF` and `+58 = 0` on all 85 PROJECT_A task rows, while `+84`
holds the duration; a COM duration edit saves it back as 0.  Manual rows (every
other fixture) store real copies in all three.  `resolve_task_values()`
materialised copies from `+84/+104/+108` and then refused its own write as a
"single-copy split" -- so a duration or date edit on a real auto file could not
be saved.  Absence is now preserved (and the false refusal/warning skipped);
rows with a coherent Fixed2Data block are unchanged.  That fix is what turns
`assignment-50pct` green, because its placeholder row had the same absent-copy
shape.

### `task-calendar` is now recorded and passing

`tests/data/synthetic-calendars.mpp` (new, committed, SHA-256 pinned in
`engine_diff.py` and `scripts/check_fixtures.py`) is `synthetic-base` with its
three tasks converted to automatic scheduling plus a second **named** base
calendar, uid 4 "Shift A" (Mon-Fri 09:00-13:00 + 14:00-18:00), built by
`scripts/build_calendar_fixture.py`.

Measured COM surface (this build): `Application.BaseCalendarCreate`,
`BaseCalendarEditDays`, `BaseCalendarRename`, `Application.BaseCalendars` and
`Project.BaseCalendars` all exist; `Project.Calendars` does not; and
`Project.BaseCalendars` has **no UniqueID** property, so the uid -> name map is
pinned with the fixture and used only by the COM half (`--record`).  The
scenario JSON stays ids-only.  `--check` maps uid 4 -> task `+178`, builds the
calendar through `core/cal`, and the whole state dump matches Project exactly.

## Wave 20: schema 2 -- the wider dump found the late pair, E5 modelled it

The dump also compares `tasks[].late_start/late_finish` (`+112/+116`, MPP12
`+12/+152`) and `resources[].calendar_uid` (from `TBkndCal`).  All eight oracles
were re-recorded against schema 2, so the first schema-2 run read **4 pass /
4 fail** -- a measurement increase, not a new defect: the dump had not compared
the late pair before.

### Score: 7 pass / 1 fail (E5, 2026-10-23)

| scenario | result | what differs |
|---|---|---|
| recalculate | **PASS** (247) | - |
| duration-change | **PASS** (247) | - |
| link-fs-lag | **PASS** (254) | - |
| constraint-snet | **PASS** (247) | - |
| constraint-mso | FAIL (1/247) | `task 3 duration_ticks` 9600 vs 12000 |
| assignment-50pct | **PASS** (247) | - |
| task-calendar | **PASS** (253) | - |
| delete-task-with-successor | **PASS** (209) | - |

### The cause and the rule

For a **manual** row, Project's stored `+112/+116` is not the CPM late pair.  E2
measured it as the inverse of the manual boundary arithmetic; E5 established the
generalisable rule over every Project-written manual row in the tree:

    late finish = min(project finish,
                      min over outgoing FS links of successor KEPT start - lag,
                      min over outgoing FF links of successor KEPT finish - lag)
    late start  = max(kept start, plain calendar walk back from late finish)

The FS/FF subtraction and the duration walk are `Calendar::sub_work` (the plain
backward walk, **not** `align_back`); SS/SF outgoing links do not bound the late
finish; the successor bound reads the successor's **kept** (early) date, not its
late date.  The slack words do not change -- they remain the effective pass's
`TotalSlack = LF - EF` and `StartSlack = TotalSlack`, because the stored pair's
difference from the CPM pair is normally the zero-working-time lunch break.

It lives in `core/plan/plan.cpp` as a second pass after the results are built,
gated on `PlanTaskResult::manually_pinned`; auto rows and `core/sched/cpm.cpp`
are untouched.  The applier writes the plan's pair for a pinned manual row.  The
evidence table (19 measured rows: the eight oracles, `w12-linkadd`,
`manual-start-edited`, PROJECT_B) and the inference boundaries (FF inferred,
elapsed lag falls back and is counted in `PlanResult::manual_late_unmodelled`,
task calendar from the row) are in `docs/handoff/e5.md`.

The four cases disagreed in both directions under the old "keep the stored pair
when the dates do not move" rule (ours later by 2 days in duration-change,
earlier by 7 days in delete), which is why a shift fitted to one scenario was
rejected.

### constraint-mso: one field, still open

`task 3 duration_ticks` is 9600 here and 12000 in Project's stored file.  This is
Project's save-time recomputation, not CPM: nine measurements show the stored
duration gains the remaining work windows of an off-calendar start day (a
weekday start gains 0).  The applier does not model it.  Decision: leave it
documented rather than fit it; it is the only engine-scenario mismatch left.
