0C/0H/0M/3L

# Wave 35f: whole-wave review (`47665e1..a20f493`, 25 commits)

Read-only and adversarial. I read everything with `git show a20f493:<path>` and
`git diff 47665e1..a20f493`. Nothing was built, run or edited. Line numbers are at `a20f493`. I
do not repeat R35fG0-1 to -9 or R35fG1-1 to -5.

## Merge rule

I found no new Critical or High, so none is open. The rule still needs:
- CI green on three OSs, including the push-triggered `macos-appkit` run for `wave35f`. That run
  is the only compile of W35f-1.10.
- 4(e): no writer's output changed. The only `core/mppwrite` change is the seam at
  `calculate.cpp:1121`, which is inert at 0. So 4(e) is not a rerun.

## What holds (the cross-group walk)

### 1. Undo and Redo against Save, Qt shell (`app_qt/main_window.cpp`)

- **The redo-list invariant holds.**
  - `push()` clears `undone_` (`app/edit_model.cpp:4715`), and an exclusive kind only stands
    alone (`:4097-4107`).
  - So the redo list holds either one exclusive edit or only non-exclusive edits. The
    `redo_top_is*` tests (`main_window.cpp:264-274`) are therefore complete.
  - `redo()` (`edit_model.cpp:4795`) bypasses `push()`. Every route that could leave an
    exclusive edit on the list beside a changed state ends the list:
    - a cell commit (`:3640`);
    - Cut, Paste and Fill Down (`:5179`, `:5265`, `:5416`);
    - Task Information, both the partial and the ok path (`:6058`, `:6067`, `:6072`);
    - the three document-edit queue sites (`:4298`, `:4984`, `:5036`; W35f-1.5);
    - a sort that is set (`:6234`, `:6312`).
- **0.1 x 1.4.** A Show Level can reach the redo list only by an Undo of a queued Level.
  - A session sort cannot be set while a Level is queued (`:6224`).
  - Setting a sort ends a Level on the redo list (`:6234`, `:6312`).
  - So clearing a sort with a Level on the list (1.4) always starts from the file's order.
    Ctrl+Y re-queues the Level through `after_structural_change`, which shows it
    (`:3686-3688`).
  - The 0.1 branch order is: read-only, then a pending document edit, then the builder, then
    the shown sort, then `push()` (`:6416-6441`). Only the queue is held back. I found no route
    that queues a Level under a shown sort, either by `push()` or by `redo()`.
- **0.1 x 1.8 x 1.5.** Two cases:
  - A queued Sort, then Undo (the Sort is on the redo list and the sort is shown), then a
    session Level (shown, not queued), then Table / Date Format. The format is accepted (nothing
    exclusive is applied), and 1.5 ends the redo list. Save writes the format and the file's
    order and outline. The display's sort and level were both announced as session only.
  - A pending document edit, then Sort, then Level. Both fall back to session only with the
    pending edit named (`:6261`, `:6419`). The pending-edit branch sits before the
    `sort_active()` branch, so no queue is reached.
- **Every exclusive builder entry has the reverse check.** These are all the `make_*` call sites
  for the ten kinds (`:3200`, `:3257`, `:3297`, `:3386`, `:3441`, `:3474`, `:3515`, `:6290`,
  `:6427`). The non-exclusive pushes need none. These include:
  - Task Information's trial: custom-field values and percent (`:5957-5998`);
  - Task Fields (`:4221`);
  - Predecessors;
  - Insert, Delete, Indent and Outdent;
  - project priority;
  - new-task mode.
