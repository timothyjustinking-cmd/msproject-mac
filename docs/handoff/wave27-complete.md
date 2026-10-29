# Wave 27 - the completion record

One table row per brief item.  A group ends only when its exact
`WAVE-27-<group>-COMPLETE` line is appended below (wave-27-brief section 1).
Deferrals need the attempt evidence the standing rules and the brief name.

## G0 - the wave-26 Highs (save safety)

All three items are DONE; nothing in G0 is deferred.  Each item's fixture
commit precedes its code commit under the same finding id.

| item | finding | status | evidence |
|---|---|---|---|
| W27-0.1 | High: % Complete saves one word of the eight-plus copies Project writes (`corpus/probe/var-pct.mpp`) | **DONE** | fixtures `d30c2e8`, code `0961f19`; gates `build/w27/gates-d30c2e8-W27-0.1.log`, fixtures `build/w27/gates-fixtures-W27-0.1.log` |
| W27-0.2 | High: % Complete accepted on summary tasks; the refusal lived only in the shell | **DONE** | commit `29f6827`; gates `build/w27/gates-0961f19-W27-0.2.log` |
| W27-0.3 | Medium: Project Information Priority save unmeasured | **DONE (KEPT)** | fixtures `a40ef6d`, code `5a991e9`; gates `build/w27/gates-a40ef6d-W27-0.3.log`, fixtures `build/w27/gates-fixtures-W27-0.3.log` |

W27-0.1 detail:

- `scripts/oracle_w27.py` (committed) opened `tests/data/synthetic-base.mpp` on
  a copy under `C:/w27`, set Task ID 1 `PercentComplete = 50` through Project
  2021 COM, neutralised UserName/Author/Manager/Company/Title and saved
  `tests/data/wave27-percent-actuals.mpp` (239,616 bytes, SHA-256
  `4C9AECBF...CC7AA2`).  A byte-by-byte Task FixedData diff against
  synthetic-base shows the task row changing +80/+81 ActualDuration,
  +88/+89 RemainingDuration, +92 PercentComplete, +120..123 ActualStart,
  +132..139 the two date words, +169 (the +168 flag word) and Var2Data key
  0x0B40015E, and the project row changing +80/+81, +88/+89, +92, +120..123 and
  +132..139.  Project writes eight-plus copies; the port wrote +92 only.
- Because the port has no measured actuals writer, the five Task > Schedule %
  buttons are DISABLED with the exact decision-9 tooltip ("not implemented in
  this port"): the pin (`app_qt/ribbon_pin.tsv`), the live ribbon spec, the
  enabled-wiring list and the live gate all agree.  `MainWindow::
  set_percent_complete` is removed and the five ribbon cases collapse to an
  unreachable return, so no UI route can queue `StructuralKind::set_percent`.
- The edit kind, its save path and its tests are kept and marked NOT ENABLED,
  ready for the wave-28 actuals writer that re-enables the buttons.
- The Move Project gate can no longer create actuals by clicking 50 %; it opens
  the Project-saved actuals fixture, checks the prompt text/buttons and asserts
  answering Yes queues no edit (app_qt 23/23).

W27-0.2 detail:

- The leaf/summary rule is now in the edit model.  `app::make_percent_edit(plan,
  uid, percent, out, error)` refuses uid 0 (project summary), an unknown uid (a
  pending insert has no saved row), a value above 100 and any `PlanInput`
  TaskRow whose `summary` is set, naming the reason in `error`.
- `StructuralEdits::push(edit, const PlanInput* = nullptr)` REQUIRES the plan
  for a `set_percent` edit and refuses a plan-less one rather than trusting it;
  the validated edit replaces the caller's.  The other kinds ignore the new
  argument.
- The new app test covers every refusal, a hand-built summary edit pushed with
  the plan, a plan-less push, leaf acceptance and the 50 % display value; the
  two real-save pushes pass the loaded plan.

W27-0.3 detail:

- The parent pair `tests/data/wave27-priority-control.mpp` /
  `wave27-priority-750.mpp` is ONE Project 2021 session's saves of synthetic-base
  at Priority 500 and after `ProjectSummaryTask.Priority = 750`, with neutral
  metadata.  The pair's Task FixedData differs in exactly row 0's +78/+79 word
  (f4 01 -> ee 02, 500 -> 750); every other stream difference is the per-save
  bookkeeping the wave-24 same-session pair also shows (114/Props GUID tail,
  the Fixed2Data GUIDs, the edit list, cached view state).  Project's stored
  edit is therefore the one +78 word, which is what `set_priority` writes, so
  the save is KEPT, not disabled.
- `app/project_dialogs_tests.cpp` now saves Priority 750 FROM the control
  fixture through the shared structural cascade and asserts, through
  `mpp::CompoundFile` rather than the port reader, that the saved
  `114/TBkndTask/FixedData` stream is byte-identical to Project's 750 fixture.
  It also pins the pair itself (only +78/+79, 0x01F4 -> 0x02EE) and keeps the
  round-trip checks.  This is the field-by-field fixture comparison standing
  rule 1 requires.

Gates on the group's final commit `5a991e9`, fresh trees configured from scratch
and built serially under `build/w27-fresh{,-app,-qt}` (log
`build/w27/g0-fresh-gates.log`):

- core configure/build exit 0 with `CORE_WARNINGS_AS_ERRORS=ON`; core ctest
  `-E plan_recalc` 51/51
- `plan_recalc` 512 checks with corpus / 266 with `MPP_NO_CORPUS=1`
- `engine_diff.py --selftest` 53 checks, 0 failures; `--suite
  --expect-fail constraint-mso` 7 pass + the one expected failure, 0 unrecorded
- reader 1100 checks with corpus / 420 without; M2 `recalc --compare` 332/332
  (268 leaf + 64 summary fields)
- app ctest 17/17; app_qt ctest 23/23
- `check_fixtures.py` OK (28 documented hashes, 24 tracked `.mpp`), oracle
  provenance OK (8)
- client-name scan 0 hits on HEAD; whitespace-normalised `guard_names.py` 0 on
  `c632c8f..HEAD`; client-string hex grep no matches

## Found, not fixed

- `app/edit_model.cpp` is wholly CRLF in the index (1,093 lines) while
  `app/edit_model.h` and the surrounding app sources are LF; the wave-26 review's
  W27-3.6 names two other wholly-CRLF files and does not name this one.
  Converting it under W27-0.2 would have put a whole-file EOL diff under a
  two-function finding, so it is left for the G3 EOL item.  Everything G0 added
  or created elsewhere is LF.
- Project changes per-save GUIDs and edit-list/view-state bytes on EVERY save
  (the wave-24 same-session pair proves it), so no future save test can compare
  two whole files byte-for-byte even when both were produced by Project.  The
  comparison has to be the semantic stream that carries the edited field, as
  W27-0.3 does for Task FixedData.

WAVE-27-G0-COMPLETE

## G1 - CI green on all three OSs

All three items are DONE; nothing in G1 is deferred.

| item | finding | status | evidence |
|---|---|---|---|
| W27-1.1 | `widest_line` unused: an error under GCC/Clang, so the Linux and macOS Qt builds stop | **DONE** | commit `5cd1c61`; gate log `build/w27/gates-8e7895f-W27-1.1.log` |
| W27-1.2 | oracle-provenance sub-check needs history; CI checks out one commit | **DONE** | commit `2f80dd1`; gate log `build/w27/gates-5cd1c61-W27-1.2.log` |
| W27-1.3 | push, dispatch both workflows, fix the first macOS/Linux failures | **DONE** | commits `61772a5`, `231a422`, `9b69e9c`, `07f67b6`, `1d0a92a`; final green runs `40056060660` (qt-shell, three OSs), `40056462711` (ci, two OSs), `40056064713` (app-macos) |

W27-1.1 detail:

- `app_qt/ribbon_bar.cpp`'s anonymous-namespace `widest_line()` had no
  caller.  It and the two includes only it used (`<QFontMetrics>`, `<algorithm>`)
  are removed; `<QFont>` stays for `ribbon_font()`.  No behaviour change.
- MSVC passed it; GCC and Clang fail `-Wunused-function` under `-Werror`.
  The first wave-27 qt-shell run (`40053878076`) reached the Linux and macOS
  builds past this point, and the final green run `40056060660` shows both
  compilers accept the tree.

W27-1.2 detail:

- Both checkouts now use `actions/checkout@v4` with `fetch-depth: 0` (ci.yml
  keeps its `ref:` expression for the workflow_run case beside it).
  `scripts/check_fixtures.py`'s provenance walk sees full history, so the
  last-measured-body check verifies on a runner rather than failing every
  oracle on a one-commit clone.
- The fixtures step passed on windows-msvc and ubuntu-gcc in ci run
  `40056462711`, and in all three qt-shell jobs of `40056060660`.

W27-1.3 detail:

The brief names `app/main.mm` ("not compiled since wave 24"), which only the
`app-macos` workflow compiles, so that workflow was dispatched alongside the
two the standing rules name.  It found two real breaks.  Dispatch series
(all on branch `wave27`; every red was fixed before the next dispatch):

| tip | qt-shell | ci | app-macos |
|---|---|---|---|
| `2f80dd1` | `40053878076` red: linux-qt backstage ink, macos-qt build (`kRailBackground`, `kSeparator`) | `40053880511` **green** | `40053935957` red: `main.mm:1524` undeclared `refresh_menu_items` |
| `61772a5` | - | - | `40054198682` red: declaration put in the anonymous namespace, recursive call ambiguous |
| `9b69e9c` | `40054734014` red: macos-qt About check (`activeModalWidget` null) | `40054736287` **green** | `40054738405` **green** |
| `07f67b6` | `40055403392` red: macos-qt About title | `40055405582` **green** | `40055407782` **green** |
| `1d0a92a` | `40056060660` **green** (linux-qt, windows-qt, macos-qt) | `40056062664` red: windows-msvc `cal` SEGFAULT; re-dispatch `40056462711` **green** | `40056064713` **green** |

- `61772a5` + `231a422` (main.mm): W26-6.1's recursive
  `refresh_menu_items()` is defined at global scope near the bottom of the
  file and called from `refresh_menu_bar()` inside the first anonymous
  namespace.  `231a422` puts the forward declaration at global scope, before
  the namespace, matching the definition; the wrong-scope first attempt is
  kept in history as the evidence for the ambiguity error.
- `9b69e9c` (qt): removed the two unused colour constants Clang rejected
  (GCC had accepted them); and replaced the two "HSL-darkest pixel equals the
  ink" rail checks with "the row contains the measured ink within the
  12-per-channel tolerance the selected-green check already uses" (Linux
  FreeType can make an antialiasing fringe darker than the glyph core).  The
  selected-green and disabled-ink assertions are unchanged in strength:
  #242424 is still separated from black and from #999999.
