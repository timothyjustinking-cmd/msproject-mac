0C/0H/1M/4L

# Wave 35f, group G1: independent review (79d24be..135d253)

Read-only. Everything below was read with `git show 135d253:<path>` and
`git diff 79d24be..135d253`; the gate logs named in the records were read under `build/w35f/`.
Nothing was built or run. Line numbers are at 135d253.

## Answers to the questions asked

1. **1.8 coverage.** Every exclusive kind in `is_exclusive_kind` (`app/edit_model.cpp:105-116`:
   task mode, resource rename, add, assign, unassign, Bold, WBS code, Calculate, Sort, Show Level)
   is covered in the Qt shell, and that set matches the save-time refusals one for one
   (`app/edit_model.cpp:5500-5800`, each `document_edits.any()` branch). Every builder entry that
   pushes one of those kinds has the check: Manually Schedule (`main_window.cpp:3190`), Resource
   Information (`:3244`), Add Resources (`:3284`), Assign/Unassign (`:3335`), Font Bold (`:3425`),
   Define WBS Code (`:3467`) and Calculate (`:3502`, its own wording). Every route to those
   functions (menu `:2193-2211`, ribbon `:2499-2596`, the Mode menu `:3545`) goes through them.
   Sort (`:6261`) and Show Level (`:6419`) fall back to session only beside a pending document
   edit. That is right, because the save would refuse them and the display still changes, and the
   note says so. All three document-edit surfaces (`:4249`, `:4948`, `:5001`) refuse beside any
   queued exclusive edit. The only queue sites for a document edit are those three
   (`request_view_state_edits` `:4976`, `apply_custom_formula` `:4287`, `:5027`). The new code
   refuses nothing the save would accept, with one exception: the Custom Fields dialog is refused
   even to look at (R35fG1-1). The Sort and Show Level wording on the document-edit refusal gives
   advice that does not hold (R35fG1-2).
2. **1.5, the redo routes.** The redo list cannot hold an exclusive edit while a document edit is
   pending. It is cleared when a document edit queues (`:4298`, `:4984`, `:5036`; AppKit `:4322`,
   `:4448`), and no exclusive edit can then be pushed (all the entries above refuse, and Sort and
   Level only show for the session). `push()` lets an exclusive edit stand alone, so nothing else
   can reach the list beside one. The structural Redo (`:2370`; AppKit `:5000`) has no route left
   that re-queues an exclusive edit beside a pending document edit. A side effect, not a finding:
   the clear also drops a non-exclusive edit (an Insert Task, say) from the redo list.
3. **1.1, the renderings.** `task_information_values` (`:5778-5836`) builds Name, Duration,
   Start, Finish and Percent with `app::cell_text(*doc_.sheet(), row, column, prefs_)`, the same
   function the skip now uses (`:6028`), with the same sheet, row and preferences. The dialog
   copies the texts through `QLineEdit` unchanged (`dialogs.cpp:1138-1191`, `:1309-1315`).
   `commit_task_form_field` builds them the same way (`:2959-2979`). `commit_edit` does not
   recompute other columns (`core/sheet/sheet.cpp:1980-2045`), so the Duration commit cannot make
   the Finish compare fail inside the loop. Percent and Start were already compared under
   `prefs_`. Custom fields do not use this skip: `make_task_field_edits` handles them, and G1 did
   not change it. I found no field that is still re-committed when untouched.
4. **1.10, AppKit.** I found nothing that AppleClang `-Wall -Wextra -Wpedantic -Werror` (C++20 /
   ObjC++20, `app/CMakeLists.txt`) would reject:
   - The two helpers are in the file's anonymous namespace (`main.mm:122-2062`) and are both
     used.
   - The `if (const std::string reason = ...; ...)` form was already used in this file at
     79d24be.
   - `return reason;` returns a const local, so `-Wreturn-std-move` does not apply.
   - No new integer conversion, sign compare or selector is introduced.
   - There is no name clash with the Qt members.

   It mirrors the Qt checks: the same predicates over the same model, Calculate's 1.6 text word
   for word, and the redo clears placed after a successful queue. The shell's only exclusive
   builders are add, rename, assign/unassign, Bold (`make_cell_bold_edit`), WBS and Calculate
   (`main.mm:3307-3689`), all covered. The record's claim that the shell has no Sort, Show Level
   or Manually Schedule command checks out. `app-macos.yml` runs on push to any branch, as the
   records say.
