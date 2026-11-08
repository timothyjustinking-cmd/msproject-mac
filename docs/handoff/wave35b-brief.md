# Wave 35b brief - fix-only: the wave 35 reviews' Highs, Mediums and Lows (for the Sonnet builder)

_Written Mon 9 Nov 2026 by the Opus coordinator from the wave 35 verification.  Baseline:
branch `wave35b`, cut from `wave35` at its CLOSE (`18465b9`) plus the coordinator's records.
Sources, all in `docs/handoff/wave35-verification/`: `review-g0.md` to `review-g4.md` (R35G0-n
to R35G4-n), `close-review-writers.md` (RW35-Wn), `close-review-shells.md` (RW35-Rn), the
coordinator's mutation records in `gates-g0.md` to `gates-g3.md`, and the CI runs on `wave35`.
`docs/handoff/standing-rules.md` binds this wave.  The repo lives at `D:\dev\msproject-mac`.
**This wave adds no writer, lifts no refusal and measures no new Project pair: every change is
a refusal, a test, a test fix, a shell wiring fix or a record.**  Where a fix says "refuse",
the shape is unmeasured; refusing it is the whole fix._

## 0. Publication

The coordinator pushes `wave35b` and dispatches both CI workflows AFTER CLOSE.  No group and
not CLOSE pushes, dispatches a workflow or rewrites history.  Never force-push, and never work
round the pre-push guard.  (The branch carries the wave 35 G3 history defect in `9fa599b`; that
is the operator's and the coordinator's, not this wave's.)

## 1. Rules for this wave

The wave 34b brief's section 1 rules apply unchanged, with `35b` for `34b`: groups in order;
the marker `WAVE-35b-<group>-COMPLETE` (CLOSE `WAVE-35b-COMPLETE`) in
`docs/handoff/wave35b-complete.md`, committed; one finding id per commit; gates per commit
under `build/w35b/`; deferral only with evidence of an attempt; red then green, both quoted;
specific needles (never the command prefix or "not implemented in this port" alone); both
layers (edit-model builder and `push()`, and the writer); the measured shape still writes and
matches its pair with `effects` on; trailer `Generated-By: Claude Sonnet 5.5 via Claude Code
(wave 35b)`; no names; the three scans before every commit; LF; **no COM**.

Also:
- **Scan every new or regenerated fixture BEFORE its first `git add`**, with
  `scan_client_names.py` and a UTF-16LE scan of the author fields.  Wave 35 G3 committed
  `wave35-g3-notes-generator.mpp` carrying the operator's name (9fa599b) and fixed it a commit
  later; the old blob stays in history and the pre-push guard refuses the branch.  This wave
  makes no new fixture; a synthetic test file is built in memory or under `build/`.
- **Run ctest by its real name** and treat "No tests were found" as a failure, never a pass.
- **A test-only fix still needs a red run**: show the old assertion failing on the case it
  must catch, or explain why it cannot fail on Windows and how CI proves it.
- **The GCC/Clang warning classes MSVC does not report** (`-Wswitch` on an enum switch with no
  `default`, unused results, sign compares): read for them in every switch or header you
  touch.  Wave 35's RW35-R1 was this class and only a review found it.
- **`app/main.mm` cannot be compiled here.**  A change to it is read twice against the Qt shell
  and named in the record as "CI is its only compile gate".

## 2. Groups

### G0 - the macOS CI failure and the Calculate Project Highs

- **W35b-0.1 (R35G0-1, CI-4)  macos-qt fails on prompt titles.**  On macOS a `QMessageBox`
  carries no window title (already recorded at `app_qt/tests/backstage_gate_test.cpp:449-453`).
  The failing checks compare the title: `app_qt/tests/assign_resources_gate_test.cpp:326` and
  `app_qt/tests/prompt_hook_test.cpp:51`.  Identify and assert message boxes by their TEXT (a
  needle from the prompt at `app_qt/dialogs.cpp:1081-1089`), keep title checks only for input
  dialogs, and make `report_prompt` print on ANY failed prompt check, not only "not seen".
  Sweep every other Qt test for a `QMessageBox` title comparison and fix it the same way.
  Done when the coordinator's CI run shows macos-qt green; logs:
  `build/verify-w35/ci-macos-qt-42491592188.log` and `ci-macos-qt-42509520915.log`.
- **W35b-0.2 (R35G1-1, High)  Calculate Project ignores a task Deadline.**  Neither
  `calculate_plan_refusal` (`app/edit_model.cpp:3238`) nor the writer's `check_task_row`
  (`core/mppwrite/calculate.cpp`) reads `task_row::kDeadline` (+182), and `app::TaskRow` has
  no deadline field.  A deadline changes the backward pass Project writes.  Refuse a deadline
  on any task in both layers, as the outline save does (`edit_model.cpp:1069-1077`); add the
  reader field the edit model needs.
- **W35b-0.3 (R35G1-2, High)  any pending recalculation passes, not only the measured one.**
  The checks are only "0x02400024 = 01 00" and "row 0 Finish is not the latest leaf Finish".
  The pair measured one case: the critical leaf lengthens and neither critical flag changes
  (`build/w35/W35-1.2-calculate.json`).  Refuse, in both layers: a recalculation that moves
  the critical path (the computed critical flags differ from the stored ones), one that
  shortens the project, and one where a leaf's start is not the project start (the writer
  stamps Fixed2Data +46 from the Props project start).
