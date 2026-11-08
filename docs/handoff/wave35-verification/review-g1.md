# Review: wave 35 group G1, Calculate Project stage 1

- Range: `b8238bb..ec78e7a` on `wave35`, excluding `a7272d4` (coordinator record). Commits reviewed: 18cfb08, 15d8bb5, 2f0ae13, 82a7d92, 1d04674, 1bd09ee, ec78e7a.
- Date: 9 November 2026.
- Method: read-only. `git diff`/`show` in `D:\dev\msproject-mac`, source read in `D:\dev\msproject-mac-wt-v34g3` (HEAD ec78e7a). No build, no ctest, no COM. Read-only checks run: SHA-256 of the 12 fixtures against `scripts/check_fixtures.py` (all 12 match); UTF-8 and NUL-stripped UTF-16 scans of the 12 fixtures for names, hosts and paths (0 hits; the only embedded paths are `C:\w35\fixtures-calculate*\...`); the oracle's COM facts `build/w35/W35-1.2-calculate.json` (read only).
- Line numbers are in the ec78e7a tree.

Counts: Critical 0, High 3, Medium 6, Low 5.

## Findings

### R35G1-1  High  Deadline not refused in either layer; slack and late dates written wrong

- Where: `core/mppwrite/calculate.cpp:117-207` (`read_calculate_shape`), `core/mppwrite/task_mode.cpp:438-512` (`check_task_row`, reused through `read_task_file_shape`), `app/edit_model.cpp:3238-3277` (`calculate_plan_refusal`).
- Defect: no check of `task_row::kDeadline` (+182) anywhere on the Calculate path. `app::TaskRow` has no deadline field, so the plan side cannot see it, and neither file-side check reads it. The port's own outline save already refuses a deadline for this exact reason (`app/edit_model.cpp:1069-1077`: "no Project save pair measures the automatic outline save's slack and late dates with a deadline").
- Failure: a control with a Deadline on Task B before the project finish (for example 9 January 2026), pending duration change on A. Both layers accept it. `make_calculate_edit` gives B late finish = project finish and slack = working time to the project finish (7 days). Project's backward pass limits B's late finish by the deadline, so its total slack is smaller. The port writes a file whose slack and late dates differ from Project's; Project opens it without complaint (manual calculation shows the stored words). Confirmed against source (absence of the check); Project's bytes for this shape are not measured, so the exact difference is inferred from Project's documented deadline behaviour and the port's own W31-0.2 refusal.
- Fix: in `read_calculate_shape`, refuse any leaf with `is_date(rows.u32(row, task_row::kDeadline))`, needle "carries a deadline"; add a writer test (set +182 on row 2 of the control) and an app test through a file mutation, since the plan has no deadline field.

### R35G1-2  High  Any pending recalculation passes; only "critical leaf lengthened, project start unchanged" was measured

- Where: `core/mppwrite/calculate.cpp:165-201` (shape check), `:212-258` (`check_request`); `app/edit_model.cpp:3281-3376`.
- Defect: the brief (W35-1.2) and the header (`calculate.h:9-12`) describe the shape as one leaf's Duration changed, pending. The checks only prove that a recalculation is pending (`0x02400024 = 01 00`) and that the project row's Finish is not the latest leaf Finish. Nothing pins what changed, or that the leaves' early dates are already final. The pair (COM facts in `build/w35/W35-1.2-calculate.json`) has A critical before and after, and B non-critical before and after. No critical flag flips and the project gets longer. Three unmeasured pending shapes get through both layers:
  1. B's duration is raised past A's, so the critical path moves from A to B. Project's F9 flips the critical flags. The writer never writes a critical flag, so A stays marked critical while the port writes slack on it.
  2. The critical leaf is shortened, so the project gets shorter. No pair measures this.
  3. The project start is changed under manual calculation. The leaves keep their old Start and Finish, because the writer only rewrites slack and late words. The writer does not check that each leaf starts at the project's start, and it stamps Fixed2Data +46 with the Props project start (`calculate.cpp:312-319`). That stamp is right only when the leaf's start bound is the project start (the linked and lag pairs show +46 holds the predecessor-finish bound instead).
