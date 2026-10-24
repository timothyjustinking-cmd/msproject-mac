# Wave 28 - the completion record

One table row per brief item.  A group ends only when its exact
`WAVE-28-<group>-COMPLETE` line is appended below (wave-28-brief section 1).
Deferrals need the attempt evidence the standing rules and the brief name.

## G0 - the wave-27 verifier findings (Critical/High only)

All four items are DONE.  W28-0.1 has a follow-up commit: its first commit left
the operator's name inside the re-saved calendar blob, and the pre-push guard
refused that blob; the name was then replaced in place without touching the
scheduling payload.

| item | finding | status | evidence |
|---|---|---|---|
| W28-0.1 | High: seven fixtures embed the operator's local save path | **DONE** | commits `076438f`, `d1b5a8a`; gates `build/w28/gates-bc90915-W28-0.1.log`, `build/w28/gates-076438f-W28-0.1-name.log` |
| W28-0.2 | High: outline numbers pin impossible values | **DONE** | commit `3dbdce0`; gates `build/w28/gates-d1b5a8a-W28-0.2.log` |
| W28-0.3 | Medium (promoted): the fixture builder reads past its FAT vector | **DONE** | commit `2b365e5`; gates `build/w28/gates-3dbdce0-W28-0.3.log` |
| W28-0.4 | Medium (promoted): Unlink under standing rule 1 | **DONE (KEPT)** | commit `01c9a0b`; gates `build/w28/gates-2b365e5-W28-0.4.log` |

W28-0.1 detail:

- Six fixtures were re-derived through Project 2021 COM on the neutral root
  `C:/w28` in one session with neutral Author, Manager, Company, UserName and
  Title, reproducing the edit each records (`scripts/oracle_w28_privacy.py`).
  `synthetic-calendars.mpp` is the engine suite's calendar base whose oracle is
  pinned to its bytes, so its two path strings were replaced IN PLACE with
  neutral same-length strings (`scripts/w28_neutralise_path.py`); every other
  byte, including the scheduling payload, is unchanged.
- `scripts/check_fixtures.py` now fails on any tracked test-data file that
  embeds a drive path (at either byte alignment) or a user-profile path; the
  only allowed drive root is the neutral `C:\wNN`.  Falsified against the
  pre-fix copies: 14 leaked strings; the fixed tree reports 0 failures over 68
  tracked test-data files.
- Follow-up `d1b5a8a`: the path pass had left the operator's name in
  `synthetic-calendars.mpp` SummaryInformation (two ANSI values) and one
  Var2Data value (UTF-16LE).  `scripts/w28_neutralise_operator.py` replaces the
  name in place with an equal-length neutral string (the name itself is passed
  on the command line, never written into the script): three strings, 21
  changed bytes, file size unchanged.  The new SHA-256 is pinned in
  `check_fixtures.py`, `engine_diff.py` and `tests/data/README.md` with the
  reason.  The task-calendar scenario's recorded expectations still hold (the
  engine suite passes) because only those text bytes moved.
- History is not rewritten; the old blobs still hold the path and the name,
  and that decision is Tim's (see the blocker below).

W28-0.2 detail:

- Project measured through COM on copies under `build/oracle-work`
  (`scripts/oracle_w28_outline.py`; numbers in
  `build/w28/W28-0.2-project-outline.json`): Tasks(1).OutlineNumber = "1",
  Tasks(12).OutlineNumber = "2"; the twelve task rows measure 1, 1.1, 1.1.1,
  1.1.2, 1.2, 1.3, 1.3.1..1.3.5, 2.  The old expectations ("2" ... "1") were
  impossible.  `wave24-order.mpp`, whose stored row order differs from its ID
  order, measures 1..5 by ID.
- `core/mpp/reader.cpp`: both outline derivations now sort every sibling group
  by the stored ID column (Task::row_id for MPP12; the FixedData ID word for
  the MPP14 Fixed2Data pass), with the row index as a deterministic
  tie-breaker.  The stored outline level stays authoritative.
- `app_qt/tests/task_order_test.cpp`: corpus expectations now hold Project's
  measured values, and `case_order` asserts OutlineNumber on the committed
  `wave24-order.mpp`, so CI without `corpus/` fails if the stored-order
  derivation returns (before the fix, ID 2 would read "5").

W28-0.3 detail:

- `tests/mpp_fixture_builder.h` sized the FAT vector as `total * 4` entries
  while the writer reads `fat_sectors * (kSector / 4)` = 128 per FAT sector.
  The resize is now exactly what the loop reads and still above the highest
  index `set_fat` wrote (`total - 1`), so it never shrinks a live entry.
- Debug confirmation: before the fix `mpp_error_paths` and `cal` did not finish
  (two live test processes, no output; killed after 613.7 s and 33.7 s), while
  `mpp_perf_large`, whose file is large enough for the old allocation, passed.
  After the fix the same Debug binaries pass 3/3 in 3.08 s.

W28-0.4 detail:

