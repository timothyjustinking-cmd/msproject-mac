# Wave 34b tip: coordinator verification (Sun 8 Nov 2026)

The gated commit is `3c69150`: `WAVE-34b-COMPLETE` (`c825eb4`) plus the coordinator's
macos-qt runner pin.  Gates ran in the verify worktree with fresh build trees, Release,
`CORE_WARNINGS_AS_ERRORS=ON` and the main corpus junctioned in read-only, from 23:19 to
23:41.  Logs are in `build/verify-w34b/` (git-ignored).

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 62/62 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; expected failing set reached (`constraint-mso`) |
| M2 `recalc --compare` | 332/332, 268/268, 64/64 |
| app_qt / app | 47/47 / 31/31, 0 warnings |
| fixtures; markers | OK; OK |
| `guard_names.py`, `scan_client_names.py`, `check_markers.py` on `main..wave34b` | 0 hits |

## CI on `3c69150`

| workflow (run) | result |
|---|---|
| `ci` 42479685193 | ubuntu-gcc, ubuntu-asan, windows-msvc green (CI-1 fixed) |
| `qt-shell` 42479681310 | linux-qt green (CI-2 fixed, AppImage packaged); windows-qt green; **macos-qt red: `qt_assign_resources_gate`, "an assigned task is offered the removal"** (CI-4) |

- **CI-3:** GitHub retired the `macos-14` image.  The job annotation read "The
  configuration 'macos-14-arm64-us-default' is not supported", and every macos-qt job since
  29 Oct 2026 was cancelled before start.  The coordinator's commit `3c69150` pins macos-qt
  to `macos-15`.  This run is the first macOS Qt run since 29 Oct.  CI-4 comes from W34-4.3
  and had no macOS run until now.
- On wave 34b's G0 commit, linux-qt's first attempt failed only in AppImage packaging:
  HTTP 504 three times fetching `linuxdeploy`, while the build and tests were green.  The
  re-run passed.

## Independent review of 34b (`review-34b.md`)

A read-only agent reviewed `023c858..3c69150`: 0 Critical, 1 High, 5 Medium, 5 Low.

- **R34b-1 (High):** Assign Resources and its removal never check the resource's base
  calendar contents.  The coordinator confirmed this at source: the writer checks only
  resource row word 154 = UniqueID + 2 (`assignment_growth.cpp:846-849`), and the edit
  model checks only the task's own calendar.  The CLOSE review had recorded it as a Medium
  (RW34b-1).
- The review verified these items as complete: 0.2 (the +16/+32 identity references are
  compared, and planted wrong values go red), 0.3, 0.4, 0.5, 0.6, 0.7 (the insertion sort
  is stable and orders the same as `std::sort`), 1.1, 1.2, 1.3, 1.4 and 1.5.
- Item 0.1 is incomplete: R34b-1, and R34b-2, under which several of its checks exist only
  in the writer.

## Merge rule

Waves 34 and 34b are not mergeable while R34b-1 (High) and CI-4 (macos-qt) are open.  Both
are the first items of wave 35's G0 (W35-0.1 and W35-0.3), with the Mediums as W35-0.2.
Wave 35 is cut from `wave34b` at this record.  The coordinator merges once G0's CI is green
on all three OSs and its review has no Critical or High.  Brief section 4(e), the Project
reopen check, is still the coordinator's and runs before any merge.
