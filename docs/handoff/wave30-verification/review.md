# Wave 30 independent review (`git diff wave29..wave30`, tip 338feaa)

Read-only review. The only repo file written is this one. Scratch scripts (a
minimal CFB reader, the fixture privacy scan, the drive-path context dump) live
in the session scratchpad. I formed the findings below before reading the
wave's own CLOSE review section of `wave30-complete.md`; the last section says
where we agree.

## Verdict

G0 did real work, and most of it holds. R29-1 (the Critical) is closed: the
edit model now refuses an indent unless the new summary has exactly one physical
assignment row that is Project's 0xFFFF0001 placeholder, and the writer refuses
the tombstone on the raw +8 word. Tests exist and would fail without the checks.
R29-3, R29-4 and R29-7 are closed with tests. R29-5 and R29-6 are narrowed but
still wider than their pairs. R29-2 is only partly closed. The linked-file
refusal can be bypassed: queue the automatic indent first, then add a link in
the Predecessors dialog. The refusal also never covered the other conditions
R29-2 named, such as a completed row or a deadline elsewhere in the file.

Of the new commands, Manually Schedule / Mode is the model to follow. It has a
committed pair compared through `save_pair_noise.h`, a file-side shape check
that pins nearly every fixture condition, and the reverse direction kept
disabled on evidence. Fill Down, however, is a newly enabled `.mpp` write
whose only Project evidence is an uncommitted corpus probe. Every
"measured and deferred" command I checked is disabled with the exact decision-9
tooltip, and I found no write path that reaches it. Inspect, both Details panes,
Scroll to Task and Spelling are read-only.

