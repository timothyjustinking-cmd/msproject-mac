0C/0H/1M/9L

# Wave 35e, group G2: independent review (07c4ef6..2dbb5d9)

Read only, at `2dbb5d9`, through `git show` and `git diff`. Nothing was built or run. Every line
number below is at `2dbb5d9`.

## The questions asked

1. **2.7, `app/main.mm`.** I found nothing that would fail to compile or warn under AppleClang
   with `-Wall -Wextra -Wpedantic -Werror` (C++20/ObjC++20, ARC).
   - `calculate_queued_refusal` (`:1445-1451`) is defined inside the file's anonymous namespace
     (`:122` to `:2037`), after `note` is declared (`:286`), and it is used twice, so no
     missing-prototype or unused-function warning.
   - `StructuralEdits::applied() const noexcept` (`edit_model.h:884`) returns a const vector
     reference, so the const-ref binding is fine.
   - `"literal" + const std::string& + "literal"` is fine. The `if (init; cond)` form is C++17.
   - It adds no Objective-C selector, no implicit narrowing, no sign compare and no new
     `#include`.
   - It mirrors the Qt shell's Calculate branch word for word: the same text as
     `data_change_refusal`, `main_window.cpp:1646-1651`.
   - The two call sites are the shell's only cell writers. Return, Tab, a click elsewhere and
     Backspace-to-clear all reach `commit_open_edit` (`:2120`, `:2197`, `:2205`, `:2286-2293`),
     and Paste reaches `paste_from_pasteboard` (`:1828`).
   - Cut is a stub (`:4949`), there is no Fill, and the Task Fields dialog pushes through
     `StructuralEdits::push`, whose exclusivity check refuses beside a Calculate.
   - The AppKit shell has no Sort or Show Level, so a Calculate-only refusal is the right
     mirror.
   - `calculateProject:` cancels an open edit before it queues (`:3607-3609`), and the sheet is
     rebuilt on the push, so no sheet undo or redo can bring an edit back.
   - Not mirrored: 2.6 (R35eG2-3).
2. **2.6, `has_pending_edits()`.** It is exactly the save's predicate.
   - `Document::has_pending_edits()` (`document.h:338-340`) is `view_state_edits_.any() ||
     custom_formula_.any()`.
   - `save_structural` (`document.cpp:757-771`) copies those two members into
     `StructuralDocumentEdits`, whose `any()` (`edit_model.h:1091`) is the same disjunction.
   - The Calculate exclusivity check tests `document_edits.any()` (`edit_model.cpp:5719-5724`).
   - With a Calculate queued, `structural_` is non-empty, so Save always takes this path.
   - So it cannot refuse an edit Save would accept, or miss one Save would refuse. The guards
     do have gaps: a third formula surface (R35eG2-1), a Redo route (R35eG2-2), and the AppKit
     shell (R35eG2-3).
