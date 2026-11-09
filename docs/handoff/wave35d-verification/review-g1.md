# Wave 35d G1 review (86dbee9..2ebcfd8)

0C/0H/3M/4L

Read-only review at 2ebcfd8. Code was read from a detached worktree and through `git show`. Nothing
was built or run. Line numbers are at 2ebcfd8.

## Item by item

- **1.1 (R35cG1-3).** Both new cases reach the Paste guard. In `paste_selection`
  (`app_qt/main_window.cpp:5051-5057`) only `refuse_read_only()` runs before
  `refuse_view_change("a pasted cell")`, and its text is different. Each test asserts the needle
  that only that call produces ("... File > Save would write it, not a pasted cell"), so with Q10
  applied the status check fails in both `qt_sort_persist` and `qt_outline_persist`. In
  `outline_persist_test.cpp` the target is selected by record row (`select_cell(1, ...)`) but
  "name unchanged" reads display row 1. Under Level 1 these can be different tasks, so that
  check may not prove anything there. The needle check alone still makes the test red.
- **1.2 (R35cG1-4).** Both new call sites are in place (`main_window.cpp:6065`, `:6216`). Every
  production route is covered: the Sort dialog and the one ribbon Sort button go through
  `show_sort_dialog` to `apply_sort_choice`; View > Outline (menu and ribbon) goes through
  `show_outline_menu` to `apply_outline_level`; Expand All, Collapse All, the row toggle and the
  summary toggle call `view_change_refusal`. No other production code calls
  `doc_.set_sort`/`clear_sort`, `show_outline_level` or `toggle_collapsed`.
  - The Sort-branch outline refusals now have tests (`sort_persist_test.cpp:427-540`). Each
    would turn red without its guard, because the row count or the status needle changes.
  - Expand All under a queued Sort is tested only as disabled. I accept that. Its call site is
    tested under a queued Level (`outline_persist_test.cpp:194`), and the Sort branch is tested
    through Collapse All and the other callers.
  - "Clearing the sort is not a change of the order Save writes" is right in the code.
    `PlanDocument::clear_sort` (`plan_document.cpp:1014`) leaves the sheet's collapse state
    alone, and `after_model_change` only rebuilds the views, so the queued level stays shown in
    file order. But the rule is not consistent end to end (R35dG1-1), and the clear case is
    tested only as a no-op (R35dG1-5).
- **1.3 (R35cG2-1).** The seam resets on every path that can continue. Every exception the save
  throws is a `std::exception` (`Error` derives from `std::runtime_error`, `cfb_writer.h:76`), so
  `set_calculate_fault_after_appends(0)` (`tests/roundtrip_calculate.cpp:1269`) always runs. A
  non-std throw would end the process at `main`'s catch (`:1363`). `refused_requests` is the last
  test function, and `check` does not throw, so the seam cannot leak into a later test. The
  codebase has no threads (no `std::thread`, `QThread` or `QtConcurrent` in `app`, `app_qt` or
  `core`), so the plain global is safe here.
  - The test proves what it claims. The scratch exists after each append, so removing the
    cleanup at `calculate.cpp:1069` fails "no scratch file left". Pointing the appends at `path`
    fails "target unchanged".
  - It does not reach the Props edit (R35dG1-4).
- **1.4 (R35cG0-3).** The refusal text (`calculate.cpp:321-324`) is accurate and keeps the shared
  prefix the spec table quotes (`calculate.md:310`). `calculate.md:330-344` states the scope
  correctly: Calculate Project is enabled for the measured control's values only, and a null save
  cannot lift the pin. The list of port saves that are now refused is incomplete, and one of its
  claims is wrong (R35dG1-2).
- **1.5 (R35cW-2).** The four ids read "partly fixed" in `wave35c-complete.md` (item table and
  CLOSE id table), `gates-g1.md` and `gates-g2.md`, and each names its R35c remainder. The
  summaries in START-HERE, `coordinator.md` and `plan-waves27-30.md` still contradict this
  (R35dG1-3).
- **1.6 (R35cW-1, R35cG2-8).** The new "wave 35d" blocks match `coord-gates-close.md`:
  - Highs: R35cG0-1, R35cG1-1 and R35cG1-2.
  - CI: R35cW-CI1 and R35cW-CI2.
  - Mediums: R35cG0-2 still open; R35cG0-3, R35cG1-3, R35cG1-4, R35cG2-1, R35cW-1 and R35cW-2
    fixed.
  - Lows: R35cG0-4 to -6, R35cG1-5 to -7, R35cG2-2 to -7 (with -8 fixed) and R35cW-3 to -5.

  But the same files still carry ids that wave 35c closed as open, and the record says they are
  gone (R35dG1-3). 2.6 now has one status in both records.
