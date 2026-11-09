# Wave 35b G4 review: the Lows

Range: `af78097..90d8b4e` (25 commits), excluding the coordinator's G3 records
`docs/handoff/wave35b-verification/coord-gates-g3.md` and `review-g3.md` (swept into `ec07ab7`).
Brief: `docs/handoff/wave35b-brief.md`, "### G4" and section 1.  Builder's record:
`docs/handoff/wave35b-verification/gates-g4.md`.  Date: Mon 9 Nov 2026.

Reviewer: independent, adversarial, read-only.  Nothing in the repository was built, run or
edited.  Every statement below comes from reading the source and tests at `90d8b4e` and the
commit messages.  Two checks ran outside the build: the 51 README hashes (R35G3-9) were
recomputed from the committed blobs, and the 114/Props record sizes (R35G0-10) were read from
the committed pair fixtures with a scratch script.  Line numbers are at `90d8b4e`.

Verdict: 0 Critical, 0 High, 1 Medium, 4 Low.

## Per id

| Id | Commit | Verdict | Evidence |
|---|---|---|---|
| R35G0-6 | `51b3ef8` | Closed | `assignment_plan_refusal` has a `calendar_base_uid < 0` branch ahead of the `!= 1` branch, with the text "has no calendar row of its own" (`app/edit_model.cpp:2978-2983`).  The case needle is that text (`app/app_edits_assignment_tests.cpp:643-644`).  The writer already refuses the shape: `check_resource_calendar` fails on `mine_count != 1` (`core/mppwrite/assignment_growth.cpp:833-837`). |
| R35G0-7 | `dacb8a7` | Closed for the assignment refusals | `ResourceRow::calendar_base_uid` now defaults to -1 and `TaskRow::type_block_known` to false (`app/layout_model.h:148, 231`).  Only the reader sets them: `reader_adapter.h:137` (`resource.calendar_base_uid`) and `:194` (`type_block_known = true`).  `plan_input_from_document` is the only production source of file rows (Qt `edit_model.cpp:3262`, macOS `main.mm:845`).  `sync_plan_from_records` and `apply()` copy the base rows (`layout_model.cpp:389`), so a measured row keeps the flag and no measured path is newly refused.  The pending rows (`placeholder_row`, `edit_model.cpp:1229`; Add Resources, `:4879`) now fail closed.  The tests have their own needles: "was not read from the file", and the -1 text for a default-built row (`app_edits_assignment_tests.cpp:632-638`).  The same pattern is left in Calculate (R35bG4-4). |
| R35G0-8 | `0cc8d85` | Closed (documentation) | `spec/mpp-records.md` row 128 now carries a dated erratum.  It marks the word PLAUSIBLE (Created), not measured, and keeps the writer's refusal. |
| R35G0-9 | `678fb45` | Closed | The task WBS key on uid 0 is refused by the generic project-row gate (`core/mppwrite/varmeta_edit.cpp:1060-1066`).  The test is `tests/roundtrip_varmeta.cpp:739-741`.  Its needle "uid 0 is the project row" is shared with the notes case two lines up.  That is acceptable because the generic gate is the only guard on that path, and the red run (accepting `kTaskWbsFieldId` in `project_row_append_measured`) is the right mutation. |
| R35G0-10 | `6d1a801` | Closed | `measured_old_length` (`core/mppwrite/props_edit.cpp:121-131`) is checked after the "would keep its payload" check and before the payload pin (`:444-450`).  The six "from" sizes match each pair's own control, read from the fixtures: `wave30-wbs-control` 0x0240138b 4 B and 0x024013be 2 B (edit 24 and 6); `wave34-field-set-control` 0x02401392 2 B (edit 2) and 0x024013c5 8 B (edit 16); `wave35-calculate-unlinked-control` 0x02400003 4 B and 0x02400024 2 B.  No measured caller is newly refused.  `wbs_code.cpp:215-216` already requires 4 and 2, and `calculate.cpp:465, 500` already require 2 and 4.  No `save_props_edit` caller writes 0x02401392 or 0x024013c5 (the field-set path is `app/document.cpp`).  The test resizes each record to one byte under its old size, asserts the control's size first, and the needle carries both numbers (`tests/roundtrip_props.cpp:917-951`). |
| R35G0-11 | `dd59f78` | Closed | `static_assert` ties both app constants to the writer's (`app/edit_model.cpp:51-55`).  The refusal prints `hex_u32` for both ids (`core/mppwrite/viewstream_edit.cpp:612-617`).  The needle is the full hex sentence (`tests/roundtrip_viewstream.cpp:638-645`). |
| R35G0-12 | `ec07ab7` | Closed; the record's note holds | A positive `push()` control per direction sits ahead of the shape loops (`app/app_edits_assignment_tests.cpp:650-663`).  The record says the review's "stays green" claim did not hold.  That is the builder's mutation result, and nothing in the test contradicts it. |
| R35G0-13 | `0438005` | Closed | `identity_collision_problem` compares the 16-byte +0 with every Assignment, Task and Resource Fixed2Data record at its own stride.  It runs on Project's save and on the port's output, and two planted collisions prove it can fail (`tests/roundtrip_assignment_growth.cpp:436-469, 499-525`). |
| R35G1-10 | `bdbcd7e` | Partly closed (R35bG4-2) | The substitution loop is correct.  `at = error.find(shared, at + pair.size())` restarts just past the inserted label.  The loop cannot repeat forever when `pair` contains "task-mode pair", and it skips no later occurrence, because the text after a replaced match starts exactly at `at + pair.size()`.  `pair == shared` is skipped (`core/mppwrite/task_mode.cpp:684-691`).  The exception path, though, returns before the substitution (`:679-682`). |
| R35G1-11 | `c3765b1` | Closed | `calculate.cpp:523-527` has the review's sentence, and `tests/roundtrip_calculate.cpp` asserts it in full.  No app text says "nothing to calculate" for this case.  The remaining "nothing to calculate" (`calculate.cpp:470-473`) is the not-pending case, where it is true. |
| R35G1-12 | `0699eac` | Closed for the FixedMeta checks, with a gap (R35bG4-3) | Both FixedMeta sites resolve the entry by `task_row_offset(row)` (`calculate.cpp:154-168, 350, 399`).  The lookup is sound: it uses the same key and the same first-match rule as `task_mode.cpp`'s `meta_entry_for_row` (`:172-185`), so both layers check the same entry.  The State constructor fixes FixedMeta at exactly 16 + 47 * (rows + 3) (`task_mode.cpp:289-293`).  `fixed_meta_entry_at` returns only `at` with `at + 47 <= meta.size()`, so dropping `meta.size() < entry_at + kFixedMetaEntry` is sound for `[at + 17]`, `[at + 18]` and the 47-byte loop.  The test swaps the leaf entries, then breaks the moved entry, and needs uid 2 named, which the positional formula cannot give.  The red run quotes "task uid 1" (`tests/roundtrip_calculate.cpp:657-680`). |
| R35G1-13 | `7b2ad9a` | Record accurate | The commit exists on the branch, and its subject is the RW35-W6 uid pin.  The G3 review found it closed. |
| R35G1-14 | `3edd463` | Closed (COM script; read only) | `WORK` is on `C:/w35`.  `app.Calculation = 1` is restored in a `finally` inside `oracle_session` (`scripts/oracle_w35_calculate.py:50, 196-208`).  It uses the same value the repo's other oracles use for automatic. |
| R35G2-9 | `ede7553` | Closed | All three copies of `with_control_path_size` assert both of Project's words equal its CEdl length before overwriting.  The copies are `tests/edl_control_path.h:65-74`, `app/app_edits_show_level_tests.cpp:195-203` and `tests/roundtrip_viewstate_outline.cpp:229-238`. |
| R35G2-10 | `231ab61` | Closed (option 2, the header) | The headers now state that a post-save mismatch leaves `path` holding the new bytes (`viewstate_outline.h:132-141`, `viewstate_sort.h:173-182`). |
| R35G2-11 | `db9001c` | Closed | The texts are "two-level (0-based levels 0,1)" and the 0-based outlines (`edit_model.h:676-679, 697`; `viewstate_outline.cpp:140-142`).  The matrix now says Duration descending only (`viewstate_sort.h:50-52`).  The spec rows agree. |
| R35G3-8 | none | Record accurate | `9b33d6d`, `2634bb0`, `a46d9f9`, `ee4826f`, `639638c` and `2468736` each have no `Generated-By` line.  Recording without a history rewrite is what the brief says. |
| R35G3-9 | `895b620` | Closed | 51 `wave35-g3-*` rows in `tests/data/README.md`, and 51 `wave35-g3-*` blobs in `tests/data/` at `90d8b4e`.  Every SHA-256 recomputed from the committed blob matches its row. |
| R35G3-11 | `38700bc` | Closed (COM script; read only) | `WORK = C:/w35/work-g3` (`scripts/oracle_w35_g3.py:42`). |
| R35G3-12 | `7b0238d` | Closed | Four pins back the 0x00/0x00 gap signature: Project's normalised CEdl is 430 B, the port's is 494 B, the port's equals the control's, and the port's byte 430 is 0x00 (`tests/roundtrip_assignment_growth.cpp:2640-2664`). |
| R35G4-3 | `ee7c63f` | Closed | Five permissive checks (`app/menu_tests.cpp:564-579`), each with its own mutation red. |
| R35G4-4 | `48630a4` | Record accurate | The W35b-1.7 commit message records the macOS name prefill. |
| RW35-W6 | `7b2ad9a` | Record accurate | Reviewed in G3 and found closed. |
| RW35-W7 | `6d3a1bd` | Record accurate | Reviewed in G3 and found closed.  G4's R35G0-10 adds the "from" half. |
| RW35-W8 | `a69943c` | Record accurate | The commit exists on the branch.  `save_calculate` writes through a scratch file and saves the target once (`calculate.cpp:748-795`). |
| RW35-W9 | `0476516` | Record accurate | Reviewed in G3 (3.5). |
| RW35-W10 | `a1fa209` | Closed | The README row names the 0x90 top byte and the other sessions' 0x6b and 0x00. |
| RW35-W11 | `619bcea` | Closed | The comment cites the null-save evidence and the R35G2-9 assertion (`tests/roundtrip_viewstate_outline.cpp:218-224`). |
| RW35-W12 | `425019a` | Closed | The comment is accurate.  No removal is written from the file Project resaved, because every case ends in the refusal.  The port output in that test, from the port-saved control, still goes through `compare_all`, Props14 included. |
| RW35-W13 | none | Record accurate | Left, as the row allows. |
| RW35-R6 | none | Record accurate | `app_qt/ribbon_pin.tsv` lines 60, 69, 71 and 102 are pin 0, and line 133 (Move Project) is pin 1, as stated. |
| RW35-R7 | `1d25bf1` | Closed as the row asks; R35bG3-1 still partly open (R35bG4-1) | Collapse All and Expand All return early when `applied().back()` is a `show_level` (`app_qt/main_window.cpp:2185-2212`, helper `:256-259`).  Undo of a queued Sort names the kept session sort (`:2235-2247`).  The needles "not this expanded state", "not this collapsed state" and the Sort sentence are carried only by the new texts.  The toggle's text is "not this expanded or collapsed row".  Collapse All is exercised on the deep outline, where it is enabled (`outline_persist_test.cpp:139-160, 212-225`; `sort_persist_test.cpp:132-153`).  Both commands stay enabled and refuse on click.  That matches the per-row toggle and the row's "say so in the status line". |
| RW35-R8 | `444731f` | Closed for the five migrated tests; one reason inaccurate (R35bG4-5) | No migrated check lost coverage.  The summary-delete case still accepts, so a regression that opened the box would queue the delete and fail.  The leaf, Insert Task and uid-routing checks still read the captured text.  The two dirty-document prompts now answer with `reject()`, which the shell treats as not-Discard (`main_window.cpp:3801-3805`), and the "keeps the open project" checks still follow.  `wbs_code_gate_test.cpp` and `cell_bold_gate_test.cpp` contain no QMessageBox, QInputDialog, QTimer or `exec()`. |
| RW35-R9 | none | Record accurate | As the row says. |
| RW35-R10 | `e4a380d` | Record accurate | Resolved with RW35-R2 in G1. |

