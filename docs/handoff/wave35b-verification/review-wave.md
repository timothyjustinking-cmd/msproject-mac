# Wave 35b whole-wave review: cross-group interactions, the save entry points, the CLOSE record and hygiene

Range: `2ed490c..e95c6ca` on `wave35b` (75 commits: G0 to G4, CLOSE and the coordinator's
records).  Brief: `docs/handoff/wave35b-brief.md` (all groups, section 1) and
`docs/handoff/standing-rules.md`.  Builder's record: `docs/handoff/wave35b-complete.md`.
Per-group records and reviews: `docs/handoff/wave35b-verification/`.  Date: Mon 9 Nov 2026.

Reviewer: independent, adversarial, read-only.  Nothing in the repository was built, run or
edited.  Every statement below comes from reading the source, tests, records and commit
messages with `git show`, `git diff`, `git log` and file reads.  One mapping check ran outside
the build: each of the 75 commits on `backup/wave35b-pre-rebuild` (`ec326b6..18b84f1`) was
paired in order with the 75 on `wave35b` and compared by message and by `git patch-id --stable`.
Line numbers are at `e95c6ca`.

This review does not repeat the per-group findings R35bG0-1 to -9, R35bG1-1 to -10, R35bG2-1
to -9, R35bG3-1 to -5 and R35bG4-1 to -6.  None was found to be worse than recorded.

Verdict: 0 Critical, 0 High, 2 Medium, 4 Low.

## Scope checks

| Question | Result | Evidence |
|---|---|---|
| `read_task_file_shape` signature change (`bdbcd7e` old / `cc4c979` new): every caller | Clean | The only caller is `core/mppwrite/calculate.cpp:393`, which passes `"Calculate Project pair"`.  `read_task_mode_shape` and `write_task_mode` build `State` directly, so the task-mode texts are unchanged.  The default `pair` equals `shared`, so the loop is skipped for the default (`task_mode.cpp:684-691`).  The exception path is R35bG4-2. |
| G4 fail-closed defaults (`ResourceRow::calendar_base_uid = -1`, `TaskRow::type_block_known = false`) against earlier paths | Clean | Only `assignment_plan_refusal` reads them (`app/edit_model.cpp:2936`, `:2978-2989`).  Both shells build file rows through `plan_input_from_document` (Qt `plan_document.cpp:494`; macOS `main.mm` uses `plan.base`).  `sync_plan_from_records` copies the base rows by position (`app/layout_model.cpp:388-450`), and the session sort is display-only (`plan_document.cpp:530-540`, `:1048`), so no file row loses its reader-set value. |
| G3 and G4 Props pins (RW35-W7 payload pin, R35G0-10 old size) against the earlier writers that call `save_props_edit` | Clean | Record edits come from `calculate.cpp:772-783` (4-byte date, `00 00`: the pinned values) and `wbs_code.cpp:267-271` (the WBS mask and prefix: the pinned values).  `assignment_growth.cpp` and `resource_growth.cpp` pass scalars only (`:1907`, `:2238`, `:2696`; `resource_growth.cpp:846`).  The field-set path (`app/document.cpp`) does not use `save_props_edit`. |
| G3 project-row key pin (RW35-W6) and G4's R35G0-9 against WBS and Calculate | Clean | `check_project_row_key` (`varmeta_edit.cpp:202-235`) admits the WBS project record `"03"` built by `number_payload(3)` (`wbs_code.cpp:247-253`) and the five 8-byte zero overtime records (`calculate.cpp:757-769`).  A task WBS key on uid 0 is refused by the generic project-row gate. |
| Shared sort and outline helpers across layers (`sorted_order_of`, `outline_shape_measured`) | Clean apart from what is recorded | The app (`edit_model.cpp:3243`, `:4405`) and the writers (`viewstate_sort.cpp:594`, `viewstate_outline.cpp:138`) call one function each.  R35bG1-1 and R35bG1-3 already cover the defects. |
| Every Qt File > Save path reaches one cascade | Clean | Save, Save As and the close prompt all go through `MainWindow::save_document_path` (`main_window.cpp:3566`, called at `:3767`).  It calls `Document::save_structural` (`:3618`), which calls `save_structural_edits` (`app/document.cpp:770`).  `document_` is rebuilt on every open (`main_window.cpp:1147`, `:1207`), so the source path is always the open file's. |
| Dry run before the write, per kind (`save_structural_edits`) | Clean | Assignment: dry run, then uid compare, then write (`edit_model.cpp:6408-6436`).  Bold: dry run, then write (`:6443-6456`).  WBS: the dry run runs the whole chain on a scratch file, Props pins included (`wbs_code.cpp:306-332`).  Calculate: digest, then `validate_calculate`, then the atomic `save_calculate` (`edit_model.cpp:6485-6515`; `calculate.cpp:749-797`).  Sort and Show Level: dry run, then write; both dry runs return after every pre-write check (`edit_model.cpp:6523-6558`; `viewstate_outline.cpp:635`).  Task mode: `write_task_mode` re-validates, and the save fails when nothing is written (`edit_model.cpp:6330-6351`).  Each kind is exclusive before the first write (`:5317-5615`). |
| Calculate's dry run and the Props and VarMeta pins | Clean | `validate_calculate` does not run the append chain, so a Props or VarMeta pin refusal would first surface in `save_calculate`.  That function writes only a scratch file until the whole result is proved (`calculate.cpp:753-797`), so the target is untouched.  Its payloads are the pinned ones, so this does not happen. |

