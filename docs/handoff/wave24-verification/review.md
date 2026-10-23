# Wave 24 independent review -- git diff wave23..wave24 (tip 2561a51)

Reviewer: independent read-only pass, 2026-10-24.  Nothing built, no git write
command, no tracked file edited, nothing under build/audit-w22 read.  Evidence
is from git show/diff/grep and small Python byte checks (olefile) of the
committed fixtures, run from scripts in build/verify-w24/ (props.py,
strings.py, metadiff.py, streamdiff.py, histadd.py).  The local gate logs in
build/w24/ were read only for their timestamps and summary lines.

## Verdict

NOT READY TO MERGE, even after the publication hold is lifted.  The Qt half of
the R1 fix is real.  qt_uid_routing selects real rows on a fixture that shows
the project row and is stored out of ID order.  Most of its assertions fail
under the old row-as-index code, and the save-and-reread and Insert checks are
discriminating.  R2 is also well evidenced: I re-decoded 214/Props 0x03400009
in every committed fixture, and the off/on pair differs in exactly that record
plus the per-save GUID records.

But W24-2 moved the "show the project row" decision out of the Qt shell and
into the shared reader adapter, and so into app/ and the macOS shell.  Those
two never handled row 0, and the macOS screen Gantt now has the same
off-by-one that R6 fixed for Qt print.

Second, the new "measured stock" bar palette mislabels Project's colours.  The
teal "task" colour was sampled from a fixture whose tasks are all manually
scheduled.  The light-blue "critical" colour was sampled from a blank project
whose tasks were all set to auto (and all critical).  So the palette is most
likely Manual Task vs Task, not Task vs Critical.  Three gates were re-pinned to
the new constants.

Third, the privacy scrub replaces names with unsalted 8-hex SHA-256 prefixes
that a dictionary inverts at once.  It also left UTF-16 hex dumps of the same
strings next to the hashes, and it edited recorded oracles and specs.

Finally, the per-commit gate numbers are not trustworthy: ca9e7d7 claims
app_qt 18/18 on a tree that has 17 Qt tests and a corpus test that cannot pass
against the golden format committed in that same commit.

## Findings (most severe first)

1. **High -- ff22c7b (W24-2) -- app/reader_adapter.h:144, app/main.mm:625/638/654,
   app/smoke.cpp:380/395-401.**  `plan_input_from_document` now sets
   `show_project_summary` from the file for EVERY front end.  In wave 23 only
   app_qt/plan_document.cpp forced it, so app/ and macOS never saw a RecordSet
   row 0.  On macOS, `plan.records = make_record_set(plan.base)` now carries
   the project row whenever the file stores 0x03400009 = 1 (PROJECT_A,
   wave24-order, wave24-summary-on).  But `plan.view =
   sync_plan_from_records(...)` has no such row, and the screen Gantt is
   `make_gantt_source_for_rows(plan.view, plan.row_map.rows, ...)`.  That
   indexes `input.tasks[row]` with RecordSet rows.  Result: bar N is drawn
   beside sheet row N+1's task, the project row gets the first task's bar,
   and the last bar is dropped.  This is exactly R6, on screen, on macOS.  The
   wave fixed the macOS PRINT path for uid 0 (main.mm:2276-2292), but not the
   screen path that sits next to it.  app/smoke.cpp has the same pattern (it
   reads `cell_text(0, start)` as the first task and builds bars from
   `plan.loaded` by RecordSet row).  It only stays green because it runs on
   synthetic-base, which stores the row OFF.  The macOS sheet also has no uid-0
   edit guard (the Qt guard lives in SheetTableModel), so R5 (an edit accepted
   and then silently dropped) is back on macOS.  So complete.md's claim that
   "no call site indexes plan().tasks with a RecordSet row" does not hold for
   app/.  Falsifying test: run the macOS app smoke, or a unit test of
   make_gantt_source_for_rows, on tests/data/wave24-order.mpp.  Assert that
   bars.size() == row_map.size() and that bar[i].task_uid ==
   sheet.row_uid(row_map.rows[i]).  Predicted: the sizes differ by one and
   every uid is shifted.  Not built here (no macOS toolchain).

