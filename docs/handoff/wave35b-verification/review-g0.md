# Wave 35b G0 review (adversarial, read-only)

- Range: `git diff ec326b6 e1397d1` (6472da3, efd30c6, 8695ea8, cc9b581, 15b9efd, plus marker e1397d1)
- Date: 9 November 2026
- Method: I read the source at e1397d1 (`git show`, `git diff`, `git grep`). Nothing was built, run or edited, and COM was not used. I tried to refute each finding against the code before writing it down.

## Counts

| Severity | Count |
|---|---|
| Critical | 0 |
| High | 2 |
| Medium | 3 |
| Low | 3 |

## Findings

### R35bG0-1  High  0.3's "leaf starts at the project start" compares against the stale row 0 roll-up, not the Props project start the writer stamps; R35G1-2 case 3 still passes both layers

- Where:
  - Writer guard: `core/mppwrite/calculate.cpp:358` (`rows.u32(row, kStart) != rows.u32(0, kStart)`).
  - Writer stamp: `core/mppwrite/calculate.cpp:552-566` (Fixed2Data +46 = Props `0x02400002`).
  - Plan guard: `app/edit_model.cpp:3651` (`task.start != plan.project_start`), where `plan.project_start` is row 0's calculated Start (`app/reader_adapter.h:207`, `core/mpp/reader.cpp:1915`).
- Defect: R35G1-2 case 3 is "the project start is changed under manual calculation; the leaves keep their old Start". Both new guards compare the leaf Start with row 0's FixedData Start. Row 0 is the project summary roll-up. It is exactly the word that manual calculation leaves stale: the measured pending shape is defined by row 0 Finish being stale. The writer, though, stamps every leaf's Fixed2Data +46 from Props `0x02400002`. Neither layer reads that record (the reader does not expose it; `git grep 02400002` under `core/mpp` and `app` finds nothing). The measured control already shows that the two values differ: Props start is Sunday 21:00 and row 0 and the leaves are at Monday 08:00 (R35G1-9). So this guard could never have related them.
- Failure: take the unlinked control (A lengthened, pending) and change Project Information > Start to a week later under manual calculation. The leaves and row 0 keep 5 January 08:00, so 0.3 passes. Each leaf's Finish is still its Start plus its Duration, so 0.4 passes. The flag is still `01 00`. Both layers accept. The writer writes slack and late dates for the old dates, stamps +46 with the new Props start and clears the pending flag. Project's F9 would have moved both leaves to the new start. The port's file claims to be calculated, but its leaves sit before the project start. Project would open it, because manual calculation shows the stored words. This is inferred: Project's bytes for this edit are not measured. It would be Critical if Project behaves as inferred.
- Test gap: the 0.3 tests and the 0.4 "start edit" tests (`tests/roundtrip_calculate.cpp` "leaf start", "leaf start edit"; `app_edits_calculate_tests.cpp:480-490, 575-589, 685-697`) change the FixedData Start words. None of them changes Props `0x02400002`.
- Fix:
  - Writer: in `read_calculate_shape`, read Props `0x02400002` (kind 4, length 4) and refuse unless it stands in the measured relation to the leaf and row 0 Start. That relation is the measured one: the first working instant at or after the Props start, on the pinned Standard calendar, equals the leaf Start. Alternatively, pin it to the control's exact pattern, a Props start in the non-working time just before the leaves' start. Use a needle such as "project start in 114/Props".
  - Plan: add a `PlanInput` field for the Props project start, filled by the reader, with the same check.
  - Tests: in both layers, the control with only Props `0x02400002` moved by a week.

### R35bG0-2  High  The project's calculation options in 114/Props are not pinned; "Calculate multiple critical paths" changes the slack Project writes and both layers accept it

