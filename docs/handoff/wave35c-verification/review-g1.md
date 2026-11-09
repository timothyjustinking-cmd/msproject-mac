0C/2H/2M/3L

# Wave 35c G1 review: items 1.1 to 1.11 (`b538646..ea54226`)

Independent, adversarial, read-only.  Nothing was built, run or edited; every statement comes
from reading the source, the tests, the records and the earlier reviews at `ea54226`.  Line
numbers are at `ea54226`.  Brief: `docs/handoff/wave35c-brief.md` (G1).  Builder's record:
`docs/handoff/wave35c-verification/gates-g1.md`.  Severity is the coordinator's scale (High
includes "a bypass of a guard"); no finding below writes a corrupt file: each High is a
measured Sort written over a display, or a plan, the guard was meant to keep it from.

## Per item

| Item | Finding | Closed? | Notes |
|---|---|---|---|
| 1.1 | R35bG3-1 | Yes for the paths named | Expand All, Collapse All, the row toggle and the summary toggle share `view_change_refusal` (`main_window.cpp:1616`); the summary refusal puts the box back (the `setAccessibleName` fix makes `set_stack_check` find it, signals blocked).  The guard is not applied to a session Level or Sort under the other kind's queue (R35cG1-4). |
| 1.2 | R35bG4-1 | Yes | `after_structural_change` re-shows the queued level on every rebuild (`:3573-3576`); `apply_outline_level` clears redo after a withdrawal (`:6164`).  Undo, then a refused level, then Redo re-queues Level 1 and the rebuild shows it, so display and file agree. |
| 1.3 | R35bW-1 | Partly | The cell gate (`sheet_table_model.cpp:498-506`), Task Information/Form, Cut, Paste and Fill Down refuse while a Sort or Level is queued.  Redo still re-queues a Sort or Level over a plan edited by Cut or Paste, bypassing the 1.5 rule (R35cG1-2).  Paste's guard is untested (R35cG1-3). |
| 1.4 | R35bW-2 | Partly | `clear_redo()` after the in-place withdrawal (`:6033`).  The same Redo returns when the Sort was withdrawn by Undo first (R35cG1-1). |
| 1.5 | R35bG0-3 | Yes | Free Slack (+74) as the before-state, in both layers (`edit_model.cpp:3873-3893`, `calculate.cpp:745-776`), with plan, file and `push()` tests; residual limits recorded in `calculate.md` section 8. |
| 1.6 | R35bG0-4 | Yes | Deadline, Active, task and assignment baseline loaded through `load_plan` from mutated files with plan needles.  The new reader fields (free slack, FixedMeta entry, VarMeta keys, resource session byte) fail closed, so deleting their adapter lines turns the positive control red. |
| 1.7 | R35bG0-5 | Yes | `calculate_leaf_meta_mismatch` and `calculate_leaf_var_keys` shared with the writer; the plan reads the reader's 47-byte entry and sorted keys; empty entry refused. |
| 1.8 | R35bG1-1 | Yes | Reason built before `sorted.clear()` (`viewstate_sort.cpp:503-510`).  Release tests are a pin only, as recorded. |
| 1.9 | R35bG1-2 | Yes | Every resource row 1..N plus the target (`assignment_growth.cpp:2347-2370`); writer test changes only stream byte 176. |
| 1.10 | R35bG1-3 | Sort and removal yes; Show Level partly, as recorded | Plan outline pin (`edit_model.cpp:3251-3265`, size checked at `:3239`); plan session-byte rule (`:3044-3058`); Show Level builder calls the writer's predicate at its own call site.  Each layer has a test whose needle the other layer cannot produce. |
| 1.11 | R35bG2-1, R35bG3-2 | Yes | Tie (`edit_model.cpp:3783-3795`) and first-leaf (`:3816-3831`) in `calculate_plan_refusal`, so `push()` refuses both; plan-only tests assert the plan text and the absence of the writer's. |

No `-Wdangling-reference` pattern, cleared-vector read, uninitialised member or off-by-one was
found in the range: `calculate_leaf_var_keys` returns a reference to a namespace-scope vector,
`require_measured_outline_view` holds its `Bytes` by value, every new `TaskRow`/`ResourceRow`
field has a fail-closed default, and the 1.9 loop reads each row once (target first, row 0
only when it is the target).

