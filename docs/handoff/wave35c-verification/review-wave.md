0C/0H/2M/3L

# Wave 35c whole-wave review: interactions between G0, G1 and G2 (`2a97272..edb3051`)

Independent, adversarial, read-only.  Nothing was built, run or edited in the repository;
every statement comes from reading the code, the tests and the records at `edb3051` (with
`git show` and `git diff`) and the per-group reviews and coordinator records in
`build/verify-w35c/`.  Line numbers are at `edb3051`.  The per-group findings (R35cG0-1..6,
R35cG1-1..7, R35cG2-1..8) are not repeated; where one bears on an interaction it is cited by
id.

Scope of this review: what a per-group review cannot see.  That is the order of the checks
across groups in `read_calculate_shape` and `calculate_plan_refusal`, whether a later group's
check shadows an earlier group's guard or test, the CLOSE id table against the code, the
warning classes MSVC cannot report, and whether the writer can now produce a file Project
reads differently.

## Findings

### R35cW-1 (Medium): no per-group finding is recorded in the repository, and START-HERE reads as "no High open"

- Where:
  - `docs/START-HERE.md:14-23` (the wave 35c bullet).
  - `docs/handoff/wave35c-complete.md`, the CLOSE "Found, not fixed" list.
  - `memory/coordinator.md` and `docs/handoff/plan-waves27-30.md` (no wave 35c state at all).
  - The brief's section 1 puts the coordinator's `review-*.md` and `coord-gates-*.md` in
    `docs/handoff/wave35c-verification/`.
- What is wrong:
  - `git grep "R35cG\|R35cW" edb3051` finds nothing.
  - The three reviews and the three coordinator gate records exist only in
    `build/verify-w35c/`, which `.gitignore:36` (`build/`) excludes.
  - `docs/handoff/wave35c-verification/` at `edb3051` holds the builder's `gates-g0.md` to
    `gates-g2.md` only.  Wave 35b committed its reviews to its verification folder.
  - START-HERE says the wave "fixed both Calculate Project Highs ... and the wave 35b Mediums
    and Lows", and lists R35bG0-9 and R35bG1-10 as the only ids still open.  It then gates the
    merge on "CI is green on all three OSs and no Critical or High is open".
  - At `edb3051` three Highs are open: R35cG0-1 (the untested `check_props_pin` branches,
    `core/mppwrite/calculate.cpp:281-284, 296-301, 318-321`; mutation P3 stayed green),
    R35cG1-1 and R35cG1-2 (Redo re-queues a withdrawn Sort or Show Level,
    `app_qt/main_window.cpp:6026-6033, 5012, 5095, 2286-2288`).  None of them is named in the
    repository.
  - `memory/coordinator.md` still calls R35bG0-1 and R35bG0-2 "Open Highs", and its "Also
    open" list still names R35bW-CI1, R35bG4-1..6, R35bW-1, R35bW-2 and R35bW-4..6, which this
    wave addressed.  START-HERE's wave 35b bullet (line 37) keeps the same "The queue also
    holds" list directly under the 35c bullet that says they were fixed.  This is R35cG2-8
    carried through CLOSE: CLOSE added the START-HERE line and nothing else.
- Failure scenario: a new session reads START-HERE first, as the file tells it to.  It sees
  wave 35c closed with only two unassigned Lows open, waits for CI to go green, and merges
  waves 34 to 35c with three open Highs.  The build folder is not on the other machine, so
  the reviews are not there to contradict it.
- Fix: commit the three reviews, the three coordinator records and this file into
  `docs/handoff/wave35c-verification/`.  Add the open R35c queue, Highs first, to START-HERE,
  `plan-waves27-30.md` and `memory/coordinator.md`.  Drop the wave 35b "queue also holds"
  ids that wave 35c closed.

### R35cW-2 (Medium): the CLOSE id table marks four ids "fixed" that the code at `edb3051` closes only in part