2. **High -- ff22c7b (W24-13/W24-18) -- app_qt/shell_style.h:123-142,
   app_qt/gantt_canvas.cpp:62-67, parity_test.cpp:225-228,
   ux_gate_test.cpp:168-175, shell_test.cpp:106-110.**  The new constants are
   task (116,204,212) teal, critical (138,187,237) light blue and summary
   (43,163,173) dark teal.  The teal task and summary values were sampled from
   the wave23-outline capture (capture_w24_reference.py; build/w24/
   sample_colours.py).  That fixture was built with FileNew + Tasks.Add and
   no `Manual = False` (scripts/oracle_w23.py:84-138), and it stores
   NewTasksAreManual = 1: 114/Props 0x024013c8 = 1, which I decoded in every
   committed fixture.  So those bars are MANUALLY SCHEDULED task bars.  The
   "critical" value comes from build/w24/build_styles.py.  That script sets
   every task `Manual = False` and links them in one chain, so every bar is an
   auto task, and all of them are critical.  With Project's default Gantt
   (Critical Tasks highlight off), that colour is the ordinary auto-task fill,
   not a critical fill.  The port now paints every non-critical auto task in
   the manual-task colour, and every critical task (most of PROJECT_A) in the
   auto-task colour.  The links and the status-date line (gantt_canvas.cpp:398
   now uses kBarCritical) changed with them.  Three gates were moved to the new
   constants on this evidence.  The parity reference cannot catch the error,
   because its only fixture is all-manual.  Falsifying test: in Project, FileNew
   with one auto task that has slack (non-critical) and one manual task.
   Sample both fills, or read Format > Bar Styles "Task" and "Manual Task".
   Predicted: auto = (138,187,237), manual = (116,204,212), and no distinct
   critical fill until Critical Tasks is ticked.

