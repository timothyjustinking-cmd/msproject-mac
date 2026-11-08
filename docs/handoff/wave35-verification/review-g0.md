# Wave 35 G0 review (read-only, adversarial)

- Range: `1b432b6..b8238bb` (branch `wave35`, 14 commits eceb4f5..b8238bb).
- Date: 9 November 2026.
- Method: read at `b8238bb` (worktree `D:\dev\msproject-mac-wt-v34g3`) and through
  `git show` / `git diff`. Nothing was built, run or edited. Line numbers are at `b8238bb`.
- Read against: `docs/handoff/wave35-brief.md` section 1 and G0,
  `docs/handoff/wave34-verification/review-34b.md`, `docs/handoff/wave35-complete.md`,
  `docs/handoff/standing-rules.md`, and the CI log
  `build/verify-w35/ci-macos-qt-42489754746.log`.

Counts: Critical 0, High 0, Medium 5, Low 8 (13 in total).

## Findings

### R35G0-1: Medium: CI-4 is misdiagnosed; the hook sees the dialog, and the failing assertion is the QMessageBox title, which macOS does not carry

Locations:
- `app_qt/tests/assign_resources_gate_test.cpp:326-327`. Section 3's check is
  `prompt.seen && prompt.question && prompt.title == "Assign Resources"`.
- `app_qt/tests/prompt_hook_test.cpp:51-52`. `result.title == "Late question"`.
- `app_qt/tests/assign_resources_gate_test.cpp:328-330`. `report_prompt` runs only when
  `!(seen && question)`.
- `app_qt/dialogs.cpp:1081-1089`. `confirm_remove_assignment` uses the static
  `QMessageBox::question(parent, "Assign Resources", ...)`.

Defect:
- On macOS, `QMessageBox::setWindowTitle` is a no-op: Qt's `qmessagebox.cpp` has "Message
  boxes on the mac do not have a title" under `#ifndef Q_OS_MAC`. I quote that from Qt's
  source from memory; I did not check it against the 6.5.3 tree here. So `windowTitle()`
  comes back empty and only the title comparison fails.
- The repo has already recorded this. `app_qt/tests/backstage_gate_test.cpp:450-453`
  says: "the macOS runs do not carry the title the Windows run does (qt-shell
  40054734014 and 40055403392)". That test identifies its box by text for this reason.

Evidence in the logs:
1. `qt_prompt_hook` fails ONLY "the late question's title is recorded", once per loop
   pass (accept and reject). "a late QMessageBox is seen as a question" and "Yes/No
   answers the late question" pass, so the box was found and answered.
2. `qt_assign_resources_gate` takes 0.69 s. If the hook had not answered the removal
   question, `QMessageBox::question` would block in `exec()` until ctest's 60 s timeout.
3. The log has no `note: prompt ...` line, because `report_prompt` cannot fire when only
   the title differs.
4. Section 2's removal check (`:292`, no title term) passes on macOS. Section 3's check
   (`:326`, with the title) fails. The wave 34b log
   (`build/verify-w34b/ci-macos-qt.log:57`) also has exactly one such FAIL. The
   brief's "the first of the two checks" is therefore the wrong one of the two.

What G0 did instead:
- W35-0.3 (b0bbd5f) replaced the single look with a poll.
- That fixes nothing and adds a new red test, `qt_prompt_hook`, on macOS.
- `wave35-complete.md` records "cause unproven", which is honest but not a fix.

Failure scenario: macos-qt stays red at every dispatch. The coordinator's merge gate
("CI green") cannot pass.

Fix:
- Identify the question by its content, not by its title. Add `QString text` to
  `gate::PromptResult`, set from `box->text()` in `prompt_hook.h:73-82`.
- In `assign_resources_gate_test.cpp:326` assert
  `prompt.question && prompt.text.startsWith("Remove \"")`.
- In `prompt_hook_test.cpp:51` assert the text ("Remove it?") for the QMessageBox case.
  Keep the title assertion for `QInputDialog`, which does carry a title on macOS: the
  section 1 check at `:223` passes there.
- Optionally pin the platform fact with `#ifdef Q_OS_MACOS` (title empty) and
  `#else` (title == "Assign Resources").