- Where: `docs/handoff/wave35c-complete.md`, the CLOSE "Ids" table, rows G1 (R35bG3-1,
  R35bW-1, R35bW-2) and G2 (R35bG2-2).  The same statuses are in the group rows and in the
  CLOSE commit message ("Every id ticked: fixed, partly fixed (R35bG1-3, R35bG2-6) or
  recorded").
- What the code shows:
  - **R35bW-2, "fixed `37e0e1f`".**
    - `apply_sort_choice` calls `clear_redo()` only inside `if (had_queued_sort)`
      (`app_qt/main_window.cpp:6026-6033`).
    - After Ctrl+Z withdraws the Sort, `had_queued_sort` is false, so a refused or cleared
      Sort keeps the redo entry.  Ctrl+Y (`:2286-2288`) re-queues it.  This is R35cG1-1.
  - **R35bW-1, "fixed `e439dd2`".**
    - Cut and Paste end with `after_cell_change()` (`:5012`, `:5095`) and never call
      `structural_.clear_redo()`.  The other cell paths clear it first (`:3527`, `:5244`).
    - So Ctrl+Z, a cut or pasted cell, then Ctrl+Y re-queues a Sort or Level over an unsaved
      edit.  This is R35cG1-2.
    - The row's own note ("Paste and Fill Down carry the guard, no test of their own") also
      fails the brief's red-then-green rule for Paste.  This is R35cG1-3, mutation Q10.
  - **R35bG2-2, "fixed `21b61d6`".**
    - `d8cfb6c` (2.7) put the scratch-existence refusal (`core/mppwrite/calculate.cpp:1011-1015`)
      in front of the chain (`:1016-1052`).  The squat test (`tests/roundtrip_calculate.cpp:1162-1186`)
      now stops there.
    - The pending-kind test (`:1144-1161`) is refused earlier still, by the 0.3 Props pin.
      Its comment says so.
    - No test reaches a refusal after the first append.  This is R35cG2-1.
  - **R35bG3-1, "fixed `c408a07`".**
    - The shared guard is not applied to a session Level chosen under a queued Sort, or to a
      session Sort chosen under a queued Level (`apply_outline_level`, `apply_sort_choice`).
    - Its Sort branch's outline refusals are untested.  This is R35cG1-4.
- What is wrong: the brief's rule, which R35bW-5 restated and 2.25 applied to the wave 35b
  table, is that "fixed" means fully closed.  An id closed in part is "partly fixed: <what
  remains>".  The wave 35c table breaks that rule in four rows.  The builder could not see
  the reviews: they sit in the ignored build folder.  Even so, at `edb3051` the record
  misstates the code.
- Failure scenario: the next fix wave's brief is drawn from the CLOSE table.  It takes
  R35bW-1, R35bW-2 and R35bG2-2 as done and never re-tests the Undo and Paste routes or the
  scratch chain.
- Fix: mark the four rows "partly fixed" and name the remainder by R35c id: R35cG1-1;
  R35cG1-2 and R35cG1-3; R35cG2-1; R35cG1-4.

### R35cW-3 (Low): G2's 2.2 closes the writer half of R35cG0-2, and no record says so; a test comment G1 made stale

- Where:
  - `core/mppwrite/calculate.cpp:753-761`: the 2.2 check, `+104 == +96` and `+108 == +100` on
    each leaf.  It runs before the 0.2 check at `:778-790`.
  - `app/edit_model.cpp:3785, 3871-3883`: the plan compares `task.start`, which is +104
    (`core/mpp/reader.cpp:585, 1995`).
  - `app/app_edits_calculate_tests.cpp:1206-1209`.
- What is wrong:
  - R35cG0-2 (G0 review) found the two layers' 0.2 checks on different Start words: the
    writer on +96, the plan on +104.  Its failure was that `save_calculate` alone accepts a
    file whose +96 is right and whose +104 is stale.
  - Since `ecc51d1` (2.2) the writer refuses that file at `:754` ("second date copy
    (+104/+108) differs").  So both words are now guarded in the writer.  The same
    equality makes every plan check that reads +104/+108 agree with the writer's +96/+100
    check: the leaf start, the tie, the shorten and critical-path checks, the first-leaf
    check, and the 0.4, 0.2 and 1.5 checks.
  - Nothing records it:
    - the 2.2 row says only "fixed `ecc51d1`";
    - R35cG0-2 appears in no committed record (R35cW-1);
    - calculate.md section 8 still describes 0.2 as "both layers" with no mention that the
      writer's half rests on 2.2's equality.
  - The plan half of R35cG0-2 (the plan never reads +96) is unchanged.
  - Separately, the comment at `app_edits_calculate_tests.cpp:1206-1209` ("the plan does not
    carry either, so the builder's only gate is the writer's dry run") has been false since
    1.7 (`b9588b7`).  The plan now pins FixedMeta byte 16 through `fixed_meta_entry`
    (`edit_model.cpp:3745-3751`).  The test still exercises the writer, because it passes
    the clean plan.
- Failure scenario: the next wave takes R35cG0-2 as open and adds a second +104 check.  Or a
  later edit drops 2.2 as "a Low" without knowing that 0.2's two-word coverage depends on it.
- Fix:
  - Record that R35cG0-2's writer half is closed by `ecc51d1`, and leave the plan half open.
  - Add one line to calculate.md section 8 saying that 0.2's writer check covers +104
    through 2.2.
  - Correct the test comment.

### R35cW-4 (Low): the view-change guard of 1.1 and 1.3 does not cover a queued Calculate Project, and Redo re-queues Calculate without `push()`

- Where:
  - `app_qt/main_window.cpp:1616-1626`: `view_change_refusal` checks `show_level` and
    `sort_view` only.
  - `:741`: the cell edit gate.
  - `:3389-3415`: `calculate_project` queues through `make_calculate_edit` and `push()`, with
    no `plan_unsaved_edits`.
  - `:2286-2288`: Redo re-appends without `push()`.
  - Cut and Paste at `:5012` and `:5095` (R35cG1-2).
- What is wrong:
  - G1 designed "one guard over every change ... while a Show Level or Sort is queued".
    G0's Calculate Project is the third view-affecting queued edit (the rebuild shows its
    slack and late dates), and it was left out.
  - With Calculate queued, a grid Duration edit, Cut or Paste is accepted.
  - After Ctrl+Z on Calculate and a Cut or Paste, Ctrl+Y re-queues the Calculate edit without
    `push()`.  So `calculate_plan_refusal` and `calculate_slack_refusal` (the 0.2, 1.5, 1.11
    and 2.10 plan guards) do not run on the plan as it now is.
- Why only Low:
  - The save ties the edit to the file (`app/edit_model.cpp:6652`, `source_digest`) and
    re-validates against the file (`:6677`).
  - Duration grid edits are session-only (Save writes structural, start and document edits
    only, `main_window.cpp:3622-3640`).
  - So the file written is the measured calculation of the file's own rows.  The defect is
    the display (durations and slack the file does not carry), not the file.
  - Not run.  This is by reading.
- Fix: add a `calculate_project` branch to `view_change_refusal` ("a Calculate Project is
  queued ..."), or record in calculate.md section 8 why Calculate needs none.  Clear redo
  after Cut and Paste as R35cG1-2 asks, which also covers this route.

### R35cW-5 (Low): the 0.3 Props pin runs before the W35b-0.5, 2.2, 2.9 and start-anchor checks, so for every real file other than the control the writer names the wrong reason

- Where:
  - `core/mppwrite/calculate.cpp:824` (`check_props_pin`) runs before `:826`
    (`check_unvaried_parameters`).  That function holds the resource-table, calendar,
    VarMeta-key, Active (2.9) and FixedMeta (2.2 bytes 8-11) checks.
  - The start-anchor loop is at `:827-834`.
- What is wrong:
  - R35cG0-3 shows the pin also fixes per-file identity records (title, creation stamp).  So
    every Project-saved file except the control and its copies fails the pin.
  - With the pin earlier in the order, the core writer refuses all such files with "114/Props
    record ... differs ...; the project's calculation options and defaults are stored in
    these records".  It never reaches the checks after it.
  - The W35b-0.5 parameter checks, 2.2's FixedMeta bytes 8-11, 2.9's Fixed2Meta pairing and
    the start-anchor check are therefore reachable only by synthetic mutations of the
    control.
  - Through the app, the plan layer names most of them first with its own text, so the
    effect is on `mppwrite::save_calculate` callers and the core tests' coverage claims.  It
    is not on users.
- Failure scenario: a core caller saves a real two-leaf pending project that has a resource
  on the Resource Sheet.  The refusal blames calculation options, not the resource row.  A
  future relaxation of the pin (the measuring wave R35cG0-3 proposes) would expose those
  later guards to real files for the first time.  Their tests have only ever seen the control.
- Fix: move `check_props_pin(props)` to the end of `read_calculate_shape`, after the
  start-anchor loop.  It stays a refusal and keeps its own tests.  Or record that the pin
  intentionally masks the later reasons.

## Checked and clean (by reading)

1. **G2 checks inserted before G0 and G1 guards (item 1).**
   - G2 put 2.3 (row-0 deadline and constraint, `calculate.cpp:565-572`) and 2.2
     (`:753-770`) ahead of 0.2 (`:778`), 1.5 (`:799`) and the Props pin (`:824`).
   - Every 0.2, 1.5 and 0.3 test mutates only what its own guard reads:
     - the Props start alone (`tests/roundtrip_calculate.cpp:747-775`);
     - +74 alone (`:776-799`);
     - one Props payload, kind or tail byte (`:800-881`).
   - Each of those files passes 2.2 and 2.3, so each guard is still the first to refuse it.
   - In the plan, 2.10 (`edit_model.cpp:3726`) runs before 1.7 (`:3745-3756`).  The 1.7 tests
     keep reader-built rows (`type_block_known` true), so they still reach 1.7.
   - The two are independent: 1.7 alone already refuses a default-built row (empty entry,
     `calculate_leaf_meta_mismatch` returns 0).  So 2.10's guard is redundant, as recorded
     ("defence in depth").  Its test stays specific through its needle.
   - The coordinator's mutations ran on each group's own head only.  Re-running A1, W1,
     P1-P4, E1-E5 and W1-W4 at `edb3051` would confirm this reading.  It is not a finding.
2. **Refusal sets of the two layers (item 2).**
   - The writer refuses, and the plan does not:
     - the Props pin (recorded as writer-only, R35cG0-4);
     - 2.3 and 2.2's +104/+108/+88 (recorded writer-only in the calculate.md table);
     - the pre-existing pending-flag, overtime, no-move and start-anchor checks.
   - The plan refuses, and the writer does not: nothing beyond the session-only fields.
   - Same set through `make_calculate_edit`, which runs both.  `push()` alone sees the plan
     half, as recorded.
   - No test needle moved to an earlier refusal, except the pending-kind test now hitting
     the Props pin (R35cG2-1, already raised).
3. **CLOSE id table (item 3).**
   - Every one of the 46 wave 35b ids in `wave35b-verification/` is in the table.
   - Spot-checked against code and correct:
     - R35bG0-1 (`calculate.cpp:778-790`, `edit_model.cpp:3871-3883`);
     - R35bG0-2 in source (`calculate.cpp:278-351, 824`);
     - R35bG0-3 (`:799-822`, `edit_model.cpp:3891-3911`);
     - R35bG0-8 (`:565-572`);
     - R35bG1-1 (`viewstate_sort.cpp:501-510`);
     - R35bG1-4 (`viewstate_outline.cpp:470-475`);
     - R35bG2-7 (`calculate.cpp:1011`);
     - R35bG4-3 (`:484-485`);
     - R35bG4-6 (`app_edits_calculate_tests.cpp:633-646`).
   - Wrong: the four in R35cW-2.
4. **Non-MSVC warnings (item 4).**
   - Swept the added lines for:
     - a reference bound to a call result (only `PropsPinRecord const& want = kPropsPin[index]`,
       an array element);
     - range-for over a call result;
     - lambdas with unused explicit captures;
     - sign compares (`date32` is `std::uint32_t`; `outline_level` and `kMeasuredPlanLevels`
       are both `int`);
     - `-Wswitch` holes;
     - format strings (`%08X` with `std::uint32_t`).
   - None found.
   - `unique_edl_item`'s remaining reference parameters (`items`, `function`) are lvalues at
     all three calls (`viewstate_outline.cpp:438, 445, 452`).
   - The functions added this wave that return references (`calculate_leaf_var_keys`) take no
     parameters.
   - `app/main.mm` is untouched in the range, and no signature it calls changed.  The
     `require_measured_view_block` default parameter is additive.
   - CI is still the only proof.
5. **A file Project reads differently (item 5).**
   - No writer's output path changed in the range:
     - `save_calculate`'s writes (`:949-1052`) are as in wave 35b, with only the scratch
       existence check added;
     - the view writers changed only in refusals;
     - the assignment growth change is a refusal.
   - Fixed2Data stamps go to the row's own record (`fixed2_record_offset(row)`), which is what
     a Fixed2Meta entry names (`task_mode.cpp:577-582`).  So 2.9's index change moved only
     which Active bit is read, not where the writer writes.
   - Every acceptance change is a narrowing:
     - 0.2, 0.3, 1.5, 1.7, 1.11, 2.2, 2.3 and 2.10 add refusals;
     - 2.12 narrows the second view-block digest to the six-task outline.
   - The one unmeasured acceptance left in the range is R35cG2-4's (already raised).
