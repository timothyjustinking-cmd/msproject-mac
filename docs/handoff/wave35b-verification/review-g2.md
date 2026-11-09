# Wave 35b G2 review: the Calculate Project and assignment Mediums (items 2.1 to 2.12)

- Range: `git diff e3c86fe 733d6d3 -- . ':!docs/handoff/wave35b-verification'` (commits a69943c,
  4a491df, 38a6c92, eb5aa96, 5284d58, e860427, 93b9524, 4315ef7, b304545, 06eff3f, 96d1cd4,
  b179918, marker 733d6d3; 20cdeff ignored).
- Date: 9 November 2026.
- Method: independent, adversarial, read-only.  Source read at 733d6d3 with `git show`; no build, no
  test run, no COM, no fixture parsed.  The only file outside git read is the oracle JSON
  `build/w35/W35-1.2-calculate.json` (grep only), to check the 2.8 record.  "Confirmed" below means
  confirmed by reading the code path, not by a failing run.  Governing text: `docs/handoff/wave35b-brief.md`
  section 1 and "### G2", `docs/handoff/standing-rules.md` rule 1, and the cited findings in
  `docs/handoff/wave35-verification/` (review-g0 R35G0-2/3/4, review-g1 R35G1-4 to R35G1-9, review-g3
  R35G3-6).  R35bG0-1 and R35bG0-2 are known and not re-reported.  Line numbers are in the 733d6d3 tree.

## Counts

| Severity | Count |
|---|---|
| Critical | 0 |
| High | 0 |
| Medium | 1 |
| Low | 7 |

No Critical or High.  All twelve items are closed for the shape the brief names.  The one Medium is
the "both layers" pattern already raised as R35bG1-3: the two new Calculate shape refusals (2.7, and
2.3) live only in the writer's `read_calculate_shape`, which the edit model calls; the plan-side rule
still accepts a tie.

## Findings

### R35bG2-1 (Medium): 2.7's tie and swapped-order refusals exist only in the writer's shape check; the plan-side rule accepts a tie

- Status: confirmed by reading.
- Where:
  - `core/mppwrite/calculate.cpp:371-376` (tie) and `:475-480` (critical leaf not row 1), both in
    `read_calculate_shape`.
  - `app/edit_model.cpp:3604-3735` (`calculate_plan_refusal`).  Its critical-path loop
    (`:3706-3715`) skips every task whose finish equals `latest`, so two leaves that finish together
    pass.  Nothing in it compares the critical leaf's position with `plan.tasks[0]`.
  - `app/edit_model.cpp:4289-4303`: `push()` for `calculate_project` checks only
    `calculate_plan_refusal` and the slack count.
- What is wrong:
  - Brief section 1 and standing rule 1 ask for the refusal in each layer: the edit-model builder and
    `push()`, and the writer.
  - For 2.7 the edit model refuses only because `make_calculate_edit` calls the writer's
    `read_calculate_shape` (`edit_model.cpp:3758`).  A hand-built edit on a tie plan passes `push()`.
  - The plan already holds the data for both checks: the task finishes and their order.  So the
    edit-model half is cheap to add, unlike 2.3, where the plan has no field for the overtime records.
  - 2.3 has the same one-guard shape.  Both layers read the same lines, `calculate.cpp:408-421`.
- Why it matters:
  - Removing either `calculate.cpp` guard turns both layers green.  The app tests go red, but only
    because the writer goes red.
  - Reach is limited.  At save, W35b-2.2's digest refuses any hand-built edit (digest 0,
    `edit_model.cpp:6416-6422`), and `validate_calculate` re-runs the shape check.  No file is
    written wrongly.
- Suggested fix:
  - In `calculate_plan_refusal`, refuse two leaves with equal `finish`, and a latest-finishing leaf
    that is not `plan.tasks[0]`, each with its own needle.
  - Add plan-only app tests (plan mutated, file untouched), so that the needle can only come from the
    edit model.
  - For 2.3, resolve together with R35bG1-3: either record shared-helper-as-mirror as accepted, or add
    a reader flag on `PlanInput`.