- The port's Unlink save removes the same links as Project's own Unlink save:
  the three link-table streams are byte-identical to
  `tests/data/wave27-unlink.mpp`.  Project's own save also rewrites ID 4's
  stored date cache (start 2026-12-16 17:00 -> 2026-12-11 08:00, finish
  2026-12-18 17:00 -> 2026-12-14 17:00, slack 0 -> 19200), which the port's
  no-CPM save leaves as the base has it; ID 3's stored dates are unchanged.
- Standing rule 1's test for this item is Project's reopen of the PORT's save.
  Measured through COM on copies under `build/oracle-work`
  (`scripts/oracle_w28_unlink.py`; facts in
  `build/w28/W28-0.4-project-reopen.json`): Project 2021 (automatic
  calculation) reopened the port's save and Project's own save with 0
  differing fields over the project row and IDs 1..6 -- start, finish,
  duration, OutlineNumber, Predecessors, Successors and each task's
  TaskDependencies count.  Both reopens remove the same links (dependency
  counts on IDs 3, 4 and 6 fall 2->1, 2->0 and 1->0); every other row is
  unchanged.  No displayed date differs between the base and either Unlink
  save after reopen, and an explicit CalculateAll changes nothing, so the
  on-open read was already Project's recalculated schedule.  The stored date
  cache the port does not write is therefore invisible to Project.
- Unlink stays enabled with the no-reschedule save.  The app test comment now
  records the measurement instead of claiming the comparison is "Unlink's
  bytes only".

Group gates (fresh trees configured from scratch under
`build/w28-fresh{,-app,-qt}`, serial, tree `01c9a0b`, log
`build/w28/g0-fresh-gates.log`):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 51/51 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks passed |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded, 7 pass, 1 expected fail |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 checks / 420 checks passed |
| M2 `recalc --compare` | 332/332 fields, 0 mismatches |
| app ctest / app_qt ctest | 17/17 / 23/23 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (46 documented hashes, 26 tracked `.mpp`); paths OK (68 files); provenance OK (8 oracles) |
| client-name scan on HEAD / `guard_names` wave range | 0 files / 0 hits; `wave27..HEAD` refuses only the 076438f intermediate blob (exit 1); hex grep no matches (exit 1, expected) |

The gate script's own exit code is 1 because its last command is the hex grep
(no matches is its success); every numbered stage above exited 0.  The
`guard_names` refusal is the wave-level blocker below, not a G0 failure: the
final tree's `synthetic-calendars.mpp` is clean (the HEAD scan and the path
check pass); only the blob committed in `076438f` still carries the name.

## Found, not fixed

- **The pre-push guard still refuses the wave branch because of commit
  `076438f`'s intermediate blob.**  The final tree is clean (W28-0.1's
  follow-up replaced the name), but the blob added in `076438f` still carries
  the operator's name, and `guard_names.py wave27..wave28` scans every added
  blob in the range.  Section 0 forbids rewriting history or working round the
  guard.  Only Tim can authorize a rewrite of that commit or rule on the
  guard; until then CLOSE cannot push the branch.  This is recorded here and
  in `d1b5a8a` rather than resolved locally.
- **Two pre-wave fixtures still embed the operator's name at HEAD.**
  `tests/data/synthetic-base.mpp` and `tests/data/wave23-outline.mpp` each
  store it twice in SummaryInformation (ANSI) and once in a Var2Data value
  (UTF-16LE); scan `build/w28/W28-0.4-operator-scan.log` (3 hits each, no
  string printed).  They were added in earlier waves, are outside the
  `wave27..wave28` push range, and `synthetic-base.mpp` is the engine suite's
  base with seven scenario oracles and two scripts pinned to its hash.  The
  same in-place method could clean the tip, but it would move those pins and
  would not remove the historical blobs; the tree also carries the name in 69
  tracked text files (historical docs).  A repo-wide name purge is a history
  decision for Tim, not a fixture edit under this wave.
- **Project's stored date cache and its displayed schedule differ.**  Project's
  own Unlink save stores ID 4 at 2026-12-11..12-14 but displays
  2026-12-17..12-18 after the automatic recalculation on open; the same
  happens to the base.  This is why the reopen comparison, not the stored
  bytes, is the right test for W28-0.4.  No action for the port: it neither
  reads nor writes that cache as a schedule.

WAVE-28-G0-COMPLETE

## G1 - the actuals writer

All three items are DONE.