## Findings

### R35bW-1 (Medium): a grid edit made after a Sort or Show Level is queued is accepted, so 1.5's refusal holds in one order only

- Where:
  - `app/edit_model.cpp:3258` (`plan_unsaved_edits`): called only by the builders, at `:3427`
    (`make_show_level_edit`) and `:3492` (`make_sort_edit`).
  - `push()`, the `sort_view` and `show_level` cases (`:4368-4418`): no file path, so no check.
  - `app_qt/main_window.cpp:3489-3500` (`after_cell_commit`): clears the structural redo list
    but does not look at a queued `sort_view` or `show_level`.  The same is true of Fill Down
    (`:5196-5199`) and the multi-cell apply (`:5819-5828`).
  - The sheet's own Redo runs before the structural one (`main_window.cpp:2261-2266`), and a
    structural `push()` does not clear the sheet's redo list.
  - Tests: `app_qt/tests/sort_persist_test.cpp:189-213` and
    `outline_persist_test.cpp:230-251` only rename first and then choose the command.
- What is wrong: RW35-R2 (brief 1.5) asks that a persisted Sort or Show Level be refused
  "while any record edit is pending".  The fix tests the plan only when the edit is built.  After
  it is queued, a rename, a typed Duration or a Summary flip in the grid is accepted with no
  message, and File > Save still writes the queued edit.  The session sort re-sorts the
  display on the changed names (`plan_document.cpp:1048`), while the writer writes the order
  of the names the file stores (1.2's recompute, `viewstate_sort.cpp:594`).
- Failure scenario, on `wave35-viewstate-sort-name-desc-control.mpp`:
  1. Sort by Name descending.  It is queued ("queued; File > Save writes it").
  2. Rename two sibling rows in the grid so that their order by name swaps.  The rows re-sort on
     screen and the status line says nothing.
  3. File > Save writes the file's own Name-descending order, so the saved file and the order
     on screen differ.  The renames are not written.  This is RW35-R2's user-visible outcome.
  4. A second route: rename, then Ctrl+Z (sheet undo), then Sort (accepted, because the plan is
     clean), then Ctrl+Y.  The sheet's redo reapplies the rename under the queued Sort.
  The file written is a measured Sort and agrees with its own stored names, so no bad file is
  written.  The user's view and the saved state disagree, and the guard sits at the builder
  layer only.
- Fix:
  - In the Qt shell, have every cell-commit path (`after_cell_commit`, Fill Down, the multi-cell
    apply and the sheet Redo) do one of two things while `applied().back()` is a `sort_view` or
    `show_level`:
    - refuse the cell edit with its own text; or
    - withdraw the queued edit (`structural_.undo()` then `clear_redo()`) with a status message.
  - Or re-run `plan_unsaved_edits` against `doc_.plan()` in `save_document_path` before
    `save_structural` when a Sort or Show Level is queued.
  - Add `qt_sort_persist` and `qt_outline_persist` cases: queue, then rename, then assert the
    refusal or the withdrawal by its needle.  Add a case for the undo, Sort, Redo order.

### R35bW-2 (Medium): replacing or clearing a queued Sort leaves it on the redo list, and Ctrl+Y re-queues it under a different session sort (the Sort counterpart of R35bG4-1)

- Where:
  - `app_qt/main_window.cpp:5975-5981` (`apply_sort_choice`): withdraws the queued Sort with
    `structural_.undo()` and no `clear_redo()`.
  - `:5982-5988`: the "sort cleared; the queued sort was withdrawn" path, the same.
  - `:2267-2271`: Redo calls `structural_.redo()`.
  - `app/edit_model.cpp:4608-4621`: `StructuralEdits::redo` re-appends without `push()`, so
    the wave's `push()` checks (`:4368-4384`) do not run again.
  - Only `push()` clears the redo list (`:4536-4537`).
- What is wrong: R35bG4-1 and R35bG3-1 name this pattern for Show Level only
  (`apply_outline_level`, `:6112-6115`).  `apply_sort_choice` has the same withdrawal.  When the
  replacement is refused (a two-key sort, Start, Finish, Duration ascending, or an ascending
  reorder), the old Sort stays on the redo list, while `apply_session_sort` shows the new order
  (`:6026`).
- Failure scenario, on `wave35-viewstate-sort-name-desc-control.mpp`:
  1. Sort by Name descending (queued).
  2. Sort by Start.  The builder refuses it, the status says "session only; the file keeps its
     order", and the rows show by Start.
  3. Ctrl+Y.  The status says only "redo the structural edit".  Name descending is queued
     again, and the display still sorts by Start.
  4. File > Save writes Name descending.
  A variant: Sort, then clear the sort ("the queued sort was withdrawn"), then Ctrl+Y.  The
  rows show in file order while Save writes the sort.  The file is a measured Sort, so no bad
  file is written; what the user sees and what Save writes disagree.
- Fix:
  - Call `structural_.clear_redo()` after both withdrawals in `apply_sort_choice`.  Fix it in
    one helper with R35bG4-1's `apply_outline_level` fix.
  - Or, in the Redo handler, re-push a `sort_view` or `show_level` through `push()` and
    re-apply its display.
  - Add a `qt_sort_persist` case: Name descending, then Start, then Ctrl+Y.  Assert either
    nothing queued, or a queued edit whose order equals the displayed order.

### R35bW-3 (Low): every commit id in the wave's records points at the pre-rebuild branch, the promised old-to-new map is not committed, and the `9fa599b` statements are now false

- Where:
  - `docs/handoff/wave35b-complete.md:11-12, 26-34, 39-55, 80`.
  - `docs/START-HERE.md:8, 19-24`.
  - `memory/coordinator.md:11-15`.
  - `wave35b-verification/gates-g0.md:9-12`: "the old-to-new map is committed then".
  - Every `gates-gN.md`, `coord-gates-gN.md` and `review-gN.md` table.
- What is wrong:
  - `wave35b` was rebuilt onto the new `wave35`.  The old tip is kept as
    `backup/wave35b-pre-rebuild` (`18b84f1`).
  - Every id the records cite is on that backup branch only, not on `wave35b`.  Examples:
    `6472da3`, `90d8b4e`, `425019a`, `ec07ab7`, `e1397d1`.
  - The 75 commits map one-to-one, in order, with identical messages and identical patch-ids
    (for example `6472da3` to `d5756d6`, `90d8b4e` to `d117c15`, `425019a` to `428d570`,
    `ec07ab7` to `d60b3a6`).  No commit was lost or changed.  But no map is committed, so a
    merge into `main` leaves every cited id unresolvable once the backup branch goes.
  - The records also say `guard_names main..HEAD` exits 1 on the two `9fa599b` blobs, that
    `wave35b` "still carries the original commit", and that the coordinator owes "the
    `9fa599b` decision".  Since the rebuild, `9fa599b` is not an ancestor of `e95c6ca`.  The
    notes-generator fixtures enter through `68f6e94` only, the neutral version.  Those
    statements are now wrong.
- Fix:
  - Commit the 75-row old-to-new map under `docs/handoff/wave35b-verification/`, as was done for
    waves 28, 29 and 33.
  - Re-run `guard_names main..HEAD` on the rebuilt branch.
  - Correct the `9fa599b` and `guard_names` lines in `wave35b-complete.md`, `START-HERE.md`
    and `memory/coordinator.md`.

### R35bW-4 (Low): the open-finding queue the next wave reads omits the G4 review's ids and two coordinator ids

- Where:
  - `docs/handoff/plan-waves27-30.md:377-401` ("Queued from wave 35b").
  - `docs/handoff/wave35b-complete.md:61-77`.
  - `docs/START-HERE.md:14-25` and `memory/coordinator.md:8-16`.
  - The ids are raised in `review-g4.md` and `coord-gates-g4.md:65-70, 113-117`.
- What is wrong:
  - CLOSE predates the G4 review.  The coordinator's G4 record (`e95c6ca`) says "R35bG4-1 joins
    the next fix wave's Mediums ... R35bG4-2 to -6 join its Lows", but it updated only its own
    two files.
  - So the queue lists R35bG0-1 to -8, R35bG1-1 to -9, R35bG2-1 to -8 and R35bG3-1 to -5, but
    not:
    - R35bG4-1 (Medium);
    - R35bG4-2 to -5;
    - R35bG4-6 (coordinator, G7c);
    - R35bG2-9 (coordinator: the intermittent `qt_assign_resources_gate`, open until macOS CI-4
      is green, `coord-gates-g4.md:25-26`).
  - R35bG0-9 and R35bG1-10 appear only as mutation labels ("G0 W11; G1 S1, S4, A6, Q1, Q2"),
    not by id.
  - `wave35b-complete.md:63` still reads "review-g0.md to review-g3.md".
- Fix: add R35bG4-1 to -6, R35bG2-9, and the ids R35bG0-9 and R35bG1-10 to the queue, to
  `wave35b-complete.md` "Open at CLOSE", to START-HERE and to `memory/coordinator.md`.  Add
  this review's R35bW ids as well.

### R35bW-5 (Low): the CLOSE id table ticks ids "fixed" that the per-group reviews found only partly closed, omits two ids, and misnames the coordinator records

- Where: `docs/handoff/wave35b-complete.md:35-59`.
- What is wrong:
  - Only items 0.3 and 0.5 carry a "but see" caveat.  These ids are ticked "fixed" without the
    open finding a review raised against them:
    - R35G2-6 (3.2): R35bG3-1 and R35bG4-1, other paths still change the outline;
    - R35G3-1 and RW35-W1 (1.6): R35bG1-2, the guard reads FixedMeta byte 12 only;
    - R35G2-1, R35G2-3 and R35G3-1 "both layers": R35bG1-3, one shared guard serves both;
    - RW35-W3 (2.7): R35bG2-1, the tie and swapped-order refusals are writer-only;
    - R35G1-10 and R35G1-12 (G4 row): R35bG4-2 and R35bG4-3, partly closed.
  - RW35-R5 (closed in 2.5 and 3.3) and R35G3-10 (recorded in 3.5, `9cd003e`) are in no row.
  - Line 59 calls `gates-g0.md` to `gates-g4.md` "the per-group coordinator records".  For G3
    and G4 those are the builder's.  The coordinator's are `coord-gates-g3.md` and
    `coord-gates-g4.md` (`coord-gates-g3.md:3-6`).
- Fix: add a "but see R35bGn-m" note to each row above, list RW35-R5 and R35G3-10, and name the
  `coord-gates-*` files.

### R35bW-6 (Low): a builder commit carries the coordinator's G3 records

- Where: `d60b3a6` (was `ec07ab7`), "W35b-4.G0-12 (R34b-9, R35G0-12)".  It adds
  `docs/handoff/wave35b-verification/coord-gates-g3.md` (118 lines) and `review-g3.md`
  (140 lines) beside its 14-line test change.
- What is wrong:
  - Standing rule 6 and brief section 1 ask for one finding id per commit.
  - The coordinator's verification of G3 entered history under a builder's id and a builder's
    `Generated-By` trailer, so the trailer no longer tells who wrote those records.
  - The G4 review notes it only as a range exclusion.  No record says it happened or why.
- Fix: no history rewrite.  Record it in `coord-gates-g4.md`, or in the old-to-new map note
  (R35bW-3), as an accepted defect.  Have the coordinator's records commit from the verify
  worktree only after the builder's tree is clean.

## Clean

- **Trailers.**
  - All 75 commits were checked by script.  60 carry
    `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35b)`.
  - The 15 that do not:
    - the four coordinator records (`a53da76`, `5950bb8`, `b6c8944`, `e95c6ca`), which is
      expected;
    - eleven G2 builder commits: `6035aff` (2.3), `98ef680` (2.4), `f1a3f5b` (2.5), `34b88bc`
      (2.6), `11ffe7b` (2.7), `29adb21` (2.8), `87b5ed2` (2.9), `1db1aa8` (2.10), `8696150`
      (2.11), `a5ac1c9` (2.12) and `1505896` (the G2 marker).  These are the eleven R35bG2-4
      already records, now under their rebuilt ids.  Each carries the Sonnet co-author line.
- **Names and paths.**
  - 7,255 added lines were scanned, along with every commit message in the range.
  - No client task name, project title, person's name, or personal or company path was found.
  - Test-data names are synthetic ("Task Z", "Task C", "Night Shift", "24 Hours").  The only
    drive paths are the neutral oracle roots (`C:/w35/...`, and `C:\w30\...` in one README
    provenance row).
  - No `.mpp` or other binary is added or changed in the range.  The three added files are
    test headers (`tests/cfb_stream_removal.h`, `edl_control_path.h`, `edl_damage.h`).
  - The commit author field is the repository's git identity, unchanged from earlier waves.
- **The rebuild.**
  - The 75 rebuilt commits equal the pre-rebuild ones in message and patch.  The tree at
    `e95c6ca` equals `18b84f1`.
  - The notes-generator fixtures reach the branch only in their neutral form (`68f6e94`).
- **Calculate across groups.**
  - G0's leaf and parameter checks, G2's atomic write and request checks, G3's recomputed slack
    and G4's FixedMeta-by-offset lookup all read the same `TaskTable`.  They all run inside
    `read_calculate_shape` or `check_request`, before the scratch chain.
  - The source digest (2.2) is taken over the same `to_bytes()` at build time and at Save.
  - A grid Duration edit after Calculate is queued cannot slip through.  `push()` recomputes
    from the session plan (RW35-R4), the plan-side Finish = Start + Duration rule (0.4) fails,
    and the writer re-reads the file.
- **macOS.**  The macOS shell builds every edit from `plan.base`, the file's plan, not the
  grid-edited view (`main.mm:808, 3249-3591`).  So R35bW-1 is Qt-only.  Show Level is not wired
  on macOS, as the G4 review says.
- **Qt menu wiring (1.8).**  The five new `handle_action` cases call the same handlers as their
  ribbon buttons (`main_window.cpp:2086-2100` against `:2385-2483`).  So the Font Bold shortcut
  (`menu_model.cpp:113`), now live, reaches `make_cell_bold_edit` and `push()` like the ribbon.
  The Define WBS Code backlog row is R35bG1-7.
- **Cross-group refusal order.**  No later group moved a refusal ahead of a guard in a way that
  hides it.  The 1.5 check sits before the writer dry run for Sort and after it for Show Level,
  as its commit says.  G4's `type_block_known` and `calendar_base_uid` refusals sit ahead of
  the checks they protect.  The Props old-size check sits after the "keeps its payload" check
  and before the payload pin.