### R35bG2-2 (Low): the scratch-file chain in 2.1 is untested, and one record the chain touches is still checked only after the appends

- Status: confirmed by reading.
- Where:
  - `core/mppwrite/calculate.cpp:665-709` (scratch chain).
  - `:390-401` (the shape check reads `0x02400024` for length 2 and value 01 00, not its kind).
  - `core/mppwrite/props_edit.cpp:368-373` (refuses a kind change).
  - Tests: `tests/roundtrip_calculate.cpp:727-764`.
- What is wrong:
  - Both new atomicity tests remove a Props record that `read_calculate_shape` now refuses up front
    (`:425-441`).  So they never reach the scratch chain.  With the `.calculate.tmp` indirection
    reverted, and `save_var_record` and `save_props_edit` writing `path` directly, both stay green.
  - The commit's red run reverted both changes together.
  - A `0x02400024` record of length 2 but a kind other than `kPropsRecordKindFlag` is still refused
    only by `save_props_edit`, after the five appends.  The scratch file protects the target, so the
    file is safe.  But the command queues, the save fails late, and that is the only post-append
    refusal left.
  - R35G1-4's fix also asked for "a test that plants a refusal after the first append against an
    existing target".
- Suggested fix:
  - Check the kind of `0x02400024` in `read_calculate_shape`.
  - Before adding that check, write a test with an existing target and a pending record of another
    kind.  Today that case reaches `save_props_edit` after the appends.  Assert the target is
    byte-identical and no scratch file is left, and show it red with the scratch indirection
    reverted.  Then add the up-front check and its own needle.

### R35bG2-3 (Low): the 2.9 edit-model refusal drops the decision-9 text and normalises the name differently from the writer

- Status: confirmed by reading.
- Where: `app/edit_model.cpp:2983-2999`; `core/mpp/reader.cpp:4070-4076`;
  `core/mppwrite/assignment_growth.cpp:798-805, 910-933`.
- What is wrong:
  - The text:
    - Every other return in `assignment_plan_refusal` reads `what + " ... is not implemented in this
      port: ..."`.
    - The two new returns read `what + ": the project's default calendar ..."`.  The user sees, for
      example, "Assign Resources: the project's default calendar is not the Standard base
      calendar ...", with no decision-9 wording.
    - Through the dry run, the same shape gets the "... is not implemented in this port for this
      project:" prefix.  So the wording now depends on which layer fires.
  - The name comparison:
    - The reader space-trims the Props name (`trim_ascii_spaces`).  The writer strips only trailing
      UTF-16 NULs and compares bytes.
    - So the layers can disagree on a name with a trailing space.  The writer still refuses through
      the dry run, so no file is mis-written.
  - Commit b304545 says the edit model "refuses with the writer's needles"; that is true only of the
    needle substrings.
- Suggested fix: phrase both as "`what` on a project whose default calendar is not ... is not
  implemented in this port: ...", keeping the current needle substring.  Compare the untrimmed name,
  or trim the same way in both layers.

### R35bG2-4 (Low): eleven of the thirteen commits lack the wave's `Generated-By` trailer

- Status: confirmed by reading (`git log --format=%B`).
- Where: 38a6c92, eb5aa96, 5284d58, e860427, 93b9524, 4315ef7, b304545, 06eff3f, 96d1cd4, b179918 and
  733d6d3 carry only `Co-Authored-By`.  a69943c and 4a491df carry both.
- What is wrong: brief section 1 requires the trailer `Generated-By: Claude Sonnet 5.5 via Claude Code
  (wave 35b)` on every commit.  G1's nine commits all carried it.
- Suggested fix: no history rewrite (brief section 0).  Record the omission in the wave record, and
  restore the trailer from G3 on.

### R35bG2-5 (Low): the writer still trusts the non-critical leaf's slack and late start; the record says it cannot check them

