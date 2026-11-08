# Wave 35 G1 (Calculate Project, stage 1): coordinator verification (Mon 9 Nov 2026)

The gated commit is `ec78e7a` (`WAVE-35-G1-COMPLETE`).  Gates ran in the verify worktree,
detached at `ec78e7a`, with fresh build trees, Release, `CORE_WARNINGS_AS_ERRORS=ON` and the
main corpus junctioned in read-only, from 03:08 to 03:37.  Logs are in `build/verify-w35/g1/`
(git-ignored).

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 63/63 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; expected failing set reached (`constraint-mso`) |
| M2 `recalc --compare` | exit 0 |
| app_qt / app | 50/50 / 32/32, 0 warnings |
| fixtures; markers | OK; OK |

## Fixtures

The twelve new `tests/data/wave35-calculate-*.mpp` files (four pairs and their null saves)
were scanned byte by byte, in ASCII and UTF-16LE, for operator, company and path markers.
There were 0 hits.  The only embedded paths are under the neutral `C:\w35\` root.  The
reviewer's independent scan agrees and matched all twelve SHA-256 values to
`check_fixtures.py`.

## CI on `ec78e7a`

| workflow (run) | result |
|---|---|
| `ci` 42498368626 | ubuntu-gcc, ubuntu-asan, windows-msvc green |
| `qt-shell` 42498371322 | linux-qt green; windows-qt green; macos-qt red, only on the two prompt-title checks of R35G0-1 (`qt_calculate_gate` passes on macOS) |

## Mutations (coordinator, `build/verify-w35/mutate-g1.py`)

Each guard was switched off with a non-constant false (MSVC C4127 is an error on a constant
one).  The listed tests were rebuilt and run by exact name, then the file was restored and
the tests re-run green.

| mutation | result |
|---|---|
| M1: writer "project finish already equals" off | **green** in `mppwrite_roundtrip_calculate` and `app_edits_calculate` (R35G1-7) |
| M2: writer "late finish after project finish" off | **green** in both (R35G1-7) |
| M3: writer start-anchor check off | **green** in both (R35G1-7) |
| M4: edit-model link refusal off | red in `app_edits_calculate` only on the synthetic "a plan with a link"; **green** in `qt_calculate_gate` (R35G1-8) |
| M5: positive control, leaf stamp 0x30 to 0x28 | red in core and app (Fixed2Data byte 298); **green in `qt_calculate_gate`**, whose save check is vacuous (new, queued as W35b-1.5b) |
| M6: task-mode link-table check off | red: the three refused pairs reach other guards |

## Independent review (`review-g1.md`)

A read-only agent reviewed `b8238bb..ec78e7a`, excluding `a7272d4`: 0 Critical, **3 High**,
6 Medium, 5 Low.  The coordinator confirmed the Highs at source.

- **R35G1-1 (High):** a task Deadline is refused in neither layer.
  `calculate_plan_refusal` (`app/edit_model.cpp:3238-3280`) checks the task count, links,
  outline, manual scheduling, constraints, progress and task calendars.
  `core/mppwrite/calculate.cpp` never reads `task_row::kDeadline`.  The outline save
  already refuses a deadline because it changes the backward pass (`edit_model.cpp:1069-1077`).
- **R35G1-2 (High):** any pending recalculation passes, not only the measured one (the
  critical leaf lengthens and no critical flag changes).  A recalculation that moves the
  critical path, shortens the project or shifts the project start is still written.
- **R35G1-3 (High):** the brief's rule 5 pinned parameters are not recorded, and these are
  refused in neither layer: resources, assignments, the project calendar's identity and
  exceptions, baselines and inactive tasks.
- **Mediums R35G1-4 to -9:** the save is not atomic (seven writes to the target); the
  request check misses late finish = project finish; an already-calculated file is refused
  only at save; eight writer guards are untested (M1 to M3 confirm three); generic needles
  (M4 confirms); the oracle's dates carry the host's UTC offset.
- **Lows R35G1-10 to -14:** listed in the review.
- **Clean:** the `task_mode.cpp` refactor is a pure extraction; there are no out-of-bounds
  reads; the fixtures and the diff carry no names.

## Merge rule

G1 does not meet the rule: three Highs are open.  Calculate Project stays on the unmerged
`wave35` branch.  The later wave 35 groups do not depend on it, so the run continues.  The
Highs, R35G0-1 (macOS) and the Mediums are the first groups of the fix wave 35b, which runs
after wave 35 CLOSE.