- Make `report_prompt` fire on any failure of the combined check.
- Proof: macos-qt goes green with section 3 still asserting `question`. If
  `confirm_remove_assignment`'s text or the dialog type is changed, the check goes red on
  all three platforms. That proves the check is not vacuous.

CONFIRMED from the source and both logs. The Qt-internal line is quoted from memory.

### R35G0-2: Medium: the project-calendar half of W35-0.1 is refused only in the writer

Locations:
- `core/mppwrite/assignment_growth.cpp:897-921`. `check_project_calendar` checks 114/Props
  0x0240000E (default calendar = Standard) and 0x02401388 (the stock week).
- `assignment_growth.cpp:793-895`. Also writer-only: the FixedMeta flag bytes
  (0x0E / 0x8F), the single-calendar-row count, and the Standard row
  shape {0, -1, 1}.
- The edit model (`app/edit_model.cpp:2952-2970`) checks only `calendar_base_uid`,
  `calendar_stored_blocks` and `calendar_base_extra_blocks`.
- `app/layout_model.h` has no field for the project calendar or its week. So `push()`
  of a hand-built edit cannot refuse these shapes.

Defect:
- This breaks the "both layers" rule (brief rule 4, standing rules). The edit-model
  comment (`edit_model.cpp:2952-2957`) claims it covers "an edited week (its own or the
  Standard calendar's)".
- That holds only if Project stores an edited Standard week as a VarMeta block on
  calendar 1. The writer's own comment (`:745-750`) says the stock week lives in Props
  0x02401388. If an edit of Standard's week lands there, the edit model accepts it.

Failure scenario:
1. Take a fixture-family file whose Project Information calendar is changed, or whose
   default week is edited in Props.
2. A hand-built `assign_resource` edit passes `push()`.
3. It is refused only at save, in the cascade's dry run.

The shells are covered: the builder's dry run refuses before anything is queued. So no
wrong file results.

Fix:
- Expose the project's default-calendar name or uid and a week digest (or a "stock week"
  flag) on `PlanInput`.
- Refuse in `assignment_plan_refusal`.
- Add an edit-model case per shape, with the writer's needles.

CONFIRMED that the gate is absent from the edit model. Whether a Standard-week edit lands
in Props or as a block is UNCONFIRMED.

### R35G0-3: Medium: the reader's "Standard calendar has extra blocks" input to the edit-model refusal is tested by nothing

Locations:
- `core/mpp/reader.cpp:3959-3966`. This derives `calendar_base_extra_blocks`.
- `app/app_edits_assignment_tests.cpp:639-640`. The tests set
  `row.calendar_base_extra_blocks = 1` on the plan directly.
- `app_qt/tests/assign_resources_gate_test.cpp:377-394`. The Qt gate drives only the
  base-uid and resource-block shapes from a real file.

Defect: no test anywhere loads a file whose Standard calendar owns a second block and
expects the edit model's wording. The writer test (`roundtrip_assignment_growth.cpp`,
"a Standard calendar with an exception") exercises only the writer.

Mutation that stays green:
- Replace `reader.cpp:3962-3963` with `resource.calendar_base_extra_blocks = 0;`.
- Every app, core and Qt test passes. The edit model then never refuses a Standard
  exception, and the user is refused only by the writer's dry run, with the writer's
  wording.

Lesser case:
- `calendar.stored_blocks` (`reader.cpp:3001-3005`) and `calendar_base_uid` are covered
  only by the Qt gate (`:377-394`), not by the core or app suites.
- A Linux or Windows dev run without Qt would not see a regression there.

Fix: add an app-level case that loads
`mppmutate`-style copies (Standard with a 0x0D400008 block; resource calendar with a
block; resource calendar based on 3). Expect the edit model's needle from
`make_assignment_edit`, ahead of the dry run, for add and removal.

CONFIRMED (grep: the fields are read only in `edit_model.cpp:2958-2964` and set only in
tests by direct assignment).

### R35G0-4: Medium: the settled add copies the existing assignment's row and checks neither the already-assigned resources nor effort-driven

Locations:
- `core/mppwrite/assignment_growth.cpp:1591-1611`. `model` is the first real row, copied
  into the new row with only uid and resource changed.
- `:1449-1451`. `check_measured_shape`, and with it `check_resource_calendar`, runs only for
  `key.resource_uid`.

