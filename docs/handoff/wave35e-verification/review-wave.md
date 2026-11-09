# Wave 35e whole-wave review (2ea87e5..c0c81e5, 23 commits)

0C/0H/0M/4L

Read only, through `git show c0c81e5:<path>` and `git diff 2ea87e5..c0c81e5`. Nothing was built,
run or edited. Line numbers are at `c0c81e5`. The per-group reviews (`review-g0.md` to
`review-g2.md`) and the coordinator records (`coord-gates-g0.md` to `-g2.md`) were read first;
their findings (R35eG0-1 to -3, R35eG1-1 to -6, R35eG2-1 to -10) are not repeated below.

**Merge-rule verdict: this review opens no Critical and no High.** Nothing found here would stop
the merge of waves 34 to 35e once CI is green on three OSs and 4(e) passes.

## The five questions

### 1. Undo and Redo across every edit surface: Save against the display

What Save can write (`app_qt/main_window.cpp:3707-3760`): the structural queue and the two
document edits (view state, custom formula). Plain cell edits are session only. The only path
that puts an edit on the queue without `push()` or a builder is `StructuralEdits::redo()`
(`app/edit_model.cpp:4786-4799`, no exclusivity check). So the question is which actions leave
the redo list in place while they change state that a redone edit depends on. I walked every
route:

- **Cell gate, Cut, Paste, Fill Down, Task Information / Task Form (partial apply included):**
  each clears the structural redo list after a commit (`:3574`, `:5097`, `:5183`, `:5334`,
  `:5965`, `:5971`, `:5976`). Each refuses at entry beside a queued Sort, Show Level or
  Calculate (`:756`, `:5009`, `:5109`, `:5199`, `:5765`). The sheet's own undo stack does not
  survive a structural push, Undo or Redo: `PlanDocument::rebuild_from_base` makes a new
  `SheetModel` (`app_qt/plan_document.cpp:766-793`). So no sheet Undo or Redo can change cells
  beside a queued exclusive edit. The `trial` copy in `apply_task_information` (`:5875`) is
  assigned only on `ok`, and push clears its redo list. **Closed.**
- **Sort and Show Level on the redo list:** every session-sort change clears both
  (`:6135`, `:6206`, `:6217`; W35e-1.1(b)). A Level under a shown sort is refused (`:6288`;
  W35e-1.1(a)). Every other view change (Collapse All, Expand All, the row toggle, the summary
  toggle) is undone on Redo by `after_structural_change`. That function rebuilds every row
  expanded, then re-applies the queued level (`:3620-3622`) or the queued sort (`:3623-3646`).
  So after Redo the display is what Save writes. **Closed.**
- **Calculate Project on the redo list:** cell surfaces clear the list (above). Session view
  changes do not matter, because Calculate writes no view state. The document-edit dialogs do
  not clear it: that is R35eG2-2 (Low), and the save refuses the pair.
- **Table / Date Format, Custom Field Formula, Project > Custom Fields:** every exclusive kind
  beside a pending document edit is refused at save (`app/edit_model.cpp:5533-5795`, one branch
  per kind, each with a test in its `app_edits_*_tests.cpp`, Calculate's at
  `app_edits_calculate_tests.cpp:1405`). The only kind with an early refusal is Calculate
  (R35eG2-1 covers the third surface). The other kinds are R35eW-1.
- **AppKit cell writers:** `commit_open_edit` and `paste_from_pasteboard` are the only two
  (`app/main.mm:1471`, `:1862`). Both have the 2.7 refusal and both clear the redo list
  (`:1482`, `:1867`). Undo and Redo cancel an open edit first (`:4874-4876`, `:4900-4902`). The
  sheet history is cleared on every push (`:4883-4884`).

**I found no route where File > Save writes something other than what the display shows, and
none where it writes an edit that `push()` or a builder would refuse.** Every remaining mismatch
of exclusivity ends in a save-time refusal, and no file is written.

### 2. Did G1 or G2 weaken G0, or each other?

No.
- The G0 block (`main_window.cpp:5959-5967`) is unchanged at `c0c81e5`. No G1 or G2 commit
  touches `apply_task_information`.
