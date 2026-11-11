0C/0H/0M/5L

# Wave 35g: whole-wave review

Range `1735250^..98a3d7e` on `wave35g`: 22 commits, the coordinator's brief `1735250` and 21
builder commits (G0 `284c3cb`, `24a5c1d`, `fe61064`; G1 `1663aaf` to `a0fb695`; CLOSE `98a3d7e`).
The coordinator's merge of `main` (`1678237`) is `97f6a70`, which is also `origin/wave35g`.

Everything was read with `git show` and `git diff`. Nothing was checked out, built or run, and no
gate script (`check_markers` included) was run. Line numbers are at `97f6a70` unless stated.

The group reviews are not repeated here: `review-g0.md` (R35gG0-1 to -4) and `review-g1.md`
(R35gG1-1 to -5).

## Merge rule

**No review leaves a Critical or a High.** G0 has 0C/0H/0M/4L, G1 has 0C/0H/0M/5L, and this
review has 0C/0H/0M/5L.

The other merge conditions are not this review's to settle:
- CI must be green on three OSs at `97f6a70`.
- 4(e) is not owed. The wave diff touches no `core/` and no `app/` file, so no writer's bytes can
  have changed.
- The `check_markers` hit that the CLOSE record says blocks the merge should be gone at `97f6a70`
  (see R35gW-1). This was read, not run: CI's `check_markers` step is the proof.

## What holds

- **The Formula... wording is right beside each exclusive kind.** There are ten exclusive kinds
  (`app/edit_model.cpp:105-116`). This is what Custom Fields... does beside each one at `98a3d7e`:
  - **Calculate Project.** The dialog does not open (`main_window.cpp:4265-4267`, W35f-0.2 kept).
    The pre-open text is the Calculate text, "save or undo the calculation first". So
    Formula... cannot be reached here. On the older "would write it, not X" form, see R35gW-4.
  - **Show Level.** The dialog opens. The callback refusal (`:4279-4283`) takes 1.5's branch
    (`:1676-1679`): "...would refuse a custom-field formula edit beside it; choose All Subtasks or
    undo the level first". Both steps are accepted afterwards:
    - All Subtasks withdraws the queued Level;
    - Undo puts it on the redo list, and `apply_custom_formula` then ends that list (`:4328`).
  - **Sort.** The dialog opens, and the text is "clear the sort or undo it first" (`:1680-1683`).
    G0's test needle was moved to the 1.5 wording inside the G0 block
    (`sort_persist_test.cpp`, the "W35g-1.5" check in the 0.1 block), and it also asserts that
    "another sort first" is absent. So the two groups' changes are tested together, not just side
    by side.
  - **The seven others** (task mode, resource rename, add, assign and unassign, Bold, WBS
    definition). The dialog opens, and the text is the generic one, "save or undo the queued edit
    first" (`:1688-1689`). `qt_task_mode_document_edit_gate` asserts it for Manually Schedule.
  - 1.5 put Show Level and Sort ahead of `data_change_refusal`. An exclusive kind stands alone in
    the queue (`edit_model.cpp:4097-4107`), so no reason that used to win is hidden now.
  - Read-only is still refused first at every caller.
- **Undo and Redo against Save, across the wave.** Only 0.1 changes behaviour. 1.4, 1.5, 1.6 and
  1.7 change status text only.
  - **Nothing new is queued when the dialog opens beside an exclusive edit.** The accept path does
    something only through `has_formula_edit()`. That is set only by an accepted editor, and the
    editor is refused while `has_exclusive_edit()` holds. The queue cannot change under the modal
    dialog.
  - **An exclusive edit on the redo list.** Suppose a Sort, Show Level or Calculate has been undone
    onto the redo list, so `applied()` is empty. Then Formula... is accepted,
    `apply_custom_formula` queues the document edit, and `structural_.clear_redo()` runs
    (`:4328`). So Ctrl+Y cannot re-queue the exclusive edit beside the formula.
  - Table / Date Format ends the redo list the same way (`:5014`).
  - The Undo that 1.5's advice names reaches `structural_.undo()` only when the sheet has no cell
    undo (`:2342-2361`). The document edit that follows then ends the redo list.
  - 1.4's "sort cleared" path calls `doc_.clear_sort()` and `after_model_change()`, and touches
    no queue.
  - 1.6 and 1.7 add words in the `!ok && applied` branch. That branch was already
    `clear_redo()` + `after_cell_change()` (`:6090-6091`).
  - No route found in the wave lets File > Save write something other than what is shown.