### R35bG3-1 paths under a queued Show Level, at `90d8b4e`

| Path | State | Evidence |
|---|---|---|
| Expand All | Closed | `main_window.cpp:2199-2212`; `outline_persist_test.cpp:139-155` |
| Collapse All | Closed | `main_window.cpp:2185-2198`; `outline_persist_test.cpp:212-225` |
| Delegate toggle of one row | Closed in G3 | `main_window.cpp:6085-6101` |
| Structural undo | Closed (display follows the file) | `main_window.cpp:2234-2247`, the rebuild resets collapse; `outline_persist_test.cpp:156-160` |
| Structural redo after that undo | **Open** | `main_window.cpp:2267-2271` |
| Redo after All Subtasks, or after a refused replacement level | **Open** | `main_window.cpp:6110-6115` |
| Project-summary toggle | **Open** | `main_window.cpp:2329-2349`; `plan_document.cpp:816-831, 775` |

## Findings

### R35bG4-1 (Medium): Redo and the project-summary toggle still show every row while File > Save writes the queued Show Level (R35bG3-1, three of its paths)

- Where:
  - `app_qt/main_window.cpp:2267-2271`: redo calls `structural_.redo()`, then
    `after_structural_change()`, and sets "redo the structural edit".
  - `app/edit_model.cpp:4608-4620`: `StructuralEdits::redo` re-appends the edit without
    `push()`.
  - `app_qt/main_window.cpp:3527-3560`: `after_structural_change` rebuilds through
    `apply_structural_edits` and `rebuild_from_base`, which makes a new `SheetModel` with
    nothing collapsed (`app_qt/plan_document.cpp:775`).  It never re-applies a queued level.
  - `app_qt/main_window.cpp:6110-6115`: `apply_outline_level` withdraws a queued level with
    `structural_.undo()` and no `clear_redo()`.
  - `app_qt/main_window.cpp:2329-2349`: the summary toggle has no queue check, and
    `set_project_summary_visible` rebuilds the sheet (`plan_document.cpp:816-831`).
