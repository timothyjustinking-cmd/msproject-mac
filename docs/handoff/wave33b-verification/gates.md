# Waves 33 and 33b verification: gates, CI and the merge rule

_Written 30 Oct 2026 by the Opus coordinator.  `main` = `17f5782` before the merge;
`wave33b` = wave 33 (`17f5782..da5f4da`, record in `docs/handoff/wave33-verification/`)
plus the fix-only wave 33b and four coordinator fix commits.  Logs are local under
`build/verify-w33b/` and `build/w33b-coord/` (git-ignored)._

## Fresh-tree gates on `bde04b3` (wave 33b CLOSE)

Fresh trees `build/vs-verify33b`, `-qt`, `-app` (none existed before), VS 2022 x64 Release,
Qt 6.5.3, `CORE_WARNINGS_AS_ERRORS=ON` on all three.

| gate | result |
|---|---|
| core build / ctest (`-E plan_recalc`) | exit 0, 0 warnings / 55/55 |
| plan_recalc_tests, `MPP_NO_CORPUS=1` | 266 checks passed |
| engine_diff_tests self-test | 265 checks, 0 failures |
| engine suite `--expect-fail constraint-mso` | exit 0, 7 pass + the known expected failure |
| M2 | 332/332 fields (268/268, 64/64), unexplained: none |
| `check_fixtures.py`, `check_markers.py` | exit 0 / exit 0 (1306 files, 0 hits) |
| app_qt build / ctest | exit 0, 0 warnings / 41/41 |
| app build / ctest | exit 0, 0 warnings / 25/25 |
| lane B property-set scan | 174 `.mpp/.mpt`, 0 flagged |

## The coordinator's fix commits after CLOSE

The first post-CLOSE CI run (`ci` 40965688565 on `bde04b3`) was the `ubuntu-asan` job's
first test run: 6 of 55 failed.  Fixed in one commit per cause, each with its gate:

| commit | id | fix | gate |
|---|---|---|---|
| `90d7eab` | CI-4 | the asan tree moves `build-asan` -> `build/asan`; `engine_diff` refuses a work directory outside `build/` (rule unchanged) | CI |
| `52e15c8` | CI-5 | `tests/gantt_tests.cpp` "month zoom name" compares strings, not two `const char*` addresses | gantt 1/1 |
| `b96ebd5` | CI-6 | `core/formula/expr.cpp` refuses a duration literal past +/-9.2e18 ticks (was an unchecked double -> int64 cast, UBSan in three fuzz tests) | RED 3/1252 on the unfixed lexer; GREEN formula + 3 fuzz targets |
| `b914006` | R33b-1 | the wave 33b review's Medium: a column change on any table but the first is refused by the request and the save hook, as the date format already was (only table 0 is measured) | RED 4/349; GREEN core 55/55, app 25/25, app_qt 41/41 |

A narrow independent review of the four commits (`review-fixes.md`): 0 Critical,
0 High, 0 Medium, 4 Low (queued).

## CI on `b914006`

| workflow (run) | job | result |
|---|---|---|
| `ci` (40969809660) | windows-msvc | success: core 55/55, `app/` 25/25, marker check OK |
| | ubuntu-gcc | success: core 55/55, `app/` 25/25, marker check OK |
| | ubuntu-asan | success: core and fuzz suite 55/55 under ASan + UBSan, marker check OK |
| `qt-shell` (40969813179) | windows-qt, linux-qt, macos-qt | success |
| `app-macos` (40969809471) | macos-appkit | success: 25/25 |

## Merge rule

CI green on all three OSs including the `app/` suite, the asan job and the marker check;
reviews: wave 33 (`../wave33-verification/review.md`) one High, R33-1, fixed by W33b-0.1;
wave 33b (`review.md`) 0 Critical, 0 High (its Medium R33b-1 fixed in `b914006`, R33b-2 was
the red asan job, fixed by CI-4 to CI-6); the fix commits (`review-fixes.md`) 0 Critical,
0 High.  The rule is met: `main` fast-forwards to this branch.

## Not covered

- The brief's RED-on-`17f5782` runs for W33-0.1, W33-0.2 and W33-1.1 were not re-run by
  the coordinator; the wave's own RED/GREEN logs are under `build/w33/`.
- macOS behaviour at run time (CI compiles and runs the AppKit tests only).
- No Project/COM measurement was made in waves 33 or 33b (none was needed: every change
  is a refusal, a safety mechanism, a test, a CI job or a record).