3. **Medium (privacy) -- ff22c7b (W24-26) -- docs/handoff/s6.md:96-104,
   spec/mpp-records.md:277/372-383, scripts/probe_names.py:15-19,
   tests/mpp_reader_tests.cpp:846-855/2445-2457, reader.h/reader.cpp
   comments.**  `client-string-<sha8>` is the first 8 hex digits of an unsalted
   SHA-256 of the plaintext.  I confirmed that:
   - sha256 of the generic phrase whose first word is "Site" gives 10dbe851;
   - the single-word 12-character task name gives 9c4bcb75;
   - the phrase whose first word is "Receipt" gives 25d8f08b.

   Each was one hash call.  Construction schedule names come from a small
   vocabulary, so a phrase list inverts most of them.  The resource-name pins
   (`name_hash` = 64-bit prefix, mpp_reader_tests.cpp:2445-2457) cover
   "First Last" person names.  A census first-name x surname list is about
   1e9 hashes, which is minutes of work.  And because the plaintext is already
   in public history, the hashes add linkage rather than protection.
   spec/mpp-records.md:372-383 still prints the UTF-16LE hex of the first five
   or six characters of each scrubbed name (for example "53 00 69 00 74 00 65
   00 20 00"), right next to the hash.  ff22c7b also re-adds a line to
   app_qt/sheet_views.cpp that still contains a 9-character client person name.
   39d11b4 then removes it, so the name sits in a wave-24 commit as an added
   line, not only in older history.  The fixtures are clean: all seven
   wave24-*.mpp carry Author/LastSavedBy/Manager "Fixture Builder", Company
   "Fixture Company", the path C:\w24\..., and only synthetic task/resource
   names.  I found no operator name and no Dropbox path in UTF-16LE or latin-1.
   Fix: use a salted HMAC with the key kept out of the repo, or plain row IDs,
   and drop the hex dumps.

4. **Medium -- ff22c7b (W24-26) -- docs/analysis/oracle/writer-name.json:126/329,
   spec/mpp-writer.md:312/442, spec/mpp-records.md:277,
   scripts/phase2d_propinfo_layout.py:129/142,
   docs/handoff/wave23-verification/review.md:204/218.**  The scrub was a
   blind global substitution, and it changed things that are not client
   content:
   - writer-name.json is a COM oracle recording.  It now holds a Name that
     Project never returned.
   - spec/mpp-writer.md case B still says the file grew "+512" for a string
     whose length is now different.
   - spec/mpp-records.md:277 says the 22-character replacement is "12 chars +
     NUL = 26 B".
   - A decompiler-analysis script's ordinary English word in a print banner
     was replaced (the "... SITES" / "... FUNCTION" headings).
   - The previous review's falsifying command now greps for the hash instead of
     the word, so it no longer finds anything.

   Falsifying test: `git grep -n client-string- wave24 --
   scripts/phase2d_propinfo_layout.py spec/`, then compare each replaced
   string's length with the byte counts quoted beside it.

5. **Medium (claims) -- ca9e7d7, ff22c7b, cc4ce8a commit messages;
   wave24-complete.md section 2.**
   - ca9e7d7 claims "app_qt ctest 18/18".  At ca9e7d7, app_qt/CMakeLists.txt
     registers 17 tests (`grep -c "add_test(NAME"`: wave23 17, ca9e7d7 17,
     ff22c7b 18).  The same commit also rewrites
     project_a_preds_golden.sha256 into the per-row format.  At ca9e7d7,
     task_order_test.cpp:243-252 still takes the first non-comment line as a
     single aggregate digest, so qt_task_order_corpus must fail on that tree.
   - ff22c7b and cc4ce8a were both committed at 00:09, each claiming a full
     fresh set of gate numbers.
   - build/w24 holds no log for those trees.  The gate logs are overwritten
     names; the earliest qt log is 00:43.
   - complete.md says core/app/qt were re-run "on fresh trees" of the final
     code (39d11b4, 00:47).  But build/w24/fresh-app-ctest.log and
     fresh-engine-suite.log are dated 00:22, before the 00:41 rebuild that
     includes 39d11b4's app/layout_model.h change.
   - START-HERE still says "fixtures OK (11 hashes, 10 .mpp)" and "the tree is
     clean as of ff22c7b"; 39d11b4 had to scrub a name after ff22c7b.

   The brief's "every gate before every commit, numbers in the message" was
   not met as written.  Falsifying test: `git checkout ca9e7d7` in a scratch
   worktree, build app_qt with the corpus present, and run ctest -R
   qt_task_order_corpus.  Predicted: it fails.

6. **Medium -- ff22c7b -- app_qt/tests/stored_table_test.cpp:787-803.**  The
   corpus gate used to assert what Project's own capture of PROJECT_A shows
   (row 3 italic and red).  It now asserts that the port does NOT render it
   that way (`!font.italic()`, `!colour.isValid()`).  A gate that compared
   against Project's capture of the same file was turned into one that pins a
   known divergence as a pass.  The fixer disclosed this ("ACCEPTED,
   LISTED").  It should be an expected failure or a removed check, not a green
   assertion of the wrong output.  Falsifying test: decode the file's text
   styles later, or compare against the capture; the gate will then have to be
   flipped back by hand, which is exactly the problem.

7. **Medium -- ca9e7d7 (W24-7) -- tests/data/scenarios/*.oracle.json:3-11.**
   The added provenance names `recording_commit` 4299ff8 (seven files) or
   d6724da (task-calendar).  Every one of the eight files was last rewritten
   by e91f19b ("wave 20: schema-2 oracles re-recorded"), and the bytes on disk
   are from that recording (`git log -- <file>`).  So the provenance points at
   a recording whose data was replaced.  The block is also hand-written in a
   different schema (`source_base`, date-only `recorded_at`) from what
   `--record` writes (engine_diff.py:975-983).  It is harmless to the
   comparison, because flatten() ignores it, but W24-7's "say which" is
   answered wrongly.  Falsifying test: `git log --format=%h -1 --
   tests/data/scenarios/recalculate.oracle.json` returns e91f19b.

8. **Medium (process) -- ff22c7b, cc4ce8a, ca9e7d7, 39d11b4.**  The brief
   required one finding id per commit, and tests/data changes in their own
   commit.
   - ff22c7b bundles nine items, including the unrelated palette and
     text-style change (W24-13/18) that re-targets three gates (finding 2)
     under a "row routing" title.
   - cc4ce8a bundles five, including a change to the parity bar DETECTOR
     (scripts/parity_report.py, gap merging).  That change is what makes the
     newly required colour measure findable, so it sits in the same commit as
     the gate that now depends on it.
   - 39d11b4 adds two .mpp fixtures (wave24-flag2/-flag20) together with
     reader, Qt and doc changes.
   - The two tools commits (394d69a, 763bbc5) carry no finding id.  Their
     prompt still tells the agent to make "one CI dispatch per workflow on the
     branch at the end", which conflicts with the publication hold.  They also
     grant WebFetch/WebSearch to an unattended run in a repo holding client
     data.
   - R16 (gutter "0" on pending rows, Insert-on-project-row appends at the end)
     and R17 (macOS duplicate menu label) were never mapped to a W24 item, and
     wave24-complete.md gives no disposition for them.  That is the "nothing
     silently dropped" rule.

9. **Low -- cc4ce8a (W24-12) -- app_qt/tests/ribbon_inventory_test.cpp:157,
   204-208, 426-429.**  The "kind" assertion checks that the PIN FILE's kind
   column says "button".  It never inspects the live widget.  If the port
   builds a combo as a button, or a button as a combo, the gate compares only
   labels and stays green.  The predicate self-check
   (`!pin_kind_is_button("combo")`) only tests a string comparison.  So the
   claim "a combo built as a button therefore turns this red" does not hold.
   Falsifying test: change one live control to a QComboBox with the same
   label; the gate stays green.

10. **Low -- ff22c7b -- app/layout_model_tests.cpp:605-610;
    tests/mpp_reader_tests.cpp:1324.**  The corpus record-count check derives
    its expected extra row from `input.show_project_summary`, which is the
    value under test.  A reader that decoded 0x03400009 wrongly would still
    pass.  Add `check(input.show_project_summary)` for PROJECT_A.  Separately,
    the whole Flag1..20 block sits inside `if (file_exists(all_flags))` with no
    else branch, so a missing wave24-flags20.mpp silently skips about 15 checks
    (check_fixtures would catch the missing file, but this test alone would
    not).

11. **Low -- ff22c7b (W24-15) -- core/mpp/reader.cpp:609-631; fixtures.**  I
    byte-checked the decode and it is sound.  Task FixedMeta wave23-outline vs
    wave24-flags20 differs only at offsets 322/323/331/332 (entry +24/+25/+33/+34)
    with xor fe/07/fe/07.  flag2 is 322 xor 04, flag20 is 332 xor 04, and
    wave23-flags is 323 xor 04.  Flag1 still maps to +24 bit 0x02, unchanged
    from before.  Two gaps remain:
    - The bit ORDER inside each group is pinned only for Flag2, Flag10 and
      Flag20.  No single-bit control exists for the +33 group (Flag11..17), so
      a permuted mask there still passes the all-20 aggregate.
    - The generator measure_flags.py, named in check_fixtures.py:67, is not
      committed (it exists only in build/w24).

    reader.cpp:380 still says "MPP14 exposes Flag1 only (its other bits are
    still unmeasured)".  Also, on MPP12 files, Flag10 now feeds the SUMMARY
    column through flag10_stored = true; it was blank before.  It is backed by
    the tf2007 measurement, but it is a behaviour change with no committed
    fixture.

12. **Low -- ca9e7d7/ff22c7b (W24-3) -- tests/data/README.md:15,
    check_fixtures.py, reader.h ProjectRow.**  The measured shown/hidden pair
    came from one session (oracle_w24.py TIMELINE_SHOWN / TIMELINE_HIDDEN), but
    only the hidden half is committed.  The named "shown partner" is
    synthetic-base, a different-session file.  Against the hidden file it
    differs in 114/Props by 57k bytes and in about 25 streams, so the committed
    pair cannot reproduce the "only that record differs" claim.  Even
    wave24-summary-off vs wave24-timeline-hidden (both from that session)
    differs in 214/CV_iew/Var2Data (4 bytes) as well as 0x03400008.  The
    decode (non-empty 0x03400008 = shown) also treats ANY top-pane view name as
    "Timeline shown".  Falsifying test: commit the shown partner, re-run
    streamdiff.py on the pair, and decode the 4 CV_iew bytes.

13. **Low -- ff22c7b -- core/session/session.cpp:383/434/456,
    tools/recalc/main.cpp:485-595.**  A blanket rename turned
    `summary_flag_disagreements` into `flag10_disagreements`.  Those counters
    count Task.Summary-vs-schedule disagreements and rows with no schedule
    result; they have nothing to do with Flag10.  There is no behaviour
    change, but the names are now wrong.

14. **Low -- app_qt/tests/shell_test.cpp:121-125; app_qt/main_window.cpp:2138.**
    shell_test still says the project row's refusal "is pinned in
    qt_task_order_corpus / save_reopen".  save_reopen uses synthetic-base,
    which hides the row; the real pin is qt_uid_routing.  The PDF export
    subtitle still counts RecordSet rows (project row included), so it reads
    "N+1 of N+1" where Print now says "N of N".

Checked and found sound (no finding):
- uid routing in app_qt: show_task_fields, delete_task, build_row_menu,
  task_information_values/apply_task_information, print_gantt by uid,
  current_print_job.
- The Insert template: edit_model.cpp:476-493 picks the largest file_row among
  live tasks, which matches add_delete.cpp:719-733 (last live row with a
  VarMeta group, deletes applied first).
- sync_plan_from_records offset and collect_start_edits by uid (uid 0 is
  skipped).
- 0x03400009 decode: off/on differ only in it plus GUID records; values decoded
  in all 11 fixtures match the tests.
- The R13 props test: mutating the reader constant to 0x024013c9 lands on a
  kind-4 record and fails the agreement check.
- --expect-fail compares the failing set exactly, and every non-pass status is
  counted.
- The workflow_run guard (success only, head_sha, tag dedupe; qt.yml has only
  dispatch and tag triggers).
- The corpus-free plan_recalc CI step (the test is registered unconditionally).
- PLAN_RECALC_UPDATE_GOLDEN == "1".
- The per-row Pre/Suc golden (83 rows, matched==83 required; same source sha
  as wave 23).
- The real double-click gates (indexAt and signal count).
- No client name in any wave-24 commit message, and no screenshot or corpus
  file added.
