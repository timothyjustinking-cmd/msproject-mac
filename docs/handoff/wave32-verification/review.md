# Wave 32 independent review (`git diff wave31..wave32`, tip 92baa32)

This was a read-only review. The only repo file written is this one. I built
and ran the app and app_qt trees from scratch in new directories
(`build/review-w32-app`, `build/review-w32-qt`, both with
`CORE_WARNINGS_AS_ERRORS=ON`); the build log is in the session scratchpad. I
formed the findings below before reading the record's own CLOSE review
section. The last section says where the two reviews differ.

## Verdict

The three wave-31 High findings are closed for every path that can still reach
a saved file.

- **R31-1** is closed in the model, so the shell entry points cannot get round
  it. Every percent, Mark on Track, Update Project and Task Information or Task
  Form percent edit enters through `StructuralEdits::push`. `push` now refuses
  both orders on every row. `make_outline_edit` repeats the refusal with a
  reason. Start cannot be typed any more, and `save_structural_edits` refuses a
  Start edit beside an automatic outline edit before the first write. Undo and
  redo cannot rebuild the pair, because a successful `push` clears the redo list.
- **R31-2** is closed in both shells. The shared policy makes Start read-only
  for the default and stored tables, so typing, pasting and cutting into Start
  are all refused. Task Information, the Task Form and the Inspector cannot
  change it either. Fill stays disabled. The Qt and macOS save funnels both
  refuse a collected Start edit. `save_structural_edits` refuses a writable
  Start edit as defence in depth. One residual is Low: the non-structural
  `Document::save` still writes Start pins if a caller hands it some.
- **R31-3 and R31-4** are closed in the Qt shell, under a sort and under a
  filter. Range Copy and Cut, multi-row Paste and Shift-click extension are
  disabled and also refused in the handler. Indent, Outdent and Insert are
  refused under a reordering sort, including from the always-enabled ribbon
  buttons. Single-cell edits map through the display rows.

The macOS shell's filter still selects, copies and pastes in file order. Since
W32-0.2, though, no sheet cell other than Start reaches a save: Name, Finish and
Duration are view-only. So that gap no longer writes wrong data into a file.

I found no Critical or High. Two Medium findings remain:

- Task Information can leave a custom-field edit queued behind a percent
  refusal that W32-0.1 widened.