- `redo_top_is_view_edit` only widens the redo clears from `sort_view` to `sort_view` or
  `show_level`. It never keeps a list that was cleared before.
- The 1.1(a) refusal runs after the queued-Sort refusal (`:6280-6283`), so the 35d needles are
  unchanged. Its over-reach is R35eG1-1.
- The 2.6 guards are entry refusals. They add no state and run before any dialog.
- The 2.1 mapping changes only the key that `after_structural_change` compares and sets
  (`:3632-3644`). No other code reads a queued `sort_view`'s `field_id` against the display (a
  grep of `.field_id` in `main_window.cpp`). The Qt branch it feeds is unreachable (R35eW-3).

### 3. `app/main.mm` (2.7) under AppleClang `-Werror`

Nothing found.
- `calculate_queued_refusal` (`:1445-1452`) is inside the anonymous namespace (`:122` to
  `:2037`) and comes after `note` (`:1436`). It is used twice.
- The `if (init; cond)` form is C++17. The target is `OBJCXX_STANDARD 20`
  (`app/CMakeLists.txt:848`).
- `plan.edits` is `app::StructuralEdits` (`:211`), and `applied()` returns a const reference.
- The flags are `-Wall -Wextra -Wpedantic -Werror` (`app/CMakeLists.txt:67-70`). No shadowing,
  narrowing, sign compare, selector or include was added.
- The header change that main.mm sees (`app/edit_model.h` now includes `"sort.h"`) resolves to
  `core/view/sort.h`, already on the path through `layout_model.h`. It is the only `sort.h` in
  the tree. `SortKey` has exactly the two members (`core/view/sort.h:28-31`), so the new
  two-element brace inits do not trip `-Wmissing-field-initializers`.

### 4. The CLOSE id table and the open-queue lists

- **Statuses:** the CLOSE table (`docs/handoff/wave35e-complete.md:159-173`) matches the commits.
  Three ids the group reviews showed to be only partly fixed are still "fixed" there: R35dG2-2
  (R35eG2-1), R35dG1-3 (R35eG1-2) and R35dG1-1 (R35eG1-3: the Undo-of-a-queued-Sort route to (a)
  is closed but untested). I found no further "fixed" id that is only partly fixed.
- **Informational (the builder could not know):** none of START-HERE, `memory/coordinator.md`
  and `plan-waves27-30.md` names any R35eG0-*, R35eG1-* or R35eG2-* id. All three still say
  R35dG0-1 stays open until the coordinator's G0 review (`START-HERE.md:21`,
  `coordinator.md:15`, `plan-waves27-30.md:432`), but `coord-gates-g0.md` has closed it. The
  status file is R35eG2-9. The coordinator should add the 35e queue (R35eG1-1, R35eG1-2,
  R35eG1-3 and R35eG2-1 are the Mediums) to the three lists when it closes the wave.
- The CLOSE text has two small record errors of its own: R35eW-2 and R35eW-4.

### 5. The writer

No writer output changed.
- In `core/mppwrite/` this wave changed only the two test seams (`calculate.cpp:35-46`,
  `:1060-1065`, `:1106-1111`).
- At their defaults, `g_fault_after_appends == 6` cannot hold (it is 0). A default `error_code`
  selects the real `symlink_status`.
- `app/edit_model.cpp` gained only `queued_sort_display_key` (`:3504-3506`), which is display
  only.
- **So nothing here can make a file that Project reads differently from the measured pairs.**
  4(e) needs no rerun for a writer change.

## Findings

### R35eW-1 (Low): every exclusive kind except Calculate still accepts a table / date format or formula edit (and is accepted beside one) that the save then refuses

- **Where:**
  - The 2.6 guards test only `calculate_project`:
    - `app_qt/main_window.cpp:4872` (Table / Date Format);
    - `:4922` (Custom Field Formula);
    - `:3437` (Calculate beside a pending edit).
  - `show_custom_fields` (`:4171-4215`) has no guard at all.
  - Sort and Show Level queue with no check of `document_->has_pending_edits()` (`:6186`,
    `:6328`).
  - The save refuses each pair (`app/edit_model.cpp:5755` Sort, `:5786` Show Level, and
    `:5533-5694` for task mode, resource rename, Add Resources, assignment, Bold and WBS code).
