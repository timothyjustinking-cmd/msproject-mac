# Wave 29 - the completion record

One table row per brief item.  A group ends only when its exact
`WAVE-29-<group>-COMPLETE` line is appended below (wave-29-brief section 1).
Deferrals need the attempt evidence the standing rules and the brief name.

## G0 - rule 1 for wave 28's commands (the wave-28 review's four High findings)

| item | finding | status | evidence |
|---|---|---|---|
| W29-0.0 | High: the enabled commands cover unmeasured shapes | **DONE** | commit `21ca5b2`; gates `build/w29/gates-b6c28d0-W29-0.0-*.log` |
| W29-0.1 | High: the save-pair tests mask Project's changes | **DONE** | commit `f650c10` and the record commit that adds this file; gates `build/w29/gates-21ca5b2-W29-0.1-*.log` |
| W29-0.2 | High: the percent writer on real schedules | **DONE** | commit `b751435`; gates `build/w29/gates-4a644c7-W29-0.2-*.log` |
| W29-0.3 | High: Mark on Track / Update Project on rows with progress | **DONE** | commit `bd9e13d`; gates `build/w29/gates-61f19c8-W29-0.3-*.log` |
| W29-0.4 | High: indent and outdent on auto-scheduled rows | PENDING | - |

W29-0.1 detail:

- Both save-pair tests now compare every owned stream with PROJECT's file,
  field by field, and every stream outside the owned set is either
  "port == Project" or a named `EXPECTED FAIL` check -- never "port ==
  control":
  - `app/app_edits_actuals_tests.cpp`: the six Task streams, the six
    TBkndAssn streams and the 114/Props counters by name; 15 named gaps per
    pair (13 streams Project rewrites: SummaryInformation,
    DocumentSummaryInformation, 114/TBkndCal/Fixed2Data,
    114/TBkndRsc/Fixed2Data, 214/CEdl/{FixedData,Var2Data,VarMeta},
    214/CFilter/FixedData, 214/CGrouping/FixedData, 214/CTable/FixedData,
    214/CV_iew/FixedData, 214/Props, Props14; plus the two per-save opaque
    Props id words 0x02400029 / 0x024013c2).
  - `app/app_edits_outline_tests.cpp` and `tests/roundtrip_outline.cpp`: the
    same discipline for the Task and TBkndAssn streams and 114/Props; 14
    named gaps per outline pair (12 streams, the same two id words).  The
    core test also chains indent->outdent and keeps four named expected
    failures for the chained input's two assignment counters, because that
    input carries Var2 blocks Project's summary save had already dropped.
- The measured gaps this group had to close are written:
  - actuals (`core/mppwrite/actuals.cpp`): Task Fixed2Meta +88, Task
    FixedMeta +44 and the +8..23 blob, the TBkndAssn actual-work update
    (planned/actual/remaining, resume/stop, the 0F400032 append and remove),
    and the six 114/Props counters.
  - outline (`core/mppwrite/outline.cpp`): the indent tombstone (state 0x02,
    count 0, header 4 -> 6, the VarMeta logical removal with the stale
    physical tail kept), the outdent re-create (row uid max+1, new FixedMeta
    and Fixed2Meta entries, the parent task's identity, four Var2 blocks),
    and the six 114/Props counters.
- No W29-0.0 refusal is lifted.  The commands stay enabled only for the
  shapes the committed pairs measured; W29-0.2 .. W29-0.4 lift a refusal only
  when their own pair passes field by field.
- Project's reopen of the port's saves, for rule 1
  (`scripts/oracle_w29_reopen.py`, copies under `build/oracle-work`; facts in
  `build/w29/W29-0.1-project-reopen.json`, sha256
  `cc68d5957124b86611477f954270e57945820d3b45515ee4ba9afd69b00c8d1f`):
  Project 16.0 reopened 10 port saves against Project's own save of the same
  edit -- five actuals, three outline, two new-task-mode -- and the comparison
  reports **0 differing fields in every pair** over the project summary row
  and IDs 1..3 (Manual, Summary, OutlineNumber, Duration, Start, Finish,
  PercentComplete, PercentWorkComplete, Actual/Remaining Duration,
  ActualStart/ActualFinish, Work, ActualWork, RemainingWork,
  NewTasksCreatedAsManual).  The port-only saves check out too:
  `app-actuals-mixed.mpp` reopens at 40/83/100 % on IDs 1..3, and the insert
  saves' highest ID carries Manual = True (manual insert) / False (auto
  insert).  The wave-28 fixtures' TBkndAssn rows are resource-less (resource
  word 0xFFFF0001), so Project exposes no Assignment objects and that layer is
  covered by the field-by-field TBkndAssn comparison, not by the reopen.
- The reopen artifact is under `build/` and is not committed, as standing
  rule 3 requires; this record quotes its path, hash and numbers.

W29-0.2 detail:

- The three row-local wave-29 pairs pass field by field against PROJECT's
  file (`app_edits_actuals_tests`, pairs `wave29-leaf40`, `wave29-work40`,
  `wave29-milestone100`):
  - leaf under a summary (ID 3, 0 -> 40): the summary rows above the leaf
    take Project's roll-up (+80 = own Duration * sum(leaf actual) / sum(leaf
    Duration), +88 = the rest, +92 = rounded percent, +120 = the summary's
    Start2, +132/+136 = the earliest valid leaf resume/stop); the project
    row takes its Resume/Stop from the earliest WRITTEN summary date
    (3d462418) even though that is later than the project start;
  - work-bearing assignment (ID 6, 0 -> 40): task +94 and +8/+16/+24, the
    assignment's +28/+44/+60/+104, 0F400031 72 -> 100 bytes (one record per
    started 4800 ticks, appended and repointed), 0F400032 appended at 56
    bytes, the project row's +94/+8/+16/+24 from the real-assignment sums;
  - zero-duration milestone (ID 7, 0 -> 100): +92/+94/+104/+108/+120/+124/
    +132/+136, +168 |= 0x9000, Fixed2Data +42 0x01 / +46 NA, a step block
    appended, 0x0B4004E3/0x0B4004E5 removed, 0x0B4004E4 12 -> 8 with 4
    freed bytes, a 56-byte all-zero 0F400032 block, FixedData row and
    0F400031 untouched.
