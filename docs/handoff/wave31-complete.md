# Wave 31 - the completion record

One table row per brief item.  A group ends only when its exact
`WAVE-31-<group>-COMPLETE` line is appended below (wave-31-brief section 1).
Deferrals need the attempt evidence the standing rules and the brief name.

## G0 - the wave-30 review's High findings

From `docs/handoff/wave30-verification/review.md` (0 Critical, 4 High).

| item | finding | status | evidence |
|---|---|---|---|
| W31-0.1 | R30-1 (High): a link added after a queued automatic outline edit | **DONE** | commit `33fbb3d`; gates `build/w31/W31-0.1-*.log` |
| W31-0.2 | R30-2 (High): the automatic outline save on files unlike the pairs | **DONE** | commit `d9d8d8d`; gates `build/w31/W31-0.2-*.log` |
| W31-0.3 | R30-3 (High): percent/date edits with progress off the edited chain | **DONE** | commit `659fd97`; gates `build/w31/W31-0.3-*.log` |
| W31-0.4 | R30-4 (High): Fill Down is an unmeasured write | **DONE** | commit `9c3213e`; gates `build/w31/W31-0.4-*.log`; measured pair `wave31-fill-control.mpp` / `wave31-fill.mpp` / `wave31-fill-control-null.mpp` |

No G0 item was deferred.  Items 0.1-0.3 narrow an enabled save path to the
shapes its Project pairs measured, or refuse every other shape up front; item
0.4 measured Project's Fill Down pair, and the measurement shows the port's
start writer cannot reproduce the save, so the command stays disabled with
decision 9's exact tooltip (the brief's "until then" rule).  The group found no
new Critical or High finding.

W31-0.1 detail:

- An automatic (slack-carrying) outline edit computes its slack and late dates
  on the unlinked file, and the save writes them at step 5b, after the link
  add at step 2.  Add Link was not checked against a queued outline edit, so
  the shape W30-0.2 refuses in the other order could be saved.
- `validate_link_add` refuses while an applied outline_indent/outdent carries
  a non-empty slack list (the Predecessors dialog path), and `push(add_link)`
  refuses the same queue for a caller that skipped validate;
  `save_structural_edits` refuses a hand-built queue holding both, before the
  first write.
- The reverse order is unchanged and still refused: `make_outline_edit` sees
  the pending link in the display plan and `push` sees the plan's links.
- Tests: `app_edits_links` covers both orders and the save-level refusal on
  `wave29-outline-auto-indent-control.mpp`; the targeted run is 5/5.

W31-0.2 detail:

- `check_automatic_outline_file` reads the saved task table and refuses, file
  wide, a progressed/completed row or stored actuals, a constraint type other
  than ASAP, a deadline (+182), and a stored or structural summary outside the
  moved subtree and anchor.  The moved subtree may hold summaries: the
  automatic-summary pair measured one.
- Both automatic branches (indent and outdent) call it after the linked-file
  refusal, so a refused shape builds no edit.
- Tests: `app_edits_outline` patches one word of one row on copies of the
  committed controls and checks a refusal per shape, plus the outdent path.

W31-0.3 detail:

- `check_summary_rollup_scope` takes the scope flag and, unless the edit is
  the measured Entire project date update, refuses any live row outside the
  edited uid set whose stored percent is above zero (the project row is the
  measured roll-up target and is not counted).  The ancestor-chain rules run
  first.
- `make_percent_edit`, `make_update_actuals_edit` (which now computes its
  scope flag before the check) and both `push()` paths pass the flag.
- Test (e) of `check_summary_rollup_scope`, which used to accept a 40 % row
  outside the edited set, now asserts the refusal, the message, the push
  refusal and the Entire project exception; a patched flat control proves the
  date-edit half (`app_edits_actuals` with its 50 expected failures).

W31-0.4 detail:

- Measurement: `scripts/oracle_w31_fill.py`.  FileNew synthetic project,
  neutral Author/Manager/Company/UserName/Title, ProjectStart 2026-01-05
  08:00; Task A 2400 min with Start 2026-01-12 08:00 (stored as SNET type 4),
  Task B 1440 min at the project start; automatic calculation.  The seed is
  saved and reopened (this build's first save after FileNew is not
  reopen-stable, W30-2.4), the control is saved from that session, then
  `SelectTaskField(1, "Start", False, 0, 1)` and `FillDown(True)` run and the
  edit is saved.  The null save is a no-edit save from a COPY.
- Pair facts: the control's Task B is 2026-01-05 08:00 -> 2026-01-07 17:00;
  the edit's Task B is 2026-01-12 08:00 -> 2026-01-14 17:00 with SNET type 4
  and total slack 9600 ticks; Task A is untouched; the project row rolls up
  from 4800 to 2400 min at finish 2026-01-16 17:00.  Reopens and the null
  facts match.  Evidence: `build/w31/W31-0.4-oracle.log` (exit 0) and
  `build/w31/W31-0.4-fill-pair.json`.
- Noise-mask comparison: the control-vs-null difference is empty (Task
  FixedData 0 bytes, `DocumentSummaryInformation` byte-identical), so the
  measured effect is Project's edit alone: Task FixedData 25 bytes (the
  edited row's constraint window +64/+66 AND calculated/effective dates
  +98/+102/+106/+110, slack +74/+186/+190, +169/+170; the project row's
  duration/remaining +84/+88, start +96/+104, total slack +186), FixedMeta
  byte 253, Fixed2Data 24 bytes, Var2Data 350 -> 366 (one 16-byte record
  APPENDED), VarMeta 9 bytes, TBkndAssn FixedData 4 bytes, Props 10 bytes,
  and `DocumentSummaryInformation` 1104 -> 1100 with 181 effect bytes.
  `SummaryInformation` is byte-identical.
- Conclusion: `save_date_edits` may change only the 6-byte constraint window
  of an edited row (plus the manual bit), so this pair cannot pass
  `tests/save_pair_noise.h` and the port has no writer for the row rewrite,
  the Var2 append or the DSI summary strings.  Standing rule 1 keeps Fill
  Down disabled.
