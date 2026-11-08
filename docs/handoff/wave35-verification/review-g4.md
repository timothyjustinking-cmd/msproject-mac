# Review of wave 35 group G4 (commit 1cd37a9, "the macOS shell")

Reviewer: independent, adversarial, read-only.  Read with `git show 1cd37a9:<path>` and
`git diff 2468736 1cd37a9`.  Nothing was built, edited, checked out or committed.  `main.mm`
was not compiled (it cannot be compiled on this machine); the compile judgements below are
from reading the source.

Note on the record: at 1cd37a9 `docs/handoff/wave35-complete.md` has no "## Group G4"
section.  The section was added in the next commit, 4e3ba80, and was read from there.

## Findings

### R35G4-1 (High): the macOS resource pop-ups can target a resource other than the one picked

- **Where:** `app/main.mm:3244-3250` with `3275-3281` (Resource Information), and
  `app/main.mm:3366-3373` with `3391-3397` (Assign Resources, the assign direction).
- **What happens:** both handlers fill an `NSPopUpButton` with
  `[popup addItemWithTitle:to_ns(resource.name)]`, push each uid onto a parallel
  `uids` vector, and then map `[popup indexOfSelectedItem]` back through `uids`.  Apple's
  documented behaviour for `-[NSPopUpButton addItemWithTitle:]` is that when an item with
  the same title already exists, the existing item is removed and the new one is appended.
  Project allows two resources to share a name, and it allows blank names, which collide
  with each other as `@""`.  When that happens the pop-up has fewer items than `uids`, and
  every index from the first duplicate onwards points at the wrong uid.
- **Scenario:** resources uid 1 "A", uid 2 "B", uid 3 "A", uid 4 "C".  The pop-up shows
  B, A, C while `uids` is [1, 2, 3, 4].  Picking "B" queues the edit on uid 1, picking "A"
  queues it on uid 2 ("B"), and picking "C" queues it on uid 3.  Even a resource with a
  unique name maps to the wrong uid.  The builder and `push()` accept the edit, because the
  wrong target is still a valid resource, so Save writes a rename or an assignment of the
  wrong resource.  The only clue is the status text, which gives the uid but not the name.
  Resource Information makes this worse: unlike Qt it does not prefill the chosen resource's
  current name, so the user never sees which resource the dialog resolved to.
- **Qt shell:** Resource Information takes the resource from the Resource Sheet selection,
  so there is no ambiguity.  Assign Resources uses `QInputDialog::getItem` and then
  `names.indexOf(chosen)` (`app_qt/dialogs.cpp:1077`).  With duplicate names that always
  picks the first of the duplicates.  That is an older and smaller version of the same flaw
  (see note N1), but in Qt a uniquely named resource is never mis-mapped.  This wrong target
  happens only in the macOS shell.
- **Fix (one line):** build each item with
  `[[popup menu] addItemWithTitle:... action:nil keyEquivalent:@""]`, which allows duplicate
  titles, give it `setTag:uid`, and read `[[popup selectedItem] tag]` instead of indexing
  `uids`.  A cheaper alternative is to title each item "uid N: name" so that no two titles
  can be equal.

### R35G4-2 (Medium, CI compile failure): `msqt::shell_implements` is no longer exhaustive

- **Where:** `app_qt/command_status.h:20-79` at 1cd37a9.  The switch over `app::MenuAction`
  has no `default:` and ends with `case hide_app: case none: return false;`.  The five new
  enumerators (`resource_information`, `assign_resources`, `font_bold`, `define_wbs_code`,
  `calculate_project`, added at `app/menu_model.h:103-107`) are not handled.
- **Failure:** in GCC and Clang, `-Wswitch` is part of `-Wall`, and `qt.yml` configures the
  Qt build with `-DCORE_WARNINGS_AS_ERRORS=ON`, so `qt_set_warnings` adds `-Werror`.  The
  Qt ubuntu-latest and macos-15 jobs therefore fail to compile `main_window.cpp` and every
  other file that includes `command_status.h`.  MSVC's matching warning, C4062, is off by
  default, which is why the local Windows gate ("Qt 52/52", "warnings-as-errors clean")
  passed.  The record's reason for keeping W35-4.1 as one commit is that a partial commit
  "would not compile under -Wswitch".  That reason makes the missed fifth switch site
  stand out: `main.mm:2960`, `menu_model.cpp:303` and `menu_model.cpp:413` were updated,
  but `command_status.h:20` was not.
- **Side effects in the Qt shell even once it compiles:** `MainWindow::build_menus()`
  (`app_qt/main_window.cpp:828-893`) creates a QAction for every `menu_specs()` item.  The Qt
  shell therefore now has five hidden Edit-menu actions, Font Bold carrying Ctrl+B, that
  are disabled with the "not implemented in this port" tooltip, although Qt implements all
  five through the ribbon.  The generated `docs/ui-command-backlog.md`, which lists menu
  rows from `shell_implements`, is stale for these five rows.  `check_command_backlog.py`
  checks only ribbon rows, so it stays green.