| item | finding | status | evidence |
|---|---|---|---|
| W28-1.1 | measure Project's % Complete edits (auto 0->40, 40->100, 100->0; manual 0->40) | **DONE** | commits `ae1a065` (four pairs, README, hashes), `18a11ce` (oracle); measurement `build/w28/W28-1.1-pair-diff.log`; gates `build/w28/gates-W28-1.1-fixtures.log`, `build/w28/gates-W28-1.1-oracle.log` |
| W28-1.2 | actuals writer; refuse a percent edit on a row with no saved task; re-enable the five % buttons and Task Information's percent field; every pair asserts the non-owned streams | **DONE** | commits `a181229` (refusal + tests), `137bca7` (writer + % controls); gates `build/w28/gates-a181229-W28-1.2.log` |
| W28-1.3 | Mark on Track and Update Project on the same writer, each with its own Project pair; Update Project's OK live for the covered options | **DONE** | commits `cd83e31` (three pairs), `2362916` (oracle + status-date rule), `92f45e8` (reader + writer + UI); gates `build/w28/gates-2362916-W28-1.3.log` |

W28-1.1 detail:

- Four same-session Project 2021 save pairs on copies under `C:/w28` with
  neutral metadata: auto row 1 0->40, 40->100 and 100->0, and the manual row
  0->40.  The word-level diff is `build/w28/W28-1.1-pair-diff.log`; the pairs
  fix the field list the writer had to produce: % Complete, Actual and
  Remaining Duration, % Work Complete, Actual Start/Finish, Resume/Stop, the
  +168 progress word and +174 anchor, the 0x0B40015E Var2Data step block, the
  FixedMeta variable-field count and the project row's roll-up.
- Project's own 50 % save (`wave27-percent-actuals.mpp`) is the standing-rule-1
  reference for the percent path and the source of the measured Var2Data field
  0x0B40015E the wave-26 review asked for.

W28-1.2 detail:

- `core/mppwrite/actuals.{h,cpp}` writes the measured actuals and edits VarMeta's
  logical entries, the 0x0B40015E step block and Var2Data step ids, merging into
  the original streams; the manual-finish charge follows core/plan's rule.
  `app/edit_model.cpp` step 5a writes the set_percent edits through it.
- The four W28-1.1 pairs match FixedData and VarMeta byte-for-byte, Var2Data
  after masking the per-session step ids, FixedMeta except the edited row's
  count, and every stream outside the four owned task streams equals the
  control.  The app test carried 432 checks at that commit.
- `a181229` first made `make_percent_edit` refuse a uid with `file_row == 0`
  (a pending insert's value would otherwise be dropped while the save reported
  success), with the refusal case in `app_edits_actuals_tests`.
- The five ribbon % buttons and Task Information's Percent complete field are
  enabled on that writer and queue the same set_percent edit; the sheet's
  Percent complete column stays read-only.

W28-1.3 detail:

- Three more Project 2021 pairs (`cd83e31`): Mark on Track on the auto row 1,
  Mark on Track on the manual row 2, and Update Project 0%-100% over the whole
  project, all through Wed 2026-01-07 00:00.  Mark on Track has no type-library
  member, so the oracle (`2362916`) invokes Project's ribbon button through UI
  Automation under the oracle lock.
