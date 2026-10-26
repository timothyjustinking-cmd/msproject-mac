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
  committed controls and checks a refusal per shape, plus the outdent path
  (3201 checks).

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
  date-edit half (`app_edits_actuals` 3782 checks, 50 expected failures).

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
| W31-0.2 `d9d8d8d` | app build exit 0 | `app_edits_outline` 3201 checks | 22/22 |
| W31-0.3 `659fd97` | app build exit 0; app_qt build exit 0 | `app_edits_actuals` 3782 checks, 50 expected failures; app_qt 32/32 | 22/22 |
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
