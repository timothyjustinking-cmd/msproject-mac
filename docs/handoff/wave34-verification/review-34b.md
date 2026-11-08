# Wave 34b review (read-only, adversarial)

Scope: `git diff 023c858..3c69150` (branch `wave34b`, 17 commits a0b60fb..3c69150).
Read against `docs/handoff/wave34b-brief.md`. Nothing built or run; every claim below
comes from reading the source at `3c69150`. Line numbers are at `3c69150`.

## Brief items

| Item | Verdict | Notes |
|---|---|---|
| W34b-0.1 Assign Resources shapes | **INCOMPLETE** | Writer: byte image of the task row, FixedMeta, Fixed2Meta, Fixed2Data, VarMeta keys, resource row, FixedMeta and keys; 40 h work on add, removal and append. Edit model: mode, top level, duration, task calendar, constraint, rates, cost per use, accrual (add and removal). Gaps: the resource calendar's content and base calendar are not checked in either layer (R34b-1). Type, effort-driven, resource base calendar and removal units and work are checked in the writer only (R34b-2). |
| W34b-0.2 identity references | Complete for +16/+32 | `identity_reference_problem` resolves +16/+32 to the task and resource Fixed2Data identities in Project's save AND in the port's output, in all four assignment pairs, and the planted wrong references go red. The first dword stays masked; the other 12 bytes are compared raw. RC-W4 is resolved as a reference and compared by what it names. Residual masked identity: R34b-4. |
| W34b-0.3 Add Resources template | Complete in the writer; plan side partial (acceptable, as the dry run runs in the builder) | Byte template over the row, FixedMeta and Fixed2Meta. The reproduction (`wave30-assign-add.mpp`) is refused at both levels with specific needles. The calendar-reference mask: R34b-5. |
| W34b-0.4 Font Bold | Complete | `require_measured_view` runs inside `save_view_cell_style`. So it runs in the builder's dry run, the save's dry run and the real write: the applied view is resolved through 214/CEdl to a CV_iew uid, and an FNV-1a digest of the 42,692-byte block is checked. No path skips it. |
| W34b-0.5 Define WBS Code | Complete | Edit model: id == uid and file_row == uid. Writer: id == uid == row, outline level 1, not a summary (FixedMeta bit and has-children), FixedMeta state 0. A reordered file and a nested file are tested at both levels. |
| W34b-0.6 unused functions | Complete as far as read | Removed `owned_stream`, `get_u16` (assignment_growth), `checkq` (Qt test) and `check_unchanged` (resource test). Every function the wave adds is used. No `/*` inside a block comment in any changed C/C++ file (scripted scan). No new sign-compare. |
| W34b-0.7 gantt sort | Complete, correct | See the CI-2 note below. |
| W34b-1.1 bounds | Complete | Every listed loader uses `off > size \|\| n > size - off` in `size_t`, or iterates in `size_t` with bounded `get_u32`. Checked: assignment_growth 362/367/555, resource_growth 322-323, varmeta_edit 387-397, viewstream_edit 229-240 and the new CEdl walk, props_edit 208, wbs_code 189, summary_info 152-242/585. No 32-bit `offset + n > size` remains. |
| W34b-1.2 settled-append tests, needles | Complete | The 3->4 refusal, two-placeholder and no-placeholder refusals are tested with specific needles. The unreachable count guard was removed with a reason. No bare "measured" or "not implemented in this port" needle remains in the wave 34 refusal tests. |
| W34b-1.3 exclusive + document edits | Complete for the five kinds | The refusal comes after the hook-presence check and before any write. Each kind is tested with hook_calls == 0, no target written and the queue unchanged. A sixth exclusive kind has the same hole: R34b-6. |
| W34b-1.4 names | Complete for the upper cap and delimiters, in the edit model, push() and both writers | Lower bound: R34b-8. |
| W34b-1.5 errata | Present | Overstated: R34b-11. |

**CI-2 (0.7), `core/gantt/gantt.cpp:406-414`.** A straight insertion sort with a strict
`<` on `std::pair<int32,int32>`.
- It is stable, and it terminates (`j > 0`, `i < count && i < working.size()`).
- It sorts by the same total order `std::sort` used (lexicographic pair `<`). With a total
  order the sorted sequence of values is unique, so the output equals `std::sort`'s for
  distinct keys. It also equals `std::sort`'s for equal starts: those order by end, and
  fully equal pairs are indistinguishable.
- `count <= kMaxDayIntervals` by the fill loop, so the cost is trivial.
- No other bounded-prefix `std::sort` remains in `core/`.

## Findings

### R34b-1: High: Assign Resources and its removal still accept a resource whose calendar is not the measured one
- `app/edit_model.cpp:2868-2962` (no check on `ResourceRow::base_calendar` or the calendar).
- `core/mppwrite/assignment_growth.cpp:846-849`. The only calendar check is FixedData word
  154 == resource uid + 2, which says the resource points at *a* calendar of its own.
