# A4 -- Engine differential score audit (wave 22)

Read-only audit of the claim "engine differential suite 7/8, constraint-mso
deliberately not scored; MSO save-time rule implemented only behind a
self-test opt-in, not in production".  Repo HEAD e252041.  Nothing was built
or run; evidence is file:line from the tree plus read-only git log/diff.

## Verdict: PARTLY

The number 7/8 is honest and the opt-in is real: `--suite` counts
constraint-mso as a recorded FAIL in the denominator and exits 1, the bump
function is unreachable from any scored scenario, and no production source
contains the rule.  Three wordings overclaim or mislead, in descending order
of weight:

1. "the scored scenarios compare the production save path" /
   "production (`mppwrite::save_plan`) still writes 9600" -- `save_plan` has
   NO caller in `app/`, `app_qt/` or `tools/`; its only callers are the two
   test files.  The product saves through `save_field_values`,
   `save_date_edits`, `pin_task_start`, `add_task_row` etc.  So "production
   writes 9600 for this edit" is a statement about a library function the
   product does not call, not a measured product behaviour.  The suite scores
   a test-side applier that builds `PlannedRow`s itself.
2. "deliberately not scored" (coordinator.md:77, :143; wave21-complete.md:83)
   -- the scenario IS scored, as a FAIL, and `--suite` returns exit 1.  No
   exclusion list exists.  The honest phrase is "deliberately left failing".
   A reader of "not scored" would expect `--suite` to be green; it is red.
3. "265 checks, 0 failures" is a harness self-test, and the MSO part of it
   is a regression pin of the rule against the same nine numbers the rule
   was fitted to (transcribed constants, no committed artefact for eight of
   the nine, no held-out row).  It does not validate the rule; the Python
   half says so itself (engine_diff.py:1263 "this does NOT claim parity").

Secondary: `--suite` is not in any ctest/CI lane (only `--selftest` is), so
7/8 is a hand-run number; the oracle JSONs carry no recorded-at, Project
version or saved-file hash; and the dump on both sides is produced by the same
reader (`core/mpp`), so a symmetric reader misread would compare equal.

---

## Q1. How does `--suite` score constraint-mso?

Counted as a recorded FAIL in the denominator; never skipped or excluded.

`scripts/engine_diff.py:984-1033` (`cmd_suite`):

```
    for path in scenarios:
        scenario = load_scenario(path)
        status, differences, field_count, message, oracle_path = check_scenario(
            scenario, apply_bin, workdir, None)
        if status == "pass":
            recorded += 1
            rows.append(f"{scenario['name']}: PASS ({field_count} fields)")
        elif status == "fail":
            recorded += 1
            failed += 1
            rows.append(f"{scenario['name']}: FAIL "
                        f"({len(differences)}/{field_count} fields differ)")
    ...
    print(f"suite: {len(scenarios)} scenario(s), {recorded} recorded "
          f"({recorded - failed} pass, {failed} fail), {unrecorded} unrecorded")
    ...
    if failed:
        return 1
```

There is no per-scenario skip, allow-list, or "known failure" table anywhere
in `engine_diff.py` (grep for skip/exclud/not_scored returns only the
`file.*` comment at :613-616).  `check_scenario` (:648-664) returns `"fail"`
whenever `compare_dumps` yields any difference.  The e6.md:243-245 tail
confirms the observed output: `suite: 8 scenario(s), 8 recorded (7 pass, 1
fail), 0 unrecorded   (exit 1)`.

So "7/8" = 7 pass / 8 recorded with the eighth a FAIL.  Honest as a number.
The phrase "not scored" (coordinator.md:76-78 "measured and pinned but
deliberately not scored"; :143 "stays measured/pinned, not scored";
wave21-complete.md:83 "measured, not scored") is wrong about the mechanism:
nothing un-scores it.  What was deliberately NOT done is applying the rule so
that it passes.

Also: `--suite` is not a ctest lane.  `CMakeLists.txt:209` registers
`engine_diff_apply` (the C++ self-test, no args) and `:215-219`
`engine_diff_harness` runs `--selftest` only.  No `.github/workflows/*.yml`
mentions `engine_diff`.  The 7/8 is therefore a coordinator-run figure, not a
gate.

## Q2. What does the comparison ignore or tolerate?

Ignored (excluded from scoring), `scripts/engine_diff.py:606-618`:

```
    ignored_prefixes = ("file.", "file stream")
    paths = [path for path in sorted(set(ours_fields) | set(oracle_fields))
             if not path.startswith(ignored_prefixes)]
```