- **W35b-0.4 (RW35-W2, High)  Calculate never checks the leaves' dates.**
  `read_calculate_shape` (`core/mppwrite/calculate.cpp:120-185`) requires no leaf Start equal to
  the project start and no Finish equal to Start + Duration, so a file left pending by a Start,
  Finish, progress or calendar edit is saved with the old early dates.  Require both relations
  (on the measured calendar) in the writer and in `calculate_plan_refusal`, with a refusal test
  on a leaf Start edit.  It overlaps 0.3: keep one commit per id and say which guard each adds.
- **W35b-0.5 (R35G1-3, High)  parameters not pinned.**  Record the pinned parameters (brief
  wave 35 rule 5) in `spec/writers/calculate.md` and refuse each unmeasured one in both
  layers: resources on the Resource Sheet (with or without an assignment) and assignments;
  a project calendar other than the measured Standard calendar, or one with exceptions or an
  edited week; baselines; inactive tasks (find where Active is stored, or refuse with the
  evidence that it cannot yet be read); a task type or effort-driven flag other than the
  pair's.

### G1 - the view-state, assignment and shell Highs

- **W35b-1.1 (R35G2-1, High)  Show Level pins no view state.**  `save_view_outline`
  (`core/mppwrite/viewstate_outline.cpp:121-150, 411-481`) never reads the view block, the
  applied view or the applied table; the committed table-cost, table-schedule, five zoom and
  sort-name-asc saves would all be collapsed.  Pin the uid-2 view block of the measured
  controls (FNV digests `b2a54e044baa30a2` and `6f6082994d512927`, default sort spec in both
  copies) and the CEdl applied-view and applied-table records; mirror the refusal in
  `make_show_level_edit`; a writer and an app test with a specific needle on each of the
  table-cost, zoom-entire and sort-name-asc fixtures.
- **W35b-1.2 (R35G2-2, High)  the view-state writers trust the caller's outline.**  Both
  writers read each row's OutlineLevel (+172) from `114/TBkndTask` and refuse unless it equals
  the requested level; `save_view_sort` also recomputes the order from the file (names and
  durations through the reader, or a tree check at minimum) and refuses a mismatch.  A writer
  test per guard.
- **W35b-1.3 (R35G2-3, High)  Sort accepts any five-task outline.**  `sort_plan_order`
  refuses every outline but {0,1,1,0,0} and the flat outline the flat-name-desc pair measures
  (the needle names the shape); the same check in the writer from +172 (1.2); tests on a
  three-level plan, and the untested "deeper than its parent" guard.
- **W35b-1.4 (R35G2-4, High)  Duration ascending and any ascending reorder are enabled with
  no pair.**  Refuse Duration ascending in `make_sort_edit`, `push()` and `save_view_sort`,
  each with its own needle, and refuse an ascending key whose order differs from the base.
  (The measuring wave adds `sort-duration-asc` and an ascending-reorder pair.)
- **W35b-1.5 (RW35-R2, High)  Sort and Show Level are built from the session plan.**
  `apply_sort_choice` (`app_qt/main_window.cpp:5970`) passes `doc_.plan()`, which carries unsaved
  grid edits (Name, Duration, Summary, Milestone); the writer checks only the base order.
  Rename two siblings, sort by Name, save: the file gets the order of names it does not store.
  In the builder (so both shells inherit it), refuse a persisted Sort or Show Level while any
  record edit is pending ("save or undo the grid edits first"), or build from the file's own
  rows; a Qt gate: rename, sort, nothing queued, specific needle.  Same for `outline_plan_rows`.
