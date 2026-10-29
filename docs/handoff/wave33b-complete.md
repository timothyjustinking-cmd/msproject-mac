# Wave 33b - the completion record

One table row per brief item.  A group ends only when its exact
`WAVE-33b-<group>-COMPLETE` line is appended below (wave-33b-brief section 1).
Deferrals need the attempt evidence the standing rules and the brief name.

## G0 - the review's High and the CI compile errors

| item | finding | status | evidence |
|---|---|---|---|
| W33b-0.1 | R33-1 (High): Table / Date Format writes an unmeasured table | **DONE** | commit `3257a4f`; gates `build/w33b/W33b-0.1-*.log` |
| CI-1 | ubuntu-gcc, macos-appkit: `tests/fixture_guard.h:9` `"/*" within comment` | **DONE** | commit `eeee5c4`; gates `build/w33b/CI-1-*.log` |
| CI-2 | ubuntu-asan: `tests/atomic_write.cpp:50` unused function `join` | **DONE** | commit `26c0fb1`; gates `build/w33b/CI-2-*.log` |
| CI-3 | ubuntu-asan: `core/plan/plan.cpp:584` `manual_pin_failures` set but not used (`-Werror,-Wunused-but-set-variable`) | **DONE** | commit `939ddc8`; gates `build/w33b/CI-3-green-core.log`; found at CLOSE from the CI run of the G0 gates commit (`ci` 40958074116) |

No G0 item was deferred.  CI-3 was added at CLOSE from the CI run of the G0
gates commit; the G0 history above is unchanged and its detail is under
CLOSE.

W33b-0.1 detail:

- `Document::request_view_state_edits` now refuses a date-format edit whose
  `table_index` is not 0 before anything is queued (it returns a bool and the
  caller gets `view_state_date_format_refusal`); `apply_view_state_edit` adds
  the same refusal as a save-hook warning, so a hand-built request leaves
  `214/` byte-identical.  The Qt shell refuses Table / Date Format in the
  status line with the decision-9 reason before the dialog opens when the
  shown table is not the first; the macOS handler checks the request result
  (its view cannot show another table).  The table index stays in the request
  so a measured Project pair can lift the refusal later.
- RED: `app_document` reported "FAIL: 10 of 337 checks failed"
  (`build/w33b/W33b-0.1-red-app-ctest.log`); `qt_table_date_format` reported
  8 failures with both refusals temporarily disabled
  (`build/w33b/W33b-0.1-red-qt-ctest.log`).  GREEN: app ctest 25/25 with
  `document_tests: 337 checks passed` (`W33b-0.1-green-document-tests.log`);
  app_qt ctest 41/41.  The Qt test applies a second table, asserts the
  refusal and that the shell save writes no file, asserts a model save is
  byte-identical to the source, asserts the second table's stored block is
  unchanged, and asserts a table-0 edit changes exactly one `Var2Data` byte
  at `tables[0].block_offset + 4 + 7` to 0x05.
- The erratum in `docs/handoff/wave33-complete.md` says the "added no writer"
  sentence is false and why; the sentence itself is left as written.
- `docs/ui-command-backlog.md` regenerated: command backlog 114/114, 0
  mismatches.

CI-1 detail:

- `tests/fixture_guard.h:9` reworded to "tests/data/, and the tests/ and
  analysis/ TSV files", so no `/*` sits inside the block comment.
- Sweep `git grep -nE "/\*[^/]*/\*" -- '*.h' '*.hpp' '*.cpp' '*.mm'`: no
  matches; a block-comment scanner over all 342 tracked C/C++ files in
  `tests/`, `core/`, `app/` and `app_qt/`: 0 hit classes
  (`build/w33b/CI-1-grep.log`, `CI-1-sweep.log`).
- Gates: core build exit 0 and ctest 56/56; app build exit 0 and ctest 25/25
  (`build/w33b/CI-1-green-*.log`).

CI-2 detail:

- `join` removed from `tests/atomic_write.cpp`.  The sweep over the 79 C/C++
  files wave 33 added or changed found four more helpers left uncalled by
  W33-3.3 (`file_exists` in the cal, plan-recalc, session and usage tests);
  all were removed.  The same conversion also left the
  `CAL_NO_CORPUS` / `MPP_NO_CORPUS` forced-absent switch uncalled in
  cal_tests and session_tests, so those two uses were restored rather than
  deleted.
