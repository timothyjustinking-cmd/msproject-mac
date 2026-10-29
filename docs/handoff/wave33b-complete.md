# Wave 33b - the completion record

One table row per brief item.  A group ends only when its exact
`WAVE-33b-<group>-COMPLETE` line is appended below (wave-33b-brief section 1).
Deferrals need the attempt evidence the standing rules and the brief name.

## G0 - the review's High and the two CI compile errors

| item | finding | status | evidence |
|---|---|---|---|
| W33b-0.1 | R33-1 (High): Table / Date Format writes an unmeasured table | **DONE** | commit `3257a4f`; gates `build/w33b/W33b-0.1-*.log` |
| CI-1 | ubuntu-gcc, macos-appkit: `tests/fixture_guard.h:9` `"/*" within comment` | **DONE** | commit `eeee5c4`; gates `build/w33b/CI-1-*.log` |
| CI-2 | ubuntu-asan: `tests/atomic_write.cpp:50` unused function `join` | **DONE** | commit `26c0fb1`; gates `build/w33b/CI-2-*.log` |

No G0 item was deferred.

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
