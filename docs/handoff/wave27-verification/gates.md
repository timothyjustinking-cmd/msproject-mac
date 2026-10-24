# Wave-27 verification gates (Opus, fresh trees, 2026-10-24 16:28-16:50)

Tip verified: wave27 = 8981356 (pushed).  CI on 9323db2: qt-shell, ci, app-macos all green.

| gate | result |
|---|---|
| core / qt build | first run hit transient MSB6003 locks on link .tlog files (build/ is Dropbox-ignored, so a scanner such as Defender is the likely holder); incremental rebuild exit 0, 0 warnings |
| core ctest -E plan_recalc | 51/51 (before and after the rebuild) |
| plan_recalc_tests MPP_NO_CORPUS=1 | 266 checks, 0 failures |
| engine_diff_tests self-test | 265 checks, 0 failures |
| engine --suite | 7 pass / 1 fail (constraint-mso), 0 unrecorded |
| M2 recalc --compare | 332/332 |
| check_fixtures | OK (46 documented hashes, 26 tracked .mpp); oracle provenance OK (8) |
| app_qt ctest | 23/23 |
| app/ ctest | 17/17 |

Independent review (review.md): Critical 0, High 2 (seven fixtures embed the local save
path with the operator's name and company folder; an outline-number test that probably
pins a reader defect), Medium 5, Low 4.  Not merged into main: the merge condition was
green CI AND no Critical/High.  The local pre-push guard now also refuses embedded paths
in binary fixtures (seen to refuse wave 27's five).
