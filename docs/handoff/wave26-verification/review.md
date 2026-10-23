# Independent review of wave25..wave26 (save-path first)

Reviewer: fresh-context independent reviewer, 2026-10-24.  Read-only: no build,
no git write, no tracked file edited.  Scratch scripts (byte diffs of the
committed corpus and of the fixer's own test outputs) are under
build/verify-w26/scratch/ (pctdiff.py, varmeta.py, binstrings.py, row0prio.py).
build/audit-w22/verifier-notes*.md was not read.

## Verdict

NOT CLEAN on the save path.  Wave 26 adds no new core/mppwrite code; its two
new save paths are both in app/edit_model.cpp step 5a and both use the
pre-existing single-word TaskTable setters.  Project Priority (task rows) is
genuinely single-copy (Project's own var-priority.mpp save changes only
FixedData +78), but PercentComplete is NOT: the repo's own Project-written
fixture corpus/probe/var-pct.mpp shows Project's 50 % edit changing 35 bytes of
114/TBkndTask/FixedData across the task row AND the project row (ActualDuration
+80, RemainingDuration +88, PercentComplete +92, ActualStart +120, the
+132/+136 dates, a flag at +168, and the project row's rolled-up +80/+88/+92)
plus a new Var2Data field 0x0B40015E, while the port's own saved
app-edit-percent.mpp differs from its source in exactly one byte (+92).  The
port therefore saves a row shape that, per the reader's own comment, no
Project-written file contains, and the wave's own record says "the port has no
actuals writer" when it disables Update Project's OK.  The %-complete buttons
should be High until a COM measurement shows Project keeps the value AND the
derived actuals; summary rows make it worse.  The other Priority-2 areas
(project-summary fold, backstage guards, Task Form, Options) hold up; the
remaining findings are Medium/Low.  Privacy: clean in the new code, fixtures and
measures JSON.  Counts: Critical 0, High 2, Medium 4, Low 7.

## Findings (most severe first)

### 1. High - % Complete is saved as one word; Project writes eight-plus copies and the project-row rollup

- sha: e803933 (W26-1.2); comments repeated in 7b2268e
- files: app/edit_model.cpp:922-945 (step 5a), app/edit_model.cpp:331-343
  (push), app/edit_model.h:82-86 and :326-328 ("the writer has exactly one
  stored copy of PercentComplete ... so no cross-stream fix-up"),
  app_qt/main_window.cpp:2072-2108, docs/handoff/wave26-complete.md:102-109.

What the port writes: TaskTable::set_percent_complete(row, p) puts a u16 at
FixedData +92 of the one row and nothing else.  Confirmed on the fixer's own
output: build/w26-fresh-app/test-out/app-edit-source.mpp ->
app-edit-percent.mpp differs only in 114/TBkndTask/FixedData, row 1, +92
0 -> 0x28 (one byte); no other stream changes.

What Project writes for the same edit (corpus/probe/base.mpp ->
corpus/probe/var-pct.mpp, task 1 set to 50 % by Project, scripts/
probe_fixtures.py): task row 1: +80 ActualDuration 0 -> 12000, +88
RemainingDuration 24000 -> 12000, +92 0 -> 50, +120 ActualStart NA ->
0x3bf11e78, +132 NA -> 0x3bf412c0, +136 NA -> 0x3bf327d8, +168
0x00000080 -> 0x00009080 (a progress flag bit); project row 0: +80 0 -> 5400,
+88 21600 -> 16200, +92 0 -> 25 (the project's rolled-up % complete), +120/
+132/+136 dates; VarMeta gains (uid 1, 0x0B40015E) - the 76-byte block that
scripts/fk_build.py:45 already records as "appears only after PercentComplete
is set".  For contrast, Project's own Priority edit (var-priority.mpp) changes
only FixedData +78 - so Priority really is single-copy and PercentComplete is
not.  The repo already knew: core/mpp/reader.cpp:93-102 says "Every
Project-written row with progress has +120; the four rows with progress and no
+120 are writer-patched fixtures that changed PercentComplete alone", and all
38 100 % rows carry +124 ActualFinish; spec/scheduling.md 2.4 says
RemainingDuration = Duration - ActualDuration for a started task and that
completing a task sets RemainingDuration = 0.  The port's 100 % button saves
RemainingDuration = Duration, ActualDuration = 0, ActualStart/ActualFinish NA.
The wave's own Update Project record (wave26-complete.md:754-757) disables OK
because "the port has no actuals writer" - the % buttons are that actuals write.

Why the "single copy survives" evidence does not cover this: spec/mpp-writer.md
8.1's pv-percent result (Project shows 50) was measured on base.mpp task A,
which is Manual=True, and the oracle (scripts/oracle_patch_variants.py
cmd_open) read only Priority/PercentComplete/Duration/Start/Finish - never
ActualStart, ActualDuration, RemainingDuration, ActualFinish, PercentWorkComplete
or the project summary's % complete.  spec/mpp-writer.md:972 also records that
on auto-scheduled added rows "percent/dates [were] recalculated", which is the
case the ribbon hits for any auto-scheduled task.  Consequences on reopen in
Project are therefore at best a task showing N % with 0 d actual duration, full
remaining duration and no Actual Start (wrong progress bar, status/earned value,
and the next recalculation free to rewrite it), and at worst the value
re-derived to 0 for auto-scheduled tasks.  In the port itself the saved file
re-reads the project row's stale % (it is never updated), so the port's own
project row disagrees with its tasks after a save.

The gates do not test the claim: app/app_edit_tests.cpp:489-532 and the ribbon
gate (app_qt/tests/ribbon_inventory_test.cpp:1096-1140) read the value back
through the port's own reader of the same word (+92), so they would pass for
any byte Project ignores.

Falsifying test (COM; the reviewer cannot run Project): (a) open
build/.../app-edit-percent.mpp read-only and, for the edited uid, read
PercentComplete, ActualStart, ActualFinish, ActualDuration, RemainingDuration,
PercentWorkComplete, and ActiveProject.ProjectSummaryTask.PercentComplete, then
CalculateProject and re-read; repeat after setting the task's Duration +1 d (does
Project re-derive % from ActualDuration?).  Do it for an auto-scheduled and a
Manual=True leaf at 40 % and 100 %.  (b) In Project set the same task to 40 %
via Task.PercentComplete and save; the stream diff against the port's file is
the list of copies the writer must fill.  If (a) shows Actual Start NA or a
% other than 40 after recalculation, this stays High.  Smallest fix: disable the
five buttons with the decision-9 tooltip (as Update Project's OK is) until a
measured actuals writer exists, or route through write_task_row with
actual/remaining filled plus +120/+124 and the project-row rollup, proven by (b).

### 2. High - % Complete is accepted on summary tasks, whose % Project computes from the children

- sha: e803933
- files: app_qt/main_window.cpp:2085 (only uid 0 is refused),
  app/edit_model.cpp:331-343 (push refuses uid 0 and pending deletes only),
  app/edit_model.cpp:570-581 (apply sets the summary row's value directly).

In Project a summary task's % Complete is a rollup of its subtasks' actual and
total durations; setting it on a summary distributes actuals to the children.
The port writes +92 on the summary row only and leaves every child at its old
value, so the saved file holds a summary % that disagrees with its children -
the "copies disagree" shape spec/mpp-writer.md 8.1 says Project recalculates
away.  The ribbon gate picks "the first non-zero uid" row
(ribbon_inventory_test.cpp:1104-1110), so on a file whose first task is a
summary the gate exercises exactly this path and still passes.  Falsifying
test: COM-open a port-saved file where a summary task was set to 50 % and its
children are 0 %; read the summary's PercentComplete after open and after
CalculateProject (expect 0, i.e. the edit lost).  Smallest fix: refuse
set_percent when the row is a summary (TaskRow::summary) until a distributing
writer exists, with a gate on a summary row.

### 3. Medium - Project Information Priority is mapped to project-row FixedData +78 without any measurement

- sha: 7b2268e (W26-5.1)
- files: app/edit_model.cpp:935-941, app/edit_model.h:87-91,
  app/reader_adapter.h:141-144, app/project_dialogs_tests.cpp:138-160,
  wave26-complete.md:692-696.

Every Priority measurement in the repo (var-priority.mpp, pv-priority.mpp,
spec/mpp-writer.md 8.1) is a TASK row.  No fixture has Project Information's
Priority changed by Project, so neither "Project Information Priority == row 0
+78" nor "row 0 +78 is the only copy" (114/Props or the project row's Fixed2Data
record could also carry it) is measured.  The test saves 750 and re-reads 750
through the port's own reader of the same word, which cannot falsify the
mapping.  Falsifying test: in Project set Project Information > Priority (or
ActiveProject.ProjectSummaryTask.Priority) to 750 on base.mpp and save; diff
the streams against project-priority-saved.mpp; then COM-open the port's file
and read ProjectSummaryTask.Priority and the dialog's value.

### 4. Medium - the new save-path tests verify through the write's own path and skip silently

- sha: e803933, 7b2268e
- files: app/app_edit_tests.cpp:494 (`if (percent_base.ok && ...)`) and :521
  (`if (percent_reread.ok)`), app/project_dialogs_tests.cpp:152-157.

Both new round trips re-read the single word they wrote with the port's own
reader; neither compares against a Project-written fixture (var-pct.mpp and
var-priority.mpp exist and would have shown finding 1 immediately).  The percent
block is also wrapped in `if (load ok)`: a load failure of the copy, or a saved
file the reader cannot open, skips every percent check with no failure.
Falsifying test: make load() of app-edit-percent.mpp fail (e.g. truncate it
after the save) and observe the test still passes; replace the guards with
check(...) and add a byte-level assertion that the saved row's +80/+88/+120
match what var-pct.mpp shows for the same percent.

### 5. Medium - Unlink removes only the selected task's saved predecessors

- sha: e803933
- files: app_qt/main_window.cpp:2111-2148, wave26-complete.md:111-112.

My understanding of Project's Task > Unlink Tasks is that with one task selected
it removes all links to AND from that task (predecessors and successors), and
with several selected it removes the links among them.  The port deletes only
predecessor links of one task, skips pending (unsaved) link adds while
reporting "removed N predecessor link(s)", and ignores a multi-row selection.
The gate (3 links -> 2) uses a task with predecessors only, so it cannot see
the difference.  Falsifying test (COM): select one task that has both a
predecessor and a successor, call Application.UnlinkTasks, and count
ActiveProject.TaskDependencies before/after; then do the same in the port on
the same file and compare the saved link rows.

### 6. Medium - set_percent on a pending inserted task still breaks every later save

- sha: e803933; listed by the wave's own review as #9 and not fixed
- files: app/edit_model.cpp:331-343, :926-929 (row_for_uid throws).

Confirmed in the code: push() checks only uid != 0 and no pending delete, so a
% edit on a row that exists only as a pending insert reaches step 5a, where
TaskTable::row_for_uid throws "no task row with UniqueID", the whole save fails,
and the edit stays queued so every subsequent Save fails too until the user
undoes it.  Kept at Medium (no silent loss, but a stuck document).  Falsifying
test: insert a task, press 25 %, Save - expect a failed save; Save again - still
failed.

### 7. Low - deleting a task permanently discards its pending % edit (undo does not restore it)

- sha: e803933
- file: app/edit_model.cpp:249-266.

delete_task's push now erases set_percent entries for the uid from applied_
(extending the existing set_field pattern).  Undoing the delete restores the
task but not the % edit the user made, with no message.  Falsifying test: 25 %,
delete, undo, Save -> the saved file has the old percent.

### 8. Low - Options > Schedule "New tasks created" is a per-project file setting in Project; the port neither applies nor saves it

- sha: 6cc5181 (W26-4.6)
- file: app_qt/main_window.cpp:3030-3053.

The wave's review already lists that insert_task() ignores it (#8).  In
addition, Project stores this option in the project file ("Scheduling options
for this project"); the port changes only shell state, does not mark the
document dirty and writes nothing, so the choice is lost on reopen.  Options
writes no file at all (only QSettings theme/recent), which answers the brief's
question: no .mpp write from Options.

### 9. Low - Timeline check box is view-only and not saved

- sha: 3660091 (W26-1.3)
- file: app_qt/main_window.cpp:2005-2012.

It starts from the file's stored setting but a toggle is never written and does
not dirty the document, while Project saves Timeline visibility with the view.
Not data loss of schedule values; a parity gap the record calls DONE (decision
5).  Falsifying test: toggle, Save, reopen -> the old state returns.

### 10. Low - Project Information layout gate is partly self-referential and loose

- sha: 7b2268e
- files: app_qt/project_dialogs_qt.cpp:86 (setFixedSize 616x410),
  tests/data/reference/w26-project-dialogs.measures.json ("width": 616,
  "height": 410, "aspect": 0.6643), app_qt/tests/project_dialog_gate_test.cpp:
  115-159.

The aspect check compares the port's fixed size with a JSON size that is the
port's own logical size; it cannot fail unless someone edits one side.  The
fraction checks are real capture numbers but at +/-0.03 they allow ~18 px
horizontally and ~12 px vertically (half a 22-px row).  Falsifying test: move
the Priority row down 10 px; the gate still passes.

### 11. Low - backstage "Close" closes the application, not the project

- sha: fb5d37c / 6fb6ec5
- file: app_qt/main_window.cpp:575-577.

File > Close in Project closes the active project and leaves the application
open.  The port calls close() on the main window.  The unsaved-edit guard does
run (closeEvent), so no data loss.

### 12. Low - wave-26 code still states the disproved premise

- files: app/edit_model.h:82-86, :326-328, edit_model.cpp:922-924, and
  core/mppwrite/table.h:190 / row_writer.h:9-10 (pre-existing).

The comments "PercentComplete has exactly one stored copy" should be corrected
with the var-pct.mpp evidence so a later wave does not build more on it.

### 13. Low - New Recent list and Project Summary toggle: no defect found, one residual

- sha: 3d78191
- The REV-2 fold (`base_ = sync_plan_from_records(base_, records_)` before the
  flag, app_qt/plan_document.cpp:609-624) is correct: sync uses the old flag to
  compute the row offset, matching records_ built from the old base_.  The
  New/Recent/Open/This PC/New-from-existing/Close entries all reach
  confirm_proceed_with_discard().  Residual: a cell editor still open when the
  toggle fires is destroyed by sheet_model_.set_sheet(nullptr) without a
  commit; not tested.

## Priority-2 answers in one line each

- Link/Unlink: Link reuses the Predecessors dialog (unchanged path); Unlink is
  finding 5.
- Project Summary Task toggle: fold is correct (13).
- Timeline check box: view-only, not saved (9).
- Task Form: all fields setReadOnly(true) (app_qt/task_form.cpp:22); it cannot
  edit anything, including uid 0.
- Backstage New/Open/Recent: guarded (13).
- Options General/new-task mode: writes no file (8).

## Privacy

No client or person name found in added lines, commit messages reviewed, the two
new binary fixtures (UTF-16 strings are only stock bar-style names and the fonts
Calibri / Segoe UI), or the four new measures JSON files (no file names; only
local capture paths under corpus/reference/).  The history residual the record
discloses (a task name in 18 wave-26 commit trees) is unchanged and remains
Tim's decision.

## Claims in wave26-complete.md not supported by the diff

- "PercentComplete ... single-copy table write" (lines 102-109, 692-696):
  contradicted by var-pct.mpp (finding 1).
- The reviewers' positive check "the percent/priority edits are validated,
  last-wins and re-read" (line 1104-1105): true but the re-read is through the
  port's own reader of the same word, so it says nothing about Project.
- Decision 5 "DONE" for the Timeline toggle rests on a non-saved toggle (9).