- **No refusal narrowed a save guard, and none widened past a save that would succeed.**
  - Every one of the ten kinds is refused at save beside `document_edits.any()`
    (`edit_model.cpp:5542-5810`).
  - Each of those save refusals has an app-level test (the `*_tests.cpp` greps for "cannot be
    combined with a table format"; Calculate's is `app_edits_calculate_tests.cpp:1405`). So
    reworking `qt_task_mode_document_edit_gate` to drop its save step leaves no save-time guard
    untested.
  - The one over-wide refusal is R35fG1-1 (Custom Fields cannot be opened).
- **Save.**
  - With anything structural queued, Save goes through `save_structural` with the document hook
    (`:3788-3826`). Otherwise it goes through `Document::save`, which then discards and reloads
    (`:3832-3870`).
  - Both routes reload the file, so neither leaves a pending document edit behind to block
    later exclusive edits.

### 2. AppKit (`app/main.mm`)

- **The shell's surfaces are mirrored, and its Redo cannot re-queue an exclusive edit beside a
  document edit.**
  - The exclusive builders are add, rename, assignment (assign and unassign), Bold, WBS and
    Calculate (`:3307-3689`). Each one refuses beside a pending document edit.
  - Both document dialogs refuse beside any exclusive edit (`:4260`, `:4368`) and end the redo
    list after a successful queue (`:4322`, `:4448`).
  - A cell commit and a paste end it too (`:1507`, `:1892`).
  - The structural Redo is at `:5000`. With those clears in place, it has no route left that
    re-queues an exclusive edit beside a pending document edit.

### 3. AppleClang `-Wall -Wextra -Wpedantic -Werror`

- I found nothing it would reject:
  - Both helpers sit in the anonymous namespace (`:122-2062`), are defined before use, and are
    both used.
  - The C++17 if-init form was already in the file.
  - `return reason;` of a const local draws no `-Wreturn-std-move`.
  - `choices` stays used after the early return.
  - There is no new sign or integer conversion.
- The new Qt tests are clean for GCC and Clang too:
  - every lambda capture is used;
  - the `size_t` comparisons match;
  - `apply_preferences` is not `[[nodiscard]]` (`main_window.h:178`), so the bare call in
    `task_information_apply_test.cpp:671` draws no warning.

### 4. `core/mppwrite`

- The step-7 seam (`calculate.cpp:1121-1123`) comes after the scratch save and the re-read
  compare, and before `reread.save(path)`. Its throw lands in the `catch (...)` that removes the
  scratch file.
- With the seam at 0, the bytes and the control flow are unchanged. The append loop's check
  (`g == appended`, with at most 5 appends) can never match 7.
- No change can make the writer produce a different file.

### 5. Records

- The CLOSE id table lists every id in the 35e open queue. It calls none of them closed, and
  R35eG2-3 is the only one that waits on CI.
- Informational, since the builder could not know them: the coordinator's 35f findings
  (R35fG0-1 to -9, R35fG1-1 to -5) are not in the CLOSE table, START-HERE, `coordinator.md` or
  the plan. Two CLOSE rows read "fixed" although a review has since reopened part of them:
  - R35eG1-3: R35fG0-1 shows that its record correction is still wrong;
  - R35eW-1: its follow-on is R35fG1-1.

  The coordinator's close-out should carry them.

## Findings

### R35fW-1 (Low): at the CLOSE commit, the three state files still describe G0 in progress and give two CI statuses

- **Where:**
  - `memory/coordinator.md:1`: "wave 35f G0 in progress".
  - `memory/coordinator.md:16`: "(G1, open)".
  - `docs/START-HERE.md:21`: "Still open until G1 and the review".
  - `docs/handoff/plan-waves27-30.md:459`: 0.2 is "the Custom Fields dialog's Formula... button
    guards a queued Calculate". 1.8 widened that guard to every exclusive kind.
  - `docs/handoff/wave35f-complete.md:118` contradicts START-HERE `:42`, the plan and
    `coordinator.md:32` on CI:
    - the CLOSE record says R35cW-CI1 and R35cW-CI2 "stay 'fixed in source; CI is its gate'
      until the coordinator's run is green";
    - the other three say both were proved by the green wave 35d run (`3d22c39`).
- **Failure:** CLOSE (`a20f493`) updated none of the three state files, although START-HERE says
  to keep it current at every wave close.
  - `9c2fb1c` (after this range) prepends "CLOSED" to START-HERE but keeps the opening queue.
  - `coordinator.md` and the plan are still stale at HEAD.
  - A new session reads G1 as open and gets two CI statuses for the same two ids.
- **Fix:**
  - At the coordinator's close-out, set all three files to "35f closed; G0 and G1 fixed, reviewed
    by `coord-gates-g0.md` and `coord-gates-g1.md`, with R35fG0-* and R35fG1-* open".
  - Drop R35cW-CI1 and R35cW-CI2 from `wave35f-complete.md:118`.

### R35fW-2 (Low): 1.9's record generalises past 1.4, from the same wave

- **Where:** `docs/handoff/wave35e-complete.md:79` (the 2.1 row, written by W35f-1.9). It says
  "every route that changes the shown sort clears the redo list first".
- **Failure:** since W35f-1.4 (`d880529`, an earlier commit in this wave), clearing the sort
  (`main_window.cpp:6234`, `:6324`) keeps a Show Level on the redo list.
  - The row's conclusion still holds, because a Sort on the list is still ended. The builder's
    own `gates-g1.md:109` words it correctly ("while a Sort is on the redo list").
  - As written, though, the row states a rule the code no longer has. A later change could be
    reasoned from it.
- **Fix:** replace it with "every route that changes the shown sort while a Sort is on the redo
  list ends that list".

### R35fW-3 (Low): the 1.3 partial-apply status says what stuck but not what was dropped

- **Where:** `app_qt/main_window.cpp:6062`, the `!ok && applied` branch. The trial is dropped
  there (`:6057`), so a Percent or Custom Fields change in the same apply is not queued.
- **Scenario:**
  1. Task Information with a new Percent, a new Name and a Duration that does not parse.
  2. The status reads "task information: <duration error>; Name applied (Undo takes it back)".
  3. Nothing says that the Percent was dropped, and the dialog has closed.
  - Save then writes no percent. The status line was the place R35eG0-3 meant to make complete.
    This is not a wrong file, because the grid shows the old percent.
- **Fix:** when `structural_applied` was set, append "; the percent / custom-field changes were
  not queued". The 1.2 trial test (`calculate_gate_test.cpp:75-97`) can take the needle.
