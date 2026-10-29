# Wave 25 independent review -- git diff wave24..wave25 (tip 3b4da5f)

Reviewer: independent read-only pass, 2026-10-24.  Nothing was built.  No git
write command was run, no tracked file was edited, and nothing under
build/audit-w22 was read or run.  The evidence comes from git show/diff/grep, a
reading of the code, and small Python checks (olefile, PIL) of the committed
fixtures and the local captures.  The scripts are in build/verify-w25/scratch/:
strings.py, namescan.py, flagbits.py, streamdiff.py, oracle_body.py,
jsondiff.py, readme_hashes.py and colours.py.  The build/w25 gate logs were
read for their contents and timestamps.  No client string is quoted anywhere
in this file.

## Verdict

NOT CLEAN: one High finding remains, so under the operator's rule it interrupts
the parity wave.

Most of the S-fixes are real:
- **S1 (screen paths).** Fixed for the Qt and macOS screen Gantt and the shared
  edit-entry guard.  make_gantt_source_for_displayed_rows resolves rows by uid,
  and app_layout's wave24-order test fails under the old row arithmetic.
- **S2.** Real.  I re-counted the pixels in the local captures
  (build/w25/styles-off.png and styles-on.png).  They show auto (138,187,237),
  manual (116,204,212), auto summary (84,84,84), manual summary (43,163,173) and
  links (38,124,211)/(27,173,186).  With Critical Tasks ON they show
  (247,138,136) and (221,37,34) on the critical auto row only.
- **S6 and S7.** Real.  The bodies of all eight oracles are byte-identical in
  canonical form at e91f19b, wave24 and wave25; the large diff is only the
  CRLF-to-LF change.
- **W25-9.** Real.  Each wave25-flagNN differs from wave23-outline in exactly
  one bit, at FixedMeta offset 331 (0x02..0x80).
- **New fixtures.** All are clean: neutral metadata, C:\w24 or C:\w25 paths,
  and only synthetic names.
- **Name scan.** My own scan of the corpus names against every added line and
  commit message found only generic English words.

The High finding: the macOS PRINT path still counts the RecordSet rows from the
task list.  On any file that shows the project row (PROJECT_A does), Print and
Print-to-PDF on macOS drop the last task from both the table and the chart.
W25-1 is recorded as FIXED for "the shared and macOS paths".  The defect came
in with 39d11b4 (wave 24), and neither wave-25 review pass caught it.  The
remaining findings are Medium or Low: one undisclosed invertible name-hash
pin, the smoke half of the W25-1 acceptance not done, deferral reasons that
are labels, and a final-gate record that again overstates which gates ran on
which tree.

Counts: Critical 0, High 1, Medium 6, Low 10.

## Findings (most severe first)