- `07f67b6` + `1d0a92a` (qt): the About box is found by walking
  `QApplication::topLevelWidgets()` (the repo's dialog idiom) instead of
  `activeModalWidget()`, which returned null on macOS; and it is identified
  by its text (product name + measured version), because macOS does not
  carry the QMessageBox window title the check had pinned.  The dialog is
  closed so `exec()` returns.
- ci `40056062664`'s windows-msvc `cal` SEGFAULT was triaged as a runner
  flake, not a code defect: the core sources at `1d0a92a` are identical to
  the green ci run `40055405582` at `07f67b6` (the only change between them
  is `app_qt/tests/backstage_gate_test.cpp`, outside the root project), and
  `cal` passed repeated local MSVC runs (the repeat count was not logged).
  Re-dispatch `40056462711` was green on both OSs.

Group gates on the final commit `1d0a92a`, fresh trees configured from
scratch under `build/w27-fresh{,-app,-qt}` and built serially (log
`build/w27/g1-fresh-gates.log`):

- core configure/build exit 0 with `CORE_WARNINGS_AS_ERRORS=ON`; core ctest
  `-E plan_recalc` 51/51
- `plan_recalc` 512 checks with corpus / 266 with `MPP_NO_CORPUS=1`
- engine `--selftest` 53 checks, 0 failures; `--suite --expect-fail
  constraint-mso` reached the expected failing set, exit 0
- reader 1100 checks with corpus / 420 without; M2 `recalc --compare`
  332/332 (268 leaf + 64 summary fields)
- app ctest 17/17; app_qt ctest 23/23
- fixtures OK (28 documented hashes, 24 tracked `.mpp`), oracle provenance
  OK (8)
- client-name scan 0 hits on HEAD; whitespace-normalised `guard_names.py` 0
  on `c632c8f..HEAD`; client-string hex grep no matches

Per-commit gate logs: `build/w27/gates-8e7895f-W27-1.1.log`,
`gates-5cd1c61-W27-1.2.log`, `gates-2f80dd1-W27-1.3.log`,
`gates-61772a5-W27-1.3.log`, `gates-231a422-W27-1.3.log`,
`gates-9b69e9c-W27-1.3.log`, `gates-07f67b6-W27-1.3.log` (all under
`build/w27/`).  CI watch logs are beside them as `ci-watch-*`.

## Found, not fixed (G1)

- macOS does not surface the `QMessageBox::about` window title the Windows
  run carries; the gate now asserts the box's text and prints the title as
  a note.  This is a platform difference in Qt, not a port defect.
- ci run `40056062664`'s `cal` SEGFAULT happened once on an unchanged core
  tree and did not reproduce in repeated local MSVC runs (the count was not
  logged); the re-dispatch `40056462711` was green.  If it recurs, treat it
  as a runner-memory symptom and add the `cal` binary to the triage, not a
  scheduling change.

WAVE-27-G1-COMPLETE

## G2 - commit the decoding evidence

W27-2.1 is DONE; nothing in G2 is deferred.

| item | finding | status | evidence |
|---|---|---|---|
| W27-2.1 | The one-change-per-save Project files behind the wave-26 text-style, bar-style and timescale decoders live only under `build/w26/` | **DONE** | fixtures `7a6ec96`, tests `72bf912`, docs `3049471`; gates `build/w27/gates-97e0b81-W27-2.1.log`, `gates-7a6ec96-W27-2.1.log`, `gates-72bf912-W27-2.1-readme.log`, `build/w27/g2-fresh-gates.log` |

W27-2.1 detail:

- Took the brief's lighter option: commit the extracted records, not the
  239,104-byte `.mpp` saves.  `tests/data/wave26-gantt-saves/` holds 16 files:
  14 `*.style.bin` (41,363 bytes each) and 2 `*.fonts.bin` (8,710 bytes each).
  Each style blob is the applied view's `0x22400008` record from one Project
  2021 save (the applied view is main-block index 1 of `214/CV_iew`, which is
  the block the COM calls edited; index 0 decodes but shows no diff).  Each
  font blob is that save's `214/Props` `0x03400000` FONT_BASES payload.
- `control.style.bin` is the same session's unedited save.  Its FONT_BASES
  payload is byte-identical to the tracked `synthetic-gantt-fonts.bin`, so it is
  not duplicated; every non-font save's payload is byte-identical too, so only
  the two real font-table changes are committed.  Duplicate evidence was
  committed once: `control-a` = `control-b`, `t3-milestone-italic` = `-int`,
  `b2-task-red-int` = `-name`, `ts-ymd` = `ts-2tier`; the Arial and size-8 saves
  share a style record and differ only in the font table.  Failed and no-op COM
  calls (`b2-task-red-explicit`, `ts3-toplabels`, the wave-23 timescale
  variants) are not committed; `ts3-count2` has distinct bytes but decodes to
  the same tiers as `ts-yqm`, so it adds no field evidence.
- No metadata needed neutralising: a record is the style payload only.  A
  printable ASCII/UTF-16 scan of all 16 blobs finds only Project's stock bar
  style names and font families -- no path, author, project title or task text.
  The source `.mpp` SHA-256s are in `tests/data/README.md`; the `.mpp` files
  stay local under the git-ignored `build/w26/g3/`.
- 13 tests, one per save (198 checks): `gantt-save-font-family`, `-font-size`,
  `-critical-colour`, `-milestone-italic`, `-milestone-bold`,
  `-milestone-underline`, `-highlight-fill`, `-task-bar`, `-summary-bar`,
  `-milestone-bar`, `-timescale-yqm`, `-timescale-ymd`, `-timescale-yhq`.  Each
  loads the control record and the save through the public decoder, builds the
  full decoded field-difference list (`gantt_differences` compares every field
  the decoder exposes, including the has-colour flags, bar ids, tier counts and
  view flags), asserts the list is exactly the recorded field(s), then asserts
  the changed field's decoded value.
- The decoded changes: Arial (point size 0xFFFF) on all 17 text styles; Calibri
  8 on all 17 plus the bold/foreground resets the explicit-argument call made;
  critical foreground (0,0,255); external italic/bold/underline on with the grey
  foreground becoming automatic; noncritical background (255,255,0); Task
  middle red with start/end becoming automatic and id 40; Summary's three
  colours green and id 40; Milestone start blue with middle/end becoming
  automatic and id 40; middle/bottom tiers weeks/13 -> quarters/2 and days/20 ->
  months/10 (`ymd`: months/9 only; `yhq`: half_years/123 and quarters/5).  The
  raw records also differ in per-save tail bytes and in undecoded regions; the
  tests assert the decoded projection, and every decoded field except the
  recorded change is equal.
- Wave-26 errata confirmed by the files and written into the README: Project's
  `TextStyles` `Item` index is not the slot index (Item 3 edited the external
  slot, Item 7 the noncritical slot), and a family-only save stores 0xFFFF as
  the point size.

Group gates on the final code commit `72bf912`, fresh trees configured from
scratch under `build/w27-fresh{,-app,-qt}` and built serially (log
`build/w27/g2-fresh-gates.log`):

- core configure/build exit 0 with `CORE_WARNINGS_AS_ERRORS=ON`; core ctest
  `-E plan_recalc` 51/51
- `plan_recalc` 512 checks with corpus / 266 with `MPP_NO_CORPUS=1`
- engine `--selftest` 53 checks, 0 failures; `--suite --expect-fail
  constraint-mso` 7 pass + the one expected failure, 0 unrecorded
- reader 1100 checks with corpus / 420 without; M2 `recalc --compare` 332/332
  (268 leaf + 64 summary fields)
- app ctest 17/17; app_qt ctest 23/23
- `check_fixtures.py` OK (44 documented hashes, 24 tracked `.mpp`), oracle
  provenance OK (8)
- client-name scan 0 hits on HEAD; whitespace-normalised `guard_names.py` 0
  on `c632c8f..HEAD`; client-string hex grep no matches
- The log's privacy line records the hex grep's exit 1 (no matches,
  expected); the script itself ends normally.  Clearing `build/w27-fresh-app`
  also hit a transient
  directory-handle lock; CMake then re-ran compiler detection and every source
  compiled, so that tree was still configured and built from scratch (the core
  and app_qt trees were cleared normally).

Per-commit gate logs: `build/w27/gates-97e0b81-W27-2.1.log` (fixtures),
`gates-7a6ec96-W27-2.1-tests-build.log` and `gates-7a6ec96-W27-2.1.log`
(tests), `gates-72bf912-W27-2.1-readme.log` (docs).

## Found, not fixed (G2)

- **Medium**: a Project save that changes only the Gantt text font family stores
  point size 0xFFFF (65,535) in the target FONT_BASES slot (the `arial` save
  proves it; the `size8` save stores 8 when a size is given).  The decoder
  reports the stored value faithfully, but `app_qt/gantt_canvas.cpp:719` and
  `app_qt/sheet_table_model.cpp:261` pass any `point_size > 0` straight to
  `QFont::setPointSize`, so reopening such a file in the port would request a
  65,535-point font for the timescale header and the sheet rows.  A sanity
  bound in the consumers (or an "unset size" flag from the decoder) belongs to
  the rendering finding, not to W27-2.1's fixtures and tests.

WAVE-27-G2-COMPLETE

## G3 - the cheap queued review items

All six items are DONE; nothing in G3 is deferred.

| item | finding | status | evidence |
|---|---|---|---|
| W27-3.1 | A % edit on a pending inserted task makes every save fail (wave-26 review item 9) | **DONE** | commit `1ad385d`; gates `build/w27/gates-36cafbf-W27-3.1.log` |
| W27-3.2 | Deleting a task discards its pending edits and undo does not restore them | **DONE** | commit `032cee2`; gates `build/w27/gates-1ad385d-W27-3.2.log` |
| W27-3.3 | Unlink: measure Project's Unlink Tasks on a task with both a predecessor and a successor (COM `UnlinkTasks`, count `TaskDependencies`) and match it | **DONE** | fixtures `8cad902`, code `8fe6c18`; gates `build/w27/gates-8cad902-W27-3.3.log` |
| W27-3.4 | The new save tests in `app/app_edit_tests.cpp` pass when the load fails; make a failed load fail the test | **DONE** | commit `c7679bc`; gates `build/w27/gates-8fe6c18-W27-3.4-clean.log` |
| W27-3.5 | `app_qt/tests/task_order_test.cpp` pins 13 unsalted FNV-1a hashes of PROJECT_A display names, including the project title (invertible); replace with ID-and-structure checks against the corpus at test time | **DONE** | commit `c7283d7`; gates `build/w27/gates-c7679bc-W27-3.5.log` |
| W27-3.6 | Two files are wholly CRLF against `docs/build.md`'s LF rule (`app_qt/project_dialogs_qt.cpp`, `app_qt/tests/project_dialog_gate_test.cpp`) | **DONE** | commit `1bf7b99` (also `app/edit_model.cpp`, left by G0); gates `build/w27/gates-c7283d7-W27-3.6.log` |

W27-3.1 detail:

- No UI path can queue the edit.  The five % buttons were already disabled
  (W27-0.1), and `StructuralEdits::push` refuses a `set_percent` whose uid has
  no live, non-summary file row through `make_percent_edit` (W27-0.2).  The new
  model check builds a hand-made percent edit for a uid the plan lacks and
  asserts the refusal.
- The save layer no longer trusts the queue.  `save_structural_edits` looks a
  percent edit's uid up with a non-throwing table scan instead of
  `TaskTable::row_for_uid`; an absent uid refuses that ONE edit with a warning
  naming the uid, and the rest of the cascade and the target write still
  happen.  `row_for_uid` stays throwing for its other callers, which treat an
  absent uid as a bug.
- The real-save test appends uid 999 to the display plan only, queues 25 %, and
  saves the same queue twice: both saves return ok, zero percent rows are
  written, the warning names 999 and the target is non-empty -- so a stale or
  hand-built queue cannot poison every later Save.  app_edit 148 checks at this
  commit.

W27-3.2 detail:

- `StructuralEdit::displaced` holds the field, clear-field, percent and
  link-add edits a delete removes from the applied list.  They still must not
  be written before the delete (the delete cascade would undo them), but they
  are no longer destroyed: `undo()` restores them in their original order and
  `redo()` detaches them again into the delete so a second undo restores them.
  One helper, `detach_edits_naming`, implements the removal for push and redo.
- The model test queues 40 %, a custom text field and a 3 -> 1 link add, deletes
  the task and asserts three displaced edits; it then checks the display plan
  (task hidden), undo (all three applied again, percent and link visible), redo
  (all three displaced again) and undo after redo (restored again).
- The real-save test does the review's falsifying sequence: 25 %, delete the
  task, undo, Save.  The saved file re-reads with the task at 25 % and one
  percent row written.  app_edit 177 checks at this commit.

W27-3.3 detail:

- Measurement.  A neutral copy of synthetic `wave23-outline.mpp` was opened in
  Project 2021 and saved as the same-session control; uid 4 -- the one task
  with both a predecessor (3 -> 4 FS+2 d) and a successor (4 -> 6 FF) -- was
  selected and Task > Unlink Tasks was run.  Through COM, `TaskDependencies`
  went 3 -> 1: both links touching uid 4 were removed, the unrelated
  3 -> 5 SS+1 d link survived, and the count was still 1 after reopen.  The
  selected task's own collection went 2 -> 0.  The oracle is
  `scripts/oracle_w27_unlink.py`; the pair is
  `tests/data/wave27-unlink-control.mpp` / `wave27-unlink.mpp` (synthetic,
  neutral metadata; SHA-256s in `tests/data/README.md` and
  `scripts/check_fixtures.py`).
- On disk the link `FixedData` stream is byte-identical in the base, control
  and unlinked saves; `FixedMeta` tombstones entries 0 and 2 (state byte
  +0 00 -> 02) and sets the header flags word 4 -> 6.  `Fixed2Data` carries
  per-save GUIDs (the control already differs from the base and the unlinked
  save differs from both); `Fixed2Meta` is byte-identical in all three.
  Project's own save also moves task-table bytes (`TBkndTask/FixedData` 11
  bytes, `FixedMeta` 3) because its Unlink reschedules, while the port's
  no-CPM save leaves them unchanged; the test therefore compares the three
  link-table streams only.  (Corrected in the CLOSE pass: this paragraph
  previously said `Fixed2Meta` differed per save; a direct CFB comparison of
  the three fixtures shows it does not.)