- What is wrong: `1d25bf1` closes the two bulk commands, which is all RW35-R7 asked for.  The
  G3 review's R35bG3-1 also named the redo and summary paths, and they are unchanged.  The
  G4 record says "Undo of a queued Show Level needed no change, because the rebuild after the
  undo shows every row again".  That is true of Undo alone, but it leaves the level on the
  redo stack.  No test covers redo or the summary toggle: the outline test stops at the Undo
  (`app_qt/tests/outline_persist_test.cpp:156-160`).
- Failure scenarios, all on `wave35-viewstate-outline-level1-control.mpp`:
  1. Level 1, then Ctrl+Z (every row shown), then Ctrl+Y.  Level 1 is queued again, the sheet
     shows every row, and the status line says only "redo the structural edit".  File > Save
     writes UID 1 collapsed and UIDs 2 and 3 hidden.
  2. Level 1, then All Subtasks, then Ctrl+Y.  The result is the same.  A variant: Level 1,
     then Level 3 (refused, so the session shows Level 3), then Ctrl+Y.  The withdrawn Level 1
     comes back under a Level 3 display.
  3. Level 1, then Project Summary Task on or off.  The rebuild expands every row while Level 1
     stays queued.
  The file written is the measured Level 1 pair, so no bad file is written.  The user's view
  and the saved outline disagree, as in R35G2-6.
