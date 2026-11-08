# Wave 35 CLOSE review, refusals and shells

Reviewer: an independent read-only pass over `git diff wave34b..wave35`, restricted to the refusals
and the two shells (`app/edit_model.*`, `app/menu_model.*`, `app/main.mm`, `app_qt/main_window.*`,
`app_qt/ribbon_bar.*`, `app_qt/ribbon_pin.tsv`, `app_qt/command_status.h`,
`app_qt/tools/command_backlog.py`, `docs/ui-command-backlog.md`, and the gate tests under `app/` and
`app_qt/tests`). Date: 09 November 2026. Nothing was built, run or edited; every finding below is from
reading source. No Windows, Linux or macOS build or test was executed for this review, so "confirmed"
means confirmed by reading the code path, not by a failing run.

Standards applied: `docs/handoff/standing-rules.md`, `docs/handoff/wave35-brief.md` section 1 (rules 4,
5, 8), the format of `docs/handoff/wave34-verification/close-review.md`.

## Verdict

0 Critical, 2 High, 3 Medium, 5 Low. Do not close wave 35 until RW35-R1 (a likely red Linux and
macOS build) and RW35-R2 (Sort by Name built from unsaved grid renames) are decided.

RW35-R1 is rated High here on the rule 8 ground (a CI break that MSVC hides), which is wider than the
coordinator's "bad save" test; if the coordinator holds to the narrower test, treat it as a Medium that
is still a merge blocker.

## Critical

None.

## High

| Id | Finding | File:line | Direction |
|---|---|---|---|
| RW35-R1 | `shell_implements()` is a `switch` over `app::MenuAction` with no `default`. The wave added five enumerators (`resource_information`, `assign_resources`, `font_bold`, `define_wbs_code`, `calculate_project`) to `app/menu_model.h` and `command_status.h` was not touched (`git diff --stat` shows no change). Under GCC, Clang and AppleClang, `-Wall` includes `-Wswitch`, and `app_qt/CMakeLists.txt` `qt_set_warnings` adds `-Werror` when `CORE_WARNINGS_AS_ERRORS=ON`, which `qt.yml` (line 176) sets. This is the compiler-portability class of brief rule 8: the local MSVC `/W4` does not flag a missing enumerator, so the Windows gates stayed green. A second effect: no `app_qt` source names any of the five actions, so the hidden Qt menu items for them stay disabled with the "not implemented in this port" tooltip while the ribbon controls work, and `command_backlog.py` (which reads this switch) cannot see them as implemented. Confirmed by reading. Not run. | `D:\dev\msproject-mac\app_qt\command_status.h:19-79`; `D:\dev\msproject-mac\app_qt\CMakeLists.txt:45-52`; `D:\dev\msproject-mac\.github\workflows\qt.yml:172-176`; `D:\dev\msproject-mac\app\menu_model.h` (new enumerators) | Add the five cases above the marker comment (return true for the ones the Qt shell now handles, or list them as unimplemented below it if the Qt menu is meant to stay dark), wire or deliberately omit them in `handle_action`, regenerate `docs/ui-command-backlog.md`, and re-run the Linux and macOS Qt builds. Add a test that every `all_menu_actions()` value is either implemented or listed, so the next enumerator cannot slip through. |
| RW35-R2 | Sort by Name is computed from the session plan. `apply_sort_choice` calls `app::make_sort_edit(doc_.plan(), ...)`, and `doc_.plan()` is `sync_plan_from_records(base_, records_)`, which carries unsaved grid edits (Name, Duration, Percent, Summary, Milestone). `sort_plan_order` orders sibling rows by `TaskRow::name` and `duration_ticks` from that plan. `push()` recomputes from the same plan, and the writer (`core/mppwrite/viewstate_sort.cpp`) checks only the base order, not the names. So rename two siblings in the grid, sort by Name, save: the file keeps its stored names (Name edits are session only and not flagged dirty) but gets the order of the renamed names. Project would order the stored names, so a shape Project writes differently reaches a save through the UI. The macOS path is unaffected because `plan.base` has no record edits. The `sort_persist_test` has no pending-record-edit case, so nothing gates this. Confirmed by reading the data flow. Not run. | `D:\dev\msproject-mac\app_qt\main_window.cpp:5970` (`doc_.plan()`); `D:\dev\msproject-mac\app\edit_model.cpp:3183-3260` (`sort_plan_order`), `:4185` (`push()`); `D:\dev\msproject-mac\core\mppwrite\viewstate_sort.cpp` | Build the sort input from the file's own task rows (`doc_.loaded()` or a re-read), or refuse the persisted sort while any record edit is pending ("save or undo the grid edits first") in the builder so both shells inherit it. The same applies to Show Level (`outline_plan_rows`) for Summary and outline edits. Add a Qt gate: rename, sort, assert nothing queued and a specific needle. |

## Medium

