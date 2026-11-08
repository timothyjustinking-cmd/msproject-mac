# Wave 34b: completion record

Branch `wave34b`.  Push and CI: by the coordinator.  Logs are under `build/w34b/`
(untracked, named for the parent commit and the item).

## G0

### W34b-0.1 (R34G3-1 = RC-R1, R34G4-1, R34G4-2)

Assign Resources and its removal refuse every shape the four assignment pairs did not
measure, in the shared edit-model builder, in `push()` and in the writer.  Commit
`f08249e`.  RED then GREEN: writer 85 failures of 1983 checks to 0 of 1983; edit model
54 failures (784 passed) to 0 of 886.

### W34b-0.2 (R34G3-2, RC-W4)

**Assignment Fixed2Data (R34G3-2).**  The appended record is `[own identity][task
identity][resource identity]`, 16 bytes each.  The control and its null save differ on
every existing record at +0, +16 and +32 (for example the task's own word `89b9c496` to
`f958d0d0`), so the first dwords are per-save renumbering and stay masked as absolute
values.  They are references, though: +16 is the first 16 bytes of the task's Task
Fixed2Data record (record uid + 3) and +32 is the first 16 bytes of the resource's
Resource Fixed2Data record (record uid + 3), or the placeholder identity for an
unassigned row.  The harness now resolves them in Project's save and in the port's
output in all four pairs (`identity_reference_problem`), and plants a wrong task and a
wrong resource reference in scratch copies of the port's output; each is reported.

**Calendar Fixed2Data +32 (RC-W4).**  A reference, not a counter.  The control and null
save of the resource-add pairs differ at Cal Fixed2Data 272, 304 and 320 (record 5 +32,
record 6 +16 and +32), Rsc Fixed2Data 160 and 184 (record 4 +0 and +24) and the 16-byte
identity in `114/Props` (2834 in the wave 30 pair, 2854 in the third); the calendar
records' own +0 (240, 288) is identical in both.  So +16 is the identity of the
calendar's resource and +32 is the one identity every calendar record shares, held once
in `114/Props`.  The harness (`calendar_identity_problem`) requires, in Project's save and
in the port's output, that the appended record's +16 is the new resource's identity,
its +0 equals its +16, and its +32 is the other calendars' and the single one in
`114/Props`; it plants a wrong +16 and a wrong +32.

**Red run.**  With the writer deliberately changed to put the previous resource's
identity at the calendar record's +16 the pair comparison (`compare_all`) reported no
failure, because the first dword is masked and the remaining twelve bytes are the same
stable suffix; the new check failed (`build/w34b/f08249e-W34b-0.2-rsc-red.log`).  For
the assignment record, the writer with the wrong resource identity fails the new
Project-tied check in the first pair and both settled pairs
(`build/w34b/f08249e-W34b-0.2-red.log`, 7 failures).  Both changes were reverted;
GREEN: assignment growth 2007 checks, resource growth 899 checks, 0 failures
(`build/w34b/f08249e-W34b-0.2-green.log`, `-rsc-green.log`).

### W34b-0.3 (R34G4-3, RC-W3)

Add Resources clones the last resource's 172-byte row, its FixedMeta entry and its
Fixed2Meta entry; it now writes only when those equal the template both pairs measured
(a work resource at 100 %, no work, cost, accrual, calendar or flag set).  The writer
(`add_work_resource`) compares every byte outside the ones it rewrites (row id, uid and
counter 0..7 and 154..157; entry offset, session bit and state bytes) and names the
first differing offset; the two controls agree on the template byte for byte.  The edit
model (`add_resource_template_refusal`, shared by `make_add_resource_edit` and
`push()`) also refuses a last resource whose Max. Units is not 100 %, whose stored
accrual is not the default, or that holds an assignment.

The reproduction, `wave30-assign-add.mpp` (last resource with 40 h of work), is refused
at the writer ("row offset 52") and at the edit model ("holds an assignment", and the
writer's dry run when the plan hides the assignment).  RED then GREEN: writer 33
failures of 932 checks to 0 of 932 (`build/w34b/afb0aab-W34b-0.3-red.log`,
`-green.log`); edit model 7 failures (815 passed) to 0 of 832
(`-app-red.log`, `-app-green.log`).  Both measured pairs still write and match Project's
pair field by field (the `resource-add-second` and `resource-add-third` pair tests are
inside the 932).

### W34b-0.4 (R34G4-4)

Font Bold wrote into the view with UniqueID 2 whatever the file applied.  The writer
(`save_view_cell_style`, so the dry run in `make_cell_bold_edit` too) now requires two
things.  (1) The file's applied view is uid 2: `214/CEdl` names it (record
`0x26400018`), the `214/CV_iew` FixedData row with that caption (ignoring `&`) must be
exactly one and carry uid 2.  `wave31-resource-sheet.mpp` (uid 4) and
`wave31-usage.mpp` (uid 5) are committed files that apply another view.  (2) The uid-2
main block equals the measured control's outside the bytes the edit rewrites (the two
size words and the item count): length 42,692 and an FNV-1a 64 digest
(`0xc4a86de4acee2cfb`) that the wave30-textstyle control and its null save share with 67
other committed fixtures saved in the same Project session; the fixtures' other digest
groups (for example the `synthetic-base` family) are refused as
unmeasured.  That pins the text style the Bold item sits on, the font table and every
other item.

RED then GREEN (fix reverted, tests kept): writer `mppwrite_roundtrip_viewstream` 13
failures of 477 checks to 0 of 477 (`build/w34b/f8452b4-W34b-0.4-red.log`,
`-green.log`); shell `app_edits_cell_bold_tests` 3 failures (373 passed), all UNEXPECTED
ACCEPT, to 0 of 382 (`-app-red.log`, `-app-green.log`).  The measured cell still
writes and matches Project's pair (the pair test is inside the 477 and the 382).

### W34b-0.5 (R34G4-5, RC-W5)

Define WBS Code stamped "01" "02" "03" from each row's UniqueID, but an ordered code
numbers a task by position, and the writer accepted any three-task file.  The writer
(`save_wbs_code`, so the dry run in `make_wbs_code_edit` too) now also refuses a task
whose ID is not its UniqueID, a task whose outline level (FixedData +172) is not 1, a
task that is a summary (FixedMeta +26 bit 0x80, or the FixedData +140 has-children word)
and a FixedMeta state other than live, each with its own needle ("has ID 2 but UniqueID
1", "has outline level 2", "is a summary", "FixedMeta state").  The edit model
(`wbs_code_plan_refusal`, shared by the builder and `push()`) refuses a plan whose task
ID differs from its UniqueID or whose stored row differs from its UniqueID.

Reproductions: three tasks with IDs 2, 1, 3 against UniqueIDs 1, 2, 3 (rows reordered),
one ID that is not its UniqueID, the nested `wave28-outline-indent.mpp`, and the level,
summary-bit, has-children and deleted-state words changed alone on the measured control.
RED then GREEN (fix reverted, tests kept): writer `mppwrite_roundtrip_wbs` 24 FAIL lines
to 0 of 368 checks (`build/w34b/dee4998-W34b-0.5-red.log`, `-green.log`); shell
`app_edits_wbs_tests` 6 failures to 0 of 383 (`-red-app.log`, `-green-app.log`).  The
nested file and the deleted state were refused before only by an unrelated downstream
VarMeta message; the new needles prove the refusal now comes from the numbering rule.
The measured pair still writes and matches Project's pair field by field (the
`wbs-code` pair totals, noise=149 effects=216, are unchanged and inside the 368).