- Fix:
  - In `after_structural_change`, when `applied().back()` is a `show_level`, call
    `doc_.show_outline_level(level)` after the rebuild.  This covers redo and the summary
    rebuild, if that path is routed the same way.  Or refuse the summary toggle while a level
    is queued, with its own text.
  - In `apply_outline_level`, call `structural_.clear_redo()` after a withdrawal, so a level
    the user replaced or removed cannot come back unseen.
  - Add `qt_outline_persist` cases for redo after Undo, redo after All Subtasks, and the
    summary toggle.  Each should assert that the displayed row count equals the queued
    level's.
  - Correct the RW35-R7 paragraph of `gates-g4.md` so it names the redo path.

### R35bG4-2 (Low): a Calculate refusal raised as an exception still names the task-mode save

- Where:
  - `core/mppwrite/task_mode.cpp:679-682`: the catch returns before the substitution.
  - `:274-276`: "the task-mode save needs `<stream>`, which this file does not have".
  - `:289-327`: the size and geometry throws.
- What is wrong: `read_task_file_shape` retargets only texts that `validate_file` returns.  The
  State constructor's throws skip the loop.  The missing-stream text names "the task-mode save",
  and that text never contains "task-mode pair", so the loop would not catch it anyway.  A
  Calculate Project user who opens a file without, say, `114/TBkndAssn/Var2Data` is told what
  the task-mode save needs.  The refusal is right; the named feature is wrong, which is what
  R35G1-10 is about.  The commit's tests cover only the link-row text.
