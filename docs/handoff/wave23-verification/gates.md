# Wave-23 verification gates (Opus, fresh trees, 2026-10-23 22:43-22:58)

Tip verified: local wave23 = d236f91 (not pushed; origin/wave23 = 63cd087).
Trees: build/vs-verify, build/vs-verify-qt, build/vs-verify-app (Release, /WX).

| gate | result |
|---|---|
| core build | 0 warnings |
| core ctest -E plan_recalc | 51/51 (50 + engine_diff_suite) |
| plan_recalc_tests MPP_NO_CORPUS=1 | 266 checks, 0 failures |
| engine_diff_tests self-test | 265 checks, 0 failures |
| engine --suite (no --apply-bin: the new newest-helper default) | 7 pass / 1 fail (constraint-mso), 0 unrecorded - the old stale-exe trap is gone |
| M2 recalc --compare | 332/332 (268 + 64) |
| check_fixtures | OK (6 documented hashes, 5 tracked .mpp) |
| app_qt ctest WITHOUT the Qt bin dir on PATH | 17/17 - the A-10 fix holds |
| app/ build | first build hit a transient file lock (MSB3061 on a .tlog, Dropbox sync); clean rebuild exit 0 |
| app/ ctest | 15/15 |

CI: origin/wave23 63cd087 - qt-shell 39945768811 GREEN x3; ci 39945764244 RED on
ubuntu-gcc only (the wave-17 nested comment, fixed locally in f40111f, not yet run
in CI because the push is held).  Final-sha CI: NOT RUN.
Live same-file comparison: not repeated by the verifier (desktop control off).