- Measured and implemented on the same writer:
  * the percent-through rule: `finish = add_work(start, duration)` for an auto
    row, `manual_finish_of` for a manual row; 100 % at/after the finish, 0 % at
    or before the start, else the working time between (`app::percent_through`,
    with the manual start day's partial charge).  The exact elapsed ticks are
    stored, not Duration*percent/100 (the manual 83 % row is 1200 of 1440
    minutes); the step block stores the unrounded percent (83.33333333333333);
    a computed 0 % has no actual time.
  * date updates leave FixedData +168 at its existing 0x0080 (a percent edit ORs
    0x9000); +174 is still the actual start.
  * every written row with progress loses StartSlack (+186 := 0); at 100 % it
    also loses FreeSlack/TotalSlack (+74/+190 := 0) and stores ActualStart/
    ActualFinish in the late pair (+112/+116).  A 0 % row keeps all of them.
  * the project row's Resume/Stop are the project start for Mark on Track and
    the percent pairs, but Update Project's are the earliest valid per-row
    resume/stop (Tue 12:00 on the update pair).  The discriminator is the
    command's status-date flag, not "date update vs percent edit".
  * Update Project stores its through date as the project status date
    (`114/Props` record 0x02400045, kind 4, four bytes, NA -> 0x3bf40000); the
    reader now decodes it into ProjectRow/PlanInput, and the writer patches only
    those four bytes.  Mark on Track leaves the record alone.
- UI (`92f45e8`): Mark on Track is enabled on the ribbon split's main click
  (its unmeasured drop-down item stays disabled) and reads the file's stored
  status date; Update Project's OK computes the date update for Entire project
  or Selected tasks.  "100% complete" and rescheduling remain disabled with the
  exact decision-9 tooltip.  Files without the status-date record are refused
  before the dialog with an honest message.
- Tests: `app_edits_actuals_tests` now checks all seven save pairs (874 checks),
  including the update pair's status-date word and the Props stream (exactly
  four bytes differ from the control; the mark pairs' Props equals the control),
  the percent-through values against the update pair, and group/mixed edit-model
  cases (undo/redo, a pending delete displacing the whole update, a later
  percent edit winning one target).  `qt_ribbon_inventory` clicks Mark on Track
  on the committed status-date fixture and checks the queued update; the
  `qt_project_dialog_gate` checks the dialog's live and disabled choices.
- Docs: `docs/START-HERE.md` and `app/README.md` describe the enabled commands
  and the measured status-date write; `docs/ui-command-backlog.md` was
  regenerated (Mark on Track is `implemented`), with the generator taught that
  a split whose main click runs a real command is implemented.

Group gates (fresh trees configured from scratch under
`build/w28-fresh{,-app,-qt}`, serial, tree `92f45e8`, log
`build/w28/g1-fresh-gates.log`):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 51/51 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks passed |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded, 7 pass, 1 expected fail |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 checks / 420 checks passed |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match |
| app ctest / app_qt ctest | 18/18 / 23/23 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (60 documented hashes, 40 tracked `.mpp`); paths OK (82 files); provenance OK (8 oracles) |
| client-name scan on HEAD / `guard_names` wave range | 0 files / 0 hits; `wave27..HEAD` refuses only the 076438f intermediate blob (exit 1); hex grep no matches (exit 1, expected) |

The app ctest output in the gate log is a rerun of the same command on the same
tree: the first append hit a Windows file lock while this session was reading
the log, and the script's own exit for that stage was 0.  The gate script
exited 0; the `guard_names` refusal is the wave-level blocker inherited from
G0, not a G1 failure.

WAVE-28-G1-COMPLETE

## G2 - outline editing

Both items are DONE.

| item | finding | status | evidence |
|---|---|---|---|
| W28-2.1 | Indent and outdent: measure Project's saves (a leaf under a sibling, a summary with children, outdent back), implement the writer, enable the ribbon buttons, shortcuts and menu items; undo/redo | **DONE** | commits `b427bde` (three pairs, README, hashes), `2bb3bca` (oracle), `669d49f` (writer), `40a5827` (shell); measurement `build/w28/W28-2.1-project-outline-edit.json`; gates `build/w28/gates-669d49f-W28-2.1-*.log` |
| W28-2.2 | The sheet and Gantt reflect the new outline at once (collapse triangles, indentation, summary bars) | **DONE** | commit `8027462`; gates `build/w28/gates-40a5827-W28-2.2-*.log` |

W28-2.1 detail:

- Three Project 2021 save pairs in one continuous session each, on neutral
  copies under `C:/w28` with neutral metadata: indent (ID 2, a leaf, under ID
  1; ID 1 becomes a summary), summary (ID 3 under ID 2, then ID 2 with its
  child under ID 1) and outdent (the indent shape, then ID 2 back to the top
  level).  The pairs are `tests/data/wave28-outline-{indent,summary,outdent}`
  and their `-control` files; the word-level diffs are
  `build/w28/W28-2.1-full-diff-{indent,summary,outdent}.log`.
- The pairs fixed the six owned `TBkndTask` streams: FixedData
  +84/+96/+100/+140/+142/+164/+172/+174 (roll-up duration, earliest/latest
  child, has-children/has-parent, duration format, outline level, start
  anchor); Fixed2Data parent identity, +42 flags and +46 start word;
  Fixed2Meta +88/+95; FixedMeta +2 and the variable-field count, +26 summary
  bit; Var2Data 0x0B40007C/0x4E4/0x4E5/0x505/0x506 (marker and child list).
  A summary's roll-up duration is `0x2EE0` (1200 min); outdent restores
  `0x5DC0` (2400 min), format 7, start `0x3bf212c0`, finish `0x3bf627d8`.
  The writer never changes the effective date pair +104/+108, so displayed
  dates stay put on indent and outdent.
- `core/mppwrite/outline.{h,cpp}` writes Indent/Outdent onto those streams.
  The core round-trip test applies each pair and compares the port's save with
  Project's field by field: every non-owned stream equals the control,
  Fixed2Data equals after canonicalising Project's per-save identity GUIDs,
  and the edited row's opaque FixedMeta +8..23 blob is the only masked range
  (+2/+26 still match).  `TBkndAssn` is not written; its row counts are
  reported, not skipped silently.  560 checks, 0 failures.
- `app/edit_model.cpp`: `StructuralKind` outline_indent/outline_outdent and
  `make_outline_edit` read the file's stored outline and project calendar —
  the display-level match, the DFS roll-up span from working time between the
  subtree's +104/+108, and the outdent restore words.  Save step 5b calls
  `write_outline_edits` once; a refusal is a warning, not a failed save.  The
  display replay sets level/OutlineNumber/the summary flag and takes the
  summary +84 from the queued duration, leaving +104/+108 alone.  Undo also
  recovers the rows owned by an outline edit sitting on the redo stack; without
  that the sheet kept the summary, shifted level and roll-up duration after
  Undo (found by the new ribbon click check).  A pending delete of either end
  displaces the edit, and undo restores it.
- UI: the Task tab's Indent/Outdent buttons are enabled on this writer; Edit
  gains Indent Task / Outdent Task on Option+Shift+Right / Left with no
  Command (Qt maps the named arrows, AppKit maps the NS arrow function keys).
  Both shells drive the same `make_outline_edit`/`push` path; the Qt handler
  refuses an unmeasured shape with a status message.  The generated command
  backlog lists both as implemented.
- `app_edits_outline_tests` walks all three pairs: the measured words, push,
  display against Project's reopened files, undo/redo including recovery from
  the displayed plan, the real save cascade, the six streams and the untouched
  non-owned streams — 942 checks, 0 failures.  The ribbon inventory pins
  Indent/Outdent enabled, checks their wiring, clicks each on the committed
  control (one queued outline edit, the measured summary/restored durations on
  the display) and undoes the indent through the menu; `menu_tests` checks the
  two items' titles, keys, enablement and edit guard — 986 checks.

W28-2.2 detail:

- The new `qt_outline_display` opens W28-2.1's Project-saved indent and outdent
  controls, drives the real Edit > Indent Task / Outdent Task actions and
  checks the live views with no save and no reopen:
  * sheet: uid 2 level 0 -> 1, indent 0 -> one `SheetMetrics::indent_per_level`,
    OutlineNumber "1.1" (uid 3 renumbers 3 -> 2); uid 1 is a summary with a
    working collapse triangle — `toggle_collapsed` hides uid 2's row and
    expanding restores the row count; the outdent control reverses all of it
    and the former summary's triangle refuses to collapse;
  * Gantt: uid 1's bar becomes kind `summary` / shape `summary_bracket` while
    uid 2 stays a normal rectangle; outdent returns uid 1 to a normal bar;
  * Undo restores the flat display at once and redo reapplies it in both
    cases.
- This item adds no save path.

Group gates (fresh trees configured from scratch under
`build/w28-fresh{,-app,-qt}`, serial, tree `8027462`, log
`build/w28/g2-fresh-gates.log`):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks passed |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded, 7 pass, 1 expected fail |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 checks / 420 checks passed |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match |
| app ctest / app_qt ctest | 19/19 / 24/24 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (66 documented hashes, 46 tracked `.mpp`); paths OK (88 files); provenance OK (8 oracles) |
| client-name scan on HEAD / `guard_names` wave range | 0 files / 0 hits; `wave27..HEAD` refuses only the 076438f intermediate blob (exit 1); hex grep no matches (exit 1, expected) |

The gate script exited 0; the `guard_names` refusal is the wave-level blocker
inherited from G0 (an intermediate blob history cannot rewrite), not a G2
failure.

WAVE-28-G2-COMPLETE

## G3 - new-task mode

W28-3.1 is DONE.

| item | finding | status | evidence |
|---|---|---|---|
| W28-3.1 | Measure Project's save of the project's new-task mode (`114/Props` record `0x024013c8`) when changed in Options > Schedule; write it; Insert Task uses it; the status bar follows it | **DONE** | commits `fc7272e` (four pairs, README, hashes), `5d22f54` (oracle), `d2e89c7` (writer, edit model, app test), `feb0ba1` (shell, status bar, Insert Task, backstage gate), `da7f030` (insert-probe correction); measurement `build/w28/W28-3.1-project-new-task-mode.json`; gates `build/w28/gates-d2e89c7-W28-3.1-qt-{build-1,gate}.log`, `build/w28/gates-feb0ba1-W28-3.1-app-{test-recheck,ctest-recheck}.log` |

W28-3.1 detail:

- Two Project 2021 save pairs in one continuous session each, on neutral copies
  of `tests/data/synthetic-base.mpp` under `C:/w28` with neutral
  Author/Manager/Company/UserName/Title: manual -> auto and auto -> manual.  The
  files are `tests/data/wave28-newtask-manual-control.mpp` /
  `wave28-newtask-auto.mpp` and `wave28-newtask-auto-control.mpp` /
  `wave28-newtask-manual.mpp`, 239104 bytes each, SHA-256 pinned.  Project's own
  reopen reads the property back flipped in both directions
  (`manual_to_auto`: True -> False; `auto_to_manual`: False -> True; oracle log
  `build/w28/W28-3.1-oracle.log`).
- The measured record is `114/Props`, id `0x024013c8`, kind 2, length 2, payload
  at stream offset 2998 in these four fixtures.  Project stores `0x00FF` for
  manual and `0x0000` for auto; the base fixture's `0x0001` also reads as
  manual.  The two pair diffs (`build/w28/W28-3.1-pair{1,2}-diff.log`) show that
  one semantic byte plus Project's own per-save churn in 15 of 96 streams:
  SummaryInformation byte +232, the two opaque Props id payloads at 921/2859
  (`0x02400029`, `0x024013c2`), Fixed2Data identity bytes in
  TBkndTask/Assn/Cal/Rsc, 214/CEdl FixedData +147 and its Var2Data/VarMeta
  rewrite, CFilter/CGrouping/CTable/CV_iew, 214/Props and Props14's length.  The
  port changes only 114/Props and leaves the opaque ids as the base has them.
- `core/mppwrite/project_props.{h,cpp}` walks the 16-byte stream header and the
  12-byte record headers, checks the record's shape, replaces the two payload
  bytes with Project's measured values, rewrites the stream and returns the
  previous value; a missing, truncated or wrong-shape record throws instead of
  writing.  `app::make_new_task_mode_edit` refuses a file without the record
  before anything is queued (an original Project 2007 MPP12 file), naming
  `0x024013c8` in the message.
- Edit model: `StructuralKind::set_new_task_mode` (11), last edit wins, one
  writer op; save step 5d runs after the document hook and before the final
  write, so the mode lands in the container the hook produced.  The edit touches
  no task row: `StructuralEdits::apply` ignores it, and `save_structural_edits`
  only adds the one Props write.
- Shell (`feb0ba1`): with a saved document, Options > Schedule queues the edit
  and File > Save writes it; after a structural save the shell reloads the
  target, so the saved mode becomes the new baseline.  The status bar shows the
  loaded file's stored mode with the last pending mode edit applied, recomputed
  in `after_structural_change`, so undo/redo walk back to the file's value (the
  Qt gate triggers the real Edit-menu actions).  Insert Task stamps the shown
  mode into the new row (`Fixed2Meta` entry byte +8 bit `0x80`; false `0x7f`,
  true `0xff`).  A file without the record shows the reason and queues nothing;
  with no document the mode stays the shell's live new-project mode.
