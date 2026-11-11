# Wave 35g, group G0: independent review

Range `1735250..fe61064` on `wave35g` (commits `284c3cb` W35g-0.1, `24a5c1d` W35g-0.2, `fe61064`
the group record). Read with `git show` and `git diff` only; nothing built, run or checked out.
Line numbers are at `fe61064`.

## What holds

- **The refusal still stops every route to a formula beside a queued exclusive edit.**
  `show_custom_fields` (`app_qt/main_window.cpp:4237`) has only one route that queues: OK with
  `dialog.has_formula_edit()`, then `apply_custom_formula` (`:4305`). `has_formula_edit_` is set
  only in `CustomFieldsDialog::edit_formula` (`project_dialogs_qt.cpp:844-868`), and only when the
  editor returns a non-null, non-empty string. The callback (`main_window.cpp:4267-4284`) returns a
  null `QString` whenever `document_edit_refusal` is non-empty, before `QInputDialog::getText`, so
  a refused Formula... cannot set it.
  - **Several Formula... uses in one session; refused then accepted.** `formula_refusal` is
    overwritten on each use, but it depends only on `structural_`, which cannot change while the
    application-modal dialog (and its nested input dialog) is up. So in one session every use is
    refused or none is. "Accepted, then refused" and "refused, then accepted" cannot happen.
  - **Accept or cancel after a refused Formula....** Both reach the `formula_refusal` branch at
    `:4286` before the cancel and `has_formula_edit` branches. It sets the reason as an error and
    returns. Nothing is queued, `clear_redo` is not called, and the title and menus are untouched.
    The reason is not overwritten by "custom fields cancelled".
  - **A dialog opened with nothing exclusive queued** behaves as before: Formula... accepted, OK,
    `apply_custom_formula`, `clear_redo` (W35f-1.5). `qt_calculate_gate`'s redo case
    (`calculate_gate_test.cpp:606-`) still drives that path.
- **W35f-0.2's queued-Calculate guard is intact.** `push()` keeps an exclusive kind alone in the
  queue (`app/edit_model.cpp:4097-4107`), so `queued_edit_is(applied, calculate_project)` (top
  only) is true exactly when a Calculate is queued. `refuse_document_edit` then gives the Calculate
  wording through `data_change_refusal`. The `qt_calculate_gate` surface case
  (`calculate_gate_test.cpp:474-499`) is unchanged in the range. It closes any modal it sees, so
  dropping the pre-open guard would turn it red rather than hang it.
- **The new 0.1 tests go red without their guards**, as the record's two red runs show and as
  the assertions bear out:
  - At the parent, the dialog never opens: three `dialog_seen` or "cancelled" FAILs per file,
    6 in all.
  - With the callback refusal disabled, the input dialog is seen and filled, and the formula is
    queued. That trips `!input_seen` and the status needle in both files, plus
    `pending_edits().empty() && !document_dirty()` in `qt_sort_persist`: 5 in all.

  The counts match the record.
- **Second exclusive kind.** `qt_task_mode_document_edit_gate` uses a queued Manually Schedule,
  as the brief asks. Its status needle is the generic wording, which only a non-Calculate,
  non-view exclusive kind produces.
- **The W35g-0.2 record correction is right.**
  - In `apply_sort_choice` (`main_window.cpp:6232-6318`), only a single key with
    `make_sort_edit` accepting it queues. The builder refuses every key but Name, so Start, Finish,
    ID and multi-key sorts are session only on the five-task file.
  - The rewritten paragraph (`wave35e-verification/gates-g1.md:56-63`) matches the review's Fix
    text. The `wave35e-complete.md` 1.1 row names the new test.
  - No other record copied the old sentence. The only hits are the reviews and records that
    quote it as the finding.
- **The 0.2 test is real.** It runs Level 1 (asserted as one `show_level`), Ctrl+Z (Redo
  enabled), then Start descending (asserted session only, Redo disabled), then Ctrl+Y (nothing
  queued) (`sort_persist_test.cpp:573-606`). Mutating `redo_top_is_view_edit` is the right
  point. The builder's note that narrowing only the `apply_sort_choice` call site stays green is
  correct, because `apply_session_sort` (`:6330`) calls the same helper.
- **Hygiene.**
  - Each of the three commits carries exactly one
    `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35g)`.
  - Each code or record commit names one finding id. The group-record commit names both, as
    group records do.
  - No CR bytes appear in the diff, and `git diff --check` is clean.
  - No names, personal paths or client strings appear in the added lines.

## Findings

### R35gG0-1 (Low): the "refused, then cancelled" status is claimed but not tested

- **Where:** `app_qt/main_window.cpp:4285-4290`; the tests at `app_qt/tests/sort_persist_test.cpp:145-172`
  and `app_qt/tests/task_mode_document_edit_gate_test.cpp:152-179` (`run_custom_fields`).
- **Failure or scenario:** `gates-g0.md` (0.1) says the reason stays on the status line "even if
  the dialog is then cancelled", and the code orders the `formula_refusal` branch before the
  cancel branch for that reason. `run_custom_fields(window, true)` always clicks OK after
  Formula..., and the cancel run never uses Formula.... So swapping the two branches would not be
  caught. Neither would replacing `dialog_accepted` with a check that skips the refusal on cancel.
  Either change leaves "custom fields cancelled" on the status line after a refusal the user saw
  happen. The gate stays green.
- **Fix:** give `run_custom_fields` a third mode: Formula..., then `reject()`. In one of the two
  files, assert that the status line still names the refusal and does not read "custom fields
  cancelled", and that the queue is unchanged. Mutate the branch order once to see it red.

### R35gG0-2 (Low): the mode-edit case does not check that no formula is pending, and one sort needle claims more than it checks

