# Wave 34 tip: coordinator verification (Sun 8 Nov 2026)

Gated commit `8400c2c` (WAVE-34-COMPLETE).  The run used the verify worktree with fresh
build trees (core, Qt and app deleted and reconfigured), Release and
`CORE_WARNINGS_AS_ERRORS=ON`.  The main tree's `corpus/` was junctioned in read-only, so
the corpus lanes ran too.  Logs are in `build/verify-w34close/` (git-ignored); the run
went from 17:04 to 17:29.

| gate | result |
|---|---|
| core configure / build | exit 0 / exit 0, 0 compiler warnings |
| core ctest `-E plan_recalc` | 62/62 passed |
| `plan_recalc_tests` with `MPP_NO_CORPUS=1` / with corpus | 266 / 512 checks, exit 0 |
| engine self-test; `--suite --expect-fail constraint-mso` | 265 checks, 0 failures; 8 scenarios, 7 pass, 1 expected fail, 0 unrecorded |
| M2 `recalc --compare` | 332/332 and 268/268 fields match |
| app_qt configure / build / ctest | exit 0 / exit 0 / 47/47, 0 warnings |
| app configure / build / ctest | exit 0 / exit 0 / 31/31, 0 warnings |
| `check_fixtures.py` | 219 tracked `.mpp/.mpt` property sets OK, 8 oracles' provenance OK |
| `check_markers.py --range 4fd9b90..HEAD` | OK, 0 hits |
| `guard_names.py main..wave34`; `scan_client_names.py wave34 --messages main..wave34` | exit 0; 0 files, 0 hits, 0 commits |
| trailer | 18 Sonnet commits (G3, G4, CLOSE) carry `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 34)` |

The local numbers agree with the CLOSE record.

## First CI run of `wave34` (pushed 17:08; runs on `8400c2c`)

| workflow (run) | jobs |
|---|---|
| `ci` 42453370829 | windows-msvc green; **ubuntu-gcc and ubuntu-asan red** |
| `qt-shell` 42453367272 | windows-qt green; **linux-qt red**; macos-qt cancelled (fail-fast) |
| `app-macos` 42453351442 (push) | **macos-appkit red** |

- **CI-1:** `core/mppwrite/props_edit.cpp:245`, the unused function `owned_stream`
  (`-Werror=unused-function`), breaks gcc, clang and AppleClang.  This is a wave 34 defect.
- **CI-2:** `core/gantt/gantt.cpp:390`, gcc 15 `-Werror=array-bounds` in `std::sort` over
  a prefix of an 8-element array.  The prefix is bounded by `working.size()`, so this is a
  false positive.  The file did not change in wave 34, and `main`'s last green qt run was
  27 Oct 2026, so the runner's compiler moved underneath old code.

Both are G0 items of `wave34b` (W34b-0.6 and 0.7).  Neither is reachable locally: MSVC
builds clean.

## Independent review of G4 and CLOSE (`review-g4-close.md`)

A read-only agent reviewed `3a8a31c..8400c2c` and found 0 Critical, 5 High, 3 Medium and
3 Low.  The coordinator re-read each High at source:

- **R34G4-1:** every assignment pair is on a MANUAL leaf, and `assignment_plan_refusal`
  never reads the scheduling mode.  CONFIRMED.
- **R34G4-2:** the assignment ignores rates, cost per use and the calendars.  CONFIRMED
  from the code: the placeholder's zero cost words are copied.
- **R34G4-3:** Add Resources clones the last resource's 172-byte row, including its work.
  The template checks cover only MaxUnits and the two var blocks
  (`resource_growth.cpp:496-511`).  CONFIRMED.
- **R34G4-4:** the Bold writer is pinned to view uid 2
  (`viewstream_edit.cpp:396`), and nothing proves that is the applied view.  CONFIRMED.
- **R34G4-5:** WBS writes `number_payload(uid)` (`wbs_code.cpp:191`), with no
  ID-equals-UniqueID gate.  CONFIRMED.

The question set at the G3 verify ("does Assign Resources refuse every work amount other
than the measured 40 hours?") is answered **no**.  The CLOSE review's RC-R1 and this review
agree: R34G3-1 is live in the UI.

## Merge rule

Wave 34 is not mergeable:

- seven Highs are open: R34G3-1, R34G3-2 and R34G4-1 to R34G4-5;
- CI is red on four jobs.

A fix-only `wave34b` (`docs/handoff/wave34b-brief.md`) runs first.  The coordinator's
mutations of the G4 gates are moved to the `wave34b` tip, because 34b rewrites those gates.
The brief section 4(e) Project reopen check of the enabled saves also runs on the `wave34b`
tip.