- **The merge of `main` conflicts with nothing.**
  - `git diff 98a3d7e 97f6a70` touches 14 files, all under `spec/writers/evidence/` and
    `tools/trace/`.
  - `git diff --name-only 1678237 97f6a70` is exactly the same 25 paths as
    `git diff --name-only 5863054 98a3d7e`: the 24 wave files plus the brief.
  - No wave commit touches either barred directory.
  - None of the merged files has a CR byte.
  - No merged line has a person's name or a personal or company path. The only absolute paths
    are the VM's own (`C:\trace\...`, `E:\runs`, `D:\vm\...`).
  - The trace notes do not touch `spec/writers/calculate.md` or `view-state.md`, which this wave
    edited.
  - `052b026` replaced the operator marker in `trace-captures-r5.md`, and
    `git diff 1678237 97f6a70 -- spec/writers/evidence` is empty.
- **The new test code reads clean for GCC and AppleClang with `-Wall -Wextra -Wpedantic -Werror`**
  (`qt_set_warnings`, `app_qt/CMakeLists.txt:42-54`). I name no likely break. What I checked:
  - **Sign compares:**
    - `plan_index` and `rowCount()` are both `int` (`resource_information_gate_test.cpp:172`).
    - `level_rows.size() < sorted_rows.size()` compares two `size_t`.
    - `has_collapsed_rows` loops `std::size_t` against `std::size_t row_count()` into
      `is_collapsed(std::size_t)` (`core/sheet/sheet.h:533,554`).
  - **Missing field initialisers:**
    - `core::view::SortKey` has two members (`core/view/sort.h:28-31`), so
      `SortKey{field, false}` is complete. Other green tests already use that idiom.
    - `Entry{action, command}` sets both of its fields.
  - **Unused lambda captures and parameters:**
    - every capture is used: `[this, &formula_refusal]`, `[seen, tries]`, `[&run, use_formula]`
      and `[&dialog_seen]`;
    - `field_id` and `current` are both used.
  - **Unused or set-but-unused variables:** none. The removed `target` takes its `remove` with it.
  - **Linkage and `-Wstring-concatenation`:**
    - the helpers sit in anonymous namespaces (`sort_persist_test.cpp:57-209`, and the same in
      the task-mode test);
    - no string-literal concatenation is inside an array initialiser.
  - **`dynamic_cast` to `CustomFieldsDialog`:** the class is `final` with no `Q_OBJECT` and a
    full definition is included.
  - **Conditional operators:** the operands of 1.6's `?:` are both string literals, which decay
    to `const char*`.
- **The ids are all accounted for.** Every id in wave 35f's "Open after the merge" appears in the
  CLOSE table. Nothing is deferred, and no id is gated on CI (no `app/main.mm` change). Taken with
  the group reviews:
  - **Fully closed at `98a3d7e`:** R35fG0-1, R35fG0-2, -3, -6, -7, -8, -9, R35fG1-2, -4, -5,
    R35fW-2.
  - **Closed with Lows against them:**
    - R35fG1-1 (R35gG0-1 to -4);
    - R35fG0-4 (R35gG1-1);
    - R35fW-3 (R35gG1-2);
    - R35fG0-5 (R35gG1-3);
    - R35fG1-3 (closed; its records are R35gW-3).
  - **Partly fixed:** R35fW-1, whose remainder is R35gG1-4. CLOSE (`98a3d7e`) rewrote the
    START-HERE and `coordinator.md` headers to "waves 34 to 35f merged". But the 35f bullet
    (START-HERE:30-34), `coordinator.md:23`, the "R35fG0-* and R35fG1-*" open lists and the plan's
    "since W35f-1.8" line are unchanged, so R35gG1-4 still stands.
- **Hygiene.**
  - CLOSE carries the trailer. The merge carries the coordinator's attribution only, which is
    right for a coordinator commit.
  - The wave's diff and the merge's diff add no CR bytes.

## Findings

### R35gW-1 (Low): at the pushed tip the state files and the CLOSE record still call the marker gate open and the wave unpushed