Defect:
- In the settled shape (wave34-assign-add-second / -third), the existing real rows
  are checked for 100 % units, positive work, the zero word and the work copies
  (`check_assn_row_work`, `:1212-1233`), and for mutual identity.
- They are not checked for dates, and their resources' rows and calendars are never
  read.
- If the already-assigned resource has an exception or another base calendar, its row
  can hold dates no pair measured. The port then gives the new resource those dates.
- The task's effort-driven flag is also undecoded (R34b-2 deferral). Effort-driven
  matters exactly in this shape: spec/scheduling.md:558-566 says adding a resource to an
  effort-driven task keeps Work. The pairs grow it. Whether a byte compare catches the
  flag is unproven.

Reachability:
- The edit model refuses any assign on a task with a real assignment
  (`edit_model.cpp:2995-2999`).
- At save, the cascade's dry run compares the assignment uid (`:5576-5583`).
- So the shells do not reach this path except through an improbable stale file.
  This is a writer-width gap (the RC-W class), not a UI one.

Failure scenario: a direct writer caller (a future scheduler or the macOS shell
enabling the settled add) assigns R3 to a task already holding R2, where R2's calendar
has a day off inside the task. The port writes R3's row as a copy of R2's.

Fix:
- Run `check_resource_calendar` and the resource-row image check for every resource in
  `assigned`.
- Compare the model row's date words with a measured settled row.
- Refuse an effort-driven task once the bit is decoded. Until then, say in the writer
  header that the settled add is unproven for effort-driven tasks.

CONFIRMED that the checks are absent. That the output differs is PLAUSIBLE (no pair).

### R35G0-5: Medium: R34b-2's effort-driven half is deferred on a corpus search, not on an attempt to measure

Locations:
- `docs/handoff/wave35-complete.md`, "Deferred". Commit 3003681.
- `app/edit_model.cpp:2921-2925` checks only `type_block_stored`.

Defect:
- Brief rule 1 ("Deferral only with evidence of an attempt") allows deferral after an
  attempt.
- The record says no COM and no `oracle_*` script was run. The evidence is a search
  of committed files, but rule 4 allows a COM pair through `scripts/oracle_lock.py`.
- The type check is also block-presence only. A Fixed Work task whose type is stored
  somewhere other than VarMeta 0x0B400080 passes the edit model. The writer's FixedMeta
  compare then catches it only at the builder's dry run. That is plausible, not proven:
  no file with a non-default type has been decoded.

Failure scenario: a hand-built removal or assign on an effort-driven or Fixed Work task
passes `push()`. It is refused (probably) only at save. For the shells, this matters only
together with R35G0-4.

Fix: measure one pair (a Fixed Work task and an effort-driven task, each with a no-edit
save) through `oracle_lock.py`, decode the bits, and add the plan-side refusal.
Otherwise record the attempt's command, error and log path, as rule 1 requires.

CONFIRMED (process). Bit location UNCONFIRMED.

### R35G0-6: Low: the edit model reports "based on another calendar" for a resource that has no calendar row

`app/edit_model.cpp:2958-2962` and `app/reader_adapter.h:135-136`.
- A resource with no calendar row arrives as `calendar_base_uid = -1`. It gets the
  base-calendar message.
- The test at `app/app_edits_assignment_tests.cpp:635-636` pins that misleading text as the
  needle.

Fix: a separate branch for `-1` ("has no calendar of its own"), with its own needle.

CONFIRMED.

### R35G0-7: Low: the ResourceRow and TaskRow defaults equal the measured shape, so the new refusals fail open

`app/layout_model.h:213-219` (`calendar_base_uid = 1`, blocks 0) and `layout_model.h:140`
(`type_block_stored = false`).
- Any `ResourceRow` not built by `resource_row_from_mpp` passes the calendar refusal.
  Examples are `edit_model.cpp:4140` (a pending Add Resources row) or a future adapter.
- Today `push()`'s exclusivity keeps pending rows out of the assignment path, and both
  shells use `plan_input_from_document`. So nothing is reachable.

Fix: default to the refused values (`-1`, or a `calendar_known = false` flag). Set the
measured values only from the reader.

CONFIRMED (design). Not reachable today.

