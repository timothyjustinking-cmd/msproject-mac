# Wave 34 G4 + CLOSE adversarial review (read-only)

Scope: `git diff 3a8a31c..8400c2c` (13 commits, 9c5f497 .. 8400c2c). Reviewer: independent, read-only. Started 08 November 2026.

Status: COMPLETE (08 November 2026). Read-only: no tracked file was changed. Overlap with the CLOSE review: R34G3-1/RC-R1 (assign work amount) and R34G3-2 (identity mask) are not repeated here. R34G4-1/-2 restate RC-R2 with evidence and upgrade it. R34G4-3 widens RC-W3 and shows the gap is reachable from the UI.

## Findings

### R34G4-1 (High, CONFIRMED in code; Project side PLAUSIBLE) - Assign Resources lets through automatically scheduled tasks; every committed pair is a MANUAL task

- Where: `app/edit_model.cpp:2816` (`assignment_plan_refusal`), `core/mppwrite/assignment_growth.cpp` (no task-mode check anywhere; `grep -i manual` finds nothing).
- Pair data: `tests/data/README.md` row for `wave30-assign-add` says the fixture is "three top-level 0 % MANUAL leaves"; `wave34-assign-add-second/third` are built on the same file ("three 0 % MANUAL leaves"); `wave30-assign-delete` is the same file. No pair assigns to an automatically scheduled task.
- Gate: the plan rule checks summary, percent, actual start/finish/duration, resource type, Max. Units and the real-assignment count. It never reads `TaskRow::manually_scheduled`. The writer's dry run checks geometry, placeholder work, roll-up words and flag bytes (0x07), none of which encode the task mode.
- Failure: open any Project file whose tasks are auto-scheduled (Project's default) with placeholder-only assignments and a 100 % work resource, Assign Resources on an unstarted leaf, save. The port writes the manual-task shape (the placeholder row's work and dates copied verbatim). Project's scheduler runs on an auto task when a resource is assigned (it can re-time the assignment to the resource calendar and rewrites the assignment and task scheduling words), so the outputs are not shown to match. The pair's own assignment starts on 2026-01-04 13:00, a Sunday, which is only possible because the task is manual: an auto task's assignment would not be dated like that.
- Missing: a refusal of `!task->manually_scheduled` (or an auto-task pair), and a refusal test.

### R34G4-2 (High, CONFIRMED in code; Project side PLAUSIBLE) - Assign Resources / removal ignore resource rates, the task calendar and the resource calendar

- Where: `app/edit_model.cpp:2816-2867`; `core/mppwrite/assignment_growth.cpp:748-830` (the row checks are units, work, a zero word and the work copies only).
- The plan rule checks `type` and `max_units_percent` of the resource but not `standard_rate_per_hour`, `overtime_rate_per_hour`, `cost_per_use` or `accrue_at`, and not `TaskRow::calendar_uid` or the resource's base calendar. The writer copies the placeholder row (whose cost words are 0) and writes only the work roll-ups (task, project, resource +48/+90/+106) and the DocumentSummaryInformation work strings.
- Failure: a file with a work resource at a non-zero Standard rate (or a Cost/Use), Assign Resources on a leaf, save. Project writes Cost = work x rate (+ Cost/Use) on the assignment, the task, the project row and the resource; the port writes 0 everywhere. Same for the removal of such an assignment (the task/resource costs would be left stale). A task calendar or a resource calendar with different working time changes Project's assignment finish/contour; the port copies the placeholder's dates. None of these shapes has a pair (all pairs use default-rate resources on the project calendar), and none is refused. Contrast W34-4.2, which does refuse a rated template (`add_resource_template_refusal`), and the W30-0.6 percent writer, which refuses a task calendar for the same contour reason (`app/layout_model.h:144-149`).
- Missing: refusals for rates/Cost/Use, a task calendar and a non-default resource calendar, each with a test.

### R34G4-3 (High, CONFIRMED in code) - Add Resources clones the template row's work roll-ups (and every other unchecked FixedData word) into the new resource