- `assignment_growth.cpp` never reads a `TBkndCal` stream (grep: no `TBkndCal`, `Cal/` or
  `kCal`). So neither layer checks the calendar's base calendar (the Cal FixedData row's
  parent word, `1` = Standard in the pairs), its exceptions or its work weeks.
- The brief requires this explicitly: "a resource with ... a non-standard base calendar"
  must be refused, in both layers.

Failure scenario:
1. Take a file of the fixture family (needed because of R34b-3).
2. In Project, give resource R2 a calendar exception on a day inside task 1's five days,
   or base R2's calendar on "24 Hours", and save. Nothing in the task row, the resource
   row, the resource FixedMeta or the resource VarMeta keys changes.
3. Assign Resources (Qt or macOS) passes `assignment_plan_refusal`, the dry run, `push()`
   and the save.

The port writes the assignment over the task's own start and finish with the flat 40 h
contour. Project schedules the assignment's work on the resource calendar, so it can store
a different assignment finish, a different contour and a different task finish. The same
holds for removal.

- CONFIRMED that the gate is absent. PLAUSIBLE that the stored bytes differ (no pair
  measures it).
- The CLOSE review recorded this as RW34b-1 at Medium. Under this rubric it is High: a
  UI-reachable save that can differ from Project's, and an explicit brief requirement
  that was not met.