- Insert Task's mode is measured, not assumed: Project 2021's own `InsertTask`
  probe (json `insert_probe`; not a committed fixture) creates its new row (new
  unique id 4) with Manual=True under the manual option and Manual=False under
  auto.  Project inserts before the active row and renumbers, so the new row is
  the new unique id at ID 1, not the newly occupied ID 4; `da7f030` corrects the
  oracle's log line, which had picked the old row that moved to ID 4.  The
  port's Insert Task therefore matches Project's own command
  (`build/w28/W28-3.1-insert-probe-recheck.log`).
- Tests: `app_edits_new_task_mode_tests` (1933 checks, 0 failures) walks both
  pairs: read the stored mode, make/push/undo/redo, the real save cascade, the
  port's Props against Project's edited Props record by record (masking only
  Project's two opaque id payloads), every other stream byte-identical to the
  control, exactly one changed stream, and the reader's read-back.  The insert
  check runs both controls x both modes and asserts the display placeholder and
  the saved row carry the flag.  The missing-record check splices the record
  out, proves the reader reports unknown, the helper refuses naming
  `0x024013c8`, a hand-pushed edit's save fails, and neither the target nor a
  temp file is left.  `qt_backstage_gate` loads a fixture storing manual, shows
  its stored mode, queues Auto, checks one pending `set_new_task_mode` edit and
  the label, then undoes/redoes through the Edit menu
  (`build/w28/gates-d2e89c7-W28-3.1-qt-gate.log`).

Group gates (fresh trees configured from scratch under
`build/w28-fresh{,-app,-qt}`, serial, tree `da7f030`, log
`build/w28/g3-fresh-gates.log`):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks passed |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded, 7 pass, 1 expected fail |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 checks / 420 checks passed |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match |
| app ctest / app_qt ctest | 20/20 / 24/24 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (70 documented hashes, 50 tracked `.mpp`); paths OK (92 files); provenance OK (8 oracles) |
| client-name scan on HEAD / `guard_names` wave range / G3 range | 0 files / 0 hits; `wave27..HEAD` refuses only the 076438f intermediate blob (exit 1); `7fe86e8..HEAD` 0 hits (exit 0); hex grep no matches (exit 1, expected) |

The gate script's own exit code is 1 because its last command is the hex grep
(no matches is its success); every numbered stage above exited 0, including
the 52 core, 20 app and 24 app_qt tests.  The `guard_names` wave-range refusal
is the wave-level blocker inherited from G0 (an intermediate blob history
cannot rewrite), not a G3 failure.

WAVE-28-G3-COMPLETE

## G4 - the Task Form edits

W28-4.1 is DONE.

| item | finding | status | evidence |
|---|---|---|---|
| W28-4.1 | Make the docked Task Form's fields editable through the same edit model and save paths as the sheet (no new write path; disabled where the sheet's is). Selection sync and undo covered | **DONE** | commit `167beea`; gates `build/w28/gates-c233ac8-W28-4.1-app-build-all.log`, `-app-ctest-all.log`, `-qt-build-all.log`, `-qt-ctest-all.log`, `-app-ctest.log`, `-qt-gate-final.log`; fresh `build/w28/g4-fresh-gates.log` |

