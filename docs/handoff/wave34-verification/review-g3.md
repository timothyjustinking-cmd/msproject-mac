# Wave 34 G3 independent review (db305b6..3a8a31c)

Reviewer: independent read-only review, 08 November 2026.  Worktree
`D:\dev\msproject-mac-wt-v34g3` detached at `3a8a31c`.  Nothing was built and
no test was run.  The findings come from reading the code paths, plus read-only
olefile dumps of the committed fixtures and of the uncommitted COM JSON under
`D:\dev\msproject-mac\build\w30`.  The scratch scripts sit outside both trees.

## Findings (appended as they land)

### R34G3-1 (High, CONFIRMED that the shape is accepted; the mismatch is PLAUSIBLE) - the assignment writer accepts any assignment work, but every pair measures 40 hours

- Where: `core/mppwrite/assignment_growth.cpp:748-769` (`check_assn_row_work`
  only requires positive, finite work), `:813-828` (`contour_ticks8` turns any
  whole tick count into the contour); `core/mppwrite/summary_info.cpp`
  (W34-3.1 hunk, `write_project_summary` work string, about lines 629-666 at
  3a8a31c) formats any whole hour count from 0 to 1e15 as `"<n> hrs"`.
- Defect: all four committed pairs (`wave30-assign-add` 0 -> 40 h,
  `wave30-assign-delete` 40 -> 0, `wave34-assign-add-second` 40 -> 80,
  `wave34-assign-add-third` 80 -> 120) add or remove ONE 40-hour assignment,
  yet `assign_resource` / `unassign_resource` accept a placeholder of any work.
  The 72-byte contour block (`[work][units*16][ticks8]...` with
  `ticks8 = work/100*8`), the Task / Resource roll-ups and the DSI work string
  are all extrapolated from that one value.  Brief section 1 rule 2 names "a
  different duration" as exactly the factor that needs a second pair before a
  rule is generalised.
- Failure scenario: assign a resource to a 30-day leaf (240 h) or to a leaf
  whose span crosses non-working time.  The writer runs, because no refusal
  covers a work value, a duration or a calendar span.  The contour record may
  not be the single flat record Project writes for that shape, and that is
  untested.  The DSI string for a total of 1,000 h or more is written as
  `"1000 hrs"`.  Every committed fixture's DSI work string is between "0 hrs"
  and "160 hrs" (I scanned all `tests/data/*.mpp`), so the digit-grouping form
  Project uses for 1,000 and above is unmeasured.  No refusal test covers a
  work value other than 40 h (`tests/roundtrip_assignment_growth.cpp`
  refusals around lines 1412-1560 cover only damaged layouts, units and zero work).
- Fix direction: refuse any assignment whose work is not the measured
  2400000.0 (40 h) until a second pair varies duration.  Cap the DSI work
  string at the measured range, or measure one total of 1,000 h or more.
  Add a refusal test for each.

### R34G3-2 (High, CONFIRMED) - `TailNormalisation` masks more than per-save noise: the dwords it skips are the only bytes that say WHICH row an appended record points at

- Where: `tests/save_pair_noise.h:175-183` and `:445-448` (a declared range
  skips the mismatch check outright); `tests/roundtrip_assignment_growth.cpp:341-350`
  (the ranges `{base+0,4},{base+16,4},{base+32,4}` on `114/TBkndAssn/Fixed2Data`);
  `tests/roundtrip_resource_growth.cpp:278-291`.
- Defect: within one Project save, every 16-byte identity in these streams
  shares bytes 4..15.  Only the first dword tells one row from another.  The
  appended assignment record is `{fresh, task identity, resource identity}`,
  so masking the first dword at +16 and +32 removes the only evidence of
  which task and which resource the record references.  The save-pair
  comparison then checks 12 bytes that every live row has in common.  In the
  REMOVE pair, +32 is the null-resource identity `788bcba0 8c2a6d43 00..ff`,
  which is NOT per-save noise.  It is byte-identical in
  `wave30-assign-delete-control`, `-control-null` and the edit save, yet its
  first dword is masked too.  The header's justification ("Project renumbers
  every identity's first dword on every save") is also false as stated.
  Record 0 (`89b9c454...` in the delete pair, `5eb14c16...` in the
  add-second pair) and the tombstoned row keep their first dword across
  control, null and edit saves.
