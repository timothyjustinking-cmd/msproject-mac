# Wave 35 G2 review: view-state persistence (98f74fb..daf46f8)

_Independent, adversarial, read-only review of the tree at `D:\dev\msproject-mac-wt-v34g3`
(detached at daf46f8).  9 November 2026.  No build, no COM, no edits to the repo.  The
fixture facts below come from a read-only olefile pass over the committed `tests/data`
fixtures (scripts kept in the reviewer's scratchpad, not in the repo)._

Scope: W35-2.1 Sort (enabled), W35-2.2 Applied table (deferred), W35-2.3 Show Level
(enabled), W35-2.4 Zoom (deferred).  Governing rule: standing rule 1 and brief section 1
rules 4 and 5 ("Pin every parameter ... view, row order and outline level"; refused in
both the edit model and the writer, each refusal with a specific needle).

## Findings

### R35G2-1 (High): the Show Level writer pins no view state; Project saves carrying an applied table, a zoom or a sort are accepted

- **Where:** `core/mppwrite/viewstate_outline.cpp:121-150` and `:411-481`.  The edit model
  has no check of its own either (`app/edit_model.cpp:3349` and the `show_level` case at
  `:4194`).
- **What is wrong:** `validate_edit` checks `edit.view_uid == 2`, but that is the caller's
  own field.  The edit model always sets it (`show_level_view_edit`), so the "view uid 2,
  refused otherwise" row of the capability matrix never looks at the file.  The writer
  never reads `214/CV_iew`.  It has no digest pin like the sort writer's, and it does not
  check the CEdl applied-view record (0x26400018), the applied-table name, or the sort spec.
  Its only file-side checks are the task row UIDs, the FixedMeta flags, the CEdl order,
  copy and layout records, and the two size words.
- **Verified at source and on the fixtures.** Each of these committed Project saves has six
  task rows with UIDs 1..5, every FixedMeta flag 0x01, a CEdl order of `1 2 3 4 5 ffdc0 0 0`,
  a 64-byte copy, a layout record that is absent or the 22-unit one, and size words equal to
  the CEdl length.  Each one therefore passes every check that `save_view_outline` makes:
  - `wave35-viewstate-table-cost.mpp` (Cost table applied; uid-2 block 42,690 B)
  - `wave35-viewstate-table-schedule.mpp` (Schedule table applied)
  - `wave35-viewstate-zoom-entire.mpp`, `-zoom-custom.mpp`, `-zoom-reset.mpp`,
    `-zoom-custom-10d.mpp`, `-zoom-custom-narrow.mpp` (a zoomed timescale)
  - `wave35-viewstate-sort-name-asc.mpp` (a persisted Name-ascending sort: spec
    `0e 00 40 0b 01 00`, displayed order unchanged)
- **Failure scenario:** a user sorts by Name ascending in Project (or applies the Cost
  table, or zooms), saves, opens the file in the port and picks View > Outline > Level 1.
  `make_show_level_edit` passes, because the writer's dry run accepts the file.  The shell
  shows "queued; File > Save writes it", and Save writes a Level 1 collapse on top of a
  sort, table or zoom.  No pair measures that combination.  Show Level under a sort is the
  "interaction with sort" case the brief names.
- **Fix:** in `save_view_outline`, pin the uid-2 view block of the measured controls: an
  FNV digest with the default sort spec in both copies, needing two digests
  (`b2a54e044baa30a2` and `6f6082994d512927`, because the deep-level2 control differs).
  Also pin the CEdl applied-view and applied-table records.  Mirror the refusal in
  `make_show_level_edit`, and add writer and app tests with a specific needle for each of
  the table-cost, zoom-entire and sort-name-asc fixtures.

### R35G2-2 (High): both view-state writers trust the caller's outline, names and order and never read them from the file

- **Where:**
  - `core/mppwrite/viewstate_outline.cpp:136-142`: the levels come only from `edit.rows`.
  - `:428-441`: only `table.uid()` is read.  The file's OutlineLevel (+172, the u16 that
    `wbs_code.cpp:152` and `outline.cpp:1000` already read) is never compared.
  - `core/mppwrite/viewstate_sort.cpp:125-171`: `sorted_order` only has to be a
    permutation of `base_order`.  No name, duration or outline level is read.