- Reachability is narrowed by R34b-3 (only files whose target task row is the fixture's).

### R34b-2: Medium: the edit model checks fewer Assign Resources facts than the brief's both-layers rule requires
`app/edit_model.cpp:2888-2941` (builder) and `3589-3609` (`push()`).

The plan side does not check:
- the task type and the effort-driven flag;
- the resource's base calendar (`ResourceRow::base_calendar` is available);
- group, code or material label;
- for a removal, the removed assignment's `units_percent` and `work_ticks`, both in
  `AssignmentRow`.

All of these are refused only by the writer: in the builder's dry run, and at save.
- A hand-built edit passes `push()` and is refused late, in the save cascade. So no
  corrupt file results, but the rule "a refused shape fails in push() and in the core
  writer alike" does not hold for these shapes.
- No edit-model-level tests exist for them. `app_edits_assignment_tests.cpp:726-757`
  tests the writer's dry run through the builder, which is a test of the writer.

CONFIRMED.

### R34b-3: Medium: the writer's task-row template pins a timestamp, so Assign and Remove refuse every file not derived from the fixtures
- `core/mppwrite/assignment_growth.cpp:742-754`.
- `measured_task_row()` is compared byte for byte outside {0..31, 74..77, 96..119,
  174..177, 186..193}.
- Row +128 holds `0x3d141482`. As a packed date that is day 0x3d14, late 2026: the
  fixture authoring session, while the task start at +96 is 2026-01-05.
  `spec/mpp-records.md:294` records +128 as the save time on the project row; on a task
  row it is a per-task stamp.
- Every task created or edited at any other moment differs at byte 128. The refusal then
  says "a duration, constraint, calendar, percent or other word differs from the measured
  task", which names none of those.

Effect: Assign Resources is enabled in both shells but refuses every real user file that
is otherwise exactly the measured shape. This is "a measured shape now wrongly refused"
plus a misleading message. Before 0.1 these files reached the (unsafe) writer; now no
real file can.
- Fix: mask +128 and name the byte in the message, or say in the docs that the command
  works only on the fixture family.
- CONFIRMED that the word is compared. PLAUSIBLE that it is a timestamp.

### R34b-4: Medium: an identity copy in the Add Resources pair is still masked and never checked
- `tests/roundtrip_resource_growth.cpp:307` masks the first dword of the appended
  resource Fixed2Data record's +0 and +24.
- `calendar_identity_problem` (`:337-372`) ties the calendar record's +16 to the resource
  record's +0. Nothing checks that the resource record's +24 equals its +0, or names the
  new resource. The writer writes `fresh` at both (`resource_growth.cpp:723-725`).
- Scenario: a regression that writes a stale or other identity at +24 differs from
  Project only in the masked dword, and passes the pair test.
- Lesser case: in `tests/roundtrip_assignment_growth.cpp:349` the appended record's own
  +0 dword is masked, and its uniqueness against the other identities is not asserted.

CONFIRMED (harness gap; no wrong output today).

### R34b-5: Medium: Add Resources masks the template's calendar-reference word and derives the new one by +1, never tying it to the calendar row it appends
- `core/mppwrite/resource_growth.cpp:658` masks template row bytes 154..157.
- `:699-703` writes `u16(old) + 1` at 154 as a "counter".
- `:603` and `:781` append a calendar row with `cal_uid = max_cal_uid + 1`.
- `assignment_growth.cpp:846` treats the same word as the resource's calendar uid.
- Nothing asserts `u32(template, 154) == max_cal_uid`, nor that the new word equals
  `cal_uid`. The upper half (156..157) is masked and copied.

Scenario: a file whose last resource's word 154 is not the highest calendar uid, for
example after a calendar uid is reissued or in a crafted file, still passes every count
check. The writer then makes the new resource reference an existing (other resource's)
calendar, and the new calendar row names a resource that does not use it. That is a
cross-linked file.
- The CLOSE review listed the mask as a Low (RW34b-7). The missing equality makes it
  wrong output, not only an unexplained mask.
- PLAUSIBLE (not reached by any committed fixture).

### R34b-6: Medium: the W30 task-mode exclusive edit can still share a save with pending document edits
- `app/edit_model.cpp:4546-4568`. The task-mode block refuses other structural edits and
  start edits, but not `document_edits.any()`.
- 1.3 fixed the five wave 34 kinds and left this sixth exclusive kind with the same RC-R3
  hole.
- Scenario: Change Task Mode (auto -> manual) plus a pending table date format or a
  Text1 formula. One save runs the mode writer and then the document-edit hook, a
  combination no pair measures.
- Outside the brief's list of five, but the same defect.

CONFIRMED.

### R34b-7: Low: the task-row mask is wider than its comment and hides Actual Work
- `core/mppwrite/assignment_growth.cpp:744-746` masks {0, 32}.
- The comment (`:632-634`) says the masked work words are 8..15 and 24..31.
- +16 is Actual Work (`core/mppwrite/actuals.cpp:110-117`).
- It is covered indirectly, because % Work Complete (+94), % Complete (+92) and Actual
  Duration (+80) are compared. Still, a value word is masked without a stated reason.

CONFIRMED.

### R34b-8: Low: the name caps bound only from above
- `app/edit_model.cpp:2707` and `resource_growth.cpp:463` accept a 1-character Add
  Resources name. Both pairs add 2 characters.
- The rename (`edit_model.cpp:2583`, `varmeta_edit.cpp:1100`) accepts 1->2, 1->3 and 2->3.
  Only 2->3 ("R2" -> "R2b") is measured.
- The block sizes are 4, 6 and 8 bytes and both the 6- and 8-byte forms are measured, so
  the risk is small. Still, rule 1 is stated as "measured shape only".

CONFIRMED.

### R34b-9: Low: the removal push() refusal tests have no positive control
- `app/app_edits_assignment_tests.cpp:647-654`. For each ShapeCase, `push()` of a
  hand-built *removal* on `remove_plan` must fail.
- No check shows that the same hand-built removal on the unchanged `remove_plan` is
  accepted. The only positive push control is the assign at `:800`.
- If `push()` refused every hand-built removal for an unrelated reason (for example the
  assignment uid), these nine checks would pass with the plan rule removed.

PLAUSIBLE.

### R34b-10: Low: the "type or effort" writer test proves an arbitrary FixedMeta byte, not the property
- `tests/roundtrip_assignment_growth.cpp:2124` sets FixedMeta byte 17 to 0x44 and labels
  it "a task of another type or effort setting".
- Nothing in the repo maps byte 17 (or any masked or unmasked word) to the task type or
  effort-driven. `spec/scheduling.md:158` gives the type as field 0x0b400080, which the
  VarMeta key test covers only as a renamed key.
- The whole-entry compare probably catches both. But no file with a real Fixed Work or
  effort-driven task is tested, so the label overstates.

CONFIRMED (test content); coverage PLAUSIBLE.

### R34b-11: Low: the 1.5 erratum and the CLOSE record overstate 0.1
- `docs/handoff/wave34-complete.md:784`: "W34b-0.1 (`f08249e`) added the missing
  refusals".
- `docs/handoff/wave34b-complete.md:287`: "0 Critical, 0 High".
- Both stand although the brief's base-calendar refusal is absent (R34b-1). The next
  errata should be dated and appended, not rewritten.

CONFIRMED.

## Checked and found sound

- No shell calls a wave 34 writer directly. Every call is through `app/edit_model.cpp`:
  builders at 2823, 2992, 3091 and 3172, and the save cascade at 5465-5572. Each cascade
  write runs a dry run first.
- The Qt (`main_window.cpp:3089-3302`) and macOS (`main.mm:3177`) handlers use the
  builders and `push()` with the plan.
- Exclusivity plus the document-edit refusal shuts every cascade combination for the five
  kinds.
- The new writer tests mutate the measured controls one byte at a time and use needles
  that name the byte or gate. Removing any single writer gate turns its case green ->
  red, because the unmutated control writes.
- The edit-model ShapeCase tests change only the plan, so the file's dry run would pass
  and each case fails if its plan rule is removed.
- The 1.3 tests fail if the `document_edits.any()` branch is removed: the hook would be
  called and the save would succeed.
- The template hex lengths match their strides: task 202/64/96/47 bytes, resource 172/37
  bytes, Add Resources template 172/37/51 bytes.

Counts: Critical 0, High 1, Medium 5, Low 5 (total 11).