- Port change.  `app::saved_links_touching(plan, edits, uid)` returns every
  saved link naming the task at either end, skipping a link a pending
  `delete_link` already removes; a pending add has no saved row (uid 0) and is
  not returned.  `make_link_delete_edit` addresses the link by `link_uid`,
  carries its predecessor and names its successor as the edit's uid whichever
  end was selected.  `MainWindow::unlink_selected_task` and the ribbon gate use
  both; the gate selects a task with links at both ends and requires both to go
  while the untouched link survives (links 3 -> 1 for selected uid 4).
- The app link test saves the same two deletions from `wave23-outline.mpp` and
  compares the link table with Project's own save: `FixedData`, `FixedMeta` and
  `Fixed2Meta` are byte-identical to `wave27-unlink.mpp`.  `Fixed2Data` is
  excluded because Project rewrites the link GUIDs on every save.  app_edits_links
  200 checks.

W27-3.4 detail:

- A failed load already counted one failed check through `load()`, but the
  wave-26 blocks asserted only inside `if (load.ok)`, so a failure skipped the
  body without adding a second check; a successful load with an empty plan
  skipped it with no failure at all.  Every later `Loaded` the real save test
  uses has its own named check before the dependent body: `after_add`,
  `after_delete`, `combined_base` and its non-empty task list, the combined
  re-read, `percent_base` and its non-empty task list, `percent_reread` and
  `own_loaded`.  The first load, `base`, keeps its earlier
  `if (!base.ok || base.plan.tasks.empty()) return;`: a failed load still
  counts through `load()`, but an ok-but-empty plan returns without a named
  check (a one-line check is queued as a Low).  Every other failed or empty
  load is now a reported failure.