- **Where:** `app_qt/tests/task_mode_document_edit_gate_test.cpp:276-282`;
  `app_qt/tests/sort_persist_test.cpp:284-285`.
- **Failure or scenario:**
  - A custom-field formula is queued on `app::Document`, not on `structural_`. In the task-mode
    case, "a refused formula leaves the mode edit queued once" checks only
    `pending_edits().size() == 1`. `document_dirty()` is already true from the mode edit, and
    nothing checks that no document edit is pending. The commit message says the tests show "no
    formula is queued", but the task-mode case relies only on `!input_seen` and the status needle.
    A regression that queued a formula without the input dialog would pass it. Examples: an
    editor returning `current`, or a refactor applying the formula after `exec()`.
  - In `qt_sort_persist`, the needle text "and queues nothing beside it" sits on a
    `queued_sort != nullptr` check, which cannot see a document edit. The real check is the later
    one at `:291-293`, which holds.
- **Fix:**
  - In the task-mode case, after the refused run, Ctrl+Z the mode edit and assert
    `pending_edits().empty() && !window.document_dirty()`, as the sort case does after clearing
    the sort.
  - Reword the `:285` needle to "a refused formula leaves the sort queued".

### R35gG0-3 (Low): the input-dialog poller outlives `run_custom_fields` and holds a pointer into its frame

- **Where:** `app_qt/tests/sort_persist_test.cpp:126-136` and `:162`, the same at
  `task_mode_document_edit_gate_test.cpp:133-143` and `:169`.
- **Failure or scenario:**
  - In the green case no input dialog opens, so `accept_input_dialog` keeps re-arming 10 ms timers
    (up to 6) after `show_custom_fields` returns. It holds `&run.input_seen`, a member of the local
    `run`. NRVO is not guaranteed, so that pointer can dangle once `run_custom_fields` returns.
  - The tail fires during whatever `processEvents` the next blocks run. If any later block opens
    a `QInputDialog` within that window, the poller would type `[Duration]/240` into it, accept
    it, and write through the dead pointer. Examples: Insert Task (`dialogs.cpp:1021`), the rename
    and WBS prompts (`:1041`, `:1056`), or `getItem` (`:1097`).
  - `qt_sort_persist` has no such later block today, and the task-mode case ends the program. So
    this is latent, but it is a trap for the next block added to either file.
- **Fix:** give the poller a lifetime it cannot outlive.
  - Option 1: hold `seen` in a `std::shared_ptr<bool>` and stop when the CustomFieldsDialog is
    gone.
  - Option 2: drain the chain before returning. For example, have `run_custom_fields` keep a
    `QPointer` or flag that the poller checks, and clear it after `action->trigger()`.
  - Option 3: install the input-dialog driver only once `formula_button()->click()` is known to
    open one.

### R35gG0-4 (Low): a refused Formula... is a silent dead button while the dialog is open

- **Where:** `app_qt/main_window.cpp:4267-4272` (the callback returns null), with the reason
  shown only at `:4286-4290` after `exec()` returns.
- **Failure or scenario:**
  1. With a Sort queued, the user opens Custom Fields..., selects Text1 and presses Formula....
  2. Nothing happens. The status line behind the modal dialog is not updated until the dialog
     closes.
  3. The user can press it repeatedly, then OK or Cancel, and only then learn why.

  The refusal is correct, and the mechanism is the one the R35fG1-1 Fix named. But it is the
  one surface in the port where a refusal gives no feedback at the point of use. The other
  document-edit refusals all report at once on the status line, because they refuse before a
  dialog opens. This is rated Low because nothing wrong is queued or saved. If the coordinator
  counts it as visible wrong behaviour, it is Medium.
- **Fix:** do one of the following.
  - Show the reason inside the dialog, for example `QMessageBox::information(&dialog, ...)` in
    the callback before returning null.
  - Disable the Formula... button at open beside a queued exclusive edit, with the reason as its
    tooltip. This keeps the post-exec status line as well. If the button is disabled, the tests
    must then assert that it is disabled instead of clicking it.

## Not findings

- **"Only an accepted Formula... is refused" (brief) versus "Formula... is refused before the
  input dialog" (built).** The brief and the R35fG1-1 Fix both allow the callback placement.
  Refusing before the text is typed loses nothing, because the state cannot change inside the
  session. The brief's "an accepted formula edit ... is still refused" test is met by the
  red2 mutation: without the refusal the formula is accepted and queued, and the test goes red.
- **Calculate is checked at the top of the queue only, not with `has_exclusive_edit`.** These are
  equivalent, because an exclusive kind is always the queue's only edit (`edit_model.cpp:4097-4103`).
- **A queued Calculate still stops the dialog opening to look.** The brief says to keep
  W35f-0.2's guard. The builder records this under Found, not fixed, correctly.
- **No re-check of `document_edit_refusal` after `exec()` when `has_formula_edit()`.** It would be
  belt and braces only: the queue cannot change under the modal dialog, and the pre-1.8 code had
  no such re-check either.
- **The refusal's Sort and Show Level advice wording ("clear the sort or choose another sort
  first").** R35fG1-2 covers it in G1.
- **Per-commit gates were run on incrementally built trees, with no fresh-tree group set in the
  builder record.** This matches wave 35f's practice and the brief's section 3: the coordinator
  runs fresh-tree gates after each group marker, and CLOSE reruns them.
- **The group-record commit `fe61064` names both finding ids.** It is a records-only group
  commit, as in earlier waves. The code and record items are one id each.
- **The `check_markers` hit in `spec/writers/evidence/trace-captures-r5.md`.** It is known, came
  from `5863054`, and was fixed on main as `052b026`.
