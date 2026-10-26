# Wave 30 - the completion record

One table row per brief item.  A group ends only when its exact
`WAVE-30-<group>-COMPLETE` line is appended below (wave-30-brief section 1).
Deferrals need the attempt evidence the standing rules and the brief name.

## G0 - the wave-29 review's Critical/High save-path findings

| item | finding | status | evidence |
|---|---|---|---|
| W30-0.0 | R29-8 (Medium; the brief moves it up front): fail-at-save refusals the edit model accepted | **DONE** | commit `8314f6c`; gates `build/w30/W30-0.0-*.log` |
| W30-0.1 | R29-1 (**Critical**): indent deletes a real zero-work assignment | **DONE** | commit `bd83a6b`; gates `build/w30/W30-0.1-*.log` |
| W30-0.2 | R29-2 (High): automatic indent/outdent on linked files | **DONE** | commit `022f68c`; gates `build/w30/W30-0.2-*.log` |
| W30-0.3 | R29-3 (High): outline moves over progress, resource work or queued actuals | **DONE** | commit `6599e33`; gates `build/w30/W30-0.3-*.log` |
| W30-0.4 | R29-4 (High): the 0 % -> 0 % date-update target | **DONE** | commit `660d72e`; gates `build/w30/W30-0.4-*.log` |
| W30-0.5 | R29-5 (High): the summary roll-up recompute scope | **DONE** | commit `048ed72`; gates `build/w30/W30-0.5-*.log` |
| W30-0.6 | R29-6 (High): the resource-loaded contour rule | **DONE** | commit `4a774c5`; gates `build/w30/W30-0.6-*.log` |
| W30-0.7 | R29-7 (High): Add Link's falsified "later link = 0x07" byte | **DONE** | commit `b2517bc`; gates `build/w30/W30-0.7-*.log` |

No G0 item was deferred.  Every item narrows an enabled save path to the shape
its Project pair measured, or refuses every other shape up front; G0 measured
no new Project pair.  The review's other Medium/Low findings
(R29-9 .. R29-19) stay queued for later waves as brief section 3 says; G0
touched none of them beyond R29-8, which the brief placed first.

W30-0.0 detail:

- The actuals writer's four throw-at-save refusals -- a task whose TBkndAssn
  table does not hold exactly one row, two or more live incoming links, one
  live incoming link with a transition other than 0 % -> non-zero, and a date
  update lowering a recorded percent on a real assignment -- are now refused
  in the edit model: `make_percent_edit`, `make_update_actuals_edit` and the
  plan-aware `push()` re-validation, each with a status-line reason.  The
  writer keeps its checks as last-line defence for a hand-built queue.
- Tests: `make_plan()` gives every live task the one placeholder assignment
  the fixture rows carry, with the work-loaded row on task 2; one check per
  refusal branch in `app_edit_tests` (no/two assignment rows, two incoming
  links, one incoming link with a non-entering transition) and in
  `app_edits_actuals_tests` (two links, one linked progressed row, one linked
  0 % entrance accepted, assignment count, real-assignment lowering).

W30-0.1 detail:

- The indent path now requires the new summary's assignment to be exactly
  Project's placeholder (one row, resource word 0xFFFF0001 / resource uid 0):
  `check_new_summary_shape` refuses a real assignment of any work -- including
  the zero-work cost/material or zero-units shape this Critical finding was
  about -- and `core/mppwrite/outline.cpp` refuses to tombstone when the raw
  FixedData +8 word is not 0xFFFF0001, so a hand-built queue cannot either.
- Tests: the outline refusal suite refuses a real zero-work assignment and a
  missing assignment row; an indent whose SOURCE file names a real resource
  while the plan still reports the placeholder is refused by the writer (the
  warning names 0xffff0001) and the saved file keeps the real assignment.

W30-0.2 detail:

- Automatic indent/outdent -- the only shape that carries the slack/late-date
  rewrite -- is refused when the plan has any link or any unreadable link row;
  `StructuralEdits::push` refuses a slack-carrying edit against such a plan.
  Manual outline edits carry no slack and stay accepted on linked files.
- Tests: an automatic indent and an automatic outdent are refused on a plan
  given a synthetic link, an automatic indent is refused with an unreadable
  link row (both messages name the links), a manual indent stays accepted on
  the same linked plan, and `push()` refuses an automatically built edit once
  the plan carries a link.

W30-0.3 detail:

- `make_outline_edit` walks the display-order subtree of the moved row (and,
  on an outdent, the parent that becomes a leaf again) and requires every
  member to be 0 %, to carry no resource assignment with work, and to have no
  actuals edit queued in the same save.  `push()` repeats the checks against
  the queued actuals edits, and a percent or date edit pushed over a pending
  outline edit is refused for the move's subtree, its anchor and their
  ancestors, closing the step-5a/5b combination the review found.
