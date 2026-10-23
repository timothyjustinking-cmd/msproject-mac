# Wave-25 verification gates (Opus, fresh trees, 2026-10-24 03:37-03:49)

Tip verified: local wave25 = 3b4da5f (not pushed; publication hold).
Trees: build/vs-verify25, build/vs-verify25-qt, build/vs-verify25-app (Release, /WX).
No file locks this run.

| gate | result |
|---|---|
| core build / qt build / app build | exit 0, 0 warnings each |
| core ctest -E plan_recalc | 51/51 |
| plan_recalc_tests MPP_NO_CORPUS=1 | 266 checks, 0 failures |
| engine_diff_tests self-test | 265 checks, 0 failures |
| engine --suite | 7 pass / 1 fail (constraint-mso), 0 unrecorded |
| M2 recalc --compare | 332/332 |
| check_fixtures | OK (23 documented hashes, 21 tracked .mpp); oracle provenance OK (8) |
| app_qt ctest (no Qt PATH workaround) | 18/18 |
| app/ ctest | 15/15 |
| client-name scan (live names) on the tip | 0 files / 0 hits |
| corpus-string scan (every UTF-16 string in the client files, incl. deleted rows) | tip: only generic Project wording, dates and stream names; no client content |

CI: not run (publication hold; DeepSeek stopped with BLOCKED as the brief requires).