- Concrete mismatch the harness now accepts: a port save whose appended
  record names resource row 2 instead of row 1.  In
  `wave34-assign-add-second.mpp`, rsc rec 5 is `a59c9c38 d42d11f1 9f661cc10c054c03`
  and rec 4 is `a59c9c37 d42d11f1 ...`, so the two identities differ only in
  the masked dword.  Another accepted mismatch: a remove that writes
  `00000000 8c2a6d43 ...ff` instead of the null identity.
  `save_pair::compare_all` reports 0 mismatches for both.
- What still catches it today: hand-written checks on the port's own output
  (`roundtrip_assignment_growth.cpp:494-502`, `:1077-1088`, `:740`).  They hard-code
  task row 1, `p.rsc_row` and the `null_identity()` constant, and they are not
  cross-checked against Project's file.  The gate itself is weaker than rule
  1 allows, and any later caller of `TailNormalisation` gets no such
  compensation.
- Fix direction: do not exempt reference dwords.  Resolve each masked dword
  to the row index whose identity carries it, separately in Project's file
  and in the port's file, and compare the indices.  Or restrict the ranges to
  the fresh identity (+0) and keep +16 / +32 compared after mapping.  Drop the
  +32 range on the remove pair, since it is constant.

### R34G3-3 (Medium, CONFIRMED) - the assignment writer's VarMeta bound check wraps in 32 bits: out-of-bounds read on a crafted file

- Where: `core/mppwrite/assignment_growth.cpp:367` (`load_var_meta`).
- Defect: `entry.offset` is `std::uint32_t`, so `entry.offset + 4` is computed
  in 32-bit unsigned arithmetic and wraps for offsets `0xFFFFFFFC..0xFFFFFFFF`.
  The check `entry.offset + 4 > var2.size()` then passes.  On line 371,
  `var2.size() - entry.offset - 4` is a `size_t` subtraction that underflows
  to a huge value and also passes.  `get_u32(var2, entry.offset)` (unchecked
  `operator[]`, lines 156-161) then reads about 4 GB past the buffer.
- Failure scenario: a damaged or hostile `.mpp` with one Assignment or Task
  VarMeta entry whose offset is `0xFFFFFFFE`, opened and then given an
  assign / unassign.  This is a heap out-of-bounds read, which the ASan+UBSan
  CI job would flag if a test fed it.  No refusal test covers it.
  `resource_growth.cpp:270-271` writes the same check correctly
  (`entry.offset > var2.size() || var2.size() - entry.offset < 4`).
- Fix direction: copy the resource writer's form, and add one damaged-VarMeta
  refusal test with offset `0xFFFFFFFE`.

### R34G3-4 (Medium, CONFIRMED) - the headline "3 -> 4 is refused" claim has no refusal test

- Where: `core/mppwrite/assignment_growth.cpp:1017-1028` (`real_rows.size() > 2`
  and "no free placeholder" refusals); `tests/roundtrip_assignment_growth.cpp`
  (no test reaches either message; grep for "measured counts are 1 and 2",
  "free placeholder" and "real assignments" finds nothing in the test).