W28-4.1 detail:

- The form reports a committed field; it never writes. `TaskForm` gained a
  `TaskFormField` enum and a `CommitHandler`. Return, or focus leaving a field,
  calls back into `MainWindow::commit_task_form_field`, which builds the same
  `TaskInformationValues` Task Information's OK builds and passes it to
  `apply_task_information(values, "task form")` -- the path that already exists.
  That method IS the sheet's edit model: Name/Duration/Start/Finish go through
  `SheetModel::begin_edit`/`set_edit_text`/`commit_edit(false)`, so they share
  the sheet's undo stack and the Start save diff; a changed percent is parsed and
  queued as the measured `StructuralKind::set_percent` edit (W28-1.2). No writer
  was added. `apply_task_information`'s new `source` parameter defaults to
  "task information", so the dialog's status messages are unchanged.
- Enabled exactly where the sheet is: `refresh_task_form` looks up each field's
  column in the live `SheetModel` and enables the field only when the column
  exists and is not read-only (`set_field_enabled` sets both the read-only and
  the enabled state). Predecessors and Resources stay disabled in the widget:
  the stored table declares those columns read-only, link edits are the Link
  dialog, and there is no assignment writer; each carries a tooltip saying so.
  Percent has no sheet cell but keeps its measured structural path, so it stays
  enabled.