- **Fix (one line):** add the five to the implemented block of `shell_implements`, and add
  matching `handle_action` cases that call `rename_selected_resource()`, `assign_resources()`,
  `bold_selected_cell()`, `define_wbs_code()` and `calculate_project()`.  Set
  `state.resource_sheet_view` in Qt's `update_menu_state()`, then regenerate the backlog.
  Putting them below the UNIMPLEMENTED marker also compiles, but it would make the backlog
  claim that the five commands are unimplemented.

### R35G4-3 (Low): the menu tests leave the permissive half of three new rules untested

- **Where:** `app/menu_tests.cpp` (the W35-4.1 block added after line 511).
- The checks that exist are real, not vacuous.  For each action they cover: offered once,
  disabled without a document, disabled during a cell edit, Cmd-B (key "b" with Command
  and no Shift or Option), and Resource Information requiring the Resource Sheet, with a
  red check recorded.  These are not covered:
  - Resource Information is enabled with **no** selection.  This is the macOS-specific
    choice: the resource is picked in the dialog, and `has_selection` in macOS describes
    the hidden task sheet.  A later `&& state.has_selection` would pass every test.
  - Define WBS Code outside the task view.
  - Calculate Project without a selection.
- **Fix (one line):** add `check(menu_item_enabled(resource_information, resource_only` with
  `has_selection=false))`, plus the WBS-in-resource-view and Calculate-without-selection
  checks.

### R35G4-4 (Low): Resource Information gives less context than the Qt dialog

- **Where:** `app/main.mm:3258-3260`.  The name field is empty with the placeholder
  "New name".  Qt's `ask_resource_name` (`app_qt/main_window.cpp:3085`) prefills the
  selected resource's current name.  This changes nothing in the edit (the builder owns
  every name rule), but it is what lets R35G4-1 go unnoticed.
- **Fix (one line):** when the pop-up selection changes (or after the pick), prefill the
  field with that resource's current name, or put the uid and current name in the
  informative text.

## Notes (not findings)

- **N1 (pre-existing, Qt, outside this commit):** `app_qt/dialogs.cpp:1077` resolves the
  picked resource by `names.indexOf(chosen)`, so with duplicate names Qt always assigns the
  first of the duplicates.  Worth fixing alongside R35G4-1, using the uid in the item data.
- **N2 (pre-existing, macOS):** the Predecessors dialog's delete-link pop-up
  (`app/main.mm:3961-3971`) uses the same `addItemWithTitle:` with a parallel index vector.
  Two links with the same predecessor, type and lag text would collapse.  This is outside
  G4, but the same fix applies.

## Checked and clean

1. **Builder, `push()` and refusal handling (item 1).**  Each of the five handlers calls its
   shared builder (`make_rename_resource_edit`, `make_assignment_edit`,
   `make_cell_bold_edit`, `make_wbs_code_edit`, `make_calculate_edit`) and then
   `plan.edits.push(std::move(edit), &plan.base)`.  It shows the builder's `error` string
   with the same prefix Qt uses ("resource information: ", "assign resources: ",
   "Font Bold: ", "Define WBS Code: ", "Calculate Project: ").  It shows "the edit was
   refused" when `push()` refuses.  The success note and `apply_structural_change` run only
   after both succeed.  No handler builds a `StructuralEdit` by hand or skips `push()`.  Every
   handler refuses a read-only session first, as Add Resources does.
2. **The five save paths (item 2).**  All five kinds are in `is_exclusive_kind`
   (`app/edit_model.cpp:95-106`), so `push()` refuses a second edit of any kind.  Calculate
   Project queued twice, or Bold followed by Calculate, is refused in both shells.  The
   shared save cascade (`edit_model.cpp:5233-5356`) refuses each kind with another
   structural edit, a start edit, or a table-format or formula document edit.  The macOS
   save funnel goes through the same `Document::save_structural` / `save_structural_edits`
   (`main.mm:4711-4715`).  `push()` repeats the plan-side refusal for each kind
   (`edit_model.cpp:4072-4176`).
3. **Selection and targets (item 2).**  `app::view_mode_is_task` is `task_sheet` only
   (`app/view_model.cpp:257`), the same as Qt's `prefs_.view == task_sheet`.  Font Bold uses
   the same `anchor() == focus()` range check, with the shared refusal text, and the same
   `sheet.column(focus().column).field`.  The builder refuses any field other than
   `kBoldMeasuredFieldId` and any task other than uid 1.  Assign Resources uses the same
   `selected_task(sheet, row_map)` and the same `held_assignment_resource`, so it removes a
   held assignment or assigns otherwise.  A removal on a task that holds two real
   assignments is refused by the builder in both shells.  Define WBS Code on a summary,
   pending or other unmeasured population is refused by `wbs_code_plan_refusal` in the
   builder and again in `push()`.