- Where: `core/mppwrite/resource_growth.cpp:600-609` copies the last live row's whole 172-byte FixedData record and changes only id, uid and the +154 counter; validation (`:496-509`) checks only MaxUnits and the 0x55/0xe3 var blocks. `app/edit_model.cpp:2704` (`add_resource_template_refusal`) checks only type and the three rates.
- Failure (reachable from shipped fixtures): open `wave30-assign-add.mpp` (or the port's own save of the enabled W34-4.3 assign on `wave30-assign-add-control.mpp`), which has two resources with the last one (R2) carrying 40 hrs of work in Rsc FixedData +48/+90/+106 (the assign writer itself sets those words). Every dry-run condition still holds: 3 resource rows, 4 calendar rows (assign does not touch the calendar table), the Props resource scalars unchanged by the assign, rates zero, MaxUnits 100 %. Add Resources "R3" then writes a new resource whose work words are 2400000 (40 hrs) with no assignment. Project's Add Resources writes 0. The same clone carries the template's Accrue At, flags and any actual/overtime/remaining-work words; `StructuralEdits::apply` (`app/edit_model.cpp:3985-3999`) even notes "the accrual ... ride[s] along", so a template with Accrue At = Start gives a new resource Project would create as Prorated.
- Neither committed pair has an assigned or non-default-accrual template, so the pair tests cannot see it.
- Missing: a check that the template's work/cost/actual words are zero and its accrual is the default (or the writer writing Project's defaults instead of cloning), plus a refusal test with an assigned template (the fixture already exists).

### R34G4-4 (High, CONFIRMED in code; Project side PLAUSIBLE) - Font Bold always writes into CV_iew uid 2 without proving that uid 2 is the applied task view, and stores an undecoded font word

- Where: `app/edit_model.cpp:2964-2973` (`cell_bold_view_edit` hard-codes `mppwrite::kMeasuredViewUid`); `core/mppwrite/viewstream_edit.cpp:180-365` (`load_view` checks the framing of the uid-2 main block, the VarMeta offsets, "no cell-format item yet" and the Props size tag) and `:396-400` (refuses only a caller-supplied uid other than 2). Nothing reads which view the file has applied, or what kind of view uid 2 is.
- Parameters a user varies: row (pinned to uid 1), column (pinned to Task Name), selection (single cell, checked in Qt), the file's view table (NOT pinned), the view's text style or font (NOT pinned), existing formats (any cell-format item in the block refuses).
- Failure: in any file other than the synthetic base, CV_iew uid 2 is whatever view that file created second. It can be a resource view, a usage view or a Gantt that is not the current one. The dry run accepts any uid-2 block with the measured framing, and the port appends a Task Name cell format for task uid 1 there. Project's Bold goes into the view the user is in. The spec says uid 2 is "the pair's applied view" (`viewstream_edit.h:131`), which is a fact about one file, not a rule. Separately, `viewstream_edit.h` says no bit of the font word `0x90000001` is decoded. Whether Project writes the same word when the view's base text style is a different font or size is unmeasured, and no file-side check pins the text style.
- Missing: a check that uid 2 is the file's applied Gantt/task view (or a refusal for every file that does not have the pair's view table), a refusal for a non-default text style, and tests for both.

### R34G4-5 (High, CONFIRMED in code; Project side PLAUSIBLE) - Define WBS Code numbers the tasks by UniqueID, not by ID (row) order, and no gate pins ID = UniqueID

- Where: `core/mppwrite/wbs_code.cpp:206-215` (`number_payload(uid)` for uid 1..3); `wbs_code.cpp:66-107` checks that physical row r holds uid r; `app/edit_model.cpp:3016-3040` (`wbs_code_plan_refusal`) checks the uid set {1,2,3}, `file_row`, `summary` and `outline_level`, and never `TaskRow::id`.
- Failure: a three-task flat file whose rows were reordered in Project (Move Task or a drag), for example uid 3 at ID 1. `TaskRow` documents that the adapter sorts tasks into ID order because "vector position is no longer the stored order" (`app/layout_model.h:86-92`), so physical-uid order and ID order can differ. Project's Renumber issues numbers in outline (ID) order and gives uid 3 "01". The port writes "03" for uid 3. In the pair, IDs equal UniqueIDs, so the pair test cannot see this.
- Missing: refuse unless `id == uid` for all three tasks (or number in ID order with a pair that measures it), plus a test.