- Failure: case 1 on the unlinked control (B 1440 to 9600 minutes, pending). The shape check passes: the flag is 01 00 and row 0 Finish is not B's new Finish. `make_calculate_edit` builds A's slack as more than 0 and B's as 0, and the writer writes them, but leaves the critical bits as Project's pre-F9 values. Project would rewrite those bits. Confirmed against source (no check). Project's bytes for these shapes are not measured, so whether the flags live in FixedData/FixedMeta is unconfirmed.
- Fix: pin the measured transition in the writer:
  - the critical leaf's identity in the file before the save must equal the one after it (refuse if the leaf that ends the project changes);
  - require the new project finish to be later than the stored row 0 Finish;
  - require every leaf's Start to equal the stored row 0 Start;
  - require each leaf's late finish to equal the project finish (see R35G1-5).

  Also in the edit model, check every leaf's Finish against `calendar.add_work(start, duration)` so that only a duration change can be the pending one. Add a writer test for each, with a needle.

### R35G1-3  High  Rule 5 pin record absent; several user-variable parameters unrefused in both layers

- Where: `spec/writers/calculate.md` section 8 and `spec/writers/evidence/wave35-calculate-unlinked.md` (no parameter-pin table: grep for "pin", "calendar", "resource", "effort" in the evidence finds nothing); `core/mppwrite/task_mode.cpp:338-435` (`validate_file`); `app/edit_model.cpp:3238-3277`.
- Defect: brief section 1 rule 5 needs a record of every parameter a user can vary, with the value the pair pinned, and needs the refusal to cover every unpinned value. That record does not exist, and these values reach the writer unrefused:
  1. **Resource-sheet rows with no assignment.** `validate_file` checks only that each assignment names the 0xffff0001 placeholder. Neither layer reads `TBkndRsc` or `plan.resources`. In `wave30-calculate`, F9 changed `TBkndRsc/FixedMeta` byte 139 (`calculate.md` section 2). A file with resources is unmeasured.
  2. **Project calendar identity.** A non-Standard base, 24 Hours, or Standard with exceptions in the slack window is not refused. The edit model computes slack through `load_project_calendar`, which may well be right, but it is unmeasured.
  3. **Baselines.** Baseline var records on the rows are not checked.
  4. **Inactive tasks.** `calculate.md` section 1 says the engine skips inactive rows, but no check of the Active flag was found in either layer (unconfirmed: I did not locate where Active is stored).
  5. **Task type and effort-driven.** These are listed in rule 5. They are probably inert with placeholder assignments, but they are not recorded as pinned.
- Failure: a project with one unassigned work resource on the Resource Sheet and the measured two tasks. Both layers accept it and write it, and no pair shows whether Project's F9 touches the TBkndRsc rows. Confirmed against source.
- Fix: write the pin table into `calculate.md` section 8. Refuse up front in both layers:
  - resource rows beyond the placeholder (plan: `!plan.resources.empty()`; file: TBkndRsc live row count);
  - a project calendar other than the pair's (compare the calendar uid, and refuse exceptions or edited working weeks);
  - baseline records;
  - inactive rows.

  Each refusal needs its own test and needle.

### R35G1-4  Medium  The "all-or-nothing" save is not atomic over an existing target

