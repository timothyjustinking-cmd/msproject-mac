# Wave 35d CLOSE: coordinator verification (Tue 10 Nov 2026)

Wave 35d is `d3b68fa..3d22c39`, 31 commits:
- 1 coordinator commit: `e17ddb7`, the brief;
- 30 builder commits for G0 to CLOSE.

The CLOSE commit `3d22c39` (`WAVE-35d-COMPLETE`) changes docs only.  The driver ran from 02:33 to
05:10 on 10 November 2026 and exited.

## The groups

| group | gated commit | gates | mutations | review |
|---|---|---|---|---|
| G0 | `86dbee9` | green, 0 warnings (65 / 52 / 34) | 7/9 red; S2 and S4 green but redundant | 0C / **1H** / 1M / 3L |
| G1 | `2ebcfd8` | green, 0 warnings (65 / 52 / 34) | 5/5 red | 0C / 0H / 3M / 4L |
| G2 | `d335791` | green, 0 warnings (65 / 52 / 34) | 9/9 red | 0C / 0H / 0M / 5L |

The details are in `coord-gates-g0.md` to `-g2.md` and `review-g0.md` to `-g2.md`, all in this
folder.

**Re-run mutations now red:**
- 35c's P3, the Props record count: R35cG0-1 is closed.
- 35c's Q10, the Paste guard: R35cG1-3 is closed.
- 35c's M9, now red in the app lane: the second text style is driven through the builder.

## Range scans (`d3b68fa..3d22c39`)

- `guard_names`: 0.
- `scan_client_names` at `3d22c39`: 0 files, 0 name hits.
- `check_markers`: 0.
- `.mpp` blobs added: 0.
- Every builder commit carries `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35d)`.

## CI: pending, because of a GitHub incident

`wave35d` was pushed at `3d22c39` as a new branch, with no force.  `ci.yml` and `qt.yml` were
dispatched at 18:11 UTC on 9 November 2026.

- **GitHub's incident:** at 18:12 UTC GitHub opened "Incident with Actions and Checks API".
  githubstatus reported Actions as `major_outage`.
- **What reached CI:**
  - `app-macos` failed both attempts with "Internal server error" before any job started.
  - `qt-shell` and `ci` created only some of their jobs.  `windows-msvc`, `ubuntu-asan` and
    `linux-qt` passed; `ubuntu-gcc`, `windows-qt` and `macos-qt` never started.
- **The reruns:** a watcher re-runs all three once githubstatus reports Actions operational.  The
  result is recorded in the status file.
- **Until then, 0.1 (R35cW-CI1, GCC) and 0.2 (R35cW-CI2, macOS) are unproved.**  The builder's
  "fixed" for both is premature (R35dG1-7).

## 4(e): Project's reopen of the enabled saves

- **Inputs:** the port saves from the verify worktree's app test output at `d335791`, whose code
  equals `3d22c39`.  `app` ctest was re-run first, 34/34.
- **Result: 9 pairs pass, with 0 fields differing between the port's reopen and Project's.**
  - The 9: Calculate Project under manual calculation, Sort x2, Show Level x3, and the
    assignment add and remove.
  - **Vacuous:** Sort Name ascending (by design) and Bold (not readable through COM).
- This is the same as waves 35b and 35c.  The whole-wave review also confirms that no writer's
  output bytes changed this wave.
- Evidence (git-ignored): `build/verify-w35d/reopen-4e/`.

## Whole-wave review (`review-wave.md`)

0 Critical, 0 High, 3 Medium, 3 Low.

- **R35dW-1 (Medium):** R35dG0-1's route also re-queues a withdrawn Calculate Project.
  - The steps: Calculate, Ctrl+Z, a Task Information edit with a valid Duration and a failing
    Finish, then Ctrl+Y.
  - The 2.11 records say this route is closed.
  - **Fix with R35dG0-1:** clear the redo list and rebuild whenever any field committed, and add
    the case to `qt_calculate_gate` as well.
- **R35dW-2 (Medium):** no R35d id was in the repo at CLOSE.  This commit adds the reviews and
  records here.  START-HERE, `memory/coordinator.md` and `plan-waves27-30.md` still read "no High
  open" and need the queue below.
- **R35dW-3 (Medium):** five wave 35c ids are recorded "fixed" that are only partly fixed:
  R35cG1-2, R35cG1-4, R35cW-1, R35cG0-3 and R35cW-4.  The CI ids are also marked fixed before CI.
- **Lows:**
  - R35dW-4: the `calculate.md` Scope sentence predates 2.8's move of the pin.
  - R35dW-5: the macOS AppKit shell has no 2.11 refusal.
  - R35dW-6: R35cW-3 has two statuses, and some headers are stale.

## Open queue for the next fix wave (High first)

1. **CI:** confirm R35cW-CI1 and R35cW-CI2 once GitHub Actions recovers.
2. **R35dG0-1 (High), with R35dW-1:** a partly applied Task Information edit keeps the structural
   redo list.  Tests go in `qt_sort_persist`, `qt_outline_persist` and `qt_calculate_gate`.
3. **Mediums:** R35dG0-2, R35dG1-1, R35dG1-2, R35dG1-3, R35dW-2, R35dW-3.
4. **Lows:** R35dG0-3 to -5, R35dG1-4 to -7, R35dG2-1 to -5, R35dW-4 to -6.

## Merge rule

**Waves 34 to 35d are not merged.**  One High is open (R35dG0-1), and CI on three OSs is not yet
green.  4(e) passes.
