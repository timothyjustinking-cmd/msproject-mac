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
| W29-0.4 | High: indent and outdent on auto-scheduled rows | **DONE** | commit `8f80c2a`; gates `build/w29/gates-7fc719b-W29-0.4-*.log` |

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
- Citation for the raw words above (CLOSE errata B4): the 0x0B40015E
  sizes/offsets are in `build/w29/W29-0.3-15e-mark.log`; the 0F400032 counts
  and the per-stream changes are in `build/w29/W29-0.3-changed-streams.log`;
  the full-row walks (including the 3bf status words) are in
  `build/w29/W29-0.3-fullrow.log`.  The committed JSON carries the COM date
  strings, not those raw words.
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

W29-0.4 detail:

- Four new Project 2021 save pairs on the synthetic base, all built by
  `scripts/oracle_w29_outline_auto.py` through Project on the neutral root
  (`C:/w29/fixtures-outline`); facts, including Project's COM reopen of every
  one of the eight files, are in
  `build/w29/W29-0.4-project-outline-auto.json` (not committed).  Rows stay
  ID-only in the record.
  - `wave29-outline-auto-indent(-control)`: all rows automatic; ID 2
    indented under its preceding sibling ID 1, which becomes a summary
    (summary span 0x3840);
  - `wave29-outline-auto-summary(-control)`: ID 3 indented under ID 2 first,
    then ID 2 (with ID 3) indented under ID 1 -- the summary-making indent,
    summary span 0x2580;
  - `wave29-outline-auto-outdent(-control)`: the automatic indent shape,
    then ID 2 outdented back to the top level (restored duration 0x5dc0);
  - `wave29-outline-manual-parent(-control)`: the hybrid -- ID 1 manual,
    ID 2 automatic; ID 2 indented under ID 1, then outdented again.
- The five schedule words Project's save rewrites on EVERY recalculated row:
  +74 == +186 == +190 = `work_time_between(row resulting +108, resulting
  project +108)`; +116 = resulting project +108; +112 = `sub_work(resulting
  project +108, row resulting +84 duration)` -- the backward pass, NOT
  `add_work(row +104, slack)`.  The project row is the exception: its +112
  is the project-start record (the hybrid pair's 3bf11e78 lies earlier than
  the backward pass lands).  The earlier add_work guess fails the automatic
  pairs: auto-indent ID 3's +112 is 3bf312c0, while add_work(+104, 4800)
  lands on 3bf227d8.
- Resulting durations the backward pass uses: indent -> new summary and
  project row take the summary span (0x3840 auto-indent, 0x2580
  auto-summary); outdent -> restored parent and project row take 0x5dc0.
  The resulting project finish is the latest subtree finish on indent and
  the restored finish on outdent.
- Project's save also rewrites the project row's display/effective words:
  automatic indent sets +84/+88 = summary span and +100/+108 = latest;
  automatic outdent sets +84/+88 = restored duration and +100/+108 =
  restored finish (the hybrid leaves +88/+108, while +84 still ends 0x5dc0).
- Hybrid (automatic child of a manual parent): the manual parent IS
  recalculated and joins the slack set (+74/+186/+190 = 9600, +112
  Wed 08:00), while the untouched manual row keeps its stale words; the
  project row's stored +96 stays the project-start record and its +108
  stays the restored finish.  A mixed-scheduling indent and a manual row
  under an automatic parent stay refused.
- The writer's automatic gates (all before the first mutation): manual
  project row; summary not level 1 / leaf not level 2; project start not
  the subtree's earliest date; another top-level row finishing after the
  rolled-up finish; the moved row finishing after the restored finish; and
  a slack vector that does not name exactly the live automatic rows (plus
  the restored manual parent of a hybrid outdent) once each with valid
  packed LateStart/LateFinish.  The manual shape still takes no slack
  vector.
- The app computes the same words with the project calendar
  (`build_outline_slack` in `app/edit_model.cpp`) from each row's resulting
  +108/+84 and passes them through `OutlineEdit::slack`; its display update
  now also moves the summary's effective finish and the project row's
  duration/finish, and `without_structural_edits` restores the automatic
  anchor's finish from the file so an undo still matches the control.
- Both save-pair tests compare every recalculated row's five words, the
  project row's duration/finish and the four automatic pairs field by field
  against PROJECT's file; the core refusal cases cover a partial slack set,
  a non-date LateStart, an automatic outdent without slack and a manual
  indent carrying slack.
- No Project COM reopen of the PORT's saves was run for this item, so this
  record makes no reopen claim; the eight-file reopen JSON above is Project
  reopening its own measured files, and the port is judged against Project's
  file field by field.

Gates for commit `8f80c2a` (parent `7fc719b`), logs under `build/w29/`
(`gates-7fc719b-W29-0.4-*.log`):

| gate | result |
|---|---|
| core Release build (`mppwrite_roundtrip_outline`) | exit 0 |
| core `mppwrite_roundtrip_outline` | 1081 checks, 0 failures, 108 expected failures, exit 0 |
| app Release build (eight edit/dialog test targets) | exit 0 |
| `app_edits_actuals_tests` | 2280 checks, 0 failures, 278 expected failures, exit 0 |
| `app_edit_tests` | 208 checks, 0 failures, exit 0 |
| `app_edits_fields_tests` | 160 checks, 0 failures, exit 0 |
| `app_edits_links_tests` | 200 checks, 0 failures, exit 0 |
| `app_edits_new_task_mode_tests` | 1933 checks, 0 failures, exit 0 |
| `app_edits_outline_tests` | 2470 checks, 0 failures, 101 expected failures, exit 0 |
| `project_dialogs_tests` | 70 checks, 0 failures, exit 0 |
| `start_edit_warning_tests` | 41 checks, 0 failures, exit 0 |
| `check_fixtures.py` | fixtures OK (98 documented hashes, 78 tracked `.mpp`); paths OK (120 files); provenance OK (8 oracles) |
| client-name scan on HEAD and the staged tree / name guard `7fc719b..HEAD` / `client-string` grep | 0 files, 0 hits / exit 0 / no matches (exit 1, expected). CLOSE errata A3: the cited per-item guard and grep logs are empty; the G0 group log's `guard_names 21646f4..HEAD` exit 0 and zero named commits are the range evidence. |

Gates for commit `bd9e13d` (parent `61f19c8`), logs under `build/w29/`
(`gates-61f19c8-W29-0.3-*.log`; only four files carry that prefix -- the
remaining test logs recorded in this table are the same run's
`61f19c8-W29-0.3-*.log` files without `gates-`, as recorded in CLOSE errata
B2):

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
| client-name scan on HEAD and the staged tree / name guard `61f19c8..HEAD` / `client-string` grep | 0 files, 0 hits / exit 0 / no matches (exit 1, expected). CLOSE errata A3: the cited per-item guard and grep logs are empty; the G0 group log's `guard_names 21646f4..HEAD` exit 0 and zero named commits are the range evidence. |

Gates for commit `b751435` (real parent `6581a74`, the sha-map commit; the log
tag `4a644c7` is the pre-rewrite ID of the W29-0.2 fixture commit, mapped to
`eddd9e9` in `wave29-sha-map.txt`), logs under `build/w29/`
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
| client-name scan on HEAD and the staged tree / name guard `4a644c7..HEAD` / `client-string` grep | 0 files, 0 hits / exit 0 / no matches (exit 1, expected). CLOSE errata A3: the cited per-item guard and grep logs are empty; the G0 group log's `guard_names 21646f4..HEAD` exit 0 and zero named commits are the range evidence. |

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
| client-name scan on HEAD and the staged tree / name guard `21ca5b2..HEAD` / `client-string` grep | 0 files, 0 hits / exit 0 / no matches (exit 1, expected). CLOSE errata A3: the cited per-item guard and grep logs are empty; the G0 group log's `guard_names 21646f4..HEAD` exit 0 and zero named commits are the range evidence. |

G0 group gates (fresh trees configured from scratch on `50df4b2` under
`build/w29-fresh{,-app,-qt}`, built serially, log
`build/w29/g0-fresh-gates.log`; `50df4b2` is the W29-0.4 record, docs-only
over the code tree `8f80c2a`):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 checks / 420 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app ctest / app_qt ctest | 20/20 / 24/24 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (98 documented hashes, 78 tracked `.mpp`); paths OK (120 files); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `21646f4..HEAD` / `guard_names` `wave28..HEAD` / hex grep | 0 files, 0 hits / 0 commits / exit 0 / no matches (exit 1, expected) |

The numbers belong to `50df4b2`; the marker commit that follows is docs-only.

WAVE-29-G0-COMPLETE

## G1 - PROJECT_A gaps (decision 7)

| item | finding | status | evidence |
|---|---|---|---|
| W29-1.0a | High (CI): wave 28's two platform compile errors are still in the tree | **DONE** | commit `a790a24`; gates `build/w29/gates-4fee013-W29-1.0a-*.log` |
| W29-1.0b | High (R28-1 residual): separate Project's per-save noise from the edit's effects | **DONE** | commits `fed21fb`, `2b9862e`; gates `build/w29/gates-a790a24-W29-1.0b-*.log` and `build/w29/gates-fed21fb-W29-1.0b-*.log` |
| W29-1.1 | Stored sheet column widths: 8 columns as the file stores them | **DONE** | commit `7b01d37`; gates `build/w29/gates-2b9862e-W29-1.1-*.log` |
| W29-1.2 | The yellow Highlighted row fill, as stored per row | **DONE** | commit `1a743b6`; gates `build/w29/gates-7b01d37-W29-1.2-*.log` |
| W29-1.3 | Bar-label coverage: every label the stored bar styles call for | **DONE** | commit `aaf05fe`; gates `build/w29/gates-1a743b6-W29-1.3-*.log` |
| W29-1.4 | Timescale scroll origin and zoom as stored | **DONE** | commit `c79a45d`; gates `build/w29/gates-aaf05fe-W29-1.4-*.log` |

W29-1.0a detail:

- macOS (`app/main.mm`): `note(...)` is called at 722/728/732/740 and was
  defined at 1347; a forward declaration now sits with the other forward
  declarations above the first use.
- Linux GCC (`tests/roundtrip_outline.cpp`): the anonymous-namespace
  `describe(...)` at 296 is unused and is removed.
- A scan of all 43 C++/header files wave 28 and wave 29 touched found no
  other instance of the two patterns
  (`build/w29/W29-1.0a-pattern-scan.log`); the scan is falsified on the two
  pre-fix blobs, which it does flag
  (`build/w29/w29-1.0a-falsify.py`; CLOSE re-run at
  `build/w29/W29-1.0a-falsification.log`, "FALSIFICATION OK: both pre-fix
  files are flagged").  No clang or GCC is
  installed on this machine, so this record makes no claim that the Windows
  gates see either error; the CLOSE dispatch is the real compiler check --
  and on the first dispatch it did see a third one (the unused constant
  `kPropSessionWordCopy`, fixed as W29-C3).

W29-1.0b detail:

- `fed21fb` (fixtures only): Project 2021 opened a copy of each of the 23
  actuals/outline save-pair control fixtures under `C:/w29` and saved it with
  no edit, then saved the same file a second time.  The first save of each is
  committed as `tests/data/<control-stem>-null.mpp`; the second stays under
  `build/w29` and its per-stream stability is recorded in
  `build/w29/W29-1.0b-null-saves.json`
  (`scripts/oracle_w29_null_saves.py`).  Every committed null save was checked
  against its `-save1` snapshot byte for byte, and the staged path scan reports
  no fixture embeds a local path.
- `2b9862e`: `tests/save_pair_noise.h` classifies every compared stream:
  `fixed_block` 16-byte records (a differing byte is per-save noise and is
  masked); `props14/cedl` variable-length streams (size-cache head zeroed, the
  save-path record sentinel-replaced, compared per byte, framing tail reported
  separately); `exact` DocumentSummaryInformation (zero measured noise).  A
  byte that differs between the null save and Project's edit save outside the
  mask is the edit's effect: it must equal the port's save or the check fails,
  and an unmeasured shape is refused.
- Measured on the 22 compared pairs (15 actuals, 7 outline): pair totals
  noise=2549, effects=2614, masked-tail=836; DocumentSummaryInformation exact
  noise=0, effects=2501 (17/22 pairs changed by the edit; the port matches all
  2501 bytes); 114/Props block noise=338, effects=92; Props14 var noise=112,
  effects=20, tail=836; 114/TBkndCons/Fixed2Meta block noise=0, effects=1;
  SummaryInformation block noise=120, effects=0; the other Fixed2Data/Var2Data/
  FixedData streams noise only (for example TBkndCal 330, TBkndRsc 220,
  CFilter 220, CGrouping 220, TBkndCons 135, CV_iew FixedData 330).
- The missing effects are now written: the Props14 marker payload
  0x00 -> 0x01 (`edit_marker.cpp`), the project row's display summary in
  DocumentSummaryInformation (`summary_info.cpp`, rebuilt from the row and
  refusing unmeasured shapes), and the link driving-state byte
  (`add_delete.cpp` stage_progress_link_flags).  The falsified "first/later
  link row" reading of Fixed2Meta +8 is corrected in `add_delete.h`.

W29-1.1 detail:

- A `CTable` column stores its width in characters; Project multiplies by a
  pixel unit measured from the file's default text font (the first
  `FONT_BASES` entry, the pair the ribbon Font box shows), the mean lowercase
  advance rounded.  Measured with Qt 6.5.3/GDI on this box: Arial 8
  133/26 = 5.12 -> 5 px, Segoe UI 10 167/26 = 6.42 -> 6 px, Calibri 11
  179/26 = 6.88 -> 7 px (Carlito 11 the same); the pixel width is
  characters * unit + 3 padding.