- Where: `core/mppwrite/calculate.cpp:343-386`; `calculate.h:41-43`; `app/edit_model.cpp:5228-5231` (with Calculate as the only op, `next_write_path()` returns `result.path` itself).
- Defect: `save_calculate` writes `path` seven times: five `save_var_record` calls (each runs `work.save(path)`, `varmeta_edit.cpp:1064`), then `save_props_edit` (`props_edit.cpp:493`), then `work.save(path)`. The catch removes `path` only when it did not exist before. `validate_calculate` checks `0x02400024`, and the save checks `0x02400002`, but nothing checks `0x02400003` (kind 4, length 4) before the first write.
- Failure: Save over the open file (`allow_source_overwrite`), or Save As onto an existing file, where `0x02400003` is absent or has another kind or length. The five appends land in the target, then `save_props_edit` throws. The user's file is left with the recalculated rows and five overtime records, but the pending flag is still 01 00 and the Props finish is stale: a partial F9 that Project will open. The app test at `app_edits_calculate_tests.cpp:560-584` covers only a refusal raised before the first write. Confirmed against source. How likely a Project file is to lack `0x02400003` is not measured.
- Fix: run every check (including `0x02400003`'s kind and length) in `validate_calculate`. Build the whole chain against a private temporary path and rename onto `path` once at the end, or keep the existing bytes and restore them in the catch. Add a test that plants a refusal after the first append against an existing target.

### R35G1-5  Medium  Writer request check does not pin the unlinked rule; the save-time dry run does not prove the file is unchanged

- Where: `core/mppwrite/calculate.cpp:212-258`; `app/edit_model.cpp:5837-5858` (the comment "a dry run first proves the file is still the one the edit was built from").
- Defect: `check_request` takes any late finish between the leaf's finish and the project finish. It takes any late start at or after the early start, and any slack value at least 0 as long as the three slacks are equal. Section 8 states the rule as late Finish = project Finish, and the critical leaf's slack is 0. Neither is enforced, so the writer trusts the caller's engine. At save time, `validate_calculate` re-checks only the project finish and duration. If the source file changes between queueing and saving (another tool, or a reload), stale per-leaf slack still passes as long as it stays in range.
- Failure: the non-critical leaf's duration changes on disk after the edit was queued, with the project finish unchanged. The save writes the old slack (7 days) where Project would write the new value. Confirmed against source.
- Fix: in `check_request`, require `late_finish == request.project_finish` for every leaf. For the critical uid, require `slack == 0` and `late_start == start`. Record the source file's hash in the edit and refuse at save time if it differs. Fix the comment.

### R35G1-6  Medium  A file calculated once before is accepted by the builder and refused only at save, with a generic message

- Where: `core/mppwrite/calculate.cpp:117-207, 262-270`; `core/mppwrite/varmeta_edit.cpp:665-671`.
- Defect: neither `read_calculate_shape` nor `validate_calculate` checks that the five project-row overtime records (`0x0B4000A4/A5/A8/A9/AA`) are absent. Take a file that Project calculated and saved, edited again under manual calculation (pending again). `make_calculate_edit` accepts it and the command queues. The save then fails at the first append with "already has a record for 0x0B4000A4 ... use the re-point shape". That is not an up-front refusal (brief rule 4), and the user sees the wrong reason. The repeat-calculation shape is unmeasured: Project probably rewrites nothing in VarMeta there. No test covers it. Confirmed against source.
- Fix: in the shape check, refuse a file whose uid 0 already holds any of the five keys ("has been calculated before; the measured pair is the first calculation of an automatically built file"). Test it in both layers with a synthetic file: the port's own save of the control, with the pending flag set back to 01 00.

### R35G1-7  Medium  Several writer guards have no test; removing them stays green

- Where: `core/mppwrite/calculate.cpp`. Each guard below has no refusal test in either suite:
  - `:129` row 0 uid;
  - `:152-157` FixedMeta +17/+18 `0x48/0xEF`;
  - `:180-184` missing `0x02400024`;
  - `:190-193` "project finish already equals";
  - `:194-201` start anchor;
  - `:242-245` late before early;
  - `:246-249` late finish after the project finish;
  - `:308-311` missing `0x02400002`.
- Defect: `tests/roundtrip_calculate.cpp` and `app/app_edits_calculate_tests.cpp` do not reach these guards. Both "already calculated" tests use Project's result file, whose flag is 00 00, so they stop at "not pending" (`calculate.cpp:185`) and never reach the finish-equals guard. The row-count guard (`:123`) is reached only through the app test "a file whose Task table lost a row", whose needle is the generic prefix "not implemented in this port" (`app_edits_calculate_tests.cpp:431`). With `:123` removed, that file is still refused by `validate_file`'s assignment count, so the test stays green.
- Failure: delete any one of these guards and both suites stay green (predicted; not run).
- Fix: add one synthetic-mutation refusal test per guard in `roundtrip_calculate.cpp`, with the guard's own words as the needle. For example, set row 0 +100 to the latest leaf finish while the flag stays 01 00 (needle "already equals the latest task finish"), and set the leaf +174 to 0xFFFFFFFF (needle "has no start anchor").

### R35G1-8  Medium  Refusals of the three real pairs assert only generic text

- Where:
  - `app/app_edits_calculate_tests.cpp:340-357`: linked, lag and snet assert only "not implemented in this port", the prefix every refusal carries;
  - `app_qt/tests/calculate_gate_test.cpp:150-152`: the linked status line, same prefix;
  - `tests/roundtrip_calculate.cpp:190-203`: all three pairs, including snet, assert "link rows".
- Defect: brief rule 4 asks for "a needle naming the specific gate". With the plan-side link check (`edit_model.cpp:3246`) removed, the app linked/lag/snet loop and `qt_calculate_gate` stay green, because the writer's "link rows" text carries the same prefix. Only the synthetic "a plan with a link" test goes red, as the builder's red run showed. The real SNET pair never exercises the constraint gate in either layer. Confirmed against source.
- Fix: assert "links" (plan side) for the app loop and the Qt status line. Add an SNET-only file case: the snet control with its link row tombstoned, or the unlinked control with constraint type 4 at file level in the app suite. Its needle should be "a constraint" or "carries a constraint".

### R35G1-9  Medium  Oracle dates are shifted by the host's UTC offset; the README records values the fixtures do not hold

- Where: `scripts/oracle_w35_calculate.py:121, 135`; `tests/data/README.md` (the four new rows); `build/w35/W35-1.2-calculate.json`.
- Defect: the script passes naive `datetime(2026, 1, 5, 8, 0)` through pywin32. COM reports `ProjectStart` as 2026-01-04 21:00 and the SNET date as 2026-01-25 21:00, an 11-hour shift (the host's UTC offset). The README rows say "Built ... from ProjectStart 2026-01-05 08:00". The SNET row half-notices the shift ("a Sunday 21:00 value"). So the recorded pinned value is wrong, the fixtures depend on the host's time zone, and the shift puts the host's UTC offset into a public fixture: a minor locality leak, not an identity leak. Every measured shape also has a project start in non-working time (Sunday 21:00) that differs from the leaves' Monday 08:00 start. The usual user file has a 08:00 project start, which is an unpinned variant of the enabled shape: Fixed2Data +46 and Props `0x02400002` would hold a different value. Confirmed from the JSON.
- Fix: pass dates as COM date strings or `pywintypes.Time` with an explicit zone, so that ProjectStart reads back exactly 2026-01-05 08:00. Correct the README rows to the stored values. Either refuse a project start that does not equal the measured relation (a start in non-working time snapping to the leaves' start), or measure a 08:00-start pair. Assert ProjectStart in the script after setting it.

### R35G1-10  Low  Calculate refusals carry the task-mode pair's wording

- Where: `core/mppwrite/task_mode.cpp:345-350, 353-434, 438-512` reached through `read_task_file_shape`.
- Defect: Calculate users see text such as "the file has link rows; the measured task-mode pair has an empty link table" and "the measured task-mode pair has exactly one placeholder assignment per task". The gate is right; the named pair is the wrong one. Confirmed.
- Fix: take a pair label as a parameter of `validate_file` / `read_task_file_shape`.

### R35G1-11  Low  "There is nothing to calculate" is false when a non-critical leaf's change is pending

- Where: `core/mppwrite/calculate.cpp:190-193`.
- Defect: if B's duration changes but the project finish does not move, Project's F9 still rewrites B's slack and late dates. Refusing is right (unmeasured), but the message says there is nothing to calculate. Confirmed.
- Fix: "the pending change does not move the project finish; that recalculation has no measured pair".

### R35G1-12  Low  FixedMeta entry addressed by position, not by lookup

- Where: `core/mppwrite/calculate.cpp:51-56, 143-157`.
- Defect: the 0x48/0xEF check reads entry `row + 3` directly. `task_mode.cpp` resolves entries with `meta_entry_for_row`, so a file with reordered meta entries would have the wrong entry checked. The comment at `:51-52` is garbled ("row + 3 is not needed: ..." followed by code that uses row + 3). Harmless for the three in-order rows of the pair. Confirmed.
- Fix: use `meta_entry_for_row`.

### R35G1-13  Low  Overtime keys widened table-wide in the var-record writer

- Where: `core/mppwrite/varmeta_edit.cpp:136-145` (`kMeasuredKeys`).
- Defect: `check_measured_key` ignores the uid, so after this change `save_var_record` accepts an overtime append (`0x0B4000A4..AA`) on any leaf row too, which no pair measured. Only `project_row_append_measured` limits uid 0. No caller does this today, so nothing reachable writes it; this is a writer-layer gap. Confirmed.
- Fix: give `MeasuredKey` an optional `uid0_only` flag, and refuse these five keys for uid != 0, with a test.

### R35G1-14  Low  Oracle hygiene: null-save source under the repo; calculation mode left manual

- Where: `scripts/oracle_w35_calculate.py:49-50, 175-179, 139`.
- Defect: the control copy for the null save is opened from `build/w35/calculate-work` under the repo, not under `C:\w35\`. My scan found no embedded repo path, since the save goes to `C:\w35\fixtures-calculate-null`, but rule 4 says the copy itself should be on the neutral root. The script also leaves `app.Calculation = 0` (manual) on exit, and `oracle_lock.oracle_session` does not restore it (no "Calculation" in `scripts/oracle_lock.py`), so the operator's Project can be left in manual calculation. Confirmed against source.
- Fix: set `WORK = C:/w35/calculate-work`, and restore `app.Calculation = 1` in a `finally`.

## Item verdicts

| Item | Verdict | Notes |
|---|---|---|
| W35-1.1 decode note (18cfb08) | Pass with notes | Sources and limits stated; section 8 errata (1bd09ee) correct the nine-entry and Fixed2Meta claims against the wave-35 pairs. No rule-5 pin table (R35G1-3). |
| W35-1.2 fixtures (15d8bb5) | Pass with notes | 12 files, hashes match `check_fixtures.py` and the README; scans clean (only `C:\w35` paths). README's ProjectStart wrong and dates host-zone dependent (R35G1-9). Commit message's "209 hashes" corrected later, as recorded. |
| W35-1.2 evidence (2f0ae13) | Pass with notes | Anatomy reports consistent with the writer (FixedData 23 effect bytes, FixedMeta entry 3 only, five 12-byte appends, Props two records). Oracle script uses `oracle_lock`, `neutralise` and `C:/w35` for saves; R35G1-9 and R35G1-14. |
| W35-1.2 writer (82a7d92) | Fail | Byte match on the one pair is real (the effect-declared comparison is sound and the 0x30/+46/+58 and overtime constants match all four pairs where they apply). But the shape gate admits unmeasured shapes: deadline (R35G1-1), other pending transitions (R35G1-2), resources, calendar and baselines (R35G1-3). Not atomic over an existing target (R35G1-4). No out-of-bounds access found: the Fixed2Data, FixedMeta and Fixed2Meta sizes are validated by the `State` constructor before the writes, and the Props parse is bounds-checked. |
| W35-1.2 task_mode.cpp refactor | Pass | Pure extraction of the whole-file checks into `State::validate_file`; same order (after the uid lookup, before the row precondition bytes), same messages, no guard lost. `read_task_file_shape` catches the constructor's `Error`. |
| W35-1.3 Calculate Project (1d04674) | Fail | The edit model has the same gaps as the writer for R35G1-1 to -3, plus R35G1-6 (not refused up front). Exclusivity in `push()` and in the save cascade is correct. The tests have untested guards and generic needles (R35G1-7, -8). The Qt button is live, its refusals go to the status line, and the ribbon pin and backlog are updated consistently. |

## Recommended mutations for the coordinator

Plant one at a time and rebuild; restore after each.

1. **Finish-equals guard.**
   - File: `core/mppwrite/calculate.cpp`.
   - Anchor: `if (rows.u32(0, task_row::kFinish) == shape.project_finish) {`
   - Change: `if (false) {`.
   - Expected: `mppwrite_roundtrip_calculate` and `app_edits_calculate` STAY GREEN today (gap R35G1-7). Once the R35G1-7 test lands, `mppwrite_roundtrip_calculate` should go red.
2. **Late-finish bound.**
   - File: `core/mppwrite/calculate.cpp`.
   - Anchor: `if (leaf.late_finish > request.project_finish) {`
   - Change: `if (false) {`.
   - Expected: green today (gap). After the R35G1-5 fix (equality check), `mppwrite_roundtrip_calculate` red.
3. **Start-anchor guard.**
   - File: `core/mppwrite/calculate.cpp`.
   - Anchor: `if (rows.u32(row, task_row::kStartAnchor) == kNoDate) {`
   - Change: `if (false) {`.
   - Expected: green today (gap); `mppwrite_roundtrip_calculate` red once tested.
4. **Plan-side link check (needle specificity).**
   - File: `app/edit_model.cpp`.
   - Anchor (unique): the line before `return prefix + "for a project with links: the measured Project pairs "`, i.e. the `if (!plan.links.empty() || plan.skipped_links != 0) {` inside `calculate_plan_refusal` (line 3246; the same text also appears at 1549, 2224 and 2400, so anchor on the return line).
   - Change: `if (false) {`.
   - Expected: `app_edits_calculate` red (the synthetic "a plan with a link" only). `qt_calculate_gate` and the app linked/lag/snet loop stay GREEN, which shows R35G1-8.
5. **Positive control on the byte match.**
   - File: `core/mppwrite/calculate.h`.
   - Anchor: `inline constexpr std::uint8_t kCalculateLeafStamp = 0x30;`
   - Change: `0x28`.
   - Expected: `mppwrite_roundtrip_calculate`, `app_edits_calculate` and `qt_calculate_gate` red. The Qt gate goes red only if its save path re-verifies; if `qt_calculate_gate` stays green, note it as a vacuous save check, since it asserts only "the file differs from the control".

Optional sixth, for the writer-layer link guard:

6. **Writer-layer link guard.**
   - File: `core/mppwrite/task_mode.cpp`.
   - Anchor: `if (!link_table_is_empty(*doc, table_name)) {`
   - Change: `if (false) {`.
   - Expected: `mppwrite_roundtrip_calculate` red ("link rows" needle). The task-mode suites should go red too; if they stay green, record that the task-mode link refusal is untested.
