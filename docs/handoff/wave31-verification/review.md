# Wave 31 independent review (`git diff wave30..wave31`, tip 27332b3)

This was a read-only review. The only repo file written is this one. Scratch
scripts (a minimal CFB reader and fixture privacy scan, plus a cross-check of
the committed usage facts against the raw COM output) live in the session
scratchpad. I formed the findings below before reading the record's own CLOSE
review section. The last section says where the two reviews differ.

## Verdict

G0 closed R30-1 and R30-4 as written, and R30-3 for every path the shell can
build. R30-2 is only partly closed. The new file-wide refusal reads the saved
file only, so edits queued in the same save get past it, in either order:

- a percent, Mark on Track or Update edit on a row outside the moved subtree;
- a typed Start (an SNET pin) on such a row.

The save then writes progress or a constraint beside slack that was computed
for a 0 %, ASAP row. That is the exact shape W31-0.2 refuses when it is already
stored in the file.

The Fill Down measurement is careful. It also proves something the wave did
not act on. The same `save_date_edits` writer is still reached by typing or
pasting a Start, and the committed pair shows that writer cannot reproduce
Project's Start-edit save.

The View-tab commands do not touch the saved `.mpp`:

- Sort, Outline, Tables and Zoom are session-only.
- I found no view-state writer they feed.
- Window and Macros are disabled with the exact tooltip.

The new session Sort does have one data-path defect. Shift-click, Copy, Cut and
Paste still work in file order, so a multi-row paste in a sorted sheet writes
rows other than the ones shown. Those rows are then saved.

The Resource Sheet and usage readers are bounds-checked. The usage gate compares
against committed COM TimeScaleData: 72/72 series match the raw oracle output.
The six new fixtures are clean.

**Counts: Critical 0 / High 3 / Medium 3 / Low 6.**

## How R30-1 .. R30-4 stand

| finding | status at 27332b3 | evidence |
|---|---|---|
| R30-1 (link added after a queued automatic outline edit) | **Closed.** `validate_link_add` refuses while an applied outline edit carries slack. `push(add_link)` refuses the same queue, and `save_structural_edits` refuses a queue holding both before the first write. The reverse order is still refused. The only shell entry point that adds a link is the Predecessors dialog, reached from the ribbon Link Tasks button and the menu: Task Information and Task Form only point to it, and the sheet's Predecessors column is read-only (`task_column_editable`). Undo/redo cannot rebuild the queue, because `push` clears the redo stack. One test covers each of the four layers, and each layer's check is independently asserted. | `app/field_link_edits.cpp:417-433`; `app/edit_model.cpp:2505-2516, 3440-3460`; `app/app_edits_links_tests.cpp` `test_pending_outline_link_order`; `app_qt/dialogs.cpp:600-615`; `app_qt/main_window.cpp:2186-2190` |
| R30-2 (automatic outline save on file shapes no pair measured) | **Partly closed.** `check_automatic_outline_file` refuses stored progress or actuals, a non-ASAP constraint, a deadline and an outside summary, file-wide, in both automatic branches. Each refusal has a patched-copy test. Still open: (1) the same shapes queued in the same save are not refused (R31-1, High); (2) task calendars, inactive rows, manual rows elsewhere and elapsed or estimated durations are not pinned (R31-5). | `app/edit_model.cpp:979-1065, 2090-2095, 2249-2253`; `app/app_edits_outline_tests.cpp` `check_automatic_outline_file_shape` |
| R30-3 (percent/date edits with progress off the edited chain) | **Closed** for the shell. `check_summary_rollup_scope` refuses any live row outside the edited set with stored percent > 0, except Update Project's Entire project scope. `push(set_percent)` re-runs `make_percent_edit` on the display plan, so a second queued percent on another row is also refused. Test (e) now asserts the refusal. Residual: the exception flag is caller-set (the record's C2), and only `percent_complete` is seen (R31-12). | `app/edit_model.cpp:565-588, 2550-2575`; `app/app_edits_actuals_tests.cpp` (e) |
| R30-4 (Fill Down an unmeasured write) | **Closed as to Fill.** The pair, control and null are committed and documented with hashes. The Fill control is disabled (`ribbon_bar.cpp:348`), and the generic disabled path gives it the exact tooltip `not implemented in this port` (`ribbon_bar.cpp:686, 1096`). The row-menu entry is disabled the same way (`main_window.cpp:4869`). No shortcut, keytip or dynamic enable reaches `fill_down()`. `qt_fill_gate` asserts the disabled state, the tooltip and an inert click, and the summary refusal stays as defence in depth. **But** the pair shows that the shared Start writer diverges from Project, and typing or pasting a Start still reaches it (R31-2). | `app_qt/ribbon_bar.cpp:336-349`; `app_qt/tests/fill_gate_test.cpp`; `tests/data/wave31-fill*.mpp`; `docs/handoff/wave31-complete.md:66-100` |

