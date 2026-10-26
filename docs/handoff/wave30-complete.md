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
