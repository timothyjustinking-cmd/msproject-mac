# Wave 33 - the completion record

One table row per brief item.  A group ends only when its exact
`WAVE-33-<group>-COMPLETE` line is appended below (wave-33-brief section 1).
Deferrals need the attempt evidence the standing rules and the brief name.

## G0 - data safety (Critical)

| item | finding | status | evidence |
|---|---|---|---|
| W33-0.1 | A-2 / D-1 / D-4 (Critical): every `.mpp` write opens the final path with `ios::trunc`, so a failed or short write leaves the target truncated | **DONE** | commit `c176a0f`; gates `build/w33/W33-0.1-*.log` |
| W33-0.2 | A-1 (Critical): a second Save after a formula-only or date-format-only save silently discards it | **DONE** | commit `a6cfa05`; gates `build/w33/W33-0.2-*.log` |

No G0 item was deferred.

W33-0.1 detail:

- One shared helper, `mppwrite::write_file_atomically(path, bytes, verify)`
  (`core/mppwrite/atomic_write.h/.cpp`), writes `<target>.tmp-<pid>` in the
  target's directory, flushes it, re-reads it and compares every byte, runs
  the caller's self-check, then renames it over the target with
  `std::filesystem::rename`.  The target is never opened for writing.  On any
  failure the temporary file is removed and the target is byte-identical to
  what it was; only a file the call created is ever removed, so the test's
  directory at the temporary name is left alone.
- `CfbDocument::save` passes a verifier that re-parses the re-read container,
  runs `check()` and requires byte-identical re-emission (the D-4 self-check).
  `InPlacePatcher::save` calls the helper directly.  Both `save.cpp` call
  sites (`save_date_edits` and `save_plan`, `core/mppwrite/save.cpp:489` and
  `:598`) end in `CfbDocument::save`, and every write of the
  `.w51-structural-tmp` cascade in `app/edit_model.cpp:3738-3752` is a
  `CfbDocument::save` to the intermediate or the final path, so all four
  named sites and the cascade are covered.
- `core/mpp`'s self-contained `mpp_reader_tests` compiles `atomic_write.cpp`
  beside `cfb_writer.cpp`; `core/mppwrite` registers the new test as
  `mppwrite_atomic_write`.
- Tests (`tests/atomic_write.cpp`), RED then GREEN: with a directory
  occupying `<target>.tmp-<pid>`, the pre-fix writer rewrote the target --
  7 checks, 3 failures (`build/w33/W33-0.1-red.log`); after the fix the same
  binary refuses and reports 16 checks, 0 failures
  (`build/w33/W33-0.1-atomic-green.log`).  Sections: the blocked temporary
  name leaves the target byte-identical and removes nothing it did not
  create; an ordinary save and the no-edit save are byte-identical to the
  source; the temporary name; a forced re-read mismatch leaves the target
  byte-identical and no temporary file; the helper writes a new file and
  replaces an existing one.

W33-0.2 detail:

- `app::Document::save` always derives its output from the file it opened.
  The non-structural route now discards the pending view-state and
  custom-formula operations, reloads the written file as the open document
  (which sets the shell's `owns_path` and re-points the document), and
  refreshes the title and menus.  A failed re-read is reported in the status
  line but does not claim the save failed -- the file is already on disk.
- Qt shell: `MainWindow::save_document_path`
  (`app_qt/main_window.cpp:3271`).  macOS shell:
  `saveDocumentWithPanel` (`app/main.mm:4148`), which mirrors the structural
  route's adoption; the macOS shell is compiled by CI only, as the brief
  says, and this change stays mechanical (existing `PlanState` /
  `PlanInput` fields only).  The locally executed RED/GREEN is the Qt shell.
- Tests (`app_qt/tests/save_reopen_test.cpp`): a custom-field formula is
  saved to a new target, read back from `114/Props`, then a second Save with
  nothing pending leaves the file unchanged, then Insert Task and Save keep
  the formula; the same shape for the table date format read from
  `214/CTable/Var2Data` byte 11.  RED on the pre-fix shell: `qt_save_reopen`
  2 failures -- "the formula survives the next save (the document adopted
  its own write)" and "the date format survives the next save"
  (`build/w33/W33-0.2-red.log`).  GREEN: 0 failures
  (`build/w33/W33-0.2-green.log`); the full app_qt suite 37/37.
- The test copies the committed fixture into the build tree and writes only
  there; nothing under `tests/data` is written.

Per-commit gates (logs under `build/w33/`; every item's scan reports 0 files /
0 hits and fixtures OK):

| commit | builds | tests |
|---|---|---|
| W33-0.1 `c176a0f` | core build exit 0; app build exit 0; app_qt build exit 0 | core ctest `-E plan_recalc` 53/53; `mppwrite_atomic_write` 16 checks, 0 failures; app ctest 25/25; app_qt ctest 37/37 |
| W33-0.2 `a6cfa05` | app build exit 0; app_qt build exit 0 | `qt_save_reopen` 0 failures; app_qt ctest 37/37; app ctest 25/25 |