## Findings

### R35cG1-1 (High): Undo of a queued Sort, then a refused or cleared Sort, then Redo re-queues the withdrawn Sort under a different display (R35bW-2 still open on the Undo route)

- Where: `app_qt/main_window.cpp:2253-2266` (Undo withdraws the queued Sort into redo and keeps
  the session sort), `:6026-6043` and `:6068-6080` (`apply_sort_choice` calls `clear_redo()`
  only when a Sort is queued at that moment, `had_queued_sort`), `:2286-2293` (Redo calls
  `structural_.redo()`), `:3570-3576` (the rebuild re-applies a queued Show Level, never a
  queued Sort's order).
- Failure, on `wave35-viewstate-sort-name-desc-control.mpp`:
  1. Sort by Name descending (queued).
  2. Ctrl+Z: "undo the queued Sort; the rows stay in the sorted order"; the Sort is on the redo
     list.
  3. Sort by Start: the builder refuses it, nothing is queued, `had_queued_sort` is false so the
     redo list is kept, and the rows show by Start.  (Or clear the sort: rows in file order.)
  4. Ctrl+Y: the sheet has no history, so the structural Redo re-queues Name descending; the
     status says "redo the structural edit" and the rows still show by Start (or unsorted).
  5. File > Save writes Name descending.
  This is R35bW-2's own scenario with one Undo inserted.  The Show Level counterpart is safe
  only because 1.2 re-shows the level on rebuild; Sort has no such re-apply.  The new
  `qt_sort_persist` cases (`sort_persist_test.cpp:143-186`) cover only the in-place
  replacement and clear, never Undo first.
- Fix: in `apply_sort_choice` (and the clear path) call `structural_.clear_redo()` whenever the
  session sort changes and the redo top is a `sort_view`, or have Redo of a `sort_view`
  re-apply that edit's key as the session sort; add the case Name descending, Ctrl+Z, Start,
  Ctrl+Y (and Ctrl+Z, clear, Ctrl+Y) asserting nothing queued or the displayed order equals the
  queued one.

### R35cG1-2 (High): Cut and Paste leave the structural redo list, so Redo re-queues a Sort or Show Level over unsaved grid edits, bypassing the 1.5 rule 1.3 claims closed

- Where: `app_qt/main_window.cpp:5012` (Cut ends with `after_cell_change()`, no
  `clear_redo()`), `:5095` (Paste, the same), against `:3527` (`after_cell_commit`), `:5244`
  (Fill Down) and `:5872`/`:5877` (Task Information), which all clear it.  `:2286-2288`: Redo
  re-appends without `push()` or the builder, so `plan_unsaved_edits`
  (`app/edit_model.cpp:3293`, called only at `:3473` and `:3538`) never runs.
- Failure, on the same control:
  1. Sort by Name descending (queued), then Ctrl+Z (withdrawn into redo; nothing is queued, so
     the 1.3 gate is open, correctly).
  2. Copy a name and paste it over a sibling's Name (a one-row block is allowed under a
     session sort), or Cut a Name cell.  The paste is a new sheet transaction, so the sheet's
     own redo list is empty; the structural redo list still holds the Sort.
  3. Ctrl+Y: the Sort is re-queued over a plan that carries the renamed row.  The rebuild folds
     the pasted name into the display and the session sort re-sorts on it.
  4. File > Save writes the file's own Name-descending order (the writer re-reads the file's
     names) and not the rename: R35bW-1's outcome, reached without any refused step.
  The same route re-queues a Show Level over an unsaved Duration or name paste.
- Fix: call `structural_.clear_redo()` after a successful Cut and Paste, as the other cell paths
  do (or make Redo of a `sort_view`/`show_level` re-run the builder against `doc_.plan()`);
  add a `qt_sort_persist` case Sort, Ctrl+Z, paste a name, Ctrl+Y asserting Redo is disabled or
  refused with its own needle.

### R35cG1-3 (Medium): Paste's new guard has no test, and it is the only guard on that path

- Where: `app_qt/main_window.cpp:5023-5025`; `gates-g1.md:89-90` and `wave35c-complete.md:57`
  record "no test of their own".
