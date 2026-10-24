# Wave 27 independent review (git diff wave26..wave27, read-only)

Reviewer: independent session, 2026-10-24.  Nothing built, no tracked file
edited.  Scratch scripts are in build/verify-w27/scratch/ (streamdiff.py,
taskdiff.py, propsdiff.py, strs.py, findpath.py).

## Verdict

The save-safety work in G0 holds up.  All UI routes to the % Complete edit
are closed: the five ribbon buttons are disabled, the handler and
set_percent_complete are gone, the sheet column is read-only, the Task
Information field is disabled, and main.mm has no route.  The summary and
uid-0 refusal now sits in the edit model.  An independent olefile stream
diff confirms that Project's Priority pair differs only at Task FixedData
row 0 +78/+79, plus GUID and timestamp churn elsewhere.  It also confirms
that the Unlink link-table bytes (TBkndCons FixedMeta: tombstones and header
flag) are what the test compares.

Two findings should interrupt the next wave.

- **Privacy (High).** All five new Project-saved .mpp fixtures embed the
  full local save path in UTF-16LE (in Props14 and 214/CEdl/Var2Data). The
  path contains a person's name and the company's Dropbox folder.  The
  completion record says these fixtures carry "neutral metadata".
- **Possible wrong display (High, probable).** W27-3.5 replaced the name
  hashes with OutlineNumber values that are impossible in MS Project: ID 1
  is outline "2" and ID 12 is outline "1".  The new test pins what is most
  likely a reader defect as an expected value.

Separately, the intermittent `cal` SEGFAULT is not a runner flake.  It has
a concrete code cause: a heap over-read in the shared test fixture builder.
That is Medium, because it affects test code only.  Counts: 0 Critical,
2 High, 5 Medium, 4 Low.

## Findings

1. **High.**
   - **Commits:** a40ef6d, d30c2e8, 8cad902.
   - **Files:** tests/data/wave27-percent-actuals.mpp,
     wave27-priority-control.mpp, wave27-priority-750.mpp,
     wave27-unlink-control.mpp, wave27-unlink.mpp.
   - **Location:** Props14 at offset 160 and 214/CEdl/Var2Data at offset
     about 410/422, both UTF-16LE.

   Each file embeds the absolute path it was saved to: "D:\<company> Staff
   Dropbox\<person>\Claude\Projects\...\build\w27\fixtures\<name>.mpp".
   That is a personal name plus the company's Dropbox root.  The oracle
   scripts neutralised UserName/Author/Manager/Company/Title, but not
   Project's last-saved-path record.  The CLOSE name scan,
   guard_names.py and the "client-string hex grep" all passed, because
   none of them looks for the owner's path or name.  The branch has been
   pushed.  Two older fixtures carry the same path (synthetic-calendars.mpp,
   added 39e30b4, and wave23-flags.mpp, added 5272242), so this is a
   pre-existing pattern that wave 27 extended fivefold.  Tim decides
   whether his own name and path count as personal data.  At minimum, the
   "neutral metadata" claim (wave27-complete.md W27-0.1, 0.3, 3.3) is
   untrue.

   **Falsifying test:** `python -E build/verify-w27/scratch/findpath.py
   tests/data/wave27-*.mpp` prints no stream.  Today it prints two per file.

2. **High (probable; needs one COM read to confirm).**
   - **Commit:** c7283d7.
   - **Location:** app_qt/tests/task_order_test.cpp, case_corpus
     (expected_outline); core/mpp/reader.cpp:2998-3014.

   The new corpus pin expects OutlineNumber "2, 2.2, 2.2.1, 2.2.2, 2.1,
   2.3, 2.3.1..2.3.5, 1" for IDs 1..12, while the rows display in ID order
   at levels 0,1,2,2,1,1,2,2,2,2,2,0.  In MS Project, ID order is outline
   order by definition.  The captured levels imply 1, 1.1, 1.1.1, 1.1.2,
   1.2, 1.3, 1.3.1..1.3.5, 2.  The port computes outline_number from its
   own parent-chain and sibling walk (reader.cpp:3014), not from a stored
   string.  So the pinned values show that the port's sibling order
   disagrees with its row order.  Either the Outline Number column, or the
   tree order, is wrong for this file.  wave27-complete.md presents this as
   a fact about the file ("ID order is not outline order in this file"),
   and the test now enforces it.  This is the "gate passing on the port's
   own output" pattern: the hashes were replaced by values that were never
   compared with Project.

   **Falsifying test:** open corpus project_a.mpp through COM and read
   `Tasks(1).OutlineNumber` and `Tasks(12).OutlineNumber`.  If they are "1"
   and "2", the reader is wrong and the test pins the bug.

