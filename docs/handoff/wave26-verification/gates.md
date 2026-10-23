# Wave-26 verification gates (Opus, fresh trees, 2026-10-24 10:41-10:56)

Tip verified: local wave26 = 97f3986 (not pushed).
Trees: build/vs-verify26, build/vs-verify26-qt, build/vs-verify26-app (Release, /WX).

| gate | result |
|---|---|
| core / qt / app build | exit 0, 0 warnings each, no file locks |
| core ctest -E plan_recalc | 51/51 |
| plan_recalc_tests MPP_NO_CORPUS=1 | 266 checks, 0 failures |
| engine_diff_tests self-test | 265 checks, 0 failures |
| engine --suite | 7 pass / 1 fail (constraint-mso), 0 unrecorded |
| M2 recalc --compare | 332/332 |
| check_fixtures | OK (25 documented hashes, 21 tracked .mpp); oracle provenance OK (8) |
| app_qt ctest | 23/23 |
| app/ ctest | 17/17 |

Independent review (build/verify-w26/review.md): Critical 0, High 2, Medium 4, Low 7.
The two Highs are the new % Complete save path (one word written where Project
writes the actuals, remaining duration and the project rollup; also accepted on
summary tasks).  Verifier spot-check: the port's 40% save differs from its source
in exactly 1 byte.
CI: not run (publication hold).  Live same-file check: not repeated by the verifier.
