0C/0H/0M/5L

# Wave 35g G1 review (`fe61064..a0fb695`, 16 item commits and the record, items 1.1 to 1.16)

Read-only review. Everything was read through `git show a0fb695:<path>` and
`git diff fe61064..a0fb695`. Nothing was checked out, built or run, and the builder's gate logs
under `build/w35g/` were not used as evidence. Line numbers are at `a0fb695`.

## What holds

- **1.5 (R35fG1-2) changes words only.**
  - `document_edit_refusal` (`app_qt/main_window.cpp:1668-1690`) is still gated on
    `structural_.has_exclusive_edit()`, so the set of refused document edits is unchanged.
  - The two new branches test `applied().back()`. An exclusive kind stands alone in the queue, so
    Show Level, Sort and Calculate are mutually exclusive there. Moving Show Level and Sort ahead
    of `data_change_refusal` cannot hide Calculate's text.
  - Read-only is not part of this helper. All three callers call `refuse_read_only()` first
    (`show_custom_fields`, `show_table_date_format`, `show_custom_formula`), so a read-only file
    still gets its own reason.
  - The advice holds on the routes it names:
    - All Subtasks withdraws a queued Level (`:6442-6455`);
    - clearing the sort withdraws a queued Sort (`:6280-6305`);
    - Undo moves either onto the redo list, and the document edit then queues and ends that list
      (W35f-1.5).
  - `view_change_refusal` keeps its text for the view changes. `app/main.mm` has no Sort or
    Show Level command, so there is nothing to mirror there.
- **1.6 (R35fG1-4): `applied_count` is right.**
  - `SheetModel::commit_edit(false)` pushes an undo step only when `outcome.changed`
    (`core/sheet/sheet.cpp`, `commit_edit`). The count goes up under the same condition
    (`main_window.cpp:6077-6081`).
  - A field that commits with no change (the same value written another way) adds no undo step,
    no count and no name. A field whose text matches the rendering is skipped before
    `begin_edit`.
  - With `coalesce_with_previous` false, no two fields merge into one step, so "Undo takes back
    one field per step" is true. The test's Ctrl+Z check (one Undo leaves the Name committed)
    proves it on the shell's route.
- **1.7 (R35fW-3) in the partial branch.** `structural_applied` is set exactly when the trial
  holds a custom-field or Percent edit (`:6018`, `:6026`). In the `!ok && applied` branch the trial
  is always dropped, so the note there is never false. The first partial-apply case asserts that
  the note is absent when nothing was dropped. The remaining gap is R35gG1-2.
- **1.4 (R35fG0-4): no note on a read-only file or beside a queued edit.** The condition
  (`:6299-6300`) needs `read_only_reason_` empty and `structural_.applied()` empty.
  - A queued Sort that is withdrawn goes through `after_structural_change`, which rebuilds with
    every row expanded, so `has_collapsed_rows()` is false.
  - A queued Level cannot coexist with a shown sort.
  - On the measured six-task outline, the advice holds: the test ends by choosing Level 1 again
    and asserting that it queues. The cases where it does not hold are R35gG1-1.
- **The new tests assert what they claim, and each has a specific needle.**
  - 1.2: `level_rows.size() < sorted_rows.size()` turns the subsequence check into a shown-Level
    check.
  - 1.3: the sentinel (All Subtasks, which queues nothing under a queued Calculate) is checked to
    replace the earlier refusal before each row.
  - 1.6 and 1.7: positive and negative needles on the exact status text.
  - 1.8: each builder row's needle is `<command>: a table / date format ... is pending`. Only
    `pending_document_edit_refusal(<command>)` writes that text, so each row would go red if its
    own call site were removed, even for the three entries (Add Resources, Assign Resources,
    Define WBS Code) whose call sites the builder did not mutate one by one.
  - The Custom Field Formula cases need both no dialog and the widened wording, so a revert to
    the Calculate-only check fails both. The Resource Information case reaches the Resource
    Sheet with the format pending and needs the command-named text.
  - The read-only fixture `tests/data/wave33-fat-slack.mpp` is an existing tracked fixture, and
    the test only opens it.
- **The records (1.9 to 1.13) match their findings' Fix paragraphs.**
  - 1.9: there are ten rewordings in three files. No "not yet closed by the coordinator" or "stays
    open in the coordinator's queue" is left outside the 35f review that quotes them. The
    follow-on ids match the Fix. `wave35e-verification/coord-gates-g1.md` exists and is the review
    the rows cite.
  - 1.10: "built but not pushed", and the red is credited to 0.3's mutation, in
    `wave35f-verification/gates-g0.md` and `wave35f-complete.md`.
  - 1.11: `spec/writers/view-state.md:383-385` names 0.3's five-task case.
  - 1.13: the 2.1 row (`wave35e-complete.md:79`) uses the Fix's words.
  - 1.8's erratum, `spec/writers/calculate.md` and both `wave35f-complete.md` rows state the
    lifted limit correctly.