| Id | Finding | File:line | Direction |
|---|---|---|---|
| RW35-R3 | Calculate Project pins one measured pair and refuses count, links, summary, manual, constraint, progress and a task calendar, but rule 5 (pin or refuse every user-variable parameter) is not met for: task duration (including zero and milestone), task type and effort-driven, a non-default project calendar, and a project whose two leaves finish together. `calculate_plan_refusal` does not read them, and `app_edits_calculate_tests.cpp` has no refusal case for any of them. The writer's `read_calculate_shape` pins several file-side facts, which limits the damage, but the up-front, test-per-refusal discipline of rule 4 asks for the refusal in the shared builder with a named needle. Suspected, not confirmed: I did not trace whether the writer's dry run already rejects each one. | `D:\dev\msproject-mac\app\edit_model.cpp` (`calculate_plan_refusal`); `D:\dev\msproject-mac\app\app_edits_calculate_tests.cpp` | For each, either show by test that the writer dry run refuses it (then pin the needle in a builder test), or add a builder refusal and a test. |
| RW35-R4 | The `push()` backstop for `calculate_project` checks only the plan refusal, `uid == 0`, the name, and `slack.size() == plan->tasks.size()`. It does not recompute slack, late dates or duration the way `sort_view` and `show_level` recompute their orders. A hand-built edit with arbitrary slack values therefore passes `push()` and depends on the writer's dry run alone. Not reachable from either shell today (both use the builder). | `D:\dev\msproject-mac\app\edit_model.cpp` (`push()` case `calculate_project`) | Recompute and compare in `push()` for parity with the other two kinds, or document that the writer's `validate_calculate` is the backstop and test a hand-built bad edit. |
| RW35-R5 | Needle specificity (check 9). Most new gate needles name the gate (assignment calendar shapes, sort keys, `hides nothing on this outline`, the task-mode combination). Three are generic: `calculate_gate_test` asserts only the prefix "Calculate Project is not implemented in this port" for the linked project, so any refusal passes it; `outline_persist_test` asserts only "is not implemented in this port" for Levels 3, 4 and 9; the reopened-file case does use the specific "not pending". A wrong-gate refusal would pass the first two. | `D:\dev\msproject-mac\app_qt\tests\calculate_gate_test.cpp` (section 1); `D:\dev\msproject-mac\app_qt\tests\outline_persist_test.cpp:107` | Assert the linked-specific text and the level-range text. |

## Low

| Id | Finding | File:line | Direction |
|---|---|---|---|
| RW35-R6 | Project > Schedule "Move Project" is a live no-op prompt (pin 1), whereas the brief says Move stays disabled with the generic tooltip. It writes nothing, so it is a presentation issue, not a save issue. | `D:\dev\msproject-mac\app_qt\ribbon_pin.tsv`; `D:\dev\msproject-mac\app_qt\ribbon_bar.cpp` | Confirm the pin 1 is intended, or disable with the standard tooltip. |
| RW35-R7 | Collapse All, Expand All and Undo leave the session display out of step with a queued Sort or Show Level (the queue and the display are separate). The saved file is still the queued edit alone. | `D:\dev\msproject-mac\app_qt\main_window.cpp` (`apply_outline_level`, `apply_sort_choice`) | Withdraw or note the queued edit when the display changes it, or say so in the status line. |
| RW35-R8 | The prompt hook migration is complete for the assign-resources, resource-add and resource-information gates (all three include `prompt_hook.h` and construct a scoped `gate::PromptAnswerer`; the destructor stops the timer). The same one-look `QTimer::singleShot(0, ...)` pattern remains in `delete_summary_test`, `inspect_gate_test`, `spelling_gate_test`, `save_reopen_test`, `uid_routing_test`, `dialog_gate_test` and others, so the macOS timing flake CI-4 described can recur there. wbs_code and cell_bold gates were reworked in the diff but do not include `prompt_hook.h`; I did not confirm whether they open a prompt. | `D:\dev\msproject-mac\app_qt\tests\prompt_hook.h`; the files listed | Migrate any test that answers a modal prompt to `PromptAnswerer` in the next wave, or record why not. |
| RW35-R9 | The task-mode plus table-format or formula combination is refused at save (cascade), not up front at queue time. It is tested with a specific needle in the app tests and the Qt gate, the queue is kept, no file is written, so there is no bad save. It differs from rule 4's "refused up front" wording. | `D:\dev\msproject-mac\app\edit_model.cpp` (`save_structural_edits` cascade); `D:\dev\msproject-mac\app_qt\tests\task_mode_document_edit_gate_test.cpp:181` | Acceptable as is; note it in the group record. |
| RW35-R10 | macOS `plan.base` and Qt `view_` differ in whether session grid edits are visible to the builders. Not a refusal difference, but it is the root of RW35-R2 and the two shells will answer differently for the same user actions. | `D:\dev\msproject-mac\app\main.mm`; `D:\dev\msproject-mac\app_qt\main_window.cpp` | Resolve with RW35-R2. |

## Checked and fine