- **Scenario:**
  1. Sort Name descending on the five-task file. It reads "queued; File > Save writes it".
  2. Table / Date Format, OK. It reads "table / date format edit pending; File > Save writes
     214/CTable".
  3. File > Save then refuses the whole save ("a Sort edit cannot be combined with a table
     format or custom-field formula edit").
  - The other order behaves the same: a pending date format, then Level 1 is queued.
- **Severity:** no file is written, and this is older than 35e. But 2.6 now gives one exclusive
  kind the early refusal and leaves the other nine without it. `calculate.md:420-426` does not
  say so.
- **Fix:** expose `is_exclusive_kind` from `edit_model.h`. In the three dialogs, refuse when the
  queued edit is any exclusive kind. Refuse each exclusive builder entry while
  `has_pending_edits()`. Or record the remaining kinds in `calculate.md` and `view-state.md`.

### R35eW-2 (Low): the CLOSE gate note calls `6fa4c19` "tests only"; it changes `core/mppwrite/calculate.cpp` and `calculate.h`

- **Where:** `docs/handoff/wave35e-complete.md:131` ("the last code change is `aa06507`, 2.9,
  with `d697a05` and `6fa4c19` tests only").
- **What is wrong:** `6fa4c19` (2.11) changes the scratch-probe seam's signature and the probe
  expression in the shipped writer (`calculate.cpp:40-46`, `:1060-1062`; `calculate.h:183`).
  The fresh gates ran at `2dbb5d9`, which contains it, so nothing went ungated. The sentence
  understates what the last code commits touched.
- **Fix:** "the last code changes are `aa06507` (2.9) and `6fa4c19` (2.11), test seams in
  `core/mppwrite`; `d697a05` is tests only".

### R35eW-3 (Low): 2.1's Qt call site is unreachable, so "tested for Name and Duration" holds for the helper only

- **Where:**
  - `app_qt/main_window.cpp:3628-3646` (the re-apply that uses `queued_sort_display_key`).
  - `:6170-6181`: the Qt Sort dialog maps a Duration key to `SortKeyKind::other`, which
    `make_sort_edit` refuses, so no Duration sort can be queued from the Qt shell.
  - `wave35e-complete.md:77` (the 2.1 row).
- **What is wrong:**
  - A Name sort's two ids agree, so the mapping changes nothing for the one kind the Qt shell can
    queue.
  - Every route that changes the shown sort while a Sort is on the redo list clears that list,
    so `!shows_queued` is never true on Redo. The 2.2 commit says so itself: it dropped the
    "rows show" check for that reason.
  - The coordinator's U4 mutation agrees: `qt_sort_persist` stays green with the writer's id put
    back.
  - Reverting the call site to `queued.field_id` would therefore fail no test. Only
    `app_edits_sort_tests.cpp:218-225` holds the helper.
- **Fix:** record the branch as defensive and unreached in the 2.1 row. Or add a Qt seam test
  that pushes a hand-built Duration `sort_view` and checks the shown key after a Redo.

### R35eW-4 (Low): the CLOSE section still names `qt.yml` / `macos-qt` as 2.7's gate (extends R35eG2-5)

- **Where:**
  - `docs/handoff/wave35e-complete.md:187` ("CI on `macos-qt` is its only gate", in the CLOSE
    "Found, not fixed" list).
  - `:191` ("dispatch `qt.yml` and `ci.yml` (the two CI ids and W35e-2.7 are gated there)").
  - `docs/START-HERE.md:15`.
  - R35eG2-5 lists `:117`, `gates-g2.md:119` and `calculate.md:418`, not these lines.
- **What is wrong:** the `-Werror` compile of `app/main.mm` is the `macos-appkit` job in
  `.github/workflows/app-macos.yml`, which runs on push. The merge check "CI green on three OSs"
  must include that job's run for `wave35e`. A coordinator who follows `:191` and watches only
  the two dispatched workflows can miss it.
- **Fix:** in both CLOSE lines and START-HERE, add "`app-macos.yml` (`macos-appkit`, runs on
  push) is 2.7's gate", with R35eG2-5's fix.