- Forced-absent proof with the corpus present: before, 785 cal checks and
  821 session checks with and without the variable; after, cal 785 -> 323
  with `CAL_NO_CORPUS=1` and session 821 -> 121 with `MPP_NO_CORPUS=1`, each
  printing the documented SKIP line (`build/w33b/CI-2-red-env-var.log`,
  `CI-2-green-env-var.log`).
- Gates: core build exit 0; core ctest 56/56 and 56/56 with
  `MPP_NO_CORPUS=1` (`build/w33b/CI-2-green-core-*.log`); the sweep rerun is
  clean (`CI-2-sweep-after.log`).

Every commit ran `check_fixtures.py` (OK), the staged-tree client-name scan
(0 hits), `guard_names.py main..HEAD` (exit 0) and `check_markers.py --range
main..HEAD` (0 hits); the logs are under `build/w33b/`.  Every changed file
is LF and `git diff --check` is clean.

After CI-1 and CI-2 the asan job reaches its tests for the first time: any
sanitizer report it raises is a G0 item of this wave (wave-33b-brief section
2), to be added by the coordinator on re-dispatch.  This group did not push
or dispatch CI.

G0 group gates (fresh trees configured from scratch on `1d5fc07` under
`build/w33b-g0-fresh{,-app,-qt}`, log
`build/w33b/W33b-G0-fresh-gates.log`; configured 08:14, log written 08:19 to
08:24 on 30 October 2026; the code tree is `26c0fb1` and this record differs
from it by documentation only):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 55/55 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failure(s) / 8 recorded (7 pass, 1 fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1136 checks / 456 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| app ctest | 25/25 passed |
| app_qt configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| app_qt ctest | 41/41 passed |
| `check_fixtures.py` | fixtures OK (140 documented hashes, 174 tracked `.mpp`); paths OK (223 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `main..HEAD` / `guard_names main..HEAD` | 0 files, 0 name hits / 0 commits / exit 0 |

The same `check_markers.py --range main..HEAD` pass recorded 0 hits over
1306 tracked files (9 markers, none an operator marker).

WAVE-33b-G0-COMPLETE

## G1 - the review's Mediums

| item | finding | status | evidence |
|---|---|---|---|
| W33b-1.1 | R33-2 (Medium): formula CLEAR is still live in the save hook | **DONE** | commit `b5f7e00`; gates `build/w33b/W33b-1.1-*.log` |
| W33b-1.2 | R33-3 (Medium): macOS Task Fields leaves refused edits queued | **DONE** | commit `2f2f8fb`; gates `build/w33b/W33b-1.2-*.log` |

No G1 item was deferred.

W33b-1.1 detail:

- `apply_formula_edit` refuses `Operation::clear` with the decision-9 reason
  (`custom_formula_clear_refusal`, shared with `Document::clear_custom_formula`)
  before the Props stream is read: the container is untouched and the whole
  save fails with no target written, exactly as W33-1.3's SET backstop does.
  The measured-clear implementation and its flag heuristic remain beneath the
  refusal so a future Project pair can lift it without reconstructing them.
- `test_cascade_with_document_edits_in_one_write` builds a hand-built clear,
  asserts the refusal, no document-edit write, no formula write and an
  unchanged pre-existing target (byte-identical to the prepared source), and
  drives the reachable one-write leg with the measured SET `[Duration]/240`
  (stored on Text2, decoded to `0x0b400507`, flag 1).  The no-value control
  leg and its flag-restoration proof are removed with the unreachable
  heuristic they tested.
- RED: app_document "FAIL: 7 of 341 checks failed", all seven the new
  clear-refusal assertions, with the pre-existing target growing 237,568 ->
  243,200 bytes because the clear still ran
  (`build/w33b/W33b-1.1-red-app-ctest.log`).  GREEN: app_document 341 checks
  passed (`build/w33b/W33b-1.1-green-document-tests.log`); app ctest 25/25.
- Per-commit gates: app build exit 0 and app ctest 25/25
  (`W33b-1.1-green-build.log`, `W33b-1.1-green-app-full-ctest.log`); app_qt
  build exit 0 and ctest 41/41 (`W33b-1.1-green-qt-build.log`,
  `W33b-1.1-green-qt-ctest.log`); fixtures OK; staged-tree scan 0 hits;
  `guard_names main..HEAD` exit 0; `check_markers` 0 hits.

W33b-1.2 detail:

- The macOS Task Fields handler pushed each edit of the dialog's batch onto
  the live `plan.edits` and, on a later refusal, reported "The field edits
  could not all be queued" and returned: the accepted edits stayed queued and
  the next Save wrote edits the user had been told had failed.  It now pushes
  onto `app::StructuralEdits trial = plan.edits` and moves the trial into
  `plan.edits` only when every edit is accepted -- the Qt shell's W33-1.5
  rule.  Before: a two-edit batch with a refused second edit leaves the first
  queued.  After: a refused push leaves the live queue untouched.
- `app/main.mm` is compiled only by the macOS app target
  (`app/CMakeLists.txt` adds it under APPLE); no local tree compiles it, so
  the local gate is the brace balance (`build/check_braces.py` exit 0, final
  depth 0, `W33b-1.2-braces.log`) and, for the rule itself, the Qt test
  `qt_task_information_apply` 1/1
  (`W33b-1.2-qt-task-information-apply.log`).  The macos-appkit CI job
  compiles the change.

G1 group gates (final code tree `2f2f8fb`, run 08:44-08:46 on 30 October 2026
in the G0 fresh trees `build/w33b-g0-fresh{,-app,-qt}`, log
`build/w33b/W33b-G1-fresh-gates.log`; the log's first two engine commands were
raw-binary usage errors and are superseded by its CORRECTION section):

| gate | result |
|---|---|
| core build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 |
| core ctest `-E plan_recalc` | 55/55 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine selftest / suite with `--expect-fail constraint-mso` (via `scripts/engine_diff.py`) | 53 checks, 0 failure(s) / 8 recorded (7 pass, 1 fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1136 checks / 456 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app build / ctest | exit 0 / 25/25 passed |
| app_qt build / ctest | exit 0 / 41/41 passed |
| `check_fixtures.py` | fixtures OK (140 documented hashes, 174 tracked `.mpp`) |
| `check_markers.py --range main..HEAD` | 0 hits over 1306 tracked files |
| client-name scan HEAD / messages / `guard_names main..HEAD` | 0 files, 0 hits / 0 commits / exit 0 |

WAVE-33b-G1-COMPLETE

## CLOSE

| item | status | evidence |
|---|---|---|
| fresh-tree gates on the final commit, logged (core, app with `CORE_WARNINGS_AS_ERRORS=ON`, app_qt, M2, engine suite, `check_fixtures`, `check_markers`) | **DONE** | log `build/w33b/W33b-CLOSE-fresh-gates.log`, all three trees configured from scratch on the final code tree `5efc8a5`; table below |
| both name scans and the marker check clean on `main..HEAD` | **DONE** | client-name scan HEAD 0 files/0 hits and messages `main..HEAD` 0 commits; `guard_names.py main..HEAD` exit 0; `check_markers.py --range main..HEAD` 0 hits over 1306 tracked files (9 markers) |
| one review pass over `git diff wave33..wave33b` | **DONE** | independent review of baseline `da5f4da`..`HEAD 21bdf13`: 0 Critical / 0 High / 0 Medium, 7 Low; repairs `5efc8a5`; queue RW33b-1/RW33b-2 |
| record "push and CI: by the coordinator" | **DONE** | "Push and CI" below |
| update START-HERE and `memory/coordinator.md` (wave 33 + 33b, not merged until CI is green and the review has no Critical/High) | **DONE** | `docs/START-HERE.md`, `memory/coordinator.md`; INDEX and the queue also updated |
| add R33-4..R33-8 to the queue in `plan-waves27-30.md` with file:line (R33-9 closes when the post-replay CI run is green) | **DONE** | `plan-waves27-30.md`, "Queued from the wave-33 verification review"; R33-9 stays open |
| append `WAVE-33b-COMPLETE` | **DONE** | last line |

CLOSE did not push or dispatch CI (wave-33b-brief sections 0 and 2).

### CI-3, added at CLOSE from the G0 gates commit's CI run

The `ci` run of the G0 gates commit (`6fdf3af`, run 40958074116, 2026-10-29
21:28Z) was red in one job: ubuntu-asan `Build` stopped at 76% on

```
core/plan/plan.cpp:584:17: error: variable 'manual_pin_failures' set but not
used [-Werror,-Wunused-but-set-variable]
```

(log `build/w33b/CI-3-asan-build-failed.log`; ubuntu-gcc and windows-msvc were
green, and `qt-shell` 40958079007 and `app-macos` 40958074537 were green --
CI-1 and CI-2 are therefore confirmed fixed on that commit).  The counter was
incremented when a manual pin's finish ran off the calendar but nothing read
it: `PlanResult::manual_unpinned` is `manual_rows - manual_pinned`
(`core/plan/plan.cpp:763`), so those rows were already counted.  The
declaration and the increment are removed; the comment now names
`manual_unpinned`.  No behaviour change.

Commit `939ddc8`; gates `build/w33b/CI-3-green-core.log` on tree `21bdf13`
plus the change, incremental in a tree configured
`CORE_WARNINGS_AS_ERRORS=ON` (plan.cpp was recompiled): core build exit 0;
core ctest `-E plan_recalc` 55/55; `plan_recalc` 512 checks with corpus /
266 with `MPP_NO_CORPUS=1`; M2 `recalc --compare` 332/332, 268/268 and 64/64
fields match; fixtures OK (140 documented hashes, 174 tracked `.mpp`);
`check_markers --range main..HEAD` 0 hits; scan HEAD and messages 0;
`guard_names main..HEAD` exit 0; backlog 114/114, 0 mismatches;
`git diff --check` clean.

### The CLOSE review

An independent review read `git diff wave33..wave33b` (baseline `da5f4da`,
HEAD `21bdf13`), the brief, this record, the standing rules and the
surrounding call sites, read-only.  Its verdict: **safe for CLOSE**, with no
Critical, High or Medium finding and seven Lows.  It re-checked refusal
reachability (the only production caller of `core::viewstate::set_table_date_format`
sits inside `apply_view_state_edit`, reachable only through the two save
paths; a non-zero table date format cannot be written through a public save),
the ordering of both refusals (before any stream read or queue change), the
formula-clear refusal in the cascade (target byte-identical), both shells, the
RED/GREEN numbers against the named logs, the record's gate numbers, privacy
and portability.  It confirmed the wave-33 erratum is accurate.

| # | severity | site | disposition |
|---|---|---|---|
| 1 | Low | `app/document.cpp:344,574-583` -- the backstops cover a date format but not a hand-built `replace_column` on a non-zero table, measured only at table 0; no production caller can build it | queued RW33b-1 |
| 2 | Low | `app/document.h:239` -- the refusal comment said "Empty for index 0"; the function always returns its message | fixed `5efc8a5` |
| 3 | Low | `app_qt/CMakeLists.txt:192-198` -- the `qt_table_date_format` comment still described the wave-33 gate (second table saves format 4) | fixed `5efc8a5` |
| 4 | Low | `memory/app-save.md:34` -- the snippet ignored the new `[[nodiscard]]` bool | fixed `5efc8a5` |
| 5 | Low | `tests/cal_tests.cpp:107ff` -- `corpus_gated` restores the documented forced-absent switch instead of deleting the helper | accepted: justified in the CI-2 commit; the CAL/MPP forced-absent outcomes are proven in its logs |
| 6 | Low | `tools/deepseek/run-wave.ps1:9` -- `[int]$Wave` to `[string]$Wave` is outside the brief's fix list | accepted: needed to run wave "33b"; `$Wave` is interpolated and used via `-f {0}`, never in arithmetic |
| 7 | Low | `app/document_tests.cpp` -- removing the no-value control leg leaves the retained clear/flag heuristic beneath the refusal untested | queued RW33b-2 |

The review's condition 1 (the in-flight CI-3 fix must be committed and gated)
is met by `939ddc8`; CI-3 was outside the reviewed range and the coordinator
checked the removed counter against `manual_unpinned` (above).  Condition 2
(the pre-CLOSE CI dispatch) is reconciled under "Push and CI".

### Fresh-tree gates

The table is written from `build/w33b/W33b-CLOSE-fresh-gates.log`: the three
trees (`build/w33b-close-fresh{,-app,-qt}`) were deleted and configured from
scratch on the final code tree `5efc8a5` (the CLOSE record commit differs from
it by documentation only).

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 55/55 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` (via `scripts/engine_diff.py`) | 53 checks, 0 failure(s) / 8 recorded (7 pass, 1 fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1136 checks / 456 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| app ctest / app ctest with `MPP_NO_CORPUS=1` (the CI step) | 25/25 / 25/25 passed |
| app_qt configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| app_qt ctest | 41/41 passed |
| `check_fixtures.py` | fixtures OK (140 documented hashes, 174 tracked `.mpp`); paths OK (223 tracked test-data files, no local path); provenance OK (8 oracles) |
| `check_markers.py` self-test / worktree / `--range main..HEAD` | 19 checks, 0 failures / 0 hits over 1306 tracked files / 0 hits, 0 messages |
| client-name scan HEAD / messages `main..HEAD` / `guard_names main..HEAD` | 0 files, 0 hits / 0 commits / exit 0 |
| block-comment sweep / unused-helper sweep | 0 hit classes over 342 files / 0 over the 13 changed files |
| line endings / client-string hashes | 0 CRLF or mixed among the pinned types / no match |
| `git diff --check` worktree / `main..HEAD` | exit 0 / exit 2: the 13 known wave-33 CRLF-json whitespace hits (RW33-10); `wave33..HEAD` is clean |

The log's working-tree section lists the four CLOSE documents being written
(`docs/START-HERE.md`, `docs/handoff/INDEX.md`,
`docs/handoff/wave33b-complete.md`, `memory/coordinator.md`); they are the
only files the CLOSE record commit adds.

### Push and CI

* `main` = **`17f5782`** = `origin/main`; **`wave33` = `da5f4da`** =
  `origin/wave33`, pushed after the operator-approved replay of the unpushed
  range (the replay replaced the FAT-slack fixture blob and reworded the one
  commit message the marker guard named; see
  `docs/handoff/wave33-verification/`).  `guard_names.py main..HEAD` exits 0.
* **`origin/wave33b` = `6fdf3af`**: the coordinator pushed the branch once
  before CLOSE, at the G0 gates commit, and dispatched both workflows.  That
  run is how CI-3 was found (`ci` 40958074116 red in ubuntu-asan only;
  `qt-shell` 40958079007 and `app-macos` 40958074537 green).  CLOSE itself
  did not push and did not dispatch (brief sections 0 and 2); this is the
  coordinator's pre-CLOSE push, recorded so the CI-3 evidence is not
  mistaken for a CLOSE action.
* **Push and CI: by the coordinator.**  The CI-3 fix `939ddc8`, the review
  repairs `5efc8a5` and this record are local.  The coordinator pushes them
  to `wave33b` and dispatches both CI workflows (`app-macos.yml` runs on the
  push) after CLOSE, then records the run ids.  R33-9 (the wave-33 record's
  CI-pending DONE items) closes when that post-replay run is green; the red
  runs of `da5f4da` and `6fdf3af` are the evidence that the first attempts
  were not.
* The wave is **not merged** until that CI is green and the review has no
  Critical or High finding -- this review has none, so the merge decision is
  the operator's after the coordinator's dispatch.

### Records updated

`docs/START-HERE.md` (wave 33 pushed after the replay; wave 33b complete;
CI-3 and the review; branch state; the new paste-in prompt),
`memory/coordinator.md` (waves 33 and 33b), `docs/handoff/INDEX.md` (the
wave-33b section, the wave-33 verification entries, the Now table),
`docs/handoff/plan-waves27-30.md` (R33-4..R33-8, RW33b-1/RW33b-2, RW33-3
closed by W33b-1.2).  The command backlog row for `table_date_format` was
regenerated by W33b-0.1 (`docs/ui-command-backlog.md`, 114/114, 0
mismatches).

### Found, not fixed

* **R33-9 stays open**: the wave-33 record's three CI-dependent DONE items
  are unproven until the coordinator's post-CLOSE dispatch is green.  The
  wave-33 run of `da5f4da` was red in CI-1 (ubuntu-gcc, macos-appkit) and
  CI-2 (ubuntu-asan); CI-3 (ubuntu-asan) sat behind CI-2 and only surfaced on
  the 33b G0 run of `6fdf3af`, and ubuntu-asan has not run since CI-3 was
  fixed -- so any further clang error or sanitizer report behind it is still
  unknown.
* **The review's Lows**: RW33b-1 (column-replacement backstop on a non-zero
  table) and RW33b-2 (the retained clear/flag heuristic is untested), plus
  R33-4..R33-8 from the wave-33 verification review, are queued in
  `plan-waves27-30.md`.
* **Local evidence limits**: `app/main.mm` is not compiled by any local tree
  (the W33b-1.2 change is brace-checked and proxied by the Qt trial-copy test;
  macos-appkit CI compiles it), and no sanitizer ran locally -- the asan job
  is the only sanitizer gate.
* **Pre-existing whitespace**: `git diff --check main..HEAD` reports the 13
  trailing-whitespace hits in five CRLF `docs/analysis/oracle/*.json` files
  inherited from wave 33 (RW33-10); `git diff --check wave33..HEAD` is clean
  and no wave-33b file adds one.
* **The wave-33b push history**: the branch carries one pre-CLOSE push at
  `6fdf3af`; the CLOSE rule ("CLOSE does neither") binds this session, and
  the coordinator's post-CLOSE push is the one that carries the fixes.

WAVE-33b-COMPLETE