- **What is wrong:** the measured outline shape ({0,1,1,0,0} / {0,1,2,2,1,0}) and the
  measured sort order are guarded in the edit model only.  The writer's guard checks what
  the caller claims, not what the file holds.
- **Failure scenarios:**
  - Direct writer API: on any five-task flat file whose CEdl lists 1..5, the call
    `save_view_outline(doc, {uid 2, level 1, rows {1:0, 2:1, 3:1, 4:0, 5:0}}, out)` passes
    every check.  It writes flag 0x03 (collapsed) on a leaf and 0x00 (hidden) on two
    top-level rows, a state Project never wrote.
  - Same API: `save_view_sort` with Name descending and `sorted_order {2,1,3,4,5}` on the
    measured control writes a sort spec that contradicts its own displayed order.  No test
    covers this.
  - Through the app: the cascade comment at `app/edit_model.cpp` step 5l/5m says the dry
    run "proves the file is still the one the edit was built from".  For levels, names and
    durations it does not.  If the file is re-saved on disk between queue and Save (for
    example from Project, with the outline or names changed but the CEdl and view block
    intact), the stale levels or order are written onto the new file.
- **Fix:** in both writers, read each row's OutlineLevel (+172) from `114/TBkndTask` and
  refuse unless it equals the requested level.  In `save_view_sort`, also recompute the
  pinned order from the file (names and durations through the reader, or a tree check at
  minimum) and refuse a mismatch.  Add a writer test per guard with a specific needle.

### R35G2-3 (High): Sort is enabled for every five-task outline shape and all task data; outline level is unpinned in both layers

- **Where:** `app/edit_model.cpp:3183-3290` (`sort_plan_order`); `core/mppwrite/viewstate_sort.cpp:125-171`.
- **What is wrong:** brief rule 5 lists "view, row order and outline level" among the
  parameters a pair must pin.  The sort pairs measure exactly one tree: UID 1 a summary
  over 2 and 3, with 4 and 5 top-level leaves ({0,1,1,0,0}).
  - `sort_plan_order` accepts any five-row tree: flat {0,0,0,0,0}, three-level
    {0,1,2,1,0}, {0,1,1,1,1}, and so on.  It refuses only a row deeper than its parent
    allows, and that guard is untested (no test contains "deeper than its parent").
  - The writer cannot refuse an unmeasured tree at all (R35G2-2).
  - Nothing pins milestones, manual or inactive tasks, summary-duration roll-ups or links.
  - The "Outline = True" claim rests on one tree.  On a flat plan, Outline = True and
    Outline = False are indistinguishable, and that indistinguishable case is exactly what
    the builder used to refuse Outline = False.
- **Failure scenario:** open the measured control in the port, indent UID 5 under UID 4 and
  save (the port's outline writer does not touch the uid-2 view block or the CEdl order
  records).  Reopen it and Sort by Name.  `make_sort_edit` accepts it, the shell queues it,
  and Save writes a sort order for a tree no pair measured.  Through the writer API alone,
  any five-task file with the measured view block is accepted with any permutation.
- **Fix:** in `sort_plan_order`, refuse every outline other than {0,1,1,0,0} (needle naming
  the shape).  Add the same check to the writer from the file's +172 words (R35G2-2), and
  test both with a flat and a three-level plan.

### R35G2-4 (High): Duration ascending, and any ascending sort that reorders rows, is enabled without a pair