- Defect: brief rule 1 and the W34-3.1 record ("the refusal suite asserts
  that") claim a test per refusal.  The two refusals that fence off the only
  unmeasured neighbour of the settled append are untested.  A related test,
  `refuses_add(remove, key(1, 2), "measured", "add: the settled removal shape")`
  (line 1356-1357), uses the needle "measured", and nearly every refusal
  message in the writer contains that word.  It would pass whichever refusal
  fired.
- Failure scenario: a later edit drops or loosens `> 2` (say, to support a
  count-3 task) and the suite stays green.  The writer then appends a fourth
  assignment with no measured file behind it.
- Fix direction: add `assign_resource` on `wave34-assign-add-third.mpp`
  (task uid 1 holds three real assignments, plus a resource made unassigned
  through `with_edit`), expecting "the measured counts are 1 and 2".  Add a
  no-placeholder variant too.  Give the settled-removal test a specific
  needle.

### R34G3-5 (Low, CONFIRMED) - the W34-3.4 "COM facts" are not in the repository

- Where: `tests/mpp_reader_tests.cpp:3793-4110` (`test_w34_active`,
  `test_w34_insert`, `test_w34_baseline`, `test_w34_leveling`) and
  `core/mpp/reader.h:438`.  They cite `W30-1.2-project-inactivate*.json`,
  `W30-1.1-project-insert.json`, `W34-0.5-task-ops.json`,
  `W30-G3-project-pairs.json` and `W30-2-level.json`.
- Defect: none of these files is tracked.  They live under the git-ignored
  `build/w30/` and `build/w34/` folders (`scripts/oracle_w30_project_pairs.py:36`
  says "(not committed)").  The reader's existing pattern cites committed
  `docs/analysis/oracle/*.json`.
- Check made: I read the uncommitted JSON in `D:\dev\msproject-mac\build\w30`.
  Active per uid, the insert IDs, levels, outline numbers, manual flags and
  durations, the baseline start, finish and duration, LevelingDelay
  10080 / 14400, and PeakUnits 3.0 -> 1.0 / Overallocated True -> False all
  match the hard-coded test values.  So the values come from COM, not from
  the port's output.  But nobody outside this machine can check that, and CI
  cannot either.  Two items are inferences that the test labels: the
  Preleveled dates (no COM field; taken from the control's Start and Finish)
  and the Fixed2Data ordinal ("measured", no COM field).  `overallocated()`
  as `peak > max_units` (`reader.h:717`) is one-pair inference, and it
  ignores variable availability.
- Fix direction: commit the five JSON files (privacy-scanned) under
  `docs/analysis/oracle/` with their hashes, or quote the facts in a
  committed evidence note.

### R34G3-6 (Low, CONFIRMED) - the DSI test does not use the save-pair harness or its null saves

- Where: `tests/roundtrip_summary_info.cpp:153-181`.
- Defect: the header claims "control vs the committed null save is
  byte-identical in every pair", but the test never loads a null save, and it
  compares directly rather than through `save_pair::compare` with `effects`
  on.  Exact equality is stricter than the harness, so this cannot cause a
  false pass.  But the noise-free premise is never asserted.  The "edited"
  task table it feeds the writer is Project's own edit save, so the test
  proves the DSI derivation only, not any port save path.  The record's
  wording ("DONE ... against Calculate Project and Move") should say that.
  `check_dsi.py` is a manual gate (olefile is not in CI).
- Fix direction: load each pair's `-control-null.mpp` and assert DSI
  equality with the control, or route the stream through
  `save_pair::compare(Kind::exact, ...)`.

### R34G3-7 (Low, CONFIRMED) - a declared tail range also skips the length check at its positions

- Where: `tests/save_pair_noise.h:445-448`.
- Defect: the range `continue` comes before the `both_absent` and length
  logic.  If a declared range ever reaches the end of Project's stream, a
  port save truncated inside that range would report 0 mismatches.  No
  current range touches a stream end: the last masked byte is record +36 of
  48 and Rsc +28 of 40.  So this is latent.
- Fix direction: exempt a position only when
  `i < output.size() && i < result.size()`.

### R34G3-8 (Low, CONFIRMED) - W34-B5 leftovers

- `core/mpp/CMakeLists.txt:55` still says "(83 tasks, hashed names)".  The
  brief names this file's golden-path users for update.
- `scripts/ref_reader.py:116-117` silently `continue`s on any row that does
  not have 7 fields.  If every row were malformed, `check()` would print
  `0/0 ok` and return 0.  The C++ reader test fails on a malformed row
  (`mpp_reader_tests.cpp:294-298`) and asserts the 83-row count, so CI is
  not affected.
- Verified fine: the TSV header is `uid id duration_min start finish priority
  pct` with 83 rows, and no `sha256:` value remains anywhere in the tree.
  Both new hashes (`BA3D69A6...`, `4107C5B5...`) equal `sha256sum` of the
  blobs at 3a8a31c.  `check_fixtures.py` takes them from
  `tests/data/README.md`.  Dropping the name check is the decided scope, and
  the other six fields are still compared per uid.

### R34G3-9 (Low, CONFIRMED) - a proof failure after `save_props_edit` leaves the output file behind

- Where: `core/mppwrite/assignment_growth.cpp:874-918` (`save_and_prove`).
- Defect: `save_props_edit` writes and re-proves its own facts.  The
  assignment writer's `require_stream`, scalar and changed-set checks run
  AFTER the file is on disk, and a failure there throws without removing
  `path`.  The header's "every refusal leaves ... the output path untouched"
  holds for refusals, not for these post-save proofs.  This is reachable
  only through an internal inconsistency, so it is hygiene.

## Checked and not raised

- `both_absent` (`save_pair_noise.h:455-460`) is sound.  A position counts as
  agreement only when NEITHER Project's edit save nor the port's save stores
  it.  Unequal lengths still mismatch on every position one side lacks.  I
  could not build a mismatching save that it accepts.
- Range bases come from the committed null save's stream length (Project's
  file), not from the port's output.  `in_tail_range` only applies past the
  null save's length.
- Every pair test builds its output with the port's writer from Project's
  control, re-reads it from disk (`CfbDocument::from_file(out_path(...))`)
  and compares it with Project's edit save.  None compares the port with
  itself.  Fixtures go through `fixture::require_committed`, so a missing
  file fails rather than skips.
- The relations in the resource writer's masked identities are asserted on
  the port's output (`roundtrip_resource_growth.cpp:398`, `:448-452`): the
  repeated identity, slot identities 1 = 2, and slot 3 = the previous slot's.
- Reader additions (`reader.cpp` W34-3.4 hunks) go through the
  bounds-checked `u32_at` / `f64_at`, and the outline record bound is
  checked first.
- Privacy: no person, client or user-profile path in the diff (excluding
  `.mpp`) or in the commit messages of the range.  All six new
  `wave34-assign-add-*.mpp` fixtures carry "Fixture Builder" / "Fixture
  Company" metadata and contain no ASCII or UTF-16 user-path or name hits.
- New fixture hashes in `check_fixtures.py` and `tests/data/README.md`
  match `sha256sum` of the files.

## Refutation pass

- R34G3-1: refuted only if some upstream gate restricts the work.  I found
  none: `check_assn_row_work` and `contour_ticks8` accept any whole positive
  tick count, and the summary path accepts any whole hour count except 1.
  The finding stands.  Whether Project writes a different contour for a
  longer or weekend-spanning task is PLAUSIBLE, not proven.
- R34G3-2: the strongest refutation is that the hand checks still catch a
  wrong reference today.  That is true and stated.  It lowers the practical
  risk but does not restore the gate, and the remove pair's +32 mask is
  provably over a byte with no measured noise.  It stays High because the
  brief makes the harness the gate.  The coordinator may reasonably triage
  it to Medium given the compensation.
- R34G3-3: refuted only if every caller validated offsets first.  The Assn
  and Task VarMeta streams are not validated elsewhere before
  `load_var_meta` (lines 957, 1454, 1716, 1877).  The finding stands.
- A candidate finding was dropped: that the summary writer's `work_changed`
  could miss a change.  It compares the full f64 now (the old code compared
  only the low u32), so the new code is stricter.

Count: Critical 0 / High 2 / Medium 2 / Low 5