1. **High -- 39d11b4 (wave 24, inside W25-1's scope; not fixed by b73fd26) --
   app/main.mm:1845, 1866-1873 (make_print_request); consumed at
   main.mm:2290-2302 (PrintView chart) and 2400-2403 (print table).**
   - **The defect.** `total = plan.view.tasks.size()` is the TASK count; the
     plan carries no project row.  When no filter is active, the request's
     rows are built as `for row < total: rows.push_back(row)`, and those
     indexes are then read as RecordSet rows (`plan->records.uid(row)`,
     `cell_text(*plan->sheet, row, ...)`).
   - **The effect.** When the file stores 0x03400009 = 1, RecordSet row 0 is
     the project row, so rows 0..N-1 print the project row plus tasks 1..N-1.
     The highest-ID task is missing from the printed table and chart.  The
     sheet print (PrintKind::sheet) has the same loss, because it uses the
     same request.  The subtitle counts uid != 0 over all records and still
     says "N of N".
   - **Why nothing caught it.** Before 39d11b4 the total was
     `records.row_count()`, which was correct.  The wave-24 change was meant
     only for the subtitle, but the row loop reuses `total`.  The AppKit smoke
     prints synthetic-base, which hides the row, so it stays green.
   - **Status.** wave25-complete.md section 1 marks W25-1 FIXED, and START-HERE
     says "every displayed Gantt row now resolves through its uid".  The Qt
     print path is correct (print_rows uses records().row_count()).  Not built
     here (no macOS toolchain).
   - **Fix.** Build the unfiltered list from `plan.records.row_count()` and
     keep the task count for the subtitle only.
   - **Falsifying test.** On a Mac, open tests/data/wave24-order.mpp and use
     File > Print > Save as PDF.  Predicted: 5 printed rows (ID 0 and IDs
     1..4), where 6 are expected, and no ID 5 bar.  Better: move
     make_print_request into app/ and assert in app_layout that
     `request.rows.size() == records.row_count()` on that fixture.

2. **Medium (privacy, undisclosed residual) -- pre-existing (3d3ef34/d236f91,
   wave 23), missed by W25-3 -- app_qt/tests/task_order_test.cpp:16, 60-67,
   322, 336-341, 354.**
   - **What is pinned.** qt_task_order_corpus pins thirteen 64-bit FNV-1a
     hashes of PROJECT_A display names.  The pinned rows are the project row
     (the project title) and IDs 1..12.
   - **Why it matters.** FNV-1a is unsalted and much cheaper than SHA-256, so
     a phrase list inverts it at least as easily as the client-string labels
     that W25-3 removed.  The project-title hash is the most guessable.
   - **What the record says.** The W25-3 commit title says "no invertible hash
     pins".  wave25-complete section 5 lists only the golden TSV as a residual,
     and START-HERE says every person-name hash pin is gone.  This pin is
     listed nowhere.
   - **Falsifying test.** `git grep -n "fnv1a64(name)" wave25` shows the
     comparison, and lines 336-341 hold the constants.  A dictionary of
     construction phrases plus the project title will match several.  I did
     not run the inversion.

3. **Medium -- b73fd26/a4ac887 (W25-1 acceptance) -- app/smoke.cpp:359, 374,
   380-381, 619; app/main.mm:4323-4336; wave25-complete.md section 11 item 10.**
   - **What the brief asked.** A headless test that "fails under row
     arithmetic for the Gantt source AND the smoke paths".
   - **What was done.** The Gantt source half is done.  The smoke paths still
     run only on synthetic-base, which hides the row.  They still assume that
     RecordSet row 0 is the first task: `records.uid(0)` equals
     `tasks.front().uid`, `cell_text(0, Start)`, `begin_edit(0, ...)`, and
     records == tasks.  The AppKit step still asserts bars == tasks.
   - **Where it is recorded.** Running smoke_plan on a row-shown fixture
     appears only as "listed for a W25-10 follow-up" in the section 11 review
     table.  It is not in the section 3 deferral list, and W25-10 is closed
     FIXED.
   - **Why it matters.** This is the gap that lets finding 1 through.
   - **Falsifying test.** Run `app_smoke --fixture tests/data/wave24-order.mpp`.
     Predicted: the load step fails on "the task RecordSet has one row per
     task" and on the row-0 checks, which shows that the path was never
     exercised with row 0 shown.

4. **Medium (claims) -- wave25-complete.md section 3 (W25-12..W25-21) and
   section 2.3 (R16/R17).**
   - **The stated reason.** "This wave spent its budget ... none of the parity
     streams was started."  The record also says the brief "says 'as capacity
     allows'".  It does not: section 2 is headed "Authorised now - in this
     order", and "as capacity allows" covers only W25-21's W24-25 items.
   - **Item by item:**
     - W25-12, W25-13 and W25-14 give "not decoded" as the reason.  That is the
       task itself, not an obstacle.
     - W25-15 describes the scope of the work, not an obstacle.
     - W25-16 to W25-20 give no reason at all.
     - R16a and R16b are preferences ("changes the header contract in the same
       wave").
     - Only R17 names a real obstacle with evidence: main.mm cannot be built
       or run on this machine.
   - **Severity.** These are labels, not blockers.  That matters because the
     next wave inherits them as its work order, but it is not a correctness
     defect.
   - **Falsifying test.** Compare the brief's section 2 header and its
     W25-12..W25-21 text with complete section 3, line 96.

5. **Medium (S5 recurrence: gate record) -- wave25-complete.md section 9;
   docs/START-HERE.md "Gates at the wave-25 close (fresh trees, final commit)";
   build/w25/final-gates-core.log; build/w25/gates-0d6c241-W25-REV2.log.**
   - **The claim.** Section 9 says the table is "the post-review re-run on the
     final code commit 336ec0c" on fresh trees.
   - **What ran on 336ec0c.** On tree 6279096 (= 336ec0c) the log holds core
     ctest 51/51, reader 1100/420, app 15/15, qt 18/18, fixtures and the two
     parity gates.
   - **What did not.** plan_recalc 512/266, M2 332/332, the explicit engine
     suite (7 pass / 1 fail) and "app_layout 273 checks" are not in that
     section.  They come from b6df460 or earlier: 273 appears only in
     gates-33217e2-W25-8.log and gates-b04b22e-W25-8-review.log.
   - **"Fresh" is not quite true.** The w25-fresh* directories were configured
     at 02:36-02:48.  The 03:35 runs are incremental rebuilds in them, not a
     fresh configure.
   - **Logs edited after commit.** The per-commit logs were appended after the
     fact (mtimes of 02:49-03:20 for commits made at 01:42-02:34).  Their
     timestamps can therefore no longer show that a run came before its
     commit.
   - **Severity.** 336ec0c touches no core scheduling file, so the numbers are
     probably still right.  The defect is the claim, which is the S5 class
     the brief tightened.
   - **Falsifying test.** `awk 'NR>=1066' build/w25/final-gates-core.log |
     grep -c "plan_recalc\|332 / 332"` returns 0.

6. **Medium (privacy, risk statement only) -- tests/data/project_a_golden.tsv
   (wave-24 carrier; disclosed in complete section 5).**
   - **What it holds.** 83 rows, each with
     `sha256:<16 hex>` = the first 64 bits of an unsalted SHA-256 of the task
     name.
   - **The risk.** The same dictionary inversion applies as for the removed
     labels.  Construction task names come from a small vocabulary, so most
     can be recovered offline, and 64 bits leaves no collision ambiguity.
     Because the plaintext is already in public history, the hashes mainly add
     linkage: they confirm a guessed name belongs to PROJECT_A at a known row
     and uid.
   - **What would fix it.** A keyed HMAC whose key stays in corpus/, or
     comparing against a local-only export.  Either keeps the gate
     discriminating.  The decision belongs with the operator and the publication hold.

7. **Medium -- 7a64baf/a35a01f (W25-3) -- tests/mpp_reader_tests.cpp:
   2607-2625 (resources 1 and 3), 869-880 (uid 10733), 772-781 (orphan name).**
   - **What changed.** These value pins were replaced by "non-empty and equal
     to trim(name_raw)".
   - **What is lost.** For resources, nothing else checks PROJECT_A resource
     names by value.  A reader that decodes a different resource's block, or
     the wrong VarMeta key, now passes, as long as the string is non-empty and
     has no surrounding spaces.  Task names stay covered by the golden TSV
     (finding 6); resources have no such oracle.
   - **What the brief allowed.** A keyed hash with an uncommitted key or a
     local-only map, either of which keeps the value check.
   - **Falsifying test.** Mutate find_resource(project_a, 1) to return
     resource uid 2.  Predicted: test_resources stays green.

8. **Low -- 9ec15a8/5f3a12d (W25-2) -- scripts/parity_report.py:89, 158-160;
   tests/data/reference/wave25-auto.measures.json.**
   - **The margin.** The auto-vs-manual gate separates the two colours by one
     level: the max channel delta is 25 against a tolerance of 24.  A future
     tolerance change of 1, or a port colour anywhere between the two, makes
     the gate pass either way.
   - **Unreferenced constants.** The highlight-ON, summary and link constants
     have no committed reference measurement.  parity_test and ux_gate pin
     them against literals and against the shell's own constants, which
     cannot detect a mismeasurement.  The only evidence is the local captures,
     which I checked and which do support the values.
   - **Falsifying test.** Run parity_report on a render with kBarTask =
     (127,195,224).  Predicted: PASS.

9. **Low (privacy hygiene) -- core/mpp/reader.cpp:1836; build/w25/
   final-gates-core.log:175, 435 and the post-review section.**
   - **The leak.** The reader's dead-row warning concatenates the orphan
     block's text (uid 10856) into the warning string.  So every gate log
     holds that client name in plaintext, and so does any shell that lists
     reader warnings.
   - **Scope.** The logs are git-ignored and nothing was added to the repo.
     But W25-REV2 removed only the test literal and called it "the one real
     name literal left", while the product still prints the name.
   - **Falsifying test.** `grep -c "holds the name '" build/w25/final-gates-core.log`
     returns a non-zero count.

10. **Low -- cbf80ef (W25-5) -- app_qt/tests/stored_table_test.cpp:88-99,
    815-826.**
    - The two expected failures sit inside `if (name_display >= 0 &&
      model->rowCount() > 3)`, and their count is never asserted.  If that
      guard is false, both vanish and the test stays green.
    - The "by name" listing is a printf, not a harness-level expect list like
      `--expect-fail`.
    - **Falsifying test.** Force rowCount() to 3.  Predicted: 0 failures and 0
      expected failures, with exit 0.

11. **Low -- b6df460/4c28039 (S14 class) -- app_qt/main_window.cpp:1352;
    app/main.mm:466-467, 4677-4678.**  The status-line filter text still uses
    `records.row_count()` as its total, and on macOS also
    `filter_rows.size()`.  So both shells say "k of N+1" on a row-shown file,
    while the PDF subtitle (fixed) and Print say "of N".  Falsifying test:
    apply "ID >= 2" to wave24-order.mpp in the Qt shell and read the status
    text.

12. **Low -- 336ec0c -- app_qt/main_window.cpp:2450-2455, 2513-2520;
    app/main.mm:1722-1726; core/sheet/sheet.cpp:1443.**
    - The Cut and Paste uid-0 guards have no test in any ctest.
    - On macOS, a paste with no selection now reports "the project summary row
      is read-only", because row_uid(kNoIndex) returns 0.
    - None of the wave-25 main.mm edits has been compiled anywhere: there is no
      macOS toolchain, and CI was not dispatched under the hold.
    - **Falsifying test.** A Qt test that selects row 0 of wave24-order and
      calls cut_selection().  Today nothing asserts that the RecordSet is
      unchanged.

13. **Low -- 97433ce/65a270f (W25-10/S12) -- tests/mpp_reader_tests.cpp:
    1296-1414; 65a270f commit message.**
    - **What the files show.** The committed pair differs in 15 streams,
      including 1,954 bytes of 214/Props and 4 bytes of 214/CV_iew/Var2Data
      (streamdiff.py).  The commit message calls it "byte-identical ... except
      the two saves' own churn".
    - **How the claim moved.** The test now claims decoded-plan identity.
      That is a fair claim and it is well tested, but it is not S12's "only
      that record differs".
    - **Still open.** The 4 CV_iew bytes (S12's falsifying step) are still not
      decoded.  Any non-empty 0x03400008 still decodes as "Timeline shown".
    - **Falsifying test.** Decode those 4 bytes in the pair.

14. **Low (process) -- 336ec0c, 5f3a12d.**
    - 336ec0c fixes eight findings in one commit, against the brief's "one
      finding id per commit, no exceptions" (S8).  The overlap reason is
      stated.
    - 5f3a12d, a "fixtures only" commit, also changes check_fixtures.py
      behaviour (it skips the HEAD comparison for staged first-commit files).
    - That skip means a NEW, staged, undocumented .mpp passes with no check.
      A modified copy of an existing fixture does NOT pass: it is in HEAD and
      fails "differs from HEAD", and it is also in DOCUMENTED.  The only
      tracked .mpp outside DOCUMENTED is analysis/test_project.mpp.

15. **Low (S4 remnants) -- scripts/phase2d_propinfo_layout.py:129/142;
    docs/handoff/wave23-verification/review.md:216-223; spec/mpp-records.md:
    377-387; scripts/probe_names.py:15-21; docs/handoff/s6.md:96-104.**
    - The banner word was deleted, not restored.
    - The old review's new falsifying command runs a script that is not in the
      repo (build/audit-w22), so it cannot be reproduced from a clone.
    - The specs still state the exact UTF-16 lengths of three client names
      (19, 34 and 12 units) and that one ends in a space.  That is small, but
      it narrows a guess.
    - The opaque labels collide: client-name-01..03 in the spec and
      client-name-01..09 in s6.md name different rows.

16. **Low -- c21dd0e -- scripts/measure_flags.py.**  The committed generator
    writes C:/w25 paths and names its aggregate output "flags20.mpp".  The
    wave-24 fixtures it is credited with (wave24-flags20/-flag2/-flag20, with
    C:\w24 paths) cannot be byte-reproduced by this version.  Only the seven
    wave-25 controls are.

17. **Low -- 4c28039 -- app/export_pdf.cpp:110-119.**  An active zero-match
    filter now sends an empty RecordSet to render_sheet_report and
    render_gantt_report.  export_tests checks only export_rows, not a
    zero-row export_report call.  The renderer appears to handle it
    (report.cpp:389, `plans.empty()`), but no test renders a zero-row PDF.

## Checked and found sound

- **make_gantt_source_for_displayed_rows** resolves each row through its uid,
  and project_summary_row stands in for uid 0.  Qt build_gantt and macOS
  rebuild_view both call it with the same RecordSet the sheet uses.
  sync_plan_from_records keeps its offset.
- **begin_cell_edit** is the single edit entry on both shells.  macOS delete,
  links and custom fields refuse uid 0 through EditList::push or
  plan_task_row.
- **check_fixtures.** The provenance check's body comparison, its fail-closed
  handling of an empty history and its use of --follow are correct.  The
  tests/data/README hash table is correct (40 of 40 rows match their committed
  blobs).
- **writer-name.json.** Only Name, run_at and OutlineLevel leaves changed.
- **The W25-7 classifier** reads the live QToolButton/QComboBox/QCheckBox
  class.  A replaced control drops out of page_buttons_ and fails the label
  comparison.
- **The W25-8 label pin** compares strings, not pointers.
- **A missing flag fixture** now fails the test instead of skipping it.
- **S13 rename.** The counter split is accurate.
- **wave24-complete errata (section 2).** These match the build/w24 evidence
  as the wave-24 review described it.