- Check 1: Calculate Project is enabled for the measured unlinked two-leaf shape only. The refusal list is in `calculate_plan_refusal` (called by `make_calculate_edit` and by `push()`), so the Qt handler and the macOS handler inherit it; the file side is pinned again in the writer's `read_calculate_shape` and dry run. Respect Links, Auto Schedule and Level All stay 0 in `ribbon_pin.tsv` and carry the exact tooltip "not implemented in this port"; Calculate moves 0 to 1.
- Check 2 (partly): Sort persists for one Name key only (ID, Start, Finish, Priority and two keys are refused with a specific needle each in `sort_persist_test`, which also covers desc and asc replacement and clear withdrawal); Show Level persists for levels 1 and 2 only, refuses other levels, a pending task and "hides nothing"; both are exclusive in the queue and the save cascade. The caveat is RW35-R2.
- Check 3: the resource calendar refusals (W35-0.1) live in `assignment_plan_refusal` (`calendar_base_uid != 1`, stored and extra calendar blocks), shared by the add and removal builders and by `push()`. `assign_resources_gate_test` section 5 runs both shapes against both add and removal with a specific shape needle, and asserts nothing queued and a clean document.
- Check 4: every Qt handler refusal I traced is in a shared builder (Calculate, Sort, Show Level, assignment, Bold via `bold_range_refusal`, task mode); `main.mm` calls the same builders with `plan.base`. The group record's claim holds for these.
- Check 5 (reading only; macOS cannot be compiled here): the five new handlers are declared in the interface, implemented and dispatched in `performMenuAction:` (about `main.mm:3147-3159`); the `MenuAction` dispatch covers every enumerator; `resource_sheet_view` is set in the menu state; Sort and Show Level are not wired on macOS (consistent with the brief). I found no dangling selector, no missing case and no ownership problem in the diff, with the caveat that this is reading, not a build.
- Check 7: `menu_item_enabled` matches the handlers (Resource Information needs the Resource Sheet view; Assign Resources and Font Bold need the task view and a selection; Define WBS Code and Calculate need only a document and no cell edit); `menu_tests.cpp` pins each, plus Font Bold as Cmd-B and one item per action.
- Check 6: the polling hook looks every 5 ms for up to 2 s, handles `QInputDialog`, `QMessageBox` and any other `QDialog` (rejected), records `found` when it gives up, and stops in the destructor so a stale timer cannot answer a later dialog; `prompt_hook_test` covers the late-opening case. The three gates that answered prompts with the old one-look timer are migrated and print the hook's findings on failure.
- Check 8: `ribbon_pin.tsv`, `command_backlog.py` and `docs/ui-command-backlog.md` agree with each other (pin 52 enabled and 62 disabled; backlog 114 with 63 disabled; Calculate marked implemented with a gap text). They are consistent with the enabled ribbon set but, as RW35-R1 shows, not with `shell_implements()` for the menu items.
- Rule 8 scan of the added code: every new function and constant in `edit_model.cpp` (`sort_ascii_letter`, `sort_name_pinned`, `sort_plan_order`, `calculate_plan_refusal`, `outline_plan_rows`, `kAssignMeasuredWorkTicks`, `kAssignMeasuredUnitsPercent`) is used; no `/*` inside a block comment and no string pointer comparison in the diff of the files I read; the `StructuralKind` switches in `push()` and `make_sort_edit` cover the new kinds; `handle_action` and `unmeasured_path_refusal` have a `default`. The one rule 8 failure is RW35-R1.

## Confirmed versus suspected

- Confirmed by reading source (not run): RW35-R1, RW35-R2, RW35-R5, RW35-R6, RW35-R9, and every item in "Checked and fine".
- Suspected only: RW35-R3 (whether the writer's dry run already rejects those parameters was not traced), RW35-R4 (reachable only by a hand-built edit), RW35-R7, RW35-R8 (the wbs_code and cell_bold prompt use was not confirmed), RW35-R10.
- Not reviewed in depth: `core/mppwrite/task_mode.cpp` was only skimmed; `app_edits_sort_tests.cpp`, `app_edits_show_level_tests.cpp` and `app_edits_assignment_tests.cpp` were assessed from the diff and greps, not read line by line.

## Errata, 09 November 2026 (the CLOSE coordinator)

- **RW35-R1 is closed by `513de29`.**  The CLOSE coordinator re-read `app_qt/command_status.h`
  (the switch has no `default` and names none of the five new enumerators) and the other
  switches over `MenuAction` in `app/menu_model.cpp`, `app/main.mm`, `app_qt/main_window.cpp`
  and `app_qt/shell_icons.cpp`: only `command_status.h` was non-exhaustive.  The fix names the five
  below the UNIMPLEMENTED marker (behaviour unchanged).  Red check: MSVC with `/w14062 /WX`
  fails the parent header and accepts the fixed one (`build/w35/rw35r1/`).  The GCC, Clang and
  AppleClang jobs remain the real gate and cannot run here.
- RW35-R2 to RW35-R5 are open and queued for the fix-only `wave35b`.  RW35-R2 was confirmed by
  the reviewer's reading of the data flow and was not re-read by the CLOSE coordinator.