The 66 new fixtures are clean. Their metadata is "Fixture Builder" / "Fixture
Company" / "Synthetic ... Fixture", and the only embedded paths are the neutral
`C:\w30\` root (132 occurrences, 0 elsewhere). The CLOSE gate numbers and CI
claims check out against `build/w30/`.

**Counts: Critical 0 / High 4 / Medium 4 / Low 6.**

## How R29-1 .. R29-7 stand

| finding | status at 338feaa | evidence |
|---|---|---|
| R29-1 (Critical, indent deletes a real zero-work assignment) | **Closed.** `check_new_summary_shape` refuses physical/live row mismatch, a row count other than 1, any real resource (work ignored), and a raw-0 word. `outline.cpp` refuses to tombstone when FixedData +8 is not 0xFFFF0001. The percent and date paths also refuse a zero-work real assignment (W30-0.6). The outdent counterpart is only refused at save (R30-6). | `app/edit_model.cpp:629-690`; `core/mppwrite/outline.cpp:1499-1521`; tests `app/app_edits_outline_tests.cpp` (real zero-work, no row, physical rows, raw 0, and a writer-side save that keeps resource 7) |
| R29-2 (High, automatic outline on linked files) | **Partly closed.** `make_outline_edit` and `push()` refuse a slack-carrying edit when the plan has a link. Bypass: link adds queued after the outline edit are never re-checked (R30-1). Not addressed: the other recalculated rows' conditions, such as progress, completed rows and deadlines elsewhere (R30-2). | `app/edit_model.cpp:1978-1990, 2135-2144, 2602-2605`; `app/field_link_edits.cpp:405-476`; `app/edit_model.cpp:2353-2380` |
| R29-3 (High, outline moves over progress/work/queued actuals) | **Closed** for the moved subtree, the restored parent and pending actuals, in both queue orders, with tests that name the branch. Only `percent_complete > 0` and work > 0 are seen (R30-13, Low). | `app/edit_model.cpp:758-830`, push at `:2403-2430, 2609-2680` |
| R29-4 (High, 0 % -> 0 % date update) | **Closed.** The model drops the target. An all-no-op update is refused with "nothing to write". `push()` refuses a hand-built 0 -> 0 target. Tested. The no-op is the review's suggested behaviour, not a Project measurement. | `app/edit_model.cpp:1663-1673, 1726-1733, 2461-2466`; `app/app_edits_actuals_tests.cpp:1715-1791` |
| R29-5 (High, summary recompute scope) | **Partly closed.** The writer now writes only the edited rows' ancestor chain. The model refuses a second progressed leaf or a manual summary on that chain, in both the make and push paths. Still open: with progress off the chain, the project row's Resume/Stop and roll-up are written by a rule measured only with no other progress in the file. The wave's own test (e) saves exactly that shape without comparing it with Project (R30-3; the record's C5). | `core/mppwrite/actuals.cpp:1438-1461, 737-835`; `app/app_edits_actuals_tests.cpp:1945-1960` |
| R29-6 (High, resource-loaded contour) | **Mostly closed.** Task duration 9600, units 100 %, assignment work 9600 and no task calendar are pinned in the model, and duration/units in the writer. Not pinned: the project calendar's definition, the resource's calendar and the source contour (R30-7). | `app/edit_model.cpp:345-385, 448-462`; `core/mppwrite/actuals.cpp:1641-1663` |
| R29-7 (High, Add Link 0x07 byte) | **Closed.** `validate_link_add` and `add_task_link` both refuse a successor that already has a live incoming link or has progress. Every remaining add writes 0x0f. Tests in `tests/roundtrip_links.cpp` and `app_edits_links_tests`. The two "measured adds" are still corpus probes, not committed pairs (pre-existing). | `app/field_link_edits.cpp:439-476`; `core/mppwrite/add_delete.cpp:1815-1866` |

## Findings

| id | severity | file:line | finding | evidence | suggested fix |
|---|---|---|---|---|---|
| R30-1 | **High** | `app/field_link_edits.cpp:405-476` (`validate_link_add`); `app/edit_model.cpp:2353-2380` (`push` add_link); `app_qt/dialogs.cpp:600-610`, `app_qt/main_window.cpp:3456-3466` | **The R29-2 refusal can be bypassed by another entry point.** The linked-file check runs only when the automatic indent/outdent is queued. In the other order, the save is unchecked: (1) queue an automatic indent on an unlinked file, which carries a slack list computed with no links; (2) open Task > Predecessors and add a link (a first link into a 0 % successor is accepted). Neither `validate_link_add` nor `push(add_link)` looks at pending outline edits. The save writes the link at step 2 and the unlinked slack and late dates at step 5b, and `write_outline_edits` has no link-table guard. The result is the exact unmeasured shape W30-0.2 meant to refuse. No test covers this order. | Code read: `validate_link_add` checks pending inserts, deletes, duplicates, skipped links, incoming links and successor progress only. `push(add_link)` checks deletes and duplicates. `grep Cons/link core/mppwrite/outline.cpp` finds no guard. `app_edits_links_tests` and `app_edits_outline_tests` have no add-after-outline case. Not run in the app. | Refuse Add Link while a slack-carrying outline edit is queued (in `validate_link_add` and `push`), and add the same check in `save_structural_edits` (refuse when `add_link` and a non-empty `slack` coexist). Add a test for each order. |
| R30-2 | **High** | `app/edit_model.cpp:847-940` (`build_outline_slack`), `:1976-2045`, `:2135-2180` | **The automatic outline save still rewrites slack and late dates on every live automatic row, and nothing checks those rows' conditions.** All three automatic pairs are the three-row synthetic base: every row 0 %, no deadline, no constraint. R29-2 listed completed rows (W28-1.3: slack 0, actual dates as the late pair) and summaries outside the edit as unmeasured. W30-0.2 only added the link check, and W30-0.3 only checks the moved subtree and the restored parent. An unlinked file with a completed or progressed task elsewhere, or a deadline or finish constraint on any row, passes every gate. That row then gets `slack = work_time_between(finish, project finish)` and `late start = project finish - duration`, which is not what Project stores for those shapes. | Code read: `build_outline_slack` takes every `!deleted && !manual` row and has no percent, deadline or constraint input. `tests/data/README.md:48-51` (fixture conditions). No refusal or test for progress outside the moved rows. | Refuse an automatic outline edit when any live row has progress, a deadline, or a non-ASAP constraint, or when there is a summary outside the edit. Alternatively, measure a pair with a completed row elsewhere. Add a test per refusal. |
| R30-3 | **High** (the record's C5, rated Medium there) | `core/mppwrite/actuals.cpp:737-835` (`write_project_rollup`, fallback at `:818-823`); `app/edit_model.cpp:1444-1461` (`make_percent_edit`); `app/app_edits_actuals_tests.cpp:1945-1990` | **The project-row roll-up with progress elsewhere in the file is enabled but not measured.** `check_summary_rollup_scope` looks only at the edited rows' ancestor chain. A percent edit or Mark on Track in a file that already has progress off the chain is accepted. The project row's percent, actual duration and work are then summed over the whole file. Its Resume/Stop come from the chain summaries only, and for a top-level edit they fall back to the project start, even though another row carries progress with a later Resume. W30-0.5 changed this behaviour (before it, the value came from every progressed summary) without a pair. Test (e) builds exactly this file and asserts only that the off-chain summary is untouched. It never compares the project row with Project. R29-5's text already named the project row. | Code read; test (e) at `:1945-1990`. The measured flat, leaf, work and milestone pairs each have a single progressed row (`tests/data/README.md`). | Refuse percent and date edits when any live row outside the edited set has progress, apart from the measured "Entire project" date update. Alternatively, commit a pair with progress elsewhere and compare the project row through `save_pair_noise.h`. |
| R30-4 | **High** (rule 1) | `app_qt/main_window.cpp:4256-4380` (`fill_down`), `app_qt/ribbon_bar.cpp` (`editing.fill` enabled), row menu `:4694-4696` | **Fill Down was newly enabled as a `.mpp` write, but its writer has no committed Project pair.** Fill stages Start values that the start/SNET writer (`save_date_edits`) saves. The record cites "Project's `manual-start-edit.json` pair". That JSON (`docs/analysis/oracle/manual-start-edit.json`) names `corpus/probe/base.mpp` and `corpus/probe/manual-start-edited.mpp`, which are git-ignored corpus files. `tests/roundtrip_manual_start.cpp` builds from the same corpus. No committed Project save of a Start edit exists, and no test compares a Start save with Project through `save_pair_noise.h`. The write was grandfathered before rule 1, and typing and Paste already reach it. But rule 1 says no `.mpp`-changing command is enabled without a committed pair, and Fill is a new enablement. The record's U1 adds that Fill stages Starts on summary rows. | `git ls-files` shows no Start-edit pair under `tests/data`. `grep` finds the probe paths only in `core/` comments and `docs/analysis/oracle/`. `fill_gate_test` checks staging, dirty state and undo, not saved bytes. | Measure and commit a Project Start-edit pair (a manual row and an automatic SNET row), with its null save, and compare through `save_pair_noise.h`. Until then, disable Fill with the decision-9 tooltip. In any case, refuse summary rows (U1). |
| R30-5 | Medium (extends the record's C4) | `app/edit_model.cpp:3255-3289`; `app_qt/main_window.cpp:4046-4064` (Table/Date Format), the cell-commit path | **Manually Schedule's exclusivity has two holes.** (a) A queued view-state or formula document edit (Table/Date Format, Custom Formula) is not checked, so 214/CTable or Props and the task-mode streams are written in one save, a combination no pair measures (the record's C4). (b) A Start cell edit or Fill on another row while Manually Schedule is queued (or the reverse order) is accepted in the shell. Only `save_structural_edits` refuses it, so the whole save fails and blocks every later Save until the user undoes. That is the R29-8 class G0 was told to move up front. | Code read: `push()` exclusivity checks `applied_` only. `make_task_mode_edit` compares only the edited row's dates with the file. `app_edits_task_mode_tests.cpp:966-990` confirms the refusal happens at save time. | In `make_task_mode_edit`/`push`, refuse when `document_edits.any()` or when any start edit is pending. Refuse a Start commit or Fill while a task-mode edit is queued. Test both orders. |
| R30-6 | Medium | `app/edit_model.cpp:2050-2085` (outdent branch); `core/mppwrite/outline.cpp:1913-1925` | **Outdent's parent assignment shape is refused only by the writer, as a warning, so the edit is silently dropped.** The outdent writer requires the parent summary to have exactly one physical assignment row, and that row must be a tombstone. The model checks the parent only for progress and for work > 0 (`check_outline_progress_row`). It does not check for a live zero-work real assignment, a second physical row, or a missing tombstone. The save then succeeds with a warning, and the reload shows the outdent gone. W30-C5 fixed exactly this pattern for indent and its comment names it ("the edit would simply be absent from the saved file"), but not for outdent. No data is lost; the user's edit is. | Code read. | Mirror `check_new_summary_shape`'s physical/live, real and raw-0 checks for the outdent parent, requiring one tombstoned placeholder row, and add tests. |
| R30-7 | Medium | `app/edit_model.cpp:345-385`; `core/mppwrite/actuals.cpp:1641-1663` | **The R29-6 pinning leaves three measured conditions unchecked.** (1) The project calendar's definition: the writer's 4800 ticks per day assumes the fixture's 8-hour Standard calendar, and only a task calendar is refused. (2) The resource's own calendar. (3) The source assignment's contour: the header admits the reader does not expose it, and the writer rebuilds a flat record pair, overwriting any other contour. The `work_ticks != 9600` branch has no test. | Code read. The tests at `app_edits_actuals_tests.cpp:2059-2105` cover duration, units, task calendar and zero work only. | Read and refuse a non-Standard project calendar or hours per day, a resource calendar, and a non-flat contour (or expose the contour in the reader). Add the missing test. |
| R30-8 | Medium (unverified) | `core/mppwrite/task_mode.cpp:658-...` (`read_task_mode_shape`); `app/edit_model.cpp:1278-1397` | **The task-mode shape does not pin some per-row conditions.** It checks links, progress, summaries, task calendar, placeholder work, the precondition bytes and the date words. It does not check the row's constraint type and date, deadline, duration format (elapsed or estimated) or task type. The fixture's rows have defaults. These may already be implied by FixedMeta +17/+18 and Fixed2Meta +8, but nothing shows that. | `grep` of `task_mode.cpp` finds no constraint, deadline or format reads. `tests/data/README.md:52`. | Refuse non-default constraint, deadline, elapsed or estimated duration and task type, or show from Project files that the precondition bytes encode them. |
| R30-9 | Low | `app/app_edits_task_mode_tests.cpp:417-420, 782-784` and 6 more new sites | A missing pair fixture still prints SKIP and passes (R28-9/R29-17). The wave added 8 new SKIP returns in app tests, including the only test of the newly enabled task-mode save. | `git diff wave29..wave30 -- app/*.cpp \| grep -c '^+.*printf("SKIP'` = 8 | Fail on a missing committed fixture. |
| R30-10 | Low | `scripts/check_fixtures.py:349-479` | 33 of the 66 new fixtures (insert, inactivate, respect-links, move, notes, timeline, text style) have no pinned hash in `DOCUMENTED`. They are guarded only by the HEAD comparison. The `tests/data/README.md` hash table lists all 66, and I re-hashed all 66: every one matches. | Script count (`check_fixtures.py` names 33 `wave30-` files). | Add the 33 hashes. |
| R30-11 | Low | `docs/START-HERE.md`, `docs/handoff/wave30-brief.md`, `docs/handoff/wave30-complete.md` | Six added lines name the operator by first name ("...'s decision", "... approved"), contrary to rule 6. This is the R29-18 habit carried forward. The fixtures and commit messages are clean. | Line count from my term scan over added lines (terms read from a file, reported by count). | Write "the operator". |
| R30-12 | Low | commits `8314f6c`, `bd83a6b`, `022f68c`, `6599e33`, `660d72e`, ... | Several commit messages carry claims their logs do not support. `8314f6c` says app_edits_actuals passed 3746 checks; `build/w30/W30-0.0-app-actuals.log` says 3736. "guard wave29..HEAD exit 0" cites zero-byte `W30-0.0..0.4-guard.log` and `W30-1.1/1.2/1.6-1.8/1.10/1.11-guard.log`. The record corrects both (A2/A3), but history keeps them. | `tail` of the logs; `ls -la` of the guard logs. | Process: write the gate log before quoting it in the message. |
| R30-13 | Low | `app/edit_model.cpp:758-790` | The outline progress check sees only `percent_complete > 0` and real work > 0. An ActualStart at 0 % and actual work at 0 % are not seen. All rows in the pairs have neither, so the check is narrower than the stated "no progress" condition. | Code read. Unverified whether Project can store such a row in practice. | Also refuse a valid ActualStart or actual work on the moved and restored rows. |
| R30-14 | Low | `scripts/check_fixtures.py` (`PROFILE_UTF16`) | R29-11 is still unfixed: the UTF-16 profile regex is not escaped and has no UNC branch. My own scan of the 66 new fixtures (reassembled streams plus the raw file, ANSI, UTF-16LE and UTF-16BE at both alignments, drive/profile/UNC paths, operator name terms, company and e-mail patterns) found 0 hits apart from the neutral `C:\w30\` root. | Scratch scan `priv_scan.py` and `drive_ctx.py`: 132 drive-path matches, all `C:\w30\...` in `Props14` and `214/CEdl/Var2Data`. Summary info: Author and LastAuthor "Fixture Builder", Company "Fixture Company", titles "Synthetic ... Fixture". | As in R29-11. |

## Checks that passed (spot-checked)

- **Task-mode pair** (`app_edits_task_mode_tests.cpp:520-720`): the five owned
  streams are compared with Project's file through `save_pair::compare`, using
  control, null save, Project and port. The 18+1 effect bytes are checked by
  offset, every other stream goes through the null mask, and the port's save is
  reopened. The file-side shape pins no links, no progress, top-level leaves,
  the project calendar, one live 0xFFFF0001 placeholder per task with work equal
  to duration, and the precondition bytes. Auto Schedule is disabled in the
  ribbon, the Mode menu, the row menu and Task Information, and the model and
  the save refuse it. The 331-check count matches `W30-1.0-task-mode-test.log`.
- **Deferred commands**: every one of the 22 measured-and-deferred items, plus
  Deliverable, Resource Pool, Team Planner, Subproject and the Add-ins, stays
  `enabled 0` in `ribbon_pin.tsv`. The inventory gate asserts the exact
  tooltip. The ribbon diff enables exactly the eight DONE controls.
  Task Information's Notes, Schedule mode, Display on Timeline and Advanced
  controls stay disabled with the tooltip. The row menu's Inactivate and
  Auto/Manually Schedule entries stay disabled.
- **Read-only panes**: `spelling_dialog.cpp`, `task_inspector.cpp`,
  `resource_form.cpp` and `app/spelling.cpp` contain no push, stage or save
  call. Scroll to Task's chart pan is session state in `PlanDocument`, cleared
  on load. The word list states its own provenance.
- **R29-8 at W30-0.0/C5**: the assignment-count, incoming-link, link-transition,
  real-lowering, physical-row, raw-0 and skipped-link checks run in
  `make_percent_edit`, `make_update_actuals_edit` and the plan-aware `push()`.
  Tests name each branch.
- **CLOSE gates**: `build/w30/W30-CLOSE-fresh-gates.log` is tree `0aca9d2`:
  core 52/52, plan_recalc 512/266, M2 332/332, 268/268, 64/64, app 22/22,
  app_qt 32/32, fixtures OK (131 documented, 167 tracked), scans 0. The pair and
  fixture counts (22 pairs, 13/6/3, 66 fixtures) and the backlog counts
  (69 + 7 disabled, 26 + 44 implemented) match the tree.
- **Regressions**: I found no data-loss path. W30-0.5 only narrows what the
  writer touches. W30-0.7 rewrites the core link tests to valid first links
  rather than loosening them, and adds two refusal tests. W30-C4 deletes unused
  symbols only.

## Agreement with the wave's own review (read after forming the above)

- R30-3 is the record's **C5**. I rate it High rather than Medium: it is an
  enabled save path wider than its pair, and R29-5 already named the project row.
- R30-5(a) is the record's **C4**. Part (b), the start-edit fail-at-save, is not
  in the record.
- R30-4 includes the record's **U1** (summary rows). The record treats the Start
  writer as measured; I do not, because its evidence is an uncommitted corpus probe.
- R30-12 restates A2/A3; the record already corrects them.
- R30-14 and R30-9 are the carried R28-5/R29-11 and R28-9/R29-17.
- The record does not have R30-1, R30-2, R30-6, R30-7, R30-8, R30-10, R30-11 or
  R30-13. Its statement that "G0 moved every enabled edit path's unmeasured-shape
  refusal up front" (decision 9) is too strong, given R30-1, R30-2, R30-3 and
  R30-6.