### R34G4-6 (Medium, CONFIRMED in code; Project side PLAUSIBLE) - Rename and Add Resources accept names that Project's own UI rejects

- Where: `app/edit_model.cpp:2558-2578` (`rename_name_refusal`) and `:2650-2672` (`add_resource_name_refusal`). Any printable ASCII is accepted, including `,` `[` `]`. The rename has no length cap below the writer's 65,535 bytes.
- Failure: Project refuses a resource name that contains the list separator or square brackets, because they delimit the task-side Resource Names text. The port writes such a name, and no pair measures it. A later assignment then shows a Resource Names string that Project parses as two resources. Project's resource-name length limit is also unmeasured for the rename (Add Resources caps at 64).
- Missing: refuse `,` `[` `]` (and cap the rename length), with tests.

### R34G4-7 (Medium, CONFIRMED) - The CLOSE records say more than the code does

- `docs/handoff/wave34-verification/close-review.md`, "Per enabled command": it says Add Resources, Font Bold and Define Code refuse "every other shape" in the shell. R34G4-3, R34G4-4 and R34G4-5 are shapes the shell builders let through.
- RC-W3 says "the shell builder refuses non-default rates; the writer does not", and lists "a resource with work" as a writer-only gap. The shell does not refuse a template with work (R34G4-3), so the gap is reachable from the UI, not a defence-in-depth gap.
- RC-R2 is filed as a "SUSPECTED" Medium. The rate half is certain from the code: no rate is read anywhere on the assign path. Together with R34G4-1 and -2, it is a UI-reachable save of an unmeasured shape, which is High under the brief's own rule.
- `close-review.md`, "Section 4 (e)", says "The record's G4 entries carry the evidence each item attached". No G4 entry in `wave34-complete.md` carries any Project-reopen evidence: a case-insensitive search for "reopen" finds only the core/mpp reader reopen and the CLOSE "NOT RE-RUN" row. Brief section 4(e) was never run for any of the five enabled commands, yet all five rows say DONE. "NOT RE-RUN" implies an earlier run that did not happen.
- `docs/handoff/wave34-complete.md:972-985` (4.3) says "the identity dwords of the appended Assn Fixed2Data record [are] compared under `TailNormalisation`". They are masked, not compared (R34G3-2 is that finding).
- The 4.2, 4.3, 4.5 and 4.6 rows each say "every other shape refused up front". Decision 15 says the three dialogs "refuse every other shape in the shell (Assign Resources not yet for the work amount)". R34G4-1 to R34G4-6 contradict both.

### R34G4-8 (Medium, CONFIRMED in code; concurs with RC-R3) - The new exclusive kinds can share a save with the document-edit hook

- Where: `app/edit_model.cpp:4462-4584` (each exclusivity block tests `edits.applied().size()` and `writable` only); `app/document.cpp:760-771` passes `view_state_edits_` and `custom_formula_` whenever they are pending; the hook runs at step 5c, before steps 5f-5j.
- Failure: queue a table-format (`214/CTable`) or custom-field formula edit, then Font Bold, Add Resources, Assign, Rename or Define Code. `push()` accepts the exclusive edit, because the document edits are not in `applied_`, and one save writes both. For Bold, two 214 view writes then land in one save. The rule "one change on an otherwise untouched save" is therefore not enforced, and no test covers it.

### R34G4-9 (Low, CONFIRMED) - Refusal tests whose needles match any refusal

- `app_qt/tests/resource_add_gate_test.cpp:199` and `:276` ("add resources:") and `app_qt/tests/assign_resources_gate_test.cpp:280` ("assign resources:"): the needle is the handler's own prefix, so it matches every refusal the handler emits, whether from the builder, from push or for a different builder reason.
- The app tests' file-side refusals use `"not implemented in this port"` (`app/app_edits_assignment_tests.cpp:620-662`). One example is "a file whose 114/Props is not the measured shape", which truncates Props to 40 bytes. That needle matches any writer refusal.
- None of these tests can show that the gate they name actually fired.