## Findings

| id | severity | file:line | finding | evidence | suggested fix |
|---|---|---|---|---|---|
| R31-1 | **High** | `app/edit_model.cpp:836-857` (`outline_edit_pending_for`); `app/edit_model.cpp:2760-2830` (`push`, outline); `app/edit_model.cpp:979-1065` (`check_automatic_outline_file`); `app/edit_model.cpp:3440-3460, 3592` (`save_structural_edits`) | **The R30-2 refusal checks only the file on disk, so a queued edit in the same save gets past it, in either order.** On the 3-row automatic control: queue Indent of uid 2 under uid 1 (automatic, carries slack for uids 1-3), then type 40 in uid 3's % Complete cell, a measured "flat automatic leaf 0->40". `outline_edit_pending_for` only names the moved subtree, the anchor and ancestors, so the percent push is accepted. In the other order, `make_outline_edit` reads the file (still 0 %), and push's `pending_actuals` is checked only against the moved and anchor rows. The save writes uid 3's actuals at step 5a and then, at step 5b, the slack and late dates computed for a 0 % row. This is the "progressed row elsewhere" shape W31-0.2 refuses when it is stored. A typed Start on uid 3 (an SNET pin, so a non-ASAP constraint) is not cross-checked against a pending outline edit either. `save_structural_edits` refuses start edits only beside a task-mode edit, and it writes them after the outline. | Code read; not run in the app. `grep pending_actuals app/edit_model.cpp` shows only the subtree and anchor checks (`:2022-2027, 2211-2214, 2788-2825`). There is no outline/start exclusivity in `save_structural_edits`. | In `push`/`make_outline_edit`, refuse an automatic outline edit while any `set_percent`/`update_actuals` is queued, or any start edit is pending. Refuse a percent, actuals or Start commit while an automatic outline edit is queued (all rows, not just the moved ones). Mirror both in `save_structural_edits`. Add a test for each order. |
| R31-2 | **High** (pre-existing path, newly evidenced) | `app/edit_model.cpp:4083-4093` (`save_date_edits` call); `app/view_model.cpp:801-805` (`task_column_editable`: Start editable); `app_qt/main_window.cpp:4360-4410` (Paste) | **Typing or pasting a Start still saves through the writer the wave just measured as unable to reproduce Project's Start-edit save.** W31-0.4's committed pair is a Start change on an automatic 0 % leaf. In the same save, Project also rewrites the row's calculated and effective dates (+98/+102/+106/+110) and slack, rolls up the project row, appends a Var2 record and changes DSI. The record concludes that `save_date_edits` writes "only the 6-byte constraint window ... (plus the manual bit), so this pair cannot pass `tests/save_pair_noise.h`" and disables Fill. A typed Start on the same row is the same edit through the same writer, and it stays enabled. The port's save leaves stale stored Start/Finish behind a new SNET date, and the port's own reader shows those stale dates on reopen. | `docs/handoff/wave31-complete.md:84-97`; `build/w31/W31-0.4-fill-pair.json`; `collect_start_edits` feeds `save_document` (`main_window.cpp:3192-3194`). | Apply rule 1 consistently: make the Start column read-only with the decision-9 reason until a full Start writer passes the committed pair, or extend the writer and compare `wave31-fill.mpp` through the noise mask. Do the same for Paste into Start. |
| R31-3 | **High** (code read) | `app_qt/plan_document.cpp:1035-1050` (sort lives only in the display order); `app_qt/main_window.cpp:772-781` (Shift-click `extend_to(record row)`); `core/sheet/sheet.cpp:2461-2520` (paste walks `visible_row(ordinal)` in record order); `app_qt/main_window.cpp:1920` (Paste enabled under sort) | **Under the new session Sort, range selection, Copy, Cut and Paste work in file order, not the displayed order.** Sort reorders only `PlanDocument`'s display rows. The core sheet's selection rectangle and `paste` iterate the core's own visible rows (record order minus collapsed). So a 3-row block pasted at a displayed row lands on the next three file rows, which can be anywhere on screen. A Shift-click range likewise spans rows the user did not see selected. Name and Start writes reach the saved file, so the user's values are saved into the wrong tasks. Filter had the same gap, but it was less visible; Sort makes it routine. No test pastes under sort. | Code read; `grep -n paste app_qt/tests/data_commands_test.cpp` finds no case. | Disable multi-cell Paste, Cut and range extension while a sort or filter is active. Alternatively, map the block through `display_rows()` before calling the core, and test it under ID-descending sort. |
| R31-4 | Medium (unverified against Project) | `app_qt/main_window.cpp:2826-2860` (indent/outdent); insert path | **Structural commands ignore the session sort.** Indent moves the row under its preceding sibling in file order, which under a sort is not the row displayed above it. Insert likewise places the row by file position. The saved shape is still a measured one, but it is not what the user aimed at. Project's own behaviour with a sorted view (it may disable outline commands) was not measured. | Code read; `sort_active()` is consulted only at `main_window.cpp:1287`. | Refuse Indent, Outdent and Insert while a non-ID sort is active ("clear the sort first"), or measure Project's behaviour. |
| R31-5 | Medium | `app/edit_model.cpp:870-940` (`build_outline_slack`), `:979-1065` | **The W31-0.2 file shape still leaves several conditions outside all three pairs unpinned.** `build_outline_slack` computes every automatic row's slack on the project calendar. A row with a task calendar, an inactive row, an elapsed or estimated duration, or a manual row elsewhere (the hybrid pair kept its stale slack, which shows Project treats manual rows specially) passes the check. All three pairs are the three-row base. | Code read: `check_automatic_outline_file` reads percent, actuals, constraint, deadline and summary flags only. | Also refuse `kCalendarUid != -1`, inactive, elapsed/estimated duration and manual rows outside the edit, with a test each. Alternatively, measure one more pair. |
| R31-6 | Medium (performance, timing unverified) | `app/usage_grid.cpp:170-185` (`column_work`) | Each assignment's day buckets are matched to columns by a linear scan from column 0, which is O(days x columns) per assignment. With day columns over a multi-year file and hundreds of assignments this runs to around 10^9 steps, so the window would stall when the usage view opens. | Code read. | Walk days and columns together (both are sorted), or binary-search the column. |
| R31-7 | Low | `app/view_model.cpp:316-375` (`resource_load`) | Overallocation (the red row) is a port-computed sweep of concurrent units against MaxUnits. Project's stored flag is not read. The rule was measured on one shape (three concurrent 100 % assignments), and the header admits calendars are ignored. Assignments that overlap only in non-working time, or units contoured by a resource calendar, can be flagged differently from Project. | Header comment `app/view_model.h:126-144`; `build/w31/W31-G3-resource-sheet.json` (one overallocated row). | Read Project's stored overallocation bit if the file carries one. Otherwise label the rule as approximate and add a second measured shape. |
| R31-8 | Low | `app_qt/tests/resource_sheet_gate_test.cpp:66-90` | The Resource Sheet gate's COM facts exist only as constants in the test and as prose in `tests/data/README.md:92`. The raw COM JSON is the git-ignored `build/w31/W31-G3-resource-sheet.json`. Usage and Zoom commit theirs under `tests/data/reference/`. I checked the 14 widths and field ids against the raw JSON and they match, so this is provenance, not a wrong value. | `git ls-files tests/data/reference` lists no resource-sheet JSON. | Commit a `wave31-resource-sheet.measures.json` and have the gate read it. |
| R31-9 | Low | `app/app_edits_links_tests.cpp` (`test_pending_outline_link_order`), `app/app_edits_outline_tests.cpp` (`check_automatic_outline_file_shape`), `tests/mpp_reader_tests.cpp` (`test_resource_sheet_fixture`) | Four new SKIP-and-pass returns on a missing committed fixture, including the only tests of two G0 refusals (R30-9 carried forward). The `#else` stub prints SKIP too. | `git diff wave30..wave31 -- app app_qt tests core \| grep '^+.*SKIP'` gives 4 hits. | Fail on a missing committed fixture. |
| R31-10 | Low | commits `d9d8d8d`, `659fd97` | The commit messages quote "app_edits_outline 3201 checks" and "app_edits_actuals 3782 checks, 50 expected failures". The cited `build/w31/W31-0.2-*.log` / `W31-0.3-*.log` hold only ctest summaries, and no local W31-0.x log contains either number. The record's A4 removed such counts from the record, but history keeps them (the R30-12 pattern). | `grep -l "3201\|3782" build/w31/W31-0.*.log` finds nothing. | Process: log the direct-run output before quoting it. |
| R31-11 | Low | `docs/handoff/wave31-brief.md` (one added line) | The brief, added in the range, names the operator by first name once ("waves 27-31 ... approved"), contrary to rule 6. The record, commit messages and fixtures are clean. | Term scan over the added lines: 1 hit, in that file. | Write "the operator". |
| R31-12 | Low | `app/edit_model.cpp:576-578`; `app_qt/tests/usage_gate_test.cpp:169-182` | (a) W31-0.3's outside-progress test sees only `percent_complete > 0`, unlike W31-0.2's check, which also reads ActualStart, ActualFinish and ActualDuration. A row with stored actual words at 0 % is not seen. (b) `check_series` accepts `got.size() >= want.size()`, and an absent JSON series passes vacuously. `grid.periods.size() == 5` limits the effect. | Code read. | (a) Reuse W31-0.2's actual-word test. (b) Require equal sizes and a non-empty expected series. |

