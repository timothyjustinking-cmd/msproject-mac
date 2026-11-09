# Wave 35d G0 review (e17ddb7..86dbee9)

0C/1H/1M/3L

Read-only review at 86dbee9, from a detached worktree. Nothing built or run. Line numbers are
at 86dbee9.

## Item by item

- **0.1 (R35cW-CI1).** The partial designated initializer is gone
  (`app/app_edits_assignment_tests.cpp:638-647`). I grepped `app/`, `app_qt/`, `tests/` and
  `core/` for both forms, inline `{.x =` / `, .x =` and line-leading `.x =`. No designated
  initializer is left in any `.cpp`, `.h`, `.hpp`, `.mm` or `.inc`. The one new positional
  brace init in the range, `core::view::SortKey{queued.field_id, queued.ascending}`
  (`main_window.cpp:3597`), names both members. CI is still the gate.
- **0.2 (R35cW-CI2).** The About case (`app_qt/tests/backstage_gate_test.cpp:443-494`) matches
  `c235973^` token for token: the QTimer top-level walk, the QMessageBox-by-text check, the
  fallback close and both check texts. Only the comment changed. `check_account` runs before
  any `PromptAnswerer` is built (`:1190` vs `:1193`, `:1196`). `prompt_hook.h` installs
  nothing at include time, and `show_about` is unchanged. I found nothing else that would
  differ on macOS offscreen compared with the last green run.
- **0.3 (R35cG0-1).** Each case reaches only its own branch, and its needle text appears
  nowhere else in `calculate.cpp` (`check_props_pin`, `:278-351`):
  - the header flips (bytes 8 and 12) throw at `:283`;
  - removing the 161st record leaves a tail that ends the walk, so the record count is checked
    at `:318`;
  - the appended mask-matching record throws at `:296` before `kPropsPin[161]` is read.
- **0.4 (R35cG1-1).** The shell's only sort paths are `show_sort_dialog` →
  `apply_sort_choice` (`:6052`), `apply_session_sort` and `clear_session_sort`. Outside the
  tests, nothing else in `app_qt/` calls `doc_.set_sort` or `doc_.clear_sort`. All three now
  clear a `sort_view` on top of the redo list. Checking only the top is enough: `sort_view` is
  exclusive, and `push()` clears `undone_` (`edit_model.cpp:4702`).
  - The two `const&` locals in the re-apply (`main_window.cpp:3589-3590`) are last read before
    `set_sort`, so neither dangles.
  - The record's Show Level conclusion is correct. Redo always runs `after_structural_change`,
    which re-shows a queued level. Under a queued level, `view_change_refusal` refuses Expand
    All, Collapse and the row toggle. A Sort choice made while Level 1 is on the redo list does
    not move the outline. So rows and save cannot diverge on that route.
- **0.5 (R35cG1-2).**
  - Cut (`:5044`) and Paste (`:5130`) now clear the redo list.
  - Fill Down (`:5281`) and `after_cell_commit` (`:3535`) already did.
  - The sheet's own Undo and Redo correctly do not.
  - `result.changed != 0` is safe. `SheetModel::paste` (`core/sheet/sheet.cpp:2461-2580`) is all
    or nothing, and `changed` counts the cells whose value moved, so a paste with
    `changed == 0` leaves the plan alone.
  - Task Information has a gap: see R35dG0-1.

## Findings

### R35dG0-1 (High): a partly applied Task Information or Task Form edit changes the plan and keeps the structural redo list (the R35cG1-2 route through the dialog)

- **Where:** `app_qt/main_window.cpp:5871-5917` (`apply_task_information`).
  - The loop commits Name, then Duration, then Finish, each through `commit_edit`
    (`:5896`).
  - A later field that fails sets `ok = false` (`:5891`, `:5899`) without rolling back the
    fields already committed.
  - `clear_redo()` and `after_cell_change()` run only inside `if (ok)` (`:5906-5915`).