- Where: `core/mppwrite/calculate.cpp:301-454` (no Props record is checked except `0x02400024`); `app/edit_model.cpp:3568-3699`; `spec/writers/calculate.md` section 8 (the pin table has no Props row).
- Defect: R35G1-3 and rule 5 ask for every parameter a user can vary to be pinned or refused. 0.5 pins the Rsc, Cal, Task, Assn and Rsc VarMeta tables and the FixedMeta and Fixed2Meta flags, but nothing in `114/Props`. The project-level options live there: CalcMultipleCriticalPaths, CriticalSlack (the limit), minutes per day and per week, and schedule from. The port does not decode them for MPP (`git grep -i multiplecritical` finds only the type library and the string tables). `make_calculate_edit` (`app/edit_model.cpp:3736-3790`) applies one fixed rule: slack is the working time to the project finish, and the late finish is the project finish.
- Failure: take the measured control with File > Options > Advanced > "Calculate multiple critical paths" ticked (a per-project option, stored in the file). Project's backward pass gives every task without a successor its own early finish as its late finish, so the non-critical leaf gets total slack 0 and is critical. The port writes 7 days of slack and a late finish at the project finish, and Project accepts the file. The minutes-per-day option has a second effect: a duration entered as 40 h with 450 minutes per day is 24000 ticks. 0.4 accepts that, because it is a whole number of 4800-tick days. `summary_info.cpp:383` then renders it as "5 days", while Project would write "5.33 days". The mechanism is confirmed at source (no Props pin, a fixed slack rule). Project's output for the option is inferred from its documented behaviour; it is not measured.
- Fix: pin `114/Props` to the control in the writer. Every record id and payload must equal the control's, except the records the save itself writes (`0x02400003`, `0x02400024`, the tail key `0x00010001`), the start relation in R35bG0-1, and any record the null save shows to be per-save noise. Use a needle such as "114/Props record ... differs from the measured control". For the plan layer, carry a Props digest or the decoded options on `PlanInput`, or state in `calculate.md` section 8 that this pin is writer-only and why. Add a writer test that flips one non-written Props byte.

### R35bG0-3  Medium  The critical-path guard is a finish-position proxy that does not see a critical-path move made by two Duration edits; the residual limit is not recorded

