# Wave 23 independent review -- git diff main..wave23 (tip d236f91)

Reviewer: independent read-only pass, 2026-10-23.  Nothing built, no git write
commands, no tracked file edited.  Evidence below is from git, grep, Read and
small olefile byte comparisons of files under tests/data and build/w23 (run
from a scratchpad script; nothing in the repo was changed).

## Verdict

NOT READY TO MERGE.  The wave does a lot of honest gate work (read-only golden,
COM-pinned predecessor hash, literal parity numbers, the Flag10 bit, which I
re-derived from the committed fixtures), but B-1 (3d3ef34) brought in a
synthetic row 0 without auditing every place that uses a RecordSet row index as
an index into `plan().tasks`.  The result is a data-writing regression: Custom
Fields (formerly Task > Information / Task Fields) queues its edits on the task
BELOW the one the user selected, and File > Save writes them there.  Second,
the premise behind the always-on project summary row is false.  The file DOES
store the setting: `214/Props` record 0x03400009 (int16) is 0 in
summary-off.mpp and 1 in summary-on.mpp; it is 1 in Project's saves of
project_a and 0 in every synthetic fixture.  So the port now shows a row that
Project would not show on every committed fixture, and five gates were changed
to expect it.  The Timeline reversal rests on the same flawed "no 214 part
changed" method, with no saved pair on disk.  CI also lost the corpus-free
plan_recalc checks, and the new workflow_run trigger cannot do what the docs
claim.  No screenshots were added, and nothing under corpus/ or decompiled/.
The client task names this wave added are gone at the tip but are still in the
history of 3d3ef34 and 4d789b5, including a commit message.

## Findings (most severe first)