- **Where:**
  - `docs/START-HERE.md:8`: "Nothing of wave 35g is pushed";
  - `docs/START-HERE.md:24-26`: "**One open gate:** `check_markers.py` reports 1 hit ... needs the
    coordinator's or the trace agent's decision before a merge";
  - `memory/coordinator.md:8` ("NOT pushed") and `:18-19` ("Open gate ... resolve it before a
    merge");
  - `docs/handoff/plan-waves27-30.md:485-486` ("Open gate");
  - `docs/handoff/wave35g-complete.md:113-118` ("One gate is not green") and the closing "For the
    coordinator" line ("the marker-guard hit above resolved");
  - the START-HERE header (`:3-6`) gives `5e1c8ea`, the 35e merge, as the merge of waves 34 to
    35f, and does not name `59e7de4`, which merged 35f.
- **Failure:**
  - `97f6a70` brought in `052b026`, and its message says so. `check_markers.py` scans the tracked
    files at HEAD plus the messages in the range (`tracked_entries`, `scan_messages`).
  - The one hit was a tracked-file hit in `trace-captures-r5.md`. That file now equals `main`'s
    corrected copy. By reading, both `--worktree` and `--range main..HEAD` should now exit 0.
  - `97f6a70` is also `origin/wave35g`.
  - So a session that starts from START-HERE at the tip is told that the merge waits on a
    decision nobody now owes, and that nothing is pushed.
  - The 35g sentences were true when CLOSE wrote them. The merge commit, which made them false,
    did not touch them.
- **Fix:** in the coordinator's verification record for 35g, and at the merge into `main`:
  - record the gate as resolved by the merge of `052b026` (with CI's `check_markers` step at
    `97f6a70` as the proof);
  - mark the wave pushed;
  - name `59e7de4` as 35f's merge in the START-HERE header.

  Leave the CLOSE record as history, with a dated note under it.

### R35gW-2 (Low): R35dW-5 is still carried as waiting on CI after two green `macos-appkit` runs, and the CLOSE record dates it to the wrong wave

- **Where:**
  - `docs/handoff/wave35g-complete.md:148`: "R35dW-5 (fixed in source, `macos-appkit` its gate,
    from wave 35f)";
  - `docs/START-HERE.md:27`: "(fixed in source, `macos-appkit` its gate, until the coordinator's
    run)";
  - `memory/coordinator.md:20`;
  - `docs/handoff/plan-waves27-30.md:469` and `:488`.
- **Failure:**
  - R35dW-5 is W35e-2.7 `e03da3e` (`wave35e-complete.md:85`), not a wave 35f item.
  - Its gate, `macos-appkit`, was green at `c0c81e5` (run 42692007216, "compiles `app/main.mm`,
    item 2.7", `wave35e-verification/coord-gates-close.md:39`) and again at `9c2fb1c` (run
    42818310625).
  - The plan contradicts itself. `:446` says the gate "was green at `c0c81e5`", and `:469` and
    `:488` still list the id as waiting on that gate. START-HERE:49-50 says the same "green ...
    to be closed by the coordinator".
  - R35cW-CI1 and R35cW-CI2 were closed on the same evidence, the green 35d run, and W35f-1.13
    recorded that. So the queue carries one id that only needs the coordinator's word, and
    R35cW-4 looks partly open because of it.
- **Fix:**
  - The coordinator decides R35dW-5 in its 35g record. Either it is "proved by the green
    `macos-appkit` runs 42692007216 and 42818310625", which leaves R35cW-4 with no open remainder,
    or the record says why a compile alone does not close a refusal.
  - Then set the four state-file lines to match.
  - Correct "from wave 35f" to "W35e-2.7 `e03da3e`".

### R35gW-3 (Low): the two wave 35f rows that the 35f coordinator said were reopened are not annotated, though CLOSE calls them re-fixed

- **Where:**
  - `docs/handoff/wave35f-complete.md:12` and `:135`, the R35eG1-3 rows: "fixed `1a2436e`; ... the
    1.1 records corrected";
  - `:60` and `:140`, the R35eW-1 rows;
  - the claim at `wave35g-complete.md:144-146`.
- **Failure:**
  - `coord-gates-close.md` (35f, "Records") names exactly these rows as reading "fixed" while a
    later review had reopened them in part:
    - R35eG1-3, through R35fG0-1: the 1.1 correction was itself wrong in one sentence;
    - R35eW-1, through R35fG1-1: the widened Custom Fields... refusal was too broad.
  - CLOSE says both are "re-fixed by `24a5c1d`, `284c3cb` and `60d23a6`". The code and the 35e
    record were fixed, but the 35f rows were not:
    - the R35eG1-3 rows still say plainly that the records were corrected, with no pointer to
      R35fG0-1 or W35g-0.2;
    - the R35eW-1 rows now name W35g-1.8 (the coverage limit) but not R35fG1-1 or W35g-0.1, the
      over-broad refusal.
  - So the record defect the 35f coordinator queued is still there in the file it named.
- **Fix:**
  - Append to `:12` and `:135`: "(one sentence of the correction was wrong, R35fG0-1; corrected by
    W35g-0.2 `24a5c1d`)".
  - Append to `:60` and `:140`: "(the Custom Fields... refusal was too broad, R35fG1-1; narrowed
    to Formula... by W35g-0.1 `284c3cb`)".

