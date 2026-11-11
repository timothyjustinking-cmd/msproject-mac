# Wave 35e: completion record

## G0: the High

Builder record: `wave35e-verification/gates-g0.md`. Commit: `b8ee455` (0.1). The trailer is on it,
checked by script.

| Item | Finding | Status |
|---|---|---|
| 0.1 | R35dG0-1 (High), with R35dW-1 | fixed `b8ee455`; a partly applied Task Information or Task Form edit ends the structural redo list; tested in `qt_sort_persist`, `qt_outline_persist` and `qt_calculate_gate`; the sweep of the other cell-commit paths found none (Cut, Paste and Fill Down are one transaction) |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`;
`43fee2a-W35e-0.1-*`): core build 0 with 0 warnings; `ctest -E plan_recalc` 65/65; app 34/34;
app_qt 52/52; `check_fixtures` 0; `check_markers --range main..HEAD` 0; `guard_names` 0;
`scan_client_names` 0; client-string grep none. The measured pairs still write and match with
effects on.

**Guards seen red:**
- 0.1: the `if (!ok && applied)` clear off, 9 FAILs, three in each of the three tests
  (`build/w35e/0.1-red-run.log`).

**Deferred:** nothing.

**Found, not fixed:**
- Nothing new. A partly applied edit still leaves its committed fields in place (no rollback), by
  the brief's fix; the status line names the failing field and Undo takes the committed cells back.

No writer added, no refusal lifted, no pair measured, no COM, nothing pushed, no workflow
dispatched.

WAVE-35e-G0-COMPLETE

## G1: the Mediums

Builder record: `wave35e-verification/gates-g1.md`. Commits: `39c8e37` (1.1), `1da3997` (1.2),
`02f1900` (1.3), `f244d02` (1.4). The trailer is on each, checked by script.

| Item | Finding | Status |
|---|---|---|
| 1.1 | R35dG1-1 | fixed `39c8e37`; (a) a Level 1 or deeper is refused while a session Sort is shown (since W35f-0.1 only its QUEUE is refused and the Level is shown for the session), (b) a sort choice ends a Show Level on the redo list; both tested in `qt_outline_persist` on the six-task outline, and (a) on the five-task file by an Undo of a queued Sort in `qt_sort_persist` (W35f-0.3), and (b) on the five-task file by a Level 1, Ctrl+Z, Start sort, Ctrl+Y in `qt_sort_persist` (W35g-0.2) |
| 1.2 | R35dG1-2, with R35dW-4 | fixed `1da3997`; `calculate.md`'s Scope paragraph corrected (the Notes and Text1 removals, the percent-complete and indent and outdent saves, and the pin as the last check) |
| 1.3 | R35dG1-3, R35dW-2, R35dW-3 | fixed `02f1900`; the open queue is in START-HERE, `memory/coordinator.md` and `plan-waves27-30.md` from `wave35d-verification/coord-gates-close.md`; R35cG1-2, R35cG1-4, R35cW-1, R35cG0-3 and R35cW-4 read "partly fixed"; R35cW-CI1 and R35cW-CI2 read "fixed in source; CI is its gate"; the Show Level sentence is corrected |
| 1.4 | R35dG0-2 | fixed `f244d02`; 0.5's record reads "partly fixed: Task Information partial apply", closed by W35e-0.1 `b8ee455` |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; 1.1
`5d733ae-W35e-1.1-*`, the group's final tree `f244d02-W35e-G1-record-*`): core build 0 with 0
warnings; `ctest -E plan_recalc` 65/65; app 34/34; app_qt 52/52; `check_fixtures` 0;
`check_markers --range main..HEAD` 0; `guard_names` 0; `scan_client_names` 0; client-string grep
none. 1.2 to 1.4 are records only (scans, fixtures and `git diff --check`, 0). The measured pairs
still write and match with effects on.

**Guards seen red:**
- 1.1: the shown-Sort Level refusal and the Show Level redo clear off together, 6 FAILs in
  `qt_outline_persist` (`build/w35e/1.1-red-run.log`); not run one at a time, each needle belongs
  to one guard.

**Deferred:** nothing.

**Found, not fixed:**
- ~~A session-only Level chosen while a Sort sits on the redo list leaves that Sort there; Ctrl+Y
  would re-queue it over the session Level.~~ Withdrawn by W35f-0.3 (R35eG1-3): the route is
  refused by 1.1(a) at 35e and, since W35f-0.1, shown for the session and not queued; Ctrl+Y
  re-queues the Sort the display shows (`gates-g1.md`; tested in `qt_sort_persist`).

No writer added, no refusal of a persisted shape lifted, no pair measured, no COM, nothing pushed,
no workflow dispatched.

WAVE-35e-G1-COMPLETE

## G2: the Lows

Builder record: `wave35e-verification/gates-g2.md`. Commits: `086e111` (2.1), `5f9a5e7` (2.2),
`4bc2815` (2.3), `87afb79` (2.4), `ae4782a` (2.5), `c2880d7` (2.6), `e03da3e` (2.7), `da5f81d`
(2.8), `aa06507` (2.9), `d697a05` (2.10), `6fa4c19` (2.11), `9f0cb92` (2.12), `3946d9a` (2.13).
The trailer is on each, checked by script.

| Item | Finding | Status |
|---|---|---|
| 2.1 | R35dG0-3 | fixed `086e111`; the queued-Sort re-apply maps the writer's field id to the display field (`app::queued_sort_display_key`); the helper is tested for Name and Duration (`app_edits_sort_tests`), but the Qt call site's Duration branch is defensive and unreached: the Qt Sort dialog maps a Duration key to `SortKeyKind::other`, which `make_sort_edit` refuses, so no Duration sort can be queued from the Qt shell, and every route that changes the shown sort clears the redo list first, so the call site never differs from the writer's id for the one kind (Name) the shell can queue; reverting the call site would fail no test (R35eW-3, recorded W35f-1.9) |
| 2.2 | R35dG0-4 | fixed `5f9a5e7`; a direct `apply_session_sort` after Undo ends the redo list; the "rows show" check is dropped because no route reaches the re-apply with a different display (reason in the test) |
| 2.3 | R35dG1-5 | fixed `4bc2815`; a sorted display under a queued Level, then a clear, keeps the level and shows the file's order; reached through `apply_session_sort` (a seam, not a production route) |
| 2.4 | R35dG1-6 | fixed `87afb79`; the sort_view branch of `view_change_refusal` seen red; "clearing a session sort" is written into the 35d-1.2 rule as its exception |
| 2.5 | R35dG2-1 | **partly fixed** `ae4782a`: Task Information, Cut and Paste are tested; Fill Down's guard stays untested (`fill_down()` is private and its controls are disabled, W31-0.4) |
| 2.6 | R35dG2-2 | fixed `c2880d7`; Table / Date Format and Custom Field Formula are refused beside a queued Calculate, and Calculate beside a pending edit of either, both orders tested |
| 2.7 | R35dW-5 | fixed in source; CI is its gate `e03da3e`: the AppKit shell refuses a cell edit and a paste beside a queued Calculate (a refusal mirroring the Qt shell's); `app/main.mm` is not compiled or tested here |
| 2.8 | R35dG0-5 | fixed `da5f81d`; the two `check_props_pin` tail branches each have a test with their own needle |
| 2.9 | R35dG1-4 | fixed `aa06507`; the fault seam also fires after the Props edit, the target is byte-identical, the seam is reset by a scope guard |
| 2.10 | R35dG2-3 | fixed `d697a05`; the unmarked tombstone case asserts the builder accepts and the edit is the removal asked for |
| 2.11 | R35dG2-4 | fixed `6fa4c19`; the scratch-probe seam takes a file type and a not_found probe with an error code proceeds |
| 2.12 | R35dG2-5 | fixed `9f0cb92` (record) |
| 2.13 | R35dW-6 | fixed `3946d9a` (records); R35cW-3 is "fixed `6f5cc18` (record)", R35cG0-2 stays partly fixed |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; the group's final tree
`3946d9a-W35e-G2-final-*`): core build 0 with 0 warnings; `ctest -E plan_recalc` 65/65; app 34/34;
app_qt 52/52; `check_fixtures` 0; `check_markers --range main..HEAD` 0; `guard_names` 0;
`scan_client_names` 0; client-string grep none. 2.7, 2.12 and 2.13 ran the scans without a build.
The measured pairs still write and match with effects on.

**Guards seen red:**
- 2.1: the writer's id returned as the display key, 1 FAIL (`build/w35e/2.1-red-run.log`).
- 2.2: the re-apply's clear removed, 2 FAILs (`2.2-red-run.log`).
- 2.3: `clear_sort` made to `expand_all`, 3 FAILs (`2.3-red-run.log`).
- 2.4: the sort_view branch of `view_change_refusal` removed, 19 FAILs in `qt_sort_persist`
  (`2.4-red-run.log`).
- 2.5: the three guards changed to `refuse_view_change`, 5 FAILs (`2.5-red-run.log`).
- 2.6: the three guards made inert, 5 FAILs (`2.6-red-run.log`).
- 2.8: each tail branch made inert, 2 FAILs (`2.8-red-run.log`).
- 2.9: `save_props_edit` made to write the target, 1 FAIL (`2.9-red-run.log`).
- 2.10: the builder made to refuse another text, the two new checks FAIL and the old one does not
  (`2.10-red-run.log`).
- 2.11: the refusal made to bite under the seam whatever the file type, 2 FAILs
  (`2.11-red-run.log`).
- 2.7 has none: nothing compiles or tests `app/main.mm` here. 2.12 and 2.13 are records.

**Deferred:** nothing.

**Found, not fixed:**
- Fill Down's Calculate guard has no test and no route to one (2.5).
- The AppKit refusal (2.7) has no test; CI on `macos-appkit` (`app-macos.yml`, `-Werror`, runs on
  push), and on `macos-qt`, which builds `app/` too, is its only gate (corrected by W35f-1.14,
  R35eG2-5).
- `apply_session_sort` has no queued-view guard (2.3). The shell's menu does not reach it, so
  nothing is wrong today.

No writer added, no refusal of a persisted shape lifted, no pair measured, no COM, nothing pushed,
no workflow dispatched.

WAVE-35e-G2-COMPLETE

## CLOSE

### CLOSE gates

Fresh trees `build/w35e-close-fresh`, `-app` and `-qt` at `2dbb5d9` (the last code changes are
`aa06507` (2.9) and `6fa4c19` (2.11), test seams in `core/mppwrite`; `d697a05` is tests only;
every later commit is a record; corrected by W35f-1.15, R35eW-2),
configured from scratch with `CORE_WARNINGS_AS_ERRORS=ON`; script
`build/w35e/W35e-CLOSE-fresh-gates.ps1`, log `build/w35e/W35e-CLOSE-fresh-gates.log` (summary:
`build/w35e/close_log_summary.py`). The START-HERE, `memory/coordinator.md` and
`plan-waves27-30.md` state lines changed after that run; they are documents and no gate reads
them.

- core build exit 0 with warnings as errors; `ctest -E plan_recalc` 65/65;
- `plan_recalc_tests` 512 checks with the corpus, 266 with `MPP_NO_CORPUS=1`;
- engine self-test 265 checks, 0 failures; `engine_diff.py --selftest` 53 checks; engine suite 8
  scenarios, 7 pass and the expected `constraint-mso` failure (1 of 247 fields);
- reader tests 1469 checks with the corpus, 789 without; M2 `recalc --compare` 332 of 332 fields;
- app build exit 0 and ctest 34/34, with and without `MPP_NO_CORPUS=1`;
- app_qt build exit 0 and ctest 52/52; no run reported "No tests were found";
- `check_fixtures.py`, `check_markers.py` (self-test 19 checks, worktree, `main..HEAD`) and
  `check_command_backlog.py` (114 pins, 114 rows) exit 0; `gen_calculate_props_pin.py --check` up
  to date; `scan_client_names.py` on the head and on the `main..HEAD` messages 0 hits;
  `guard_names.py main..HEAD` exit 0; the client-string grep finds none; 0 CR in the LF-pinned
  types; `git diff --check main..HEAD` exit 0.
- The gcc, clang and AppleClang jobs cannot run here. **CI is the only proof of W35e-2.7**
  (`app/main.mm`, the AppKit refusal), and of R35cW-CI1 and R35cW-CI2, which stay "fixed in
  source; CI is its gate" until the coordinator's run is green.
- Trailer `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35e)`: present on all 21 builder
  commits in `2ea87e5..HEAD` (checked by script at CLOSE). The one commit without it, `43fee2a`,
  is the coordinator's wave 35e brief.

### Ids, with the commit that closes each

| Group | Ids | Result |
|---|---|---|
| G0 | R35dG0-1 (High), R35dW-1 | fixed `b8ee455`; closed by the coordinator's G0 review |
| G1 | R35dG1-1 | fixed `39c8e37`, both bypasses (bypass (a) was also reachable on the five-task file through an Undo of a queued Sort, which had no test until W35f-0.3; the "Found, not fixed" route is withdrawn; the refusal is narrowed to the queue by W35f-0.1) |
| G1 | R35dG1-2, R35dW-4 | fixed `1da3997` |
| G1 | R35dG1-3, R35dW-2, R35dW-3 | fixed `02f1900` (records) |
| G1 | R35dG1-7 | fixed `02f1900` (records): R35cW-CI1 and R35cW-CI2 read "fixed in source; CI is its gate" |
| G1 | R35dG0-2 | fixed `f244d02` (record) |
| G2 | R35dG0-3, R35dG0-4 | fixed `086e111`, `5f9a5e7` |
| G2 | R35dG1-5, R35dG1-6 | fixed `4bc2815`, `87afb79` |
| G2 | R35dG2-1 | **partly fixed** `ae4782a`: Fill Down's Calculate guard has no route to a test |
| G2 | R35dG2-2 | fixed `c2880d7` |
| G2 | R35dW-5 | fixed in source; CI is its gate `e03da3e` |
| G2 | R35dG0-5, R35dG1-4, R35dG2-3, R35dG2-4 | fixed `da5f81d`, `aa06507`, `d697a05`, `6fa4c19` |
| G2 | R35dG2-5, R35dW-6 | fixed `9f0cb92`, `3946d9a` (records) |

Every id in `wave35d-verification/` appears in this file. Nothing in this wave is deferred. Only
R35dG2-1 is partly fixed, and R35dW-5 and the two CI ids wait on CI. The wave 35c ids that 35d
left partly fixed (R35cG1-2, R35cG1-4, R35cW-1, R35cG0-3, R35cW-4) lost their R35d remainders to
G0 and G1 of this wave; R35cG0-2 and R35cG0-4 keep theirs (the plan half, and the plan-side
Props digest), both refused at the writer or the save.

**Guards seen red:** per group in the G0, G1 and G2 rows above; the logs are
`build/w35e/*-red-*.log`. CLOSE adds no guard.

**Found, not fixed** (carried from the group rows; nothing new at CLOSE):
- ~~A session-only Level chosen while a Sort sits on the redo list leaves that Sort there (G1).~~
  Withdrawn by W35f-0.3 (R35eG1-3), see the G1 row.
- Fill Down's Calculate guard has no test and no route to one (2.5).
- The AppKit refusal (2.7) has no test; CI on `macos-appkit` (`app-macos.yml`, `-Werror`, runs on
  push), and on `macos-qt`, which builds `app/` too, is its only gate (corrected by W35f-1.14,
  R35eG2-5).
- `apply_session_sort` has no queued-view guard (2.3); the shell's menu does not reach it.
- The plan half of R35cG0-2 and the plan-side Props digest of R35cG0-4.

**For the coordinator, after CLOSE:** push `wave35e` and dispatch `qt.yml` and `ci.yml` (the two CI
ids are gated there; W35e-2.7 is gated by `app-macos.yml`'s `macos-appkit` job, which runs on push,
so the merge check "CI green on three OSs" must include that run too; W35f-1.14, R35eW-4); mutations at each new guard; an independent review of
`wave35d..wave35e`. The 4(e) reopen check is a rerun only if a writer's output changed; this wave
adds no writer. Merging waves 34 to 35e waits on CI green on all three OSs and no open Critical
or High.

No writer added, no refusal of a persisted shape lifted, no pair measured, no COM, nothing pushed,
no workflow dispatched, no history rewrite.

WAVE-35e-COMPLETE
