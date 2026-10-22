# memory/session.md — W36: the session/document model (`core/session`)

_2026-10-22, W36 (wave 9). Owner: `core/session/`, `tests/session_tests.cpp`, this
file. No git command was run; the coordinator commits. The tree is shared with
four other wave-9 streams, so concurrent edits by them can make the ROOT
configure fail transiently (see Traps)._

## One line

`core::session::load(path)` turns a `.mpp` on disk into a `RecordSet` +
`SheetModel` + `GanttSource` + a copyable `Status`, with no AppKit and no ObjC;
`Session::recalculate()` re-runs `core/plan` from the in-memory `PlanInput`
(no file I/O) and refreshes rows, RecordSet values and Gantt bars;
`status_text()`/`dump()` are name-free renderings for tests and logs.

## Files

| path | what |
|---|---|
| `core/session/session.h` | `Options`, `Row`, `LoadedLink`, `Status`, `LoadResult`, `Session`, `load()`, `default_sheet_columns()`, `default_sheet_metrics()`, `default_gantt_style()`, `task_field_id()`, `kUidField = 0x0b4fff02` |
| `core/session/session.cpp` | the whole pipeline and the headless renderings; `core::plan::PlanInput`/`PlanResult` live behind a private `Session::PlanData` so the public header pulls no reader/plan types |
| `core/session/CMakeLists.txt` | `core_session` / `msproject::session`; links `core_plan core_cal core_sheet core_gantt` (PUBLIC); `session_tests` + ctest `session`; standalone mode builds `core_model`, adds `core/plan`, `core/sheet`, `core/gantt` with their tests/tools off |
| `tests/session_tests.cpp` | **821 checks** with corpus, **121** with `MPP_NO_CORPUS=1` |

Public API in brief:

```cpp
session::Options options;            // recalculate, use_file_calendar, calendar_name,
                                     // status_date, pixels_per_day (all defaulted)
session::LoadResult r = session::load(path, options);
if (!r) { r.error; }                 // never throws
session::Session& s = *r.session;
s.records(); s.sheet(); s.gantt_source(); s.gantt_layout(px_per_day);
s.rows(); s.row(i); s.task_start(i); s.task_finish(i); s.task_total_slack(i);
s.stored_task_start(i); s.row_uid(i); s.row_of_uid(uid); s.calendar();
s.status(); s.status_text(); s.dump(); s.recalculate();
```

`session::Session` is non-copyable and non-movable (the `SheetModel` holds a
`RecordSet&`); `LoadResult` owns it through `unique_ptr`. `Status` is copyable.

## What is verified (commands at the bottom)

### Synthetic, committed fixture `tests/data/synthetic-base.mpp`

* 3 tasks, 0 links, 0 summaries, 0 milestones; recalculation succeeds.
* `RecordSet` rows == `SheetModel` rows == `GanttSource::tasks` == Gantt bars == 3.
* Sheet: 8 columns (UID, ID, Outline, Name, Start, Finish, Duration, Total Slack);
  UID/ID/Outline/Total Slack are read-only.
* The layout carries a calendar; Mon 2026-01-05 is working, Sun 2026-01-04 is not.
* **The fixture is manually scheduled**: stored start 2026-01-04 13:00 (date-only)
  and stored finish 12:00; CPM aligns to Mon 08:00 / 17:00, so recalculated
  start/finish match the stored values **0/3**. This is the fixture's documented
  shape (`memory/plan.md`), not a session defect; the test asserts it explicitly
  together with equality to the independent `core/plan` run.
* `recalculate()` after `Options::recalculate = false` refreshes row + RecordSet
  + Gantt bar dates, and a second call is idempotent (same counters, no duplicate
  warnings).

### Corpus `corpus/project_a.mpp` (counts only)

```
CORPUS: project_a: 83 tasks, 88 links, 22 resources, 67 assignments, 16 summaries,
10 milestones, 35 critical, recalc start 83/83, finish 83/83, slack 64/83,
critical 83/83
```

* 83 tasks / 88 links / 88 endpoint-live / 0 skipped / 22 resources / 67
  assignments / 16 summaries / 10 milestones.
* `Session` rows, RecordSet values and Gantt bars agree with an **independent**
  `core/plan` run the test makes itself (reader -> `from_mpp` -> decoded calendar
  -> `recalculate`): every row's start/finish/slack/critical, and the match
  counters, are equal. Start and finish reproduce **every** stored value (83/83).