- **W35b-1.6 (R35G3-1 and RW35-W1, High)  the enabled assignment removal accepts a port-saved
  file that Project resaved.**  `core/mppwrite/assignment_growth.cpp:2395` checks the flag byte
  of resource row 0 only.  Project's resave sets bit 0x40 on the target resource's
  `114/TBkndRsc/FixedMeta` entries (0x1c to 0x5c, bytes 176 and 213), and its removal then
  clears `Rsc Fixed2Meta` byte 279 (0x2b to 0x0b), which the port never does.  Refuse a target
  resource FixedMeta flag other than 0x1c in the writer, and in the edit model, where
  `make_assignment_edit` has no origin check; each with a specific needle; test both on
  `wave35-g3-assign-remove-port-saved-control-null.mpp` (a writer test that runs
  `unassign_resource` on it and an `app_edits` test).  Re-point the 3.5 hybrid test, which
  today proves the writer accepts that file.
- **W35b-1.7 (R35G4-1, High, macOS only)  the resource pop-ups can target the wrong
  resource.**  `app/main.mm:3244-3281` (Resource Information) and `3366-3397` (Assign
  Resources) fill an `NSPopUpButton` with `addItemWithTitle:`, which removes an existing item
  of the same title, then map the index through a separate uid list.  With a duplicate or
  blank resource name every later choice maps to the wrong uid and Save renames or assigns it.
  Add items through `[popup menu]` with `tag = uid` and read `[[popup selectedItem] tag]`;
  prefill the current name as Qt does (R35G4-4); give the Qt dialog's first-match lookup
  (`app_qt/dialogs.cpp:1077`) the same uid-keyed fix, with a Qt test on two same-named
  resources.
- **W35b-1.8 (R35G4-2 and RW35-R1)  the Qt menu and the five wave-35 MenuActions.**  CLOSE's
  `513de29` made the switch exhaustive by listing the five as unimplemented Qt menu items, which
  fixes the compile.  Wire them instead: `handle_action` cases calling the existing Qt ribbon
  handlers, `resource_sheet_view` set in `update_menu_state()`, regenerate
  `docs/ui-command-backlog.md`.  Done when the coordinator's CI run shows linux-qt and macos-qt
  build green.

### G2 - the Mediums: Calculate Project and the wave 35 G0 assignment changes

- **W35b-2.1 (R35G1-4)** `save_calculate` is not atomic: it writes the target path seven
  times, and a missing or malformed `0x02400003` fails after the appends.  Check every record
  it touches before the first write, or build the whole result in memory and write once.
  Test: a file missing `0x02400003` leaves the target byte-identical.
- **W35b-2.2 (R35G1-5)** enforce late finish = project finish and zero slack on the critical
  leaf in the request check; make the save-time dry run prove the file is unchanged since the
  edit was queued (the comment at `edit_model.cpp:5837`), or correct the comment and add the
  check.
- **W35b-2.3 (R35G1-6)** refuse a file that was already calculated once when the command
  runs, with its own message, not at save through the var-record collision.
- **W35b-2.4 (R35G1-7)** a test, seen red, for each untested writer guard: row 0 uid,
  FixedMeta 0x48/0xEF, missing `0x02400024`, "project finish already equals", start anchor,
  late before early, late finish after project finish, missing `0x02400002`; and give the
  row-count test a specific needle.  The "already calculated" tests must reach their own guard.
  (Coordinator mutations M1 to M3 on G1 left the three middle guards green.)
- **W35b-2.5 (R35G1-8)** specific needles for the app linked/lag/snet tests and the Qt linked
  test (RW35-R5 is the same Qt needle); make the writer's SNET case reach the constraint check
  (a synthetic file with no link rows and an SNET constraint).
- **W35b-2.6 (coordinator mutation M5 on G1)** `qt_calculate_gate`'s save check is vacuous:
  with the leaf stamp changed 0x30 to 0x28 the core and app tests go red and the Qt gate stays
  green.  Make the Qt gate compare the saved file with Project's pair (as the app test does),
  and show it red on that mutation.
- **W35b-2.7 (RW35-W3)** refuse equal leaf Finishes and a critical leaf in a row other than the
  measured one (the leaf stamp is a constant outside the measured arrangement).
- **W35b-2.8 (R35G1-9)** the oracle's dates are shifted by the host's UTC offset: correct the
  README rows to the values COM reports, record the offset and the non-working project start
  in `spec/writers/calculate.md`.  Do not re-measure (no COM in this wave).
- **W35b-2.9 (R35G0-2)** the edit model checks the project default calendar and week for
  Assign Resources, as the writer does (add a plan input).
- **W35b-2.10 (R35G0-3)** a test that drives the reader's `calendar_base_extra_blocks`
  (coordinator mutation M5 on G0 left every test green).