### R35G0-8: Low: the R34b-3 decode stops short, and the spec still calls +128 a save time

- `spec/mpp-records.md:154` still reads "save/current date ... same as the file mtime".
- The G0 evidence (`assignment_growth.cpp:938-942`, commit 432a825) shows otherwise: the
  word is unchanged by a no-edit save, one value per file, and differs only between
  fixture families.
- That fits the tasks' Created date (every task of a fixture made in one script run),
  not a save stamp.
- The refusal now names the word, so the brief's minimum is met. But Assign stays
  unusable on any real file, and the spec contradicts the code's own comment.

Fix: append a dated erratum to `spec/mpp-records.md`. Check +128 against COM
`Task.Created` on one control through `oracle_lock.py`. If it is Created, it is not a
scheduling input and can be copied through, citing the null saves.

CONFIRMED that the spec is stale. The Created reading is PLAUSIBLE.

### R35G0-9: Low: the RC-W6 whitelist does not pin the row for the project-WBS key, nor any payload

`core/mppwrite/varmeta_edit.cpp:126-155` and `:980-987`.
- `check_measured_key` matches (table, shape, key) only.
- The header (`varmeta_edit.h:101-107`) says 0x0B40019F is measured "on uid 0". But an
  append of 0x0B40019F to an ordinary task uid passes.
- Payloads are unpinned: a notes append takes any bytes, and no note encoder is
  measured (RC-W10). Presence bits are unpinned too.

Fix: add the uid class (project row or task row) to `MeasuredKey`. Refuse 0x0B40019F off
uid 0 and 0x0B40018D on it. Note that payload validation rests with the caller, or pin
the measured payload forms.

CONFIRMED. No shell caller passes those values.

### R35G0-10: Low: RC-W7 pins the record ids but not their measured sizes

`core/mppwrite/props_edit.cpp:111-114, 357-363`.
- The four ids are whitelisted.
- Any equal-or-larger payload is still accepted for them. The measured sizes are
  0x0240138b 4 -> 24 B, 0x024013be 2 -> 6 B, 0x024013c5 8 -> 16 B and 0x02401392
  equal size.

Fix: pin (id, old size, new size) to the pairs.

CONFIRMED.

### R35G0-11: Low: RC-W8 duplicates the pinned cell in two layers and prints the field id in decimal

- `core/mppwrite/viewstream_edit.h:145-146` (`kMeasuredCellUid`, `kMeasuredCellFieldId`)
  and `app/edit_model.h:575-576` (`kBoldMeasuredTaskUid`, `kBoldMeasuredFieldId`) are
  separate constants that can drift.
- `viewstream_edit.cpp:589-595` prints `188743694` for 0x0B40000E.

Fix: have the app constants alias the core ones, and print the field id in hex.

CONFIRMED.

### R35G0-12: Low: R34b-9 (no positive push() control for a hand-built removal) is still open, and G0's new removal push() tests rely on it

- `app/app_edits_assignment_tests.cpp:660-665` and `:697-702` assert that `push()` refuses a
  hand-built removal on a changed `remove_plan`.
- No test shows the same hand-built removal on the unchanged `remove_plan` is
  accepted. The comment at `:670-672` points to the builder's pair, which is not a
  `push()` control. Lines 869 and 901 are refusals; 892 and 990 are assigns.
- Planted mutation: make `push()` refuse every `unassign_resource`. The ShapeCase and
  AssignmentCase removal `push()` checks stay green.

Fix: add `check(edits.push(hand_edit(false, kRemove.task_uid, kRemove.resource_uid, new),
&remove_plan))` before the refusal loops.

CONFIRMED (the test is absent). That it is vacuous is PLAUSIBLE.

### R35G0-13: Low: R34b-4's lesser case is not addressed

- `tests/roundtrip_assignment_growth.cpp` still masks the appended assignment record's
  own +0 dword.
- Nothing asserts that it is unique against the other identities (review-34b R34b-4,
  "Lesser case").
- de17e55 fixed only the resource record's +24.

Fix: assert that the new +0 identity differs from every other identity in the
Assn/Task/Rsc Fixed2Data, in both Project's file and the port's.

CONFIRMED (no such assertion in the range).

## Checked and found sound

