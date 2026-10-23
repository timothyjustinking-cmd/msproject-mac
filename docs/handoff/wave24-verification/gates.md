# Wave-24 verification gates (Opus, fresh trees, 2026-10-24 00:50-01:15)

Tip verified: local wave24 = 2561a51 (not pushed; publication hold).
Trees: build/vs-verify24, build/vs-verify24-qt, build/vs-verify24-app (Release, /WX).

| gate | result |
|---|---|
| core build | first build hit a transient file lock (MSB6003 on a link .tlog; Dropbox sync of build/); clean rebuild exit 0, 0 warnings |
| core ctest -E plan_recalc | 51/51 (before and after the rebuild) |
| plan_recalc_tests MPP_NO_CORPUS=1 | 266 checks, 0 failures |
| engine_diff_tests self-test | 265 checks, 0 failures |
| engine --suite | 7 pass / 1 fail (constraint-mso), 0 unrecorded |
| M2 recalc --compare | 332/332 |
| check_fixtures | OK (13 documented hashes, 12 tracked .mpp) |
| app_qt ctest (no Qt PATH workaround) | 18/18 |
| app/ ctest | 15/15 |
| client-name scan of the tip (watcher, after ff22c7b) | 0 files / 0 hits |

CI: not run (publication hold; DeepSeek stopped with BLOCKED as the brief requires).
Live same-file comparison: not repeated by the verifier (desktop control off).
Note: two transient MSBuild file locks in two nights, both on .tlog files under
build/ inside the Dropbox folder.  Excluding build/ from Dropbox sync (or building
outside Dropbox) would remove them.