4. **The plan handed to the builders.**  The macOS shell passes `plan.base` (loaded plus
   structural edits), the same plan every existing macOS handler uses (for example
   `main.mm:758`, `3199`).  Qt passes `doc_.plan()`, its `view_`, which is base plus folded
   cell edits.  Every file-side rule is a dry run against the file in both shells.  I found
   no macOS cell edit that reaches a save except Start, and the macOS save funnel refuses a
   pending Start whole (`main.mm:4559-4568`).  So this difference does not change what is
   written, and it does not open a Calculate Project path (or any other builder-level High)
   that Qt keeps closed.
5. **The moved helpers (item 3).**  The Qt diff is two hunks.  The Font Bold message is
   `"Font Bold: " + std::string(app::bold_range_refusal())`, which is byte-identical to the
   old literal.  `held_assignment_resource` returns the first row with `task_uid` matching
   and `resource_uid != 0`, the same as the old loop with `break`.  The needles pinned by
   `app_qt/tests/cell_bold_gate_test.cpp:232` ("more than one cell") and by the new
   `app_edits_cell_bold_tests` check still match.  The new
   `app_edits_assignment_tests::check_held_assignment_resource` covers the placeholder
   (resource uid 0), a real assignment, and an unknown task.
6. **Objective-C++ (item 4).**  `performMenuAction:` (`main.mm:2960`) has all five cases.
   `menu_item_enabled` and `menu_action_name` in `menu_model.cpp` are exhaustive.
   `menu_item_supported` and `menu_disabled_reason` have defaults.  `refresh_menu_items` uses
   if/else.  The other switch sites in `app_qt/` (`main_window.cpp:2026` and
   `shell_icons.cpp:144`) have `default:`.  The five selectors are declared in the
   delegate's `@interface` (`main.mm:2722-2726`) and defined in its `@implementation`.  The
   build uses ARC (`app/CMakeLists.txt:859`, `-fobjc-arc`), so the alloc'ed alerts, pop-ups
   and fields are not leaked.  `to_ns` and `to_std` are UTF-8 and nil-safe.  Every name used
   is in scope: `note`, `apply_structural_change`, `refresh_menu_bar`, `refreshAfterChange`,
   `plan.read_only_reason`, `plan.base`, `plan.path`, `plan.row_map`, and
   `SheetModel::anchor/focus/column`, the last also used at `main.mm:2915`.  Index bounds are
   checked before `uids[...]`.  I found no unused variable or parameter (`sender` is voided)
   and no sign-compare.
7. **Menu enablement versus the handlers.**  `validateMenuItem:` and `refresh_menu_items` use
   `menu_item_enabled`, so Cmd-B is inert during a cell edit and outside the Task Sheet.
   `current_menu_state` sets `resource_sheet_view` from `view_mode`.  Cmd-B is used by no
   other menu item (`test_key_equivalents_unique` is per menu, and no other menu has "b").
   The Keyboard Shortcuts text adds the Cmd-B line.
8. **Hygiene (item 6).**  The diff of 1cd37a9 and of 4e3ba80 contains no client, person or
   company names and no local paths; the only name is the repository's git author identity.
   The commit trailer `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35)` is
   present.  The commit carries one finding id, W35-4.1, which meets standing rule 6.  Doing
   it as one commit is reasonable, because the enum and its exhaustive switches must land
   together.  The "Stays unwired" list in the record (Sort, Show Level, Bold off and the
   pre-wave-34 Qt-only builders) gives a reason for each command, as the brief requires.

## Summary

The five macOS handlers are wired correctly at the refusal level.  Each goes through its
shared builder and `push()`, shows the builder's refusal with the Qt prefix, and saves
through the shared exclusive-edit cascade.  The moved helpers leave the Qt shell's behaviour
and message text byte-identical.  Two things need fixing before the wave closes:

- **R35G4-1 (High):** a macOS-only wrong target.  `NSPopUpButton addItemWithTitle:` silently
  drops duplicate titles, so with two resources of the same name (or two blank names)
  Resource Information and Assign Resources map the picked row to the wrong uid.  The
  builder accepts that uid, so Save writes a rename or assignment of a resource the user
  did not choose.
- **R35G4-2 (Medium):** a certain CI break.  `app_qt/command_status.h`'s `shell_implements`
  switch was not extended for the five new `MenuAction`s, so the Qt Linux and macOS jobs
  fail under `-Wswitch -Werror`.  MSVC hid this locally.  It also leaves five hidden,
  disabled "not implemented" menu actions in the Qt shell and a stale command backlog.

The rest is test coverage of the permissive enablement cases (R35G4-3) and a dialog that
gives less context than Qt's (R35G4-4).  The known builder-level Calculate Project Highs are
not reachable any differently through this shell.

Counts: Critical 0, High 1, Medium 1, Low 2.