## Checks that passed (spot-checked)

- **View-tab state never reaches a save.**
  - Sort, Outline and Tables change only `PlanDocument`'s display order, the sheet's collapse state and the column set. `records_` and the loaded plan are unchanged. Nothing in `edit_model.cpp` or `core/mppwrite` writes collapse, sort or applied-table state (`grep -i collapse` finds only comments).
  - The Zoom dialog, Entire Project and Selected Tasks set `session_timescale_`, which only `build_gantt` reads. There is no timescale writer in `app/`, `app_qt/` or `core/`.
  - Table/Date Format builds its 214/CTable edit from its own dialog, not from the session table.
  - New Window, Switch Windows, Arrange All, Hide and Macros are disabled, have their own `RibbonCommand` ids with no handler, and carry the exact tooltip. Network Diagram, Calendar and Other Views likewise, with backlog reasons.
  - Highlight and Group by are disabled combos. The backlog generator lists commands only, so the brief's condition does not apply (as the record says).
- **Reader changes.**
  - Resource liveness now also requires FixedMeta +0 == 0 (or no metadata stream), the same rule as tasks and MPP12 resources.
  - The rate and accrual words are read with `f64_at`/`u32_at`, which return 0 when out of range. Group and Code go through `read_text_field`.
  - `parse_usage_view_style` refuses records shorter than 1182 bytes, which covers every offset it reads. `parse_view_text_styles` refuses records under 634 bytes, and the last slot it reads ends at byte 631.
  - `record_data_from_view` bounds-checks the payload span.
  - `gantt_style.cpp` is a pure extraction of the text-style loop. The Gantt view's decode is unchanged.