- UI: the ribbon Fill control is disabled (`app_qt/ribbon_bar.cpp`), the pin's
  enabled column is 0 (`app_qt/ribbon_pin.tsv`), and the row-menu Fill Down is
  disabled with decision 9's exact tooltip `not implemented in this port`.
  The `RibbonCommand::fill_down` handler stays behind the disabled control;
  `fill_down()` refuses a summary row before staging anything (R30-4's U1).
- Tests: `qt_fill_gate` asserts the control exists, stays wired to
  `RibbonCommand::fill_down`, is disabled with the exact tooltip, and stages
  nothing (clean document, no undo step); the row-menu action is disabled with
  the same tooltip; and the committed pair opens read-only to pin the facts a
  future full start writer must reproduce (control and edit Task B starts and
  finishes; automatic 0 % leaves on the project calendar).
  `qt_ribbon_inventory` drops the stale enabled-wiring entry and its pin check
  asserts the disabled state and tooltip.
- Fixtures: documented in `tests/data/README.md` (provenance, COM facts, every
  measured effect byte, all three SHA-256) and pinned in
  `scripts/check_fixtures.py`.

Per-commit gates (logs under `build/w31/`, named `W31-0.x-*.log`; all four
items' logs report scan HEAD 0 files / 0 hits, guard `wave30..HEAD` exit 0,
hex grep no match and fixtures OK):

| commit | builds | item tests | app ctest |
|---|---|---|---|
| W31-0.1 `33fbb3d` | app build exit 0 | targeted 5/5: `app_edit`, `app_edits_links`, `app_edits_actuals`, `app_edits_outline`, `app_smoke` | not run (targeted) |
| W31-0.2 `d9d8d8d` | app build exit 0 | `app_edits_outline` passed | 22/22 |
| W31-0.3 `659fd97` | app build exit 0; app_qt build exit 0 | `app_edits_actuals` passed with its 50 expected failures; app_qt 32/32 | 22/22 |
| W31-0.4 `9c3213e` | app build exit 0; app_qt build exit 0 | `qt_fill_gate`, `qt_ribbon_inventory`; app_qt 32/32 | 22/22 |

G0 group gates (fresh trees configured from scratch on `9c3213e` under
`build/w31-fresh{,-app,-qt}`, log `build/w31/W31-G0-fresh-gates.log`; the run
started 22:48 and finished 22:58 with no lock retries):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 checks / 420 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app ctest / app_qt ctest | 22/22 / 32/32 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (134 documented hashes, 170 tracked `.mpp`); paths OK (217 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `wave30..HEAD` / `guard_names wave30..HEAD` / hex grep | 0 files, 0 hits / 0 commits / exit 0 / no matches (exit 1, expected) |

The numbers belong to `9c3213e`; the marker commit that follows is docs-only.

## Found, not fixed

- Nothing new.  G0 raised no Critical, High, Medium or Low finding beyond the
  four items above.
- Residual note (not a finding): Fill Down's summary-row refusal is
  defence-in-depth behind the disabled control; the UI cannot reach it, so the
  gates assert the disabled state, tooltip and inert click rather than the
  refusal itself.

WAVE-31-G0-COMPLETE

## G1 - View tab: data commands

| item | finding | status | evidence |
|---|---|---|---|
| W31-1.1 | Sort (ID, Start, Finish, Priority, Name; the Sort dialog) | **DONE (session-only; file bytes kept)** | commit `0707d4d`; gates `build/w31/W31-1.1-sort.log` |
| W31-1.2 | Outline (show level 1..9, all subtasks) | **DONE (session-only; file bytes kept)** | commit `eb39bd4`; gates `build/w31/W31-1.2-outline.log` |
| W31-1.3 | Tables (switch between the file's stored tables) | **DONE (session-only; file bytes kept)** | commit `2852bd6`; fixture `tests/data/wave31-tables.mpp`; gates `build/w31/W31-1.3-tables.log` |

All three items are done.  Project persists each of these view changes in the
file (sort order, applied table, outline level shown); per wave-31 brief
section 1 the port applies each to the loaded session only, and the gates
assert the file's SHA-256 and the RecordSet's row order are unchanged, so
no persisted view-state save path was measured or enabled.
The three commands are enabled on View > Data and wired to
`RibbonCommand::data_sort`, `data_outline` and `data_tables`; the pin, the
inventory and the generated backlog agree.  Group by and Highlight are
ribbon combos, not command controls: the generator lists only commands
("the combos ... are controls, not menu commands",
`app_qt/tools/command_backlog.py`), so the backlog does not list them
disabled and the brief's condition for changing them is not met; the pin
keeps both as they were (View Data combo rows with enabled 0).

W31-1.1 detail:

- The dialog (`app_qt/dialogs.cpp`, `SortDialog`) offers the brief's five
  keys -- ID, Start, Finish, Priority, Name -- one Order combo
  (Ascending/Descending) and Sort / Clear Sort / Cancel; the hint states
  that the sort is session-only and that children stay under their summary.
  `app::build_sort` builds one primary key; `app::sort_display_rows` runs the
  tested `core::view::apply_sort` with `keep_outline` and then keeps only the
  sheet's visible rows.  The RecordSet's OutlineLevel is 0-based while
  `apply_sort` expects Project's 1-based levels, so the sort runs on a
  short-lived copy whose app-only field `0x0b4fff02` carries level + 1; that
  field is never written to a file.  The synthetic project row (uid 0) stays
  first.
- `TaskRow` gains the reader's Priority word (Task.Priority `0x0b400019`,
  FixedData +78) and `make_record_set` exposes it; no other reader field
  changes.
- `PlanDocument` owns the definition and one display-order path
  (`ordered_display_rows()`: visible rows -> sort -> filter) used by both
  `display_rows()` and the Gantt `RowMap`, so sheet and chart agree.
  `SheetTableModel`'s display list activates for filter OR sort, otherwise a
  sort-only document would draw file order.
- Gates: `sort_tests` 36 checks; the dialog gate measures `SortDialog`;
  `qt_data_commands` asserts the sorted display order, the RowMap/Gantt
  order, the RecordSet's unchanged file order and the fixture's unchanged
  SHA-256.  app ctest 23/23 and app_qt ctest 33/33.
- Residual note (not a finding): the dialog sorts on one key, where
  Project's dialog offers up to three levels; the brief asked for the
  commonest keys and the single-key shape is what this item delivers.

W31-1.2 detail:

- `PlanDocument::show_outline_level(N)` expands everything, then collapses
  every summary at port outline level N-1, so exactly the rows through
  Project's (1-based) level N stay visible; levels outside 1..9 are refused.
  `show_all_subtasks()` is `expand_all()`.  Both reuse the tested
  `core::sheet::SheetModel` collapse state, which the display row list and
  the Gantt RowMap already follow; an active sort still orders the rows that
  remain (visible rows first, then sort).
- The shell's Outline menu (Task Sheet view) lists Level 1..9 and All
  Subtasks and reports each choice in the status bar.  The ribbon control is
  enabled and wired (`RibbonCommand::data_outline`); the pin, inventory and
  backlog agree.
- Gates: `qt_data_commands` adds the outline cases -- Level 1 -> uids
  {1,6}, Level 2 -> {1,2,5,6}, Levels 3 and 9 -> all six, 0 and 10 refused,
  All Subtasks restores everything, and under an ID-descending sort Level 1
  -> {6,1} while All Subtasks -> {6,1,5,2,4,3}.  The RecordSet stays in file
  order and the fixture's SHA-256 is unchanged in every case (59 checks at
  this commit; the same case runs in the 96-check target now).  app ctest
  23/23 and app_qt ctest 33/33.
- Residual note (not a finding): the shown level is session collapse state;
  a structural rebuild constructs a fresh `SheetModel`
  (`plan_document.cpp`, `rebuild_from_base`) and so returns to the sheet's
  default expansion.  The level shown is not persisted, which is the
  session-only choice above.

W31-1.3 detail:

- `PlanDocument` keeps every task table `214/CTable` stores, in file order.
  A table is a task table when it has a task-field column (`0x0b40xxxx`) and
  no resource-field column (`0x0c40xxxx`), so a Resource Sheet/Usage table is
  never offered in the Task Sheet.  The load still applies the file's first
  task table, so every earlier fixture renders as before.  An index the file
  does not store is refused.
- `select_table()` applies one through the sheet's stored columns
  (`SheetModel::set_columns` + `app::apply_edit_policy`) and keeps the
  RecordSet, display order, collapse state and selected task.
  `MainWindow::apply_table()` then rebuilds the adapter's display columns,
  the header band and the widths through `configure_task_columns()` before
  the row refresh and selection sync, so the sheet really shows the newly
  applied table.
- The shell's Tables menu is built from the stored captions (display
  caption preferred, Project's "&" accelerator stripped) with the applied
  table checked, and reports `table: <caption> (session only)`.  The ribbon
  control is enabled and wired (`RibbonCommand::data_tables`); the pin,
  inventory and backlog agree.
- Fixture: `tests/data/wave31-tables.mpp`, built by the new
  `scripts/oracle_w31_tables.py` through Project 2021 under the neutral root
  `C:/w31/tables` (a synthetic-base copy with neutral metadata;
  `TableEdit(Create=True, NewName="Review")`, then a replacement Text1
  column at ColumnPosition 0).  COM reopens it with `TaskTables.Count == 2`
  and Entry applied; the port decodes Entry (9 columns, second
  `0x0b4000cd`) and Review (9 columns, second `0x0b400033` titled "Text1").
  Provenance is in `tests/data/README.md` and the hash is pinned in
  `scripts/check_fixtures.py`.
- Gates: `qt_data_commands` now takes both fixtures and asserts the two
  captions, the applied table, Entry's Indicators column, Review's Text1
  title, a selection kept across two switches, unchanged display/RecordSet
  and file SHA-256, refusal of a missing index, and the one-table outline
  fixture listing only Entry (96 checks total).  app ctest 23/23 and app_qt
  ctest 33/33.  Fixture privacy check: no UTF-16LE/ASCII local path or
  operator name and no embedded `C:/w31` string.
- Residual note (not a finding): the initially applied table is the first
  stored task table; the file's applied-view table (which Project persists)
  is not read, and the applied table is not written.  Both belong to the
  unmeasured persisted view-state path above.

Per-commit gates (logs under `build/w31/`, named `W31-1.x-*.log`; all three
logs report scan HEAD 0 files / 0 hits, guard `wave30..HEAD` exit 0, hex
grep no match and fixtures OK; every commit's app and app_qt configure/build
exited 0):

| commit | item tests | app ctest | app_qt ctest |
|---|---|---|---|
| W31-1.1 `0707d4d` | `sort_tests` 36 checks; dialog gate; `qt_data_commands` sort case | 23/23 | 33/33 |
| W31-1.2 `eb39bd4` | `qt_data_commands` 59 checks | 23/23 | 33/33 |
| W31-1.3 `2852bd6` | `qt_data_commands` 96 checks; oracle and fixture privacy check | 23/23 | 33/33 |

G1 group gates (fresh trees configured from scratch on `2852bd6` under
`build/w31-fresh{,-app,-qt}`, log `build/w31/W31-G1-fresh-gates.log`; the
run started 23:42 and finished 23:50 with no lock retries):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 checks / 420 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app ctest / app_qt ctest | 23/23 / 33/33 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (135 documented hashes, 171 tracked `.mpp`); paths OK (218 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `wave30..HEAD` / `guard_names wave30..HEAD` / hex grep | 0 files, 0 hits / 0 commits / exit 0 / no matches (exit 1, expected) |

The numbers belong to `2852bd6`; the marker commit that follows is
docs-only.  G1 adds one tracked `.mpp` fixture (`wave31-tables.mpp`), taking
the count from 170 at G0's close to 171.

## Found, not fixed

- Nothing new.  G1 raised no Critical, High, Medium or Low finding beyond
  the three items above.
- Residual notes (not findings): the Sort dialog is single-key; the Outline
  level is lost on a structural rebuild; the initially applied table is the
  first stored task table rather than the file's applied-view table; and
  all three view-state save paths stay unmeasured by the session-only
  choice, with the file's bytes asserted unchanged.  Group by and
  Highlight are combos, so the generated backlog (which lists command
  controls only) does not list them disabled and the brief's condition does
  not apply.

WAVE-31-G1-COMPLETE

## G2 - View tab: zoom and window commands

| item | finding | status | evidence |
|---|---|---|---|
| W31-2.1 | Zoom (the Zoom dialog) | **DONE (session-only; file bytes kept)** | commit `712be75`; gates `build/w31/W31-2.1-*.log` |
| W31-2.2 | Entire Project and Selected Tasks (timescale fit, measured against Project on two fixtures) | **DONE (session-only; measured on both fixtures)** | commit `6439e70`; reference `tests/data/reference/w31-zoom.measures.json`; gates `build/w31/W31-2.2-*.log` |
| W31-2.3 | New Window, Switch Windows, Arrange All and Hide | **DONE (all four stay disabled with the exact tooltip)** | commit `25121ff`; gates `build/w31/W31-2.3-*.log` |
| W31-2.4 | Macros | **DONE (stays disabled with the exact tooltip; no VBA)** | commit `e477012`; gates `build/w31/W31-2.4-*.log` |

Items 2.1 and 2.2 are done as session-only view changes: Project persists
zoom (and the Window group's arrangement) in the file, so per wave-31 brief
section 1 the port applies each to the loaded session and keeps the file's
bytes; `qt_zoom_commands` asserts the fixture's SHA-256 is unchanged in every
case.  Items 2.3 and 2.4 cannot change the view at all in this shell and stay
disabled with decision 9's exact tooltip `not implemented in this port`.

W31-2.1 detail:

- Project's View > Zoom > Zoom... (dialog 756) is live: seven Zoom to choices
  (1 week, 2 weeks, 1 month, 3 months, Selected tasks, Entire project, Custom
  N Days/Weeks/Months), OK / Reset / Cancel, measured 480x420 by the dialog
  gate.  The dialog's default is Entire project, matching the captured dialog.
- `app/layout_model` owns the measured fit: 85 % of the chart pane, centred,
  origin floored to the unit start, window grown to contain the finish.  It
  also carries the fixed-span helper and the mean month used by the 1-month
  and 3-month choices.
- `PlanDocument` keeps the session-only timescale override behind
  `zoom_entire_project()`, `zoom_selected_tasks()` and `zoom_fixed_span()`; a
  shell zoom change, a changed zoom preference, a load or File > New clears
  it, and no path writes the file.
- The ribbon's Zoom button is enabled and wired to
  `RibbonCommand::zoom_dialog`; the pin, the inventory and the generated
  backlog agree.
- Gates: app ctest 23/23 and app_qt ctest 33/33; `layout_model_tests` 328
  checks; the Zoom dialog gate reports its pinned geometry and zero failures;
  `qt_ribbon_inventory` passes; scan/guard 0 hits; `check_fixtures.py` OK.
- Residual note (not a finding): the custom span did not apply in the measured
  Project run, so the dialog's Custom branch is covered by the port's own
  tests only; it is not compared with a Project window.

W31-2.2 detail:

- Measurement: `scripts/oracle_w31_zoom.py`, pure COM under
  `scripts/oracle_lock.py`.  On each fixture it selects one row
  (`Application.SelectRow(2)`; `ActiveSelection.Tasks` then reports task ID 3)
  and runs ZoomSelectedTasks FIRST, then ZoomEntireProject.  That order is the
  reproducible one: ZoomSelectedTasks raises `E_FAIL` after another zoom
  command and on multi-row selections.  Two fresh sessions per fixture agreed
  exactly.
- The numbers-only reference is
  `tests/data/reference/w31-zoom.measures.json` (SHA-256
  `C660012035C1FF922D82C35A33DE224F8DA781ED4B426A86E3E38206A8D111B5`,
  documented in `scripts/check_fixtures.py` and the reference README).
  Window 1500x800 pt requested; COM reports 1452x653 and a 1020 px chart pane.
  The four windows: base entire 2026-01-04 00:00 -> 2026-01-09 20:57:54
  (5.873541667 d, +0.689 % of the 85 % ideal), base selected task ID 3
  2026-01-04 06:00 -> 2026-01-06 13:44:42 (2.322708333 d, +0.815 %), outline
  entire 2026-12-10 12:00 -> 2026-12-19 03:51:48 (8.660972222 d, -0.179 %),
  outline selected task ID 3 2026-12-11 00:00 -> 2026-12-14 23:48:42
  (3.992152778 d, +0.543 %).  The Zoom dialog's preset windows are recorded
  alongside: 1 week 7.0 d, 2 weeks 14.0 d, 1 month 30.969652778 d, 3 months
  90.176458333 d.
- Project draws the span at 85 % of the pane and floors the origin to its
  bottom tier; on these windows that tier is finer than a day, which the
  port's day tier cannot express.  `qt_zoom_commands` therefore
  compares the measured window with the 85 % ideal within 1.5 %, then
  separately requires the port's exact rule: origin floored to the day and at
  or before the span, visible = max(ideal, span from the floored origin to the
  finish), the finish contained, and the whole window equal to
  `width / pixels_per_day`.  It also pins the no-selection fallback (Selected
  Tasks with nothing selected equals the Entire Project window), the dialog's
  fixed spans (weeks exact, mean months within 2 % of the measured windows for
  the recorded reason), `session_timescale_active()` and the fixture's
  unchanged SHA-256.
- Enable and wire the ribbon's Entire Project and Selected Tasks to the same
  fits (`RibbonCommand::zoom_entire`, `zoom_selected`); the pin and the
  generated backlog agree.
- Gates: app ctest 23/23; app_qt ctest 34/34 (`qt_zoom_commands` included);
  scan 0 hits; guard 0; no hex labels; fixtures OK (136 documented hashes).

W31-2.3 detail:

- Brief clause: "within the port's single-document shell: implement what the
  shell can honestly do; the rest stays disabled with the exact tooltip".
  New Window would need a second window onto the same document; the shell has
  one MainWindow and one PlanDocument and no view-sharing or per-window close
  semantics.  Switch Windows would list only that one window.  Arrange All has
  nothing to arrange with one window.  Hide would hide the only window and
  leave no UI.  For all four, the honest capability is none, so all four stay
  disabled with the exact decision-9 tooltip.
- Each control keeps its own `RibbonCommand` id (`window_new`, `window_switch`,
  `window_arrange`, `window_hide`) and each backlog row names the missing
  capability instead of the generic "future" wording.  `qt_ribbon_inventory`
  pins the id, the disabled state and the exact tooltip for all four against
  the independent pin; the generated backlog and `ribbon_pin.tsv` agree.
- Gates: app ctest 23/23; app_qt ctest 34/34; scan 0 hits; guard 0; no hex
  labels; fixtures OK (136 documented hashes).

W31-2.4 detail:

- Project's Macros button opens the VBA macro dialog.  The port has no VBA
  engine, editor or macro storage, so there is no macro to list or run; the
  control stays disabled with the exact decision-9 tooltip and is wired to no
  handler.
- The control keeps its own `RibbonCommand` id (`macros`) and its backlog row
  says `engine: no VBA engine, so there is no macro to list or run`.
  `qt_ribbon_inventory` pins the id, the disabled state and the exact tooltip
  against the independent pin.
- Gates: app ctest 23/23; app_qt ctest 34/34; scan 0 hits; guard 0; no hex
  labels; fixtures OK (136 documented hashes).

Per-commit gates (logs under `build/w31/`, named `W31-2.x-*.log`; every
commit's scan reports 0 files / 0 hits, guard exits 0, hex grep finds no match
and `check_fixtures.py` is OK; every commit's app and app_qt configure/build
exit 0).  W31-2.4's build and test evidence is the group fresh-tree run at
`e477012`; W31-2.3's were re-run in a detached worktree at `25121ff` so its log
exists beside the commit.

| commit | item tests | app ctest | app_qt ctest |
|---|---|---|---|
| W31-2.1 `712be75` | `layout_model_tests` 328 checks; Zoom dialog gate (480x420); `qt_ribbon_inventory` | 23/23 | 33/33 |
| W31-2.2 `6439e70` | `qt_zoom_commands`; oracle two-session agreement on both fixtures | 23/23 | 34/34 |
| W31-2.3 `25121ff` | `qt_ribbon_inventory` (Window ids, disabled state, exact tooltip) | 23/23 | 34/34 |
| W31-2.4 `e477012` | `qt_ribbon_inventory` (Macros id, disabled state, exact tooltip) | 23/23 | 34/34 |

G2 group gates (fresh trees configured from scratch on `e477012` under
`build/w31-fresh{,-app,-qt}`, log `build/w31/W31-G2-fresh-gates.log`; the run
started 01:11 and finished 01:19 with no lock retries):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 checks / 420 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app ctest / app_qt ctest | 23/23 / 34/34 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (136 documented hashes, 171 tracked `.mpp`); paths OK (219 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `wave30..HEAD` / `guard_names wave30..HEAD` / hex grep | 0 files, 0 hits / 0 commits / exit 0 / no matches (exit 1, expected) |

The numbers belong to `e477012`; the marker commit that follows is docs-only.

## Found, not fixed

- Nothing new.  G2 raised no Critical, High, Medium or Low finding beyond the
  four items above.
- Residual notes (not findings): the port's day tier cannot express Project's
  sub-day zoom margin, so the four measured windows are compared on the 85 %
  ideal (within 1.5 %) plus the port's own exact rule rather than pixel for
  pixel; the Zoom dialog's Custom span was not measured against Project
  because the measured run did not apply it; and the persisted zoom/arrangement
  save path stays unmeasured by the session-only choice for zoom and the
  disabled controls for the Window group.

WAVE-31-G2-COMPLETE

## G3 - Resource Sheet at Gantt fidelity

The brief's G3 is one prose item; the seven rows below are the work breakdown
`W31-3.1 .. W31-3.7` that closes it.  All of it is **presentation-only and
session-only**: the fixture's stored Resource Sheet view, its Entry table and
its Text Styles record are read and applied to the loaded session, and no path
writes them back (wave-31 brief section 1 -- the file's bytes are kept, nothing
is measured-and-written because nothing in this group changes a persisted view
setting).  The live gate opens the window and reads it; no save path was added
or enabled.

| item | finding | status | evidence |
|---|---|---|---|
| W31-3.1 | The synthetic Resource Sheet fixture and Project's rendering facts | **DONE** | commit `3aefddd`; fixture `tests/data/wave31-resource-sheet.mpp` SHA-256 `E8166300CA35B6042BEA4967C0E54894AC5ECE99613EBDE72EF907CFC093EF48`; COM dumps `build/w31/W31-G3-resource-sheet.json`, `build/w31/W31-G3-probe.json` (not committed) |
| W31-3.2 | Resource liveness from the FixedMeta state byte (the tombstoned base resource) | **DONE** | commit `25a45a5`; gates `build/w31/W31-3.2-*.log` |
| W31-3.3 | The reader exposes the stored Entry values (Group, Code, rates, Cost/Use, Accrue At, Base Calendar) | **DONE** | commit `af3efd4`; gates `build/w31/W31-3.3-*.log` |
| W31-3.4 | The stored resource table and the view's Text Styles record are read | **DONE** | commit `815b974`; gates `build/w31/W31-3.4-*.log` |
| W31-3.5 | The model carries the stored values and Project's computed PeakUnits/Overallocated | **DONE** | commit `d4adbb0`; gates `build/w31/W31-3.5-*.log` |
| W31-3.6 | The Resource Sheet paints the stored table, rows, fonts and indicators | **DONE (session-only; file bytes kept)** | commit `2b00548`; gates `build/w31/W31-3.6-*.log` |
| W31-3.7 | The live Qt gate measured against Project's COM facts | **DONE** | commit `bb0568d`; gates `build/w31/W31-3.7-*.log` |

No G3 item was deferred.  The group raised one Low finding (the un-stored Add
New Column stub) and two residual notes, all under "Found, not fixed" below;
no Critical or High finding was found.

W31-3.1 detail:

- `tests/data/wave31-resource-sheet.mpp` is `synthetic-base.mpp` saved by
  Project 2021 through COM with the Resource Sheet applied
  (`ViewApply("Resource Sheet")`) and the resource `Entry` table applied
  (`TableApply("Entry")`).  It was built by the committed read-only oracle
  `scripts/oracle_w31_resource_sheet.py` under the neutral root
  `C:/w31/resource-sheet` with neutral Author/Manager/Company/UserName/Title,
  and is documented (with the SHA-256 above) in `tests/data/README.md` and
  `scripts/check_fixtures.py`.
- The measured rows Project reopens: three Work resources, uids 2/3/4, IDs
  1/2/3, Crew A/B/C.  Crew A is on all three tasks at 100 % (PeakUnits 300 %,
  Overallocated), Crew B on task 1 and Crew C on tasks 2 and 3 at 50 % each
  (PeakUnits 100 %, not overallocated); Work 4800/2400/2400 minutes.  Group
  North/South/North, Code CR-A/B/C, Crew B's `$50.00/hr` and `$75.00/hr`
  rates and `$10.00` cost/use, Accrue At Prorated/Start/End, Base Calendar
  Standard.
- Project's TableFields reports the stored 14-column Entry table (ID 6,
  Indicators 6, Resource Name 20, Type 11, Material Label 13, Initials 11,
  Group 10, Max. Units 10, Std. Rate 11, Ovt. Rate 11, Cost/Use 11, Accrue At
  10, Base Calendar 14, Code 13 characters) plus an un-stored field -1 width
  20 Add New Column stub.
- The base file's placeholder resource was deleted; its FixedData row stays in
  the file as uid 1 with a FixedMeta state byte of 0x02.  Project reports three
  resources, which is the fact W31-3.2 makes the reader reproduce.
- `build/w31/W31-G3-probe.json` (read-only COM probe
  `scripts/oracle_w31_resource_sheet_probe.py`, added in W31-3.5) is the fact
  sheet the later gates compare against: the field order/char widths/alignments,
  `Table.RowHeight` 1 and `AdjustHeaderRowHeight` true, `Project.CurrencyCode`
  AUD with application symbol `$`, and every resource cell value.

W31-3.2 detail:

- `fixed_meta_coverage` now returns each row's state vector.  The resource
  decode skips a row whose state is neither live (0x00) nor "no FixedMeta
  stream", with a skipped-row reason naming the tombstone -- the same rule the
  MPP12 resource loop (`state != 0`) and the task decode (since W45) already
  apply.
- The committed-fixture test pins the three live resources (uids 2/3/4, IDs
  1/2/3), the absence of uid 1, and the tombstone skipped-row report, so the
  reader agrees with the Project reopen rather than inventing a fourth blank
  resource.  Core 53/53, `mpp_reader` 1112 checks.

W31-3.3 detail:

- Group and Code come from VarMeta keys `0x0c400003` / `0x0c40000a` (both
  formats).  MPP14 resource rows carry StandardRate at +16 and OvertimeRate at
  +24 (currency per hour), CostPerUse at +72 in hundredths, and AccrueAt at +80
  (1 Start, 2 End, 3 Prorated).  The offsets are the measured ones from two
  byte-diff probes (`build/w31/g3-rate-probe*.json`, not committed): Project
  normalised a typed `$400.00/day` to 50.0 at +16 and stored `$10.00` / `$1.23`
  as 1000.0 / 123.0 at +72.
- BaseCalendar is derived rather than a resource-row word: the resource-calendar
  row whose resource column is the resource's uid has base_uid 1, and Project
  prints calendar 1's stored name, `Standard`; `resolve_resource_calendars`
  resolves that name onto the resource.
- No MPP12 rate word has been measured, so MPP12 resources keep
  `cost_fields_stored == false` instead of reporting invented zeroes.
- The fixture test pins all three rows against the COM facts.  Core 53/53,
  `mpp_reader` 1136 checks.

W31-3.4 detail:

- `PlanDocument` keeps the file's resource tables separately from the task
  tables and selects the table the Resource Sheet ViewDef names in
  `0x2240000a`; on the fixture that is the measured 14-column `&Entry` table
  with its stored character widths.
- The view's own 1,530-byte `0x22400008` record parses as the 19 Text Styles
  slots (the same decoder as the Gantt record, no bar styles): slot 2 is
  ordinary resources (Calibri 11, automatic) and slot 3 is overallocated
  (bold Calibri 11, RGB 165,0,0).
- `core/viewstate` gains `parse_view_text_styles()` and
  `find_view_by_name()`; `parse_gantt_style()` shares the slot decoder;
  `app::read_resource_sheet_style()` joins the FONT_BASES/CV_iew seam.  This is
  presentation metadata read for the session; no write path touches it.
- The `qt_resource_sheet_gate` target starts here: it loads the fixture through
  the real `PlanDocument` and pins the stored table, the view selectors
  (`&Entry`, `&All Resources`) and both style slots.

W31-3.5 detail:

- The resource builder carries every stored Entry value the reader decodes --
  Group, Code, Material Label, Base Calendar, the per-hour rates, Cost/Use and
  Accrue At -- plus Project's computed PeakUnits and Overallocated.
  `build_resource_record_set` declares fifteen fields in stored file order
  (Indicators excluded, the two load fields appended); MPP12 rows store Null
  for the rate/cost cells.
- `app::resource_load()` sweeps the assignments naming a resource over their
  half-open `[start, finish)` intervals and returns the peak concurrent units.
  On the fixture Crew A peaks at 300 % (three concurrent 100 % assignments)
  and is overallocated while MaxUnits is 100 %; Crews B and C peak at 100 %
  and are not.
- `make_resource_view_records` renders the measured cell texts: Type and
  Accrue At words, `$50.00/hr` and `$10.00` through the measured `$` symbol
  (the file stores only the currency code AUD; `Project.CurrencySymbol` is the
  application option the probe read), and Max. Units as a percentage.
- `resource_sheet_columns(TableDef)` builds the columns from the file's own
  `214/CTable` resource table: file order, stored character widths, the
  measured COM alignments (ID centre, numeric right, text left) and Project's
  captions (436_1033 / 173_1033), with ID hidden as the row-number gutter and
  Indicators visible but captionless.  The registry fallback stays for a file
  without a stored resource table; the Qt shell picks the stored overload when
  `PlanDocument` decoded one.

W31-3.6 detail:

- The frozen gutter reads Resource.ID (`0x0c400000`), so the stored IDs 1/2/3
  appear even when the display order is not the storage order.
- Resource rows take the Resource Sheet view's own Text Styles: slot 3 (bold
  Calibri 11, RGB 165,0,0) when the stored/computed Overallocated flag is set,
  slot 2 (upright Calibri 11, automatic colour) otherwise.  The choice lives
  in `app/view_style.h` beside the task rule.
- Generic resource column widths use Project's `chars * unit + 3` rule with
  the unit measured from the view's own FONT_BASES (Segoe UI 10 -> 6 px): the
  fallback Entry list paints 39/123/69/69/63/69.  A file without the view
  keeps the stock 6 px unit; the usage grids are unchanged (G4 owns them).
- The captionless Indicators column paints Project's measured blue (i) header
  badge and an overallocated row paints the measured red person glyph (only
  that row); the Task Sheet's Indicators cells stay blank as before.
- The 22 px row pitch and 42 px caption band are unchanged.  Presentation
  only: the stored table, widths and styles are applied for the session and
  the file's bytes do not change.

W31-3.7 detail:

- The gate now opens the real `MainWindow` on the committed fixture, switches
  through the View menu's own Resource Sheet action, and reads the live widget
  tree back against `build/w31/W31-G3-probe.json`:
  - the 13 scrolling fields in COM TableFields order (ID is the frozen gutter;
    the -1 Add New Column sentinel is not a stored field), with the stored
    table/field captions;
  - Project's pixel widths from COM's character counts at this view's measured
    6 px/char plus 3 px: 39/123/69/81/69/63/63/69/69/69/63/87/81.  COM's
    TableField.Width reports characters (6/20/11/...), so the gate compares the
    live pixels with the measured Project rendering rule rather than pretending
    COM has a pixel-width property;
  - Resource.ID 1/2/3 in the frozen gutter, the 22 px row pitch, 60 px gutter
    and 42 px caption band;
  - every resource cell: names, Work type, empty Material Label, initials,
    Group, Max. Units 100 %, the measured currency rates/cost, Accrue At
    words, Standard calendar and codes;
  - only Crew A's row in slot 3's bold RGB 165,0,0; the others in slot 2.
- The gate is part of `qt_resource_sheet_gate`; Qt 35/35.

Per-commit gates (logs under `build/w31/`, named `W31-3.x-*.log`).  Every
commit's scan reports 0 files / 0 hits, guard exits 0, the hex grep finds no
match and `check_fixtures.py` is OK (137 documented hashes, 172 tracked
`.mpp`).  W31-3.1 is the fixture commit (fixture, provenance script,
documentation) and was gated by fixtures/privacy; the code commits rebuilt the
touched trees and ran their suites:

| commit | item tests | core ctest | app ctest | app_qt ctest |
|---|---|---|---|---|
| W31-3.1 `3aefddd` | fixture + oracle script; `check_fixtures.py` registers the hash | -- (fixture commit) | -- | -- |
| W31-3.2 `25a45a5` | `mpp_reader_tests` 1112 checks (tombstone row, three live resources) | 53/53 | -- | -- |
| W31-3.3 `af3efd4` | `mpp_reader_tests` 1136 checks (Group/Code/rates/cost/accrual/calendar) | 53/53 | -- | -- |
| W31-3.4 `815b974` | `viewstate_tests`, `view_style_tests`; `qt_resource_sheet_gate` (stored table, view selectors, slots 2/3) | 53/53 | 23/23 | 35/35 |
| W31-3.5 `d4adbb0` | `views_tests`, `resource_view_tests`; `qt_resource_sheet_gate` (cell values, PeakUnits/Overallocated) | 53/53 | 23/23 | 35/35 |
| W31-3.6 `2b00548` | `view_style_tests`; `qt_resource_sheet_gate` (gutter, slot-2/3 styles, badge/glyph pixels); `qt_view_gate` | 53/53 | 23/23 | 35/35 |
| W31-3.7 `bb0568d` | `qt_resource_sheet_gate` live MainWindow (fields, widths, IDs, values, styles) | -- | -- | 35/35 |

G3 group gates (fresh trees configured from scratch on `bb0568d` under
`build/w31-fresh{,-app,-qt}`, log `build/w31/W31-G3-fresh-gates.log`):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1136 checks / 456 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app ctest / app_qt ctest | 23/23 / 35/35 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (137 documented hashes, 172 tracked `.mpp`); paths OK (220 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `wave30..HEAD` / `guard_names wave30..HEAD` / hex grep | 0 files, 0 hits / 0 commits / exit 0 / no matches (exit 1, expected) |

The run started 03:26 and finished 03:34 with no lock retries.  The numbers
belong to `bb0568d`; the marker commit that follows is docs-only.

## Found, not fixed

- **Low (W31-3.1, left by design): the un-stored Add New Column stub.**  COM's
  `TableFields` enumerates a field -1, width 20 sentinel after Code.  It is not
  in the file's `214/CTable` (the stored table has 14 fields) and the captured
  Resource Sheet window never showed it, so the port's resource grid ends at
  Code and the live gate pins the 13 scrolling stored fields.  The task sheet
  already paints its own shell-appended Add New Column affordance; adding the
  resource one would be an unmeasured pixel change, so it is recorded here for
  a later pass rather than invented in this wave.
- Residual notes (not findings): `Table.RowHeight` is 1, Project's default
  encoding, and the port keeps the measured 22 px row pitch shared with the
  Gantt; the caption band keeps the shell's measured 11 pt header (shared with
  the Gantt), so the view's slot 1 row/column-title font (Segoe UI 9) is not
  applied to header captions in either view; the fixture has only the
  overallocation indicator, so the gate covers the indicator Project actually
  rendered there and claims no other indicator glyph.

WAVE-31-G3-COMPLETE

## G4 - Task Usage and Resource Usage at Gantt fidelity

The brief's G4 is one prose item; the five rows below are the work breakdown
`W31-4.1 .. W31-4.5` that closes it.  All of it is **presentation-only and
session-only**: the fixture's stored usage views -- their Details lists, their
5,199-byte timescale records and their table references -- are read and
applied to the loaded session, and no path writes them back (wave-31 brief
section 1: the file's bytes are kept).  The time-phased numbers are Project's
own COM `TimeScaleData`; the live gate opens the window and reads it.

| item | finding | status | evidence |
|---|---|---|---|
| W31-4.1 | The synthetic usage fixture and Project's time-phased facts | **DONE** | commit `6a8fd96`; fixture `tests/data/wave31-usage.mpp` SHA-256 `AF8DFE1DCC4EE319378FDBD3F454D398F5ACDC924BB66E9F0B4CC8879A1D600A`; measures `tests/data/reference/wave31-usage.measures.json` SHA-256 `A2312B41E0C32D97240A98E225329B4CFB3653FAC144F1B941EFEE10BDA10C7C`; COM dump `build/w31/W31-G4-usage.json` (not committed); gates `build/w31/W31-4.1-*.log` |
| W31-4.2 | The stored Details lists and timescale tiers decoded from `214/CV_iew` | **DONE** | commit `fe35886`; gates `build/w31/W31-4.2-*.log` |
| W31-4.3 | The usage grid's logical rows and per-period Work/Actual Work/Cost against COM `TimeScaleData` | **DONE** | commit `4c32189`; gates `build/w31/W31-4.3-*.log` |
| W31-4.4 | The measured presentation: detail sub-rows, captions, widths, label spans, gutter and tiers; typed Cost | **DONE (session-only; file bytes kept)** | commit `f0ffb6f`; gates `build/w31/W31-4.4-*.log` |
| W31-4.5 | The live Qt gate against the committed COM facts | **DONE** | commit `830d414`; gates `build/w31/W31-4.5-*.log` |

No G4 item was deferred.  The group raised one Low finding (the usage left
table's other stored fields are not drawn) and six residual notes, all under
"Found, not fixed" below; no Critical or High finding was found.

W31-4.1 detail:

- `tests/data/wave31-usage.mpp` is `synthetic-base.mpp` saved by Project 2021
  through COM (read-only, under `scripts/oracle_lock.py`) with the two usage
  views' details set, built by `scripts/oracle_w31_usage.py` under the neutral
  root `C:/w31/usage-fixture` with neutral Author/Manager/Company/UserName/
  Title, and documented (with the SHA-256 above) in `tests/data/README.md` and
  `scripts/check_fixtures.py`.
- The fixture reopens with three tasks (4800/2880/1920 minutes of Work; task 1
  is 100 % complete and carries 4800 actual minutes), three Work resources
  (stored IDs 1/2/3 at `$50.00`/`$60.00`/`$40.00` per hour, Accrue At
  Prorated) and six 100 % assignments (2400/2400/1440/1440/960/960 minutes;
  task 1 has resources 1 and 2, tasks 2 and 3 have resources 1 and 3).  The
  plan spans 5-9 January 2026.
- Both 5,199-byte `0x22400008` records store the same timescale: middle unit
  3 = weeks, bottom unit 2 = days, counts 1/1, formats 13/20, middle/bottom
  align 0/1, start 14400 (1984-01-01, the unset value), size 100.  Task Usage
  stores detail ids `00 02` (Work, Actual Work); Resource Usage stores `00 09`
  (Work, Cost).
- COM `TimeScaleData` (unit 4, days, 5-9 January) measures every task,
  assignment and resource bucket.  The committed facts include task 1's
  960 Work + 960 Actual Work minutes on each of the five days and 88,000 cost
  cents per day; task 2's 960/960/960 Work with the last two days blank;
  resource 1's 1440/1440/960/480/480 Work and
  120,000/120,000/80,000/40,000/40,000 cost cents; resource 2's 480 Work and
  48,000 cents on every day; resource 3's 960/960/480 Work.  Blank buckets
  are recorded as null.
- The measured pixels: 21 px row pitch, 42 px per logical row (two detail
  lines), 48 px Details column, 64 px Task Usage / 105 px Resource Usage day
  columns, 84 px Task Usage columns with Cost, 147/123 px label columns, and
  no total row or total column in Project's sheets (captured at the bottom and
  at later periods).

W31-4.2 detail:

- The Details list is `214/CV_iew` item `0x2240003c`: 111 raw selector bytes,
  no count prefix, 0xff-padded, and the first 0xff ends the list.  The
  measured selectors are `0x00` Work, `0x02` Actual Work and `0x09` Cost
  (Project's decompiled decoder names the item `VPID_ASSNFIELDORDER`).  Stock
  files store `00 48` (Task Usage) / `00 43` (Resource Usage) yet render one
  Work line; `DetailStylesAdd(24)` alone adds no line, so an id with no value
  rule is kept as `other`, draws nothing and warns -- Project's measured
  render, not an invented line.
- The timescale is the same record the Gantt reads, at the Gantt offsets:
  middle unit +1152, bottom unit +1154, counts +1156/+1158, formats
  +1160/+1162, bottom/middle alignment +1164/+1166, saved scroll origin +1176
  and size +1180.  The stored MIDDLE tier is the rendered TOP row and the
  stored BOTTOM tier the bottom row, as the captures' text rows measure.
- New `core/viewstate/usage_style.{h,cpp}` plus the generic
  `record_data_from_view()` accessor; the app seam
  `app::read_usage_view_style()` and PlanDocument's
  `task_usage_style()`/`resource_usage_style()`.  The fixture stores the
  table references as `Usa&ge` / `&Usage`; the accelerator placement is the
  file's and the tests pin those bytes.
- Tests: core `viewstate_tests` (including the usage-style cases),
  `app_usage_style` 31 checks; core ctest 53/53, app ctest 24/24, app_qt 35/35.

W31-4.3 detail:

- `app::UsageGrid` builds the measured logical rows: Task Usage draws each
  task then its assignment rows labelled with the RESOURCE; Resource Usage
  draws each resource then its assignment rows labelled with the TASK.  On
  the fixture that is nine logical data rows per view in the committed order
  (plus the port's Total row, the tenth logical row).
  Each row carries one value series per STORED detail (Work + Actual Work, or
  Work + Cost), and a file with no stored usage view gets one default Work
  detail.
- Every bucket is checked against `com.per_period`: the assignment rows are
  480 minutes in their tasks' periods at costs of 40,000/48,000 cents for
  task 1's two resources and 40,000/32,000 cents for the task 2 and task 3
  resource pairs; the Total row is
  9600 Work minutes (and 4800 Actual Work minutes) in Task Usage and 800,000
  cost cents in Resource Usage.  The measures key their series by Project's
  session Assignment.UniqueIDs (2097156-2097161); the file's assignment rows
  carry uids 4-9 and the test maps the measured structural order onto them.
- Value rules measured on the fixture: Work is the flat spread on the
  standard week; Actual Work is Work x the task's stored percent complete
  (exact at the fixture's 0 % and 100 %); Cost is Work x the resource's
  standard rate in cents and does not scale with completion -- the fixture's
  resources Accrue At Prorated, the measured mode.
- The port's final Total row (and its Total column) is an affordance, not a
  Project row: the measures record that Project's usage sheets have neither.
- Tests: `app_usage_grid` 214 checks 0 failures; app ctest 25/25, app_qt
  35/35.

W31-4.4 detail:

- `app_qt/UsageSheet` turns the logical grid into Project's physical rows --
  one 21 px sub-row per stored detail, two per logical row -- with the label
  merged across the pair and a Details caption per sub-row: "Work" then
  "Act. W" in Task Usage, "Work" then "Cost" in Resource Usage.
- The columns are the measured pixels: 147 px Task Usage / 123 px Resource
  Usage label, 48 px Details, 64 px Task Usage or 105 px Resource Usage day
  period (84 px when Task Usage stores Cost), 84 px port Total.  The header
  is the stored two-tier shape: "5 Jan '26" over M/T/W/T/F for the fixture's
  weeks-over-days record, single-band captions for label/Details/Total.
- Only parent rows carry a gutter ID (stored Task/Resource IDs 1/2/3);
  assignment rows and the Total row are blank.  The label column spans each
  logical row's two sub-rows; the Details column does not.
- Values are typed per detail: Work and Actual Work are Duration ticks
  (minutes x 10) and keep the app's duration-style preference; Cost is a
  Number in dollars (cents / 100) and renders through the sheet format as
  "$880.00" (task 1's first period: 8 h at $50 plus 8 h at $60), "$1,200.00"
  (resource 1's first period) and "$400.00" (an assignment bucket).
  Project's blank buckets stay blank.
- `MainWindow` installs the per-cell value-kind lookup, the custom gutter
  numbers, the label spans and the header tier cells with the usage sheet's
  lifetime, and restores the ordinary 22 px numbered presentation when
  another view opens.  A file with no stored usage view uses the shell's
  Day/Week/Month choice for the period, at the same 64 px minimum and 21 px
  pitch; a stored view's own timescale wins over that choice.
- `app::UsageGrid` carries the rendered tiers (stored middle = top, stored
  bottom = bottom), parent `display_id`/`numbered`, and the fallback period;
  the stored unit shapes this wave cannot draw fall back with a warning.
- Tests: `app_usage_grid` 245 checks; `qt_usage_gate`'s static sections
  (stored views, COM per-period equality, sheet shape, typed values and the
  Cost width probe); the updated `qt_view_gate` fallback expectations; app
  ctest 25/25, app_qt 36/36.

W31-4.5 detail:

- `qt_usage_gate` now drives the real `MainWindow` through the View menu's own
  Task Usage and Resource Usage actions and reads the live widget tree back:
  - eight columns (label, Details, five days, Total) and twenty physical rows
    (ten logical rows x two detail sub-rows);
  - widths 147/48/64 x 5/84 in Task Usage and 123/48/105 x 5/84 in Resource
    Usage, with the 21 px detail pitch;
  - the parent-only gutter numbers 1/2/3 (Task parents at physical rows
    0/6/12, Resource parents 0/8/12; every assignment blank);
  - label-column spans of two rows and a single-row Details span;
  - eight tier cells: "5 Jan '26"/M at the first period, "F" alone at the
    last, and single-band label/Details/Total captions;
  - every period of every logical row converted back to the measured units --
    Work and Actual Work by dividing Duration ticks by 10, Cost by rounding
    the Number dollars x 100 -- equal to `com.per_period`; Project's blank
    buckets stay null and render empty;
  - the rendered Cost text "$1,200.00" and "$400.00".
- The test compares the stored tier names as `std::string_view`; comparing two
  `const char*` with `==` compares pointers and passes only when the linker
  merges the translation units' literals.
- Tests: `qt_usage_gate` 1/1; app_qt ctest 36/36.

Per-commit gates (logs under `build/w31/`, named `W31-4.x-*.log`).  Every
commit's scan reports 0 files / 0 hits, guard exits 0, the hex grep finds no
match and `check_fixtures.py` is OK (139 documented hashes, 173 tracked
`.mpp`).  W31-4.1 is the fixture commit and was gated by fixtures/privacy; the
code commits rebuilt the touched trees and ran their suites:

| commit | item tests | core ctest | app ctest | app_qt ctest |
|---|---|---|---|---|
| W31-4.1 `6a8fd96` | fixture + oracle script; `check_fixtures.py` registers the hash | -- (fixture commit) | -- | -- |
| W31-4.2 `fe35886` | `viewstate_tests` (including the usage-style cases), `app_usage_style` 31 checks | 53/53 | 24/24 | 35/35 |
| W31-4.3 `4c32189` | `app_usage_grid` 214 checks | -- | 25/25 | 35/35 |
| W31-4.4 `f0ffb6f` | `app_usage_grid` 245 checks; `qt_usage_gate` static sections; `qt_view_gate` | -- | 25/25 | 36/36 |
| W31-4.5 `830d414` | `qt_usage_gate` live window | -- | -- | 36/36 |

G4 group gates (fresh trees configured from scratch on `830d414` under
`build/w31-g4-fresh{,-app,-qt}`, log `build/w31/W31-G4-fresh-gates.log`):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1136 checks / 456 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app ctest / app_qt ctest | 25/25 / 36/36 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (139 documented hashes, 173 tracked `.mpp`); fixture paths OK (222 tracked test-data files, no local path); scenario oracle provenance OK (8 oracle(s)) |
| client-name scan on HEAD / messages `wave30..HEAD` / `guard_names wave30..HEAD` / hex grep | 0 files, 0 hits / 0 commits / exit 0 / no matches (exit 1, expected) |

The run started 2026-10-27 06:43 and finished 2026-10-27 06:59 with no lock
retries.  The numbers belong to `830d414`; the marker commit that follows is
docs-only.

## Found, not fixed

- **Low (W31-4.4): the usage left table's other stored fields are not
  drawn.**  Project's stored tables carry fields beside the name column: Task
  Usage's COM `TableFields` measure eight fields at 6/6/9/24/10/13/13/13
  characters (the 24-character field is the name column the port paints at
  147 px; ID is the frozen gutter) and Resource Usage's four at 6/6/20/10
  (the 20-character name at 123 px).  The port paints the name, the 48 px
  Details column and the periods only; the remaining left-table columns are
  measured and committed but not painted.  G4's list is the time-phased grid
  (details, tiers, assignment rows, totals, per-period values), so this is
  recorded for a later pass rather than invented now.
- Residual notes (not findings): the port's Total row and Total column are its
  own affordances -- the committed captures record that Project's usage
  sheets have neither, so the brief's "totals" are the port's per-row and
  per-detail totals, not a Project layout; Actual Work is Work x the stored
  percent complete (exact at the fixture's 0 %/100 %, partial percent is a
  documented approximation); Cost is Work x the standard rate and is not
  scaled by completion (the measured resources Accrue At Prorated), so
  overtime, cost/use and the other accrual modes are unmeasured; only the
  measured header shapes are drawn (stored formats 13/20, weeks over days),
  an undrawable bottom unit falls back to days with a warning, and the 9 pt
  tier text size is chosen -- the tier text rows are measured, the point size
  is not; the port keeps its shared 42 px caption band where Project measured
  41 px for Resource Usage and 42 px for Task Usage (the left table's tallest
  caption, Task Usage's Task Mode caption being two lines, which the port
  does not draw); the stored scroll origin (start 14400, the unset value) is
  decoded and reported but not applied -- no measured state shows a shifted
  grid.

WAVE-31-G4-COMPLETE

## G5 - Network Diagram, Calendar and Other Views (decide, do not build)

The brief's G5 is a decision, not a build: Network Diagram, Calendar and both
Other Views entries stay out of this run's scope, disabled with decision 9's
exact tooltip on the ribbon and in the View menu, and the generated backlog now
names the scope reason in both tables.  The group's one item is below.

| item | finding | status | evidence |
|---|---|---|---|
| W31-5.1 | Network Diagram, Calendar and Other Views stay disabled with the exact tooltip; the backlog says why | **DONE (decision recorded; nothing built)** | commit `45d1206`; gates `build/w31/W31-5.1-*.log` |

W31-5.1 detail:

- The four ribbon controls (Task Views > Network Diagram, Calendar, Other Views;
  Resource Views > Other Views) each carry their own `RibbonCommand` id
  (`view_network_diagram`, `view_calendar`, `view_other_views`); every one is
  built with `enabled=false`, so it stays disabled with the exact decision-9
  tooltip `not implemented in this port`.  `qt_ribbon_inventory` holds the id,
  the disabled state and the exact tooltip against the independent pin
  (`app_qt/ribbon_pin.tsv` records enabled 0 for all four).
- The four View-menu entries are `MenuAction::none`, so
  `menu_item_supported()` is false and `MainWindow::build_menus()` disables each
  with the same exact tooltip.  `app_menu` now includes Resource
  Views > Other Views in its disabled set beside the three Task Views entries,
  and `qt_shell_offscreen` reads the disabled state and the exact tooltip back
  from the live View menu's two drop-downs.
- The generated backlog says why, not just that: the four ribbon rows and the
  four menu rows read `scope: ... out of this run's scope`
  (`docs/ui-command-backlog.md`, regenerated to 95 ribbon and 51 menu rows).
  Team Planner and the other `none` rows keep the generic future wording: the
  brief's G5 names Network Diagram, Calendar and Other Views only.
- Nothing was built: no view engine, no stored-definition decode, no file bytes
  and no view behaviour changed.

Per-commit gates (logs under `build/w31/`, named `W31-5.1-*.log`; the commit's
scan reports 0 files / 0 hits, `guard_names wave30..HEAD` exits 0, the hex grep
finds no match (exit 1, expected) and `check_fixtures.py` is OK):

| commit | item tests | app ctest | app_qt ctest |
|---|---|---|---|
| W31-5.1 `45d1206` | `qt_ribbon_inventory` (four View ids, disabled, exact tooltip); `qt_shell_offscreen` (live View menu); `app_menu`; backlog 95/51 rows | 25/25 | 36/36 |

G5 group gates (fresh trees configured from scratch on `45d1206` under
`build/w31-g5-fresh{,-app,-qt}`, log `build/w31/W31-G5-fresh-gates.log`; the run
started 07:08 and finished 07:17 with no lock retries):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1136 checks / 456 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app ctest / app_qt ctest | 25/25 / 36/36 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (139 documented hashes, 173 tracked `.mpp`); fixture paths OK (222 tracked test-data files, no local path); scenario oracle provenance OK (8 oracle(s)) |
| client-name scan on HEAD / messages `wave30..HEAD` / `guard_names wave30..HEAD` / hex grep | 0 files, 0 hits / 0 commits / exit 0 / no matches (exit 1, expected) |

The numbers belong to `45d1206`; the marker commit that follows is docs-only.

## Found, not fixed

- Nothing new.  G5 raised no Critical, High, Medium or Low finding: it decided
  the four entries, built no view and changed no view behaviour.
- Residual note (not a finding): the decision is deliberately static.  The four
  entries remain disabled and their stored view definitions are not surfaced by
  this run; a later wave that builds one must measure its rendering and any view
  state Project persists against Project first (standing rule 1), and only then
  refresh this row and the generated backlog.

WAVE-31-G5-COMPLETE

## CLOSE - the wave record

The wave ran as six groups plus this close, one fresh session each: G0 (the
wave-30 review's four Highs), G1 (the View tab's data commands), G2 (zoom and
the window commands), G3 (the Resource Sheet at Gantt fidelity), G4 (Task Usage
and Resource Usage at Gantt fidelity) and G5 (the views left out).  Every item
in G0..G5 is DONE; none was deferred, and no item was left without a committed
fixture, measurement log or test.  The group marker lines end each section
above.  The CLOSE work was the fresh-tree gate run, both privacy scans, the
review pass over `git diff wave30..wave31`, seven CLOSE fixes (W31-C1..W31-C7),
the push, the first dispatch (red on all three Qt jobs), the second (red on the
two Unix Qt jobs, one further platform warning) and the third dispatch that
gates the final tree, the START-HERE / INDEX / memory update, the regenerated
backlog and the re-scored 16 decisions.

The record below belongs to the final tree `072773b`; the CLOSE docs commit
after it changes docs only.  W31-C2, W31-C3, W31-C5, W31-C6 and W31-C7 change
compiled files, so the fresh-tree gates were re-run on the final tree (below);
W31-C4 adds the backlog check to the app_qt test set (37 tests).  W31-C1
(`644afe0`) is generator/docs only.

### The CLOSE fixes

| id | finding | commit | gates |
|---|---|---|---|
| W31-C1 | The generated backlog listed Fill as `implemented` with no note while W31-0.4's live ribbon control is disabled, so a reader of decision 9's backlog would take it as live | `644afe0` | generator and regenerated doc only, no build; `build/w31/W31-C1-checks.log`: regenerate exit 0, backlog-vs-pin 95/95 rows agree, scan HEAD 0 files / 0 hits, messages 0 commits, `guard_names wave30..HEAD` exit 0, hex grep no matches (exit 1, expected), `check_fixtures.py` OK (139 documented hashes, 173 tracked `.mpp`) |
| W31-C2 (U1) | Leaving Resource/Task Usage for the task sheet cleared the grid and spans but not `SheetHeaderView::tier_cells_`, so the usage two-tier captions painted over the task columns | `5fa3d5d` | app_qt build exit 0, ctest 36/36, `qt_usage_gate` 0 failures (it now switches Resource Usage -> task sheet), fixtures OK (139/173), scan HEAD 0/0, guard exit 0, hex exit 1 (expected) -- `build/w31/W31-C2-*.log` |
| W31-C3 (U2) | `open_file` (success and failure) and `new_project` skipped the usage teardown; `new_project` also left `generic_` true, so File > New from usage had no columns until a rebind | `165e497` | app_qt build exit 0, ctest 36/36, `qt_usage_gate` 0 failures (open, failed open and Blank Project each from a usage view), fixtures OK (139/173), scan HEAD 0/0, guard exit 0, hex exit 1 (expected) -- `build/w31/W31-C3-*.log` |
| W31-C4 (U3) | The generator parsed only `large`/`small`/`split`, so 19 command buttons built with `small_cmd`/`add(Kind::button)` had no backlog row; five disabled ones had no decision-9 reason, and the one-way cross-check could not see a missing row | `113d16a` | regenerate exit 0, **114 ribbon rows (69 disabled, 45 implemented)** and 51 menu rows, bidirectional pin check **114/114, 0 mismatches**, app_qt ctest 37/37 (with `qt_command_backlog`), fixtures OK (139/173), scan HEAD 0/0, guard exit 0, hex exit 1 (expected) -- `build/w31/W31-C4-*.log` |
| W31-C5 | The eight View-tab command ids added in W31-2.3/2.4/5.1 had no case in `handle_ribbon_command`'s `RibbonCommand` switch; MSVC accepted the unhandled enumerators, but `linux-qt` and `macos-qt` build with `-Werror=switch` and failed the first dispatch | `cb69a0b` | app_qt build exit 0, ctest 37/37 (`build/w31/W31-C5-app-qt.log`); the disabled controls keep a no-op case in the switch; the second dispatch's build cleared it (that run then failed only on W31-C7) |
| W31-C6 | `qt_resource_sheet_gate` sampled one logical pixel of a grabbed image; `windows-qt` failed only the Indicators-badge check while every neighbouring check passed (the sample assumed a unit device-pixel ratio and a fixed glyph footprint) | `c6e4314` | app_qt build exit 0, ctest 37/37, `qt_resource_sheet_gate` 0 failures (`build/w31/W31-C6-app-qt.log`), and the gate also passes at `QT_SCALE_FACTOR=1.25`; the check now scans the painted header and cell areas scaled by the image's device-pixel ratio, so a header that paints no badge still fails; the second dispatch cleared `windows-qt` |
| W31-C7 | `app_qt/tests/data_commands_test.cpp` kept `row_map_uids`, a helper left unused by W31-1.1; MSVC accepted it, but the Linux and macOS Qt jobs build with `-Werror=unused-function` and failed the second dispatch | `072773b` | app_qt configure/build exit 0, ctest 37/37 (`build/w31/W31-C7-app-qt.log`); the helper is removed; the third dispatch below is the platform re-gate |

W31-C1 makes the backlog's Fill row agree with its disabled control: the
generator checks `DISABLED_RIBBON_COMMANDS` before `IMPLEMENTED_RIBBON_COMMANDS`,
so a command whose handler exists behind a control that standing rule 1 keeps
disabled carries the measurement reason.  Project's Fill Down save rewrites the
row's calculated/effective dates and slack, the project roll-up and the DSI
strings, which the port's start writer cannot reproduce.

W31-C2 adds `reset_usage_presentation()` (grid, label spans, tier captions,
parent-only gutter numbers, 21 px pitch) and uses it in `show_view`; W31-C3
calls the same helper from both `open_file` outcomes and `new_project`, and
clears `generic_` there.  `qt_usage_gate` now covers Usage -> task sheet, File >
Open (success and failure) and Blank Project, all from a usage view.

W31-C4 completes the backlog: the Task tab's five percent buttons,
Indent/Outdent, Link/Unlink and Split Task, the Project tab's five
property/schedule/status buttons and the four alignment buttons now have rows.
The live ones carry their real scope (Project Information writes only the
priority; Links Between Projects, Change Working Time and Move Project say what
they cannot change); Split Task names the missing split-edit writer.  The local
one-way check became the committed `scripts/check_command_backlog.py`, which
compares the backlog and the pin in both directions and the enabled column
against the status; it is registered as `qt_command_backlog`.  The summary is
now 114 ribbon commands (69 disabled, 45 implemented) and 51 menu commands
(7 disabled).

W31-C5 and W31-C6 are the first CI dispatch's findings, not review findings.
W31-C5 names the eight disabled View commands (W31-2.3/2.4/5.1) in
`handle_ribbon_command`'s switch: MSVC accepted the unhandled enumerators, but
the Linux and macOS Qt jobs build with `-Werror=switch` and refused them.
W31-C6 replaces `qt_resource_sheet_gate`'s single-pixel sample with a scan of
the painted Indicators-header area and of the red/blank cell areas, scaled by
the grabbed image's device-pixel ratio: on `windows-qt` only that one check
failed while every neighbouring paint check passed, and the old sample assumed
a unit ratio and a fixed glyph footprint.  The gate still fails a header that
paints no badge (the scan requires 20 blue pixels in the Indicators section).
W31-C7 is the second dispatch's finding: the Linux and macOS Qt jobs build
app_qt with `-Werror=unused-function`, and `row_map_uids` had been left unused
by W31-1.1.  MSVC accepted it; removing the helper is the whole fix, and the
third dispatch is the platform re-gate.

### Errata from the record audit

The record audit (A1-A9) checked the committed group sections as well as the
working CLOSE draft.  A1 (the CLOSE draft was uncommitted) and A3 (START-HERE,
INDEX and the memory file were not yet updated) are what this commit closes:
the record, the docs and the marker are committed together.  A2 is the push
and the three dispatches below.  The remaining corrections are:

- **A4, five direct-run counts with no retained log.**  W31-0.2's 3201,
  W31-0.3's 3782, W31-2.2's 103, W31-4.2's 1133/27 and W31-5.1's 988 were
  printed by the binaries but not captured in a log.  The group sections now
  name the tests without those counts; the ctest totals in the same rows are
  logged and unchanged, the 50 expected failures are the source-level loop the
  review verified, and `app_usage_style`'s 31 checks are in
  `build/w31/W31-4.2-app-usage-style.log`.  No log was reconstructed.
- **A5, zero-byte per-commit guard/hex logs.**  Both tools print nothing on a
  clean scan, so many of the 49 per-commit logs are empty; their exit codes
  were not captured individually.  The group fresh-tree logs and this CLOSE
  run's scans (below) are the retained evidence for the range; the per-commit
  rows quote only the exit lines that exist.
- **A6, the usage oracle's stale hash line.**  `W31-4.1-oracle.log` prints
  `0459871A...`, an earlier save; the fixture committed in `6a8fd96` is
  `AF8DFE1D...`.  The oracle's own JSON, `build/w31/W31-G4-usage.json`, records
  the committed fixture's SHA-256 under `fixture.sha256` together with the
  time-phased values, and `qt_usage_gate` reads the same fixture and measures;
  the re-save happened after the text log's fixture line and changed no
  measured value.
- **A7, the finding-id convention.**  The four G0 commits say
  `W31-0.x (R30-x)` because each answers one wave-30 review id; `aff4710` is
  the brief and carries no work id; `W31-C1`..`W31-C4` are this CLOSE's review
  ids, `W31-C5`/`W31-C6` are the first CI dispatch's two findings and `W31-C7`
  is the second dispatch's.  One commit carries one finding (the G0 ones name
  both ends of the same mapping).
- **A9, the check's home.**  The backlog-vs-pin check was a local `build/`
  script, so the claim could not be rerun.  W31-C4 commits the bidirectional
  `scripts/check_command_backlog.py` and registers it as `qt_command_backlog`
  wherever Python is available; its W31-C4 run reports 114 pin command
  controls, 114 backlog rows, 0 mismatches.

### Fresh-tree gates (final compiled tree)

One run from three trees configured from scratch under
`build/w31-close-fresh{,-app,-qt}` on the final compiled tree `072773b` and
built serially with `CORE_WARNINGS_AS_ERRORS=ON`; log
`build/w31/W31-CLOSE-fresh-gates.log` (logged 08:26, finished 08:36 local, no
lock retries).  The CLOSE docs commit after it changes no compiled file.

| gate | result |
|---|---|
| core configure / build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 / 266 checks passed, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1136 / 456 checks passed, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app configure / build / ctest | exit 0 / exit 0 / 25/25 passed |
| app_qt configure / build / ctest | exit 0 / exit 0 / 37/37 passed |
| `check_fixtures.py` / paths / oracle provenance | fixtures OK (139 documented hashes, 173 tracked `.mpp`); fixture paths OK (222 tracked test-data files, no local path); scenario oracle provenance OK (8 oracle(s)) |
| line endings | no new CRLF file versus `wave30` (173 grandfathered at each tip, `tests/data` excluded; `build/w31/W31-CLOSE-fresh-gates.log`); `git diff --check` exit 0 with the same two grandfathered CRLF files excluded as at the wave-30 close (`app/app_edits_links_tests.cpp`, `app/field_link_edits.cpp`; log `build/w31/W31-CLOSE-diff-check-072773b.log`).  Without those exclusions the pre-existing CRLF line endings are reported as trailing whitespace (exit 2, 124 reported lines in those two files) -- both files are wholly CRLF at both tips, and the wave added lines to them without changing any line ending |
| client-name scan on HEAD / messages `wave30..HEAD` / `guard_names wave30..HEAD` / hex grep | 0 files, 0 hits / 0 commits / exit 0 / no matches (exit 1, expected) |

Parity gates on the fresh Qt shell from the same run (offscreen, scale 1, at
`072773b`): `wave23-outline` reports **10 comparable measures, 0 failing, 0
missing, PASS**, and `wave25-auto` colour-only reports **1 comparable measure,
0 failing, PASS**.  The pixel score is not re-run (no PROJECT_A reference PNG
is local).

### Privacy

The final tree is clean under both scans: the line-based
`scan_client_names.py HEAD --messages wave30..HEAD` reports **0 files / 0 hits**
and **0 commit messages**, the whitespace-normalised `guard_names.py
wave30..HEAD` is **0 hits, exit 0**, and `git grep -nE
"client-string-[0-9a-f]{8}"` has no match (exit 1, expected).  The six new
fixtures were built through Project on the neutral `C:/w31` roots with neutral
Author/Manager/Company/UserName/Title and are documented in
`tests/data/README.md`; no client task/resource/project/person name, profile
path, screenshot or unsalted name hash is committed.  The two pre-wave fixtures
and 69 tracked text files still carrying the operator's name, and the 13
unsalted hashes in older history, are unchanged and belong to the operator's
decision.
The wave-31 brief records the repository's own path (`D:\dev\msproject-mac`) --
machine layout, not client data, not a name-scan hit -- and the new oracle
scripts and record name the policy-allowed neutral `C:/w31` roots.

### Independent review of `wave30..wave31`

Three independent read-only reviews covered `git diff wave30..wave31` at
`644afe0` (compiled tree `d222ef3`, before the CLOSE fixes): a save-path/core
review, a UI/ribbon/gates review and an audit of the record itself.  They found
**1 Critical / 4 High / 3 Medium / 8 Low**; every Critical and High is fixed in
this wave, as are the three Mediums and all four process/repo Lows of the
record audit.  The four core Lows are queued with directions below.  The CI
dispatches then found three platform-only failures (W31-C5 and W31-C6 on the
first, W31-C7 on the second); they are CI findings, fixed and re-dispatched, and
are not counted in these review numbers; the third dispatch is green on the
final tree.

| id | severity | finding | disposition |
|---|---|---|---|
| A1 | Critical | The committed record ended at G5; the only CLOSE was the uncommitted draft with `{{REVIEW}}`/`{{CI}}`/`{{FOUND}}` placeholders, whose text already claimed the push, visits and docs | Fixed by this commit: the record, START-HERE, INDEX and memory are committed with the evidence, and the push/dispatch claims are true |
| A2 | High | No `origin/wave31` and no CI run existed; the only pre-push guard log was a REFUSED whole-branch (`main..HEAD`) scan listing seven wave-23/wave-27 fixtures already on their own origin branches | Fixed by the three pushes and the three dispatches below: the first dispatch was red on the three Qt jobs for W31-C5/W31-C6, the second was red on the two Unix Qt jobs for W31-C7, and the third is green on `072773b` (qt.yml 40404390641, ci.yml 40404395191); the hook's actual new-branch range (`HEAD --not --remotes=origin`) was clean at every push (preflight logs below) and the refused log is the known grandfathering residual, not this wave's commits |
| A3 | High | START-HERE, `memory/coordinator.md` and INDEX had no wave-31 update | Fixed by this commit (see the file list in the push section) |
| U1 | High | Leaving a usage view for the task sheet kept `tier_cells_`, painting the usage week/day captions over the task columns; no gate switched back | Fixed `5fa3d5d` (W31-C2); `qt_usage_gate` covers the switch |
| U2 | High | `open_file` (both outcomes) and `new_project` skipped the usage teardown, and `new_project` kept `generic_` true (no columns from usage) | Fixed `165e497` (W31-C3); `qt_usage_gate` covers open, failed open and Blank Project from a usage view |
| A4 | Medium | Five quoted direct-run check counts had no retained log | Fixed in this commit by removing the counts and recording the errata; the ctest totals stay |
| A5 | Medium | 49 per-commit guard/hex logs are zero bytes, so their exit codes are not individually evidenced | Documented in the errata; the group and CLOSE scans are the retained evidence for the range |
| U3 | Medium | 19 command buttons built with `small_cmd`/`add(Kind::button)` had no backlog row, five of them disabled without a reason; the one-way check could not see missing rows | Fixed `113d16a` (W31-C4): parser, 19 rows, bidirectional committed check, ctest |
| A6 | Low | The usage oracle's text log printed an earlier fixture hash than the committed fixture | Documented in the errata; the oracle's own JSON records the committed SHA-256 with the measured values |
| A7 | Low | The G0 commits carry two ids (`W31-0.x (R30-x)`); the brief and CLOSE ids fall outside the `W31-x.y` pattern | Documented in the errata as the review-id mapping, not two findings per commit |
| A8 | Low | The working CLOSE draft used the operator's first name three times | Fixed before commit: all three now say "the operator" (and INDEX's wave-28 line likewise) |
| A9 | Low | The backlog/pin check lived only in untracked `build/`, so its claim could not be rerun | Fixed `113d16a`: `scripts/check_command_backlog.py` is committed and registered as `qt_command_backlog` |
| C1 | Low | MPP12 (or stored 0) Accrue At formats as the literal "0" in the Resource Sheet, contradicting the adapter's blank intent; presentation only | Queued: map `accrue_at` outside 1..3 to empty Text and add an MPP12/view assertion |
| C2 | Low | W31-0.3's entire-project exception is read from the caller-writable `StructuralEdit::update_entire_project` at push time; production constructors compute it correctly | Queued: recompute the scope in `push()`/`save_structural_edits()` or make the flag factory-set |
| S1 | Low | A corrupt/extreme standard rate reaches `std::llround` after only a `> 0` test; an extreme rate can overflow the int64 cost multiply; display only, never a write | Queued: require `std::isfinite` plus a bound before `llround` and use a checked multiply |
| S2 | Low | With no "Resource Sheet" view, any applied view with a style record in [634, 41362) bytes supplies the Resource Sheet's fonts/colours; presentation only | Queued: accept the fallback only when the applied view's table names a stored resource table |

The core review also disclosed that it briefly wrote a scratch diff inside the
repository during its read-only pass and removed it; `git status` afterwards
showed only the two docs that were already modified.  The three reviews ran in
separate contexts and read the code; they did not run builds or change the
tree.

### Push and CI

The code tree was pushed to `origin/wave31` **three times, never with force**,
each after a clean preflight on the hook's actual range (`HEAD --not
--remotes=origin`: `scan_client_names.py HEAD`, `guard_names.py <new range>`,
`guard_names.py HEAD --not --remotes=origin`, the `client-string-` hex grep and
`check_fixtures.py`; logs `build/w31/W31-CLOSE-push-preflight*.log`).  All
three code-tree pushes passed the repository's pre-push hook, as does the
docs-only push.

| push | tree | new commits | preflight | log |
|---|---|---|---|---|
| 1 | `113d16a` | created `origin/wave31` | 0 scans, guard exit 0, hex exit 1, fixtures OK | `build/w31/W31-CLOSE-push.log`, `...-push-preflight.log` |
| 2 | `c6e4314` | `113d16a..c6e4314` (W31-C5, W31-C6) | scan 0/0, guard `113d16a..HEAD` exit 0, hex exit 1 | `build/w31/W31-CLOSE-push-2.log`, `...-push-preflight-2.log` |
| 3 | `072773b` | `c6e4314..072773b` (W31-C7) | scan 0/0, guard `c6e4314..HEAD` and `HEAD --not --remotes=origin` exit 0, hex exit 1 | `build/w31/W31-CLOSE-push-3.log`, `...-push-preflight-3.log` |

Both workflows were dispatched explicitly after each push with `gh workflow run
qt.yml --ref wave31` and `gh workflow run ci.yml --ref wave31` (the
`workflow_run` trigger only fires from `main`).  Three dispatches were needed
because the Qt jobs on Linux and macOS build with `-Werror=switch` and
`-Werror=unused-function` while MSVC tolerates both, and one Windows gate
sampled pixels:

| dispatch | tree | qt.yml (`qt-shell`) | ci.yml (`ci`) |
|---|---|---|---|
| 1 | `113d16a` | **40400256617 failure**: `windows-qt` failed `qt_resource_sheet_gate` (W31-C6) after its build; `linux-qt` and `macos-qt` failed the build on `-Werror=switch` (W31-C5) | **40400261717 success**: `windows-msvc`, `ubuntu-gcc` |
| 2 | `c6e4314` | **40401372235 failure**: `windows-qt` green (C6 fixed); `linux-qt` and `macos-qt` failed the build on `-Werror=unused-function` (W31-C7) | **40401377893 success** |
| 3 | `072773b` | **40404390641 success**: `windows-qt`, `linux-qt` and `macos-qt`, including the two parity gates | **40404395191 success**: `windows-msvc`, `ubuntu-gcc` |

The final-run status snapshots are `build/w31/close-ci-{qt,ci}-status.json`
(all five jobs `success` on `072773b`); the first and second dispatches are
snapshotted as `close-ci-{qt,ci}-firstdispatch-status.json` and
`close-ci-{qt,ci}-seconddispatch-status.json`, and the watches are
`build/w31/W31-CLOSE-ci-watch*.log`.  The two platform findings and their
fixes are W31-C5/W31-C6 (first dispatch) and W31-C7 (second dispatch), each
fixed in its own commit with an app_qt build and 37/37 test run on the tree it
claims.  The CLOSE docs commit after `072773b` (and any docs-only correction
after it) is pushed the same way and changes no compiled file; the five green
jobs cover the final code tree.

### The 16 decisions re-scored (wave-30 close as the starting point)

| # | decision | wave 30 | after wave 31 | why |
|---|---|---|---|---|
| 1 | all seven tabs | DONE | **DONE** | the View tab's Data and Zoom groups became live (six controls) on the measured/session-only path; the tab/group structure is unchanged |
| 2 | full backstage | DONE | **DONE** | unchanged; no backstage work this wave |
| 3 | original icon set | DONE (stronger) | **DONE (stronger)** | unchanged; no icon was redrawn or regressed |
| 4 | measured chrome metrics | DONE | **DONE** | unchanged; no ribbon geometry change beyond enabled/disabled state |
| 5 | timeline strip | DONE | **DONE** | unchanged; the wave-30 negative baseline for Add to Timeline is untouched |
| 6 | real Entry table | DONE | **DONE** | the Entry table is still real and file-driven; Tables now switches between the task tables the file stores, session only |
| 7 | render what the file says | DONE (stronger) | **DONE (stronger)** | the Resource Sheet renders the file's stored resource Entry table, its Text Styles and its stored widths; both usage views render the stored Details lists, timescale tiers and measured widths; no writer was added, so every enabled save path is still pinned to its measured bytes |
| 8 | status bar | PARTIAL (stronger) | **PARTIAL (stronger)** | unchanged; Team Planner and Report still have no working view to switch to |
| 9 | disabled commands + backlog | DONE (stronger) | **DONE (stronger)** | the regenerated backlog lists 114 ribbon commands with 69 disabled and 45 implemented, and 51 menu commands with 7 disabled (95/64/31 after W31-C1 corrected Fill's row; W31-C4 then listed the 19 command buttons the generator had skipped, five of them disabled with reasons); six View controls became live, the Window group and Macros stay disabled with their own reasons, and G5 names the out-of-scope views; a committed bidirectional check pins all 114 rows to the independent pin |
| 10 | light and dark themes | PARTIAL (stronger) | **PARTIAL (stronger)** | unchanged; the White theme is still laid out and disabled, and this wave made no theme change |
| 11 | new-project defaults | DONE (stronger) | **DONE (stronger)** | unchanged; no new-project path changed this wave |
| 12 | structural measurement | PARTIAL (stronger) | **PARTIAL (stronger)** | wave 31 adds four Project measurement sets (the Fill Down pair, the stored-tables fixture, the Resource Sheet fixture and the Task/Resource Usage fixture) and two numbers-only references (zoom, time-phased values); 6 new fixtures (167 -> 173 tracked `.mpp`), 8 new documented hashes (131 -> 139), four new app_qt gate targets (`qt_data_commands`, `qt_zoom_commands`, `qt_resource_sheet_gate`, `qt_usage_gate`; `qt_fill_gate`, `qt_ribbon_inventory`, `qt_shell_offscreen` and `qt_view_gate` extended) and four new test targets (`app_sort`, `app_usage_style`, `app_usage_grid`, and the `qt_command_backlog` script test); every view-state change is session-only, so no persisted view-state writer was measured; the pixel score is still not re-run (no PROJECT_A reference PNG is local) |
| 13 | Carlito bundled | DONE | **DONE** | unchanged |
| 14 | every dialog on every tab | DONE (stronger) | **DONE (stronger)** | the Sort dialog and the Zoom dialog are now live (session-only effects); no other missing dialog was added |
| 15 | dialog semantics | PARTIAL (stronger) | **PARTIAL (stronger)** | the Sort and Zoom dialogs apply a real, tested effect and never fake success; the laid-out-only dialogs (Visual Reports, Compare Projects, Page Setup's stored styles) are unchanged |
| 16 | split view | DONE (stronger) | **DONE (stronger)** | unchanged; the Details splitter still hosts one form per view and follows the selection by UID |

### Found, not fixed

- **The four core review Lows (C1, C2, S1, S2).**  Each is presentation-only or
  a defence-in-depth contract, never a save-path risk, and each has its fix
  direction in the review table above.  They are the only review findings not
  fixed in this wave.
- **The G3 and G4 disclosed gaps.**  The Resource Sheet does not paint the
  un-stored Add New Column stub and uses the shell's shared caption font
  instead of slot 1; the usage views do not draw the left table's other stored
  fields, their Total row/column are the port's affordances, Actual Work is
  Work x stored percent (approximate at a partial percent), Cost ignores
  overtime and the non-prorated accrual modes, only the measured header
  formats/week-day shapes are drawn, the stored scroll origin is decoded but
  not applied, and the tier point size is chosen rather than measured.  These
  are named in the G3/G4 sections; none is a regression against wave 30.
- **The per-commit evidence gap (A5).**  The clean guard/hex runs print no
  output, and 49 per-commit logs are empty without captured exit codes.  The
  range evidence is the group fresh-tree logs and the CLOSE scans; the wave
  does not reconstruct per-commit exit lines.
- **The wave-30 and wave-29 queues and the plan's after-run list** are unchanged
  and itemised under "What remains after this run" below.

### What remains after this run

This is the last wave of the approved run of waves 27-31.  The plan's "After
wave 30" list stands, and wave 31 adds the view-state and second-view-family
items below.  Nothing here blocks the tree: every item is either disabled with
the exact tooltip or a documented fidelity gap.

- **The measured-and-deferred commands from wave 30 and the shared writers they
  need.**  Insert Summary/Milestone, Inactivate, Respect Links, Move, Notes, Add
  to Timeline and the text-style group (Task tab); Add Resources, Information,
  Notes and Assign Resources' remove direction (Resource tab); Calculate Project,
  Set Baseline and WBS (Project tab) stay disabled with committed Project pairs
  and the decision-9 tooltip.  The writers they wait on:
  - a VarMeta entry inserter/remover (Information, Notes, Move, Add Resources,
    Calculate Project, Set Baseline);
  - a `214` view-stream writer (the text-style group, Move's StartText/
    FinishText removal, Notes' entry) and Project's RichEdit note serialisation
    for Notes;
  - a `TBkndCal` resource-calendar row plus three `114/Props` count words for
    Add Resources; Props-record insertion for WBS;
  - append-and-re-key table growth for Assign Resources' remove direction;
  - a scheduler save path and a `DocumentSummaryInformation` writer for
    Calculate Project; a baseline writer for Set Baseline; a code-definition/
    renumber writer for WBS;
  - the reader's inserted summary/milestone result (the fractional Fixed2Data
    ordinal and the reparenting), `Task.Active`, and an extended
    `tests/save_pair_noise.h` mask for the append tails;
  - a Project build where Add to Timeline actually runs: 16.0.20430's own
    command is inert, so the committed pair is only a negative baseline.
- **The View tab's persistence gap.**  Sort order, the applied table, the shown
  outline level, zoom and window arrangement are view state Project persists in
  the file.  Wave 31 applied all of them for the session only and kept the
  file's bytes (asserted by SHA-256).  A later wave that writes them must first
  measure Project's save of each shape (standing rule 1).  The Sort dialog is
  single-key; the outline level is lost on a structural rebuild; the initially
  applied table is the first stored task table, not the file's applied-view
  table; the Zoom Custom span was never measured against Project.
- **The second view family's fidelity gaps.**  Resource Sheet: the un-stored Add
  New Column stub is not painted (the group's Low), and the header rows use the
  shell's shared caption font rather than the view's slot-1 font.  Usage: the
  left table's other stored fields are not drawn (the group's Low); the Total
  row/column are the port's affordances where Project's sheets have none;
  Actual Work is Work x stored percent (an approximation at partial percent);
  Cost ignores overtime, cost/use and the other accrual modes; only the measured
  header formats and week/day shapes are drawn; the stored scroll origin is
  decoded but not applied; the tier point size is chosen, not measured.
- **The wave-30 and wave-29 queues carried unchanged** (see
  `wave30-complete.md` and `wave29-complete.md`, "Found, not fixed"): C4 (task
  mode beside a queued view-state/formula edit), C5 (off-chain progress and the
  project row), U1, U3-U7, the A1 process item, Spelling's modest word list, F3
  (the operator-name purge -- the operator's decision), F6, F7, R28-5/6/8/9/11, the named
  refusals and conventions, the theme gate's sample gap, the stored combination
  view and the White theme, Split Task, the 13 unsalted hashes (the operator's
  decision) and the 174 grandfathered CRLF files.
- **After the run** (the plan's "After wave 30" list): Team Planner, Network
  Diagram, Calendar and Report views; the `constraint-mso` engine case; macOS
  launch and notarisation; the hashed task names in
  `tests/data/project_a_golden.tsv`; the remaining review Lows.

WAVE-31-COMPLETE