### R34G4-10 (Low, PLAUSIBLE) - New resource and assignment UniqueIDs are max-live + 1

- `core/mppwrite/resource_growth.cpp:476-494`; `assignment_growth.cpp` (`new_uid` = largest assignment uid + 1). Project takes the next UniqueID from its own counter. In a file where a resource or assignment row was deleted and compacted, that counter can be above max + 1, and the tombstone refusal does not fire because there is no tombstone. This shape is unmeasured and not refused.

### R34G4-11 (Low, CONFIRMED in code; concurs with RC-W9) - The WBS dry run writes a full copy of the project to the shared temp directory under a predictable name

- `core/mppwrite/wbs_code.cpp:178-186`: the path is `temp_directory_path()/w34-wbs-dry-<n>.mpp`, where n is a per-process counter starting at 1. It is written on every Define Code click (the builder's dry run), not only on save. On a shared `/tmp`, another user can pre-create or symlink the name, and two app instances collide. If the process dies mid-chain, a copy of the project is left outside the user's chosen location.

## Per command: what can be varied and what is measured

| command | parameter a user can vary | pinned by a gate? | covered by a Project pair? |
|---|---|---|---|
| Rename (4.1) | which resource | any saved, unassigned resource | uid 2 only (one pair) |
| | new length | strictly larger, up to 65,535 B | 6 -> 8 B (plus the Task note re-point, 197 -> 209 B) |
| | characters | printable ASCII, including `,` `[` `]` | plain ASCII with no delimiters (R34G4-6) |
| | assigned | refused | n/a |
| Add Resources (4.2) | existing count | 1 or 2 | yes, both |
| | template rates, type, MaxUnits | yes | default only |
| | template work, accrual, flags | **no** (R34G4-3) | no |
| | calendar count | rows + 1 | yes |
| | name | 1..64 printable ASCII | 2 characters |
| Assign (4.3) | task duration / work | **no** (R34G3-1 / RC-R1) | 40 h only |
| | task mode | **no** (R34G4-1) | MANUAL only |
| | task calendar, resource calendar, constraint, task type | **no** (R34G4-2) | project calendar, defaults |
| | resource rates, Cost/Use | **no** (R34G4-2) | zero |
| | which task (row position) | no | task uid 1 only |
| | existing assignments in the project | writer: all placeholders (first shape) | yes |
| | units | 100 % (resource MaxUnits and placeholder 10000) | yes |
| Remove (4.3) | as for Assign, plus where the settled file came from | writer: Project-saved settled shape only | one pair |
| Bold (4.5) | row, column, single cell | yes | uid 1, Task Name |
| | which view is uid 2, text style | **no** (R34G4-4) | the synthetic base only |
| WBS (4.6) | task count, uids, outline, summary | yes | yes |
| | ID order | **no** (R34G4-5) | IDs = UIDs |

## Checked and fine

- 4.0: the five kinds are refused in the builders, in `push()` and in the cascade before the first write. The needles name the command and the stream count (`app/app_unmeasured_paths_tests.cpp:92-184`). `allow_unmeasured_paths_for_tests(true)` is called only from test sources.
- Shell parity: macOS wires only Add Resources (`app/main.mm` `addResources:`), through the same builder and `push()`. No command is enabled on macOS that Qt refuses, and none the other way round.
- The WBS pair test's coordinate shift (`tests/roundtrip_wbs.cpp:176-215`) inserts identical zero runs into the control and the null save. The mask stays the control-vs-null difference, and the inserted bytes are compared against Project's file.
- The resource-growth VarMeta loader bounds (`resource_growth.cpp:246-282`) are sound. The new WBS and rename paths read through bounds-checked helpers. No new overflow was found beyond R34G3-3.
- The assignment identity references masked at +16/+32 (R34G3-2) are backed by fixed-offset self-checks (`tests/roundtrip_assignment_growth.cpp:494-502`) that do not use the writer's own row lookup. This limits R34G3-2 but does not close it.

## Count

0 Critical, 5 High, 3 Medium, 3 Low.