- Tests: a moved row at 40 %, a progressed child in the moved subtree, a moved
  row with a resource-work assignment, a pending actuals edit on the moved
  row and a progressed outdent parent are each refused by message; an indent
  queued over a percent edit on the moved row is refused, and a percent edit
  and a date update queued over an outline edit are refused (each with a
  no-outline positive control so the refusal is the guard's).

W30-0.4 detail:

- `make_update_actuals_edit` drops a target whose row is 0 % and whose
  computed progress through the update date is 0 %, leaving the row untouched
  (the no-op the review recommended); an update whose every selected row is
  such a no-op is refused with nothing to write.  `push()` refuses a
  hand-built 0 -> 0 target.  The unmeasured 100 -> 0 reset words are therefore
  no longer reachable through the date-update path.
- Tests: an all-no-op update through a row's own start is refused and names
  the reason; a mixed scope with one row's start moved past the update date
  builds two targets and carries no 0 -> 0 target; a hand-built 0 -> 0 target
  is refused while the same edit as the model builds it queues.

W30-0.5 detail:

- The writer now takes the recompute set the save computes from its applied
  edits -- each edited row's parent chain -- and leaves every other summary's
  stored roll-up byte for byte.  The model refuses up front when a chain
  summary holds a second progressed leaf (counting pre-existing plan progress
  and every actuals edit queued in the same save, last write wins for a
  duplicated uid) or is manually scheduled; `make_percent_edit`,
  `make_update_actuals_edit` and the plan-aware `push()` all apply it.
- Tests: uid 4 patched to 40 % refuses the uid 3 edit with the two-leaf
  message; a manual uid 2 refuses; the unmodified one-leaf edit stays
  accepted; `push()` refuses a second leaf queued after a sibling edit while
  the same edit queues alone.  An off-chain top-level summary (uid 5 over a
  40 % uid 6) stays unchanged in its whole FixedData row and keeps Project's
  stored roll-up -- the old sweep wrote 40 % there -- while the real chain
  still rolls up to Project's measured 20 % on uid 2.

W30-0.6 detail:

- The resource-loaded contour shape is pinned to the one measured shape of the
  wave-29 work pair: 9600 ticks of task duration, 100 % units, 9600 ticks of
  assignment work and no task calendar (the project calendar).  Each other
  axis is refused with its own status-line reason.  The task calendar needed
  a reader-visible field, `TaskRow::calendar_uid` (FixedData +178, -1 = none).
  R29-1's residual on the actuals path is closed at the same time: a real
  assignment carrying ZERO work never reached `has_work_resource_assignment`,
  yet the writer still took its real-assignment branch, so percent and date
  edits on such a row are refused too.  The writer keeps a last-line throw for
  a hand-built queue: a real assignment is written only at 9600 ticks and
  100 % units (+12 is Units x100, 10000.0 = 100 %).
- Tests: the measured work pair stays accepted in the model and its save pair
  passes byte for byte.  `app_edit_tests` patches the synthetic measured plan
  one axis at a time -- duration 4800, 50 % units, 4800 ticks of work, task
  calendar uid 3, zero-work real assignment -- and asserts a branch-specific
  reason for each; `app_edits_actuals_tests` asserts the reader gives uid 6
  the measured values (9600 ticks, calendar -1; assignment 7: resource 1,
  100 % units, 9600 ticks of work) and refuses the same four patched axes on
  the real control.

W30-0.7 detail:

- Wave 29 measured the link Fixed2Meta +8 byte as the successor's driving
  state, not a position marker: 0x0f for a first link into a 0 % successor,
  0x07 once the successor enters progress.  In the percent-mid25 control the
  FIRST of three links into distinct successors is 0x07 because that successor
  has progress, and the work pair moves its incoming link 0x0f -> 0x07 when
  the successor enters progress.  The wave-47 "first link 0x0f / later link
  0x07" rule is falsified, so the writer no longer fabricates either shape it
  could not have measured.
- `validate_link_add` now refuses a successor that already has any live
  incoming link in the display plan (a pending delete that frees it still
  passes: that is the measured delete-and-re-add save) and a successor whose
  percent is non-zero, each with its own status-line reason.  `add_task_link`
  refuses the same two shapes as last-line defence; every allowed add is a
  first link into a 0 % successor and writes 0x0f.  The header and test claims
  that repeated the falsified rule are corrected; the link sequences in
  `roundtrip_links` use a free successor and assert 0x0f on every add.
- Tests: `app_edits_links` pins the two refusals by their branch phrases, the
  measured two-add and delete-and-re-add saves stay accepted, and the mixed
  delete+add save now targets a successor with no incoming link (203 checks).
  `roundtrip_links` adds a second-incoming-link refusal and a
  progressed-successor refusal (task 1 patched to 40 % in FixedData) and
  passes; `fuzz_writer` passes with the extra refusals.

Per-commit gates (logs under `build/w30/`, named `W30-0.x-*.log`; every
fixture log reports fixtures OK -- 98 documented hashes, 101 tracked `.mpp`,
148 tracked test-data files with no local path, 8 scenario oracles -- and every
client-name scan reports 0 files / 0 hits with `guard_names wave29..HEAD`
exit 0):

| commit | builds | item tests | app ctest |
|---|---|---|---|
| W30-0.0 `8314f6c` | app build exit 0 | `app_edit` 217 checks; `app_edits_actuals` 3746 checks, 50 expected, 0 failures | 20/20 |
| W30-0.1 `bd83a6b` | app build exit 0 | `app_edits_outline` 3149 checks; `app_edit` 217 checks | 20/20 |
| W30-0.2 `022f68c` | app build exit 0 | `app_edits_outline` 3158 checks; `app_edit` 217 checks; `app_edits_actuals` 3736 checks, 50 expected | 20/20 |
| W30-0.3 `6599e33` | app build exit 0 | `app_edits_outline` 3178 checks; `app_edit` 217 checks; `app_edits_actuals` 3736 checks, 50 expected | 20/20 |
| W30-0.4 `660d72e` | app build exit 0 | `app_edits_actuals` 3745 checks, 50 expected; `app_edits_outline` 3178 checks; `app_edit` 217 checks | 20/20 |
| W30-0.5 `048ed72` | app build exit 0 | `app_edits_actuals` 3765 checks, 50 expected; `app_edits_outline` 3178 checks; `app_edit` 217 checks | 20/20 |
| W30-0.6 `4a774c5` | app build exit 0 | `app_edit` 227 checks; `app_edits_outline` 3178 checks; `app_edits_actuals` 3776 checks, 50 expected | 20/20 |
| W30-0.7 `b2517bc` | app build exit 0; core build exit 0 | `app_edits_links` 203 checks; core `mppwrite_roundtrip_links` and `fuzz_writer` passed; core ctest `-E plan_recalc` 52/52 | 20/20 |

G0 group gates (fresh trees configured from scratch on `b2517bc` under
`build/w30-fresh{,-app,-qt}`, built serially, log
`build/w30/W30-G0-fresh-gates.log`; the run started 13:20 and finished 13:36
with no lock retries):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 checks / 420 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app ctest / app_qt ctest | 20/20 / 27/27 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (98 documented hashes, 101 tracked `.mpp`); paths OK (148 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `wave29..HEAD` / `guard_names wave29..HEAD` / hex grep | 0 files, 0 hits / 0 commits / exit 0 / no matches (exit 1, expected) |

The numbers belong to `b2517bc`; the marker commit that follows is docs-only.

## Found, not fixed

- **The display plan cannot see a link whose endpoint the reader dropped.**
  `PlanInput` records only a `skipped_links` count for a reader link whose
  predecessor or successor is not a live task; it carries no per-row endpoint
  data.  Add Link's up-front refusal checks `plan.links`, so on a file holding
  a saved link into the successor whose predecessor is not a live task the
  dialog cannot refuse; the writer's W30-0.7 guard still refuses at save time,
  so no unmeasured byte is written, but the refusal is fail-at-save (the R29-8
  shape) rather than up front.  Fix direction: carry the skipped links'
  endpoints through `PlanInput`/`reader_adapter` so `validate_link_add` can
  refuse them up front.
- **`memory/writer.md` still states the falsified link rule.**  Lines 1126,
  1144 and 1256 repeat "0x0f first link into that successor / 0x07 later
  ones".  The code (`add_delete.cpp`, `add_delete.h`) and the tests are
  corrected by W30-0.7, but the memory note is not; a documentation fix for
  CLOSE or the next wave, so the falsified rule cannot be re-derived.

WAVE-30-G0-COMPLETE

## G1 - the Task tab's commands

| item | finding | status | evidence |
|---|---|---|---|
| W30-1.0 | Manually Schedule / Auto Schedule / Mode (task-mode write) | **DONE (manual direction)** | commits `0d0770e` (pairs), `d51bcc0` (writer + UI); gates `build/w30/W30-1.0-*.log` |
| W30-1.1 | Insert Summary and Insert Milestone | **DEFERRED (measured)** | commit `70b757a`; pairs `wave30-insert-*`; gates `build/w30/W30-1.1-*.log` |
| W30-1.2 | Inactivate | **DEFERRED (measured)** | commit `2a99c98`; pairs `wave30-inactivate-*`; gates `build/w30/W30-1.2-*.log` |
| W30-1.3 | Respect Links | **DEFERRED (measured)** | commit `ac008fa`; pairs `wave30-respect-links-*`; gates `build/w30/W30-1.3-*.log` |
| W30-1.4 | Move (one working day forward and back) | **DEFERRED (measured)** | commit `8c1861b`; pairs `wave30-move-*`; gates `build/w30/W30-1.4-*.log` |
| W30-1.5 | Notes and Details | **Notes DEFERRED (measured); Details DONE** | commits `9ee38f6` (pair), `2e79aec` (view); gates `build/w30/W30-1.5-*.log` |
| W30-1.6 | Add to Timeline | **DEFERRED (measured; Project's own command inert)** | commit `b2c3366`; pair `wave30-timeline-*`; gates `build/w30/W30-1.6-*.log` |
| W30-1.7 | Scroll to Task | **DONE** | commit `2b131ec`; gate `qt_scroll_to_task_gate`; gates `build/w30/W30-1.7-*.log` |
| W30-1.8 | Fill (the selected cells) | **DONE (Start column; every other column refused)** | commit `daaf3a1`; gate `qt_fill_gate`; gates `build/w30/W30-1.8-*.log` |
| W30-1.9 | Font B/I/U and colours, Format Painter | **DEFERRED (Bold measured)** | commit `36298e9`; pair `wave30-textstyle-*`; gates `build/w30/W30-1.9-*.log` |
| W30-1.10 | Inspect (read-only pane of the scheduler's facts) | **DONE** | commit `5bbcf17`; gate `qt_inspect_gate`; gates `build/w30/W30-1.10-*.log` |
| W30-1.11 | Deliverable (Project Online) | **DONE (stays disabled, exact tooltip)** | commit `3aa4549`; `qt_ribbon_inventory`; gates `build/w30/W30-1.11-*.log` |

Five items are closed DONE (1.0's manual direction, 1.7, 1.8, 1.10 and 1.11),
1.5 is half done (Details live, Notes deferred), and seven command shapes are
deferred with a committed Project pair (1.1, 1.2, 1.3, 1.4, 1.5 Notes, 1.6 and
1.9).  No disabled button changed state without a measured pair; 1.11 is the
one item the brief names as staying disabled, and it does.  G1 added 39
tracked `.mpp` fixtures (101 at G0's close, 140 at G1's) and one new app test
target per implemented command (plus the ribbon inventory's named Deliverable
check).

W30-1.0 detail:

- Two Project 2021 save pairs for the Manually Schedule write, measured by the
  committed `scripts/oracle_w30_task_mode.py` on the synthetic base: three
  0 % top-level manual leaves with durations 24000/14400/9600 ticks, the
  project calendar, one placeholder assignment per task, no links and no
  progress.  The auto -> manual pair changes Task ID 1's Manual flag and
  reopens with Start/Finish/Duration/Percent unchanged; the manual -> auto
  pair RESCHEDULES the row (Start Sun 2026-01-04 13:00 -> Mon 2026-01-05
  08:00, Finish Fri 2026-01-09 12:00 -> 17:00), so Auto Schedule stays
  disabled with the decision-9 tooltip.
- `core/mppwrite/task_mode.{h,cpp}` writes the measured 18 bytes in five
  streams plus the Props14 edit marker: Task FixedData +174 start anchor,
  FixedMeta +17 |= 0x04 and +18 &= ~0x20, Fixed2Meta +8 |= 0x80, Fixed2Data
  +50/+54/+58 start/finish/duration copies, the placeholder assignment's
  Fixed2Meta +8 |= 0x80, and the marker payload 0 -> 1.  Validation completes
  before the first mutation.  The whole-file shape check refuses every
  unmeasured condition: links, summaries/non-leaves, non-positive durations,
  task calendars, any progress, anything but one live placeholder assignment
  per task whose stored work equals the task's duration, real resources, a
  missing automatic precondition, and an already-manual row.  One row per
  save.
- `app::make_task_mode_edit` applies the plan check and then the file check
  when the command runs, so the refusal is never deferred to save; a sheet/file
  date or duration mismatch is refused too.  The edit is exclusive in both
  directions in `push()`, and the save cascade re-checks it.  Undo/redo
  restores the file's bit from the displayed plan.  Qt enables the Task tab's
  Manually Schedule button and Mode's Manually Scheduled entry; Auto Schedule
  and Mode's automatic entry stay disabled with the exact tooltip.
- `app_edits_task_mode_tests` (331 checks) saves the control through the real
  cascade and compares every owned stream and Props14 with Project's file
  under `tests/save_pair_noise.h` (19 effect bytes, 0 mismatches), reopens the
  port's save, and pins each refusal with its own check.

W30-1.1 detail:

- Measured by `scripts/oracle_w30_insert.py` in one session per command (sheet
  row 2 selected, control saved, `InsertSummaryTask()` /
  `InsertMilestoneTask()`, saved, every file reopened).  Both APPEND a
  202-byte task record at the tail (uid 4 / ID 2, fractional Fixed2Data
  ordinal 2.5), RENUMBER the old IDs 2/3 to 3/4, and REPARENT the selected
  row.  Summary writes the summary words and appends a TOMBSTONED assignment
  row; Milestone writes the milestone words and appends a LIVE zero-work
  placeholder assignment with a 72-byte contour block.
- DEFERRED: Insert Task's measured writer appends an integer-ordinal row with
  its own eight-key VarMeta set; it has no ID renumbering, fractional ordinal,
  reparenting, summary/milestone words, assignment tombstone, placeholder
  sentinel or contour writer, and `add_assignment_row` refuses resource 0.
  The appended tails lie beyond the control's size, so
  `tests/save_pair_noise.h`'s fixed-block mask (computed over the control's
  size) leaves them unmasked, and they carry Project's per-session creation
  stamps.  The port's reader also ignores the 2.5 ordinal, so it prints the
  new row last where Project reopens it at sheet row 2.  Both buttons stay
  disabled with the exact tooltip; the two pairs and their null saves are
  committed as the oracle.

W30-1.2 detail:

- Measured by `scripts/oracle_w30_inactivate.py` and
  `oracle_w30_inactivate_second.py`.  Pair 1 inactivates ID 2 (finish before
  the project finish): four effect sites and no date movement -- Fixed2Meta
  +8 0xff -> 0xbf, the placeholder assignment's Fixed2Meta +8 0xcf -> 0x8f,
  the project row's active-ID list [3,1,2,3] -> [2,1,3], and 114/Props byte
  3358.  Pair 2 inactivates ID 1 (whose finish IS the project finish) and
  Project RECALCULATES: project duration 21600 -> 7200 ticks, the remaining
  rows' slack words zeroed, the inactivated row's FixedMeta +18 loses 0x40,
  Props bytes 390/3358 rewrite, and DocumentSummaryInformation changes the
  project Finish "Fri 9/01/26" -> "Tue 6/01/26" and Duration "4.5 days" ->
  "1.5 days".
- DEFERRED: the save shape depends on whether the task defines the project
  boundary, which is a scheduler question; the reader does not carry
  `Task.Active`; and Project's button is a toggle, so reactivation would need
  a second measured shape.  The button stays disabled with the exact tooltip;
  the two pairs are committed as the oracle.

W30-1.3 detail:

- Measured on the committed linked fixtures by
  `scripts/oracle_w30_respect_links.py` -- the manual shape from
  `wave23-outline.mpp`, the automatic shape from `wave25-auto.mpp`.  The six
  tasks carry links 3 -> 4 FS+2 d, 3 -> 5 SS+1 d and 4 -> 6 FF; the target
  ID 4 is already link-satisfied.  `TaskRespectLinks()` returns True and no
  date moves in either shape (the manual shape records the undo entry
  "Respect Links").  Every Task, assignment, link, project and calendar stream
  and DocumentSummaryInformation has ZERO effects; each pair's only unmasked
  byte is the universal Props14 edit marker (211 noise bytes).
- DEFERRED: the command's real effect is a reschedule, and no stable off-link
  shape could be produced through COM -- the manual date setters pin the
  opposite end, a lag edit under the automatic option reschedules the
  predecessor, and a violated link created with the option off produced no
  date change.  The port has no scheduler, so there is no measured reschedule
  save shape to reproduce.  The button stays disabled; the two pairs are the
  oracle and negative baseline for a later writer.

W30-1.4 detail:

- Measured by `scripts/oracle_w30_move.py`: `TaskMove` one working day forward
  and one back on a manual row (`IsWorkingDuration = True`).  Forward moves
  Start and Finish one day, keeps the duration and the project row, and writes
  57 effect bytes under the mask: the row's slack words 9600 -> 4800 ticks,
  Start/Finish copy words and StartAnchor, FixedMeta field count 0x0b -> 0x09,
  Fixed2Data +50/+54, Fixed2Meta +88 clears 0x0C, REMOVES the row's StartText
  and FinishText VarMeta entries, moves the placeholder assignment's four date
  words, and bumps 114/Props byte 3366.  Back moves only Start (the stored
  Finish stays) and additionally rewrites the project row's Start words and
  DocumentSummaryInformation's project Start; 59 effect bytes.
- DEFERRED: there is no TaskMove edit model; the writer never removes a
  VarMeta entry; and the date writes carry no slack/late/anchor words or the
  project summary's DocumentSummaryInformation date.  Rule 1 would also
  require refusing automatic rows, links, constraints, summaries, progress,
  real assignments, task calendars, multiple selected rows and the elapsed-day
  option.  The button stays disabled; both pairs are committed as the oracle.

W30-1.5 detail:

- Notes was measured by `scripts/oracle_w30_notes.py` (`Task.Notes` set to one
  plain ASCII line on row 2).  The save APPENDS one 198-byte RichEdit block to
  Task Var2Data (516 -> 718) and INSERTS a 12-byte VarMeta entry (key
  0x0B40000F) at slot 21 (logical count 42 -> 43), bumps the row's FixedMeta
  +2/+14/+44 and the 114/Props Task Var2Data size; 271 effect bytes.
- Notes DEFERRED: the RichEdit serialisation's escaping (RTF metacharacters,
  line breaks, non-ANSI text), its header/font-table/generator version and its
  re-render on edit or clear are unmeasured; the writer never inserts or
  deletes VarMeta entries; Rule 1 would also require refusing an existing
  note, multi-line or non-ASCII text, summaries, milestones, progress and
  multiple rows.  The Notes button stays disabled with the exact tooltip and
  Task Information's Notes tab keeps its laid-out-only note.  The pair is
  committed as the oracle.
- Details is a view, so it has no save path and no pair.  RibbonCommand
  `task_details` enables the Task tab's Properties > Details row and calls
  `set_details_visible(true)` on the Task Form pane that View > Details
  already docks.  The View tab's check box became member-driven
  (`details_checked_`), survives tab switches and reflects the open pane; the
  Task-tab button only SHOWS the pane, the View check box stays the toggle.
  `qt_split_view_gate` section 5 clicks the live button and proves the pane
  docks, the document stays clean and the check box follows.

W30-1.6 detail:

- Measured by `scripts/oracle_w30_timeline.py`
  (`TaskOnTimeline(TaskID=2, Remove=False)` on sheet row 2).  The call returns
  True and every COM fact is unchanged, but Project 16.0.20430's own Add to
  Timeline command is DISABLED and inert: the UIA toggle `TaskAddToTimeline`
  reports IsEnabled False with rows 1-3 selected, with Select All, and on a
  fresh Project-created project; a direct Toggle() leaves it Off; and the
  documented corrupted-Timeline-view repair did not re-enable it.  The save
  carries no timeline state -- every Task/assignment/link/project/calendar/
  view stream has zero effects, DocumentSummaryInformation is byte-identical,
  and the only unmasked byte is the Props14 edit marker (137 noise bytes).
- DEFERRED: no measurable save shape exists on this Project build, so Rule 1
  forbids a save path.  The button stays disabled with the exact tooltip; the
  pair is the negative baseline and oracle for a later wave that finds a
  working Project build.

W30-1.7 detail:

- Scroll to Task is view only, so there is no save shape and no pair.
  `MainWindow::scroll_to_task()` scrolls the sheet to the current row and, when
  a task is selected, asks `PlanDocument::scroll_gantt_to(uid, zoom)` to pin
  the chart's left edge to the task's start day through the existing
  `Timescale` origin path; the row menu's action calls the same method.  The
  origin is session state: `reset()` (load or File > New) clears it,
  structural rebuilds and zoom changes keep it; uid 0 and a start-less task
  refuse without moving the axis.  No path touches the RecordSet, the plan or
  the save diff.
- `qt_scroll_to_task_gate` clicks the LIVE Task-tab button on
  `wave23-outline.mpp`: uid 6 starts 2026-12-11 and uid 4 starts 2026-12-16;
  after each selection + click the layout's timescale origin equals that
  literal day and the selected bar sits at x = 0, and the second click clips
  the earlier bar left.  A shrunken window with 96-px user-resizable rows
  proves the button scrolls a genuinely out-of-viewport selected row back in.
  Selection unchanged and `document_dirty()` false at every step.

W30-1.8 detail:

- Fill is scoped to the one measured cell writer: a Start edit becomes an
  `app::StartEdit` through the existing start/SNET writer (Project's
  `manual-start-edit.json` pair; the port's cell-to-file path is pinned by
  `save_reopen_test`).  `MainWindow::fill_down()` copies the top visible Start
  cell's typed Value down the visible rows of the selection in ONE sheet
  transaction, so the existing save diff emits one StartEdit per changed row;
  no new writer and no new save shape are introduced.  The row menu's Fill
  Down runs the same method.
- Refused before anything is staged: a selection whose visible columns are not
  Start (the message names the column with no measured writer), fewer than two
  visible rows, a top cell with no date, the project summary row as the source
  (it is always row 0 and read-only for paste/cut), and a target row that is a
  pending insert.  The whole fill is ONE undo step.
- `qt_fill_gate` clicks the live button: a two-row Task Name selection, a
  one-row Start selection, the project row as source and a Cut-cleared top
  Start cell each leave the cells, pending edits, undo and `document_dirty()`
  untouched; filling rows 4..6 in Start writes both targets, stages exactly
  two start edits, marks the document dirty, and ONE Undo restores both and
  cleans it; the row menu is one more undo step; a Start selection ending on
  a pending Insert Task row is refused with no edit staged.

W30-1.9 detail:

- One measurement: `Application.FontBold(True)` on the Name cell of sheet row
  1 of the synthetic base.  COM reopens both files with unchanged dates,
  durations, percent, manual flags and IDs, so the edit lives only in the
  view.  Under the mask the save has 6607 effect bytes: it APPENDS one
  `0x2240000C` record to the applied view's main block in
  `214/CV_iew/Var2Data` -- 12 header bytes plus one 44-byte entry (uid 1,
  field 0x0B40000E Name, flags 0x01000003, automatic background, pattern 1,
  font word 0x90000001) -- bumps the block length 42692 -> 42748, its two
  size words 0x0000A6C0 -> 0x0000A6F8 and the item count 10 -> 11, shifts
  every later block +56 as a byte-identical copy, rewrites
  VarMeta's var2_size and five later-block offsets, writes 214/Props +9962
  (the stored CV_iew Var2Data size), and carries the Props14 marker.  Every
  Task, assignment, link, project and calendar stream has zero effects;
  DocumentSummaryInformation is byte-identical.
- DEFERRED: the port has no 214 view-stream writer at all -- the write is a
  mid-stream insertion plus a VarMeta offset rewrite and a Props size byte --
  the font word's bits are opaque beyond this one bold measurement, and Rule 1
  would need every sibling shape refused or measured (I/U/strikethrough, font
  and highlight colours, patterns, name/size, cell vs row vs multi-row
  selection, every field id, the project summary row, other views, and Format
  Painter's copy between two selections).  The Font B/I/U, highlight colour,
  font colour and Format Painter rows stay disabled with the exact tooltip.
  The pair is committed as the oracle for a later writer.

W30-1.10 detail:

- The Task tab's Inspect button toggles a Task Inspector pane docked at the
  right of the chart pane.  Every value is read from the loaded PlanInput by
  UniqueID (never a RecordSet row index): Name, ID, Unique ID, Outline, Type,
  Mode, Start, Finish, Duration, % Complete, Critical, Calendar,
  Predecessors, Successors and Resources, plus a selection line.  The
  predecessor/successor and resource texts use the same `app::` helpers as
  the sheet's computed columns; dates, durations and percent use the sheet
  formatters.  All fifteen fields are read-only QLineEdits (read-only, never
  disabled).
- Honest states: the project summary row is synthesised from
  `app::project_summary_row` ("Project summary"); a pending Insert Task row
  is "Task (not saved yet)" with Mode "not saved yet"; a task calendar reads
  "Task calendar (UniqueID %1)"; no selection reads "No task selected" with
  every field empty.  The pane is a hidden 260-px item in the chart pane's
  horizontal layout, so hidden it changes no measured rectangle; it is shown
  only in the task-sheet view.  The ribbon button toggles it (second click
  hides, third shows and refreshes) and its own Close button hides it.
- `qt_inspect_gate` runs nine sections on `wave23-outline.mpp` plus a fresh
  window with a pending insert: the live button's wiring, the facts for uid
  4/1/6 and the project row, selection following, toggle semantics, view
  switches, Close, the pending row, and the document clean at every step.

W30-1.11 detail:

- The brief keeps Project Online's Deliverable disabled with the exact
  tooltip, and no measurement is wanted.  The control is unchanged:
  `ribbon_pin.tsv` carries enabled 0 for `insert.deliverable`;
  `ribbon_bar.cpp:318` passes `MenuAction::none`, no RibbonCommand and
  enabled = false, so the disabled builder sets exactly "not implemented in
  this port"; no `RibbonCommand::deliverable` and no `main_window` handler
  exist; the generated backlog row stays disabled with that reason.
- W30-1.11 adds a named live check next to the Split Task check in
  `qt_ribbon_inventory`: the Deliverable button on the live Task tab's Insert
  group is present, disabled and carries exactly the decision-9 string.  The
  pin loop already enforces this for every disabled control; the named check
  states the brief's requirement for this one button, so enabling it without a
  measured pair fails there with the item's wording.  No product code changed.

Per-commit gates (logs under `build/w30/`, named `W30-1.x-*.log`).  Every
commit's fixture log reports fixtures OK and every client-name scan reports
0 files / 0 hits with `guard_names` exit 0; the per-commit logs carry the
then-current counts (104 documented hashes throughout; 107 tracked `.mpp` at
W30-1.0 rising to 140 by W30-1.9; 154 -> 187 tracked test-data files; 8
oracles at the end).  Fixtures-only commits changed no code and ran no build
or ctest.

| commit | builds | item tests | ctest |
|---|---|---|---|
| W30-1.0 pairs `0d0770e` | core exit 0; app exit 0 | core 52/52 | app 20/20 |
| W30-1.0 `d51bcc0` | core, app, app_qt exit 0 | `app_edits_task_mode` 331 checks, 0 mismatches; `qt_ribbon_inventory` | app 21/21; app_qt 27/27 |
| W30-1.1 `70b757a` | -- (fixtures only) | -- | -- |
| W30-1.2 `2a99c98` | -- (fixtures only) | -- | -- |
| W30-1.3 `ac008fa` | -- (fixtures only) | -- | -- |
| W30-1.4 `8c1861b` | -- (fixtures only) | -- | -- |
| W30-1.5 Notes `9ee38f6` | -- (fixtures only) | -- | -- |
| W30-1.5 Details `2e79aec` | core, app, app_qt exit 0 | `qt_split_view_gate`; `qt_ribbon_inventory` | app 21/21; app_qt 27/27 |
| W30-1.6 `b2c3366` | -- (fixtures only) | -- | -- |
| W30-1.7 `2b131ec` | core, app, app_qt exit 0 | `qt_scroll_to_task_gate` (new); `qt_ribbon_inventory` | app 21/21; app_qt 28/28 |
| W30-1.8 `daaf3a1` | core, app, app_qt exit 0 | `qt_fill_gate` (new); `qt_ribbon_inventory` | app 21/21; app_qt 29/29 |
| W30-1.9 `36298e9` | -- (fixtures only) | -- | -- |
| W30-1.10 `5bbcf17` | app_qt exit 0 | `qt_inspect_gate` (new), 0 failures; `qt_ribbon_inventory` | app_qt 30/30 |
| W30-1.11 `3aa4549` | app_qt exit 0 | `qt_ribbon_inventory` 1.48 s | app_qt 30/30 |

G1 group gates (fresh trees configured from scratch on `3aa4549` under
`build/w30-fresh{,-app,-qt}`, built serially, log
`build/w30/W30-G1-fresh-gates.log`; the run started 17:36 and finished 17:50):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 / 266 checks passed, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 / 420 checks passed, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app ctest / app_qt ctest | 21/21 / 30/30 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (104 documented hashes, 140 tracked `.mpp`); paths OK (187 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `wave29..HEAD` / `guard_names wave29..HEAD` / hex grep | 0 files, 0 hits / 0 commits / exit 0 / no matches (exit 1, expected) |

The numbers belong to `3aa4549`; the marker commit that follows is docs-only.
The gate script's process status was 1 on this run because its final expected
hex grep returns 1; every gate line exited 0 (the log carries the note, and
the script now ends with an explicit exit 0).

## Found, not fixed

- **The reader cannot represent Project's inserted summary/milestone result.**
  `read_outline` walks FixedData row order and ignores the fractional
  Fixed2Data ordinal (2.5) and the reparenting, so Project's saved result reads
  with the new row last where Project reopens it at sheet row 2 (W30-1.1).
  `TaskRow` also carries no `Task.Active` (W30-1.2), so Inactivate cannot be
  enabled without a reader change.  A later insert or inactivate writer needs
  both.
- **`tests/save_pair_noise.h`'s fixed-block mask is bounded by the control's
  size.**  The insert pairs append their new rows beyond it, so those tails
  (which carry Project's per-session creation stamps) stay unmasked; a later
  writer needs the mask extended or the tails compared as effects.  The writer
  also never inserts or removes VarMeta entries and has no 214 view-stream
  writer at all: Move removes StartText/FinishText, Notes inserts a note
  entry, and the text style appends to `214/CV_iew/Var2Data`.  Each deferral
  above is blocked on one of these writers, not only on a scheduler.
- **Project 16.0.20430's own Add to Timeline command is disabled and inert**
  (UIA toggle, Select All, a fresh Project-created project, and the documented
  view repair).  The committed timeline pair is the negative baseline; a
  later wave needs a Project build where the command runs before a positive
  save shape can be measured.

WAVE-30-G1-COMPLETE

## G2 - the Resource tab's commands

| item | finding | status | evidence |
|---|---|---|---|
| W30-2.0 | Add Resources (work resource row - measure) | **DEFERRED (measured)** | commit `363645c`; pairs `wave30-resource-add-*`; gates `build/w30/W30-2.0-*.log` |
| W30-2.1 | Information and Notes (resource writes - measure) | **DEFERRED (measured)** | commit `eff96fd`; pairs `wave30-resource-rename-*`, `wave30-resource-notes-*`; gates `build/w30/W30-2.1-*.log` |
| W30-2.2 | Details (Resource Form - view) | **DONE (read-only view)** | commit `b2e957e`; gate `qt_resource_details_gate`; gates `build/w30/W30-2.2-*.log` |
| W30-2.3 | Assign Resources (one assign and one remove - measure) | **DEFERRED (measured)** | commit `f8086c0`; pairs `wave30-assign-add-*`, `wave30-assign-delete-*`; gates `build/w30/W30-2.3-gates.log` |
| W30-2.4 | Level All / Level Selection / Level Resource / Leveling Options / Clear Leveling / Next Overallocation | **DEFERRED (Level All measured)** | commit `8cdc56a`; pair `wave30-level-control` / `wave30-level-all`; gates `build/w30/W30-2.4-gates.log` |
| W30-2.5 | Resource Pool (shared pools - stays disabled) | **DONE (stays disabled, exact tooltip)** | commit `913f81b`; `qt_ribbon_inventory`; gates `build/w30/W30-2.5-gates.log` |
| W30-2.6 | Team Planner (a view - wave 31 or later) | **DONE (stays disabled, exact tooltip)** | commit `cd85fd3`; `qt_ribbon_inventory`; gates `build/w30/W30-2.6-gates.log` |

One item is DONE as a view (2.2's read-only Resource Form), two are the items
the brief names as staying disabled (2.5 Resource Pool and 2.6 Team Planner,
each with a named live check), and four are measured and deferred with a
committed Project pair: Add Resources, Resource Information and Notes, Assign
Resources (both directions) and Leveling.  No disabled button was enabled
without a measured pair; the Leveling and Assign Resources buttons that could
write stay disabled with decision 9's exact tooltip.  G2 added 18 tracked
`.mpp` fixtures (140 at G1's close, 158 at G2's) -- six measured edits, each
with a control, a no-edit null save and the edit -- and one new app_qt gate
target (`qt_resource_details_gate`).

W30-2.0 detail:

- Measured by `scripts/oracle_w30_resource_pairs.py` on the synthetic base:
  `Project.Resources.Add("R2")`, the Resource tab's Add Resources, saved before
  and after in one session.  COM reopens the edit with Resources.Count 1 -> 2
  (ID/UniqueID 2, Work, MaxUnits 1.0, no notes) and every task date, duration,
  percent, MANUAL flag and ID identical.
- The save carries 509 effect bytes under the save-pair mask.  `114/TBkndRsc`
  grows by one 172-byte FixedData row (392 -> 564: ID 2), a 37-byte FixedMeta
  entry (201 -> 238), a 40-byte Fixed2Data row (200 -> 240), a 51-byte
  Fixed2Meta entry (271 -> 322), five 12-byte VarMeta entries (108 -> 168) and
  54 Var2Data bytes (86 -> 140, including the name record).  `114/TBkndCal`
  grows a NEW RESOURCE CALENDAR row (FixedData 100 -> 112, FixedMeta 86 -> 96,
  Fixed2Data 336 -> 384, Fixed2Meta 86 -> 96).  `114/Props` takes three bytes
  (resource count 0x05 -> 0x06, the Rsc Var2Data size 0x0056 -> 0x008C, a
  calendar count 0x07 -> 0x08) and `214/CEdl/Var2Data` byte 543 changes
  0x00 -> 0x26.  Task, assignment, project and DocumentSummaryInformation
  streams have ZERO effects.
- DEFERRED: the port's `add_resource_row` writes only the six `TBkndRsc`
  streams and leaves every other stream byte-identical (`core/mppwrite/
  add_delete.h`), so its save would miss the resource-calendar row and the
  Props/CEdl count bytes.  The button stays disabled with the exact tooltip;
  the pair and its null save are committed as the oracle.

W30-2.1 detail:

- Measured by `scripts/oracle_w30_resource_pairs.py` on the W30-2.0 edit copy,
  in one session each.  Rename: `Resources(2).Name` `R2` -> `R2b`, the one-field
  shape behind the Resource tab's Information dialog.  Notes: a first
  `Resources(2).Notes = "Resource note line."`, the Notes tab of the same
  dialog.  COM reopens both with the same resource IDs and identical task
  dates, durations and percent; the edit reopens with the 19-character note.
- Rename effects: 16 bytes -- `114/TBkndRsc` Var2Data 140 -> 152 (one 12-byte
  name record appended), VarMeta unchanged in length with two effect bytes (the
  Var2Data size word at +20 and one entry offset at +112), and `114/Props`
  40278 0x008C -> 0x0098.  Every other stream and DocumentSummaryInformation
  has ZERO effects.
- Notes effects: 219 bytes -- `114/TBkndRsc` Var2Data 140 -> 332 by APPENDING
  one u32-length-prefixed 192-byte block, Project's RichEdit document
  (`{\rtf1\ansi\ansicpg1252\deff0\nouicompat\deflang1033{\fonttbl...}}` with
  the `Riched20 16.0.20430` generator, the text as `\pard\f0\fs20 Resource note
  line.\par` and a trailing NUL), not the plain text; VarMeta 168 -> 180 with
  the new 12-byte entry INSERTED among the existing ones (logical count 12 ->
  13, every later entry rewritten); the target's Rsc FixedMeta byte 203
  0x05 -> 0x06 and byte 236 0x00 -> 0x40; and `114/Props` 40278 0x008C ->
  0x014C.  No date, assignment, summary, link or calendar effect.
- DEFERRED: the port has no resource-field writer at all (`add_resource_row`
  only appends a whole row) and its writer never inserts or updates a VarMeta
  entry; the note's RichEdit escaping surface is also unmeasured.  Both the
  Information and Notes buttons stay disabled with the exact tooltip; the
  pairs and their null saves are committed as the oracle.

W30-2.2 detail:

- Details is a view, so it has no save path and no pair.  Project opens its
  Resource Form; this port docks a READ-ONLY Resource Form in the existing
  Details splitter and switches to the Resource Sheet, whose current row it
  follows by UID.  Every value comes from `app::PlanInput` (ID, Unique ID,
  name, type, initials, max units, the sum of the resource's assignment work
  and the assignment count); no field is editable and nothing is queued or
  written.
- The pane reuses the shared Details preference: exactly one form is visible
  per view (Task Sheet -> Task Form, Resource Sheet -> Resource Form), usage
  views show neither, and a freshly shown generic view starts with no current
  row so the pane cannot carry a stale selection.  Details is a SHOW command
  like the Task tab's (W30-1.5), not a toggle; the View check box stays the
  toggle.  The live button is enabled in `ribbon_bar.cpp` and `ribbon_pin.tsv`
  and wired to `RibbonCommand::resource_details`, with the enabled-wiring row
  and command name pinned in `ribbon_inventory_test.cpp`.
- `app_qt/tests/resource_details_gate_test.cpp` (new) runs on
  `wave23-outline.mpp`: the live button docks the pane, the form follows the
  Resource Sheet's selection, every field is read-only, switching to the Task
  Sheet shows the Task Form instead, a usage view shows neither, and the
  document stays clean.  0 failures.

W30-2.3 detail:

- Two measured pairs by `scripts/oracle_w30_resource_pairs.py` on the W30-2.0
  edit copy, the brief's one assign and one remove.
  `Task ID 1.Assignments.Add(TaskID=1, ResourceID=2)` creates assignment
  UniqueID 4 (task UniqueID 1, resource UID 2, Units 1.0, Work 2400 min,
  2026-01-04 13:00 -> 2026-01-09 12:00); COM reopens assignment count 0 -> 1
  and Resource ID 2 Work 0 -> 2400 min with every task fact identical.
  Deleting that assignment reopens assignment count 1 -> 0 and Work 2400 -> 0
  min with every task fact identical.
- Add effects: 1012 bytes.  `114/TBkndAssn` grows by one 110-byte FixedData
  row (330 -> 440), a 34-byte FixedMeta slot (118 -> 152; deleted-rows flag
  0x04 -> 0x06, live count 3 -> 4), a 48-byte Fixed2Data row (144 -> 192), a
  53-byte Fixed2Meta slot (175 -> 228), one 12-byte VarMeta entry (132 -> 144;
  the three uid 1 entries dropped, four uid 4 entries appended) and 124
  Var2Data bytes (396 -> 520).  Project TOMBSTONES task 1's placeholder row in
  the same save (slot 0 state 0x00 -> 0x02, type word 3 -> 0); the scheduler
  sets four 4-byte task assignment-pointer words (0 -> 0x41424F80) and three
  resource pointer words, rewrites Task FixedMeta 173 (0x07 -> 0x17) and 220
  (0x07 -> 0x97), Task Var2Data 3 bytes, Resource FixedMeta 140
  (0x01 -> 0x00), `114/Props` 5 bytes and DocumentSummaryInformation
  1100 -> 1104 (the project Work summary `0 hrs` -> `40 hrs`).
- Remove effects: 905 bytes.  Project does NOT shrink the table: FixedData
  grows 440 -> 550 by APPENDING a 110-byte placeholder row (UniqueID 5,
  resource word 0xFFFF0001) while the deleted row's bytes stay in place;
  FixedMeta keeps its 152 bytes with eight effects (flag, live count 3 -> 4,
  FixedData size, slot 2 state 0x00 -> 0x02 and type 4 -> 0, the spare slot's
  row offset 330 -> 440 and a state byte); Fixed2Data 192 -> 240; Fixed2Meta
  four effects (live count 3 -> 4, size, the spare record offset, a state
  byte); VarMeta thirteen effects (the Var2Data size 520 -> 644 and the
  deleted assignment's four uid 4 entries re-keyed to uid 5 with offsets
  +124); Var2Data 520 -> 644.  It CLEARS the same pointer words, changes Task
  FixedMeta 220 0x97 -> 0x87, Task Var2Data 3 bytes, `114/Props` 4 bytes and
  DocumentSummaryInformation 1104 -> 1100 (`40 hrs` -> `0 hrs`).
- DEFERRED: `add_assignment_row` appends the row and leaves every existing row
  byte-identical, and `delete_assignment_row` only tombstones the row's
  existing FixedMeta entry, so neither can write Project's placeholder
  tombstone/appended row, the dropped/re-keyed VarMeta entries, the pointer
  words or the Work summary.  Assign Resources stays disabled with the exact
  tooltip; both pairs and their null saves are committed as the oracle.

W30-2.4 detail:

- Measured by `scripts/oracle_w30_level.py`: a fresh AUTOMATIC project with
  three unlinked 2400/1440/960-tick tasks starting 2026-10-26 08:00 and one
  100 % Work resource assigned to all three (PeakUnits 3.0, Overallocated
  True), saved before and after `Application.LevelNow(All=True)`.  COM reopens
  task 1 unchanged (2026-10-26 08:00 -> 2026-10-30 17:00), task 2 moved to
  2026-11-02 08:00 -> 2026-11-04 17:00 with LevelingDelay 10080 min, task 3
  moved to 2026-11-05 08:00 -> 2026-11-06 17:00 with LevelingDelay 14400 min,
  assignment LevelingDelay 0, and Resource 1 PeakUnits 3.0 -> 1.0,
  Overallocated True -> False; every duration, MANUAL flag, ID and work value
  is identical.  Manual tasks are not levelled (a first attempt with manually
  scheduled rows produced no date movement), so the fixture uses automatic
  rows.
- The save carries 502 effect bytes: `114/TBkndTask` 50 FixedData bytes, 12
  FixedMeta bytes, 2 changed and 64 appended Var2Data bytes (436 -> 500), and
  189 VarMeta bytes with EIGHT new 12-byte entries (420 -> 516; count 33 -> 41
  -- one for the project row and two per task row); `114/TBkndAssn` 8
  FixedData bytes (the moved assignments' date words); `114/TBkndRsc` 2
  FixedData bytes (the PeakUnits double 3.0 -> 1.0) and one FixedMeta byte;
  `114/Props` 3 bytes at 390/3354/3386; `214/CFilter` ZERO effects (its
  identity bytes masked, the filter-state bytes inside the masked blocks);
  DocumentSummaryInformation 1100 -> 1104 (project finish Fri 30/10/26 -> Fri
  6/11/26, duration `5 days?` -> `10 days?`).
- The control had to be saved from a REOPENED seed: the first save after
  FileNew is not reopen-stable (its 230-byte `214/CFilter` block gains a
  second copy on the next save after a reopen), so the committed control, its
  null save and the edit share one 460-byte block and the pair's only
  unequal-length streams are Props14 and `214/CEdl/Var2Data`.
- DEFERRED: the save is a scheduler rewrite.  The port has no scheduler and no
  LevelingDelay/Preleveled/Overallocated field in `app::TaskRow`,
  `ResourceRow` or `AssignmentRow`, and no writer for those words.  Level
  Selection, Level Resource, Clear Leveling, Leveling Options and Next
  Overallocation have no measured save shape either, and Next Overallocation
  has no overallocated fact to navigate.  All six stay disabled with decision
  9's exact tooltip, pinned by a named `qt_ribbon_inventory` check; the pair
  and its null save are committed as the oracle.

W30-2.5 detail:

- The brief keeps Resource Pool disabled; no measurement was wanted and none
  was made.  A shared pool is a cross-file feature: a separate pool file plus
  a sharer table in each sharer.  This port's plan model is one file
  (`app::PlanInput` holds one resources and one assignments table, with no
  pool file name, GUID or sharer list), and its only assignment writer appends
  a row inside the same file (`mppwrite::add_assignment_row`); W30-2.3
  measured the in-file assign/remove pair, and nothing there reaches a pool.
- The control is unchanged: `ribbon_pin.tsv` line 92 carries enabled 0 for
  `assignments.pool`; `ribbon_bar.cpp:354-355` passes `MenuAction::none`, no
  RibbonCommand and enabled = false, so the disabled builder sets exactly
  "not implemented in this port"; no `RibbonCommand::resource_pool` and no
  `main_window` handler exist; the generated backlog row stays disabled with
  that reason.
- W30-2.5 adds a named live check in `qt_ribbon_inventory`: the Resource Pool
  button on the live Resource tab's Assignments group is present, disabled and
  carries exactly the decision-9 string.  No product code changed.

W30-2.6 detail:

- The brief puts Team Planner in wave 31 or later and keeps it disabled;
  nothing was measured and no save path was opened.  It is a whole view --
  resource lanes with assigned work laid out per day -- and the port has no
  view mode, widget or model behind it (`app::ViewMode` has only task_sheet,
  resource_sheet, task_usage and resource_usage; `app/view_model.h:73-80`).
- The command is disabled and unwired in every place it appears:
  `ribbon_pin.tsv` lines 89 and 146 carry enabled 0 for both the Resource tab /
  View and View tab / Resource Views buttons; `ribbon_bar.cpp:349-350` and
  464-465 pass `MenuAction::none`, no RibbonCommand and enabled = false; no
  `RibbonCommand::team_planner` and no `main_window` handler exist; the View
  menu entry and the status-bar view button are disabled the same way
  (`app/menu_model.cpp:134`, `main_window.cpp:245`); the backlog rows stay
  disabled with decision 9's reason.
- W30-2.6 adds a named live check in `qt_ribbon_inventory` for BOTH ribbon
  buttons: each is present, disabled and carries exactly "not implemented in
  this port".  No product code changed.

Per-commit gates (logs under `build/w30/`, named `W30-2.x-*.log`).  Every
commit's fixture log reports fixtures OK and every client-name scan reports
0 files / 0 hits with `guard_names` exit 0 and the hex sentinel grep exit 1 as
expected.  Fixtures-only commits changed no code and ran no build or ctest.

| commit | builds | item tests | ctest |
|---|---|---|---|
| W30-2.0 `363645c` | -- (fixtures only) | -- | -- |
| W30-2.1 `eff96fd` | -- (fixtures only) | -- | -- |
| W30-2.2 `b2e957e` | app_qt exit 0 | `qt_resource_details_gate` 0 failures; `qt_ribbon_inventory` | app_qt 31/31 (stale binary -- see Found, not fixed) |
| W30-2.3 `f8086c0` | -- (fixtures only) | -- | -- |
| W30-2.4 `8cdc56a` | app_qt exit 0 | `qt_ribbon_inventory`; `qt_split_view_gate` corrected | app_qt 31/31 |
| W30-2.5 `913f81b` | app_qt exit 0 | `qt_ribbon_inventory` 0.68 s, 0 failures | app_qt 31/31 |
| W30-2.6 `cd85fd3` | app_qt exit 0 | `qt_ribbon_inventory` 1.80 s, 0 failures | app_qt 31/31 |

G2 group gates (fresh trees configured from scratch on `cd85fd3` under
`build/w30-fresh{,-app,-qt}`, built serially, log
`build/w30/W30-G2-fresh-gates.log`; the run started 18:53 and finished 19:08
with no lock retries):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 / 266 checks passed, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 / 420 checks passed, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app ctest / app_qt ctest | 21/21 / 31/31 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (122 documented hashes, 158 tracked `.mpp`); paths OK (205 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `wave29..HEAD` / `guard_names wave29..HEAD` / hex grep | 0 files, 0 hits / 0 commits / exit 0 / no matches (exit 1, expected) |

The numbers belong to `cd85fd3`; the marker commit that follows is docs-only.

## Found, not fixed

- **W30-2.2's app_qt ctest result came from a stale binary.**  Its commit
  reports 31/31, but the tree it tested had been built before the Resource
  Form changed the Details splitter's pane count, so `qt_split_view_gate`
  still expected two panes and would have failed on a rebuild.  W30-2.4
  rebuilt the tree, found the failure, corrected the gate to expect the view
  pair plus one form per sheet, and the suite has been 31/31 on every rebuild
  since.  The lesson is procedural: after a change to a shared widget, ctest
  must run on the rebuilt tree, not the previous binary.
- **A fresh Project file's first save is not reopen-stable, and the save-pair
  mask cannot detect it.**  The first save after FileNew writes a 230-byte
  `214/CFilter` block; the next save after a reopen materialises a second
  copy.  W30-2.4's first control tripped the discipline's unequal-length rule
  (only Props14/CEdl may differ in length), which is the only gate that
  catches it.  `oracle_w30_level.py` now saves a SEED, reopens it and saves the
  control; future oracles that touch a fresh project should do the same before
  assuming a pair is stable.
- **The resource-write deferrals share writer gaps.**  Add Resources is
  blocked on a `TBkndCal` resource-calendar row plus three `114/Props` count
  words; Information and Notes are blocked on VarMeta entry insertion/updates
  (and Notes on Project's RichEdit serialisation); Assign Resources' remove
  direction is blocked on Project's append-and-re-key table growth.  No
  dialog layout is the blocker for any of them; a later wave needs those
  writers before any of these commands can be enabled.
WAVE-30-G2-COMPLETE