* Slack is 64/83 and critical 83/83 because the session compares against the
  **loaded file's own stored values**. That is `recalc --compare project_a project_a`:
  **312/332** fields. It is NOT the 82/83 slack in `memory/reader.md`, which
  compares the recalculation of PROJECT_A against `corpus/probe/project_a-recalc.mpp`
  (Project's re-saved copy). Both statements are correct; do not "fix" either.
* `status_text()`/`dump()` are asserted not to contain the project title or any
  of the 83 task names — that is 167 client-data checks on the real file.
* `layout.bars.size() == 83`, `layout.links.size() == 88`,
  `GanttSource::tasks.size() == 83`, `SheetModel::row_count() == 83`.

### Error paths

Empty path, a missing path and a non-MFP file (`tests/data/README.md`) each return
`LoadResult{nullptr, non-empty error}`; no exception escapes. A CPM failure is not
a load failure: the session is returned with `Status::recalculated == false`,
`Status::recalc_error` set and the stored dates shown.

### Dates: `mpp::PackedDate::raw` IS a `core::sched::date32` value

**Verified, not inferred.** `tools/recalc --compare` compares
`PlanTaskResult.start` (date32) against `mpp::Task::start.raw` verbatim
(`tools/recalc/main.cpp:397`) and reports start 83/83, finish 83/83 on PROJECT_A. Since
`plan::format_date` (date32, day 1 = 1984-01-01) and `mpp::format_date`
(packed, day 0 = 1983-12-31) both print the same civil date for that one numeric
value, the two encodings coincide numerically: engine day = file day. The session
therefore maps a packed date by **identity**, translating only `0`, `0xffffffff`,
day < 1 or day > 0xfffe, and a time word >= 14400 to `kDateNa`.

**Finding for the app stream (W32/W38): `app/layout_model.cpp` adds 1 to the day**
(`date32_from_packed` -> `pack(day + 1, ticks)`), which makes every app date one
day later than the engine's. `app/layout_model.cpp` is another stream's file and
was NOT touched; the session does not use that conversion. If W38 keeps the app
helper, PROJECT_A bars will sit one day to the right of the session's. Suggested fix:
replace the body with the identity mapping (or call the session).

## Decisions (ours; no oracle)

1. **Computed vs stored.** When recalculation succeeds the rows/RecordSet/Gantt
   show the recalculated start/finish (what Project shows after
   CalculateProject); `Row::stored_start/finish/total_slack/critical` keep the
   file's values for comparison and for the fallback. On failure, or with
   `Options::recalculate = false`, the stored values are shown.
2. **Summary flag is the reader's** (`child_count > 0`), used for the sheet and
   bar kind. `core/plan`'s summary is cross-checked; disagreements are counted in
   a warning (never resolved silently).
3. **Milestone is `Task::is_milestone() && !summary`**, matching the app's prior
   art.
4. **Per-task calendars are not scheduled.** `Row::calendar_uid` is exposed, and
   a count of tasks naming one is a warning; every task is scheduled on the
   project calendar (correct for PROJECT_A/PROJECT_B, whose rows are all -1; `holiday1.mpp`
   would need this).
5. **Read-only columns**: UID, ID, Outline, Total Slack. The session has no write
   path, so this is presentation only.
6. **`status_date` defaults to `kDateNa`**: no clock is read; the shell supplies
   it.
7. **`Status` counters are name-free by construction.** Reader warnings are
   counted, never copied (they can quote an orphan task name); a calendar decode
   failure is reported as a generic sentence (the decoder message can quote a
   calendar name). This is deliberate client-data hygiene.
8. **`recalculate()` does no file I/O** by construction (its body calls only
   `core::plan::recalculate` on the captured `PlanInput`). Verified by reading the
   code; it is not a black-box file-access test.

## Verification (MSVC 19.44, Release, `/W4 /permissive- /utf-8 /WX`)

```powershell
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul && cmake -S . -B build\vs-w36 -G `"Visual Studio 17 2022`" -A x64 -DCMAKE_BUILD_TYPE=Release -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w36 --config Release --target session_tests"
build\vs-w36\core\session\Release\session_tests.exe
#   SYNTHETIC: 3 tasks, 0 links, ... recalc start 0/3, finish 0/3
#   CORPUS: project_a: 83 tasks, 88 links, 22 resources, 67 assignments, 16 summaries,
#           10 milestones, 35 critical, recalc start 83/83, finish 83/83,
#           slack 64/83, critical 83/83
#   session_tests: 821 checks passed
$env:MPP_NO_CORPUS='1'; build\vs-w36\core\session\Release\session_tests.exe
#   SYNTHETIC ... ; SKIP: .../corpus/project_a.mpp is absent (MPP_NO_CORPUS is set)
#   session_tests: 121 checks passed
ctest --test-dir build\vs-w36 -C Release -R "^session$" --output-on-failure
#   1/1 Test #29: session ... Passed; 100% tests passed
```

Standalone mode (verifies the CMake branches; built in `build\vs-w36-standalone3`,
a stream-local tree, not the shared one):

```powershell
cmd /c "call `"$vc`" >nul && cmake -S core\session -B build\vs-w36-standalone3 -G `"Visual Studio 17 2022`" -A x64 -DMPP_BUILD_TESTS=OFF -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w36-standalone3 --config Release --target session_tests"
build\vs-w36-standalone3\Release\session_tests.exe    # 821 checks passed
```

`-DMPP_BUILD_TESTS=OFF` is needed for the standalone configure **only because
`core/plan`'s standalone path adds `core/mpp` with its tests on**, and another
stream's in-flight `core/mpp/CMakeLists.txt` briefly named two test sources that
did not exist yet. The root build was re-run after those files landed and does not
need the flag.

## Open (ranked)

1. **No write path.** The session cannot save; `SheetModel` edits are in-memory.
   `core/mppwrite` exists (W29 pins a task start with SNET). Wiring
   session-edit -> plan -> write is the next big step, and needs a decision on
   which store to write (`+64/+66` SNET is the measured-to-work path).
2. **Per-task calendars** (reader `+178` -> `core/cal::find_calendar_by_uid` ->
   `build_calendar`): the field is exposed on `Row`; the CPM still uses one
   calendar. `holiday1.mpp` is the fixture.
3. **PROJECT_B (dead-endpoint links)** is not pinned here. Expected: 73 tasks, 72
   links, 70 endpoint-live, `skipped_links == 2`, 73 assignments. Cheap to add to
   `test_corpus` if the coordinator wants the second real file in this gate;
   PROJECT_A only was specified.
4. **No sort/filter/group**: the sheet reads RecordSet order, as designed.
5. **Status date** should eventually come from the file if the reader exposes one;
   today it is the caller's.
6. **`app/` date shift** (see above) should be fixed when W38 wires the shell to
   the session; the app's private `PlanInput`/`TaskRow` can then be deleted.

## Traps

* **Do not add 1 to a packed day.** `mpp::PackedDate::raw` is a `date32` value;
  the app's `+1` is a bug (see "Dates").
* **Slack comparisons are file-dependent**: 64/83 against PROJECT_A itself (312/332
  with dates+critical), 82/83 against `project_a-recalc.mpp`. Do not pin one number
  without naming the compared file.
* **`synthetic-base.mpp` is manual**: 0/3 date matches is correct.
* **Root configure can fail transiently on another stream's half-written
  `core/mpp/CMakeLists.txt`** (missing `tests/mpp_*.cpp`). Retry the identical
  configure; do not edit another stream's file. A stream-local standalone tree
  (`-DMPP_BUILD_TESTS=OFF`) keeps you unblocked meanwhile.
* **MSB6003 on a `.tlog`** after an incremental build is Dropbox/AV churn: retry
  the identical build (happened once here; the retry was clean).
* The `session_tests` binary is at
  `build\vs-w36\core\session\Release\session_tests.exe`, not `build\vs-w36\Release\`.
* `ctest -R session` is safe, but anchor as `^session$` like the other streams.
* Client-data rule: never print `Status::title`, `Row::name`, or a calendar name
  from a corpus file. `status_text()`/`dump()` omit both by design; the tests
  assert it.
* Dropbox mtime churn makes the editor's "file changed since read" guard trip on
  this tree; re-read immediately before editing (it happened repeatedly here).

## Next three actions

1. **Coordinator**: commit `core/session/` + `tests/session_tests.cpp` +
   `memory/session.md`; point W38 (app shell v2) at `core::session` for its rows,
   sheet and Gantt, and at the date-identity finding before it ports the app
   mapping.
2. **W38/app**: replace `app::date32_from_packed` with the identity mapping and
   add a one-day assertion against a known PROJECT_A date (or delete the app mapping in
   favour of the session's).
3. **Successor (session)**: add the PROJECT_B case to this gate (2 dead-endpoint
   links), then wire `core/cal` per-task calendars (`Row::calendar_uid` is
   ready), then start the write path with W29's SNET approach.