- **1.14 to 1.16 (R35fG0-9, R35fG1-5).**
  - Every line the findings named is now 100 columns or less. The five named test lines are
    rewrapped, and `target` and its `remove` are gone.
  - `status-2026-11-10.md:54` was already rewrapped by W35f-1.16 (`336f197`), as the record says.
  - The only lines over 100 columns that this range adds are Markdown table rows. No `.cpp` line
    over 100 columns is added.
- **Hygiene.**
  - All 17 commits carry `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35g)`, and each
    item commit's subject has exactly one finding id. R35fG0-9 took two commits, as recorded.
  - `git diff --check fe61064..a0fb695` is clean, and the diff has no CR bytes.
  - No added line or message has a person's name, a client string or a personal path.
  - `app/main.mm` and `core/` are untouched.

## Findings

### R35gG1-1 (Low): 1.4's note advises "choose it again to queue it" where choosing it again cannot queue, and is silent when another edit is queued

- **Where:** `app_qt/main_window.cpp:6299-6301` (the cleared path of `apply_sort_choice`), and
  `PlanDocument::has_collapsed_rows` (`app_qt/plan_document.cpp:1043-1053`).
- **Failure or scenario:** the condition tests that something is collapsed. It does not test
  whether the Level shown could queue. `apply_outline_level` (`:6464-6489`) queues only where no
  document edit is pending, `make_show_level_edit` accepts the shape, and `push()` accepts it.
  - **A pending date format.**
    1. Six-task outline, Table / Date Format (pending), Sort by Name, Level 1. Both are session
       only, with the pending-edit note.
    2. Clear the sort. The status reads "sort cleared; the level shown stays session only, choose
       it again to queue it".
    3. Level 1 again reads "session only; the file keeps its outline (a table / date format ...
       is pending ...)".
  - **An unmeasured Level or file.** The same happens for Level 3 to 9, and on any file the
    builder refuses. That is every file but the measured pairs. The test reaches Level 3 under
    the sort (`outline_persist_test.cpp:824`) but shows All Subtasks before clearing.
  - **A manual collapse.** Collapse All or a row toggle with no Level chosen gets the same "the
    level shown ... choose it again" text. 33e15c0's message calls this "accurate"; only "session
    only" is.
  - **Silent the other way.** With a non-exclusive structural edit queued (an Insert Task, a
    percent edit), `applied().empty()` is false, so no note is added. Yet a Level still cannot
    queue beside it, because an exclusive kind stands alone. That is the R35fG0-4 state,
    unannounced.
  - No file is at risk: nothing is queued on any of these paths.
  - This is the class of R35fG1-2: advice that the next step refuses.
- **Fix:**
  - Append "the level shown stays session only" whenever rows are collapsed and no Show Level is
    queued.
  - Add ", choose it again to queue it" only when the same Level would queue now: not read-only,
    no pending document edit, `applied()` empty, and `make_show_level_edit` succeeds for the
    shown level. That needs the level to be remembered, or derived from the collapse state.
  - Add a needle for the pending-format case, or for Level 3 then clear, asserting that the
    advice is absent.

### R35gG1-2 (Low): when every changed field fails, the dropped Percent or custom-field change is still not reported

- **Where:** `app_qt/main_window.cpp:6084-6105`. The note is added only in the `!ok && applied`
  branch. The `!ok && !applied` path leaves the last field failure as the status (set in the loop,
  `:6061-6075`), and the trial is dropped there too (it is moved in only under `ok`, `:6106-6110`).
- **Failure or scenario:**
  1. Task Information with a new Percent and a Duration that does not parse, and nothing else
     changed.
  2. The dialog closes, and the status reads "task information: <duration error>".
  3. The Percent is not queued, and nothing says so. Save writes no percent.
  - This is R35fW-3's scenario minus the Name. The brief asked whether the note shows exactly
    when the trial is dropped: it shows only when a cell also committed. This is not a wrong
    file, because the grid shows the old percent.
- **Fix:** when `!ok && !applied && structural_applied`, append the same "; the percent /
  custom-field changes were not queued" (or "nothing was applied"). Add a needle to the 1.2
  trial test with the Name left unchanged.

### R35gG1-3 (Low): 1.1's read-only case never asserts that the session Sort is shown