`flatten()` (:552-597) emits the file family as `file stream_count` and
`file stream <path>` (= size), so both are dropped.  The `"file."` prefix
matches no flattened key (harmless).  Everything else is compared exactly and
type-strictly (:619 `if type(left) is not type(right) or left != right`).
No numeric or date tolerance exists ("NO tolerance", :8; "no tolerance
anywhere", :553).

History of the ignore: engine-parity.md:149-154 originally said stream sizes
"are compared exactly, by design"; wave 18 (:440-443) excluded them because
"Project's save churn is not a parity signal".  Wave 19 (:511-513) notes one
`file.streams` size changed in link-fs-lag on re-record, "a family that is
excluded from scoring".  Reasonable: stream sizes are container layout, not
schedule state.

Can a real scheduling regression hide?  Not in `file.*` (sizes only).  But it
can hide in what is NOT in the dump at all, which the exact-compare rhetoric
does not cover.  engine-parity.md:208-213 lists the reader gaps: "task
work/cost ... resource rates/costs/calendar uid, project options (currency,
default task type, critical slack limit)".  Schema 2 added the late pair and
the resource calendar uid; task Work and Cost, task Type / effort-driven,
levelling fields and project options remain absent, so a regression in work
or cost roll-up, task-type arithmetic or levelling is invisible to the suite.
Assignment `work_ticks`/`units` are present (engine_diff_tests.cpp:2355-2361).

Coverage caveat (scope, not a hidden tolerance): all eight scenarios run on a
3-task synthetic fixture; seven use `synthetic-base` whose rows are
manually scheduled (every oracle header: `"manually_scheduled": true`), only
`task-calendar` uses the auto-mode `synthetic-calendars` fixture.  Both
"constraint" scenarios therefore measure that Project does NOT move a manual
row for its constraint (e5.md:55-56 "own constraint ignored"; snet oracle uid
2: start 01-04 13:00, finish 01-07 12:00, calculated pair 02-02/02-04).  CPM
coverage of the engine comes from M2 332/332 on `corpus/project_a.mpp`, not
from this suite.

## Q3. The "265 checks, 0 failures" self-test

What it tests (`tests/engine_diff_tests.cpp`, `run_self_test` :2206-2560):
SHA-256 known-answer vectors (:2224-2235), fixture presence and hash guard
refusals (:2240-2260), seven built-in applier cases (:2277-2290:
recalculate, duration, link, constraint-snet, assignment, calendar, delete)
each applied to a copy of `synthetic-base`, read back with `core/mpp` and
asserted against hard-coded expected values (e.g. :2402-2407 SNET uid 2
duration 14400; `check_late_pair` calls with literal dates), `dump_check` on
each output (:2047-2092: schema line, families present, no names, streams
present), `check_mso_duration_table` (:2102-2204), a dump of the fixture
itself pinning late-pair literals (:2497-2530), and the fixture is
byte-identical afterwards (:2531-2534).  It is a harness/applier regression
suite; it never reads an oracle JSON.  `TestContext::report` (:1988-1990)
prints "N checks, M failures".

Tautology check for the MSO word:

(a) Nine-row table (:2122-2149): expected `increment` and `stored_total` are
literal constants (`{2026, 1, 4, 13, 0, 2400, 9600, 12000, "Sun 13:00, 2 d"}`
etc.) transcribed from docs/engine-parity.md:526-539.  The check is
`context.equal(bump, row.increment)` and
`context.equal(plan_duration + bump, stored_total)` (:2144-2148).  Since
`increment == stored_total - plan_duration` by construction of the table,
these two checks per row are one check counted twice (18 checks for 9
facts).

(b) Five end-to-end cases (:2150-2203): `expected` is `12000`, `7200`,
`16800`, `26400`, `9600`, again literals; the applier is run with
`applier.model_project_save(true)` (:2192) and `+84` is read back via
`mpp::load_file` (:2194-2201).

So the expected 12000 is NOT derived by the rule under test inside the test;
it is a constant.  Is it a recorded Project value?  For the first row, yes:
`tests/data/scenarios/constraint-mso.oracle.json` task uid 3
`duration_ticks: 12000` (committed at 4299ff8, wave 17; unchanged through
the wave-19 and wave-20 re-records -- see Q4).  For the other eight rows
(7200, 9600, 8400, 6000, 9000, 4800, 16800, 26400) there is NO committed
artefact: no oracle JSON, no `.mpp`, no probe script, no log.  They exist
only as the markdown table (engine-parity.md:526-539, e6.md:140-151) and
the C++ constants.  grep for "Sun 10:00" / "Sat 09:00" / "2026-01-04 10:00"
across scripts/, docs/, tests/ hits only those three files.

Therefore: the test is not tautological in form (constants, not the rule's
own output), but it is a self-fit pin -- the rule was fitted to these nine
numbers (engine-parity.md:540-547 derives the "remaining windows" reading
from them) and is tested against the same nine numbers.  There is no
held-out measurement.  The inferred part of the rule -- the "standard day
windows" template taken from "the first non-empty day pattern in the
calendar's default week" (:1172-1178, :1192-1198) -- cannot be falsified on
this fixture because all five weekdays share the same shape (e6.md:299-305
admits this).  Four of the nine rows (Sun 08:00/10:00/15:00, Sat 09:00) are
asserted at the pure-function level only because "the manual Start write
path is unsupported" (:1163-1165, e6.md:318-320).

Note also that the Python harness self-test (`cmd_selftest`, :1097-1266)
records an oracle from OUR output and checks it against OUR output
(:1184-1196 "a self-recorded oracle passes --check") plus a tamper check;
it is explicitly labelled (:1263-1264) "this does NOT claim parity (the
recorded oracle does that)".  Correct and honest, but a reader who sees
"265 + 53 checks, 0 failures" next to "7/8" should know none of those 318
checks compare against Project.