- Where: `core/mppwrite/calculate.cpp:403-421`; `app/edit_model.cpp:3667-3678`; `spec/writers/calculate.md` section 8.
- Defect: the brief asked for "the computed critical flags differ from the stored ones". The builder rejected that comparison for a valid reason (stored slack is stale; the control's B has stored slack 0). Instead it uses this proxy: exactly one leaf finishes at or after the stored project finish P. Consider A (critical at P) shortened to before P while B (non-critical) is lengthened past P. That gives the same signature as the measured pair, but the critical path has moved from A to B. Both durations changed, and 0.4 only proves "Finish = Start + Duration" for each leaf, not "one Duration changed". The 0.4 commit message and the code comments say the pending recalculation "was caused by a Duration edit alone", which overstates what is checked. The same happens with two lengthened leaves where B stays before P (two edits, no measured pair).
- Failure: both layers accept these files. Whether a wrong byte results depends on whether Project stores a critical bit outside the slack words (R35G1-2 left this unconfirmed). If the flag is slack-derived only, the output matches. Otherwise the critical flags are stale.
- Fix: record in `calculate.md` section 8 that the guard cannot tell one edit from two, and that a two-edit critical-path move passes it. Better, find a before-state witness in the file and refuse unless exactly one leaf changed and it is the latest. For example, the placeholder assignment's stored finish copy in TBkndAssn FixedData, if the pending file keeps the pre-edit value there (spec section 3 says F9 rewrites those copies). Add a test for the two-edit case once a witness exists.

### R35bG0-4  Medium  The new plan-layer fields are never tested through the reader; removing their reader_adapter lines leaves app_edits_calculate green

- Where: `app/reader_adapter.h:179` (deadline), `:195-196` (active, baseline_stored), `:155` (assignment baseline_stored); `app/app_edits_calculate_tests.cpp:441-559` (the plan tests) and `:615-781` (the file tests).
- Defect: every plan-level refusal test for 0.2 and 0.5 sets the `TaskRow` or `AssignmentRow` field by hand on the control's plan. Every file-level test passes the clean control `plan` together with the mutated file, so the writer's text produces the needle. No test loads a mutated file through `plan_input_from_document` and asserts the plan needle. `git grep` finds no other test of `.active`, `.baseline_stored` or `.deadline` on a loaded plan.
- Failure: delete `row.deadline = ...`, `row.active = task.active;` or `row.baseline_stored = task.baseline.stored;`. Every test stays green, and the plan layer silently stops refusing those shapes for a real file. The writer still refuses them, so nothing is written wrong today, but the "both layers" claim is unproven for a real file. The calendar mapping is the exception: the control's acceptance needs `name == "Standard"` and `stored_blocks == 1`, so it is pinned.
- Fix: for the deadline, inactive and task-baseline mutations, save the mutated file, build the plan with `load_plan` or `plan_input_from_document` from it, and assert the PLAN needle ("for a task with a deadline", "for an inactive task", "for a task with a baseline") through `calculate_plan_refusal` before the file is opened.

### R35bG0-5  Medium  The plan layer is narrower than the writer for several 0.3 and 0.5 shapes, so "both layers" holds for the writer only

- Where:
  - `app/edit_model.cpp:3612-3622` (only Baseline 0 and `type_block_stored`).
  - `app/edit_model.cpp:3674` (`task.finish != latest`).
  - `core/mppwrite/calculate.cpp:263-296` (the FixedMeta pin) and `:189-215` (the exact var-key sets).
- Defect: the brief's 0.5 asks for "a task type or effort-driven flag other than the pair's" to be refused in both layers. The 15b9efd message says each parameter is "pinned ... in both layers". It holds for the writer only, in three places:
  - The effort-driven and task-type FixedMeta bits (byte 16) are checked only by the writer. `PlanInput` has no field for them, because the reader does not expose them.
  - Baseline 1 to 10, the interim Start1-10 and Finish1-10, notes and any other task var block are refused only by the writer's exact-key check. The plan sees Baseline 0 only (`read_baseline` with `kTaskBaselineKeys`).
  - Tied latest finishes: `task.finish != latest` skips both tied leaves, so the plan accepts a tie that the writer refuses at `calculate.cpp:411-421`. RW35-W3 / W35b-2.7 will add a tie refusal later.
- Failure: no file is mis-written, because `make_calculate_edit` always runs `read_calculate_shape` and save runs `validate_calculate`. A plan-only caller of `push()` accepts these shapes, though, and the record overstates the coverage.
- Fix: either add plan fields (the FixedMeta flag byte, a "stores only the measured var keys" flag from the reader) and the matching refusals, or correct the commit record and `calculate.md` section 8 to say which pins are writer-only and why. Make the plan tie check `>=` on both leaves against P, as the writer does.

### R35bG0-6  Low  The push() refusal tests assert a boolean, so the guard that fires is not proved

- Where: `app/app_edits_calculate_tests.cpp:448-450, 467-469, 488-490, 501-503, 520-522, 528-530, 556-558, 568-570, 586-588`.
- Defect: `push()` returns only `bool`. For example, with the 0.3 start check removed, "push() refuses ... a moved leaf start" stays green, because 0.4 refuses the same plan (its Finish no longer equals Start plus Duration). The builder test of the same plan asserts the needle and would go red, and both paths share `calculate_plan_refusal`. So this is coverage hygiene, not a hole.
- Fix: assert `calculate_plan_refusal(changed)` contains the needle next to each `push()` check, or note in the test that push() is checked for the call only.

### R35bG0-7  Low  Pins that stop short of the row: FixedMeta bytes 8-15 are excluded, and the leaf date copies and remaining duration are not compared

- Where: `core/mppwrite/calculate.cpp:285-287` (bytes 4-15 skipped); `:426-438` (only +96/+100/+84 compared).
- Defect:
  - Spec section 3 records leaf FixedMeta +9 as a flag byte that F9 changes in `wave30-calculate` (`0xbf -> 0xff`, "presence bits"). The skip comment calls bytes 4-15 "the row's offset and stamp", but only +4..+7 is the offset; +8..+23 is the opaque blob (`spec/mpp-records.md` section 1.1).
  - 0.4 checks Start, Finish and Duration only. The leaf's second date copy (+104/+108) and its Remaining Duration (+88) are not related to them. A pending cause that updates one copy but not the other would pass.
- Fix: compare leaf +104/+108 and +88 with the control's relation (presumably equal to +96/+100/+84). Either pin the FixedMeta bytes 8-15 that the control and the null save show to be constant, or correct the comment to say why they are excluded.

### R35bG0-8  Low  The project row is not checked for a deadline (or a constraint)

- Where: `core/mppwrite/calculate.cpp:327-366` (the loop starts at row 1); `app/edit_model.cpp:3607-3655` (`plan.tasks` holds only the leaves).
- Defect: a Deadline can be set on the project summary task. Whether Project propagates a summary deadline into the subtasks' late dates is not measured. Neither layer reads row 0 +182. This is SUSPECTED only: I did not confirm that Project lets row 0 carry one.
- Fix: include row 0 in the writer's `is_date(+182)` check (needle "carries a deadline"), with a writer test that sets row 0 +182.

## Clean (checked and found sound)

- **0.1 (R35G0-1).**
  - `prompt_hook.h` records `box->text()` and `input->labelText()`.
  - The removal checks in `assign_resources_gate_test.cpp:303-305, 337-340` now match the real prompt text (`app_qt/dialogs.cpp:1085`: `Remove "%1" from "%2"?\n\nThis is a pending edit; ...`).
  - The port-saved removal check gained a text assertion it did not have, so it is stronger than before.
  - The remaining `title ==` check (`:232`) is on a QInputDialog, as the brief allows.
  - `prompt_hook_test.cpp` asserts the box text "Remove it?" that it sets.
  - `check_prompt` prints on any failed prompt check.
  - A sweep of `app_qt/tests` found no other QMessageBox title comparison. The other `windowTitle` uses are QDialogs, or are only printed (backstage).
  - No gate was weakened.
- **0.2 (R35G1-1).**
  - Writer: `is_date(+182)` on both leaves (`calculate.cpp:349`), with the same `is_date` semantics (0 and `0xFFFFFFFF` mean none) as the plan's `date32_from_packed` mapping to `kDateNa`.
  - Plan: `task.deadline != kDateNa` (`edit_model.cpp:3641`).
  - Both needles are specific, the writer test reaches the guard, and the reader fills `Task::deadline` (`reader.cpp:2000`).
- **0.3 shorten and critical guards.**
  - The ordering is correct: shorten, then "already equals", then critical.
  - The writer refuses a second leaf at or past P, and the "B overtakes A" test is a pure critical-path case.
  - The plan test adjusts the duration so that 0.4 does not pre-empt it.
- **0.4 arithmetic.**
  - Packed day 0 = 1983-12-31 is a Saturday. 15346 % 7 = 2, a Monday, which matches the spec example 0x3BF212C0 = Monday 5 January 08:00.
  - Ticks are 6 s and 4800 ticks are 8 h, consistent with `table.h`.
  - Weekends are skipped, Finish is set to 17:00 of the n-th working day, and the bounds checks hold.
  - Milestones (duration 0), part days, elapsed durations (8 h multiples can never land at 17:00 in calendar time) and non-08:00 starts are refused.
  - The control's Start + 15 days check (0x3C0427D8) was verified by hand.
  - The plan side casts date32 (identity with packed) and int64 ticks safely.
- **0.5 pins are not vacuous.**
  - The Rsc and Cal FixedData sizes, the Cal VarMeta (single name block, uid 1, "Standard" UTF-16 with terminator) and the exact sorted key sets per uid and group count are all read from the file.
  - The Baseline 0 keys refuse before the generic key check, so each test's needle is specific.
  - Fixed2Meta +8 bit 0x40 matches the reader's `kF2MetaFlagActive`.
  - The FixedMeta size is checked before the byte loop (`calculate.cpp:336`), so there is no out-of-bounds read.
  - The plan's calendar check depends on reader values that the control's acceptance proves are filled.
  - The exact task key set also refuses an already-calculated file up front (it overlaps R35G1-6, with a generic message).
- **The measured control is still accepted.** `unlinked_pair()` runs `save_calculate` on the committed `wave35-calculate-unlinked-control.mpp` (`fixture::require_committed` fails loudly, it does not skip) and compares the result with Project's pair. The app suite loads the same control with `load_plan`.
- **Tests.** `refuses()` and `expect_refused()` assert the specific needle, never the prefix alone, and an unexpected accept fails. No test loops over an empty set. `add_var_key` and `set_planned_work` really change the streams the guards read.
- **Entry points.** `save_calculate`, `validate_calculate` and `read_calculate_shape` are reached only from `edit_model.cpp` (`make_calculate_edit` and the save cascade), and each runs the full shape check. `push()` calls the shared `calculate_plan_refusal`.
- **GCC/Clang -Werror reading.** The new switch is on `uint32_t`, not an enum. There are no signed/unsigned compares (the AssignmentRow and CalendarRow fields are unsigned or both int32; `duration_ticks` is int64), no unused variables or helpers, and `<map>`, `<algorithm>` and `<cstring>` are included.
- **Hygiene.**
  - All six commits carry `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35b)`.
  - The diff and the commit messages contain no client or person names and no personal paths.
  - There are no CRLF lines, and `git diff --check` is clean.
  - The marker file is correct.