- Fix: run the substitution on the caught text as well.  Word the constructor's stream text
  as "this save needs ..." (or take the label), and add one Calculate case on a missing
  stream with the full sentence as the needle.

### R35bG4-3 (Low): R35G1-12 stops at FixedMeta, and the order-varied test reaches only one of the two sites

- Where:
  - `core/mppwrite/calculate.cpp:343`: the Fixed2Meta active flag is still read at
    `(row + 3) * kF2MetaEntry`.
  - `:350-364`: the `check_unvaried_parameters` FixedMeta site.
  - `tests/roundtrip_calculate.cpp:657-680`: the only order-varied case.
- What is wrong:
  - `task_mode.cpp:52-54, 577` pairs the Fixed2Meta entry with the FixedMeta entry by index.
    Calculate now resolves FixedMeta by row offset but still reads Fixed2Meta by position, so
    a reordered file checks the active flag of one row against the metadata of another.
  - The test breaks byte 17, which `read_calculate_shape` refuses first (`:410-413`).
    Reverting `:350` to the positional formula would stay green, because the two leaf
    entries are identical outside the skipped bytes 4-15.
  - Both checks require the same bytes of every leaf, so accept and refuse are unaffected.
    Only the uid in the message can be wrong, as in the original finding.
- Fix: read Fixed2Meta at the FixedMeta entry's index, as `task_mode.cpp` does.  Add a second
  swapped case that breaks a `kLeafMeta` byte the +17/+18 check does not read, such as byte 20,
  and needs "task uid 2's FixedMeta metadata flags differ".

### R35bG4-4 (Low): the R35G0-7 fail-closed defaults stop at the assignment rule; Calculate's task checks still read open defaults

- Where: `app/edit_model.cpp:3662-3676` (`calculate_plan_refusal`: `active`,
  `baseline_stored`, `type_block_stored`); the defaults are at `app/layout_model.h:144, 152-153`.