- Status: confirmed by reading.
- Where:
  - `core/mppwrite/calculate.cpp:518-578` (`check_request`).
  - `spec/writers/calculate.md:194-195` ("The writer cannot recompute the per-leaf slack (it has no
    calendar)").
  - The same reasoning appears in `app/edit_model.cpp:6411-6412`.
- What is wrong:
  - For the leaf that does not end the project, `check_request` takes any `late_start` at or after its
    early start, and any non-negative slack, as long as its three slacks are equal.
  - The writer pins the calendar: `check_unvaried_parameters` requires the Standard calendar alone, at
    `:242-261`.  It also owns `calculate_measured_finish` for that calendar, at `:31`.  So it could
    recompute both values: late start = project finish less the duration, and slack = working time
    from the leaf's finish to the project finish.
  - Today only the app's digest (2.2) ties the slack to the file.  A direct writer caller with a wrong
    engine would write wrong slack.
- Why Low: the only caller is the edit model, and its digest closes R35G1-5's scenario (stale slack).
  The brief's 2.2 minimum (late finish = project finish; critical slack 0) is met.
- Suggested fix: add a `calculate_measured_work_between` / `calculate_measured_sub` helper and pin the
  non-critical leaf's late start and slack in `check_request`.  Or correct the spec and comment to say
  "does not" rather than "cannot".

### R35bG2-6 (Low): the old "changed-file" app test now stops at the digest and asserts no needle; the cascade's save-time `validate_calculate` is no longer reached by any test

- Status: confirmed by reading.
- Where: `app/app_edits_calculate_tests.cpp:1063-1084`; `app/edit_model.cpp:6415-6437`.
- What is wrong:
  - The test saves an edit built from the control against Project's calculated file.  Before 2.2 it
    reached the dry run ("not pending").
  - Now the digest refuses it first.  It checks only `!saved.ok` and the byte-identical target, so
    either refusal passes.
  - With the digest check in front, `validate_calculate(document, request)` at `:6436` can fire only
    on an edit whose fields were altered after it was built, and no test does that.
- Suggested fix: assert "has changed since Calculate Project was queued" in that test.  Add one case
  that alters a built edit's `restored_finish` (digest intact) and expects the writer's "is not the
  latest task finish".  Or record that the save-time dry run is defence in depth only.

### R35bG2-7 (Low): `save_calculate` deletes any pre-existing `<target>.calculate.tmp` without asking

- Status: confirmed by reading.
- Where: `core/mppwrite/calculate.cpp:665-667`.
- What is wrong: `std::filesystem::remove(scratch, ignored)` runs before the chain.  A user file with
  that exact name beside the target is removed silently, even when the save is then refused.  The
  cascade's own `.w51-structural-tmp` has the same pattern, so this is consistent but new on this
  path.
- Suggested fix: use a unique name (for example the process id plus a counter), or refuse when the
  scratch path already exists.

### R35bG2-8 (Low, suspected): the viewstate README rows keep "ProjectStart 2026-01-05 08:00" although their oracle passes the same naive datetime

- Status: suspected (no viewstate COM facts with a project start were found to confirm).
- Where: `tests/data/README.md` (the `wave35-viewstate-*` rows); `scripts/oracle_w35_viewstate.py:230`
  (`project.ProjectStart = dt.datetime(2026, 1, 5, 8, 0)`).
- What is wrong: 2.8 corrected only the four `wave35-calculate-*` rows, which is the brief's scope.
  The viewstate oracle uses the same naive-datetime call that shifted the calculate dates by the host
  offset, so its rows probably record a value the fixtures do not hold.
- Suggested fix: confirm from the viewstate fixtures' Props `0x02400002` (read-only), and correct the
  rows the same way in a later record commit.

## Per-item closure