- **W35b-2.11 (R35G0-4)** the settled add checks the copied assignment's resource calendar
  and row dates, or refuses a settled add whose first assignment differs from the pair's.
- **W35b-2.12 (R35G3-6)** an `app_edits` test with a specific needle for the edit model's
  port-saved removal refusal (today only the Qt gate's generic prefix pins it).

### G3 - the Mediums: view state, Bold and the shells

- **W35b-3.1 (R35G2-5)** `push()`'s `show_level` case checks the measured outline shape (both
  level vectors); tests: Level 1 on a three-level plan refused, Level 3 on a four-level plan
  refused (this kills the wave 35 builder's surviving a2); correct the a2 "equivalent" note in
  the wave 35 record.
- **W35b-3.2 (R35G2-6)** expanding or collapsing a row by hand in the shell
  (`app_qt/main_window.cpp:487-493`) withdraws a queued `show_level` edit with a status
  message, or is refused while one is queued; a `qt_outline_persist` case.
- **W35b-3.3 (R35G2-7)** replace the two generic "not implemented in this port for this
  project" needles with the gate text ("digest mismatch"; "task rows (with the project
  row)"), and the Qt outline needle for Levels 3, 4 and 9 (RW35-R5); a test with a specific
  needle for each unreached writer guard the review lists.
- **W35b-3.4 (coordinator mutation M13 on G2)** `qt_outline_persist`'s save check is vacuous:
  with `kOutlineFlagCollapsed` 0x03 changed to 0x02 the core and app tests go red (FixedMeta
  byte 219) and the Qt gate stays green.  Make the Qt gate compare the saved file with
  Project's pair, as `app_edits_show_level` does, and show it red on that mutation.  Do the
  same for `qt_sort_persist`: on M7 it went red only because queueing failed.
- **W35b-3.5 (R35G3-3)** the byte-42678 mask widened the enabled Bold-on writer to any
  session's view block.  Keep the mask for Bold off only, or add a test that flips bytes 42677
  and 42679 and is refused; record in the spec that the byte is a date-dependent float32
  (R35G3-10), not per-save noise.
- **W35b-3.6 (R35G3-4)** a test with a specific needle, seen red, for each untested
  `unbold_cell` guard: item not last (coordinator mutation M2 on G3 left it green), two items,
  font-word top byte other than 0x90, an already-unbolded item.
- **W35b-3.7 (RW35-W4 to -W7, RW35-R3, -R4)** each as its row in `close-review-writers.md` and
  `close-review-shells.md` says, one id per commit.
- **W35b-3.8 (R35G2-8, R35G3-2, -5, -7)** record corrections, no code: the Applied-table
  deferral is relabelled "not attempted: scope decision"; one rule for the font word's top byte
  across Bold on and Bold off ("only 0x90 is accepted"); the 3.5 "four named gaps" is a lower
  bound and the identity counter is the session identity Project renumbers on every save; the
  Underline and range deferrals rest on harness errors.  One id per commit.

Queued for the next MEASURING wave, not 35b: R35G0-5 (an effort-driven pair);
`sort-duration-asc` and an ascending-reorder Sort pair (R35G2-4); an Applied-table writer attempt
on the Cost and Schedule pairs (R35G2-8); Toolbar Underline (late-bound COM call) and a two-row
Bold range that really selects two rows (R35G3-7); a tie pair and a swapped-order pair for
Calculate (RW35-W3); UTC-neutral oracle dates (R35G1-9).

### G4 - the Lows

R35G0-6 to -13, R35G1-10 to -14, R35G2-9 to -11, R35G3-8, -9, -11, -12, R35G4-3, and the Lows
of `close-review-writers.md` and `close-review-shells.md`, one id per commit, each as its
review row says.  R35G3-8 (six wave 35 commits lack the trailer) is recorded, not fixed: no
history rewrite.

### CLOSE

Rerun every gate on a fresh tree, write the completion record with each id ticked (fixed with
commit, or deferred with evidence), and mark `WAVE-35b-COMPLETE`.  Push nothing.

## 3. The coordinator's part

Push (once the operator settles the `9fa599b` history), dispatch CI, fresh-tree gates,
mutations on each new refusal at the call site and the helper, an independent review of the
wave diff.  The 4(e) Project reopen check on every enabled save (Calculate Project, Sort, Show
Level, the wave 35 G0 assignment changes) is the coordinator's, by COM through
`scripts/oracle_lock.py`.  Merge `wave34`, `wave34b`, `wave35` and `wave35b` into `main` only
with CI green on all three OSs, no Critical or High open, and that check run.