- **The usage gate is compared with Project.** Every `com.per_period` series in the committed `tests/data/reference/wave31-usage.measures.json` matches the raw COM `TimeScaleData` in `build/w31/W31-G4-usage.json` (72/72 series, my script). The gate fails when the JSON is missing or unparsable. The Resource Sheet gate's 14 stored widths and field ids match COM's `TableFields`.
- **Privacy.** All six new fixtures (`wave31-fill{,-control,-control-null}`, `-resource-sheet`, `-tables`, `-usage`) were scanned in two forms: the raw file and every reassembled CFB stream. The scan covered ANSI, UTF-16LE (both alignments) and UTF-16BE; drive, UNC and e-mail patterns; and operator, company and profile terms. The only paths are the neutral `C:\w31\<case>\...` root (Props14, 214/CEdl/Var2Data) and Project's own `i://mainpage.htm`. SummaryInformation has Author/LastAuthor "Fixture Builder" and titles "Synthetic ... Fixture". DocumentSummaryInformation has Manager "Fixture Builder" and Company "Fixture Company". The two committed measures JSONs have no hits. The added doc lines contain no personal path; the only drive paths are `C:/w3x` and the repo root.
- **Gate claims.** `build/w31/W31-CLOSE-fresh-gates.log` shows app 25/25, app_qt 37/37 and every exit line the record quotes. The G0 per-commit ctest logs show 22/22 and 32/32 as claimed.

## Where this review and the record's CLOSE review differ

The record's three reviews (1 Critical / 4 High / 3 Medium / 8 Low) found and
fixed record, CI and usage-teardown problems. I agree with those dispositions
and with its queued Lows C1, C2, S1 and S2; C2 is folded into the R30-3 row
above. It did not report R31-1 (queued-edit bypass of W31-0.2), R31-2 (typed
and pasted Start through the writer the Fill pair disproved) or R31-3 (Paste
and Cut in file order under session Sort). Its G0 section says "Nothing new".