## Q4. Are the oracle JSONs recorded by Project through COM, or hand-edited?

Recorded through COM, by the script, as far as the repo can show; no sign of
post-recording edits; but provenance metadata is absent.

Recording path, `scripts/engine_diff.py:896-947` (`cmd_record`): copies the
base to `build/` (:919-920), `oracle_session()` opens Project via COM
(:921-923 `app.FileOpen`), applies each op with `_apply_com_operation`
(:92-131: `task.Duration = f"{int(value)//10}m"`, `task.ConstraintDate =
utc(...)`, `app.FileSaveAs(Name=str(target))`), `app.FileClose(0)`, then
`dump_state(apply_bin, saved, ...)` (:938-939) runs OUR reader
(`engine_diff_tests --dump`) over Project's saved file and writes the JSON
(:946-947).  An independent `scripts/mpp_rows.py` byte-level dump is written
to `build/` as evidence (:874-893) -- not committed.

Metadata inside the oracle: none.  Top-level keys are exactly
`schema, project, tasks, links, assignments, resources, calendar_table,
file` (inspected with python/json).  No recorded-at timestamp, no Project
version, no SHA-256 of the saved `.oracle.mpp`, no machine id.  The saved
`.oracle.mpp`, `.oracle.raw.json` and `mpp_rows.txt` live under git-ignored
`build/` (:86 "git-ignored").  So "recorded by Project" rests on commit
messages and handoff prose, not on anything verifiable in the JSON.

Git history (read-only `git log --name-only -- tests/data/scenarios/`):

- 4299ff8 2026-10-23 10:17 +1100 "wave 17 ... first recording: 7 oracles"
  -- creates constraint-mso.oracle.json with `duration_ticks: 12000` for
  uid 3 (verified via `git show 4299ff8:...`), schema engine-diff/1.
- d6724da 12:44 "wave 19: ... clean re-record of all eight oracles" --
  touches only `link-fs-lag.oracle.json` (1 line) and adds
  `task-calendar.oracle.json`; constraint-mso byte-identical, consistent with
  engine-parity.md:509-513 ("0 semantic fields changed in seven").
- e91f19b 13:59 "wave 20: schema-2 oracles re-recorded" -- diff on
  constraint-mso is exactly the schema line plus `late_start`/`late_finish`
  additions (git diff 4299ff8 e91f19b, grep: only `schema` and `late_*`
  lines).  No other field moved.
- Working copy is clean against HEAD for `tests/data/scenarios/`,
  `scripts/engine_diff.py`, `tests/engine_diff_tests.cpp`.

All three commits by the same author (the operator).  Diffs are shaped exactly
as a re-record with a widened schema would produce, not as a hand edit.
Caveat: a plain JSON file with no hash or signature cannot prove the
negative; and because both sides of the comparison are produced by the same
`core/mpp` reader, a reader misread (e.g. of `+84`) would be baked into the
oracle and ours identically and compare equal.  The `mpp_rows.py` check that
would catch that is not committed and runs only on the coordinator machine.

## Q5. Is "constraint-mso is NOT a scheduling rule" sound?

The argument in five lines (engine-parity.md:516-552, e6.md:103-163):

1. Project's stored `+84` = 12000 disagrees with BOTH of Project's own date
   pairs for the row (kept 01-04 13:00 -> 01-06 12:00 and calculated
   01-20 08:00 -> 01-21 17:00 are each 2 d = 9600).