- **Failure**, on the sort control:
  1. Sort by Name descending (queued), then Ctrl+Z. The Sort moves to the redo list and the
     edit gate at `:5712` is open.
  2. Open Task Information (or use the Task Form, `:2907`). Type a new Name and a Duration that
     does not parse.
  3. The Name commits, the Duration is refused, and the redo list is kept.
  4. Ctrl+Y re-queues the Sort with no `push()` and no builder, so `plan_unsaved_edits` never
     runs.
  5. File > Save writes the Sort and the file's own names, not the rename. This is R35cG1-2's
     outcome.
  6. The same route re-queues a withdrawn Show Level.
- **Untested:** no test covers a partial apply.
- **Fix:** clear the redo list whenever any field committed with `outcome.changed`, whatever
  `ok` is. Then add the Undo-first partial-apply case to `qt_sort_persist` and
  `qt_outline_persist`.

### R35dG0-2 (Medium): the record calls 0.5 fixed and says Task Information "already clears"

- **Where:**
  - `docs/handoff/wave35d-verification/gates-g0.md:101-102`: "Task Information (already
    clears)".
  - `docs/handoff/wave35d-verification/gates-g0.md:20`, the 0.5 row: "fixed".
  - `docs/handoff/wave35d-complete.md:14`: "fixed `6458203`".
- **Scenario:** the partial-apply path in R35dG0-1 is a cell path that changes the plan without
  clearing the list. The brief's rule says "fixed" means fully closed, so these lines overstate
  the item.
- **Fix:** record 0.5 as "partly fixed: Task Information partial apply (R35dG0-1)" until that
  path is closed.

### R35dG0-3 (Low): the queued-Sort re-apply treats the writer's field id as a display field id

- **Where:** `app_qt/main_window.cpp:3591-3600`.
- **Problem:** `queued.field_id` is `mppwrite::kSortFieldName` (0x0B40000E) or
  `kSortFieldDuration` (0x0B400507, Task.DurationText), as set by `make_sort_edit` in
  `app/edit_model.cpp:3573`. The display's Duration field is 0x0B40001D, from
  `field_table.gen.cpp:57`.
  - The `shows_queued` comparison (`:3592`) is right for Name only because Name's two ids happen
    to be equal.
  - The key pushed at `:3597` sorts a queued Duration Sort by DurationText, which is the wrong
    field.
- **Status:** dead code today. The shell maps Duration to `SortKeyKind::other`, which is refused
  (`:6108-6109`). It becomes live as soon as a Duration Sort is offered.
- **Fix:** map the id with `app::table_column_field(queued.field_id)` (`app/view_model.cpp:774`)
  for both the comparison and the key.

### R35dG0-4 (Low): two new 0.4 guards have no test that turns red without them

- **Where:**
  - `app_qt/main_window.cpp:6134-6136`, the `apply_session_sort` clear. Every test reaches it
    only after `apply_sort_choice` has already cleared the list (`:6064`). No case calls
    `apply_session_sort` directly after an Undo.
  - `app_qt/main_window.cpp:3584-3603`, the re-apply. The record says it is unreachable while
    the clears stand.
  - `app_qt/tests/sort_persist_test.cpp:309-310`, "the rows show the queued Name descending
    order". This check passes without the re-apply, because Undo keeps the session sort
    (`main_window.cpp:2265-2268`).
- **Context:** both are gate-only or defence-in-depth paths. No user route depends on them
  alone.
- **Fix:** add a direct `apply_session_sort` call after an Undo, asserting that Redo is disabled.
  Either drop the "rows show" check or make it red-capable, for example by clearing the session
  sort through a test seam before Redo.

### R35dG0-5 (Low): two more `check_props_pin` branches still have no test with their own needle

- **Where:** `core/mppwrite/calculate.cpp:325-328` (tail length) and `:340-343` (Task size
  scalar count not 1). `tests/roundtrip_calculate.cpp` has no test with either needle. The tail
  length was reached only as the fallback in the 0.3 red run.
- **Scenario:** if either check is switched off, the tail hash (`:347`) still refuses, but with
  a generic message. A future change to the hash's coverage would leave a tail of the wrong
  length or the wrong scalar count unguarded.
- **Fix:** add two cases: a tail padded by 16 bytes ("tail is"), and a second 0x01000100 scalar
  ("Task size scalars, not one").