- The macOS filter range path (R31-3's filter half, outside the Qt shell).

The gate claims in `wave32-complete.md` match the logs and the CI runs. The
added lines are clean under the privacy scan.

**Counts: Critical 0 / High 0 / Medium 2 / Low 5.**

## How R31-1 .. R31-4 stand

| finding | status at 92baa32 | evidence |
|---|---|---|
| R31-1 (queued percent/actuals/Start edits bypass the automatic-outline refusal) | **Closed.** (a) Outline after a queued edit: `make_outline_edit` refuses an automatic indent or outdent when `pending_actuals` (any row) or `pending_start_uids` is non-empty. `push` refuses the same from its own scan of `applied_`. (b) Queued edit after an outline: `push(set_percent)` and `push(update_actuals)` refuse while any applied outline edit carries slack, on every row. (c) `save_structural_edits` refuses percent/actuals beside an automatic outline, and Start beside it, before the first write. Every percent-shaped entry point goes through `push`: the % buttons, Mark on Track, Update Project, and Task Information/Task Form via `apply_task_information`. The Qt shell passes the collected Start uids into both calls. A successful `push` clears `undone_`, so redo cannot rebuild the pair. Tests: both orders for percent, a real `update_actuals` and a Start edit, on the committed 3-row automatic control with uid 3 outside the subtree, plus the manual-indent control. The fixture-missing case fails, not SKIPs. | `app/edit_model.cpp:2107-2127, 2291-2307, 2614-2624, 2650-2657, 2832-2840, 2909-2911, 3540-3574`; `app_qt/main_window.cpp:2916-2949`; `app/app_edits_outline_tests.cpp` `check_pending_edit_cross_refusals` |
| R31-2 (typed/pasted Start saves through the disproved writer) | **Closed** in both shells. Start is read-only in `task_column_editable` (stored tables) and `apply_edit_policy` (every task SheetModel the Qt and macOS shells build, including a table switch). Core `paste` refuses the whole block on a read-only target before staging. Cut refuses a selection touching Start. The Task Information Start field is read-only, and `apply_task_information` refuses a changed Start before any other field commits. The Task Form follows the sheet policy; the Inspector is read-only. Fill stays disabled. The Qt `save_document_path`/`save_document` and the macOS `saveDocumentWithPanel` refuse a collected Start. `save_structural_edits` refuses a writable Start. Residual: R32-3 (Low). | `app/view_model.cpp:801-811`; `app/layout_model.cpp:458-472`; `core/sheet/sheet.cpp:2522-2527`; `app_qt/main_window.cpp:3176-3188, 3288-3303, 4411-4427, 4528-4547, 5162-5186`; `app_qt/dialogs.cpp:1095-1104`; `app/main.mm:4033-4053`; `app/edit_model.cpp:3577-3594` |
| R31-3 (Shift-click/Copy/Cut/Paste in file order under Sort or Filter) | **Closed** in the Qt shell for sort and filter: menu enablement plus a handler refusal for each operation. **Not applied** to the macOS shell's filter (R32-2, Medium). That shell has no sort, and its paste no longer reaches a save. A one-row paste into a range selection made before the sort is a Low residual (R32-4). | `app_qt/plan_document.cpp:1053-1066`; `app_qt/main_window.cpp:788-826, 1946-1996, 4360-4369, 4402-4410, 4509-4519`; `app_qt/sheet_table_model.cpp:147-158` (single-cell `setData` maps through `display_rows_`) |
| R31-4 (Indent/Outdent/Insert under a non-ID sort) | **Closed** for sort in the Qt shell: the menu is disabled, and the handlers refuse "clear the sort first". The test clicks the live ribbon controls. An ID-ascending sort that leaves the file order alone is still allowed, by construction. A filter is not refused (R32-5, Low). | `app_qt/main_window.cpp:1989-1993, 2892-2902, 4749-4758`; `app_qt/tests/clipboard_test.cpp` `check_ribbon_refusal` |

## Findings

| id | severity | file:line | finding | evidence | suggested fix |
|---|---|---|---|---|---|
| R32-1 | Medium (pre-existing pattern, widened by W32-0.1) | `app_qt/main_window.cpp:5237-5276` (`apply_task_information`) | **Task Information reports a refusal but leaves part of the dialog queued.** The Custom Fields tab's `set_field`/`clear_field` edits are pushed (5258-5262, return ignored) before the percent edit (5269). Suppose an automatic indent is queued and the user changes a custom field and % complete in one OK. The field edits are queued; then the percent push is refused under W32-0.1's new any-row rule. The status line names only the percent refusal and the function returns false. Neither `after_structural_change` nor `after_cell_change` runs, so the sheet does not show the queued value. Save then writes the custom field. The data is what the user typed for that task, so this is not a wrong-task write. But the refusal message hides a queued save edit, against the code's own rule "a refused dialog queues nothing". Before wave 32 this needed a percent refusal on the moved subtree; now any automatic outline edit triggers it. | Code read: the field loop at 5258-5262 has no rollback when the push at 5269 fails. | Decide the percent push first, using a trial copy (`app::StructuralEdits trial = structural_; trial.push(...)`), or push the percent first and undo the field pushes on refusal. Add a test: queue an automatic indent, then apply Text1 + 40 % on uid 3, and assert `pending_edits().size()` is unchanged. |
| R32-2 | Medium | `app/main.mm:2036-2038` (Shift-click `extend_to`), `:1753` (`paste`), `:741-745` (indent/outdent), `:4935-4947` (filter is a display list only) | **R31-3's filter half is not applied to the macOS shell.** Cmd-F builds `plan.filter_rows` as a display list. The core sheet's visible rows still include the hidden ones, so the following act in file order across filtered-out tasks: a Shift-click range (`sheet_row_at` gives a file row, then `extend_to`), Copy, a multi-row Paste (`SheetModel::paste` walks `visible_row(ordinal)`) and Indent/Outdent (relative to the file sibling). Since W32-0.2 the only editable cells are Name, Finish and Duration, which no save writes. So the effect is in-session values written into rows the user cannot see, not a saved file. That is why this is not High. Indent and Outdent under the filter save a measured shape, but relative to a row the user may not see. | Code read; the macOS shell is not compiled on Windows, and no test drives its filter. | Mirror the Qt refusals when `plan.filter_active`: refuse Shift-click extension, a range Copy and a multi-row Paste. Consider Indent/Outdent too, or record the gap in the backlog. |
| R32-3 | Low | `app/document.cpp:405-440` (`save_start_edits`), `:545-620` (`Document::save` start branch) | **The defence in depth covers only the structural save.** The brief asks the edit model to keep refusing a start edit. `save_structural_edits` does. `app::Document::save(start_edits, ...)` and `app::save_start_edits` still route writable starts to `mppwrite::save_date_edits` without a refusal, and `document_tests` deliberately exercises that write. Both shells refuse a non-empty Start list before calling `Document::save`, so no shell path reaches it today. | `grep -n save_date_edits app/document.cpp` gives 433 and 611, both unguarded. | Refuse writable starts in `Document::save` with the same decision-9 text, and keep the writer reachable only from its own test entry. |
| R32-4 | Low | `app_qt/main_window.cpp:1957-1987, 4509-4519`; `core/sheet/sheet.cpp:2583-2590` | Under a sort, a one-row paste is still enabled when the core selection is a range. That range can come from a Shift-click made before the sort (applying a sort keeps the selection) or from Select All. `SheetModel::paste(block, mode)` writes at `selection_range().row_begin`, the first FILE row of the range, not the top row the user sees. Only one row is written, and only view-only cells, so the impact is small. | Code read: `set_sort` and `after_model_change` do not collapse the selection; the paste guard checks `clipboard_.rows` only. | Under a reordered display, also require `selected_cells().size() == 1` for Paste, or collapse the selection to the anchor when a sort or filter is applied. |
| R32-5 | Low | `app_qt/main_window.cpp:2892-2902, 4749-4758` | Indent, Outdent and Insert check `sort_reorders_rows()` only. Under an active filter, the row the indent attaches to (the file's previous sibling), and the insert position, can be hidden. The saved shape is a measured one; the target is not the row displayed above. The brief scoped R31-4 to sort, and Project's own behaviour under a filter was not measured. | Code read. | Refuse under any filter as well ("clear the filter first"), or measure Project's filtered Indent once. |
| R32-6 | Low | `app_qt/tests/clipboard_test.cpp:300-490` | **Test strength.** (a) The refused three-row paste is a Duration block, and the core matches columns by field, so it would land in Duration, not Name. "every Name cell unchanged" cannot fail; only the status-text check proves the refusal (the record's L1). (b) The filter case uses a filter that keeps every row in file order, so display row k is file row k. The single-cell mapping and Shift-click checks are vacuous for a filter that hides rows, the case R31-3 is about. (c) Nothing proves that an unsorted view, or an ID-ascending sort, keeps range Copy/Cut/Paste and Indent/Outdent enabled. The over-refusal guard `sort_reorders_rows` is untested; only "clearing the sort enables Insert again" is checked. | Code read. | (a) Assert the Duration cells. (b) Add a filter that hides a middle row, and check the mapping and a refused 2-row paste across the gap. (c) Apply ID ascending and assert the range actions stay enabled. |
| R32-7 | Low (performance, timing unverified) | `app_qt/plan_document.cpp:1053-1059`; `app/sort_model.cpp:66` | `sort_reorders_rows()` copies the whole RecordSet and re-sorts it on every call. While a sort is active, that is every `update_menu_state()` (every click and status refresh) plus each Shift-click, Copy, Cut, Paste, Indent and Insert check, on top of the sort the display already does. On a corpus-sized file this may make clicking lag under a sort. | Code read. | Compute the flag once in `rebuild_views`/`apply_filter_to_sheet_model` and cache it beside `display_rows()`. |

## Checks that passed (spot-checked)

- **Builds and tests at 92baa32** (my own fresh trees, warnings as errors):
  app configure/build exit 0, ctest 25/25; app_qt configure/build/ctest
  QT_RESULT_PLACEHOLDER.
- **No SKIP-and-pass added.** `git diff wave31..wave32 -- app app_qt core tests | grep '^+.*SKIP'` finds only a comment. The new W32-0.1 test checks the two committed controls exist and fails if they are missing.
- **Tests exercise the claimed paths.** `check_pending_edit_cross_refusals` puts the queued edit on uid 3, outside the moved subtree and not its anchor. It proves each refusal comes from the new rule: "the same date update is accepted without the outline edit", and the manual control is accepted beside a Start. Its save test asserts no target file. `qt_save_reopen` asserts a refused Start writes no `.mpp` and that a Text1 edit still persists. `qt_clipboard` forces the disabled actions through `force_trigger` and clicks the live ribbon controls, so the handler refusals are tested, not only the enablement.
- **No regressions found in measured paths.**
  - Single-cell Name/Finish/Duration edits stay editable in unsorted views.
  - The percent, Mark on Track, Update, outline, link, task-mode, insert/delete and custom-field paths are unchanged, apart from the new exclusivity.
  - An ID-ascending sort over an ID-ordered file is not treated as active: the sorted list equals the visible list.
  - `collect_start_edits` stays empty after structural edits: `StructuralEdits::apply` sets no file row's start, and `without_structural_edits` takes the shown rows. So the new Start refusal in the save funnels does not block measured structural saves.
- **Other Start entry points.**
  - The Task Inspector fields are read-only (`task_inspector.cpp:25`).
  - The Gantt canvas has no mouse editing.
  - The header-menu Copy/Cut/Paste go through the same guarded handlers.
  - The macOS cell editor goes through `begin_cell_edit`, and `apply_edit_policy` is applied in `build_editor` (`main.mm:589`).
  - `fill_down` stays unreachable (disabled control; any staged Start would be refused at save anyway).
- **CI-only compile hazards.** Every new helper and member is used: `push_refusal`, `has_automatic_outline_edit`, `sort_reorders_rows`, `display_order_differs_from_file`, `clicked_cell`, and the `apply_session_*`/`clear_session_*` gate entry points. `smoke.cpp`'s `start_column` locals are still read. The W32-C4 `int` fix is in place. CI on the pushed code tree `7727760` is green: `qt-shell` run 40456047971 (windows/linux/macos) and `ci` run 40456051113 (windows-msvc, ubuntu-gcc), both on head SHA 77277605. `92baa32` changes docs only (`git diff --stat 7727760..92baa32`: four Markdown files).
- **Gate claims match the logs.**
  - `build/w32/W32-CLOSE-fresh-gates.log` (tree 7727760) has exactly 22 `exit: 0` stage lines and none non-zero, with core 52/52, plan_recalc 512/266, engine 53 checks with 8 recorded (7 pass, 1 expected fail), reader 1136/456, M2 332/332 · 268/268 · 64/64, app 25/25, app_qt 37/37, fixtures OK 139/173, backlog 114/114 with 0 mismatches, scans 0/0 and CRLF 373 → 372 with 0 added.
  - `W32-CLOSE-fresh-app-edits-outline.log` shows 3230 checks.
  - `W32-0.1-app_edits_outline-direct.log` shows 3225 checks, as the W32-0.1 commit quotes.
  - The per-commit ctest logs show 25/25 and 37/37 where quoted.
  - `W32-CLOSE-push-preflight-92baa32.log` ends "ALL PREFLIGHT CHECKS OK".
  - The record itself admits that `W32-G0-guard.log` is empty (0 bytes, confirmed).
- **Privacy.**
  - I scanned the added diff lines and `git log wave31..wave32` messages for the operator's name and initials, other personal names, company names and folder paths, drive/UNC paths, profile paths and e-mail addresses: no hit.
  - The only drive paths added are the repository root and the neutral `C:\w32\` root (both in the brief), plus the record's references to them.
  - No fixture was added.
  - Commit author metadata is the repository's git identity, not added content.

## Where this review and the record's CLOSE review differ

I read the record after writing the findings above.

The record's review (0 Critical / 1 High / 4 Medium / 6 Low) found the
`-Werror` CI hazard (H1), the missing `update_actuals` order (M2), the
macOS funnel (M4), the ribbon bypass (M3) and the unreachable save-level
check (M1). All five are fixed, and I confirm the fixes in the code and the
logs. Its L1 is my R32-6(a), and I agree with its L4 (the filter wording) and
L6.

It did not report:

- R32-1: Task Information queues the custom-field edits before a refused
  percent edit, a partial queue behind a refusal message.
- R32-2: the macOS shell's filter range path. Its scope note covers the macOS
  funnel and the indent/outdent pending lists, not filtered selection or paste.
- R32-3: `Document::save` still writes Start pins without a model-level
  refusal.
- R32-4, R32-5 and R32-7: a one-row paste into a range selection made before a
  sort, structural commands under a filter, and the per-call re-sort cost.
- R32-6(b) and (c): the filter test does not hide a row, and there is no
  ID-ascending enablement test.

None of these reaches a wrong write in a saved file. So I agree with the
record that nothing Critical or High remains, and that the stack can merge on
this evidence.