2. COM `Task.Duration` reports 960 min (= 9600) before the constraint, after
   `ConstraintType=2`, after `ConstraintDate`, after `CalculateProject` and
   after `FileSaveAs` -- so the object model never sees 12000.
3. Across nine measurements the increment equals the remaining standard
   working windows of the start DAY when that day is non-working, is
   independent of task, duration and the MSO date, and is 0 on a weekday.
4. SNET on the same off-calendar Sunday start does not bump (oracle 14400),
   so the trigger is MSO specifically.
5. Hence a save-time rewrite of the duration word, not CPM; the dates, late
   pair and slack are the plan's.

Soundness: (1) and (4) are reproducible from the committed oracles -- I
confirmed uid 3 in constraint-mso: start/finish/calculated pair as above,
`constraint_type 2`, `duration_ticks 12000`; and uid 2 in constraint-snet:
`duration_ticks 14400`, `constraint_type 4`, same 01-04 13:00 start.
(2), (3) and the "probe without the harness reproduces it" sentence
(engine-parity.md:521-522) are NOT reproducible from the repo: they need a
COM session with MS Project, and no script in `scripts/` performs the
off-calendar start sweep (`sched_oracle_recipes.py:112-121` and
`oracle_w3.py:580-598` probe constraint types 0..7 on a different fixture
and date; nothing moves a manual start to Sun 08:00/10:00/15:00 or Sat
09:00).  The nine rows are prose.

The logical gap: "not a scheduling rule" is argued from the dates not moving
and COM reporting 9600 in the SAME session.  What is not measured is what
Project shows after REOPENING the saved file.  If a reopened file displays
2.5 d from `+84`, then the word has user-visible semantics and the product
must emulate it to be faithful -- "not CPM" would still be true, "not a
scheduling rule" would be a label, not a finding.  engine-parity.md:549-551
and e6.md:304-305 concede "its cause is not understood".

Reproducible from the repo today (needs a Release build of
`engine_diff_tests`, which this audit did not run):

```
python scripts/engine_diff.py --suite --apply-bin build\<dir>\Release\engine_diff_tests.exe
python scripts/engine_diff.py --check tests/scenarios/05-constraint-mso.json --apply-bin ...
build\<dir>\Release\engine_diff_tests.exe            # the 265-check self-test
python -I -c "import json; d=json.load(open('tests/data/scenarios/constraint-mso.oracle.json')); print([t['duration_ticks'] for t in d['tasks']])"
```

Not reproducible without MS Project + COM on the coordinator machine:
`--record`, the nine-row sweep, the COM `Task.Duration` readings.

## Q6. Where is the MSO rule applied: production vs self-test only?

Only in `tests/engine_diff_tests.cpp`.  Zero hits for
`model_project_save`, `off_calendar_start_bump`, `duration word gains` or
`constraint_type == 2` in `core/`, `app/`, `app_qt/`, `tools/`.

The opt-in flag (:1253-1257):

```
    /* Opt-in: model Project's save-time duration word (the measured MSO rule)
     * in the file this applier writes.  OFF for every scenario the differential
     * suite scores -- that file must be what the production save path writes.
     * The self-test sets it to exercise the rule end to end. */
    void model_project_save(bool enabled) { model_project_save_ = enabled; }
```

Default `bool model_project_save_ = false;` (:1965).  The `save` op calls
`recalculate(out_path_, model_project_save_)` (:1310); the only
`model_project_save(true)` in the tree is :2192 inside
`check_mso_duration_table`.  The application point (:1899-1917):

```
            if (project_save && !planned.summary && planned.manually_pinned &&
                stored.constraint_type == kConstraintMso &&
                mppwrite::is_date(stored.constraint_date)) {
                core::sched::ticks const bump = off_calendar_start_bump(
                    calendar_for(input, planned.calendar_uid), kept_start);
                if (bump > 0) {
                    duration_word += static_cast<std::uint32_t>(bump);
```

The scored path (`engine_diff.py --check/--suite` -> `engine_diff_tests
--apply`) constructs `Applier` at `main` :2548 and never calls the setter, so
the bump cannot reach a scored file.  That part of the claim holds.