- **Informational, not a finding.** The builder could not have known these. All three lists read
  as if no High is open ("R35cG1-2 (fixed `6458203`)"), and the G0 review opened R35dG0-1 (High:
  Task Information's partial apply keeps the structural redo list, the R35cG1-2 route) and
  R35dG0-2. The coordinator needs to update START-HERE, `memory/coordinator.md` and
  `plan-waves27-30.md` for both.
- **Hygiene.** I found no `-Wmissing-field-initializers` pattern: `SortKey{field, ascending}`
  names both members, and `SortDefinition{}` is value-initialised. I found no sign compare
  (`var_records_appended` and the seam are both `std::size_t`, and the ribbon counts are `int`)
  and no `-Wswitch` change.
  - The `(std::string(label) + ...).c_str()` temporaries go to `gate::check`, which prints at
    once (`gate_util.h:22`), so nothing dangles.
  - `int = QStringList::indexOf` is an idiom other gates already use.
  - No names, client strings or personal paths were added. All 7 commits carry the trailer.

## Findings

### R35dG1-1 (Medium): the 1.2 rule can be bypassed twice: a session Sort applied before a Level is queued, and Undo Level, Sort, Redo. Both reach the display state the new refusal exists to prevent

- Where: `app_qt/main_window.cpp:6065-6068` refuses a session Sort only while a Level is
  queued. `apply_outline_level` (`:6209-6262`) queues a Level without checking whether a session
  sort is shown. `apply_sort_choice` and `apply_session_sort` (`:6072-6074`, `:6141-6143`) end
  the redo list only when its top is a `sort_view`, not a `show_level`. On Redo,
  `after_structural_change` (`:3581-3583`) shows the level again over whatever sort is shown.
  `PlanDocument::apply_structural_edits` never resets the session sort. The spec states the rule
  at `spec/writers/view-state.md:352-356` and gives the reason at `:365`.
- Failure (outline fixture):
  - (a) Sort Name descending. It stays session only, since the file is not the measured sort
    file. Then View > Outline > Level 1 is queued with no refusal, and the rows show sorted at
    Level 1 while Save writes the level over the file's order.
  - (b) Queue Level 1, then Ctrl+Z (the level goes onto the redo list). Sort Name descending is
    accepted, because nothing is queued, and the redo list is kept. Ctrl+Y re-queues Level 1 under
    the sorted display.
  - Either way the end state is the one 1.2 refuses as "the sort re-ordered the rows while Save
    wrote the level over the file's order". The records call R35cG1-4 fixed and the G0 Redo
    question "the coordinator's decision". By 1.2's own reason, route (b) now diverges.
- Fix: decide the rule both ways. Either:
  - while a session sort is shown, keep a Level session only (or refuse it), and have a session
    Sort end a `show_level` redo entry; or
  - drop the new Sort refusal and record that a session sort under a queued Level is session
    only, as after an Undo of a Sort.

  Then add case (b) to `qt_outline_persist`.

### R35dG1-2 (Medium): the calculate.md "Scope" list of port saves that are now refused leaves out three writers, and it wrongly says the var-record writers still match

- Where: `spec/writers/calculate.md:346-354`, against `calculate.cpp:333-358`. The tail hash
  zeroes only the Task size scalar `0x01000100`.
- What is wrong:
  - `varmeta_edit.cpp:844` and `:878-880`: a `remove` var edit (the measured Notes and Text1
    removals, `varmeta_edit.cpp:132-134`) adds the freed payload to the Task freed scalar
    `0x01000002` in the tail. A port-saved file after a Notes or Text1 removal is refused at the
    pin, yet the spec says a save "such as the var-record writers, still matches".
  - `actuals.cpp:1931-1938` (Task freed, Assignment freed and the Assignment size) and `:2031`
    (the status-date record payload): percent-complete saves change pinned values.
  - `outline.cpp:2228-2260` (Task freed, Assignment rows, freed, size and deleted): indent and
    outdent saves change tail scalars.

  Item 1.4's deliverable was this list. As written it understates who is refused and
  misdescribes the var-record case.
- Fix: say "a save that changes only the Task size (the var-record appends)", and add the
  var-record removes, the actuals (percent complete) and the outline saves to the list of
  refused port saves.

### R35dG1-3 (Medium): the 1.6 lists still show ids wave 35c closed as open, and three summaries contradict the 1.5 correction, while the record says "the ids 35c closed are gone from their lists"

- Where:
  - `memory/coordinator.md:13`: "**Open Highs:** R35bG0-1 and R35bG0-2". Wave 35c fixed both
    (`7d923dc`, `743ad8b`), and nothing in the file says so.
  - `docs/handoff/plan-waves27-30.md:376-389`: "Queued from wave 35b" still lists R35bG0-1 and
    R35bG0-2 as Highs and the R35bG0-3.. Mediums with no closed mark. The new paragraph at
    `:403` covers only the G4 and CLOSE "Also open" list.
  - `docs/START-HERE.md:29-32`, `memory/coordinator.md:17` and `plan-waves27-30.md:403`: wave
    35c "fixed ... the wave 35b Mediums" or "closed the rest" except R35bG1-3 and R35bG2-6
    (partly). 1.5 has just made R35bW-1, R35bW-2, R35bG2-2 and R35bG3-1 partly fixed in the same
    wave.
  - `memory/coordinator.md:1`: the header still reads "2026-11-09, wave 35 complete on branch
    `wave35`".
  - The claim itself: `docs/handoff/wave35d-complete.md:61` ("the ids 35c closed are gone from
    their lists").
- Failure: a reader of `coordinator.md` sees two Calculate Project Highs as open that are closed.
  A reader of any of the three summaries sees the four partly fixed 35b ids as closed, which is
  R35cW-2's error repeated in the files R35cW-1 named.
- Fix: mark R35bG0-1 and R35bG0-2 (and the 35b Mediums 35c closed) as closed in
  `coordinator.md:13` and `plan-waves27-30.md:380-389`. Add the four partly fixed ids to the
  three summaries. Update the `coordinator.md` header.

### R35dG1-4 (Low): the fault seam covers only the five appends; the Props edit and the final write are not reached

- Where: `core/mppwrite/calculate.cpp:1040-1043` (the seam is inside the append loop) against
  `:1058` (`save_props_edit(work, props_edit, scratch)`) and `:1060-1066`.
- Failure: the mutation `save_props_edit(work, props_edit, path)` keeps every test green. The
  success path writes `path` twice and ends with the same bytes. A failure after it (the re-read
  compare, or I/O in `work.save`) would leave the target partly calculated. The seam is also a
  mutable global that production code exports with no RAII reset. That is safe today (see 1.3)
  but fragile if a test is added after `refused_requests`.
- Fix: let the seam also fire after `save_props_edit` (for example `appends == 6`), assert the
  target is byte-identical for that case too, and reset the seam with a scope guard in the test.

### R35dG1-5 (Low): the accepted "clear the sort under a queued Level" is tested only as a no-op

- Where: `app_qt/tests/outline_persist_test.cpp:430-435`. The Sort just above was refused, so no
  session sort is shown when the clear runs. The test asserts the level is still queued and that
  nothing was refused, but not the rows.
- Failure: the case the spec reasons about (`view-state.md:366`: a sorted display under a
  queued Level, reachable by R35dG1-1) is never run. A change that made `clear_sort` reset the
  collapse state would leave every test green.
- Fix: reach a sorted display under a queued Level (route (a) or (b) of R35dG1-1, while that
  route exists), clear the sort, and assert `sheet_row_count() == queued_rows` and file order.

### R35dG1-6 (Low): the three Sort-branch refusals 1.2 now tests have no recorded red run, and the rule sentence contradicts its own table

- Where: `docs/handoff/wave35d-verification/gates-g1.md:102` and `wave35d-complete.md:71`: only
  the two new call sites were seen red. Collapse All, the row toggle and the summary toggle under
  a queued Sort (`sort_persist_test.cpp:479-500`) were never run red, which the "red then green"
  rule requires. Separately, `view-state.md:352-356` says "every change to the displayed outline
  or order other than the queued kind's own replacement or withdrawal is refused", but `:366`
  accepts clearing a sort (a change of order) under a queued Level.
- Fix: run the mutation with the `sort_view` branch of `view_change_refusal` (`main_window.cpp`
  `:1629-1632`) removed, quote the FAILs, and add "clearing a session sort" to the rule's
  exceptions.

### R35dG1-7 (Low): the CI ids are recorded "fixed" before CI has run

- Where: `docs/START-HERE.md:19-20`, `memory/coordinator.md:25-26` and
  `plan-waves27-30.md:411-412`: "R35cW-CI1 (fixed `23de6e2`), R35cW-CI2 (fixed `171831b`)". The
  brief's Done-when for 0.1 and 0.2 is a green CI run, and "fixed means fully closed".
- Fix: write "committed, pending CI (CI is its gate)" until the coordinator's run is green.