| Item | Verdict | Evidence at 733d6d3 |
|---|---|---|
| 2.1 (R35G1-4) | Closed | `0x02400003` (kind and length) and `0x02400002` are checked in `read_calculate_shape` (`calculate.cpp:425-442`), so before any write and at command time.  The chain runs on a private `.calculate.tmp` (`:665-709`).  `path` is written once by `CfbDocument::save`, which is itself temp-plus-rename (`cfb_writer.cpp:1294-1318`).  So no path writes the target and then fails.  The test proves the existing target byte-identical (`roundtrip_calculate.cpp:727-764`).  Test gap: R35bG2-2. |
| 2.2 (R35G1-5) | Closed | `check_request` enforces late finish = project finish for every leaf, and critical slack 0 with late start = early start (`calculate.cpp:559-569`).  `source_digest` (FNV-1a 64 over the re-emitted container) is set at build (`edit_model.cpp:3829`) and compared at save against a fresh read of `source_path` (`:5803-5804`, `:6415-6422`).  Calculate is exclusive, so `document` is untouched when compared.  A changed file, or an edit with no digest, is refused.  It cannot be fooled short of a deliberate 64-bit collision or a hand-copied digest.  The stale-slack test would go red without the digest, because the writer accepts the old slack.  Residual: R35bG2-5. |
| 2.3 (R35G1-6) | Closed (writer guard shared by both layers; see R35bG2-1) | `calculate.cpp:408-421` has its own message ("has been calculated before").  It runs in `make_calculate_edit` at command time.  It sits before the "already equals" guard, so the tests (core `:589-607`, app `:626-661`) reach it: with it removed they get "already equals the latest task finish". |
| 2.4 (R35G1-7) | Closed | Each listed guard has a mutation whose needle exists only at that guard.  With the guard removed, a later guard fires with other words or the save succeeds (checked per guard): row 0 uid; FixedMeta +17/+18 (then `check_unvaried_parameters` fires); missing `0x02400024`; "already equals"; start anchor; late before early (then the new 2.2 guards fire); late finish after the project finish (then "before the project finish"); missing `0x02400002`; row count (app needle now specific, `app_edits_calculate_tests.cpp:671-674`). |
| 2.5 (R35G1-8, RW35-R5) | Closed | App loop: "for a project with links".  Qt linked: the same.  Plan constraint: "for a task with a constraint".  New app SNET-only file, clean plan: "carries a constraint", which only the writer's `calculate.cpp:342-346` emits.  The core synthetic constraint case already existed (`roundtrip_calculate.cpp:391-397`). |
| 2.6 (M5 on G1) | Closed | `qt_calculate_gate` runs `save_pair::compare_all` over every stream of its save, against Project's pair and the null save, with the app test's eight declared streams (`calculate_gate_test.cpp:210-235`).  The 0x30-to-0x28 stamp change shows in Fixed2Data outside the mask (commit e860427 quotes the red line).  Exceptions turn into a failed check, not a skip. |
| 2.7 (RW35-W3) | Closed in the writer; edit model only through the shared shape check (R35bG2-1) | Tie (`calculate.cpp:371-376`) and critical leaf not row 1 (`:475-480`).  Measured unlinked pair: the critical leaf is row 1 and the finishes differ, so it is not refused.  The tests reach their guards: the tie with the guard removed gets "moves the critical path"; the swap with the guard removed gets "ends the project, so its slack is 0". |
| 2.8 (R35G1-9) | Closed | Record only.  No COM, no `corpus/` change and no blob change (`git diff --stat` on `corpus/`, `tests/data/` touches only README.md).  The values match the oracle JSON: `project_start` "2026-01-04 21:00:00" (20 hits), `constraint_date` "2026-01-25 21:00:00", leaves start "2026-01-05 08:00:00".  The script passed `datetime(2026,1,26,8,0)` for the SNET date, consistent with the 11-hour shift.  Props `0x02400002` holding that value was not checked from the bytes. |
| 2.9 (R35G0-2) | Closed | Reader, adapter and `PlanInput` carry the Props name and week.  `assignment_plan_refusal` (`edit_model.cpp:2978-2999`) is used by the builder and `push()`.  Plan-only mutations (`app_edits_assignment_tests.cpp:671-720`) can be refused only by the edit model, because the file is untouched.  The measured add and removal fixtures are asserted to carry Standard and the measured week, so no measured shape is lost.  Wording: R35bG2-3. |
| 2.10 (R35G0-3) | Closed | `check_calendar_files` mutates the file, reads it back through the reader and the adapter, and asserts `calendar_base_extra_blocks == 1` directly.  So mutation M5 goes red on the reader-field check as well as on the edit-model needle.  That needle ("has working-time changes or exceptions") differs from the writer's dry-run text. |
| 2.11 (R35G0-4) | Closed (writer; the edit model already refuses every assign on a task with a real assignment, so its layer holds the whole shape) | `check_measured_resource` runs for every already-assigned uid (`assignment_growth.cpp:1681-1686`).  The copied rows' +52/+56/+60/+104 equal the task's Start2/Finish2 (`:1687-1712`).  Six tests damage only the already-assigned resource or row, each with that check's needle.  The measured second and third pairs are reported still saving.  Effort-driven is recorded as unproven in the comment. |
| 2.12 (R35G3-6) | Closed | `check_port_saved_removal` asserts the dry-run prefix at position 0 and the writer's specific needles for both fixtures: "(rows - 1) / rows / flag 4 settled shape" and "byte 12 is 0x5C".  With the settled-shape gate removed, the port's-own-save case gets another guard's words. |