- **Where:**
  - `core/mppwrite/viewstate_sort.cpp:96-123` and `:367-368`
  - `app/edit_model.cpp:3254-3266` and `make_sort_edit`
  - the matrix in `spec/writers/view-state.md` section 1.5 ("one key, Name or Duration,
    asc or desc: enabled")
- **What is wrong:** the enabling pairs are Name descending, Name ascending and Duration
  descending.
  - There is no `sort-duration-asc` fixture (verified: `tests/data` has none).  The
    `07 05 40 0b 01 00` slot was seen only as the secondary key of the two-key pair, which
    the builder rules is "not claimed".
  - The Name-ascending pair leaves the CEdl untouched, so a CEdl reorder under an
    ascending key is measured nowhere.  Only the two descending pairs rewrite 0x35, 0x36
    and 0x24.
  - Direction is a parameter the user can vary, and it is pinned only for Name.
- **Failure scenario:** `make_sort_edit(plan, path, {{duration, true}}, true, false, ...)`
  on the measured control returns true.  `push()` accepts it, and Save writes
  `07 05 40 0b 01 00` plus a recomputed order `5 1 3 2 4` that Project never produced.
  The Qt dialog does not offer Duration, but the shared edit model is the API the macOS
  shell (G4) will drive.  The same goes for Name ascending on any file whose rows are not
  already in alphabetical order.
- **Fix:** refuse Duration ascending in `make_sort_edit`, `push()` and `save_view_sort`, each
  with its own needle, and refuse an ascending key whose order differs from the base until
  an ascending reorder pair is measured.  Alternatively measure `sort-duration-asc` and an
  ascending-reorder pair first.

### R35G2-5 (Medium): push() has no outline-shape guard for show_level, so the a2 survivor is not equivalent

- **Where:** `app/edit_model.cpp:4194-4219`; the a2 mutation is
  `edit.show_level > kOutlineMeasuredMaxLevel` changed to `> 99` at `:4204`.
- **What is wrong:** `push()` re-checks the level range, the rows against the plan, and
  that something is hidden.  It never checks that the outline is a measured one, so the
  only shape refusal is the writer's, reached through the builder's dry run or at save.
  The builder records a2 as "equivalent through the public API: a level above 2 never
  hides a row on a measured shape".  That is true only on measured shapes, and `push()`
  does not restrict itself to them.
- **Concrete kill:** take a plan with levels {0,1,2,3,0} and push a hand-built show_level
  edit at Level 3.
  - With the cap: `push()` returns false.
  - Under a2: it returns true, because Level 3 hides the level-3 row.
  - Even unmutated, `push(hand_edit(1, ...))` on that unmeasured plan returns true, and the
    edit is refused only when Save runs the writer.
- **a4 check (the cascade's one-edit check):** the claim holds.  `applied_` is private and
  is filled only by `push()`, which enforces exclusivity in both directions.  `push()`
  clears the redo list (`edit_model.cpp:4339`).  `undo()` and `redo()` move whole edits
  between the two stacks, and a delete's displaced edits can never coexist with an
  exclusive kind, so no public sequence gives the cascade a mixed queue.
- **Fix:** add the measured-shape check (both level vectors) to the `show_level` case of
  `push()`, plus tests: `push()` refuses Level 1 on a three-level-deep plan, and refuses
  Level 3 on a four-level plan (this kills a2).  Correct the a2 note in the wave-35 record.

### R35G2-6 (Medium): the shell leaves a queued Show Level in place after a per-row expand or collapse

- **Where:** `app_qt/main_window.cpp:487-493` (the delegate's `toggle_collapsed` callback);
  `apply_outline_level` at `:6042-6087`.
- **What is wrong:** only the Outline menu replaces or withdraws a queued `show_level`
  edit.  Clicking a summary's expand control calls `doc_.sheet()->toggle_collapsed(row)`
  and `after_model_change()` and never touches `structural_`.
- **Failure scenario:** the user picks Level 1 (queued), then expands summary UID 1 by
  clicking it, and saves.  The file records UID 1 collapsed and UIDs 2 and 3 hidden, while
  the user was looking at them expanded.  Project would have saved the displayed state.
  The status line still says "queued".
- **Fix:** in the toggle callback, withdraw a queued `show_level` edit (with a status
  message) or refuse the toggle while one is queued; add a `qt_outline_persist` case.

### R35G2-7 (Medium): weak needles, and writer guards no test reaches

- **Generic-prefix needles:**
  - `app/app_edits_sort_tests.cpp:419-421` checks the resource-sheet refusal with
    "not implemented in this port for this project".  That prefix is prepended to every
    writer refusal, so the test does not show which gate fired (labelled "applied view is
    the Resource Sheet"; the actual gate is the digest).
  - `app/app_edits_show_level_tests.cpp:439-441` does the same; there the gate that fires
    is the task-row count.
  - `tests/roundtrip_viewstate_outline.cpp` uses "is not one the pairs measure" for both
    the count and the shape refusals (acceptable, but the count case has its own number).
- **Guards no test reaches:**
  - `viewstate_outline.cpp:301-306`: the row-layout record of the wrong length ("row-layout
    record holds N bytes").  The tests change bytes 0 and 8, not the length.
  - `:239-245`: a doubled order, first-row, copy or layout record.  The test header at
    `roundtrip_viewstate_outline.cpp:27` claims "doubled", but only the zero counts are
    exercised.  The same holds for `viewstate_sort.cpp:290-295`.
  - `viewstate_outline.cpp:420-424` and `viewstate_sort.cpp:180-182` / `:372-374`: the
    missing-stream refusals.
  - `viewstate_sort.cpp:253-262`: "runs past the stream".
  - `app/edit_model.cpp:3223-3226`: "a row sits deeper than its parent allows".
- **Fix:** replace the two generic needles with the specific gate text ("digest mismatch";
  "task rows (with the project row)"), and add one test with a specific needle for each
  unreached guard listed.

### R35G2-8 (Medium): the Applied-table deferral rests on a judgement, not on a failed attempt

- **Where:** `spec/writers/view-state.md` section 2; commit 94b0796; the record in
  `docs/handoff/wave35-complete.md` (G2, "Deferred, with evidence of the attempt").
- **What is wrong:** brief section 1 rule 1 allows a deferral "only with evidence of an
  attempt (the command, the error, the log path), never 'with a reason'".
  - The table deferral cites the decode and two pair reports.  Its stated blocker, that a
    writer "would replay two blobs and claim every other table and view layout", is a
    reason, not a failure.
  - Standing rule 1 permits enabling exactly the two measured shapes (Cost and Schedule on
    the measured base) and refusing every other table, which is what the sort and outline
    writers do with their digest and shape pins.
  - No write was attempted, so no command, error or log exists.
  - The Zoom deferral is different and is backed by evidence (see "Checked and clean").
- **Fix:** either attempt the two-shape writer, recording the compare result and log path,
  or relabel the record as "not attempted: scope decision" so it is not presented as
  attempt evidence.

### R35G2-9 (Low): the size-word normalisation does not first assert Project's own words

- **Where:** `tests/roundtrip_viewstate_outline.cpp:214-230` (`with_control_path_size`);
  `app/app_edits_show_level_tests.cpp` (the same helper).
- **What is wrong:** the helper overwrites Project's `214/CEdl/VarMeta +0x14` and the
  `214/Props` 0x07000100 value in the null and edit saves with
  `edl.size() - path_record + control_path`.  It never checks that Project's original words
  equalled its own CEdl length.  If Project ever wrote something else there, for example
  under a hide that changes another item, the normalisation would replace it and hide a
  port bug.
- **Verified:** on all fifteen committed outline fixtures, and on the five sort pairs,
  Project's two words equal its CEdl length.  The normalisation masks nothing today.
  `tests/save_pair_noise.h` is unchanged and no `KnownGap` is used.
- **Fix:** in `with_control_path_size`, assert
  `u32(meta, 0x14) == edl.size() && u32(props, 11402) == edl.size()` before overwriting.

### R35G2-10 (Low): the writers' "no output file left behind" contract is false after the save

- **Where:** `core/mppwrite/viewstate_sort.h:145-146` and `viewstate_outline.h:129-131`
  (the contract); `viewstate_sort.cpp:434-517` and `viewstate_outline.cpp:545-668` (the
  post-save checks).
- **What is wrong:** `work.save(path)` is atomic (`cfb_writer.cpp:1294`, temporary file
  then rename).  But every proof after it (the re-read, the owned-byte sweeps, the unowned
  streams) throws after the target has already been replaced.  In the cascade an exclusive
  edit writes straight to `result.path`, so a post-save mismatch reports a failed Save
  while the user's target already holds the new bytes.  This is unlikely, because the
  checks hold by construction, but the header promises otherwise.
- **Fix:** run the proofs on the staged bytes (`work.to_bytes()` re-parsed) before
  `save()`, or reword the header contract.

### R35G2-11 (Low): misleading text in the record and the code comments

- `app/edit_model.h:650` and `spec/writers/view-state.md:15` call the sort base
  "three-level".  It is two levels (0,1).
- `app/edit_model.h:669` and the writer's refusal text at
  `core/mppwrite/viewstate_outline.cpp:139` give the outlines 1-based ("1,2,2,1,1",
  "1,2,3,3,2,1"), while the constants and the error's own subject are 0-based
  {0,1,1,0,0} / {0,1,2,2,1,0}.
- The capability matrix in `viewstate_sort.h:50` presents "ascending or descending" as
  measured for both fields (see R35G2-4).
- **Fix:** correct the three texts; one wording pass.

## Checked and found clean

- **Sort writer view pinning.** The FNV digest of the uid-2 block (both spec copies zeroed)
  is `b2a54e044baa30a2` on all three enabling controls.  The applied table and zoom saves
  give different digests (42,690 / 42,698-byte blocks; zoom digests `2571...`, `67de...`
  and others), so they are refused.  The digest effectively limits Sort to the measured
  seed: even `outline-deep-level2-control` differs (`6f60...`).  That is conservative.
- **Sort after Show Level, and Show Level after Sort.**
  - Sort refuses a collapsed file: the order record of 24 or 28 B is not the measured
    32 B, and the tail differs.
  - Show Level refuses a reordered CEdl (`5 4 1 3 2`).
  - Both refuse a second application of themselves: "already carries a sort" and "already
    collapsed or hidden".
  - Both kinds are exclusive in `push()` and in the cascade, and the shell's
    replace-or-withdraw uses `undo()` on the last edit only, which is safe given that
    exclusivity.
- **Row-layout record 0x2640003d.** The `4N+14` rebuild (`row_layout_payload`) reproduces
  Project's records byte for byte: N=2 `2 0 16 2c 16 + 0000`; N=4 `4 0 16 2c 42 58 16`;
  N=5 and N=6 match the controls.
  - N is at most 6 by the shape check, so there is no overflow.
  - The length is even, so no pad byte is needed.
  - The record is never added: `has_layout` is taken from the control.
  - The order tail `0x000ffdc0 - hidden` matches all three effect pairs (`ffdbe`, `ffdbc`,
    `ffdbe`).
- **FixedMeta entry index.** `row + 3` and the flag values 0x03 (collapsed) and 0x00
  (hidden) match the fixtures, and the project row stays 0x01.
- **Bounds.**
  - Every CEdl read is preceded by a length check (`payload + length <= size`; order
    length checked before the tail reads; copy 64 B; first row 4 B).
  - `parse_edl_items` requires the list to end exactly at the stream end before
    `assemble_edl` copies `item.end()`, so an odd last item with no pad cannot run past
    the stream.
  - The FixedMeta size is checked against `16 + 47(n+4)` before any flag read.
  - The `props_value_offset` scan is bounded, and the VarMeta size check covers +0x14.
  - The u32 header arithmetic cannot wrap to a false match.
- **Atomic write.** `CfbDocument::save` writes a temporary file, re-reads it and renames it
  (the post-save caveat is R35G2-10).  The tests assert there is no `.w51-structural-tmp`
  file and no target after a refusal.
- **Masks.**
  - `tests/save_pair_noise.h` and `tests/save_pair_compare.h` are untouched, and every new
    pair test passes `nullptr, 0` for KnownGaps.
  - The CEdl comparison zeroes the 12 header bytes (pre-existing), but Project's header
    words follow len-4/len-8/len-8 in every fixture, the same rule the port uses.
  - `CV_iew/Var2Data` has no control-versus-null noise in these pairs, so the spec slots
    are compared unmasked.
  - In the sort pairs the size words differ only in the masked low byte (path noise),
    which the port legitimately leaves alone.
- **Zoom deferral.** Verified from the fixtures:
  - The `zoom-custom` and `zoom-custom-narrow` controls and nulls are byte-identical.
  - The two edit saves differ in five `CV_iew/Var2Data` bytes (44,938-44,946).
  - The u16 at 44,938 is 0x0257 against 0x01ff, with 0x0064 in the control.
  - The narrow-window step is in the committed oracle (`oracle_w35_viewstate.py:120-131`).
  - Nothing is queued, and there is no edit kind or writer, so the feature is session-only
    in both layers.
  - The Applied table likewise has no edit kind and no writer, and the shell's Tables menu
    is unchanged (session-only).
- **Shell queue (`apply_sort_choice`, `apply_outline_level`).**
  - It maps ID, Start, Finish and other keys to their refused kinds.
  - Multi-key sorts are refused, and read-only files are refused.
  - It only queues what `make_*_edit` and `push()` accepted.
  - All Subtasks and Clear withdraw the queued edit.
  - Every other choice stays session-only, with the reason on the status line.
- **Mutation claims.** a4 is equivalent (see R35G2-5); a2 is not.
  `98f74fb-W35-2.1-mutations.status` shows m1..m8 killed after the rewritten
  m4/m7/m8, as recorded.
- **Hygiene.**
  - No client names, people's names, company or home paths in the diff text or the seven
    commit messages; the only path is the neutral `C:/w35` root.
  - Every commit carries `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35)`.
  - One item id per commit.
  - Fixture commits carry only fixtures plus their `tests/data/README.md` and
    `check_fixtures.py` hashes.
  - No `/*` inside block comments in the new files.
- **Name pin.** "First differing byte, two ASCII letters of the same case" is sound
  against Windows collation for the cases tried: case, digits, hyphen or apostrophe
  word-sort, prefixes, and multibyte characters (refused at the first byte).

## Summary

G2's measured shapes write correctly.  The Sort and Show Level writers reproduce
Project's five effect pairs outside honest masks, with no KnownGap and no noise-file
edits, and both deferrals leave their features session-only in every layer.  The
standing-rule-1 boundary leaks in four places:
- Show Level pins no view state at all.  Committed Project saves carrying a Cost table, a
  zoom or a Name-ascending sort would be accepted and collapsed.
- Neither writer reads the outline (or, for Sort, the names and durations) from the file,
  so the measured shape is guarded in the edit model alone.
- Sort accepts any five-task tree, although rule 5 names outline level.
- Duration ascending, and any reordering ascending sort, is enabled with no pair.
In addition, `push()` lacks the Show Level shape guard, which also means the builder's
claim that mutation a2 is equivalent is false; the a4 claim holds.  The shell keeps a
stale queued level after a per-row expand.  Several guards are untested or carry
generic-prefix needles.  The Applied-table deferral records a judgement rather than an
attempt.  No Critical: nothing found writes a file Project cannot open, and every
bounds path checked is guarded.

Counts: Critical 0, High 4, Medium 4, Low 3.