- **W35-0.1 writer:**
  - `check_resource_calendar` runs inside `check_measured_shape`. That covers all three
    paths: first add `:1449`, settled add (the same call) and removal `:2249`.
  - `check_project_calendar` is on all three, at `:1696`, `:2021` and `:2473`.
  - Bounds hold. FixedData is `64 + 12n` (checked). FixedMeta entries equal rows + 4
    (checked via `check_meta`, header +12 equal to the FixedData size), so `entry_at(mine) + 9`
    is in range. `mine` and `standard` are dereferenced only after their counts are 1
    (short-circuit). VarMeta offsets and lengths are validated in `load_var_meta`.
  - The leading-4-spare-slot indexing agrees with `resource_growth.cpp:819-824`, which
    appends a new calendar's entry at index rows + 4.
  - Any block keyed by the resource calendar's uid is refused, whatever its key, so the
    refusal does not depend on decoding the exception format.
  - Tests cover base, row count, word 154, Standard row, both flag bytes, name, exception,
    Standard exception, project calendar and week. They run for add and removal, plus base
    and project calendar on the settled add. Removing any one gate leaves its case with no
    other source of its needle.
- **W35-0.1 edit model:**
  - Base uid and both block counts are checked in `assignment_plan_refusal`, which the
    builder (`:3015`) and `push()` (`:3640`) share. That covers add and removal.
  - The fixture plan's positive assign proves that the reader yields the measured values
    (1, 0, 0) on the pairs.
- **R34b-5:** The writer checks word 154 == max calendar uid, that the row is owned by the
  template resource, and that the written word equals `cal_uid` (`resource_growth.cpp:691-737`).
  It has three tests.
- **R34b-6:** `edit_model.cpp:4616-4622` refuses before any write or the hook. The test checks
  the hook count, the target file and the queue. All six exclusive kinds now carry the
  `document_edits.any()` refusal (4616, 4651, 4683, 4715, 4746 and 4777).
- **RC-W1 and RC-W2:**
  - The gates come before the FixedMeta checks, with specific needles.
  - The RC-W2 premise correction is right: per `tests/data/README.md:103`, the second pair
    adds ID 1 to [2] and the third adds ID 3 to [1, 2].
- **RC-W8:** both pins run in `validate_edit`, before the document is touched.
- **Public-repo hygiene:** the diff contains no client, project or person names and no
  personal or company paths. The only hits are context lines naming the existing
  `holiday1.mpp` / `PROJECT_A` corpus labels. The commit author metadata is the git
  identity used for 458 earlier commits on `main`. It is pre-existing and outside G0.

## Per-item verdict

| Item | Verdict | Note |
|---|---|---|
| W35-0.1 (R34b-1) | complete in the writer; incomplete in the edit model | Writer: base, blocks, flags, Standard, project calendar and week, for add, settled add and removal. Edit model: base and blocks only (R35G0-2). The extra-blocks reader path is untested (R35G0-3). The settled add's existing resources are unchecked (R35G0-4). |
| W35-0.2 R34b-2 | incomplete (deferral) | Type (block presence), removal units and work added at the builder and `push()`. Effort-driven deferred without a measurement attempt (R35G0-5). |
| W35-0.2 R34b-3 | complete (brief's minimum) | The word is named in the refusal. The spec is stale and the decode is unfinished (R35G0-8). |
| W35-0.2 R34b-4 | complete for +24; lesser case open | R35G0-13. |
| W35-0.2 R34b-5 | complete | Writer equality and ownership, three tests. |
| W35-0.2 R34b-6 | complete | |
| W35-0.2 RW34b-2 / RW34b-3 | complete | Specific needles. Qt gates for the 34b refusals, including file-driven calendar shapes. |
| W35-0.3 (CI-4) | incomplete | Wrong cause. The title is empty on macOS. `qt_prompt_hook` adds a new macOS red (R35G0-1). |
| W35-0.4 RC-W1 | complete | |
| W35-0.4 RC-W2 | complete | |
| W35-0.4 RC-W6 | complete for the brief's wording | Uid and payload unpinned (R35G0-9). |
| W35-0.4 RC-W7 | complete for the brief's wording | Sizes unpinned (R35G0-10). |
| W35-0.4 RC-W8 | complete | Duplicate constants, decimal message (R35G0-11). |