- **Where:** `app_qt/tests/outline_persist_test.cpp:912-936`.
- **Failure or scenario:**
  - The case is meant to hold "the read-only check before the sort check". Its precondition is a
    shown sort, but the case calls `apply_sort_choice` and then checks only the Level's note.
  - If a later change refused or skipped a session sort on a read-only file, the read-only note
    would still be there, and every check would pass with no sort shown.
  - The negative needle ("so the level is not queued") cannot fail on this fixture anyway.
    `make_show_level_edit` refuses `wave33-fat-slack.mpp`, so the shown-sort branch is
    unreachable there (the builder's record says this needle stayed green under the mutation).
  - The case discriminates through its first needle only.
- **Fix:**
  - After the sort, assert `window.sort_active()`, or the sort's own status ("sort: Name
    descending (session only; saved by Project with unreferenced sectors").
  - Record in the test comment that the negative needle is a guard against a future reorder
    ahead of the builder, not a red this fixture can show.

### R35gG1-4 (Low): R35fW-1 is recorded "fixed", but the state files still say 35f is in progress or unpushed, and leave out the wave review's ids

- **Where:**
  - `docs/START-HERE.md:3-7`: "_Updated 2026-11-10, wave 35f (fix-only) in progress on branch
    `wave35f`", "Waves 34 to 35e are MERGED" and "Nothing of wave 35f is pushed". Line 12 of the
    same file says 35f is CLOSED.
  - `memory/coordinator.md:1` ("waves 34 to 35e merged") and `:8` ("Wave 35f state (closed on
    `wave35f`, ..., NOT pushed)").
  - `docs/START-HERE.md:25`, `memory/coordinator.md:17` and `docs/handoff/plan-waves27-30.md:466`:
    "open from those reviews: R35fG0-* and R35fG1-*".
  - `docs/handoff/plan-waves27-30.md:459-460`: "the Custom Fields dialog's Formula... button
    guards a queued exclusive edit of any kind since W35f-1.8".
  - The 1.12 rows in `wave35g-verification/gates-g1.md` and `wave35g-complete.md` ("fixed").
- **Failure or scenario:**
  - Wave 35f is merged into `main` (`59e7de4`, "merged into main"; the 35g brief's baseline says
    so too). A new session reading START-HERE's first paragraph, or `coordinator.md`'s header,
    gets 35f in progress and unpushed, and 35e as the last merge.
  - The open lists leave out R35fW-1 to -3, which were open from the whole-wave review, the third
    35f review.
  - The plan line puts the guard at the Formula... button "since W35f-1.8". At 35f it was the
    pre-open refusal of the whole dialog (R35fG1-1); W35g-0.1 moved it to the button, except
    for a queued Calculate.
  - The builder left the "NOT pushed" sentences because the review did not name them. But
    R35fW-1's Fix was to set all three files to 35f closed, and START-HERE's own header still
    contradicts its line 12.
- **Fix:**
  - At CLOSE (which updates the three files anyway), rewrite START-HERE's header and
    `coordinator.md:1`/`:8` to "waves 34 to 35f merged (`59e7de4`), wave 35g in progress or
    closed".
  - Add R35fW-1 to -3 beside R35fG0-* and R35fG1-*, and date the plan's 0.2 wording to W35g-0.1.
  - Until then, record R35fW-1 as "partly fixed (the dated headers remain)".

### R35gG1-5 (Low): records: one more misattribution in a commit message, and a code comment that does not match its note

- **Where:**
  - The `33e15c0` message: "Since W35g-0.1 that state is reachable; at 35e the Level was
    refused". The change that made it reachable is W35f-0.1 (R35fG0-4: "0.1 makes this newly
    reachable"). W35g-0.1 is the Custom Fields change.
  - `app_qt/main_window.cpp:6296-6298`: "Nothing is queued, so say that Save writes the file's own
    outline". The note does not say that. It says "the level shown stays session only, choose it
    again to queue it".
- **Failure or scenario:**
  - The message cannot be amended, and unlike `0c35d05`'s slip it is not recorded in
    `gates-g1.md` or `wave35g-complete.md`.
  - The comment describes a message the code does not produce, and a later reader could reason
    from it.
- **Fix:**
  - Add the `33e15c0` slip to the "Found, not fixed" lists beside `0c35d05`'s.
  - Make the comment say what the note says, or make the note say what the comment says (this
    can be folded into R35gG1-1's fix).

## Not findings

- **1.5 masking.** The order inside `document_edit_refusal` changed (Show Level and Sort before
  Calculate). Only one exclusive edit can be queued, so no reason that used to win is now hidden.
  Read-only is refused earlier at every call site.
- **1.4 on the queued-Sort path.** `had_queued_sort` leads to `structural_.undo()` and then a
  rebuild with every row expanded, so the note cannot claim a Level there.
  `clear_session_sort` (`:6368`) has no status line and only tests call it, so leaving it
  unchanged is right.
- **1.6 on the Task Form route.** `apply_task_information(values, "task form")` (`:2994`) changes
  one field at a time, so the count is at most 1 there, and the single-field wording is used.
- **1.3's sentinel under a queued Calculate.** `apply_outline_level(0)` refuses only under a
  queued Sort and changes nothing queued, so it is a clean sentinel there.
- **Lines over 100 columns that remain** in the test files and state files are older lines that
  no finding named. The builder listed the state-file ones under "Found, not fixed".
- **Known, as briefed:**
  - the `check_markers` hit in `spec/writers/evidence/trace-captures-r5.md` (fixed on `main` at
    `052b026`);
  - the misattributed test lines in `0c35d05`'s message, which the builder recorded.
- **Open from the findings this group took:** R35fG0-2 to -9, R35fG1-2 to -5, R35fW-2 and R35fW-3
  are fixed as their Fix paragraphs asked. The exceptions are R35fW-1's remainder (R35gG1-4) and
  the edges in R35gG1-1 and R35gG1-2. R35fG0-1 and R35fG1-1 are G0's and are not reviewed here.