- PROJECT_A stores Indicators 0, Task Name 70, Dur 9, Start 19, Finish 21,
  % Complete 7, Pre 13, Suc 13, SUMMARY 17; at Arial 8/5 px those are
  353/48/98/108/38/68/68/88 px, which at 1920 put the display columns at
  viewport offsets 0/353/401/499/607/645/713/781 -- the clean capture's
  boundaries minus the 54 px gutter.
- A stored 0 is not "auto": Project draws no column for it, so the record
  stays in the sheet model with width 0 and `visible=false` (PROJECT_A's
  Indicators).
- The measured face/unit pairs are table-driven in `entry_columns.cpp` so a
  Linux or macOS CI runner without those faces paints identical widths; an
  unmeasured face is measured live with `QFontMetrics` and clamped.  The
  committed fixture's default font is Segoe UI 10 (unit 6), so its Entry
  presentation is unchanged: 39/57/147/81/81/81/105/105/123.
- `qt_stored_table_corpus` checks the 8 stored data columns plus the stub at
  353/48/98/108/38/68/68/88/103 px, Indicators hidden, offsets 0..781 at
  1920.

W29-1.2 detail:

- The fills are not in the text styles (every PROJECT_A text style stores an
  automatic background); they are the applied Gantt view's `214/CV_iew` item
  `0x2240000c`, Format > Font formatting saved per cell.
- The payload is a plain sequence of 44-byte entries with no count header; a
  row's entry is keyed by its task UniqueID and the `214/CTable` field id,
  with 0xffffffff as the row default; the field-specific entry wins.  The
  colour is R,G,B plus an automatic flag (the same fourth-byte rule
  `gantt_style.cpp` applies); automatic means no stored fill, so the Text
  Styles background still applies.
- PROJECT_A's applied view stores 213 entries (13 project-row, 10 each for
  20 task rows).  The Highlighted row uid 10742 stores (255,242,0) with
  pattern 1 on nine fields; its tenth, the hidden zero-width Indicators
  field, is automatic.  The project row stores grey 82,87,91 on its visible
  fields and dark blue 0,55,103 as the default.
- The shell looks the entry up with the STORED `214/CTable` field id: the
  table maps DurationText/StartText/FinishText 0x0b400507/505/506 to the
  model's Duration/Start/Finish, and the record keys on the stored ids, so
  `SheetColumn`/`DisplayColumn` now carry `table_field` alongside the mapped
  model field.
- The other `0x2240000c` record in PROJECT_A (view 4, 484 bytes) does not
  follow this layout; it is not the applied view, and the decoder is only
  ever handed the applied Gantt view's record.
- `qt_stored_table_corpus` checks all eight scrolling cells of ID 10 yellow
  (painted Name cell yellow; 6407 yellow / 18892 grey pixels).

W29-1.3 detail:

- The corpus file's applied view stores ten bar styles; four name text:
  Critical Task and Non-Critical Task put Name on the right, Milestone puts
  StartText on the left and Name on the right, Summary puts Name on the left,
  and Project Summary stores none.  The clean capture shows exactly that, so
  each label is the style's, not a constant fallback.
- Each 195-byte BarStyle record stores five label field ids
  (left/right/top/bottom/inside at +67..+83) and two 64-bit Show For masks at
  +49/+57.  A style applies when every SET Show For category's fact is true
  and every SET Not Show For category's fact is false; a category the port
  cannot answer makes the style never match.
- Answered from the model: Normal, Summary, Critical, Noncritical, Finished,
  In Progress, Manually Scheduled, Active and the project row.  False by
  construction: Split, Rolled Up, Group By Summary, Deliverable, Dependency,
  Warning and the Placeholder variants.  Marked and the flag / early-late
  variants stay unanswered.
- The first matching style that stores a field on a side claims it, so an
  earlier specific style that stores no label cannot hide a later general
  style's text.  A side with no field stores the record's 0xffffffff
  sentinel (the stock Manual Task style needs this and its Not-Warning bit to
  match).  A field id the port cannot map claims the side with empty text
  rather than borrowing another style's.
- The date label is the file's own date format with the weekday and time
  cleared (the capture prints a bare dd/MM/yy), which is why `open_file`
  now rebuilds the views after `apply_preferences`.  Top/bottom/inside are
  unmeasured by the capture: top/bottom are centred one label gap
  above/below the bar and inside spans the interior.
- `qt_stored_table_corpus` classifies every drawn bar by its row: 57 normal
  (right name), 16 summary (left name), 10 milestone (left date + right
  name), 1 project (none), with both horizontal gaps exactly 4 px.

W29-1.4 detail:

- The applied view's style record `0x22400008` carries both saved fields at
  +1176 (u32) and +1180 (u16).  PROJECT_A stores 220478400 (= 2025-12-01
  00:00, tenths of a minute since 1983-12-31) and bytes 06 01 = 262, not
  MPXJ's low-byte 6.  Every other 41363-byte view stores +1181 = 0; several
  store exactly 14400 (1984-01-01, Project's stock default), which must not
  pin the axis.  CLOSE errata (W29-C6): a stock start with the stock 100 %
  day tier now changes nothing at all.  That width is the shell's own week
  zoom (168 px / 7 = 24 px/day), and re-basing the axis to days moved the
  wave-23 outline parity left edge from the measured week start to the
  project-start day (fixed in `7e14aaa`).
- The size is a u16 in Project too: on the same synthetic view and window, a
  copy patched only at +1180 from 0x0064 (100) to 0x0106 (262) renders 2.625x
  wider (15.583 -> 5.936 visible days, 23.6 -> 61.8 px/day).  A copy patched
  to 0x0006 renders 12x wider, not the linear 16.7x, so Project floors a very
  small column; the 262 pair is the evidence for the high byte.
- The percentage scales the BOTTOM TIER's unit column, not a fixed day.  Day
  tier: 100% = 24.0 px/day (the wave25 reference and the size-100 control).
  Month tier: 100% = 38.2 px/month, from the capture's calendar-proportional
  month columns (102 px for 31 days, 98-99 px for 30) and 3.2875 px/day at
  262%.  PROJECT_A stores months, so 38.2 * 2.62 / 30.4375 = 3.2882 px/day,
  within 0.0007 of the measured 3.2875 (test tolerance 0.02).  The model
  emits a calendar-day axis and GanttCanvas paints the stored
  year/quarter/month tiers from their own units, so months stay proportional.
- The origin is pinned, not a lower bound: a task before it sits at x < 0
  (clipped) instead of pulling the axis back.  `use_stored_timescale_` is
  true from load until the user picks a shell zoom level;
  `MainWindow::set_zoom` passes that switch, so the zoom buttons keep
  working.
- Evidence: `tests/data/reference/w29-projecta-timescale.measures.json`
  (numbers only; the client file and capture stay git-ignored, rule 4).  A
  local render of a neutral PROJECT_A copy measures 23 px per 7-day gridline
  period = 3.2857 px/day, quarter rules 296/299/303 px over 90/91/92 days
  (3.2889/3.2857/3.2935), and the first quarter rule 101 px = 31 days after
  the chart's left edge, i.e. the axis starts on the stored 2025-12-01.  No
  image is committed (rule 4).

Gates for commit `a790a24` (parent `4fee013`), logs under `build/w29/`
(`gates-4fee013-W29-1.0a-*.log`):

| gate | result |
|---|---|
| core Release build (`mppwrite_roundtrip_outline`, tree configured from `50df4b2`, `CORE_WARNINGS_AS_ERRORS=ON`) | exit 0 |
| `mppwrite_roundtrip_outline` | 1081 checks, 0 failures, 108 expected failures, exit 0 |
| `check_fixtures.py` | fixtures OK (98 documented hashes, 78 tracked `.mpp`); paths OK (120 files); provenance OK (8 oracles) |
| client-name scan on HEAD / staged tree / name guard `4fee013..HEAD` / hex grep | 0 files, 0 hits / 0 files, 0 hits / exit 0 / no matches (exit 1, expected). CLOSE errata A3: the cited per-item guard and grep logs are empty; the G1 group log records `guard_names 4fee013..HEAD` exit 0 and zero named commit messages. |

Gates for commit `fed21fb` (parent `a790a24`, fixtures only), logs under
`build/w29/` (`gates-a790a24-W29-1.0b-*.log`):

| gate | result |
|---|---|
| `check_fixtures.py` (staged) | fixtures OK (98 documented hashes, 101 tracked `.mpp`); paths OK (143 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on the staged tree / HEAD | 0 files, 0 hits / 0 files, 0 hits |

Gates for commit `2b9862e` (parent `fed21fb`), logs under `build/w29/`
(`gates-fed21fb-W29-1.0b-*.log`):

| gate | result |
|---|---|
| core Release build | exit 0 |
| `mppwrite_roundtrip_outline` | 1749 checks, 0 failures, 7 expected failures, exit 0 |
| app Release build | exit 0 |
| `app_edits_actuals_tests` / `app_edits_outline_tests` | 3723 checks passed, 50 expected failures / 3138 checks passed, exit 0 |
| client-name scan | 156 corpus names; `--cached` 0 files, 0 hits |
| `check_fixtures.py` | fixtures OK (98 documented hashes, 101 tracked `.mpp`, 143 tracked test-data files, no local path, 8 scenario oracles) |

Gates for commit `7b01d37` (parent `2b9862e`), logs under `build/w29/`
(`gates-2b9862e-W29-1.1-*.log`):

| gate | result |
|---|---|
| app Release build | exit 0 |
| `views_tests` | 219 checks passed, exit 0 |
| app_qt Release build / ctest | exit 0 / 24/24 passed, 0 failed, 24.30 s |
| `qt_stored_table_corpus` | 8 stored data columns + stub at 353/48/98/108/38/68/68/88/103, Indicators hidden, offsets 0..781 at 1920 |
| client-name scan | 156 corpus names; HEAD 0 files/0 hits; `--cached` 0 files/0 hits |
| `check_fixtures.py` | fixtures OK (98 documented hashes, 101 tracked `.mpp`, 143 tracked test-data files, no local path, 8 scenario oracles) |

Gates for commit `1a743b6` (parent `7b01d37`), logs under `build/w29/`
(`gates-7b01d37-W29-1.2-*.log`):

| gate | result |
|---|---|
| core Release build | exit 0 |
| `viewstate_tests` / `sheet_tests` | 1065 checks, 0 failures / 1052 checks passed, exit 0 |
| app Release build | exit 0 |
| `app_view_style_tests` | 70 checks, 0 failures (corpus: 213 entries, 10742/0x0b40000e = 255,242,0; project row grey; default 0,55,103; automatic rows no fill) |
| `views_tests` | 219 checks passed, exit 0 |
| app_qt Release build / ctest | exit 0 / 24/24 passed; `qt_stored_table_corpus`: all eight scrolling cells of ID 10 yellow, 6407 yellow / 18892 grey pixels (test output in `build/w29/w29-1.3-qt-corpus.log`, line 36; CLOSE errata B3) |
| client-name scan / client-string grep | 156 corpus names; HEAD 0 files/0 hits; `--cached` 0 files/0 hits; no matches |
| `check_fixtures.py` | fixtures OK (98 documented hashes, 101 tracked `.mpp`, 143 tracked test-data files, no local path, 8 scenario oracles) |

Gates for commit `aaf05fe` (parent `1a743b6`), logs under `build/w29/`
(`gates-1a743b6-W29-1.3-*.log`):

| gate | result |
|---|---|
| core Release build | exit 0 |
| `gantt_tests` | 766 checks passed, exit 0 (all five label sides, the bare stored bar, the fallback when nothing is stored) |
| app Release build / app tests | exit 0 / `app_layout`, `app_views`, `app_view_style` exit 0 |
| `app_view_style_tests` | 94 checks, 0 failures (stock auto/manual/milestone/summary; PROJECT_A's masks and field ids; first-match, unmapped id, unknown category) |
| app_qt Release build / ctest | exit 0 / 24/24 passed; `qt_stored_table_corpus` notes the four label classes and both 4 px gaps (test output in `build/w29/w29-1.3-qt-corpus.log`, line 37; CLOSE errata B3) |
| client-name scan / client-string grep | 156 corpus names; HEAD 0 files/0 hits; `--cached` 0 files/0 hits; no matches |
| `check_fixtures.py` | fixtures OK (98 documented hashes, 101 tracked `.mpp`, 143 tracked test-data files, no local path, 8 scenario oracles) |

Gates for commit `c79a45d` (parent `aaf05fe`), logs under `build/w29/`
(`gates-aaf05fe-W29-1.4-*.log`):

| gate | result |
|---|---|
| core Release build | exit 0 |
| `viewstate_tests` / `gantt_tests` | 1068 checks, 0 failures (the patched 06 01 record parses as 262) / 769 checks passed (a pinned origin keeps its day; bars before it at x = -100 and -60) |
| app Release build | exit 0 |
| `layout_model_tests` | 297 checks passed (origin decode, the 3.287 px/day mapping, the stock 24 px/day, unmeasured tiers) |
| `views_tests` / `view_style_tests` | 219 checks passed / 94 checks, 0 failures |
| app_qt Release build / ctest | exit 0 / 24/24 tests passed |
| PROJECT_A render geometry (local, no capture committed) | 23 px per 7 days = 3.2857 px/day; quarter rules 3.2889/3.2857/3.2935; first rule 101 px = 31 days after the left edge, origin 2025-12-01 |
| client-name scan / client-string grep | 156 corpus names; HEAD 0 files/0 hits; `--cached` 0 files/0 hits; no matches, HEAD and index |
| `check_fixtures.py` | fixtures OK (98 documented hashes, 101 tracked `.mpp`, 144 tracked test-data files, no local path, 8 scenario oracles) |

G1 group gates (fresh trees configured from scratch on `830b5b6` under
`build/w29-g1-fresh{,-app,-qt}`, built serially, log
`build/w29/g1-fresh-gates.log`; `830b5b6` is the G1 record, docs-only over
the code tree `c79a45d`):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 checks / 420 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app ctest / app_qt ctest | 20/20 / 24/24 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (98 documented hashes, 101 tracked `.mpp`); paths OK (144 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `4fee013..HEAD` / `guard_names` `4fee013..HEAD` / hex grep | 0 files, 0 hits / 0 commits / exit 0 / no matches (exit 1, expected) |

The numbers belong to `830b5b6`; the marker commit that follows is docs-only.

WAVE-29-G1-COMPLETE

## G2 - Format dialogs (decision 14)

| item | finding | status | evidence |
|---|---|---|---|
| W29-2.1 | Text Styles, Gridlines and Bar Styles dialogs | **DONE** | commit `190e1ce`; gates `build/w29/gates-6a36a43-W29-2.1-*.log` |

W29-2.1 detail:

- The layout evidence is the local Project 2021 captures of Text Styles
  (408x352), Gridlines (472x188) and Bar Styles (747x536), recorded as
  numbers only in `tests/data/reference/w29-format-dialogs.measures.json`:
  every control's client-relative rectangle and the captured list counts
  (font styles 4, sizes 16, background patterns 14 Hollow..Dark Dither,
  14 gridline kinds and 5 line types, 41 stock bar-style rows, bar-style
  columns Task 160 / Row 90 / Type 255 / Start 45 / Finish 85 / Text 100,
  five side rows, 11 bar-text fields, start/end shapes 25 each, middle
  shape 8, middle pattern 12, three types).  The captures stay git-ignored
  (rule 4).
- Text Styles: the style record `0x22400008` carries 19 text-style records,
  not MPXJ's 17; the captures' list is 19 items, Project 2021's stock
  defaults fill slots 17/18 as Changed Cells (pale blue, RGB 239,243,255,
  solid) and Inactive Tasks (grey, RGB 192,192,192, strikethrough), and a
  controlled all-19 save changed all 19 records.  The decoder exposes both,
  the item combo is the captured order, and "All" edits every record.  Font
  family, style, size, colour and the four effect bits are live; an
  undecoded record disables the dialog and OK with the decision-9 tooltip
  and an inline note.
- Gridlines: the captured 14 line kinds, five line types and colour control
  are laid out and disabled together with OK and the decision-9 tooltip
  because wave 26 decoded no gridline engine; the inline note says so.
  The dialog never reports success.
- Bar Styles: the captured 41-row stock table with Row showing the stored
  row + 1 (every stock style stores row 0, i.e. display row 1), the Text
  and Bars tabs, the five captured side rows and 11 bar-text fields.  The
  fill, start, middle and end colours and the From/To fields are live; the
  start/end shape lists, middle shape and pattern lists and the three type
  boxes are present with the captured contents and disabled with the
  decision-9 tooltip because appearance editing has no engine, as is Text
  Styles' background-pattern combo.  Cut/Paste/Insert/Help are present and
  disabled.
- The From/To decode settles W26's label mapping: the capture shows Task
  Start / Task Finish for `0x0b400503` / `0x0b400504`, so
  `label_text_for` swaps its 503/504 arms; a stock Milestone still prints
  `0x0b400504`, now Task Finish, and the tests follow the capture.
- OK applies to the open view only: `PlanDocument::set_session_gantt_style`
  assigns the style in place (the sheet model's and canvas's pointers stay
  valid), rebuilds the Gantt and re-derives the stored bar labels;
  `MainWindow::apply_session_gantt_style` repaints through
  `after_model_change()`.  The Gantt canvas paints each bar-text side from
  its own Text Styles record (family, size, bold/italic/underline/
  strikethrough, foreground, and a solid background when the stored pattern
  is not Hollow).
- A changed style is a write path with no Project pair in this port, so
  every dialog says Save keeps the file's bytes and nothing is written;
  `document_dirty` stays false.  The ribbon's Text Styles, Gridlines and
  Bar Styles Format split are enabled and wired, and the generated
  `docs/ui-command-backlog.md` records the three commands as implemented
  with their remaining gaps (decision 15).
- Evidence: `app_qt/tests/format_dialog_gate_test.cpp` parses the measures
  JSON, measures the live widget tree (sizes, titles, every control
  rectangle, list counts and order), drives the functional paths (All
  writes 19 records, a single-item edit leaves Inactive alone, Task row
  edits, red start colour, OK applies and repaints both the sheet's
  FontRole and the bar labels) and proves the disabled paths stay disabled
  with the exact tooltip.  The gate passed offscreen in 102.14 s.
- The privacy scans and `check_fixtures.py` for this item ran with GIT_DIR
  pointing at a byte-identical local copy of `.git` because the Dropbox
  placeholders cost ~44 s per git process (156 names per scan would be
  ~1.9 h); the mirror was refreshed and its HEAD and index hash verified
  before every scan
  (`build/w29/gates-6a36a43-W29-2.1-scan-method.txt`).

Gates for commit `190e1ce` (parent `6a36a43`), logs under `build/w29/`
(`gates-6a36a43-W29-2.1-*.log`):

| gate | result |
|---|---|
| core Release build | exit 0 |
| `viewstate_tests` | 1086 checks, 0 failures (19 records; Changed Cells and Inactive defaults; Task/Milestone From/To) |
| app Release build | exit 0 |
| `layout_model_tests` / `views_tests` / `view_style_tests` | 297 checks passed / 219 checks passed / 94 checks, 0 failures |
| app_qt Release build / ctest | exit 0 / 25/25 tests passed (2866.25 s); `qt_format_dialog_gate` passed in 102.14 s |
| client-name scan / staged scan | 156 corpus names; HEAD 0 files, 0 hits; `--cached` 0 files, 0 hits |
| client-string grep / `git diff --check` | no matches, HEAD and index (exit 1, expected) / exit 0 |
| `check_fixtures.py` | fixtures OK (98 documented hashes, 101 tracked `.mpp`); paths OK (145 tracked test-data files, no local path); provenance OK (8 oracles) |

G2 group gates (fresh trees configured from scratch on `190e1ce` under
`build/w29-g2-fresh{,-app,-qt}`, log `build/w29/g2-fresh-gates.log`;
`190e1ce` is the W29-2.1 commit, and the record commit `afc7606` plus this
marker commit are docs-only over it):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 checks / 420 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app ctest / app_qt ctest | 20/20 / 25/25 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (98 documented hashes, 101 tracked `.mpp`); paths OK (145 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `6a36a43..HEAD` / `guard_names` `6a36a43..HEAD` / hex grep | 0 files, 0 hits / 0 commits / exit 0 / no matches (exit 1, expected) |

The 2 h background limit stopped the first group-gate run during the app_qt
build; the build, the app_qt ctest, the fixtures and the privacy stages
were resumed against the same fresh trees and the same log (resume notes in
`build/w29/g2-fresh-gates.log`).

The numbers belong to `190e1ce`; the record and marker commits that follow
it are docs-only.

WAVE-29-G2-COMPLETE

## G3 - Page Setup

| item | finding | status | evidence |
|---|---|---|---|
| W29-3.1 | Page Setup, wired to the shared print layout | **DONE** | commit `7fd6851`; gates `build/w29/gates-624903d-W29-3.1-*.log` |

W29-3.1 detail:

- The layout evidence is the local Project 2021 capture of Page Setup -
  Gantt Chart (476x362), recorded as numbers only in
  `tests/data/reference/w29-page-setup.measures.json`: the title, the six
  tabs, the five action buttons at y = 332 (Preview 11,332 93x23, Options
  113,332 75x23, Print 197,332 75x23, OK 311,332 75x23, Cancel 395,332
  75x23) and the 22 Page-tab control rectangles.  The captured tab control
  is owner-drawn and reads empty, so the file records the tab count, not
  the labels or band; the stack sits at the captured content origin
  (0,24).  The capture stays git-ignored (rule 4).
- The dialog is modal and fixed at 476x362 with six tabs (Page, Margins,
  Header, Footer, Legend, View).  Page edits orientation, paper (A4 or
  Letter), scaling (10..400%) and the first page number; Margins edits the
  four margins in points; Header and Footer each switch the band, edit its
  text codes and alignment; Legend switches the band and edits its title.
  The Page-tab controls are placed at the captured rectangles and marked
  with a `measuredControlId` property, so the gate measures the live tree
  against the file.  Options, the Fit-to radio and its spinners, and the
  View tab's print-notes and blank-pages boxes stay present and disabled
  with the exact decision-9 tooltip `not implemented in this port`; the
  View tab carries an inline note.  OK applies, Cancel leaves the caller's
  spec untouched, Preview and Print apply and report which button was used.
- `PageSpec` now carries four margins, `scale_percent`,
  `show_header`/`show_footer`/`show_legend`, the header/footer/legend texts
  and aligns and `first_page_number`.  The physical paper and the layout
  page are separate: `layout_spec()` returns a virtual page whose content
  box is the paper's content box divided by the scale, `build_print_job`
  lays the report out on that virtual page, and the painters translate to
  the paper margin, scale about the content origin and translate back, so
  the paper margins stay put at any scale.  The header, footer and legend
  bands reserve their captured heights only when switched on, and the
  automatic footer honours the first page number, so a first page 5 prints
  `Page 5 of N`.
- `expand_print_text` expands `&d` (today), `&p` (page), `&P` (last page)
  and `&&` (a literal ampersand); an unknown `&x` is kept verbatim.  A
  custom header/footer is expanded per page against the job's first/last
  page; an automatic footer keeps `print_footer_text`.  The macOS AppKit
  painter (`app/main.mm`) uses the same layout arithmetic and conditional
  bands as the Qt painter; this Windows machine builds the headless app
  tree and app_qt only.
- `MainWindow` owns the live spec (`print_spec_`).  The Print page's
  orientation and paper controls are the two fields it shares with Page
  Setup: `print_spec(landscape, letter)` overlays them on the stored spec,
  the preview provider renders `current_print_job(print_spec(...))` and
  Print uses the same call.  The dialog's OK stores the returned spec and
  calls `Backstage::set_print_settings`, which blocks the two controls'
  signals and refreshes the preview once; the preview refreshes after the
  Print page is shown, so its label size is final.  The backstage gate
  proves the button is live, has no decision-9 tooltip and calls its
  handler once.
- Evidence: `app_qt/tests/page_setup_gate_test.cpp` parses the measures
  JSON, checks the live dialog's title, fixed size, six tabs, the 22 marked
  controls and their rectangles exactly, and edits every field (landscape,
  Letter, 50%, first page 5, margins 48/36/40/44, header `&d plan &p/&P &&`
  aligned centre, footer `&d page &p` aligned left, legend `Key`), then
  checks OK/Cancel/Print/Preview.  The shell path opens the fixture in a
  `MainWindow`, shows the backstage Print page, drives the modal dialog to
  OK and proves the applied spec yields the virtual 1140x1500 page with
  body_top 83, footer_rule 1440, legend_top 1410, body_bottom 1406 and 99
  Gantt rows, expands the header/footer per page for first page 5, and
  renders a preview that matches `render_print_page` of the applied job
  pixel-for-pixel and differs from the default job's image.  The gate
  passed in 91.20 s under ctest in the incremental tree and 1.61 s in the
  fresh tree; the timings follow file hydration on this machine, and the
  same checks ran in both.
- The Qt build's first attempt failed at `app_qt/backstage.h(133)` with
  C2440 (`QPushButton*` to `QAbstractButton*` in the inline accessor); the
  header now stores `print_page_setup_` as `QAbstractButton*` and the
  rebuild reached `qt_build=0`.  The log keeps both attempts.
- The privacy scans and `check_fixtures.py` for this item ran with GIT_DIR
  pointing at a byte-identical local copy of `.git` because the Dropbox
  placeholders cost ~44 s per git process (156 names per scan would be
  ~1.9 h); the mirror was refreshed and its HEAD and index hash verified
  before every scan
  (`build/w29/gates-624903d-W29-3.1-scan-method.txt`).

Gates for commit `7fd6851` (parent `624903d`), logs under `build/w29/`
(`gates-624903d-W29-3.1-*.log`):

| gate | result |
|---|---|
| app Release build | exit 0 |
| `page_layout_tests` (`app_print`) | 109 checks passed |
| `export_tests` (`app_export`) | 65 checks passed |
| `layout_model_tests` / `views_tests` / `view_style_tests` | 297 checks passed / 219 checks passed / 94 checks, 0 failures |
| app ctest (`app_print`, `app_export`, `app_layout`, `app_views`, `app_view_style`) | 5/5 tests passed (46.47 s) |
| app_qt Release build / ctest | exit 0 / 26/26 tests passed (2903.24 s); `qt_page_setup_gate` passed in 91.20 s |
| client-name scan / staged scan | 156 corpus names; HEAD 0 files, 0 hits; `--cached` 0 files, 0 hits |
| client-string grep / `git diff --check` | no matches, HEAD and index (exit 1, expected) / exit 0 |
| `check_fixtures.py` | fixtures OK (98 documented hashes, 101 tracked `.mpp`); paths OK (146 tracked test-data files, no local path); provenance OK (8 oracles) |

G3 group gates (fresh trees configured from scratch on `7fd6851` under
`build/w29-g3-fresh{,-app,-qt}`, log `build/w29/g3-fresh-gates.log`;
`7fd6851` is the W29-3.1 commit, and the record and marker commits that
follow it are docs-only over it):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 checks / 420 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app ctest / app_qt ctest | 20/20 / 26/26 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (98 documented hashes, 101 tracked `.mpp`); paths OK (146 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `624903d..HEAD` / `guard_names` `624903d..HEAD` / hex grep | 0 files, 0 hits / 0 commits / exit 0 / no matches (exit 1, expected). These values are from the corrected re-run `build/w29/g3-final-privacy.log`; `g3-fresh-gates.log` carries the unquoted-range bug described in the resume notes below. |

Resume notes for the group run:

- The first fresh app build exited 1 on the environment's transient
  MSB6003 tlog file locks; the app ctest that followed still passed 19 of
  20 tests, with `app_project_dialogs` Not Run because its exe was the one
  not produced.  The same fresh tree was rebuilt to completion
  (`app build (resume) exit: 0`).  The first link of
  `project_dialogs_tests.exe` (SHA256 A4341A24...) was then refused by this
  machine's Application Control policy (CodeIntegrity events 3077/3033,
  policy `{0283ac0f-fff1-49ae-ada1-8a933130cad6}`); relinking produced
  SHA256 9ACC5100..., which runs (`app_project_dialogs: 70 checks,
  0 failure(s)`), and the app ctest then passed 20/20.  The log records
  each resume.
- A transient lock on `build/w29/g3-fresh-gates.log` lost the first lines
  of the part A log (the mirror note and the core configure output); the
  exit codes and every later stage are logged, and part B re-verified the
  same tree's mirror (HEAD `7fd6851`, index SHA256, both match).

The numbers belong to `7fd6851`; the record and marker commits that follow
it are docs-only.

WAVE-29-G3-COMPLETE

## G4 - Visual Reports, Compare Projects

| item | finding | status | evidence |
|---|---|---|---|
| W29-4.1 | Both dialogs laid out from captures; primary actions disabled with the exact decision-9 tooltip | **DONE** | commit `56f0ffa`; gates `build/w29/gates-4ee0cf3-W29-4.1-*.log` |

W29-4.1 detail:

- The layout evidence is the local Project 2021 capture of the two dialogs,
  opened through UI Automation on the Report tab's own ribbon buttons from a
  read-only working copy of `tests/data/synthetic-base.mpp` (tag
  `g4-dialogs`).  The captures stay git-ignored (rule 4); the numbers and UI
  strings are committed in
  `tests/data/reference/w29-report-dialogs.measures.json`.  Visual Reports -
  Create Report measures 625x541 and Compare Project Versions 469x251; every
  captured control is stored as [x, y, width, height] client-relative with
  its caption.  The control tables were verified against the local capture
  before the file was committed: every id and rectangle matches, and the
  only substitution is Compare Projects' `%1` in place of the opened copy's
  file name.
- Visual Reports carries the capture's 22 visible controls: the tab control
  (one captured control whose seven owner-drawn items are drawn on the two
  rows the capture shows, three summary tabs over All and the three usage
  tabs, with All selected), the ten template names in the captured order
  with the first selected, Microsoft Excel and Microsoft Visio checked, the
  "Show report templates created in:" label, the New/Edit/Manage Template
  buttons, the Sample group with its sunken frame and the capture's
  "No sample available." alternate text, the usage-level label and combo
  (5 items, current index 3, "Weeks"), "Include report templates from:"
  unchecked with its edit and Modify, the separator at y = 484, and the
  Help / Save Data / View / Close row.  The captured blank status static
  (id 11) carries the inline note.  Compare Projects carries its 11 visible
  controls: the two groups (the first with the captured caption pattern and
  the opened file's name), the empty previous-version combo, Browse, the
  wrapped description, the Task Table and Resource Table labels and combos
  (22/10 and 12/3, both showing "Entry"), OK and Cancel.  The captured
  hidden "Previous version" static is not painted.
- Both dialogs are fixed at the captured client sizes and set Project's 9 pt
  dialog face before building their children.  The shell's 11 pt face does
  not fit the capture: measured with GDI against Carlito, the 439x26
  description would need three lines, the 457-wide compare caption would
  overflow with any name, and several 13-px label rows would clip; at 9 pt
  every captured box fits (the description takes two lines, the caption
  fits 441 px, and "Task Table:" measures 54 px against the captured 53).
- There is no report engine and no comparison engine, so operator decision
  15 applies: every current-affecting control is present, disabled and
  carries decision 9's exact tooltip `not implemented in this port`; the
  dismiss buttons (Close, Cancel) are live and the shell reports "nothing
  was changed" after dismissal; and both gaps are named in
  `docs/ui-command-backlog.md`.  Neither dialog can fake success:
  OK/View/Save Data/the template commands/the combos and Browse are all
  inert, and each dialog carries an inline note where the capture leaves
  room.
- Two captured limitations are recorded in the measures file rather than
  guessed: the tab control and the three combos are owner-drawn, so
  `CB_GETLBTEXT` returns `CB_ERR` and their unseen item strings are not
  recorded (the port keeps the measured count, current index and visible
  text, with empty strings elsewhere); and four Visual Reports controls plus
  one Compare Projects control are captured but not painted (invisible
  accessibility containers or hidden statics).
- Two knowing deviations, recorded with the item.  Project grows the Compare
  Project Versions window with a long project name; the port keeps the
  measured 469 px and elides the middle of the name in the captured caption
  (a short fixture name such as `synthetic-base.mpp` fits unchanged).  And
  the captured sample pane shows a chart the port cannot draw, so the port
  shows the capture's own "No sample available." state instead.
- Evidence: `app_qt/tests/report_dialog_gate_test.cpp` parses the measures
  JSON, finds every captured control by its `measuredControlId` property,
  compares the LIVE rectangle and caption, requires the marked set to equal
  the captured set and every not-painted id to have no widget, checks both
  fixed sizes and titles, the two-row tab labels and selected item, the ten
  template names and selection, each combo's count/current/visible text, the
  three check-box states, the exact tooltips on every current-affecting
  control, and that Close/Cancel reject and hide the dialog.  The shell path
  opens the fixture in a `MainWindow`, activates the Report tab and clicks
  the ribbon's own Compare Projects and Visual Reports buttons; a modal
  inspector proves the Compare dialog carries `synthetic-base.mpp` in its
  captured caption, both dialogs open with OK/View disabled and the exact
  tooltip, and each dismisses.
- The Report tab's two buttons are now enabled (`RibbonCommand::
  compare_projects`, `visual_reports`); `ribbon_pin.tsv` marks both enabled,
  the ribbon inventory pins each button to its own command, and the
  generated backlog now lists 95 ribbon rows with 77 disabled (was 79), both
  new rows `implemented` with their named gap.
- The first gate build failed twice on the test source (a `QString::c_str`
  call and `qobject_cast` on the Q_OBJECT-free dialog classes); the test now
  builds the message with `std::string` and uses `dynamic_cast`, and the log
  keeps both attempts.  All four new text files are LF in the index and the
  worktree (the measures JSON is pinned `text eol=lf` by `.gitattributes`).

Gates for commit `56f0ffa` (parent `4ee0cf3`), logs under `build/w29/`
(`gates-4ee0cf3-W29-4.1-*.log`):

| gate | result |
|---|---|
| app_qt Release build (`qt_shell`, `qt_report_dialog_gate_tests`, `qt_ribbon_inventory_tests`, `msproject_qt`) | exit 0 |
| `qt_report_dialog_gate` | passed, 0 failures (1.43 s standalone; 0.27 s in the full ctest) |
| `qt_ribbon_inventory` | passed |
| app_qt ctest | 27/27 passed (13.28 s) |
| client-name scan on HEAD / on the staged tree | 156 corpus names; 0 files, 0 hits / 0 files, 0 hits |
| client-string grep / `git diff --check` | no matches (exit 1, expected) / clean (`build/w29/CLOSE-citation-checks.log`) |
| `check_fixtures.py` | fixtures OK (98 documented hashes, 101 tracked `.mpp`); paths OK (147 tracked test-data files, no local path); provenance OK (8 oracles) |
| `guard_names` `4ee0cf3..56f0ffa` | exit 0 |

CLOSE errata A2: only the build and `qt_report_dialog_gate` rows above have
their own item logs (`...-appqt.log`, `...-report-gate.log`); the staged-tree
scan, `git diff --check`, fixture (147) and guard rows are evidenced in
`build/w29/g4-fresh-gates.log` (fixtures/paths at lines 1385-1386; the
corrected guard and message re-run at lines 1410-1418).  There is no separate
guard log for `4ee0cf3..56f0ffa`; the group log is the evidence.

G4 group gates (fresh trees configured from scratch on the worktree at
`4448df3` under `build/w29-g4-fresh{,-app,-qt}`, log
`build/w29/g4-fresh-gates.log`; the G4 code is `56f0ffa`, and `4448df3` adds
only this record over it):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 checks / 420 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app ctest / app_qt ctest | 20/20 / 27/27 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (98 documented hashes, 101 tracked `.mpp`); paths OK (147 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `4ee0cf3..HEAD` / `guard_names` `4ee0cf3..HEAD` / hex grep | 0 files, 0 hits / 0 commits / exit 0 / no matches (exit 1, expected) |

Resume notes for the group run:

- The core and app_qt builds each hit this environment's transient MSB6003
  file locks (core once, app_qt twice); the script's retry rebuilt the same
  fresh tree and every build ended exit 0, so no tree was reconfigured.
- The first privacy invocation expanded `$base..HEAD` through PowerShell's
  range operator, so the message scan ranged from `4ee0cf3` backwards and
  reported one old wave-23 commit (not a G4 commit) while `guard_names` got
  the wrong arguments.  The corrected re-run with a quoted range is appended
  to the same log: messages `4ee0cf3..HEAD` 0 commits quote names,
  `guard_names` exit 0, hex grep no matches; the scan on HEAD was 0 files,
  0 hits both times.  The G3 script, which this run copied, carries the same
  unquoted range; the numbers recorded here are the corrected ones.
- The part B script's final native command is the expected-failure hex grep
  (exit 1), so the script process exits 1 after its last stage even though
  every gate printed exit 0; the log is the record.

The numbers belong to `56f0ffa`; the record and marker commits that follow
it are docs-only.

WAVE-29-G4-COMPLETE

## G5 - Dark chrome header bands and the shell artwork

| item | finding | status | evidence |
|---|---|---|---|
| W29-5.1 | Dark-theme sheet/chart header bands and ribbon ink measured against Project's Dark Gray and Black themes | **DONE** | commit `13e0235`; gates `build/w29/gates-12b4cf1-W29-5.1-*.log`; measurement `build/w29/g5-w29-5-1-measure.log` |
| W29-5.2 | Menu bar, QAT and status-bar artwork in the wave-26 icon style | **DONE** | commits `f09f114`, `7ea220c`; gates `build/w29/gates-13e0235-W29-5.2-*.log`, `build/w29/gates-7ea220c-W29-5.2-window-ink-*.log` |

W29-5.1 detail:

- Operator decision 10 and operator Q14: in Dark Gray and Black only the
  CHROME darkens; the sheet's cells and the chart's body keep the file's
  colours.  Wave 26 left two header gaps; this item closes both from the local
  Project 2021 audit captures (874x491, git-ignored:
  `corpus/reference/2026-10-23-audit/mcp-computer-use-blob-1792744264666-4mp1fn.jpg`
  = Dark Gray, `...-ekrn72.jpg` = Black).  The frames, crops and sampling runs
  stay in the working tree (`build/w29/g5-w29-5-1-measure.log`,
  `build/w29/g5-crops/`); only numbers are committed, in
  `tests/data/reference/w29-dark-header.measures.json` (rule 4).
- Measured fills: the sheet's caption band is Dark Gray `#262626` / Black
  `#646464`; the frozen row-number gutter, the corner above it and the chart
  timescale band are the SWAPPED pair, Dark Gray `#666666` / Black `#262626`.
  The chart's ordinary cell rules are `#5F5F5F` (DG) / `#2D2D2D` (Black) and
  its Monday/week vertical rules the strong `#545454` / `#323232`; the
  horizontal tier boundary samples match the WEAK rule (`chart_tier_rule`
  `#5D5D5D` DG / `#2D2D2D` Black), so W29-C4 paints that boundary with
  `chart_header_rule` in both dark themes (light keeps the wave-26 strong
  line; no light tier measurement exists).  Dark Gray's rules are
  DARKER than its band and Black's are LIGHTER, so the pair is not one "dark
  rule" value.  The chart's label ink does NOT follow the theme: the cores are
  the file's `#333333` in both dark themes, so Black's labels stay
  low-contrast exactly as Project's do.  No weekend tint is distinguishable in
  either dark capture, so the dark weekend fill equals the band fill.
- Caption and row-number inks: the JPEG peaks are dimmer than the ink; the
  direction is measured (light on dark) and the values are the chrome's dark
  text colour `#F2F2F2`, as `capture_samples.derived` records.  The light
  values are the exact constants the wave-17/21 painters already used
  (`shell_style.h`), so the light render cannot drift; the gate pins all 15
  fields against the JSON.
- Ribbon ink (decision 3, our one-ink art): Dark Gray's measured group row is
  light (`#BDBDBD`), so the wave-26 ink `#1F3A5F` stays (6.1:1); Black's
  `#292929` gets `#E6E6E6` (11.6:1).  `shell_ribbon_icon` now takes the ink,
  `RibbonBar::set_theme` re-renders the active page's icon-bearing controls,
  and later-built pages use the live theme's ink.
- Code: `ChromePalette` grew 15 fields (`theme.h`/`theme.cpp`); the sheet's
  caption band, row gutter and corner paint from it (`sheet_views.cpp/h`,
  with `set_theme`); the Gantt timescale band does too
  (`gantt_canvas.cpp/h`); `main_window.cpp` passes the theme to both.  The
  sheet's viewport and the chart's body are deliberately untouched.
- One measured geometry fix: the timescale's day and tier rules now end at
  `top + tier_height - 1`.  They previously painted one pixel into the chart
  body's top grid line, which was invisible in light but drew a dark tick into
  the body under the dark themes.  The change moves light pixels only along
  that 43-position boundary and matches Project's band edge.
- Evidence: `qt_theme_gate` now pins, per theme, the compiled palette against
  the committed JSON; the live caption band, gutter, corner, chart tiers and
  label inks against the measured values; caption/row-number ink presence and
  at least 4.5:1 contrast on the measured fill; the live ribbon QIcon against
  the theme-ink factory render pixel for pixel and its monochrome ink; a band
  repaint on a theme change; and the sheet viewport plus the chart body below
  42 px pixel-identical across the three themes (tolerance 0).  It writes
  band/chart PNGs to its out-dir for review.  The wave-26 light/dark chart
  equality assertion is narrowed to the body, because the band is now chrome.

Gates for commit `13e0235` (parent `12b4cf1`), logs under `build/w29/`
(`gates-12b4cf1-W29-5.1-*.log`, builds `g5-w29-5-1-build*.log`, theme-gate
`g5-w29-5-1-theme-gate.log`):

| gate | result |
|---|---|
| app_qt Release build | exit 0 on the retry (the first pass hit one transient MSB6003 tlog lock) |
| `qt_theme_gate` | passed, 0 failures |
| `qt_icon_gate` / `qt_chrome_gate` | passed, 0 failures / passed, 0 failures |
| app_qt ctest | 27/27 passed |
| client-name scan on HEAD and the staged tree `43ca0ff` / client-string grep | 0 files, 0 hits / 0 files, 0 hits / no matches (exit 1, expected) |
| `check_fixtures.py` | fixtures OK (98 documented hashes, 101 tracked `.mpp`); paths OK; provenance OK (8 oracles) |

W29-5.2 detail:

- This item is a RESTYLE, not a new measurement: decision 3 makes the chrome
  artwork the port's own, so there is no bitmap to sample.  The ink comes from
  the palette: `ChromePalette::ribbon_ink` on the ribbon's group row and (from
  `7ea220c`, see the defect below) `ChromePalette::window_ink` on the window-
  coloured menu/QAT/status surfaces.  `app_qt/shell_icons.cpp` is
  re-authored on the wave-26 grid -- a 32-px drawing scaled to 16/32,
  round-cap/round-join strokes and ONE ink per render -- with the same helper
  idiom as `ribbon_glyphs.cpp`.  `shell_action_icon()` and
  `shell_view_icon()` take the ink; an invalid colour falls back to `#1F3A5F`.
  The five status view glyphs (Gantt Chart, Task Usage, Team Planner, Resource
  Sheet, Report) each carry a distinct mark inside the shared page frame.
- Glyph coverage is unchanged from the committed set: no menu command gained
  or lost its drawing in this item, and commands without bespoke art keep the
  neutral tile (which is itself one-ink).  The Undo/Redo pair is a straight
  return arrow, chosen after a curved hook collapsed into a tick at 16 px.
- Wiring: `build_menus()` draws each menu action in
  `chrome_palette(theme_).window_ink`; the QAT reuses those same QActions, so
  it follows automatically.  `build_status_bar()` draws the five view glyphs
  in the same ink.  The new `MainWindow::apply_icon_ink()` runs from
  `set_theme()` and re-draws `actions_` and `view_buttons_`, so a live theme
  change re-inks the menu bar, QAT and status bar.
- One defect found after the first gate run and fixed in `7ea220c`: the first
  cut inked these surfaces with `ribbon_ink`, which is correct for the ribbon
  but wrong for them.  In Dark Gray the ribbon's group row is the light
  (189,189,189), while the QAT is the dark window `#52575A` (82,87,90) and the
  status bar `#545454` (84,84,84); `#1F3A5F` there measures only 1.5:1, so the
  glyphs were effectively invisible.  `ChromePalette` grew a sixteenth field,
  `window_ink`: light keeps `#1F3A5F`, Dark Gray and Black use `#E6E6E6`
  (5.9:1 on the QAT, 6.1:1 on the status bar).  The gate now also asserts
  contrast of the window ink against both measured backgrounds at >= 4.5:1 in
  every theme, so this is pinned rather than eyeballed.
- Evidence: `qt_icon_gate` gained the chrome half.  Per theme (light, Dark
  Gray, Black) it proves the menu bar exposes at least 20 command actions and
  still carries Save; every menu command's live 16-px icon is pixel-identical
  to `shell_action_icon(id, 16, ink)`, visibly inked and monochrome (0 off-ink
  pixels); the QAT's three actions hold the same themed glyphs; all five
  status buttons hold `shell_view_icon(index, 16, ink)`, visibly inked and
  monochrome; the window ink reaches at least 4.5:1 on the quick-access and
  status backgrounds; and light and Black use different inks, so the per-theme
  equality also proves the live re-ink on a theme change.  The gate writes
  `chrome-icons.png` in its out-dir (`build/w26/`) -- every menu command and
  status glyph at 32 and 16 px for human review.

Gates for commit `f09f114` (parent `13e0235`), logs under `build/w29/`
(`gates-13e0235-W29-5.2-*.log`, build/gate record `g5-w29-5-2.log`):

| gate | result |
|---|---|
| app_qt Release build | exit 0 (two passes hit transient MSB6003/LNK1104 file locks; straight retries linked every target, no code change) |
| `qt_icon_gate` (with the W29-5.2 chrome section) | passed, 0 failures |
| `qt_chrome_gate` / `qt_theme_gate` | passed, 0 failures / passed, 0 failures |
| app_qt ctest | 27/27 passed |
| client-name scan on HEAD and the staged tree `bdc5430d` / client-string grep | 0 files, 0 hits / 0 files, 0 hits / no matches (exit 1, expected) |
| `check_fixtures.py` | fixtures OK (98 documented hashes, 101 tracked `.mpp`); paths OK (148 tracked test-data files, no local path); provenance OK (8 oracles) |

Gates for commit `7ea220c` (parent `10ad181`, the W29-5.2 window-ink follow-up),
logs under `build/w29/` (`gates-7ea220c-W29-5.2-window-ink-*.log`):

| gate | result |
|---|---|
| app_qt Release build | exit 0 |
| `qt_icon_gate` (with the new contrast assertions) / `qt_chrome_gate` / `qt_theme_gate` | passed, 0 failures / passed, 0 failures / passed, 0 failures |
| app_qt ctest | 27/27 passed |
| client-name scan on HEAD tree `8ac04bb` / client-string grep | 0 files, 0 hits / no matches (exit 1, expected) |
| `guard_names` `10ad181..7ea220c` / `git diff --check` | exit 0 / clean (`build/w29/CLOSE-citation-checks.log`) |
| `check_fixtures.py` | fixtures OK (98 documented hashes, 101 tracked `.mpp`); paths OK (148 tracked test-data files, no local path); provenance OK (8 oracles) |

G5 group gates (fresh trees configured from scratch on the worktree at
`d70e97e` under `build/w29-g5-fresh{,-app,-qt}`, log
`build/w29/g5-fresh-gates.log`; the G5 code is `7ea220c`, and `10ad181` /
`d70e97e` add only the two record commits over it):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 checks / 420 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app ctest / app_qt ctest | 20/20 / 27/27 passed (including `qt_icon_gate`, `qt_chrome_gate`, `qt_theme_gate`) |
| `check_fixtures.py` / oracle provenance | fixtures OK (98 documented hashes, 101 tracked `.mpp`); paths OK (148 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `12b4cf1..HEAD` / `guard_names` `12b4cf1..HEAD` / hex grep | 0 files, 0 hits / 0 commits / exit 0 / no matches (exit 1, expected) |

Resume notes for the group run:

- The fresh builds hit this environment's transient MSB6003/LNK1104 file
  locks three times (`g5-fresh-gates.log` lines 291, 848, 959, 1348): the
  core `fuzz_writer`/`fuzz_parsers` stage (retry 1), the app
  `core_sched`/`document_tests` stage (retries 1-2) and the app_qt build
  (retry 1).  Every retry rebuilt the same tree, no code changed, and each
  build ended exit 0.
- Part A's `Add-Content` log writes lost one block to the same file-lock
  problem ("Stream was not readable" / file in use, reported on the console):
  the `plan_recalc MPP_NO_CORPUS=1` output.  The gate itself exited 0 in
  sequence; its block was re-run on the same fresh binary immediately after
  part A and appended to the log, so the 266-check line is the re-run's and
  is labelled as such.
- The part B script's final native command is the expected-failure hex grep
  (exit 1); every gate printed exit 0 and the log is the record.

The numbers belong to `7ea220c`; the two record commits (`10ad181`,
`d70e97e`) and this marker are docs-only.

WAVE-29-G5-COMPLETE

## CLOSE - the wave record

The wave ran as six groups plus this close, one fresh session each.  Every item
in G0..G5 is DONE; nothing is deferred.  The group marker lines end each
section above.  The CLOSE work is the fresh-tree gate run, both privacy scans,
the review pass over `git diff wave28..wave29`, the push and the two workflow
dispatches, the START-HERE / INDEX / memory update and the re-scored 16
decisions.  The record below belongs to the final code tree
`7e14aaa` (the CLOSE code commits `02b720a`, `4b03ecb`, `f7434d2`, `31bb11b`,
`133cb3f` and `7e14aaa` are the last code changes; the CLOSE docs commits that
follow are docs-only).

### Fresh-tree gates (final code tree `7e14aaa`)

The first CLOSE gate run was on `7262148` and was green (its numbers are kept
as **`build/w29/gates-7262148-W29-CLOSE.log`**, sha256
`aad948c84af5eb62826ff0e201c270da1c227d306209b0876cf34eaf39b364ab`).  The
independent review then found two Highs whose fixes changed `app/` and
`app_qt/` (`02b720a`, `4b03ecb`), the first CI dispatch found a third High on
macOS (`f7434d2`, W29-C3) and a GCC-only test initializer (133cb3f, W29-C5),
the record audit found a measured-paint mismatch (`31bb11b`, W29-C4), and the
parity gate found the stock-timescale axis shift (`7e14aaa`, W29-C6), so the
whole fresh-tree set was re-run FROM SCRATCH on the final code tree
`7e14aaa`; the table below is that re-run.
An earlier part-A attempt on `31bb11b` is kept as
`build/w29/close-final-fresh-gates-attempt1.log`: it hit two transient
"Process not started" core tests and an MSB3061 file lock, so the re-run's
script retries those as well.
Three trees were configured FROM SCRATCH on 2026-10-26 and built serially:
`build/w29-close-fresh` (core, `-DCORE_WARNINGS_AS_ERRORS=ON`),
`build/w29-close-fresh-app` (headless app tests) and `build/w29-close-fresh-qt`
(Qt shell and its tests).  The numbers are in
**`build/w29/close-final-fresh-gates.log`**, filed for standing rule 3 as
**`build/w29/gates-7e14aaa-W29-CLOSE.log`** (sha256
`530176474571e04d62d7cdea46204fd721bb5f4100d38d7d8a1fc33a06d79c5c`).

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | **52/52** |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | **512 checks / 266 checks** |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | **53 checks, 0 failures** / 8 recorded, **7 pass, 1 expected fail**, 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | **1100 checks / 420 checks** |
| M2 `recalc --compare` | **332/332**, **268/268** and **64/64** fields match |
| app ctest / app_qt ctest | **20/20** / **27/27** |
| `check_fixtures.py` / oracle provenance | fixtures **OK** (98 documented hashes, 101 tracked `.mpp`); paths OK (148 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name line scan / messages / normalised guard / hex grep | HEAD **0 files / 0 hits**; messages `wave28..HEAD` **0 commits**; `guard_names` `wave28..HEAD` and `origin/wave28..HEAD` **exit 0**; hex grep **no matches** |

Resume notes:

- The final re-run needed no build or ctest retry.  The only retries in the
  log are the tree-removal retries at the start (the previous attempt's trees
  were still held: retries 1-2 on `build/w29-close-fresh` and
  `build/w29-close-fresh-app`).  The earlier part-A attempt on `31bb11b` hit
  two transient "Process not started" core tests and an MSB3061 file lock
  (`close-final-fresh-gates-attempt1.log`); the re-run script retries those,
  and the retry path was not needed here.
- Part B's final native command is the expected-failure hex grep (exit 1); the
  script then writes `FINISHED` and exits 0 because its last statement is a
  PowerShell cmdlet, so the log's `hex grep exit: 1 (1 = no matches,
  expected)` line is the record.
- The app_qt ctest finished in 39.92 s on the fresh tree (the same 27 tests
  that took 2866-2903 s in the incremental trees during G2/G3; the timings
  follow file hydration on this machine and the same checks ran).

### Privacy

Both scans ran on the final tree `7e14aaa` (the CLOSE docs commits after it
are docs-only):

- line-based `scan_client_names.py HEAD --messages wave28..HEAD`: **0 files /
  0 hits** on HEAD and **0 commit messages** quote a client name.
- whitespace-normalised `guard_names.py`: **0 hits, exit 0** on
  `wave28..HEAD` and on the push range `origin/wave28..HEAD`.  The preflights
  are `build/w29/close-push4-{scan,messages,guard,hex}.log` (the earlier
  `31bb11b` preflight is `build/w29/close-push3-guard.log`).  The guard and
  hex-grep files are zero-byte because those commands print only on failure;
  their explicit exit codes are the `guard_names ... exit: 0` and
  `hex grep exit: 1 (expected)` lines in
  `build/w29/gates-7e14aaa-W29-CLOSE.log`, and the docs commit's final range
  is logged with exit codes in `build/w29/close-push5-preflight.log`.
- `git grep -nE "client-string-[0-9a-f]{8}"`: no matches.

The wave-28 push blocker is gone: The operator approved the `076438f` rewrite, `wave28`
was replayed with the clean blob and pushed (`21646f4`), and `wave29` was
moved onto it (`docs/handoff/wave29-sha-map.txt`); the guard now passes the
whole range.  The residual tree state is unchanged: two pre-wave fixtures
(`tests/data/synthetic-base.mpp`, `tests/data/wave23-outline.mpp`) and 69
tracked text files (historical docs) still carry the operator's name, no gate
scans the whole tree for it, the 13 unsalted FNV-1a display-name hashes remain
in older wave-26 commits and `tests/data/project_a_golden.tsv`, and the
156-name scanner still cannot see skipped-row names.  Those are listed under
Found, not fixed; a repo-wide purge is the operator's decision.

### Independent review of `wave28..wave29`

One read-only reviewer ran the code review of `git diff wave28..wave29` (156
files); the record audit is the second reviewer.  The code review found **0
Critical / 2 High / 2 Medium / 2 Low** (C1-C6).  The first CI dispatch then
found C7 (High: the macOS build's unused-constant rejection), C9 (High: the
ubuntu-gcc test initializer) and C10 (High: the windows-qt parity gate's axis
shift); the record audit found C8 (Medium: the tier-boundary paint contradicts
the measurement, and the record repeated the code's claim).  C7, C9 and C10
are fixed before this record: `f7434d2` (W29-C3), `133cb3f` (commit label
W29-C5) and `7e14aaa` (commit label W29-C6); C8 is fixed in `31bb11b`
(W29-C4).  The findings, with the record's dispositions:

| id | severity | finding | disposition |
|---|---|---|---|
| C1 | **High** | Two unused anonymous-namespace functions added by this wave (`bar_text_field_items()` in `app_qt/format_dialogs_qt.cpp:311`, `exact_count()` in `app_qt/tests/theme_gate_test.cpp:127`, one occurrence each) fail GCC/Clang `-Wall -Wextra -Wpedantic -Werror`; MSVC /W4 does not warn, so the Windows gates and the W29-1.0a pattern scan saw neither. | **FIXED** in `02b720a`: both removed; app_qt 27/27.  The CI dispatch is the GCC/Clang check (no local clang/GCC). |
| C2 | **High** | `measured_progress_update_shape` accepted any recomputed mid-range percent (61, 59, 99, 20, ...) on the stored-60 % row; only the 60 -> 80 and 60 -> 40 payloads were measured.  The writer then encoded an unmeasured step percent. | **FIXED** in `4b03ecb`: only exactly 40 % and 80 % with a whole step percent pass; a 20 % date is now refused and tested; app 20/20, actuals 3725 checks / 50 expected failures. |
| C3 | Medium | `add_task_link` still writes Fixed2Meta +8 with the falsified "first/later link row" rule when the successor already has a live incoming link (`core/mppwrite/add_delete.cpp:1836-1845`); W29-1.0b measured the byte as driving state, and no pair exercises a second incoming link. | queued; the writer change needs a Project pair for a second incoming link (rule 1). |
| C4 | Medium | The null-save classifier masks a whole 16-byte block on any control-vs-null difference, so an edit effect sharing the block is skipped, and the props14/cedl tail is printed (`masked_tail`) rather than asserted; a truncated null fixture cannot fail a check (`tests/save_pair_noise.h:227-249`). | queued (tooling); the measured numbers are in the record and the effect checks that exist are field-level. |
| C5 | Low | A percent edit on a row with a live incoming link is enabled and refused only during Save (by `stage_progress_link_flags`), not at `make_percent_edit`. | queued; no committed pair exercises the combined shape. |
| C6 | Low | The record's Props-mask sentence (W29-0.3) describes a whole 4-byte payload word; the tests mask 2 bytes at payload +2 (`tests/roundtrip_outline.cpp:1093-1094`) and byte +3 (`app/app_edits_new_task_mode_tests.cpp:359-363`). | errata below; documentation only. |
| C7 | **High** | The first CI dispatch (run 40245417658) failed the `macos-qt` Build step: clang `-Werror` rejects the unused file-scope constant `kPropSessionWordCopy` at `core/mppwrite/outline.cpp:167`.  MSVC /W4 does not warn, so no Windows gate saw it. | **FIXED** in `f7434d2` (W29-C3): constant removed, tree-wide scan of the 81 touched files finds no other unused file-scope constant; re-dispatch is the compiler check. |
| C8 | Medium | The dark-theme timescale tier boundary is painted with `chart_header_rule_strong` (`app_qt/gantt_canvas.cpp:887`), but the measured `chart_tier_rule` samples match the WEAK rule (`#5D5D5D` DG / `#2D2D2D` Black), and the theme gate pins neither.  The W29-5.1 record sentence repeated the code's claim ("tier rules #545454/#323232"), which the measurement falsifies. | **FIXED** in `31bb11b` (W29-C4): dark themes paint the horizontal boundary with `chart_header_rule`; light keeps the wave-26 strong line (no light tier measurement); record sentence corrected (A1).  Gate coverage gap listed under Found, not fixed. |
| C9 | **High** | The first CI dispatch (run 40245429060) failed `ubuntu-gcc`: `tests/roundtrip_outline.cpp:1147/1150/1153` still used the seven-member `OutlineEdit{...}` initializer after W29-0.4 added the `slack` vector, and GCC rejects the missing member under `-Werror=missing-field-initializers`.  MSVC does not warn. | **FIXED** in `133cb3f` (commit label W29-C5): the three manual cases carry an empty initializer; core /WX build exit 0, `mppwrite_roundtrip_outline` 1749 checks / 0 failures / 7 expected failures.  The re-dispatch is the GCC check. |
| C10 | **High** | The first CI dispatch (run 40245417658) failed `windows-qt` on the wave23-outline parity gate: splitter 0.41146 vs 0.45885, tiers 1 vs 2, widths +88.  W29-1.4 applied the stock stored record (start 14400, day tier 100 %) and the day-unit origin snapped to the project-start day (Fri 11 Dec) instead of the week start (Sun 6 Dec); the first full weekend then began 1 day after the chart edge, the detector inferred `chart_left` = 790, and the splitter/width measures followed it.  The measured capture starts on the Monday of the project-start week. | **FIXED** in `7e14aaa` (commit label W29-C6): a stock start plus the 100 % day tier is Project's default record and its width is the shell's own week zoom (168 px / 7 = 24 px/day), so it now changes nothing; any saved start or non-default size still applies.  Parity wave23-outline 10/10 PASS, wave25-auto colour-only PASS, app_qt 27/27; re-dispatch is the CI check. |

The reviewer confirmed as sound: the target file is written only on the final
copy and byte-compared; the new writers validate before mutation and leave the
document unchanged on refusal; `normalise_record`'s bounds checks have no
off-by-one; Page Setup's semantics and arithmetic match the record; the format
and report dialogs are session-only with no fake success; the icon/theme gates
prove their claims; session styles do not set `document_dirty`; and the wave-29
additions carry no operator name, local user path or unsalted name hash.

#### The record audit

A second read-only reviewer audited this record file (1164 lines at `7262148`)
against the git history and the `build/w29/` logs.  It found **no Critical or
High**.  Its findings are record defects, not code defects; every one is
corrected in place in the sections above, and the corrected text names the
errata id.  A1 also exposed the code/measurement mismatch C8.

| id | severity | finding | disposition |
|---|---|---|---|
| A1 | Medium | The W29-5.1 sentence "its Monday/week and tier rules `#545454` / `#323232`" is false for the tier half: `chart_tier_rule` is `#5D5D5D` DG / `#2D2D2D` Black and the JSON note says the boundary matches the weak rule; the painter used the strong field and the gate maps neither tier sample. | sentence corrected in place; paint fixed by C8 (`31bb11b`) |
| A2 | Medium | The G4 item gate table cited `gates-4ee0cf3-W29-4.1-*.log` for build/scan/fixture/guard rows, but that glob holds only the app_qt and report-gate logs; the other numbers and the corrected guard re-run are in `g4-fresh-gates.log`, and no guard log exists for the range. | note after the G4 item table (A2) |
| A3 | Medium | Five per-commit privacy rows assert guard exit 0 / hex grep exit 1 citing zero-byte or absent per-item logs (`gates-21ca5b2-W29-0.1-{name-guard,client-string-grep}.log`, `gates-4a644c7-W29-0.2-{guard,client-string}.log`, `61f19c8-W29-0.3-guard*.log`, `gates-4fee013-W29-1.0a-{guard,client-string}.log`, no `7fc719b` guard log). | each row annotated: the G0/G1 group logs' range scans (0 named commits, guard exit 0) are the evidence, not the empty logs (A3) |
| A4 | Low | "Gates for commit `b751435` (parent `4a644c7`)" is false: its real parent is `6581a74`, the sha-map commit; `4a644c7` maps to `eddd9e9`. | heading corrected with the mapping (A4) |
| A5 | Low | The G5 resume note said only the app_qt build hit a file lock; the log also shows core `fuzz_*` retry 1 and app `core_sched`/`document_tests` retries 1-2. | note corrected (A5) |
| A6 | Low | "FALSIFICATION OK" had no log; only the script and a commit message contained it. | the script was re-run at CLOSE on the same pre-fix blobs; `W29-1.0a-falsification.log` records "FALSIFICATION OK: both pre-fix files are flagged" (A6) |
| B1 | Medium | The G3 privacy row's cited `g3-fresh-gates.log` reports 1 named commit from the unquoted-range bug; the true 0/0/0/1 numbers are in `g3-final-privacy.log`. | citation corrected (B1) |
| B2 | Low | The W29-0.3 commit table's `gates-61f19c8-W29-0.3-*.log` glob matches only four files; nine rows' numbers are in `61f19c8-W29-0.3-*.log` without the prefix. | heading annotated (B2) |
| B3 | Low | The 6407/18892 pixel counts and the 57/16/10/1 label counts appear only in `w29-1.3-qt-corpus.log`. | citations added (B3) |
| B4 | Low | The 76 -> 96 / 616 -> 716 and 3bf status words are not in the committed JSON; they are in the three `W29-0.3-*.log` files. | citation bullet added (B4) |
| B5 | Low | The G0 privacy row swapped the message/guard range labels relative to `g0-fresh-gates.log`, and the G4/G5 `git diff --check` clean claims had no log. | labels corrected to the log's; checks logged in `CLOSE-citation-checks.log` (B5) |

### Errata to the sections above (from the CLOSE review and the record audit)

The corrections below were applied in place in the earlier sections, so the
text above now matches its evidence; this list keeps the before/after record.

- **W29-5.1 (G5):** "its Monday/week and tier rules `#545454` / `#323232`" was
  false for the tier half (A1).  The measured `chart_tier_rule` samples match
  the weak rule, and the painter used the strong field; `31bb11b` (C8) paints
  the dark tier boundary with `chart_header_rule` and the sentence is
  corrected.  Light keeps the wave-26 strong line because no light tier
  measurement exists; the theme gate still does not pin the tier sample, which
  is listed under Found, not fixed.
- **W29-1.4 (G1):** the stock record's 100 % day tier is Project's default, not
  a stored zoom: its 24 px/day is the shell's own week zoom.  Applying it moved
  the wave-23 outline parity left edge to the project-start day and turned the
  first weekend band into the detector's "full band" case (C10).  `7e14aaa`
  returns before re-basing the axis for that default; the W29-1.4 detail above
  is corrected in place.
- **W29-0.4 (G0):** the `OutlineEdit` aggregate gained the `slack` vector, but
  the three wave-28 manual cases kept their seven-member initializers; MSVC
  accepted them and GCC did not (C9).  `133cb3f` supplies the empty member;
  the manual rows' bytes and expectations are unchanged.
- **W29-1.0a (G1) record citations:** the A6 falsification is now logged
  (`W29-1.0a-falsification.log`); the W29-C3 unused constant is the third
  compile error the pattern scan did not look for and the macOS dispatch
  found.
- **Record citations (A2-A5, B1-B5):** corrected in place as listed in the
  record-audit table above; no gate number changed, only its cited evidence.
- **W29-1.0a (G1):** the pattern scan looked for the two wave-28 compile-error
  patterns in the 43 touched files; it did not look for unused internal-linkage
  functions.  Two such functions were added by this wave and are removed in
  `02b720a` (C1).  The claim "found no other instance of the two patterns"
  stands only for those two patterns.
- **W29-0.3 (G0):** "the commands stay enabled only for the shapes the
  committed pairs measured" was true for the stored-percent and progress-block
  conditions but not for the recomputed percent; the gate accepted any
  mid-range value.  `4b03ecb` (C2) confines it to the measured 40 % and 80 %.
- **W29-1.0b (G1):** the falsified "first/later link row" reading is corrected
  in the `add_delete.h` comment, but the `add_task_link` writer still produces
  that byte rule for a second incoming link (C3).  The byte's driving-state
  meaning is measured; the writer's extension to that unmeasured shape is
  queued.
- **W29-0.1 (G0):** the null-save discipline's masked tail and whole-block
  masking are not asserted (C4); the per-stream effect counts quoted in G0 are
  measured but the tail is reported, not gated.
- **W29-0.3 Props mask:** the W29-0.3 text describes masking the whole 4-byte
  per-save id word; the committed tests mask 2 bytes at payload +2 in the core
  round-trip and byte +3 in the new-task-mode test (C6).  The files compare
  equal because the remaining two bytes are equal in those pairs; the sentence
  overstates the mechanism.

### Push and CI

The pre-push guard passes the whole range (the wave-28 rewrite blocker is
gone).  `origin/wave29` was advanced without force three times, each after a
clean `scan_client_names.py HEAD --messages origin/wave28..HEAD` and
`guard_names.py origin/wave28..HEAD`:

| push | range | log |
|---|---|---|
| 1 | `21646f4..7262148` (G5 record) | `build/w29/close-push*.log` |
| 2 | `4b03ecb..31bb11b` (review + audit fixes) | `build/w29/close-push3.log`, guard `close-push3-guard.log` |
| 3 | `31bb11b..7e14aaa` (CI fixes C9/C10) | `build/w29/close-push4.log`, guard `close-push4-guard.log` |

Both workflows were dispatched explicitly on `wave29` (the `workflow_run`
trigger still only fires from `main`).  The first dispatch ran on `31bb11b`:

| workflow | run | result |
|---|---|---|
| qt-shell | 40245417658 | **RED**: `linux-qt` pass; `macos-qt` Build fails on the unused constant `kPropSessionWordCopy` (C7); `windows-qt` fails the wave23-outline parity step (C10) |
| ci | 40245429060 | **RED**: `windows-msvc` pass; `ubuntu-gcc` fails the `roundtrip_outline` compile on the missing `slack` initializer (C9) |

C7, C9 and C10 were fixed (`f7434d2`, `133cb3f`, `7e14aaa`) and both workflows
re-dispatched on the final code tree `7e14aaa`:

| workflow | run | result |
|---|---|---|
| qt-shell | 40248190323 | **success**: `windows-qt`, `macos-qt` and `linux-qt` all green, including the wave23-outline parity and wave25-auto colour steps |
| ci | 40248193087 | **success**: `windows-msvc` and `ubuntu-gcc` both green |

Dispatch logs: `build/w29/close-dispatch.log` (first) and
`build/w29/close-dispatch4-{qt,ci}.log` (second).  The first-run job logs are
`build/w29/close-ci1-*.log`; the second-run status snapshots are
`build/w29/close-ci4-{qt,ci}-status.json`.  The CLOSE docs commit that carries
this record is pushed after it with the final preflight
`build/w29/close-push5-preflight.log`; it changes docs only, so the CI result
above belongs to the final code tree `7e14aaa`.  No history was rewritten and
the guard was never worked round.

### The 16 decisions re-scored (wave-28 close as the starting point)

| # | decision | wave 28 | after wave 29 | why |
|---|---|---|---|---|
| 1 | all seven tabs | DONE | **DONE** | the tab/group structure is unchanged; the Report tab's two buttons (W29-4.1) and the Format tab's three dialogs (W29-2.1) became live in their measured groups |
| 2 | full backstage | DONE | **DONE** | unchanged; the Print page's Page Setup button is now wired to the measured dialog (W29-3.1) |
| 3 | original icon set | DONE | **DONE (stronger)** | W29-5.2 re-authors the menu-bar, QAT and status-bar glyphs on the wave-26 one-ink grid and re-inks them live per theme (`window_ink`, contrast pinned); this closes the wave-28 "older drawings" note |
| 4 | measured chrome metrics | DONE | **DONE** | unchanged metrics; W29-5.1's one measured geometry fix (timescale rules end at the band edge) is a painter boundary, not a chrome rectangle |
| 5 | timeline strip | DONE | **DONE** | unchanged |
| 6 | real Entry table | DONE | **DONE** | unchanged |
| 7 | render what the file says | PARTIAL (stronger) | **DONE (stronger)** | W29-1.1 stores the `CTable` widths through the default font's measured unit, 1.2 paints the stored per-cell yellow fill, 1.3 draws every label the stored bar styles call for, and 1.4 pins a saved timescale origin and zoom (a stock start plus the 100 % day tier is Project's default record and deliberately changes nothing, W29-C6); top/bottom/inside label placement is a stated convention the capture cannot show, and the stored combination view is still undecoded |
| 8 | status bar | PARTIAL (stronger) | **PARTIAL (stronger)** | W29-5.2 gives all five view glyphs distinct one-ink drawings, re-inked per theme; Team Planner and Report still have no working view, so two buttons cannot switch to one |
| 9 | disabled commands + backlog | DONE (stronger) | **DONE (stronger)** | the generated backlog (95 ribbon rows, 77 disabled at this close) marks every newly live dialog command implemented with its named gap; W29-0.0's refusal gate pins every enabled command to its measured shapes, and each write path was lifted only against its own Project pair |
| 10 | light and dark themes | PARTIAL (stronger) | **PARTIAL (stronger)** | W29-5.1 closes the wave-26 gaps: the sheet caption band, gutter and corner and the Gantt timescale band paint from the measured dark palettes, and the ribbon ink follows the theme; the dropdown's White theme stays laid out and disabled |
| 11 | new-project defaults | DONE | **DONE (stronger)** | unchanged code; W29-0.1's Project reopen proves both new-task-mode port saves store the measured Manual value on their highest ID (True for the manual insert, False for the auto insert) |
| 12 | structural measurement | PARTIAL (stronger) | **PARTIAL (stronger)** | wave 29 adds field-by-field pairs for 7 new Project save pairs (three percent shapes, five progress-update saves, four auto-outline saves), three new live dialog gates (format, page setup, report) and five numbers-only measures files; the pixel score is still not re-run (no PROJECT_A reference PNG is local) |
| 13 | Carlito bundled | DONE | **DONE** | unchanged |
| 14 | every dialog on every tab | PARTIAL (stronger) | **DONE (stronger)** | the six wave-28 backlog dialogs now exist: Text Styles, Gridlines and Bar Styles (W29-2.1), Page Setup (W29-3.1), Visual Reports and Compare Projects (W29-4.1), each laid out from a Project capture |
| 15 | dialog semantics | PARTIAL (stronger) | **PARTIAL (stronger)** | Page Setup is fully functional (six tabs, every field edits the shared print layout, OK/Preview/Print gated pixel-for-pixel); Text Styles and Bar Styles apply to the open view only (no stored-style write exists), and Gridlines plus both report dialogs stay inert with the exact tooltip and an inline note |
| 16 | split view | DONE (stronger) | **DONE** | unchanged |

## Found, not fixed

* **F3 - no gate scans the whole tree for the operator's name.**  Two
  pre-wave fixtures (`tests/data/synthetic-base.mpp`,
  `tests/data/wave23-outline.mpp`) still carry it three times each, 69 tracked
  text files carry it, and the wave-29 privacy gates are HEAD/range scans for
  client names.  The 156-name scanner cannot see skipped-row names.  A
  whole-tree check and the purge are the operator's decision.  [High - privacy scope]
* **F6 - the `project_a` task-order gate still expects `""` / level `-1` for
  the project row** where Project measures `"0"` / 0
  (`app_qt/tests/task_order_test.cpp:342-344`); the twelve task rows are
  Project's values.  [Low]
* **F7 - `write_project_status_date` still accepts a status-date record longer
  than 4 bytes** (`core/mppwrite/actuals.cpp:1972` checks `length < 4u`)
  although its error text names the measured 4/4 shape.  [Low]
* **R28-5 - the UTF-16 user-profile check is still dead on the backslash
  forms**: `scripts/check_fixtures.py:452` builds `PROFILE_UTF16` without
  `re.escape`, UNC paths are covered by no branch, and there is no positive
  self-test.  [Medium - the privacy gate]
* **R28-6 - save-time ordering and late refusals remain.**  A percent edit plus
  a Start edit on one task in one save is unmeasured; a percent or date-update
  edit on a row with its own calendar, or with no start, is accepted and shown,
  then refused at save as a warning and the shell reloads.  [Medium]
* **R28-8 - the docked Task Form still enables view-only Name, Duration and
  Finish edits** with no sign they never save, and a commit after the selection
  moves is dropped silently.  [Low]
* **R28-9 - a missing committed pair fixture still prints SKIP and exits 0**;
  there is no minimum-check floor, so a deleted fixture weakens the gate
  silently.  [Low]
* **R28-11/A6 - `tests/data/README.md`'s "complete inventory" still lacks 21
  tracked files**: the 12 wave-28 mark/update/outline `.mpp` pairs, the four
  wave-26 reference JSONs, and the five wave-29 reference JSONs this wave
  added; every pin that exists matches bytes.  [Low - record keeping]
* **Carried process items**: A2/A5 (gate logs that mix trees or do not carry
  the quoted counters), A4 (rule-3 log-naming gaps in wave-28 commits), A7
  (fixture commits that also carry scripts/pins - repeated by `eddd9e9`,
  `b751435`, `bd9e13d` and `8f80c2a` this wave) and R28-12 (a wave-28
  per-commit gate omitted the core tree).  History cannot be rewritten.
  [Low/Medium - process]
* **W29-0.2's mid-to-mid pair is a named expected failure.**  A 25 -> 60 %
  save makes Project reschedule the chain (50 moved fields, summary durations
  26400 -> 23040 ticks); the port has no scheduler on that path, so the
  transition stays refused and the test pins all 50 fields as EXPECTED
  FAILURE.  [Medium - deliberate refusal with evidence]
* **W29-0.3's lowering rule is measured only on the two-record assignment
  block.**  A real assignment or any other block shape refuses to lower, and
  date updates stay refused on every progressed shape outside the measured
  automatic top-level 60 % one.  [Medium - deliberate refusal]
* **W29-1.3's top/bottom/inside bar-label sides are a stated convention**, not
  a measurement (the capture shows only left/right), and W29-1.4 clips a task
  before the stored timescale origin at x < 0 instead of pulling the axis
  back.  [Low]
* **W29-2.1's Text Styles and Bar Styles apply to the open view only.**  No
  stored-style write exists, so Save keeps the file's bytes; the
  background-pattern combo and the start/end/middle shape and pattern lists
  stay disabled with the decision-9 tooltip.  Gridlines is laid out and inert.
  [Medium - decision 15]
* **W29-4.1's Visual Reports and Compare Projects have no engine.**  Every
  current-affecting control is present, disabled and carries the decision-9
  tooltip; each dialog carries an inline note.  [Medium - decision 15]
* **There is no local clang, GCC or Xcode.**  The macOS/GCC code fixes (C1,
  C7, C9) were verified only by the wave-29 CI dispatch, which is green on
  `7e14aaa` (all three Qt jobs and both ci jobs); the Windows parity gate was
  reproduced and fixed locally before the re-dispatch (C10).  [Resolved for
  this wave; the local gap remains]
* **The theme gate does not pin the timescale tier-boundary colour.**  C8
  fixed the paint from the measured JSON, but the gate samples tier fills and
  not the one-pixel boundary, so a later change back to the strong rule would
  pass.  A line sample (or a rendered-boundary assertion) is queued.  [Low -
  W29-C4 coverage gap]
* **W29-0.1's reopen covers 10 port saves x the project row and IDs 1..3.**
  The TBkndAssn layer is field-compared, not reopened, because the wave-28
  fixtures' assignments are resource-less.  [Low]
* **The stored combination view is still undecoded and the White theme is
  still an unimplemented dropdown choice.**  [Low]
* **Split Task is still deferred** (W28-5.1): the segmented 128-byte assignment
  work contour, its VarMeta relocation and the plan-wide slack/date rewrite are
  not decoded; the button keeps the exact decision-9 tooltip and the committed
  pair is the later writer's comparison.  [Medium - deferred with its
  measurement, as the brief allows]
* **The engines with no writer remain** (project move, baselines, WBS codes,
  calendars), laid out and honestly inert; Custom Fields still covers the six
  measured families only (Start/Finish custom fields and the Resource/Project
  scopes are absent).  [Low]
* **The pixel score is still not re-run**: no PROJECT_A reference PNG is
  local.  [Low]
* **A stored point size of 0xFFFF still reaches Qt unguarded** in
  `app_qt/gantt_canvas.cpp:743` and `app_qt/sheet_table_model.cpp:261` (both
  test `> 0` only).  [Medium - wave-26 review]
* **Unlink still acts on the one selected task**, and a pending link ADD naming
  an unlinked task survives Unlink (it has no saved row to tombstone); undo is
  its removal path today.  [Medium/Low - W27-3.3]
* **The ribbon check-box accessible-name sync and the Task Information
  validate-then-commit ordering remain.**  [Medium - wave-26 review]
* **`scripts/mpp_diff.py` still truncates `first_diffs` at 8/10/12 offsets
  with no truncation marker.**  [Low - tooling]
* **The 156-name scanner still cannot see skipped-row names**, so the
  corpus-backed reader warning can print a name into the local-only CLOSE log;
  the warning should carry the uid/row only.  [Low - tooling/privacy]
* **174 tracked text files still contain CRLF lines** (measured at HEAD; the
  count is unchanged).  [Low]
* **The 13 unsalted FNV-1a display-name hashes remain in older wave-26 commits
  and `tests/data/project_a_golden.tsv`.**  Rewriting history is forbidden by
  the brief; both remain the operator's decision.  [Privacy]
* **The generated backlog still does not list the five dynamic %-complete
  ribbon controls, Update Project or Split Task.**  [Low - decision 9's
  backlog stays incomplete]

* **C3 - `add_task_link` still writes the falsified Fixed2Meta +8 rule for a
  second incoming link** (`core/mppwrite/add_delete.cpp:1836-1845`).  The byte
  is measured as driving state for the first link; the "later link row"
  extension has no Project pair, and no test exercises it.  Queued for a pair
  in the next wave.  [Medium - review]
* **C4 - the null-save classifier's masked tail is reported, not asserted**,
  and a whole 16-byte block is masked on any control-vs-null difference
  (`tests/save_pair_noise.h:227-249`).  The effect checks that exist are
  field-level; a truncated null fixture cannot fail a check.  Queued
  (tooling).  [Medium - review]
* **C5 - a percent edit on a row with a live incoming link is refused only
  during Save**, not by `make_percent_edit`; the file is not written, but the
  refusal is late and no pair combines the two shapes.  [Low - review]
* **C6 - the record's W29-0.3 Props-mask sentence overstates the tests' mask**
  (2 bytes at payload +2 and byte +3, not the whole 4-byte word); corrected in
  the errata above.  [Low - review]

WAVE-29-COMPLETE