- Why it matters: Paste writes the RecordSet through `SheetModel::paste` (`:5074-5075`), not
  `setData`, so the cell gate (`sheet_table_model.cpp:498`) does not cover it.  Remove the
  `refuse_view_change("a pasted cell")` call and a one-row paste renames a row under a queued
  Sort with no message, which is R35bW-1, and every test stays green.  The record's reason
  ("Paste needs a clipboard block") does not hold: the window's own Copy fills `clipboard_`, and
  `QGuiApplication::clipboard()->setText` works offscreen.  Fill Down's guard is moot (it fills
  only Start, which is read-only), so it needs no test.
- Fix: in `qt_sort_persist` and `qt_outline_persist`, Copy a Name cell, queue the Sort or
  Level, Paste, and assert "not a pasted cell", the name unchanged and the edit still queued.

### R35cG1-4 (Medium): the "one guard" is not applied to a session Level or Sort chosen while the other kind is queued, while its new Sort branch refuses Expand All, Collapse All, the row toggle and the summary toggle with no test

- Where: `app_qt/main_window.cpp:1621-1624` (the `sort_view` branch of `view_change_refusal`),
  used by `:2216`, `:2225`, `:2360`, `:6144`; against `apply_outline_level` (`:6151-6199`) and
  `apply_sort_choice` (`:6019-6081`), which never call it.
- What is wrong:
  - With a Sort queued, Collapse All is refused ("sort: a Sort is queued ... not this collapsed
    state"), but View > Outline > Level 1 is accepted for the session ("session only; the file
    keeps its outline (the level edit was refused)": `push()` refuses it as a second exclusive
    edit) and collapses the same rows.  Save then writes the Sort with the file's expanded
    outline.  By 1.1's own rule that is the display moving away from what Save writes.
  - With a Level queued, a Sort is accepted for the session and reorders the rows.
  - Either the Sort branch is wrong (a wrong refusal of four session-only commands) or the two
    menu paths bypass it; the two cannot both be right.  No test exercises any Sort-branch
    refusal (`sort_persist_test.cpp` tests only the cell, form and Cut needles).
- Fix: decide the rule once.  Either call `refuse_view_change` at the top of
  `apply_outline_level` when a `sort_view` is queued and of `apply_sort_choice` when a
  `show_level` is queued, or drop the `sort_view` branch from the collapse/expand/summary
  callers; add a `qt_sort_persist` case for whichever rule is kept.

### R35cG1-5 (Low): the 1.10 commit subject says Show Level has a guard in both layers; the record says partly

- Where: `4a2133e` subject ("Sort, a removal and Show Level each have a guard in both layers")
  against `gates-g1.md:113-120` and `wave35c-complete.md:64` ("partly fixed for Show Level's
  view state").
- The record is right (both layers reach `require_applied_view` and
  `require_measured_view_block`; `push()` has no file).  Anyone reading `git log` alone gets the
  wrong status.
- Fix: no history rewrite; add one line to the CLOSE record that the `4a2133e` subject
  overstates Show Level, as was done for `114861b`.

### R35cG1-6 (Low): the two removal layers read different resource rows, so their refusals can disagree on one file

- Where: the plan rule loops over `plan.resources` (`app/edit_model.cpp:3049`), which the reader
  fills with live rows only (`core/mpp/reader.cpp:2287-2337` skips uid 0 and tombstones) and
  maps by FixedMeta offset; the writer loops over every physical row 1..N by entry index
  (`core/mppwrite/assignment_growth.cpp:2347-2355`).
- A file with a deleted resource row whose entry carries 0x5C passes the plan rule and is refused
  by the writer only.  No wrong write (the writer refuses), but "each layer on its own" does not
  hold for that row, and no test has a tombstoned row.
- Fix: have the reader expose the session byte of every physical row (or a "any row not 0x1C"
  flag) and test a file with a tombstoned 0x5C row in both layers.

### R35cG1-7 (Low): the reader adds a second whole-table VarMeta scan per task

- Where: `core/mpp/reader.cpp:2171-2176` (`var_keys`), beside the existing per-task scan at
  `:2074`.
- Every file open now walks `table.entries` twice per task, O(tasks x entries), for a field
  only Calculate Project's two-leaf rule reads.  Harmless on the fixtures, noticeable on a
  large plan.
- Fix: collect `var_keys` inside the existing loop at `:2074` (its `entry.uid == uid` branch),
  then sort once.