### R35gW-4 (Low): the Calculate branch keeps the "would write it, not X" form that 1.5's own comment calls misleading

- **Where:** `app_qt/main_window.cpp:1645-1649` (`data_change_refusal`), reached from
  `document_edit_refusal` (`:1684-1687`) beside a queued Calculate. The surfaces that show it are:
  - Table / Date Format;
  - Custom Field Formula;
  - Custom Fields..., before it opens.

  The comment at `:1672-1675` gives the reason for the change.
- **Failure:**
  - 1.5 gave Sort and Show Level "File > Save would refuse X beside it", because "would write it,
    not X suggests Save drops X when it refuses the two together" (R35fG1-2's second sentence).
    The generic kinds already read the same way.
  - Beside a Calculate, the same document edits still read "calculate: a Calculate Project is
    queued and File > Save would write it, not a table / date format edit; save or undo the
    calculation first".
  - So one refusal, three kinds, two forms. Of the three, the Calculate one is the form the wave
    itself judged wrong.
  - R35fG1-2's Fix named only `sort_view` and `show_level`, so 1.5 is not wrong to its Fix. The
    advice ("save or undo the calculation first") holds, and nothing is queued. This is Low.
- **Fix:**
  - Give `document_edit_refusal` its own Calculate branch ahead of `data_change_refusal`: "calculate:
    a Calculate Project is queued and File > Save would refuse X beside it; save or undo the
    calculation first". Keep `data_change_refusal`'s text for the cell edits, Cut, Paste and Fill.
  - Move the `qt_calculate_gate` needles for the three surfaces, and see one of them red.
  - Or record in `calculate.md` that the Calculate text is kept, and why.

### R35gW-5 (Low): 1.6's test cannot see an Undo that does nothing

- **Where:** `app_qt/tests/calculate_gate_test.cpp:448-471` (W35g-1.6, R35fG1-4).
- **Failure:**
  - The case commits Name and Duration, fails on Finish, presses Ctrl+Z once, and asserts only
    that the Name still reads "Third partial apply".
  - That also passes if Undo took back nothing. For example, the sheet branch's
    `doc_.sheet()->undo()` (`main_window.cpp:2344`) could be skipped, or `can_undo()` could be
    false after the partial apply.
  - The comment says "one Ctrl+Z restores the Duration only", and the new status text tells the
    user that Undo takes back one field per step. Neither the Duration's revert nor the step count
    is asserted.
  - The case's own needle is the status text alone. Its Undo check rules out "takes back both",
    not "takes back none".
- **Fix:**
  - Record the Duration cell's text before the apply.
  - After one Ctrl+Z, assert that the Duration cell is back to it, and that the Name is still
    committed.
  - After a second Ctrl+Z, assert that the Name is back too.
  - Mutate the sheet-undo call once to see the first check red.

## Not findings

- **4(e).** It is not owed. `git diff 1735250..98a3d7e --stat` has no `core/`, `app/` or `tests/data`
  path, so no writer's output can have changed.
- **CLOSE's "`main..HEAD` still lists only this wave's 21 commits".** It is true as written: CLOSE
  wrote it before its own commit, which is the 22nd. The merge adds one more, plus `main`'s three
  trace commits on the second parent.
- **The poller in `run_custom_fields`, the silent dead Formula... button, and the untested
  "refused, then cancelled" status.** These are R35gG0-3, R35gG0-4 and R35gG0-1. They are not
  repeated here, and none of them changes across groups.
- **1.4's "choose it again to queue it" and the 1.7 note on the `!ok && !applied` path.** These
  are R35gG1-1 and R35gG1-2.
- **The stale 35f lines in START-HERE, `coordinator.md` and the plan.** These are R35gG1-4.
  CLOSE fixed the two headers only. R35gW-1 covers only what the merge itself made false.
- **The `33e15c0` and `0c35d05` message slips.** These are R35gG1-5, and the CLOSE record.
- **GCC 12 and 13's `-Wrestrict` and `-Wstringop-overflow` false positives on `std::string`
  `operator+`.** The new concatenations in `apply_task_information` and `apply_sort_choice` follow
  the same `std::string + const char*` shape as many existing lines that CI already compiles green
  on `ubuntu-gcc`. No new shape is added.
- **Formula... beside a queued Show Level has no test of its own.** The wording comes from the same
  branch that `qt_outline_persist` drives for Table / Date Format beside a queued Level, with the
  new text and the "another level first" absence asserted. A separate case would test the callback
  again, not the wording.