5. **1.11 and 1.12, the seams.** Point 7 (`calculate.cpp:1121`) comes after `work.save(scratch)`,
   the re-read and the compare, and before `reread.save(path)`. Its throw lands in the
   `catch (...)` that removes the scratch file. With the seam at 0, `0 == 5 + 2` is false, so a
   normal build is unaffected: the control flow and the bytes are unchanged, and nothing in
   `app/` or `app_qt/` calls either setter. `ProbeSeamReset` (`tests/roundtrip_calculate.cpp:271`)
   is declared before each set (`:1507`, `:1537`). The guard test (`:1556-1583`) arms the seam,
   throws, and then needs a successful save. The red logs agree with the commit messages.
6. **Records.** Nothing is called closed. Every item is "fixed", "recorded" or "fixed in source;
   `macos-appkit` is its gate". `6fa4c19`'s description in 1.15 matches its diff. The G1-final
   gates exist and are green (`build/w35f/135d253-W35f-G1-final-gates.status`: 65/65, 34/34,
   52/52, scans 0). The 1.8 coverage limit understates what is untested (R35fG1-3).
7. **Tests and hygiene.** The 1.1, 1.3, 1.5, 1.6, 1.7, 1.11 and 1.12 guards each have a red run,
   and 1.2 is a test-only item with a mutation red. Each 1.8 red log has 4 FAILs. Some 1.8 sites
   have no test that would go red (R35fG1-3). All 18 commits carry the trailer
   `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35f)`. The added lines contain no
   client names, people's names or personal paths. Hygiene is R35fG1-5.

## Findings

### R35fG1-1 (Medium): Project > Custom Fields... cannot even be opened beside any queued exclusive edit

- **Where:** `app_qt/main_window.cpp:4249`. The `refuse_document_edit("a custom-field formula
  edit")` call runs before `CustomFieldsDialog dialog(...)` at `:4258`.
- **What is wrong:** the Custom Fields dialog shows the field list, each field's kind and the
  file's stored formulas (`project_dialogs_qt.cpp:823-842`), and it is cancellable. It queues a
  document edit only if Formula... is used and accepted (`has_formula_edit()`, `:4280`). Before
  1.8 the pre-open refusal fired only beside a queued Calculate (W35f-0.2). Now it fires beside
  all ten exclusive kinds. A queued Sort is one of them, and a one-key Name sort chosen in the
  Sort dialog queues one.
- **Scenario:**
  1. Sort by Name descending (the status reads "queued; File > Save writes it").
  2. Project > Custom Fields...: the dialog does not open, and the status reads "sort: a Sort is
     queued and File > Save would write it, not a custom-field formula edit; ...".
  3. The user cannot look at the stored formulas, although nothing would be queued and the save
     would accept the session as it is.
- **Fix:** move the refusal into the `set_formula_editor` callback (return a null QString with
  the reason before the nested input dialog opens), or check after `exec()` when
  `has_formula_edit()`, so that opening the dialog to look stays free.

### R35fG1-2 (Low): the Sort and Show Level wording on a document-edit refusal advises a step that is refused again

- **Where:** `main_window.cpp:1668-1677` (`document_edit_refusal` reuses `data_change_refusal`,
  which ends in `view_change_refusal`, `:1630-1640`). The advice is "choose All Subtasks or
  another level first" (`:1634`) and "clear the sort or choose another sort first" (`:1638`).