- app_edit 186 checks (was 177; the nine new load checks).  The first W27-3.4
  gate run (`gates-8fe6c18-W27-3.4.log`) also built an uncommitted W27-3.5 draft
  and failed `qt_task_order_corpus` on that draft's wrong outline numbers; the
  stashed rerun with only W27-3.4 in the tree (`-clean.log`) is the log of
  record.

W27-3.5 detail:

- The task-order test's `fnv1a64` helper and all 13 unsalted hashes are
  deleted.  (The unrelated `fnv1a64` helpers in `core/viewstate/report_main.cpp`
  and the fuzz harness hash bytes, not names, and remain.)  The corpus case now
  asserts, all read from `project_a.mpp` at test time, the ID order 0..12, each
  row's indent level, its summary flag and Project's `OutlineNumber`; no
  display name is read or stored.
- The structural values also record what the hashes concealed: ID order is not
  outline order in this file (ID 1's top-level task is outline 2, ID 12's is
  outline 1), ID 12 is a summary whose children lie beyond the 13 captured
  rows, and row 0 is the project row (level -1, empty outline, summary).
- `qt_task_order` (77 checks) and `qt_task_order_corpus` (217 checks) pass
  with the corpus present (direct-run counts; the CLOSE re-run is in
  `build/w27/direct-counters.log`).