- The mid-to-mid pair (ID 4, 25 -> 60) stays refused: the same save
  reschedules the chain (summary durations 26400 -> 23040 ticks, the
  summary/project roll-up and dates, ID 3's Start/Finish copies and start
  anchor, ID 5 and the milestone's Start2/Finish2 and +186/+190 slack, the
  edited row's Actual/Remaining Duration and Resume/Stop).  The test names
  all 50 moved words as EXPECTED FAILURE checks against Project's file, and
  `make_percent_edit` refuses the transition (the refusal is the anti-rot
  guard for that list).
- The app gate lifts the percent edit only for the three measured
  transitions on automatic rows; combinations and every other transition
  stay refused.  Date updates keep the W29-0.0 refusals for W29-0.3.
- The anchor rule is measured, not assumed (`scripts/oracle_w29_anchor_probe.py`,
  facts `build/w29/W29-0.2-anchor-probe.json`): the same flat auto leaf
  saved twice with no edit keeps +174 = NA, and the 40 % edit writes +174 =
  Start, so the wave-28 write is the percent rule rather than save churn.
- `tests/data/README.md` corrected: the work control's 0F400031 is 72 bytes
  (44 + one record), the dropped task fields are 0x0B4004E3/0x0B4004E5, and
  the mid pair's moved fields are listed as measured.

W29-0.3 detail:

- Five new Project 2021 save pairs on a shaped synthetic base whose ID 1 is
  automatic and already records 60 % (ActualStart 2026-01-05 08:00; the
  0x0B40015E block is marker, middle 60, final 60): Mark on Track on ID 1
  with the status date after (2026-01-09) and before (2026-01-07) the
  recorded progress, UpdateProject(All=True) on both dates, and
  UpdateProject(All=False) on a two-of-three selection (IDs 1 and 2).  The
  oracle is `scripts/oracle_w29_update_progress.py`; facts
  `build/w29/W29-0.3-project-progress-update.json` (not committed).
- Project DOES lower recorded progress: 60 -> 40 when the through date
  precedes it, 60 -> 80 when it follows.  The App gate
  (`measured_progress_update_shape`) lifts the W29-0.0 date-update refusal
  only for the measured automatic top-level 60 % shape with an ActualStart
  equal to the effective Start and a progress block; every other progressed
  row keeps the refusal, and the 25 -> 60 mid transition stays refused.
- 0x0B40015E: a raise appends middle 80 + final 80 (76 -> 96 bytes at a new
  Var2Data offset, 616 -> 716); a lower REPLACES the stored middle with a
  fresh id and rewrites the same 76-byte payload in place (offset 536,
  VarMeta untouched, Var2Data stays 616).  Our writer refuses a lowering
  from any other block shape.
- 0F400031: a raise/0..99 rebuild keeps the W29-0.2 rule; a lower writes
  magic 0x00200001, clears +32, record 0 = (new remaining work, units*16,
  new ticks*8) and record 1 = (the old first record's remaining work and
  units, the NEW tick word).  A real assignment or a non-two-record block
  refuses to lower.
- 0F400032: a date update's replaced block does NOT move Props 0x04000002
  (all five pairs keep it at 0; a percent edit still counts the removed
  payload: wave-28 40 -> 100 counts 56, 100 -> 0 counts 76).  The raise's
  second record is (old actual work, units 0, new tick word); the lower's
  second record is the old record byte for byte.
- Scope: only the entire-project Update Project makes the project row's
  Resume/Stop the earliest per-row dates (3bf31c20) and stores the status
  date (0x3bf60000 / 0x3bf40000).  The two-of-three Selected scope keeps
  the project start (3bf11e78) and leaves the status-date record at
  0xffffffff.  The edit carries `update_entire_project` (the scope's uid
  list vs every live saved non-summary task); the save layer uses it for
  both decisions instead of the caller's status-date flag.
- The Props comparison now masks the whole 4-byte per-save id word at
  payload +0 for records 0x02400029 / 0x024013c2: the mark-before control
  and result differ in every byte of that word (a961.... vs b02c....), so
  the old 2-byte mask left the two files unequal.  The expected-failure
  check is likewise over the full word.
- `app_edit_tests` had four stale W29-0.2 checks still asserting the
  pre-lift refusals for the leaf-under-a-summary and work-bearing shapes;
  they now assert the measured 0 -> 40 lifts (the milestone 0 -> 40 refusal
  stays).  208 checks pass.
- No Project COM reopen of the PORT's saves was run for this item, so this
  record makes no reopen claim; the field-by-field comparison is against
  Project's own file and the app's reader reopens the port's save.

Gates for commit `bd9e13d` (parent `61f19c8`), logs under `build/w29/`
(`gates-61f19c8-W29-0.3-*.log`):

| gate | result |
|---|---|
| core Release build (`core_mppwrite`, `mppwrite_roundtrip_outline`) | exit 0 |
| core `mppwrite_roundtrip_outline` | 492 checks, 0 failures, 49 expected failures, exit 0 |
| app Release build (ALL_BUILD) | exit 0 |
| `app_edits_actuals_tests` | 2280 checks, 0 failures, 278 expected failures, exit 0 |
| `app_edit_tests` | 208 checks, 0 failures, exit 0 |
| `app_edits_fields_tests` | 160 checks, 0 failures, exit 0 |
| `app_edits_links_tests` | 200 checks, 0 failures, exit 0 |
| `app_edits_new_task_mode_tests` | 1933 checks, 0 failures, exit 0 |
| `app_edits_outline_tests` | 988 checks, 0 failures, 42 expected failures, exit 0 |
| `project_dialogs_tests` | 70 checks, 0 failures, exit 0 |
| `start_edit_warning_tests` | 41 checks, 0 failures, exit 0 |
| `check_fixtures.py` | fixtures OK (90 documented hashes, 70 tracked `.mpp`); paths OK (112 files); provenance OK (8 oracles) |
| client-name scan on HEAD and the staged tree / name guard `61f19c8..HEAD` / `client-string` grep | 0 files, 0 hits / exit 0 / no matches (exit 1, expected) |

Gates for commit `b751435` (parent `4a644c7`), logs under `build/w29/`
(`gates-4a644c7-W29-0.2-*.log`):

| gate | result |
|---|---|
| core Release build (`core_mppwrite`, `mppwrite_roundtrip_outline`) | exit 0 |
| core `mppwrite_roundtrip_outline` | 492 checks, 0 failures, 49 expected failures, exit 0 |
| app Release build (three edit test targets) | exit 0 |
| `app_edits_actuals_tests` | 1452 checks, 0 failures, 203 expected failures, exit 0 |
| `app_edits_outline_tests` | 988 checks, 0 failures, 42 expected failures, exit 0 |
| `app_edits_new_task_mode_tests` | 1933 checks, 0 failures, exit 0 |
| `check_fixtures.py` | fixtures OK (80 documented hashes, 60 tracked `.mpp`); paths OK (102 files); provenance OK (8 oracles) |
| client-name scan on HEAD and the staged tree / name guard `4a644c7..HEAD` / `client-string` grep | 0 files, 0 hits / exit 0 / no matches (exit 1, expected) |

Gates for commit `f650c10` (parent `21ca5b2`), logs under `build/w29/`
(`gates-21ca5b2-W29-0.1-*.log`):

| gate | result |
|---|---|
| core Release build (`core_mppwrite`, `mppwrite_roundtrip_outline`) | exit 0 |
| core `mppwrite_roundtrip_outline` | 492 checks, 0 failures, 49 expected failures, exit 0 |
| app Release build (three edit test targets) | exit 0 |
| `app_edits_actuals_tests` | 1028 checks, 0 failures, 105 expected failures, exit 0 |
| `app_edits_outline_tests` | 988 checks, 0 failures, 42 expected failures, exit 0 |
| `app_edits_new_task_mode_tests` | 1933 checks, 0 failures, exit 0 |
| `check_fixtures.py` | fixtures OK (72 documented hashes, 52 tracked `.mpp`); paths OK (94 files); provenance OK (8 oracles) |
| client-name scan on HEAD and the staged tree / name guard `21ca5b2..HEAD` / `client-string` grep | 0 files, 0 hits / exit 0 / no matches (exit 1, expected) |