3. **Medium (test-code UB; the CI triage is wrong).**
   - **Commit:** pre-existing since 038270e; triaged in 97e0b81 / 8981356.
   - **Location:** tests/mpp_fixture_builder.h:295 and 356-362.

   The builder does `fat_entries.resize(total * 4, kFreeSector)`, but the
   write loop reads `fat_entries[i * 128 + j]` for j < 128 for each FAT
   sector.  For any CFB under 32 sectors, the vector is smaller than 128
   entries.  cal_tests builds roughly 10-sector and 3-sector files (40 and
   12 entries), so the loop reads about 350-460 bytes past the heap block
   on every run.  MSVC Release does not check, and the garbage lands in FAT
   slots past the end of file, so the output stays correct.  It faults only
   when the over-read crosses into an uncommitted page, and heap layout
   varies with the environment block and ASLR.  That fits a one-off
   SEGFAULT on an unchanged commit that passes on re-run.  The crashing
   run took 0.98 s against the normal 0.01 s, which fits crash handling
   time.  No output appeared because stdout is fully buffered on a pipe.
   mpp_error_paths and mpp_perf_large use the same builder.  The completion
   record's "runner flake, not a code defect" (G1 detail and "Found, not
   fixed") is therefore unsupported; its local evidence was "repeated runs,
   count not logged".  Classification: a real latent defect in test code,
   not in core/cal.  core/cal itself showed no clock, TZ, locale, thread or
   out-of-bounds dependence (sub-review).

   **Falsifying tests:**
   - A Debug (/MDd) build of cal_tests should stop with "vector subscript
     out of range" in make_cfb.
   - Running the Release exe under full page heap (gflags /p /enable
     cal_tests.exe /full) or ASan should fault on the first make_cfb.
   - The fix is `resize(fat_sectors * (kSector / 4))`.

4. **Medium (standing rule 1 applied unevenly).**
   - **Commit:** 8fe6c18.
   - **Location:** app/app_edits_links_tests.cpp,
     test_real_unlink_matches_project (link_streams).

   Project's own Unlink save also changes the following, against the
   same-session control:
   - TBkndTask/FixedData: 11 bytes, all date words on uid 3 and uid 4 rows
     (offsets +98/+102/+174..176 and +104..110/+187/+191, swapped
     schedule dates).
   - TBkndTask/FixedMeta: 3 bytes; TBkndTask/VarMeta: 49 bytes.
   - TBkndTask/Fixed2Meta: 2 bytes.
   - 114/Props: two non-GUID words at 3350 (0 -> 0x18) and 85182 (0 -> 2).

   The port writes none of these, and the test deliberately compares only
   the three TBkndCons streams.  The completion record says so openly
   ("Project's Unlink reschedules").  However, W27-0.1 disabled % Complete
   for exactly this shape: Project writes more than the port.  W27-0.3's
   rule, "keep only if the bytes Project changes are the bytes the port
   changes", would disable or qualify Unlink too.  The port's saved file
   carries pre-unlink computed dates and slack for the unlinked tasks, and
   the 114/Props changes are unexplained.  This is not data loss, because
   Project recomputes on calculation, but the claim that Unlink "matches
   Project" holds for the link table only.  Tim should decide whether
   no-CPM link saves are acceptable under rule 1.

   **Falsifying test:** `python -E build/verify-w27/scratch/streamdiff.py
   tests/data/wave27-unlink-control.mpp tests/data/wave27-unlink.mpp`
   shows no TBkndTask or 114/Props differences.  Today it shows them.

5. **Medium (the Priority gate checks one stream, not the whole save).**
   - **Commit:** 5a991e9.
   - **Location:** app/project_dialogs_tests.cpp, around lines 165-197.

   The test proves that the port's TBkndTask/FixedData equals Project's
   750 save byte for byte, and that Project's pair differs there only at
   +78/+79.  Independently confirmed: olefile shows the only other pair
   differences are time-based GUID tails in 114/Props, Fixed2Data GUIDs,
   view-state and Props14/CEdl path records.  The test does not assert
   that the port's save left every OTHER stream equal to the control.  A
   writer regression that touched Var2Data or Props during a Priority save
   would pass this gate.

   **Falsifying test:** add a loop over the control's streams, minus a
   documented per-save list, asserting port == control.  It should pass
   today and fail if set_priority starts touching another stream.