W27-3.6 detail:

- The two files the brief names and `app/edit_model.cpp` (left wholly CRLF by
  the wave-26 G0 note) are now LF: 1,052, 793 and 1,148 lines respectively.
  The conversion changed no content -- `git diff --ignore-cr-at-eol` against
  the parent is empty -- and each file now reports CRLF 0 / bareCR 0.

Group gates on the final code commit `1bf7b99`, fresh trees configured from
scratch under `build/w27-fresh{,-app,-qt}` and built serially (log
`build/w27/g3-fresh-gates.log`):

- core configure/build exit 0 with `CORE_WARNINGS_AS_ERRORS=ON`; core ctest
  `-E plan_recalc` 51/51
- `plan_recalc` 512 checks with corpus / 266 with `MPP_NO_CORPUS=1`
- engine `--selftest` 53 checks, 0 failures; `--suite --expect-fail
  constraint-mso` 8 scenarios, 8 recorded (7 pass + the one expected failure),
  0 unrecorded
- reader 1100 checks with corpus / 420 without; M2 `recalc --compare` 332/332
  (268 leaf + 64 summary fields)
- app ctest 17/17; app_qt ctest 23/23 (one transient MSB6003 file lock was
  retried inside the script; both trees' builds exited 0)
- `check_fixtures.py` OK (46 documented hashes, 26 tracked `.mpp`), oracle
  provenance OK (8)
- client-name scan 0 hits on HEAD; whitespace-normalised `guard_names.py` 0
  on `c632c8f..HEAD`; client-string hex grep no matches
- The log's hex-grep line records exit 1 (no matches, expected); the script
  itself ends normally.

Per-commit gate logs (each: core 51/51, app 17/17, app_qt 23/23, fixtures OK,
HEAD and working-tree name scans 0, `guard_names` 0, hex grep none):
`build/w27/gates-36cafbf-W27-3.1.log`,
`gates-1ad385d-W27-3.2.log`,
`gates-8cad902-W27-3.3.log` (fixtures committed, code in the tree),
`gates-8fe6c18-W27-3.4-clean.log`,
`gates-c7679bc-W27-3.5.log`,
`gates-c7283d7-W27-3.6.log`.

## Found, not fixed (G3)

- **Medium**: Unlink acts on the one selected task.  Project's Task > Unlink
  Tasks acts on a multi-row selection; `MainWindow::unlink_selected_task` reads
  only `selected.uid`.  The W27-3.3 measurement and save comparison cover the
  one task with links at both ends, so a multi-row Unlink in the port leaves the
  other selected tasks' links in place.
- **Low**: a pending link ADD naming the unlinked task survives Unlink.  It has
  no saved row to tombstone, and its removal path today is undo; Project would
  have the link in memory and remove it before the next save.
- **Privacy**: the 13 unsalted FNV-1a display-name hashes removed by W27-3.5
  remain in git history (added in `912b3fa`, removed in `c7283d7`).  The
  standing rules forbid rewriting commits, so they are still recoverable from
  `git log`; if the repository is published, CLOSE/Tim must decide between
  accepting that history and the rewrite the rules currently rule out.
- **Repo hygiene**: the W27-3.6 audit (`build/w27/tools/eol_scan.py`, read-only)
  still reports 173 tracked text files containing CRLF lines -- mostly
  grandfathered analysis output (`analysis/*.tsv`, `docs/analysis/oracle/*.json`),
  but also `app/field_link_edits.cpp`, `app/app_edits_links_tests.cpp`,
  `tests/io_error_paths.cpp`, `scripts/uifile_rcdata.py`, `memory/app.md` and a
  one-line `.github/workflows/app-macos.yml`.  The three files G3 owned are
  clean; normalising the rest is a mechanical repo-wide change that would bury
  this wave's diff and belongs to a dedicated cleanup.

WAVE-27-G3-COMPLETE

## CLOSE - the wave record

The wave ran as four groups plus this close, one fresh session each.  Every
item in G0..G3 is DONE; nothing is deferred.  The group marker lines end each
section above.  The CLOSE work is the fresh-tree gate run, the privacy scans,
the independent review pass over `git diff wave26..wave27` (two read-only
reviewers), the push and the CI dispatch, the START-HERE / INDEX / memory update
and the re-scored 16 decisions.

### Fresh-tree gates (final code commit `9323db2`)

Three trees were configured FROM SCRATCH on 2026-10-24 and built serially:
`build/w27-fresh` (core, `-DCORE_WARNINGS_AS_ERRORS=ON`),
`build/w27-fresh-app` (headless app tests) and `build/w27-fresh-qt` (Qt shell
and its 23 tests).  The numbers and the engine scenario suite are in
**`build/w27/close-fresh-gates.log`**; the engine harness is
`scripts/engine_diff.py --suite --expect-fail constraint-mso`.

One qualification, stated because the gate must be from scratch: the first
pass's cleanup could not delete `build/w27-fresh` (a transient file handle from
an earlier recursive search held a subdirectory), so CMake configured over the
old cache and that section is not from-scratch evidence.  The tree was then
deleted for real and **every core stage was re-run from scratch** in the
`CORE RE-RUN` section at the end of the same log (identical numbers); that
section is the core gate of record.  The app and app_qt trees were cleared
normally on the first pass.

| gate | result |
|---|---|
| core ctest `-E plan_recalc` | **51/51** |
| `plan_recalc` with corpus | **512 checks**, 0 failures |
| `plan_recalc` `MPP_NO_CORPUS=1` | **266 checks**, 0 failures |
| engine scenario suite | 8 recorded, **7 pass / 1 fail**, 0 unrecorded; expected failing set `constraint-mso` reached; exit 0 |
| engine `--selftest` | **53 checks, 0 failures** |
| reader with corpus | **1100 checks** |
| reader `MPP_NO_CORPUS=1` | **420 checks** |
| M2 `recalc --compare` | **332/332** (268/268 leaf, 64/64 summary) |
| app ctest | **17/17** |
| app_qt ctest | **23/23** |
| `check_fixtures.py` / oracle provenance | **OK** (46 documented hashes, 26 tracked `.mpp`); provenance OK (8) |
| parity wave23-outline | **10 comparable, 0 failing** |
| parity wave25-auto `--only colour.task_bar` | **1 comparable, 0 failing** |
| client-name line scan | HEAD at `9323db2` **0 files / 0 hits**; commit messages on `wave26..9323db2` **0** (`build/w27/close-privacy-head-messages.log`); the normalised pass prints **no labels and exits 0** on `c632c8f..9323db2` and on the whole `main..9323db2` ancestry, 137 commits (`close-privacy-normalised.log`, `close-privacy-ancestry.log`) |
| `client-string-<8 hex>` grep | **none** |
| pixel score | not run: no PROJECT_A reference PNG locally (unchanged from wave 26) |