G0 group gates (fresh trees configured from scratch on `ed4864b` under
`build/w33-fresh{,-app,-qt}`, log `build/w33/W33-G0-fresh-gates.log`; the only
difference from `a6cfa05` is this file's marker line):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 53/53 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failure(s) / 8 recorded (7 pass, 1 fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1136 checks / 456 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app / app_qt configure and build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| app ctest / app_qt ctest | 25/25 / 37/37 passed |
| `check_fixtures.py` | fixtures OK (139 documented hashes, 173 tracked `.mpp`); paths OK (222 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `wave33..HEAD` / `guard_names wave33..HEAD` | 0 files, 0 hits / 0 commits / exit 0 |

The run started 19:40 and finished 19:58.  The numbers belong to `ed4864b`,
the marker commit; its code tree is `a6cfa05`, and the record commit that
follows is docs-only.

## Found, not fixed

- Nothing new.  G0 raised no Critical, High, Medium or Low finding beyond the
  two items above.
- The macOS shell change (W33-0.2) is compiled by CI only; no macOS compiler
  runs locally, so its compile is the CI job, as the brief anticipates.  The
  brief's "keep it mechanical" is met: the edit uses only fields the
  structural route already sets.
- CLOSE is out of scope for this group session: the fresh-tree gates on the
  final commit, the review pass over the wave-33 diff, the push and the CI
  dispatch belong to the CLOSE section (wave-33-brief section 2).

WAVE-33-G0-COMPLETE

## G1 - refusals and disables (High and Medium)

| item | finding | status | evidence |
|---|---|---|---|
| W33-1.1 | A-3 (High): Delete Task on a summary row tombstoned the summary alone | **DONE** | commit `22218de`; gates `build/w33/W33-1.1-*.log` |
| W33-1.2 | A-9 (High): Insert Task cloned the last live row's progress, actuals and work | **DONE** | commit `f18b208`; gates `build/w33/W33-1.2-*.log` |
| W33-1.3 | A-5 (High): Formula SET accepted unmeasured field kinds | **DONE** | commit `3c996b0`; gates `build/w33/W33-1.3-*.log` |
| W33-1.4 | A-4 (High): Formula CLEAR wrote an unmeasured 0 to the `0x02401392` flag | **DONE** | commit `9660453`; gates `build/w33/W33-1.4-*.log` |
| W33-1.5 | A-7 (Medium): Task Information and Task Fields ignored `push()`'s return | **DONE** | commit `0c83489`; gates `build/w33/W33-1.5-*.log` |
| W33-1.6 | A-11 (Medium): Table edits hard-coded `table_index = 0` | **DONE** | commit `95e484a`; gates `build/w33/W33-1.6-*.log` |
| W33-1.7 | A-8 / D-7 (Medium): FAT-slack containers opened editable and then could not save | **DONE** | fixture commits `3b53b9d` and `964594d`; fix commit `397eaf9`; gates `build/w33/W33-1.7-*.log` |
| W33-1.8 | A-12 (Low): a second pending formula operation overwrote the first silently | **DONE** | commit `6a58e05`; gates `build/w33/W33-1.8-*.log` |

No G1 item was deferred.

W33-1.1 detail:

- `StructuralEdits::push` now requires the plan, refuses a delete of a
  summary row and a delete of a pending insert, and queues nothing;
  `app::make_delete_edit` turns that refusal into the status line both
  shells show: "delete the subtasks first, or outdent them".  The writer's
  `delete_task_row` re-checks the same shape from the live outline as
  defence in depth, so a hand-built queue cannot tombstone a summary either.
- RED on the pre-fix tree: `build/w33/W33-1.1-red-writer.log` (768 checks,
  3 failures: "delete a summary row is refused: no exception was thrown",
  plus the document change and the file write the refused request made);
  `build/w33/W33-1.1-red-app.log` (3232 checks passed, 5 failures -- the
  five new `push()` assertions); `build/w33/W33-1.1-red-qt.log` (4
  failures: the shell showed the confirmation, queued the delete, dirtied
  the document and did not name the refusal).  GREEN:
  `W33-1.1-green-writer.log` 768 checks, 0 failures;
  `W33-1.1-green-app-outline.log` 3237 checks passed;
  `W33-1.1-green-qt.log` 0 failures (`app_qt/tests/delete_summary_test.cpp`,
  the Qt suite's 38th test).
- The core test normalises the committed Project save's stale VarMeta
  logical count (41 declared, 42 physical) on its build-tree copy so
  `validate_table` passes and only the summary guard is measured; the
  committed fixture is never opened in place.

W33-1.2 detail:

- `add_task_row` clones the LAST live row's 202 FixedData bytes and its five
  overtime VarMeta payloads, clearing only the constraint and the start
  anchor, so Insert Task after a progressed row carried the old row's
  progress and actuals and lost Project's `0x0B40015E` block.  The model
  now checks the clone source in three places: `app::check_insert_task`
  names the row and the first dirty fact for both shells before they ask
  for a name (progress, actual work time, actual dates, task calendar, a
  constraint other than ASAP, an assignment beyond Project's `0xFFFF0001`
  placeholder, more than one physical assignment row);
  `StructuralEdits::push` repeats the plan half whenever a plan is given;
  and the save cascade re-checks the file's actual last live row before
  every append, because an undo can restore a pending-deleted row after
  the insert was queued and a hand-built queue skips `push()`.
  `core/mppwrite` gains `last_live_task_uid()` and
  `insert_task_clone_is_clean()`, which also read the words the plan does
  not carry (Resume/Stop, percent work complete, the `0x0B40015E` block,
  the copied overtime payloads).
- RED (`build/w33/W33-1.2-red-app.log`): on a build-tree copy of
  synthetic-base whose last live row reads 40 %, push accepted the insert,
  the save wrote the clone and the new row read back 40 % -- 4 of 239
  checks failed.  GREEN: `W33-1.2-green-app-edit-out.log` 281 checks
  passed; `W33-1.2-green-writer-out.log` 777 checks, 0 failures.  The 40 %
  file is built in the build tree with the port's own measured percent
  writer; no committed fixture ends at 40 % and none is written.
- wave23-outline.mpp's last live row carries a real resource assignment,
  so Insert Task is now (correctly) refused there; the spelling and inspect
  Qt gates' pending-insert legs open synthetic-base instead, whose last row
  is Project's clean placeholder row.

W33-1.3 detail:

- Only Text1..30 and Number1..20 custom-field formulas were measured
  against Project.  `app::custom_formula_field_is_settable` now accepts
  exactly `mpp::custom_field_kind` text or number ids;
  `app::custom_formula_set_refusal` names the refused field and carries the
  decision-9 text, "not implemented in this port";
  `Document::apply_custom_formula` refuses before anything is queued and
  leaves an earlier queued formula alone, and the save hook refuses a
  hand-built `CustomFormulaEdit` that skipped the queue.  Both shells
  already surface the returned error.
- RED (`build/w33/W33-1.3-red-app-document.log`): with the tests written
  against the old API, all six refused kinds were accepted (Date1,
  Duration1, Cost1, Flag1, Start, OutlineCode1) -- 29 of 311 checks failed,
  all 29 the new W33-1.3 checks.  GREEN
  (`W33-1.3-green-app-document.log`): 311 checks passed -- one refusal per
  kind, Text1/Text30/Number1/Number20 accepted, a queued request preserved
  across a refusal, and the hand-built save-hook refusal leaving
  `114/Props` untouched.

W33-1.4 detail:

- `custom_field_value_remains` decides the `0x02401392` "custom-field data
  in use" flag from Task custom VarMeta keys, the Flag1 bit and assignment
  keys only, and the CLEAR path writes 0 -- a value Project never measured,
  unlike SET's unconditional 1.  Formula CLEAR is now disabled:
  `Document::clear_custom_formula` is a `[[nodiscard]] bool` that reports
  false with the decision-9 reason, queues nothing and leaves a queued SET
  alone; the Qt dialog's clear button is disabled with the exact tooltip
  and its click handler removed, so the dialog cannot return
  `Operation::clear`; the macOS alert's second button is disabled with the
  same tooltip; both shells keep their clear branches as refusals that call
  the Document API.  The save hook still implements a hand-built clear
  request -- the existing one-write cascade test uses that shape -- but
  Document and both shells can no longer produce one.
- The two old clear tests asserted the flag-0 write and are replaced by
  `test_formula_clear_disabled`; `test_save_formula_set` now first proves
  the fixture stores no custom-field VALUE, so its existing "the
  custom-field data flag is 1 after setting a formula" check is the
  measured value-free SET case.
- RED (`build/w33/W33-1.4-red-app-document.log`,
  `W33-1.4-red-qt-dialog.log`): 2 of 296 document checks failed (the
  disabled clear queued the request and dropped the queued Text2 SET); the
  Qt dialog gate failed 3 checks (enabled button, wrong tooltip, click
  queued).  GREEN: 299 document checks passed
  (`W33-1.4-green-app-document.log`); `W33-1.4-green-qt-dialog.log`
  `qt_dialog_gate`: 0 failures.

W33-1.5 detail:

- `apply_task_information` pushed the Custom Fields tab's edits onto the
  live queue and ignored `push()`'s return; with an automatic outline edit
  pending, W32-0.1 refuses every percent push, so the dialog returned false
  while the Text1 edit stayed queued -- half the dialog applied.  Both
  handlers now decide on a trial copy: every Custom Fields edit is pushed
  onto the copy, then the percent edit with the plan; a parse error or the
  first refused push reports its reason and returns with the live queue and
  the sheet cells untouched; only when every push succeeds is the trial
  moved into `structural_` and the cells committed.  `show_task_fields`
  applies the same rule (unreachable from the menu since W23 B-13 routes
  Task > Information to Task Information, whose Custom Fields tab is the
  Task Fields surface).
- The new gate `qt_task_information_apply`
  (`app_qt/tests/task_information_apply_test.cpp`, the Qt suite's 39th)
  copies wave29-outline-auto-indent-control.mpp into `test-out`, queues the
  automatic indent on uid 2 exactly as the menu does, then applies uid 3's
  Task Information values with a Text1 value and 40 %: the apply is refused,
  the status names the pending automatic outline edit,
  `pending_edits().size()` is unchanged and the automatic indent is still
  the only edit.  RED (`build/w33/W33-1.5-red-qt-apply.log`): 1 failure (the
  Text1 edit was left behind).  GREEN
  (`build/w33/W33-1.5-green-qt-apply.log`): 0 failures.

W33-1.6 detail:

- View > Tables applies any of the file's stored task tables for the
  session (W31-1.3), but `TableDateFormatDialog::accept` hard-coded the
  request's `table_index` to 0, so with the second table on screen the save
  silently edited the FIRST table's `214/CTable` byte.  The dialog's
  constructor now takes the shown table index with no default, so a caller
  has to state which table it edits; `accept()` writes the request's
  `table_index` from it and `edit_table_date_format` passes its own index
  through.  The Qt shell supplies `doc_.selected_table()` -- the table
  View > Tables applied.  The macOS shell has no Tables command, so its
  view always shows the load-applied first table; it keeps index 0 and now
  says so instead of calling it the measured fixture's only table.
- The new gate `qt_table_date_format`
  (`app_qt/tests/table_date_format_test.cpp`, the Qt suite's 40th) copies
  wave31-tables.mpp (Entry applied at load, Review second) into
  `test-out`, opens it in the real MainWindow, switches to Review through
  the real View > Tables popup, chooses format 4 in the dialog and saves:
  the second table's date format is 4; the first table's date format, its
  whole stored block and the applied Entry table's Var2Data date-format
  byte are unchanged.  `dialog_gate_test` now constructs the dialog for
  table 1 and checks that its result carries that index.  RED
  (`build/w33/W33-1.6-red-qt-table-date.log`): 4 failures.  GREEN
  (`W33-1.6-green-qt-table-date.log`, `W33-1.6-green-qt-dialog.log`): 0
  failures in both.

W33-1.7 detail:

- The reader opened a container with unreferenced trailing sectors (MS
  Project writes them) and reported "N unreferenced sectors", but
  `mppwrite::CfbDocument::to_bytes()` refuses to re-emit such a container,
  so every Save failed after a whole session of edits: the work was
  silently unwritable.  At open,
  `app::document_has_unreferenced_sectors` (`app/reader_adapter.h`)
  detects that warning and both shells mark the document read-only with the
  brief's reason, "saved by Project with unreferenced sectors; saving is
  not supported yet":
  - `app::StructuralEdits::push` refuses every kind and queues nothing (the
    model backstop); `app::begin_cell_edit` takes the reason and refuses the
    session with it, so every cell-edit path shares one refusal;
  - the Qt shell sets the reason on the table adapter and the structural
    model at open, shows it in the left status segment, and refuses Insert
    Task, Delete Task, Indent/Outdent, unlink, the %-complete buttons, the
    task-mode menu, Task Information/Task Fields/Custom Fields/Predecessors/
    Project Information/Update Project/Mark on Track, Table Date Format,
    Custom Field Formula, Cut/Paste/Fill Down and both Save entries before
    any dialog or prompt;
  - the macOS shell stores the reason on `PlanState`, appends it to the
    status text, and refuses the same entry points and
    `saveDocumentWithPanel`.  (MSVC never compiles `app/main.mm`; the
    macOS job is its compile gate.)
- The fixture pair: `3b53b9d` added `tests/data/wave33-fat-slack.mpp` --
  synthetic-base plus 64 zero sectors, 270336 bytes with 527 physical
  sectors against a 512-entry FAT, so 15 are unreferenced -- and `964594d`
  replaced its bytes in place with neutral ones, because the inherited
  synthetic-base bytes carried the pre-wave privacy residual (the operator
  name and the local path; W33-4.3 is the item that neutralises the base
  fixture).  The clean blob's
  SHA256 is
  `E3A7D6CBBB82EA35901D6C2CE8C7E4D40DE032D424785CBA6F6BED41B93A85C8`,
  pinned in `scripts/check_fixtures.py` with a `tests/data/README.md` row.
- Tests: `app_qt/tests/fat_slack_test.cpp` (`qt_fat_slack`, the Qt suite's
  41st) copies the committed fixture, opens it in the real MainWindow and
  checks the status line, a refused cell edit that leaves the text and
  opens no session, Insert Task / Delete Task refusing without opening a
  dialog (a modal probe records any dialog) and Save refusing with no file
  written; the clean fixture still accepts the same edit.
  `app_edit_tests` gains `test_fat_slack_read_only` (the fixture is
  detected, the clean fixture is not, the model refuses insert and delete
  with nothing queued; a missing committed fixture FAILS, never skips);
  `layout_model_tests` pins `begin_cell_edit`'s refusal and its reason.
  RED (`build/w33/W33-1.7-red-qt-fat-slack.log`): 11 failures.  GREEN:
  `W33-1.7-green-qt-fat-slack.log` 0 failures; app_edit 290 checks,
  layout_model 333 checks.

W33-1.8 detail:

- `Document::apply_custom_formula` replaced the queued request with
  whatever the next call carried: two Custom Field Formula requests before
  Save silently dropped the first, and the save wrote the second while the
  status line had named the first.  A second VALID request is now refused
  before assignment with the reason "a custom-field formula is already
  pending; save it before setting another", and the queued first request is
  untouched.  The field-kind and empty-formula checks run first, so their
  own refusals (including the decision-9 text) are unchanged;
  `discard_custom_formula()` -- what a save or a shell discard runs --
  clears the pending op, after which a new request is accepted.  Both
  shells already show the returned error verbatim, so no shell change was
  needed.
- RED (`build/w33/W33-1.8-red-app-document.log`): 5 of 307 checks failed
  (the second request was accepted, carried no reason, replaced the queued
  first one and changed its field).  GREEN
  (`W33-1.8-green-app-document.log`): 307 checks passed, 0 FAIL lines.

Per-commit gates (logs under `build/w33/`; each commit's quoted gates
include a clean client-name scan and no client-string hashes; the two
fixture commits carry fixtures only and quote their own
`check_fixtures.py` lines -- `3b53b9d` reports fixtures OK on the tree it
was made from, `964594d` reports exactly one "differs from HEAD" line for
the staged replacement, the wave-28 precedent -- and every behaviour commit
quotes fixtures OK with the count in the table):

| commit | builds | tests | fixtures |
|---|---|---|---|
| W33-1.1 `22218de` | core, app, app_qt exit 0 | core 54/54; app 25/25; app_qt 38/38 (37 before) | 139 hashes / 173 `.mpp` |
| W33-1.2 `f18b208` | core, app, app_qt exit 0 | core 54/54; app 25/25; app_qt 38/38 | 139 hashes / 173 `.mpp` |
| W33-1.3 `3c996b0` | core, app, app_qt exit 0 | core 54/54; app 25/25; app_qt 38/38 | 139 hashes / 173 `.mpp` |
| W33-1.4 `9660453` | core, app, app_qt exit 0 | core 54/54; app 25/25; app_qt 38/38 | 139 hashes / 173 `.mpp` |
| W33-1.5 `0c83489` | core, app, app_qt exit 0 | core 54/54; app 25/25; app_qt 39/39 (38 before) | 139 hashes / 173 `.mpp` |
| W33-1.6 `95e484a` | core, app, app_qt exit 0 | core 54/54; app 25/25; app_qt 40/40 (39 before) | 139 hashes / 173 `.mpp` |
| W33-1.7 `3b53b9d`+`964594d`+`397eaf9` | core, app, app_qt exit 0 | core 54/54; app 25/25; app_qt 41/41 (40 before) | 140 hashes / 174 `.mpp` |
| W33-1.8 `6a58e05` | core, app, app_qt exit 0 | core 54/54; app 25/25; app_qt 41/41 | 140 hashes / 174 `.mpp` |

The G1 marker commit is docs-only; the code tree it records is `6a58e05`.

## Found, not fixed

- Known blocker for the wave push (wave owner only): `3b53b9d` added the
  slack fixture as imported synthetic-base bytes, and synthetic-base still
  carries the pre-wave privacy residual recorded for W33-4.3 (the operator
  name in SummaryInformation and in one Var2Data value, plus the local
  path), so the pre-push guard refuses that commit.  `964594d` replaced the
  tip blob with an equal-length neutral version (21 bytes changed, every
  other byte identical, size unchanged); section 0 forbids rewriting or
  force-pushing history and working round the guard, so only the wave owner
  can authorise a rewrite or another disposition.  Scans of HEAD and of the
  G1 tip's staged snapshot are clean.  `guard_names.py 22218de^..HEAD`
  (`build/w33/W33-G1-guard-range.log`) refuses on `3b53b9d` alone with its
  "blob ... embedded local path or operator name" line; the blocker is that
  intermediate blob, not the G1 tip.
- The Project save used by `mppwrite_roundtrip_add_delete` carries a stale
  VarMeta logical count (41 declared, 42 physical).  W33-1.1's core test
  normalises it on its build-tree copy so only the summary guard is
  measured; the committed fixture is untouched.  The quirk is not a G1
  item and no writer change is made for it.
- `scripts/check_fixtures.py` is CRLF at HEAD because the wave-32 merge
  commit `17f5782` rewrote the whole file (W33-4.2 restores LF).  The nine
  lines W33-1.7 adds to it are LF, so that commit adds no CRLF and
  `git diff --check` is clean.
- CLOSE is out of scope for this group session: the fresh-tree gates on the
  final commit, the review pass over the wave-33 diff, the push and the CI
  dispatch belong to the CLOSE section (wave-33-brief section 2).

WAVE-33-G1-COMPLETE

## G2 - robustness (Medium and Low)

| item | finding | status | evidence |
|---|---|---|---|
| W33-2.1 | D-2 (Medium): the writer-side DIFAT walk was unbounded | **DONE** | commit `227105f`; gates `build/w33/W33-2.1-*.log` |
| W33-2.2 | D-5 (Low, reproduced): the MPP14 Resource Fixed2Meta count was taken from the header, not the stream | **DONE** | commit `d19e201`; gates `build/w33/W33-2.2-*.log` |
| W33-2.3 | D-6 (Low): CFB v4 was accepted by a parser whose sector formula is v3 | **DONE** | commit `60d8f06`; gates `build/w33/W33-2.3-*.log` |
| W33-2.4 | D-3 (Medium): the fuzz lane fed only `mpp::load_bytes`; no sanitizer job existed | **DONE** | commit `a069d97`; gates `build/w33/W33-2.4-*.log` |

No G2 item was deferred.

W33-2.1 detail:

- `CfbDocument::parse` took `num_difat` from the header and walked the
  continuation chain with neither a visited set nor a file-size bound, while
  the read-only reader (`core/mpp/cfb.cpp`) had both.  A header edit
  (declared count 0xFFFFFFFF, continuation sector pointing at itself) made a
  direct `CfbDocument::from_file` caller append ids until `bad_alloc`.  The
  writer now mirrors the reader: the declared continuation count must fit the
  file's sector count, a continuation sector may be visited only once, and the
  resulting FAT-sector id list may not exceed the file's sectors.  The count
  bound is what the brief's 0xFFFFFFFF fixture hits; the visited set stops a
  self-pointing chain whose declared count is plausible.
- The new gate is `mpp_cfb_hardening` (`tests/cfb_hardening_tests.cpp`, built
  with `cfb_writer.cpp` and `atomic_write.cpp` and registered with ctest).
  It patches the committed fixture in memory only; a missing committed fixture
  is a FAILURE with its path, never a skip.
- RED on the pre-fix tree (`build/w33/W33-2.1-red-build.log`, writer source
  from HEAD with only the fix stashed; `W33-2.1-red-difat.log`):
  `timeout 5 mpp_cfb_hardening --case difat` -- no parser error, killed at
  5 s (exit 124).  GREEN (`W33-2.1-green-build.log` clean;
  `W33-2.1-green-difat.log`): `PASS: 16 checks` -- the pristine fixture loads
  through both parsers; the 0xFFFFFFFF fixture is refused by `mpp::load_bytes`
  and by `CfbDocument::from_bytes`, each naming the DIFAT, under 0.1 s; the
  plausible-count self-pointing fixture is refused by both with the loop
  reason, under 0.1 s.

W33-2.2 detail:

- The MPP14 Resource Type path took the Fixed2Meta header's declared entry
  count as the loop bound.  A stream that is header-only (16 bytes, so
  `available == 0`) can declare 0xFFFFFFFF: `available % count` is then 0 with
  stride 0 and the loop from entry 3 to 4.29 billion runs as cheap
  `continue`s -- the same rows after 4.3 billion extra iterations (the audit
  measured reader_dump 0.02 s -> 2.2 s; the RED run below measured 3.87 s).
  The count is now `min(declared, available / kRscFixed2MetaEntrySize)`.
- The sibling count sites were audited rather than changed: the Task
  Fixed2Meta and every FixedMeta path derive their count from the stream
  length, and the MPP12 Resource Fixed2Meta loop walks `rsc_rows` with every
  entry range-checked, so a declared word cannot size it; a comment now says
  so.
- RED (`build/w33/W33-2.2-red-build.log`, reader source from HEAD with only
  the fix stashed; `W33-2.2-red-fixed2meta.log`): 1 failed of 7 checks --
  "the row loop is bounded by the stream size; loaded in 3872.4 ms".  The
  other six passed: the patched file loads and returns the same task and
  resource uid rows as the pristine fixture.  GREEN
  (`W33-2.2-green-build.log` clean; `W33-2.2-green.log`): `PASS: 23 checks`
  (16 from W33-2.1 plus 7 here): the patched fixture loads under 0.1 s with
  the same task uids and the same resource uids.

W33-2.3 detail:

- The writer accepted major version 4 and a 4096-byte sector shift while
  `sector_offset()` is the v3 header-relative formula; the whole-sectors
  check then rejected every real v4 file with an accidental error.  The
  reader was looser still: it accepted version 4 and any sector shift from
  9 to 16.  `.mpp` is always v3 and no v4 fixture exists to measure a correct
  implementation against, so the brief's default is taken: both
  `core/mpp/cfb.cpp` and `core/mppwrite/cfb_writer.cpp` refuse the version
  itself with "CFB v4 (4096-byte sectors) is not supported; only v3
  (512-byte sectors) is", and both refuse a sector shift other than 9 with a
  message that names the shift.
- RED (`build/w33/W33-2.3-red-build.log`, both parser sources from HEAD with
  only the fix stashed; `W33-2.3-red-v4.log`): 6 failed of 17 checks -- both
  parsers accepted the v4 tag and the 4096-byte shift, and the accidental
  messages were "sector 256 lies past the end of the file" (reader) and
  "file is not a whole number of sectors" (writer).  GREEN
  (`W33-2.3-core-build.log` clean; `W33-2.3-green.log`): `PASS: 42 checks` --
  a v4 tag on the v3-shaped body is refused by both parsers with a v4
  message; a v4 body with 4096-byte sectors is refused with the v4 message,
  not the sector-size error; a v3 tag with a 4096-byte shift is refused for
  the shift; every refusal is under 0.1 s and the pristine v3 fixture still
  loads.

W33-2.4 detail:

- The fuzz lane was one-eyed: `fuzz_reader` mutated the synthetic `.mpp` but
  only ever called `mpp::load_bytes`, so `CfbDocument::from_bytes` (hardened
  in W33-2.1) never saw a mutated byte; no target called
  `core::io::import_mspdi`; and no CI job compiled with a sanitizer.
- `fuzz_reader` now passes every mutated case to
  `mppwrite::CfbDocument::from_bytes` as well.  The contract is the writer's
  own: a `mppwrite::Error`, or a `CfbDocument` whose entry and stream counts
  agree (at least one entry, no more streams than entries); any other
  exception, crash or hang fails the case with its seed.  It is on by
  default; `--no-writer-parse` exists to bisect.  The pristine baseline and
  the real-file mutation loop are covered too, and the first eight cases
  replay both parsers.
- `fuzz_xml` is new: a deterministic target over the three committed MSPDI
  samples (`tests/io_sample_minimal.xml`, `io_sample_tasks.xml`,
  `io_sample_full.xml`), six mutation families through
  `core::io::import_mspdi`, with the `MspdiError`-or-bounded-document
  contract (task, resource, assignment, link and warning counts and all text
  bounded by the file size), then every imported document through
  `core::io::export_csv` with the output parsed as RFC 4180 and checked for a
  constant column count.
- The audit names a CSV importer; none exists in this tree (`core/io/csv.h`
  is export-only; the only CSV files are `csv.cpp`, `csv.h` and
  `tests/io_csv_tests.cpp`, and no `read_csv`/`import_csv`/`parse_csv`/
  `from_csv` exists).  `fuzz_xml` therefore fuzzes the exporter -- the surface
  that does exist -- and the absence is recorded in the commit and in
  `tests/fuzz/README.md`.
- `tests/fuzz/CMakeLists.txt` can now build `core_io` standalone (the
  standalone configure builds and runs `fuzz_xml`).  `.github/workflows/ci.yml`
  gains the `ubuntu-asan` job: clang,
  `-fsanitize=address,undefined -fno-sanitize-recover=all`,
  `CORE_WARNINGS_AS_ERRORS=ON`, and `ctest -E plan_recalc` with
  `MPP_NO_CORPUS=1` (the fuzz targets included).  The job cannot be
  dispatched from this session; the brief requires it green before CLOSE.
- RED (the coverage gap) on HEAD `60d8f06`,
  `build/w33/W33-2.4-red-coverage.log`: the writer parse on HEAD exists only
  at source line 347 on the pristine bytes, while the mutated bytes reach only
  line 648's `mpp::load_bytes`; no MSPDI/CSV reference exists anywhere under
  `tests/fuzz`; the registered targets are `fuzz_reader`, `fuzz_writer` and
  `fuzz_parsers`.  GREEN: `W33-2.4-fuzz-reader-long.log` (2000 cases,
  `PASS fuzz_reader: 18 checks, 0 failures`; writer side parsed 1101,
  `mppwrite::Error` 899); `W33-2.4-fuzz-xml-long.log` (2000 cases,
  `PASS fuzz_xml: 10 checks, 0 failures`; 154 imports, 1846 `MspdiError`,
  154 CSV exports checked); `W33-2.4-fuzz-xml-standalone.log` (the standalone
  tests/fuzz configure builds and runs `fuzz_xml` too); and an MSVC ASan
  local smoke (`W33-2.4-msvc-asan-fuzz-reader.log`,
  `W33-2.4-msvc-asan-fuzz-xml.log`) passes the same 2000 cases each.  The
  local smoke is not the CI job.

Per-commit gates (logs under `build/w33/`; each commit's quoted gates
include a clean client-name scan, a clean working-tree guard and no
client-string hashes):

| commit | builds | tests | fixtures |
|---|---|---|---|
| W33-2.1 `227105f` | core build exit 0 | core ctest `-E plan_recalc` 54/54 (53 before; `mpp_cfb_hardening` is the new 54th); `mpp_cfb_hardening` 16 checks | 140 hashes / 174 `.mpp` |
| W33-2.2 `d19e201` | core build exit 0 | core ctest `-E plan_recalc` 54/54; `mpp_cfb_hardening` 23 checks | 140 hashes / 174 `.mpp` |
| W33-2.3 `60d8f06` | core build exit 0 | core ctest `-E plan_recalc` 54/54; `mpp_cfb_hardening` 42 checks | 140 hashes / 174 `.mpp` |
| W33-2.4 `a069d97` | core build exit 0 | core ctest `-E plan_recalc` 55/55 (54 before; `fuzz_xml` is the new 55th); `fuzz_reader` 18 checks; `fuzz_xml` 10 checks | 140 hashes / 174 `.mpp` |

G2 group gates (fresh trees configured from scratch on the G2 code tree
`a069d97` under `build/w33-g2-fresh{,-app,-qt}`, log
`build/w33/W33-G2-fresh-gates.log`; the marker commit that follows adds this
file's G2 section only):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 55/55 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failure(s) / 8 recorded (7 pass, 1 fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1136 checks / 456 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app / app_qt configure and build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| app ctest / app_qt ctest | 25/25 passed / 41/41 passed (the first app_qt run was 36/41: Smart App Control blocked three freshly linked binaries at image load; the same three targets were relinked from the same sources and the suite reran 41/41, `build/w33/W33-G2-appqt-retry.log`) |
| `check_fixtures.py` | fixtures OK (140 documented hashes, 174 tracked `.mpp`); fixture paths OK (223 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `wave33..HEAD` / `guard_names wave33..HEAD` | 0 files, 0 name hits / 0 commits / exit 0 |

The run started 2026-10-29 23:45 and finished 2026-10-30 00:13.

## Found, not fixed

- The audit's D-3 names an MSPDI XML importer and a CSV importer.  The MSPDI
  importer exists and is now fuzzed.  No CSV importer exists in this tree;
  `fuzz_xml` feeds the CSV exporter instead and both the commit and
  `tests/fuzz/README.md` record the absence.  Adding an importer was not a
  G2 item.
- The `ubuntu-asan` job is written and structurally reviewed but cannot be
  dispatched from this session (no workflow dispatch from this environment);
  the brief requires it green before CLOSE, so CLOSE owns the dispatch and the
  green record.
- The local MSVC ASan smoke used `/fsanitize=address` with the MSVC linker's
  LNK4044/LNK4302 warnings and only `abort_on_error=1` (MSVC ASan rejects
  `detect_leaks=1`); it is evidence for this machine, not a substitute for the
  clang ASan+UBSan CI job.
- The first app_qt gate run lost five tests to the host's Smart App Control
  policy: it blocked the newly linked `msproject_qt.exe`,
  `qt_table_date_format_tests.exe` and `qt_split_view_gate_tests.exe` at image
  load (CodeIntegrity events 3033/3077, policy
  `{0283ac0f-fff1-49ae-ada1-8a933130cad6}`; `VerifiedAndReputablePolicyState`
  is 1).  The older tree's same-named binaries ran, as did a renamed copy of
  one, so the block is on those three file hashes, not on the path, the test
  code or the G2 sources.  Relinking the same three targets from the same
  sources produced hashes that load, and the full suite then passed 41/41
  (`build/w33/W33-G2-appqt-retry.log`).  No source, test or build flag was
  changed and no security setting was touched; if the policy starts blocking
  future wave binaries, how this host runs local gates is the owner's
  decision.
- W33-2.3 refuses v4 by design, as the brief's default says, because no v4
  fixture exists to measure a correct implementation against.  A correct v4
  implementation and its round-trip test remain unmeasured.
- CLOSE is out of scope for this group session: the fresh-tree gates on the
  final commit, the review pass over the wave-33 diff, the push and the CI
  dispatch belong to the CLOSE section (wave-33-brief section 2).

WAVE-33-G2-COMPLETE

## G3 - gates and CI (High and Medium)

| item | finding | status | evidence |
|---|---|---|---|
| W33-3.1 | C-2 (High): the `app/` suite never ran on any CI job; `app-macos.yml` named 15 of its 25 tests | **DONE** | commit `1b51802`; gates `build/w33/W33-3.1-*.log` |
| W33-3.2 | C-5 (Medium): the save-pair mask hid effects and the appended tail was not compared | **DONE** | commit `c25d7b8`; gates `build/w33/W33-3.2-*.log` |
| W33-3.3 | C-6 (Medium): a missing committed fixture printed SKIP and exited 0 | **DONE** | commit `c92100a`; gates `build/w33/W33-3.3-*.log` |
| W33-3.4 | C-8, C-9, C-11, C-12 (Low): both app_qt counts, pin truth, the first-pass corpus flag, the render test's name | **DONE** | commit `f6bbb96`; gates `build/w33/W33-3.4-*.log` |

No G3 item was deferred.

W33-3.1 detail:

- `.github/workflows/ci.yml` now configures `app/` as the standalone CMake
  project it is on both matrix jobs (`APP_BUILD_TESTS=ON`,
  `CORE_WARNINGS_AS_ERRORS=ON`), builds it and runs its ctest with
  `MPP_NO_CORPUS=1`; the job timeout rises to 45 minutes for the second tree.
  Until this commit none of the 25 headless app tests -- every save-pair gate
  against a Project-saved file among them -- ran on any CI job on any OS.
- `.github/workflows/app-macos.yml` now runs on every push (branches and tags)
  as well as dispatch, and its registration step derives the expected names
  from `app/CMakeLists.txt`'s `add_test(NAME ...)` calls instead of a
  hand-typed list: it diffs `ctest -N` against the declarations in both
  directions, so an unregistered test and a stale declaration both fail.  The
  old list named 15 of the 25 tests.  At the G3 tree the same step reports
  registered 25 / declared 25 with an empty diff
  (`build/w33/W33-G3-registration.log`).
- The CI jobs themselves are not dispatchable from this session; CLOSE owns
  the push and the green logs, including this item's acceptance ("the CLOSE CI
  logs show the app ctest total on both OSs").

W33-3.2 detail:

- `tests/save_pair_noise.h` no longer masks the whole 16-byte block around a
  control-vs-null difference: the mask is exactly the bytes where control and
  null differ, for every kind.  Positions beyond the null save's length are no
  longer masked either -- they are the appended tail and are compared directly
  with Project's edit save (`tail_bytes` / `tail_effects`).  `compare()` takes
  `effects_declared` and sets `effects_ok`, so a stream the test declares
  changed cannot pass with a masked-away effect set.  `Kind::fixed_block`
  becomes `Kind::fixed`; `props14_marker_payload()` reads the edit marker so a
  pair declares Props14 only when the control still stores 0.
- The intent was to find what the wide mask hid, and it did.  Project's editor
  save resets ONE flag byte in two 214 sub-streams while the port copies the
  control's byte: `214/CEdl/VarMeta +21` port 0x02 / Project 0x01 (5 actuals
  pairs, 3 outline pairs), `214/Props +11403` port 0x02 / Project 0x01 (the
  same pairs), and `214/Props +8902` port 0x31 / Project 0x43 (1 actuals
  pair).  The mask was NOT widened back: each is a named expected failure with
  its stream and offset, any other mismatch is a hard failure, and a fixed
  byte makes the expected failure itself fail, so an entry cannot rot.
  `app_edits_outline_tests` gains the same `check_expected_failure` mechanism
  the actuals and core pair tests already had.
- Pair totals: before the mask change actuals 3782 checks passed / 50 expected,
  outline 3237 passed, task mode 331 passed, core outline 1749 checks /
  7 expected.  Narrowing alone, before the expected-failure record: actuals 11
  failures, outline 6, core 6
  (`W33-3.2-{actuals,outline,core-outline}-after.log`).  Green: actuals 3799
  checks passed / 61 expected, outline 3245 passed / 6 expected, task mode 337
  passed, core outline 1757 checks / 0 failures / 13 expected.  Whole suites:
  app ctest 25/25, core ctest 55/55 (`MPP_NO_CORPUS=1`, `-E plan_recalc`).

W33-3.3 detail:

- `tests/fixture_guard.h` is new: `require_committed()` prints "FAIL: committed
  fixture missing: <path>" and exits 1; `corpus_present()` / `corpus_absent()`
  print "SKIP: corpus absent (<path>)[: why]" for the git-ignored corpus only.
  The header states the rule: `tests/data`, `tests/*.tsv` and `analysis/*.tsv`
  are committed, so a missing file is a FAILURE; `corpus/` may be absent and
  only there is SKIP allowed.
- The sweep that found the sites (`build/w33/W33-3.3-skip-lines.txt`) listed
  171 SKIP lines across the core and app suites; 78 `require_committed()` call
  sites and 117 corpus guards replace them.  Every committed save-pair control
  the app suite reads now fails with the path instead of returning green.  Ten
  app test targets gain the `tests/` include directory for the shared header.
- 11 executable SKIP prints remain, each by design: 6 corpus-gated (four fuzz
  probes, props and layout_model), 2 MPXJ-written corpus variants excluded
  because they are not Project's layout, 2 generated W45 fixtures produced by
  `mppwrite_roundtrip_add_delete` (not committed; a hard failure would break a
  clean build), and 1 build-mode probe (`core/mpp` CFB layer not linked).
- Acceptance: renamed `tests/data/wave28-update-control.mpp`, ran
  `app_edits_actuals_tests` -- exit 1 with "FAIL: committed fixture missing:
  <path>"; restored immediately and `git status --porcelain tests/data` was
  empty.  Whole suites on the final tree: app ctest 25/25; core ctest 56/56
  with corpus present and 56/56 with `MPP_NO_CORPUS=1`; the no-corpus run
  exercises the new "corpus absent" branches, the corpus run the present ones.
- `app/app_edits_links_tests.cpp` was committed with CRLF before this wave;
  this commit's added lines are LF (W33-4.2/G4 restores the file).

W33-3.4 detail:

- C-8: `docs/START-HERE.md`, `docs/handoff/wave32-complete.md` (two gate rows)
  and `docs/handoff/wave32-verification/gates.md` now quote both counts:
  "app_qt 37/37 locally, 35/35 in CI", because `qt_stored_table_corpus` and
  `qt_task_order_corpus` are registered only where `corpus/project_a.mpp`
  exists.  The wave-32 numbers stay wave-32 numbers; at this wave's tree the
  same registry has 41 tests locally and 39 without corpus.
- C-9: `app_qt/tools/command_backlog.py` now parses `app_qt/ribbon_pin.tsv`
  and puts the pin's own count in the generated Summary: 114 command controls
  (kinds button/small/split), **47 enabled / 67 disabled**.  The table above it
  reports 69 disabled because a split is counted by its menu items; the
  summary names the two splits whose button is live while every menu item is
  disabled: Project > Properties > WBS and Project > Schedule > Set Baseline.
  The regenerated `docs/ui-command-backlog.md` gains two summary lines;
  `scripts/check_command_backlog.py` reports 114 pin controls, 114 backlog
  rows, 0 mismatches.
- C-11: the first CI Test step (`ctest -E plan_recalc`) sets
  `MPP_NO_CORPUS=1`, so a self-hosted or developer runner proves the
  committed-fixture path on the first pass instead of silently running the
  corpus-aware branches.  The exact command run locally with the flag: 55/55,
  exit 0, 8.70 s (`W33-3.4-core-firstpass.log`).
- C-12: `qt_render_parity` is renamed `qt_render_1920` in
  `app_qt/CMakeLists.txt`, `app_qt/README.md`, `docs/ui-parity-spec.md` and the
  two memory notes.  It asserts "rendered 1920x1032" and never compared
  against the reference; the comparison is `scripts/parity_report.py`, which
  needs the git-ignored `corpus/`.  The ctest registry lists `qt_render_1920`
  and no `qt_render_parity`; the test passed (0.26 s) in the 41/41 run.
  `docs/parity.md`'s "in that run" sentence is a wave-18 record and keeps the
  old name.

Per-commit gates (logs under `build/w33/`; each commit's quoted gates include
`git diff --check` exit 0, a clean client-name scan of HEAD, no client names in
the commit messages on `wave33..HEAD`, a clean `guard_names wave33..HEAD` and
no client-string hashes):

| commit | builds | tests | fixtures |
|---|---|---|---|
| W33-3.1 `1b51802` | app build exit 0 | app ctest 25/25; CMake-derived registration 25 registered / 25 declared, empty diff | 140 hashes / 174 `.mpp` |
| W33-3.2 `c25d7b8` | app and core builds exit 0 | app ctest 25/25; core ctest 55/55 (`MPP_NO_CORPUS=1`, `-E plan_recalc`); actuals 3799 checks / 61 expected, outline 3245 / 6, task mode 337, core outline 1757 / 13 expected | 140 hashes / 174 `.mpp` |
| W33-3.3 `c92100a` | app build exit 0; core build exit 0 | app ctest 25/25; core ctest 56/56 with corpus and 56/56 with `MPP_NO_CORPUS=1`; the renamed-fixture acceptance failed with its path and was restored | 140 hashes / 174 `.mpp` |
| W33-3.4 `f6bbb96` | app_qt configure/build exit 0 | app_qt ctest 41/41 (`qt_render_1920` 0.26 s; `qt_command_backlog` and both corpus-gated tests passed); core first CI command with `MPP_NO_CORPUS=1` 55/55; backlog 114/114, 0 mismatches | 140 hashes / 174 `.mpp` |

G3 group gates (fresh trees configured from scratch on the G3 code tree
`f6bbb96` under `build/w33-g3-fresh{,-app,-qt}`, log
`build/w33/W33-G3-fresh-gates.log`, started 2026-10-30 01:29 and finished
01:50; the marker commit that follows adds this file's G3 section only):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 55/55 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failure(s) / 8 recorded (7 pass, 1 fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1136 checks / 456 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| app ctest / the CI step with `MPP_NO_CORPUS=1` | 25/25 passed / 25/25 passed |
| app_qt configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| app_qt ctest | 41/41 passed; the same registry has 39 entries without `corpus/` (the two corpus-gated tests are not registered) |
| the `app-macos.yml` registration step, run locally | registered 25 / declared 25, empty diff (`W33-G3-registration.log`) |
| `check_fixtures.py` | fixtures OK (140 documented hashes, 174 tracked `.mpp`); paths OK (223 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `wave33..HEAD` / `guard_names wave33..HEAD` | 0 files, 0 name hits / 0 commits / exit 0 |

## Found, not fixed

- The three 214 flag-byte signatures W33-3.2 exposed remain: Project's editor
  save resets one byte the port copies from the control --
  `214/CEdl/VarMeta +21` port 0x02 / Project 0x01 (5 actuals pairs, 3 outline
  pairs), `214/Props +11403` port 0x02 / Project 0x01 (the same pairs) and
  `214/Props +8902` port 0x31 / Project 0x43 (1 actuals pair).  They are named
  expected failures in the actuals, outline and core outline pair tests; the
  mask was not widened back and any other mismatch is a hard failure.  A
  measured writer for Project's editor flag reset is not in this wave.
- The 11 executable SKIP prints recorded under W33-3.3 are by design, not
  leftovers: 6 corpus-gated, 2 MPXJ-written corpus variants, 2 generated W45
  fixtures and 1 build-mode probe.  A committed fixture now never skips.
- The backlog table still counts a split by its menu items, so its "69
  disabled" stays as the table's number; the generated Summary now states the
  pin truth (47 enabled / 67 disabled) and names the two enabled splits.
  Giving the row itself a split-level status would need the generator to model
  a split's button separately from its menu, which the pin/generator contract
  does not carry today.
- `qt_render_1920` is still the render, not the comparison: the comparison is
  `parity_report.py` against the reference capture, which needs the git-ignored
  `corpus/` and therefore cannot run on a CI runner.  The brief allowed either
  the rename or a real comparison; this wave took the rename.
- The CI jobs changed by W33-3.1, W33-3.4 and G2's `ubuntu-asan` cannot be
  dispatched from this session.  CLOSE owns the push and the green logs,
  including W33-3.1's acceptance that the CLOSE CI logs show the app ctest
  total on both OSs.
- `docs/handoff/wave32-verification/gates.md` remains CRLF except this wave's
  added LF line; W33-4.2 (G4) restores LF on the whole file.

WAVE-33-G3-COMPLETE