1. **Critical -- 3d3ef34 (introduced), 9e60d06 (re-homed, ungated) --
   app_qt/main_window.cpp:2175-2183.**  `show_task_fields()` passes
   `doc_.plan().tasks[selected.row]` to the Task Fields dialog.  `selected.row`
   is a RecordSet row (app/selection_model.h:72).  `plan()` is `view_`
   (plan_document.h:149), which comes from `sync_plan_from_records` and has NO
   synthetic row.  With the project row at RecordSet row 0, RecordSet row N is
   plan task N-1, so the dialog opens on task N+1.  `make_task_field_edits(row_,
   ...)` (dialogs.cpp:495) queues the custom-field edits under that wrong task's
   uid, and Save writes them to the file.  Selecting the last task gives
   "select a task first".  Before 3d3ef34 this was the Task > Information
   path; after 9e60d06 it is Custom Fields (Edit menu, Project > Properties,
   Gantt Chart Format > Columns).  The same off-by-one hits two other places:
   the Delete Task confirmation (main_window.cpp:2585-2587) names the wrong
   task, although it deletes the right uid, and the context menu's Auto/Manual
   label (main_window.cpp:2788-2790) reads the wrong row.  The
   qt_dialog_gate added in 9e60d06 checks only the dialog title ("Task
   Fields"), never which task it shows.  Falsifying test: open
   synthetic-base.mpp, select model row 1 (first task), trigger
   MenuAction::custom_fields, intercept the dialog and read its row_.uid (or
   accept a Text1 edit, then inspect structural_ / save and reopen).
   Expected: the first task's uid.  Predicted: the second task's uid.

2. **High -- 3d3ef34 -- app_qt/plan_document.cpp:361-367,
   app/layout_model.h:179-187.**  `loaded_.show_project_summary = true` is
   hard-coded on the grounds that the setting is "an APPLICATION setting... the
   two saves differ in no 214 part" / "byte-identical 214 parts".  That is
   false.  build/w23/summary-off.mpp and summary-on.mpp differ in
   214/CEdl, CFilter, CGrouping, CTable, CV_iew and 214/Props.  Most of those
   are timestamps, but 214/Props carries a clean int16 record, id 0x03400009,
   that goes 0 -> 1 (at offset 9300).  It is 0 in tests/data/synthetic-base.mpp,
   wave23-outline.mpp, wave23-flags.mpp and every Project-saved control in
   build/w23.  It is 1 in build/w23/cv-project_a.mpp and preds-project_a.mpp
   (Project saves of project_a, whose capture shows the row).  The fixer's own
   build/w23/oracle-w23.json also reads DisplayProjectSummaryTask = false for
   synthetic-base and for a new project, even after it had been set true in the
   same session.  An application setting would have carried over.  The brief
   (wave23-brief.md:59-61) asks for "project summary row 0 WHEN THE FILE SHOWS
   IT".  Consequence: on every committed fixture the port shows a row Project
   does not, and the gates were moved to match.  shell_test.cpp,
   chrome_gate_test.cpp:237, save_reopen_test.cpp:130/221,
   stored_table_test.cpp:401 and task_order_test.cpp check_project_row on both
   committed fixtures now expect 4 rows, or 7, and a project row.  Falsifying
   test: decode 214/Props 0x03400009 in the reader, set show_project_summary
   from it, and add a reader assertion (summary-off = 0, summary-on = 1,
   synthetic-base = 0).  The qt gates then fail on the fixtures as written.

3. **High -- 5150d37/dfebc66 -- app_qt/main_window.cpp:1402-1407,
   shell_test.cpp:84-88, parity_test.cpp:246-251.**  The Timeline strip is now
   forced visible for every loaded file, which contradicts the brief's
   acceptance ("port on project_a shows no strip").  The evidence offered
   (wave23-complete.md section 5: "saving the same file with the strip shown
   and hidden changes no 214 part and no Props record") uses the same method
   that missed record 0x03400009 in finding 2.  No shown/hidden save pair
   exists under build/w23; only summary-off/on, label, lag, ts and flag pairs
   do.  oracle_w23.py's probe_timeline read nothing (every member
   "<unreadable>").  The tests were flipped from hidden to visible on that
   basis.  The fixer did disclose this and refer it for a ruling, which is to
   their credit, but the "measured" label is not earned.  Falsifying test:
   save one file with the strip shown and hidden through COM, then byte-diff
   all 214/Props int16 records (not just the decoded viewstate parts) the way
   finding 2 was located.

4. **High -- 8802d10 -- .github/workflows/ci.yml:104-115.**  CI now runs
   `ctest -E plan_recalc`.  On main, CI ran plan_recalc with no exclusions, and
   without a corpus it still runs test_base (committed synthetic-base),
   test_manual_scheduling and test_free_vs_total_slack
   (tests/plan_recalc_tests.cpp:1-16, 145, 351, 946).  Those are the 266
   corpus-free checks the local gate counts.  The stated reason ("it reads the
   client corpus when present, and its golden is refreshed only deliberately")
   does not apply on a corpus-less runner.  The same wave already made the
   golden read-only (PLAN_RECALC_UPDATE_GOLDEN), so the brief's "-E plan_recalc
   OR the opt-in" was met twice, and a real gate was lost.  No other CI step
   runs plan_recalc with MPP_NO_CORPUS=1.  Falsifying test: grep the CI logs
   for "plan_recalc"; it is absent.  Fix: add a step that runs
   `MPP_NO_CORPUS=1 ctest -R '^plan_recalc$'`.

5. **Medium -- 9e60d06 / f40111f -- app_qt/main_window.cpp:298-320,
   2855-2918.**  The review fix blocks edits to the synthetic row only in
   SheetTableModel::flags/setData.  A double-click on the project row still
   opens Task Information.  apply_task_information() then writes through
   SheetModel::begin_edit/commit_edit directly, so the edit lands on
   RecordSet row 0, gets an undo entry and marks the document dirty, and
   sync_plan_from_records later drops it silently.  This is the same defect
   the review fix claimed to close.  Falsifying test: in shell_test,
   select_cell(0, name), call task_information_values / apply_task_information
   with a changed Name, rebuild_views, and check that the sheet text reverts
   or that no edit was accepted.

6. **Medium -- 3d3ef34 -- app_qt/print_page.cpp:138-148,
   main_window.cpp:2058-2071, print_page.cpp:353-356.**  File > Print builds
   print rows from `records().row_count()` (which includes row 0), but
   build_print_gantt indexes `plan.tasks[row]` from `doc_.plan()` (no project
   row).  The printed Gantt is shifted one row against the printed sheet: the
   first task's bar sits beside the project row and the last bar is dropped.
   The subtitle says rows.size() (N+1) "of" plan.tasks.size() (N).
   build_gantt() got a display plan with the inserted row, but print did not.
   Falsifying test: call make_print_job + build_print_gantt on synthetic-base
   and compare bar count and bar uids with the sheet rows on page 1.

7. **Medium -- 3d3ef34 -- app/reader_adapter.h:145-155 vs
   app/edit_model.cpp:476-483 and core/mppwrite/add_delete.cpp:718-731.**
   After the ID sort, the display placeholder for a pending Insert Task copies
   duration, start and finish from the last task in `loaded.tasks` (now the
   highest ID).  The writer still clones the last live FixedData row.  On any
   file stored out of ID order (project_a: the first stored row is ID 13), the
   pending row shows different dates and duration from what Save writes.  The
   edit_model.cpp comment ("the last live task in the FILE") is now wrong.
   Falsifying test: load project_a, insert a task, record the placeholder's
   start and duration, save, reopen, and compare with the new task.

8. **Medium -- 3d3ef34 -- app_qt/tests/task_order_test.cpp:13-14.**  "The
   committed fixtures prove the rule in CI" does not hold for the sort.  Both
   committed fixtures (wave23-outline: tasks added 1..6 in order;
   synthetic-base: 3 tasks) are already stored in ID order.  Deleting the
   std::stable_sort leaves qt_task_order green in CI.  Only the local-only
   qt_task_order_corpus exercises it.  Falsifying test: remove the sort, run
   ctest -R qt_task_order$; it still passes.  A fixture whose FixedData order
   differs from ID order (built through COM by moving a task) would close the
   gap.

9. **Medium -- 2087739 -- .github/workflows/ci.yml:36-47, 69-76.**  The
   workflow_run trigger (a) fires only from the workflow file on the DEFAULT
   branch, so it cannot fire for wave23 before the merge.  The section 6 ci
   runs were manual dispatches, so "one dispatch produces both signals" is
   untested.  (b) It checks out
   `workflow_run.head_branch`, not `workflow_run.head_sha`, so it can test a
   newer commit than the one qt-shell tested.  (c) It has no conclusion
   filter, so it also runs after a failed or cancelled qt-shell.  (d) On a
   v* tag, ci runs twice: once on `push: tags` and again after qt-shell, which
   cuts against the file's own COST CONTROL note.  (e) `actions: read` is
   granted but unused.  Nothing makes CI pass vacuously: no continue-on-error
   and no `|| true` were added, and the parity step uses `set -euo pipefail`.
   Falsifying test: dispatch qt-shell on wave23 before the merge; no ci run
   appears.  After the merge, push to the branch while qt-shell is running and
   compare the two runs' shas.

10. **Medium -- 1929668 -- scripts/engine_diff.py:1075-1082,
    CMakeLists.txt:225-240.**  `--suite --expect 7/8` compares only the COUNT
    of passing scenarios.  If constraint-mso started passing and another
    scenario regressed, the lane would stay green.  So the CMake comment "A
    change to that state ... turns this test red" is overstated.  Falsifying
    test: tamper with one passing oracle and make constraint-mso pass (or
    mark it expected); the suite still exits 0.  Fix: `--expect-fail
    constraint-mso` with the set compared exactly.

11. **Low -- 4d789b5 -- app/layout_model.cpp:270-291,
    stored_table_test.cpp:963-966.**  The project row's SUMMARY=Yes (Flag10),
    critical=false and milestone=false are hard-coded from one capture.  The
    reader never reads row 0's own FixedMeta Flag10 bit, so a file whose
    project row is No would still show Yes.  The corpus check's
    `if (uid == 0) stored = true` mirrors that hard-code (a tautology for that
    row; the 13 captured strings are the only independent part).  Falsifying
    test: in Project, set the project summary task's Flag10 = No on a fixture
    copy and save; the port still prints Yes.

12. **Low -- d01dbb4 -- app_qt/sheet_table_model.cpp:249-253.**  "Project's
    stock milestone text style is italic" comes from project_a's capture.
    That is the FILE's text styles, the same trap Q1 ruled out for bar
    colours.  No stock-file evidence is offered (wave23-outline has no
    milestone).  Falsifying test: add a milestone to a new project through
    COM, capture it or read Format > Text Styles > Milestone Tasks, and check
    italic.

13. **Low -- f2910ff -- tests/mpp_reader_tests.cpp:1143-1152, 1158-1185.**
    `context.equal(0x02401388u + 64u, 0x024013c8u, ...)` and its two siblings
    compare constants with constants.  The "+ index" rule is asserted, not
    tested.  The props check compares the 0x024013c8 record with the
    reader's own decode of the same record.  The name_at() checks compare
    the TSV with literals typed from the same TSV.  The test proves only that
    the TSV is present and unchanged.  Falsifying test: change the reader's
    record id constant to 0x024013c9; the arithmetic checks stay green.

14. **Low (privacy) -- 3d3ef34, 4d789b5; fixture b3a257b.**  Client task-like
    names added in this wave, first word only: "CONSTRUCTION" in the 4d789b5
    COMMIT MESSAGE and in that commit's stored_table_test.cpp and reader.h
    comments, and "Stage" in a 3d3ef34 task_order_test.cpp comment.  d236f91
    removed them from the tree, but they stay in history, as the fixer
    disclosed.  d236f91 also removed pre-existing pinned-percent names (an
    improvement).  The new fixture tests/data/wave23-flags.mpp embeds an
    absolute local path in UTF-16 at about byte 2700.  It includes the company
    Dropbox folder name and the operator's name, ending
    ...\build\w23\flag10-task3.mpp.  Both new fixtures carry Author and
    LastSavedBy = the operator's name.  No images were added, nothing under
    corpus/ or decompiled/, and the tree diff has no person names beyond
    synthetic ones ("Resource A", "Leaf A1a" and similar).
    analysis/project_property_names.tsv is derived from the decompiled binary
    but is the derived table the brief's Q9 authorises.  Falsifying test:
    `git log -p main..wave23 | grep -c CONSTRUCTION`; strings on
    wave23-flags.mpp.

15. **Low -- e4b2bd9 / f40111f -- app_qt/tests/ribbon_inventory_test.cpp:
    201-260, app_qt/ribbon_pin.tsv:7-8.**  The old gate asserted the
    MenuAction of EVERY button, including `none` on the disabled ones.  The
    new kEnabledWiring covers only the 13 enabled buttons, so a wrong action
    on a disabled button (for example a stray insert_resource) goes
    unnoticed until it is enabled.  The pin's `enabled` column is the port's
    implemented set, not something read from Project, which is fine as a
    change detector.  Brief Q7 asked for one JSON read by BOTH the builder
    and the gate.  The TSV read by the gate alone is arguably better, but the
    deviation is undisclosed.

16. **Low -- 3d3ef34 -- app_qt/sheet_table_model.cpp:143-160.**  The gutter
    now prints the stored ID.  Pending inserted rows have id 0
    (placeholder_row), so they show "0", the same as the project row, where
    they used to show an ordinal.  Insert Task with the project row selected
    uses anchor_uid 0, which means "append at end"; Project would insert at
    the top.  Falsifying test: insert a task in shell_test and read
    row_number_text for the new row.

17. **Low -- 9e60d06 / 63cd087 -- app/menu_model.cpp:68-72, app/main.mm:
    2879-2887.**  On macOS, MenuAction::task_fields is now labelled "Task
    Information..." but still opens the Task Fields dialog, and "Custom
    Fields..." opens the same dialog.  Two menu items run one command, and
    one of them is mislabelled.

18. **Low -- docs/handoff/wave23-complete.md (claims the diff does not
    support).**  (a) Section 3 says the "project row 0" matches.  That is
    true for project_a only; see finding 2 for the fixtures.  (b) Section 5:
    DisplayProjectSummaryTask and the Timeline "measured" as application
    state; falsified and unsupported respectively (findings 2 and 3).  (c)
    W23-12 "one dispatch covers the core lane": not demonstrated (finding
    9).  (d) "reader tests 1025 (1016 -> 1025)": the diff adds about 14 new
    passing check/equal calls (9 in test_project_property_names, 5 in
    test_flag10), which does not reconcile with +9.  Not run here, so
    unverified.  (e) W23-1 "FIXED": the brief's conditional row-0 rule is not
    implemented.  (f) stored_table_test.cpp:942-946 says SUMMARY is "Yes on
    the project row and every summary task", then in the next comment says
    summary rows 1/2/6 read No.  The second statement is the measured one.

Checked and found sound (no finding): the Flag10 bit (byte +25 bit 0x04).
Byte-diffing wave23-outline vs wave23-flags and flag-control vs flag10-task3
changes exactly one FixedMeta byte, 00 -> 04, at offset 323.  Also sound: the
plan_recalc golden read/write logic (8802d10); the lag pins "50 d", backed by
the COM hash golden and not by the port's output; the parity_test move from
symbols to literals; the save_reopen/stored_table row-index shifts, which keep
the same tasks under edit; collect_start_edits and without_structural_edits,
which match by uid, so the sort and row 0 do not misroute Start or structural
saves; check_fixtures covering all 24 tracked tests/data files except the two
READMEs; and no continue-on-error or `|| true` in any workflow.