The log's line `wave26..HEAD: 0 file(s)` is **not** evidence: `scan_client_names.py`
treats every argv as a revision and `git grep` against the range fails silently;
the separate `--messages wave26..9323db2` run is the evidence of record.  The
numbers belong to `9323db2`; the CLOSE docs commit that follows is docs-only.

One citation note: the per-test check counts quoted in G0..G3 are direct-run
figures (ctest prints pass/fail only).  They were re-corroborated at `9323db2`
in `build/w27/direct-counters.log` (app_edit 186, app_edits_links 200,
qt_task_order 77, qt_task_order_corpus 217, the whole viewstate suite 1,042).

### Privacy (brief section 0)

The publication hold was LIFTED (history scrubbed 24 Oct 2026); `wave27` is
pushed and the pre-push guard (which fails closed) passed.  Both scans ran:

- line-based `scan_client_names.py HEAD --messages wave26..HEAD` at `9323db2`:
  156 corpus names; **0 files / 0 hits** on HEAD and **0 commits** whose
  message quotes a name (`build/w27/close-privacy-head-messages.log`);
- whitespace-normalised `guard_names.py`: **no labels and exit 0** on
  `c632c8f..9323db2` (`build/w27/close-privacy-normalised.log`) and on
  `fcb1c10..9323db2` (`main..wave27`, 137 commits;
  `close-privacy-ancestry.log`), so the scrubbed history reachable from this
  branch adds no name (the script prints labels only on a refusal, so the log
  now records its range and exit code);
- `git grep -nE "client-string-[0-9a-f]{8}"`: no matches.

A raw line scan of `main`'s tree reports 12 hits in 8 older files; the three
labels are all in `guard_names`'s `GENERIC` set (generic section words that also
occur as task names), so they identify nobody and the normalised guard ignores
them.
HEAD itself is 0.  The pre-scrub commits are no longer reachable (the old
objects are pruned), so this wave has no positive control; the wave-26 positive
control on the pre-scrub `wave23` tree (14 files) remains the scanners'
detection evidence.

One local-log caveat, found in this close: the corpus-backed reader and M2
runs print a PROJECT_A task name from a row the reader skips (the orphaned
Var2Data block of a deleted row) into `build/w27/close-fresh-gates.log`.  The
156-name list is built from live `reader_dump` rows, so it does not contain
that deleted row's name, and neither scan would catch a committed copy.  No
name is quoted here or in any tracked file; `build/` is neither tracked nor
Dropbox-synced, and every committed tree and message (and the whole
`main..wave27` ancestry) is clean.  The warning should print the uid and row
only, and the scanner should include skipped-row names, in wave 28 (listed in
Found, not fixed).  The same warning is in the wave-26 log, so this is a
coverage gap in the tooling, not a new exposure.

Residuals: the first is in history, which the brief and the standing rules
forbid rewriting; the second is a committed oracle the reader still reads:

- the **13 unsalted FNV-1a display-name hashes** wave 26 added in `912b3fa`
  and wave 27 removed in `c7283d7` remain in the wave-26 commits reachable from
  `wave27`; they are hashes, not names, but a guessed name inverts them;