- **Scenario:** Level 1 is queued. Table / Date Format is refused with "...; choose All Subtasks
  or another level first". The user follows it and chooses Level 2, which queues another Show
  Level (`:6437`). Table / Date Format is refused again. A Sort works the same way: choosing
  another one-key Name sort re-queues a Sort. This is the same class as R35eG2-4, which 1.6
  fixed for Calculate. The words "would write it, not a table / date format edit" also suggest
  that Save would drop the format, when in fact it refuses both together.
- **Fix:** give `document_edit_refusal` its own text for `sort_view` and `show_level` (for
  example "...; undo the queued Sort (or clear the sort) / choose All Subtasks first"). Keep the
  `view_change_refusal` text for the view changes it was written for.

### R35fG1-3 (Low): more of 1.8 is untested than the records' coverage limit says

- **Where:**
  - The widened refusals at Custom Field Formula (`main_window.cpp:5001`) and Custom Fields
    (`:4249`).
  - The five builder entries `:3244`, `:3284`, `:3335`, `:3425` and `:3467`.
  - Records: `gates-g1.md` "Items worth reading" (1.8) and `spec/writers/calculate.md` (the "other
    exclusive kinds" paragraph).
- **What is wrong:** the 1.8 tests drive only Table / Date Format beside a non-Calculate kind
  (`queue_date_format` in all three tests). Reverting `:5001` or `:4249` to the old Calculate-only
  check (`queued_edit_is(..., calculate_project) && refuse_data_change(...)`) would keep every
  test green, because the `qt_calculate_gate` cases for those surfaces queue a Calculate. The
  records name only the five builder entries as covered by the shared helper. The save-time
  refusal still stands behind every one of these, so no file is at risk.
- **Fix:** add one case per surface (a queued Sort, then Custom Field Formula and Custom Fields)
  and one loop over the five builders with a pending date format. Failing that, add the two
  dialog surfaces to the recorded coverage limit.

### R35fG1-4 (Low): the 1.3 partial-apply status says "Undo takes it back" when one Undo takes back only the last field

- **Where:** `main_window.cpp:6062`. Each field commits with `commit_edit(false)`, and each
  commit is its own undo step (`core/sheet/sheet.cpp:2039`, `push_undo_step` per commit).
- **Scenario:** Task Information with a new Name, a Duration that does not parse and a new
  Finish. The loop goes on after the Duration failure (`:6044-6046`), so Name and Finish both
  commit. The status reads "...; Name, Finish applied (Undo takes it back)". One Ctrl+Z restores
  the Finish only, and the Name stays.
- **Fix:** word it by count ("Name, Finish applied; Undo takes back one field per step"), or
  coalesce the apply's commits into one undo step.

### R35fG1-5 (Low): hygiene

- 1.16 rewrapped the five files the finding named, but G1 added five new lines over 100 columns
  elsewhere: `app_qt/tests/outline_persist_test.cpp:859` (105) and `:872` (104), and
  `app_qt/tests/sort_persist_test.cpp:215` (101), `:231` (107) and `:238` (105).
- `app_qt/tests/task_mode_document_edit_gate_test.cpp:147,152`: the `target` save path is still
  built and removed, but nothing saves to it now that 1.8 removed the save step. It is dead
  setup (not a warning, since QString is non-trivial).
- **Fix:** rewrap the five lines, and drop `target` and its `remove`.

## Not findings

- 1.4: the only way to clear a sort while a Show Level is on the redo list is to clear a sort
  that is not shown. Setting a sort ends a Level on the redo list, and a Level cannot queue
  under a shown sort. The change is harmless, and the record describes it correctly.
- 1.9: the record matches the code at 135d253 (`after_structural_change`, and the Sort dialog's
  `SortKeyKind::other` mapping at `:6280-6287`).
- `pending_document_edit_refusal` says "save it". For the five non-Calculate builders, I found
  no writer precondition that a file saved with a formula or date format would trip. The
  Calculate Props pin, which 1.6 handles, is the one known case.
- The commit author identity is the repository's configured one. No added line names a person,
  a client or a personal path.