## Clean

Checked and found sound, by reading:

- **Atomicity (2.1)**
  - Every throw between the first write and `reread.save(path)` is inside the `try`, and the `catch`
    removes only the scratch file.
  - `doc` is replaced only after the target write succeeds.
  - The success test compares the target with `doc.to_bytes()` and checks that no scratch file is
    left.
- **Request check (2.2)**
  - The new guards come after the existing "late before early" and "after the project finish"
    guards, so each existing needle still fires first for its own case.
  - The critical-leaf check uses the row's own `kStart`.
- **Digest scope (2.2)**
  - The digest covers the whole container, including Props and the calendar streams that
    `load_project_calendar` reads.
  - The Qt and macOS shells move the built `StructuralEdit` into the queue whole
    (`main_window.cpp:3347`, `main.mm:3586`), so the digest is not lost on the way to save.
- **Guard order**
  - The 2.3 check sits after "not pending", so Project's own result still reports "not pending".
  - The 2.7 tie check sits before the critical pick, so a tie never reaches the critical-path guard.
- **UB and overflow**
  - `get_u32(props, finish_at - 4)` has `finish_at >= 12` whenever the record is found.
  - The tie check indexes rows 1 and 2 after `rows.rows() == 3` is enforced.
  - The reader's new Props decodes take payload ranges already bounds-checked by
    `for_each_props_record` (`reader.cpp:4026-4028`).
  - The 2.11 date-word reads use rows and offsets the existing loop already uses; `kAssnRowStart3`
    (104) is within the 110-byte row.
  - No read after clear, no uninitialised read and no signed overflow on the new paths.
  - FNV arithmetic is unsigned.
- **GCC/Clang classes**
  - No new enum switch.
  - No sign compare in the added code: `size_t` and `uint32` mixes are widened, not compared signed.
  - No unused variable: `d2` and `f2` are used in both 2.7 tests.
  - `<fstream>`, `<iterator>`, `<array>`, `<exception>` and `<vector>` are added where used.
  - `measured_default_week_record` is declared in the header with `<vector>`.
- **Hygiene**
  - No client, company or people's names.
  - No personal or company paths in code, comments, test data or commit messages.  "The operator's"
    in the commit messages names no one.
  - No new `.mpp` blob.  Test files are built under the test out dir and removed.
  - The UTC offset (11 hours) is now written in the README and the spec.  The brief asked for it to be
    recorded.
- **Commit claims**
  - a69943c's "written once and atomically" holds, through `CfbDocument::save`'s temp-plus-rename.
  - eb5aa96's note that the FixedMeta +17/+18 guard is backed by `check_unvaried_parameters` is
    accurate.
  - b179918's "no source changed" is accurate.
  - The one wording overreach is b304545's "writer's needles" (R35bG2-3).