- `tests/data/project_a_golden.tsv` still carries unsalted 16-hex name hashes
  (the reader's oracle), unchanged since wave 25;
- the wave-23/24 plaintext names are gone from every reachable tree and message
  (the scrub); no name is added by any commit in `main..wave27`.

### Independent review of `wave26..wave27`

Two independent reviewers with fresh contexts (neither an author) read the
wave's diff, the record and the gate/privacy evidence on 2026-10-24; both were
read-only and quoted no client data.  One reviewed the code diff; the other
audited the record against the logs and re-measured the fixtures.

**No Critical and no confirmed High finding.**  The record auditor raised one
High -- that the W27-0.1 percent-actuals offsets were false -- and a
byte-level re-measurement disproved it (R1): its tool prints only ten
differing offsets per row.  The code review found one Medium and five Low; the
record audit found four Medium and five Low.  Every item and disposition is
below; the record corrections are in this commit.

**Code review**

| # | sev | finding | disposition |
|---|---|---|---|
| 1 | Medium | `make_percent_edit` treats "pending insert" as a uid absent from the plan it is handed; on the display plan a pending insert is a real row (uid != 0, `file_row` 0, summary false), so a wave-28 caller passing `doc_.plan()` would be accepted.  The save layer still refuses the row with a warning, so no data loss, and no UI caller exists today (the % buttons are disabled). | **LISTED** for wave 28: reject `file_row == 0` explicitly and add a display-plan pending-insert test before the buttons are re-enabled. |
| 2 | Low | The Unlink save test's comment says comparing with the unlinked save compares "Unlink's bytes only"; Project's Unlink also reschedules, so its save moves `TBkndTask` bytes (`FixedData` 11, `FixedMeta` 3) that the port's no-CPM save leaves unchanged.  The test compares the link table only, so its assertions are correct. | **LISTED**: a comment fix with the no-CPM work; no gate effect. |
| 3 | Low | The G3 note said `Fixed2Meta` differed per save; a direct CFB comparison shows it is byte-identical in the base, control and unlinked saves (only `Fixed2Data` carries per-save GUIDs). | **CORRECTED** in this commit (G3 "W27-3.3 detail"). |
| 4 | Low | Each of the five new `.mpp` fixtures embeds its own absolute save path (UTF-16LE in `214/CEdl/Var2Data` and `Props14`); it is the operator's local path, not a client name, and the pattern pre-exists in `synthetic-calendars.mpp` and `wave23-flags.mpp`. | **LISTED**: these are the oracle evidence and their bytes are pinned; rebuilding them through the neutral path with new hashes is a later fixture task. |
| 5 | Low | The Unlink real-save test (and two older blocks) prints SKIP and returns without a failing check if a fixture is missing, so it can pass without measuring; the fixture-hash check is the only backstop. | **LISTED**: a missing-fixture `check()` belongs to the test-hardening pass; the pattern pre-dates wave 27. |
| 6 | Low | The CLOSE work was not yet on the tree when the reviewer read it (no marker, START-HERE/INDEX/memory unchanged, CI citations stopping at G1). | **Resolved by this commit**: the CLOSE section, the START-HERE / INDEX / memory update and the runs at `9323db2` are all here. |

**Record audit**

| # | sev | finding | disposition |
|---|---|---|---|
| R1 | High (raised) - **not a defect** | The W27-0.1 note's offsets were called false: the auditor's `mpp_diff.py` output stopped at +132 and showed no +136..139 and no +168/+169. | **DISPROVED**: a full byte diff shows uid 0 differs in 17 bytes (+80/+81, +88/+89, +92, +120..123, +132..139) and uid 1 in 18 (the same plus +169: 00 -> 90), with Var2Data key 0x0B40015E -- exactly the record's list.  `scripts/mpp_diff.py` truncates at ten differing offsets per row (`first_diffs(..., limit=10)`) without a marker, which hid them; evidence `build/w27/diff-task-rows-full.log`. |
| R2 | Medium | The W27-3.4 sentence "Every `Loaded` ... has its own named check" omitted `base` at the top of `test_real_save_round_trip`, which returns on an ok-but-empty plan without a named check (a failed load still counts through `load()`). | **CORRECTED** in this commit (W27-3.4 detail); a one-line check for `base` is queued as a Low. |
| R3 | Medium | The per-test check counts in G0..G3 (148/177/186, 200, 198, 217) are not printed by the named ctest gate logs. | **EXPLAINED** in this commit: they are direct-run figures, re-corroborated at `9323db2` in `build/w27/direct-counters.log`; no count withdrawn. |
| R4 | Medium | Five G3 commit messages (`8cad902`, `8fe6c18`, `c7679bc`, `c7283d7`, `1bf7b99`) quote numbers without naming the gate log their rule 3 requires. | **RECORDED** (Found, not fixed): the commits and their green logs stand; rewriting history is forbidden. |
| R5 | Medium | Four fixture commits (`d30c2e8`, `a40ef6d`, `7a6ec96`, `8cad902`) also carry `scripts/check_fixtures.py`, `tests/data/README.md` and the oracle script, though rule 6 says a fixture commit carries fixtures only; two of them say "Fixture only". | **RECORDED** (Found, not fixed): the hash registry and provenance are what make a fixture checkable, so the rule text and the practice need reconciling; no client data and the scans are clean. |
| R6 | Low | The G3 note paired `Fixed2Data`/`Fixed2Meta` as per-save GUID differences. | **CORRECTED** in this commit: `Fixed2Meta` is byte-identical in all three saves; the Project save also moves task-table bytes (11 + 3) that the port's no-CPM save does not; the test compares the link table only. |
| R7 | Low | "`cal` passed 20/20 local repeats" is in no log. | **CORRECTED** in this commit (G1 and its Found-not-fixed): the repeat count was not logged; the green re-dispatch is the evidence. |
| R8 | Low | "The script's exit code is 1": the gate scripts do not exit with the hex grep's code. | **CORRECTED** in this commit (G1 and G3 gate notes): the log records the hex grep's exit 1; the script ends normally. |
| R9 | Low | `app_edits_links_tests.cpp` silently returns on a shape mismatch (two blocks), the same ok-but-skipped shape as `base`. | **LISTED** (Found, not fixed): the W27-3.4 fix covered `app_edit_tests.cpp` only. |
| R10 | Low (tooling) | `mpp_diff.py` prints at most ten differing offsets per row with no truncation marker; it under-read the percent fixture in the G0 log too. | **LISTED** (Found, not fixed): print a marker (or every offset) in the next script change. |

The audit also verified clean: the 27 commits in `wave26..wave27`; every commit
id cited in the record is an ancestor of `wave27`, and every `gates-*.log` is
parent-named; the group gate numbers match the four fresh logs; the marker
lines; the percent refusal, priority measurement, delete/undo, Unlink pair and
task-order tests; the G2 blobs and fixture hashes; and the privacy scans (line
0, guard exit 0, no hex token, no names in the new fixtures or blobs).

One further record sentence was corrected in this pass, not from a reviewer's
list: the W27-3.5 note said "`fnv1a64` ... deleted", but only the task-order
test's helper is gone -- the unrelated `fnv1a64` helpers in
`core/viewstate/report_main.cpp` and the fuzz harness hash bytes, not names,
and remain, so the sentence now says which helper went.

### Push and CI

`wave27` was pushed at the final code commit `9323db2` on 2026-10-24 (the
pre-push guard passed) and the three workflows were dispatched on the branch
with `gh workflow run <file> --ref wave27`.  All of them completed **green on
`9323db2`** on the first dispatch, so no re-dispatch was needed:

| workflow | run | jobs (all success) |
|---|---|---|
| `qt.yml` (qt-shell) | [40064777633](https://github.com/<owner>/msproject-mac/actions/runs/40064777633) | `windows-qt`, `linux-qt`, `macos-qt` |
| `ci.yml` (ci) | [40064779873](https://github.com/<owner>/msproject-mac/actions/runs/40064779873) | `windows-msvc`, `ubuntu-gcc` |
| `app-macos.yml` (app-macos) | [40064782094](https://github.com/<owner>/msproject-mac/actions/runs/40064782094) | `macos-appkit` |

The watcher log is `build/w27/close-ci-watch.log` (each run
`completed success` at `9323db2`).  The Linux and macOS Qt jobs are what the
G1 fix was for: `widest_line` was an error under GCC/Clang, and both now build
the Qt shell and run its test suite, alongside the Windows MSVC job.

### The 16 decisions re-scored (wave-26 close as the starting point)

| # | decision | wave 26 | after wave 27 | why |
|---|---|---|---|---|
| 1 | all seven tabs | DONE | **DONE** | unchanged: every tab's groups are pinned and the live-kind gate reads the real widget classes (Task 8/8, Resource 5/5, Report 3/3, Project 6/6, View 7/7, Help 1/1, Gantt Chart Format 6/6 at 1920); the five % buttons keep their measured layout but are disabled |
| 2 | full backstage | DONE | **DONE** | unchanged: the Project rail and all eleven pages exist; the online entries stay laid out and disabled |
| 3 | original icon set | DONE | **DONE** | unchanged: 114 command controls carry the one-ink glyphs; menu/QAT/status-bar artwork still uses the older drawings (listed) |
| 4 | measured chrome metrics | DONE (stronger) | **DONE (stronger)** | unchanged: the measured rectangles/rows/gallery colours are committed; parity is still 10/10 plus the colour-only 1/0 |
| 5 | timeline strip | DONE | **DONE** | unchanged |
| 6 | real Entry table | DONE | **DONE** | unchanged |
| 7 | render what the file says | PARTIAL (stronger) | **PARTIAL (stronger)** | W27-2.1 committed the Project one-change-per-save evidence and 13 decoder tests for the text/bar/timescale styles; no renderer changed.  Still open: stored sheet column widths (5 vs 8), PROJECT_A's yellow row fill, bar-label coverage, timescale origin/zoom |
| 8 | status bar | PARTIAL | **PARTIAL** | unchanged |
| 9 | disabled commands + backlog | DONE | **DONE (stronger)** | the five %-complete commands are now disabled with the exact decision-9 tooltip and no route can queue the edit; the live ribbon gate and the pin agree.  The generated backlog covers menu commands; the ribbon-only % controls are recorded by the pin and the live gate, not the backlog (listed Low) |
| 10 | light and dark themes | PARTIAL (stronger) | **PARTIAL (stronger)** | unchanged: Dark Gray and Black darken the chrome and the gate matches the measured fills; the sheet/chart header bands and ribbon ink stay light (listed) |
| 11 | new-project defaults | DONE | **DONE** | unchanged; the Options new-task mode still does not reach `insert_task()` (Medium, wave 28 G3) |
| 12 | structural measurement | PARTIAL (stronger) | **PARTIAL (stronger)** | W27-2.1's 13 decoder tests and W27-3.5's structure-only corpus case add structural checks, but no new screen is measured; the pixel score is not re-run (no reference PNG locally) |
| 13 | Carlito bundled | DONE | **DONE** | unchanged |
| 14 | every dialog on every tab | PARTIAL (stronger) | **PARTIAL (stronger)** | unchanged: Text Styles, Gridlines, Bar Styles, Page Setup, Visual Reports and Compare Projects are still disabled backlog entries |
| 15 | dialog semantics | PARTIAL | **PARTIAL (stronger)** | Project Information's Priority save is now measured against Project's own save (W27-0.3), so that functional control is proven, not just wired; the other gaps are unchanged |
| 16 | split view | DONE | **DONE** | unchanged: the Task Form is display-only (listed) |

## Found, not fixed

The reviews' Medium/Low items are listed with their dispositions above; the
not-fixed ones are the first part of this list.  The wave's deliberate limits,
none Critical and no confirmed High (the one High raised was disproved, R1):

* **Stored sheet columns** (decision 7): at 1920 the port shows the five columns
  that fit its splitter; Project shows eight.  The stored widths are a wave-25
  carry.  [Medium - decision 7]
* **PROJECT_A's yellow row**, the **bar-label coverage**, the **dark themes'
  light sheet/chart header bands and fixed ribbon ink**, the **menu-bar / QAT /
  status-bar artwork**, the **display-only Task Form**, the **undecoded stored
  combination view** and the **White theme**: unchanged from the wave-26 CLOSE
  list, each with the same reason.  [Low]
* **Six dialogs are still disabled backlog entries** (decision 14): Text
  Styles, Gridlines, Bar Styles, Page Setup, Visual Reports, Compare Projects.
  [Medium - decision 14 remains PARTIAL]
* **No-writer engines** (actuals/Update Project, Move Project, baselines, WBS
  codes, calendars) are laid out and honestly inert.  [Low]
* **Custom Fields** covers the six measured families; Start/Finish custom fields
  and the Resource/Project scopes are not in the port's field table.  [Low]
* **Timescale origin/zoom** still differs from Project's scroll position.
  [Low]
* **The pixel score is not re-run**: no PROJECT_A reference PNG is local.
  [Low]
* **A point size of 0xFFFF** reaches Qt unguarded in `app_qt/gantt_canvas.cpp`
  and `app_qt/sheet_table_model.cpp` when a file stores a family-only text
  change (the W27-2.1 `arial` evidence); a sanity bound belongs to the
  rendering work.  [Medium - G2]
* **Unlink acts on the one selected task**; Project's Unlink Tasks acts on a
  multi-row selection.  [Medium - W27-3.3]
* **A pending link ADD naming an unlinked task survives Unlink** (it has no
  saved row to tombstone); undo is its removal path today.  [Low - W27-3.3]
* **A percent edit on a pending inserted task is refused only while the uid is
  absent from the plan handed to `make_percent_edit`**; a wave-28 caller
  passing the display plan (where the insert is a real row with `file_row` 0)
  would be accepted, though step 5a still refuses the write with a warning.
  Fix this and add the display-plan test before the % buttons are re-enabled.
  [Medium - review item 1, latent]
* **The Unlink save test's comment** says comparing with the unlinked save is
  "Unlink's bytes only", but Project's Unlink reschedules and so also moves
  `TBkndTask` bytes; the test compares the link table only and its assertions
  are correct.  [Low - review item 2]
* **Three real-save fixture blocks print SKIP and pass** when a fixture is
  missing (the Unlink block is the newest); a missing fixture needs a failing
  check.  [Low - review item 5, pattern pre-dates this wave]
* **The five new `.mpp` fixtures embed the operator's absolute save path**
  (UTF-16LE in `214/CEdl/Var2Data` and `Props14`), as `synthetic-calendars.mpp`
  and `wave23-flags.mpp` already do.  It is not a client name and the name
  guards do not look for paths; the fixtures are pinned oracle bytes, so a
  rebuild through the neutral path (and new hashes) is a later fixture task.
  [Low - review item 4]
* **The first load in `test_real_save_round_trip` (`base`) has no named check**
  before its `return` on an ok-but-empty plan; one line fixes it.
  [Low - record audit R2]
* **Two `app_edits_links_tests.cpp` blocks return silently on a shape
  mismatch** (ok load, wrong task count), the same skippable shape.
  [Low - record audit R9]
* **Five G3 commit messages quote numbers without naming their gate log**
  (`8cad902`, `8fe6c18`, `c7679bc`, `c7283d7`, `1bf7b99`), against standing
  rule 3; the logs exist and are green, and history cannot be rewritten.
  [Medium - record audit R4, process]
* **Four fixture commits also carry the hash registry, the provenance README
  and the oracle script**, and two say "Fixture only"; the rule text and the
  practice need reconciling by Tim.  No client data.
  [Medium - record audit R5, process]
* **`scripts/mpp_diff.py` truncates its per-row diff at ten offsets** with no
  marker; it hid the date words from a reviewer and from the G0 log.
  [Low - record audit R10, tooling]
* **173 tracked text files still contain CRLF lines** (grandfathered analysis
  output and a few sources); the three files W27-3.6 owned are clean, and a
  repo-wide normalisation belongs to a dedicated change.  [Low]
* **The 13 unsalted FNV-1a display-name hashes** the wave removed from the tree
  remain in the wave-26 history reachable from `wave27` (`912b3fa`..`c7283d7`),
  and `tests/data/project_a_golden.tsv` still carries unsalted 16-hex name
  hashes.  Rewriting history is forbidden by the brief; both remain Tim's
  decision.  [Privacy - recorded in the Privacy section]
* **`app_qt/CMakeLists.txt:302` is stale**: the comment still says the corpus
  task-order case records a "name HASH only", which W27-3.5 removed.  It is a
  comment, not a gate, and the CLOSE gate numbers belong to the tree that still
  contains it.  [Low - found in this close]
* **The generated backlog does not list the five %-complete ribbon controls**:
  `docs/ui-command-backlog.md` is generated from the menu/ribbon command model,
  which does not carry those dynamic buttons; `ribbon_pin.tsv` and the live
  ribbon gate carry them instead.  [Low - decision 9's backlog is incomplete
  for five disabled commands]
* **The 156-name list is built from live rows, so a deleted row's name is not
  in it**: a corpus-backed reader warning prints a PROJECT_A task name from an
  orphaned Var2Data block (uid 10856, a row the reader skips) into
  `build/w27/close-fresh-gates.log`, and the same warning is in the wave-26
  log; neither scan would catch a committed copy of such a name.  No tracked
  file quotes it, and `build/` is neither tracked nor Dropbox-synced.  The
  warning should print the uid and row only, and the scanner should include
  skipped-row names.  [Low - tooling/privacy, found in this close]
* **The CLOSE gate script's range scan is not evidence**: `scan_client_names.py`
  takes revisions, not ranges, and its `git grep` against `wave26..HEAD` fails
  silently, printing "0 file(s)"; the separate `--messages` run is used instead.
  The tool's contract should reject a range (or accept one) in a later cleanup.
  [Low - tooling, found in this close]

WAVE-27-COMPLETE