What "production save path" means here, however, is thinner than the prose:
the test applier builds `mppwrite::PlannedRow`s itself (:1798-1941: it sets
`row.values.duration`, `late_start`, slack etc. from the plan) and then calls
`mppwrite::save_plan(doc_, rows, out_path)` (:1942).  `save_plan`
(`core/mppwrite/save.cpp:551`) is called from exactly two places in the
tree: `tests/engine_diff_tests.cpp:1942` and
`tests/roundtrip_save_flow.cpp:507`.  No file in `app/`, `app_qt/` or
`tools/` calls it or builds a `PlannedRow`; the product's `mppwrite` calls
are `save_date_edits`, `save_field_values`, `pin_task_start`,
`add_task_row`, `delete_task_row`, `add_task_link`, `delete_task_link`,
`clear_field_value`, `check_table`, `read_var_entries`.  So e6.md:177-178
("production save path writes the plan duration (9600) for this edit") and
:325-327 ("a Qt/`app` save of the same edit write 9600") describe a code
path the Qt app does not take.  Whether the Qt app writes 9600, 12000 or
leaves `+84` untouched after set-MSO + recalc + save is not something the
repo demonstrates.

## Q7. What would make the suite 8/8 honestly, and does the wording overclaim?

To be 8/8 honestly:

1. Move the rule out of the test into the library the product actually uses
   -- either `core/plan` (as `PlanTaskResult` output, the way E5 placed the
   manual late pair in `core/plan/plan.cpp`) or `mppwrite::save_plan` /
   the edit-save functions -- gated on the measured trigger (manual pinned,
   `constraint_type == 2`, dated, off-calendar start), with the scored
   applier picking it up by construction rather than by a flag.
2. Before that, get at least one HELD-OUT Project measurement the rule was
   not fitted to: a fixture whose weekdays differ in shape (to test the
   "first non-empty default-week pattern" template), an MSO row on a
   different base calendar, and a reopen-and-read of `Task.Duration` on the
   saved file (to settle whether `+84` is user-visible).  Commit the saved
   `.mpp` or at least the dump JSON and the probe script for each.
3. Add provenance to the oracle header: recorded-at, Project build number,
   SHA-256 of the base and of the saved `.oracle.mpp`, harness commit --
   `cmd_record` already has all of these in hand (:908-912, :932-934).
4. Wire `--suite` into ctest/CI so 7/8 or 8/8 is a gate, not a
   hand-reported number.
5. Have the product's own save (app_qt) run the same scenario once, so the
   "production writes 9600" sentence becomes a measurement.

Alternatively, the honest 7/8-and-stay option is the current state, with the
wording fixed: "left failing" not "not scored"; "the test applier's
`save_plan` path" not "the production save path".

Where the current wording overclaims:

- "deliberately not scored" (coordinator.md:77, :143; wave21-complete.md:83,
  e6.md:9-10 "stays 7/8") -- mechanism misdescribed; it is scored as FAIL
  and the suite exits 1.  Minor, but it is the phrase a reader would use to
  decide whether `--suite` is green.
- "the scored scenarios ... compare the production save path"
  (engine_diff_tests.cpp:1255, :2189-2190; e6.md:174-178, :325-327;
  wave21-complete.md:89-91) -- `save_plan` is not reached from the product.
  This is the material overclaim: it presents an engine-library result as a
  product result.
- "265 checks, 0 failures" placed next to "7/8" (coordinator.md:74-75,
  wave21-complete.md:21, wave22-complete.md:21) -- true, but a harness
  self-test whose MSO section pins the fitted rule against its own fitting
  data; it adds no parity evidence.
- "The clean re-record proved the old oracles were faithful"
  (engine-parity.md:507-514) -- supported by git (constraint-mso unchanged
  across three recording commits), but "proved" is strong for a JSON with
  no provenance fields and no committed saved-file hash.
- Not an overclaim but a scope note: 7 of 8 scenarios are manual-mode rows
  on a 3-task fixture; two of the seven passes measure that Project ignores
  a constraint on a manual row.  The headline "engine parity 7/8" should
  travel with that qualifier.

## Files read

- docs/engine-parity.md (:140-160, :200-215, :331-360, :420-445, :488-643)
- docs/handoff/e6.md, e5.md, wave21-complete.md (:78-100),
  coordinator.md (:70-80, :140-146), INDEX.md
- tests/engine_diff_tests.cpp (:1160-1315, :1740-1760, :1890-1945,
  :1965-1995, :2040-2240, :2277-2290, :2400-2569)
- scripts/engine_diff.py (:1-90, :540-680, :870-1035, :1097-1270)
- tests/scenarios/01..08-*.json; tests/data/scenarios/*.oracle.json (headers,
  task 3 of constraint-mso, task 2 of constraint-snet, top-level keys)
- CMakeLists.txt (:183-225); core/mppwrite/save.h (:74, :175), save.cpp (:551)
- git log/diff/show on tests/data/scenarios (read-only)