6. **Medium (claim not supported; latent once % returns).**
   - **Commits:** 29f6827, 1ad385d.
   - **Location:** app/edit_model.cpp make_percent_edit (about lines
     238-272); app/app_edit_tests.cpp (the uid 999 case).

   W27-0.2 claims make_percent_edit refuses "an unknown uid (a pending
   insert has no saved row)".  In fact it accepts any uid present in
   whatever PlanInput it is given.  The W27-3.1 test itself pushes uid 999
   successfully by adding it to the plan.  A shell that passes the display
   plan, which includes pending inserts (as the removed
   set_percent_complete did via doc_.plan()), would queue a % edit on an
   insert.  The save then returns ok=true and drops the edit, with only a
   warning: a silent loss of the user's value under a successful Save.
   This is harmless while the buttons are disabled, but the wave-28
   actuals writer inherits it.

   **Falsifying test:** call make_percent_edit with the plan returned by
   StructuralEdits::apply after an insert_task, using the inserted uid.  It
   should return false.  Today it returns true.

7. **Medium (fixture provenance gap).**
   - **Commit:** 7a6ec96.
   - **Location:** scripts/check_fixtures.py (wave26-gantt-saves entries);
     tests/data/README.md.

   The 16 committed Gantt blobs come from scripts under build/w26/g3 that
   are git-ignored and uncommitted (oracle_styles.py, oracle_probe*.py).
   The source .mpp files are not committed either.  The provenance
   sub-check covers 8 oracles and none of these, so the "one change per
   save" evidence cannot be re-derived from the repository.

   **Falsifying test:** find a committed script that regenerates any
   *.style.bin from a COM session.  There is none.

8. **Low.**
   - **Commit:** 032cee2.
   - **Location:** app/edit_model.cpp undo() (about lines 430-445).

   Undoing a delete appends its displaced edits to the END of applied_.
   They then sit after edits that were pushed between them and the delete.
   The next undo removes a restored edit rather than the chronologically
   latest one.  No save content changes, because the edits are on
   different keys, but the undo order no longer matches the user's
   history.

   **Falsifying test:** push set_field A on t, set_field B on u, delete t,
   undo, undo.  B should be undone; today A is.

9. **Low.**
   - **Commit:** 8fe6c18.
   - **Location:** app/app_edits_links_tests.cpp,
     test_real_unlink_matches_project.

   If any of the three fixtures is missing, the test prints SKIP and
   passes.  The files are tracked, so today this only matters for a broken
   checkout.  A tracked fixture should fail when absent, not skip.

10. **Low.**
    - **Commit:** 1bf7b99.
    - **Location:** repo-wide.

    173 tracked files still contain CRLF, as the completion record itself
    lists.  The EOL item is done only for the three files named.  The
    record says so; it is listed here for completeness.

11. **Low.**
    - **Commit:** 8981356.
    - **Location:** docs/handoff/wave27-complete.md (G1 detail).

    The triage evidence "cal passed repeated local MSVC runs (the repeat
    count was not logged)" is unfalsifiable as recorded.  See finding 3 for
    the actual cause.

## Checked and found sound

- **% Complete routes:**
  - The ribbon spec (enabled=false), ribbon_pin.tsv and the handler cases
    (fall through to return) agree.
  - MainWindow::set_percent_complete is removed.
  - The sheet % column is read-only: task_column_editable allows only
    Name/Start/Finish/Duration.
  - The Task Information % line edit is setEnabled(false)
    (app_qt/dialogs.cpp:848).
  - main.mm has no set_percent route.
  - Only edit_model.cpp constructs StructuralKind::set_percent.
- **Summary refusal:** it is in make_percent_edit / push, and a
  plan-less push is refused.
- **Unlink in both directions:** saved_links_touching and
  make_link_delete_edit are correct.  The link-table bytes match Project's
  pair (TBkndCons FixedData identical; FixedMeta tombstone and header
  bytes only; Fixed2Meta identical).
- **W27-3.1:** a refused percent edit no longer fails the save.
- **W27-3.2:** undo restores displaced edits, and redo re-detaches them.
- **CI:** the fetch-depth: 0 additions only add history.  No step was
  skipped or weakened.  ctest still runs with --output-on-failure.
- **W27-3.5:** the structure checks still exercise the reader's level,
  summary and order, but see finding 2 for the outline numbers.