3. **2.1.** The mapping is correct.
   - `table_column_field` (`view_model.cpp:774-786`) maps `0x0B400507` (Task.DurationText, the
     writer's `kSortFieldDuration`) to the stock Duration field, and returns Name's `0x0B40000E`
     unchanged.
   - The Qt re-apply uses the mapped key for both the comparison and the pushed key
     (`main_window.cpp:3632-3640`).
   - `app_edits_sort_tests.cpp:218-225` checks both Name pairs and the Duration pair
     (`sort-duration-desc`, `:126`) against `0x0B40001D`, and it goes red without the mapping
     (the builder's 2.1 red run).
4. **2.9 and 2.11, the seams.**
   - `g_fault_after_appends` is reset by `FaultSeamReset` on every path through the loop
     (`roundtrip_calculate.cpp:1407-1411`).
   - The scratch-probe seam is still reset by hand (R35eG2-8). That is safe today: only
     `std::exception` is thrown, it is caught, and `check` does not throw.
   - Neither seam has any effect in a normal build. `g_fault_after_appends == 6` cannot hold
     at 0, and a default `error_code` selects the real `symlink_status` (`calculate.cpp:1060-1062`).
   - The new header include (`<filesystem>` in `calculate.h`) and `<iterator>` for `std::size`
     are present.
5. **Records.** Several records overstate or misname things: R35eG2-1 makes the 2.6 "fixed"
   wrong, and R35eG2-5, R35eG2-6, R35eG2-7 and R35eG2-9 are smaller.
6. **Hygiene.**
   - All 13 commits carry `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35e)`.
   - The diff adds no names, client strings, personal paths or CR line endings.
   - I found no GCC- or Clang-only warning.
     - `FaultSeamReset const reset;` is a const empty class, which is const-default-constructible
       under CWG 253 (GCC, Clang and MSVC all accept it). Its non-trivial destructor suppresses
       `-Wunused-variable`.
     - `Surface surfaces[]` initialises all three members, and no sign compare was added.
   - Every new test guard has a recorded red run, except 2.7, which cannot have one here.

## Findings

### R35eG2-1 (Medium): Project > Custom Fields... queues a formula beside a queued Calculate; R35dG2-2 is recorded "fixed"

- **Where:** `app_qt/main_window.cpp:4171-4230` (`show_custom_fields`).
  - Its field-definition dialog's Formula... button (`project_dialogs_qt.cpp:843-867`) feeds
    `document_->apply_custom_formula` at `:4215`.
  - That is a third formula surface beside `show_custom_formula` (`:4912`), and it has no
    Calculate guard. The command is enabled in the Task Sheet with a document
    (`menu_model.cpp:368`) and on the ribbon (`ribbon_bar.cpp:439`, `:568`).
  - The records that say it is closed: `wave35e-verification/gates-g2.md:21` ("fixed"),
    `wave35e-complete.md:82`, and `spec/writers/calculate.md:420-426` ("Table / Date Format and
    Custom Field Formula ... now refuse").
- **Failure:**
  1. Calculate Project is queued.
  2. The user opens Project > Custom Fields..., picks Text1, clicks Formula... and enters a
     formula, then OK.
  3. The status says "queued the formula for field 0x...; File > Save writes it".
  4. File > Save then refuses the whole save ("a Calculate Project edit cannot be combined with
     a table format or custom-field formula edit").
  - This is exactly the R35dG2-2 scenario on the other formula entry point. No wrong file.
- **Fix:** add `if (queued_edit_is(structural_.applied(), app::StructuralKind::calculate_project)
  && refuse_data_change("a custom-field formula edit")) return;` before the dialog in
  `show_custom_fields`. Add `MenuAction::custom_fields` to the 2c `surfaces` list in
  `calculate_gate_test.cpp`. Or record R35dG2-2 as partly fixed.

### R35eG2-2 (Low): Redo re-queues a Calculate beside a pending document edit

- **Where:**
  - The guard is only in `calculate_project` (`main_window.cpp:3437-3445`).
  - `show_table_date_format` (`:4858-4910`), `show_custom_formula` (`:4912-4960`) and
    `show_custom_fields` do not clear the structural redo list.
  - The Redo branch (`:2321-2330`) calls `structural_.redo()` with no `has_pending_edits()`
    check.
- **Failure:**
  1. Calculate Project, then Ctrl+Z.
  2. Table / Date Format, accepted, because nothing is queued.
  3. Ctrl+Y re-queues the Calculate without `push()`, beside the pending edit ("redo the
     structural edit").
  - Save refuses the pair. The record's "Calculate beside a pending edit of either [is
    refused]" does not hold on this route.
  - It is the same class as the Redo route W35d-0.5 closed for Cut and Paste.
- **Fix:** clear the structural redo list when either document edit is queued (both dialogs and
  `show_custom_fields`), and test Undo, date format, then Redo disabled.

### R35eG2-3 (Low): the AppKit shell has neither half of 2.6, and no record says so

- **Where:**
  - `app/main.mm:3596-3627` (`calculateProject:` pushes with no
    `plan.document->has_pending_edits()` check).
  - `:4174-4246` (`tableDateFormat:` reaches `request_view_state_edits` at `:4235` with no
    Calculate check).
  - `:4252-4360` (`customFieldFormula:` reaches `apply_custom_formula` at `:4352`).
  - The "Found, not fixed" lists in `gates-g2.md` and `wave35e-complete.md`, and
    `calculate.md:420-426`, name the Qt functions only.
- **Failure:** on macOS, both orders are accepted with "Save writes it", and the save then
  refuses. It is R35dG2-2 on the other shell, the way R35dW-5 was R35cW-4 on the other shell.
- **Fix:** mirror the two refusals in `main.mm`, using `calculate_queued_refusal` and a
  `has_pending_edits()` check in `calculateProject:`, with CI as the gate. Or record it under
  "Found, not fixed".

### R35eG2-4 (Low): Calculate's new refusal advises "save that edit first", which for a formula leads to another refusal

- **Where:** `app_qt/main_window.cpp:3438-3441`.
- **Failure:**
  1. A formula is pending, and the user saves it as advised.
  2. The saved file's `114/Props` now carries the formula and the custom-field flag (records
     `0x02401392` and `0x024013c5` among the pin's records, `calculate.md:362-370`), so it no
     longer matches the measured control.
  3. A Calculate queued on the reopened file is then refused at the Props pin by
     `save_calculate`. (I found this by reading; I did not run it.)
  - The shell has no discard command for a pending formula. Discard happens only through a
    save or a reopen with Don't Save.
- **Fix:** "...pending and File > Save would refuse the two together; save it (a file saved
  with a custom-field formula cannot take Calculate Project) or reopen without saving".

### R35eG2-5 (Low): three records name `macos-qt` as `app/main.mm`'s only CI gate

- **Where:**
  - `docs/handoff/wave35e-verification/gates-g2.md:119`
  - `docs/handoff/wave35e-complete.md:117`
  - `spec/writers/calculate.md:418` ("CI on macos-qt is the gate for it")
- **Problem:**
  - The dedicated compile of `main.mm` with `-Werror` is the `macos-appkit` job
    (`.github/workflows/app-macos.yml:79-81`). It runs on every push.
  - `main.mm` is also compiled under `macos-qt`, through `app_qt/CMakeLists.txt:38`
    (`add_subdirectory(app)`) and `app/CMakeLists.txt:836` (`if(APPLE)`). But `qt.yml` runs
    only on dispatch or a tag.
  - The brief's "CI ids are never fixed before CI" check therefore needs `macos-appkit` named.
- **Fix:** "CI on `macos-appkit` (app-macos.yml), and on `macos-qt`, which builds `app/` too,
  is its only gate".

### R35eG2-6 (Low): "both orders tested" covers the date-format edit only

- **Where:**
  - The 2.6 claims: `wave35e-complete.md:82`, `gates-g2.md:21` and `calculate.md:425`.
  - The reverse-order case (`app_qt/tests/calculate_gate_test.cpp:446-489`) queues a date
    format only.
- **Problem:** no test queues a formula and then clicks Calculate. The guard is one predicate
  (`has_pending_edits()`), so the guard itself is reached. But the record says both edits were
  tested in both orders.
- **Fix:** add a formula-pending, then Calculate case. Or reword to "Calculate beside a pending
  date-format edit is tested; the formula side shares its predicate".

### R35eG2-7 (Low): the seam's header says 6 is "the last step before the target is written"; three steps remain

- **Where:**
  - `core/mppwrite/calculate.h:167-168`.
  - The comment at `calculate.cpp:1106-1108` says "only the re-read and the target write
    remain". But `work.save(scratch)` and the re-read compare both run first
    (`calculate.cpp:1113-1118`).
- **Failure:** the R35dG1-4 class of mutation moved one step on stays green: `work.save(path)`
  in place of `work.save(scratch)`. The success path still ends with the same bytes, and the
  fault case 6 throws before that line.
  - A failure in the re-read compare would then leave the target fully written but unverified,
    and no test would see it.
- **Fix:** reword the header to "after the Props edit, before the scratch save and re-read".
  Optionally add a step 7 after `work.save(scratch)`.

### R35eG2-8 (Low): the scratch-probe seam is still reset by hand

- **Where:** `tests/roundtrip_calculate.cpp:1484-1493` and `:1515-1525`.
- **Problem:**
  - The fault seam gained a scope guard (2.9), but its sibling did not.
  - It is safe today: `save_calculate` throws only `std::exception`, which both blocks catch,
    and `check` does not throw.
  - But a `check` or `file_bytes` call moved between the set and the reset would leave every
    later Calculate save failing, or faking a free path.
- **Fix:** reuse the RAII guard pattern:
  `struct ProbeSeamReset { ~ProbeSeamReset() { set_calculate_scratch_probe_fault(file_type::none, {}); } };`

### R35eG2-9 (Low): R35cW-3 still has two statuses, and the status file still lists R35dG0-1 as open

- **R35cW-3:**
  - `docs/handoff/wave35d-verification/gates-g2.md:18` still reads "2.1 | R35cG0-2, R35cW-3 |
    `6f5cc18` | **partly fixed**".
  - 2.13 (R35dW-6, "give R35cW-3 one status") changed only `wave35d-complete.md`, though 2.12
    edited this same file.
  - `wave35e-complete.md:89` says R35cW-3 now has one status.
- **R35dG0-1:** `docs/handoff/status-2026-11-10.md:21` still reads "Still open, one High,
  R35dG0-1". 2.13 edited this file, and `wave35e-complete.md:10` records R35dG0-1 as fixed
  (`b8ee455`).
- **Fix:**
  - Split the `gates-g2.md:18` row so that R35cW-3 reads "fixed `6f5cc18` (record)".
  - Mark R35dG0-1 "fixed in source by W35e-0.1 `b8ee455`" in the status file, or date that
    paragraph as a snapshot.

### R35eG2-10 (Low): hygiene, lines over 100 columns

- **Where:**
  - `app/app_edits_sort_tests.cpp:224`
  - `app_qt/tests/calculate_gate_test.cpp:274`
  - `core/mppwrite/calculate.h:168`
  - `tests/roundtrip_calculate.cpp:1031`, `:1417` and `:1526`
  - `docs/handoff/status-2026-11-10.md:84` (the 2.13 edit left a 103-column line unwrapped)
- **Problem:** no CI step checks line length. These are style only.
- **Fix:** rewrap.

## Not findings

- **2.5 records Fill Down's guard as untested and unreachable.** That is accurate:
  `handle_ribbon_command` is private (`main_window.h:466`), and the ribbon control is disabled
  (W31-0.4). It is honestly recorded "partly fixed".
- **2.10's exact-outcome checks.** The checks (`app_edits_assignment_tests.cpp:1462-1475`) fail
  on any other refusal, which is what R35dG2-3 asked.
- **2.11's case.** The not_found-with-`ec` case (`roundtrip_calculate.cpp:1505-1533`) fails
  under the `if (ignored)` mutation on every platform. The platform dependence R35dG2-4 named is
  gone.
- **2.8's needles.** Each needle (`"114/Props tail is "`, `"Task size scalars, not one"`) comes
  from one branch only. The scalar search reads the tag big-endian, as `check_props_pin` does
  (`calculate.cpp:356-359`).
- **2.3 and 2.2.** The sorted-under-a-Level case, and the direct `apply_session_sort` after
  Undo, each have a recorded red run. Dropping the "rows show" check is justified in the test's
  comment.