- What is wrong: `active = true`, `baseline_stored = false` and `type_block_stored = false`
  are the measured Calculate shape.  `calculate_plan_refusal` does not consult
  `type_block_known`, so a task row the reader did not build passes those three checks.
  This is the same design issue R35G0-7 named for the assignment rule.  It cannot be reached
  today:
  - a pending row has `file_row == 0` and is refused first (`:3663-3665`);
  - the writer re-reads the active flag, the metadata and the variable keys from the file
    (`calculate.cpp:331-364`, `check_var_keys`).
- Fix: in `calculate_plan_refusal`, refuse `!task.type_block_known` with its own text.  Or give
  `TaskRow` one `read_from_file` flag that every file-derived refusal checks, and set `active`
  to false by default.  Add a `push()` case.

### R35bG4-5 (Low): the RW35-R8 record's reason for leaving `backstage_gate_test` on the one-look timer does not hold

- Where: `app_qt/tests/backstage_gate_test.cpp:454` (the About QMessageBox: reads `text()`,
  then closes), `:644` (the New-from-existing file picker: closes the modal) and `:1114`
  (Project Options: reads `windowTitle()`, then closes); the `444731f` message and
  `gates-g4.md` ("drive a bespoke dialog ... filling or reading its own controls").
- What is wrong: these three timers fill nothing.  They record a QMessageBox's text or a
  dialog's title, then close it.  `gate::PromptAnswerer` does exactly that: it records a box's
  `text` and rejects it, and records any other QDialog's `title` and rejects it.  The About
  case is the platform-sensitive `QTimer::singleShot(0)` look that CI-4 was about, and its own
  comment records two earlier macOS failures.  The other listed files are rightly left: they
  read child widgets, such as Task Information's tabs (`dialog_gate_test.cpp:660-676`).
  `project_dialog_gate_test.cpp` already polls.
- Fix: move the three backstage looks to `PromptAnswerer`, asserting `text` for About and
  `title` for Options.  Or correct the record to say why each stays.

## Clean

- **Trailers and hygiene.**  All 25 commits in the range carry
  `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35b)` and the co-author line, checked
  by script.  The added lines name no client, project title or person.  The only absolute
  paths are the neutral roots `C:/w35/...` and, in a README provenance row, `C:\w30\...`.
- **The substitution loop (`bdbcd7e`).**  It is finite for every `pair`, including one that
  contains "task-mode pair", and an empty one, which shrinks the string at every step.  No
  occurrence is skipped.
- **The FixedMeta lookup (`0699eac`).**  It is bounds-safe.  It agrees with `task_mode`'s entry
  choice, so a duplicate offset is resolved the same way in both checks.  The exact FixedMeta
  size check in the State constructor keeps the entry count at rows + 3.
- **Props old sizes (`6d1a801`).**  All six are confirmed against each pair's own control and
  edit save, as listed in the table.  The check sits before the payload pin, so the new test
  reaches it and not an earlier refusal.  The resize is one byte smaller, so it grows the
  record and no shrink check can fire first.  The guard is writer-internal behind the two
  callers' own size checks.  That is the right layer, because no edit model builds a
  `PropsEdit`.
- **Fail-closed rows (`dacb8a7`).**  No measured path is newly refused, because every file
  row comes from `plan_input_from_document`.  No pending row is newly accepted, because the
  defaults only tighten.
- **RW35-R7 (`1d25bf1`).**  The red runs fail on the row count and the status line, not on the
  queued-edit check, which is green before the fix.  That is the right shape, and the record
  says the first version's needle could not fail.
- **RW35-R8 (`444731f`).**  The answerer's scope ends after the click returns, so no stale
  timer can answer a later dialog.  The summary-delete "no dialog" check is still a real
  check, because a modal opened by a regression would be accepted inside `trigger()`.
- **Comment, README and COM-script commits** (`231ab61`, `895b620`, `a1fa209`, `619bcea`,
  `425019a`, `3edd463`, `38700bc`, `0cc8d85`).  Each says what its review row asked, and none
  changes behaviour.
- **app/main.mm.**  No G4 commit touches it.  Show Level is not wired on macOS, so R35bG4-1
  is Qt-only.