- Selection sync and stale commits: `refresh_task_form` now records the uid the
  texts were populated for (`task_form_uid_`) and clears it before every early
  return. A commit is applied only while the live selection is still that uid;
  otherwise it is dropped and the form re-populates. A programmatic `setText`
  never emits `editingFinished`, so a refresh cannot queue an edit, and a commit
  equal to the task's current value is a no-op (no undo step and no identical
  percent save).
- Undo is proved through the real Edit > Undo action in the split-view gate: a
  renamed Name commits through the sheet model and is reversed; a Start commits
  (exactly one pending start edit, document dirty) and is reversed; typing 50 %
  queues exactly one `set_percent` edit (`percent == 50`) and is reversed. Each
  time the sheet cell and the form are checked before and after. An unchanged
  Name committed with Return adds no edit and no undo step.
- The percent undo exposed a display-plan gap: `StructuralEdits::apply` writes
  `percent_complete` from a `set_percent`/`update_actuals` edit, but an UNDONE
  edit is not replayed and `without_structural_edits` copied the shown row's
  value, so the sheet (and the form) kept showing the percent the file no longer
  had. It now restores the file's stored `percent_complete` for every uid a
  percent edit in the redo list touched -- the same shape as the W28-2.1 outline
  restore. This also fixes the ribbon, Mark on Track and Update Project undo
  display; it changes no save.
- Tests: `app_edit_tests` gains the undone-percent display check; the
  split-view gate gains the enabled/disabled matrix, the three commit/undo
  cycles and the unchanged-value no-op. Full app ctest 20/20 and app_qt ctest
  24/24 pass, including `qt_ux_gate` (Task Information's apply still uses the
  same path).

Group gates (fresh trees configured from scratch under
`build/w28-fresh{,-app,-qt}`, serial, tree `167beea`, log
`build/w28/g4-fresh-gates.log`):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks passed |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded, 7 pass, 1 expected fail |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 checks / 420 checks passed |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match |
| app ctest / app_qt ctest | 20/20 / 24/24 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (70 documented hashes, 50 tracked `.mpp`); paths OK (92 files); provenance OK (8 oracles) |
| client-name scan on HEAD / `guard_names` wave range / G4 range | 0 files / 0 hits; `wave27..HEAD` refuses only the 076438f intermediate blob (exit 1); `c233ac8..HEAD` 0 hits (exit 0); hex grep no matches (exit 1, expected) |

The gate script's own exit code is 1 because its last command is the hex grep
(no matches is its success); every numbered stage above exited 0, including the
52 core, 20 app and 24 app_qt tests. The `guard_names` wave-range refusal is the
wave-level blocker inherited from G0 (an intermediate blob history cannot be
rewritten), not a G4 failure.

WAVE-28-G4-COMPLETE

## G5 - split task

W28-5.1 is DEFERRED, with the measurement evidence the brief requires.

| item | finding | status | evidence |
|---|---|---|---|
| W28-5.1 | Split Task on a leaf task: measure Project's one split and decide from the bytes whether the writer can produce it | **DEFERRED** | commits `cc803c1` (oracle), `64c3b4c` (pair, README, hashes), `1d4e830` (notes wording); measurement `build/w28/W28-5.1-project-split.json`, `build/w28/w28-5.1-split-pair-diff.txt`; decision `build/w28/W28-5.1-split-decision.log`; gates `build/w28/W28-5.1-oracle-script-gate.log`, `build/w28/W28-5.1-fixture-pair-gate.log`, `build/w28/g5-fresh-gates.log` |

W28-5.1 detail:

- Measurement. `Application.SplitTask` is the interactive split tool: the COM
  call waits for a split date and the Guard had to kill WINPROJ after 300 s
  (`build/w28/probe_split3.py`).  The split is therefore written through the
  object model the UI itself updates, on an automatic leaf: with aware-UTC
  dates (a naive datetime is shifted by the local timezone), setting
  `Task.SplitParts(1).Finish = 2026-01-07 17:00` and then
  `SplitParts.Add(2026-01-09 08:00, 2026-01-12 17:00)` turns automatic ID 1
  from 5 days (Mon 2026-01-05 08:00 -> Fri 2026-01-09 17:00, one part) into the
  same 5 days in two parts (Mon 08:00 -> Wed 17:00 and Fri 08:00 -> Mon
  2026-01-12 17:00).  The committed pair
  `tests/data/wave28-split-control.mpp` / `wave28-split.mpp` comes from ONE
  continuous Project 2021 session on a neutral `C:/w28` copy with neutral
  metadata (`scripts/oracle_w28_split.py`); Project's reopen of both files
  reads the facts back exactly (`build/w28/W28-5.1-project-split.json`).
- The bytes.  The split is stored on the ASSIGNMENT, not in the task table:
  assignment uid 1's `0x0F400031` planned-work contour is a 100-byte flat
  block in the control (VarMeta address 8, length word 100, first content u32
  `0x00200000`, two 28-byte records) and a 128-byte segmented block in the
  split (address 396, length word 128, first content u32 `0x00200003`, three
  28-byte records); the VarMeta entry moves 8 -> 396 and
  `TBkndAssn/Var2Data` grows 396 -> 528 (+132, the block and its length word),
  the old block at 8 now unreferenced.  The assignment finish moves +3 days.
  Task row 1 moves +100/+108/+116 (stored, effective and late finish) +3 days
  and gains a previously unmeasured `+168` bit `0x2000` (0x80 -> 0x2080).  The
  project row's Duration and RemainingDuration go 24000 -> 28800 ticks (+4800,
  one working day) and its +100/+108/+116 finish words +3 days; rows 2-3 gain
  4800 ticks (one working day) of free/start/total slack, with LateStart
  +1 day and LateFinish +3 days.  `TBkndTask` FixedMeta, VarMeta and Var2Data
  do not change:
  `SplitParts` is not a stored task field Project rewrites (the field appears
  only in the generated name table, `core/model/field_table.gen.cpp:254`).
- Decision: the writer cannot produce this in this wave.  `mppwrite` decodes only
  the FLAT planned-work contour (72 bytes when it writes one,
  `core/mppwrite/add_delete.cpp:2160-2165`, `add_delete.h:612`); its contour
  builder writes that flat template only and REFUSES a uid that already has a
  contour entry (:3054-3057), and the measured actuals writer records
  `TBkndAssn work contours` as NOT WRITTEN (`core/mppwrite/actuals.h:48`), with
  every save-pair test requiring the non-owned streams to equal the control.
  The segmented 128-byte block, its 28-byte cumulative work/time records and
  the VarMeta relocation are not decoded anywhere in the port; the task-table
  side is a whole-plan reschedule (project duration, every row's slack/late
  dates) plus a new progress bit, not a local row write.  Implementing it
  would need a decoded segmented-contour format, a contour replace writer, a
  full CPM reschedule save and a measured split-date policy in the app.  The
  committed pair is what a later writer's save is compared with field by
  field; this wave adds no save path.
- The button is unchanged and stays disabled with the exact tooltip:
  `app_qt/ribbon_bar.cpp:291` passes `enabled=false` and the control builder
  sets `"not implemented in this port"` (:628-630); the handler
  `app_qt/main_window.cpp:2049-2051` is still the unwired no-op.  The ribbon
  inventory gate checks every disabled control carries that exact string
  (`app_qt/tests/ribbon_inventory_test.cpp:313`).
- Full measurement/decision log: `build/w28/W28-5.1-split-decision.log`
  (local, like every build/ artifact); the pair, its hashes and the README
  row are committed so the bytes can be re-read without Project.

Group gates (fresh trees configured from scratch under
`build/w28-fresh{,-app,-qt}`, serial, tree `64c3b4c`, log
`build/w28/g5-fresh-gates.log`):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks passed |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded, 7 pass, 1 expected fail |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 checks / 420 checks passed |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match |
| app ctest / app_qt ctest | 20/20 / 24/24 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (72 documented hashes, 52 tracked `.mpp`); paths OK (94 files); provenance OK (8 oracles) |
| client-name scan on HEAD / `guard_names` wave range / G5 range | scan on HEAD: 0 files / 0 hits; `wave27..HEAD` still refuses the intermediate `synthetic-calendars.mpp` blob from 076438f (exit 1, the known G0 blocker); `3fe29cc..HEAD` 0 hits (exit 0); hex grep no matches (exit 1, expected) |

The gate script exited 0; every gate in the table exited 0 except the two
listed with exit 1 for known reasons: the hex grep's 1 is its no-match success,
and the wave-range guard's 1 is the wave-level blocker inherited from G0 (an
intermediate blob history cannot be rewritten), not a G5 failure.  The G5 range
is 0.

WAVE-28-G5-COMPLETE
