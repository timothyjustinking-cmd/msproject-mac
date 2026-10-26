# Wave-30 verification gates (Opus, fresh trees, 2026-10-26 21:38-21:55)

First wave verified from `D:\dev\msproject-mac` (moved out of Dropbox 26 Oct 2026).
Tip verified: wave30 = 338feaa (pushed; last code commit 0aca9d2).  CI on 0aca9d2,
first dispatch: `ci` ubuntu-gcc + windows-msvc green (run 40307818793); `qt-shell`
windows-qt, linux-qt, macos-qt green (run 40307814439).

| gate | result |
|---|---|
| core / qt / app build (fresh trees `build/vs-verify30*`, warnings as errors) | exit 0, no MSB6003 locks (none since the move) |
| core ctest -E plan_recalc | 52/52 |
| plan_recalc_tests MPP_NO_CORPUS=1 | 266 checks passed |
| engine_diff_tests self-test | 265 checks, 0 failures |
| engine --suite | 7 pass / 1 fail (constraint-mso, the known expected failure), 0 unrecorded |
| M2 recalc --compare | 332/332 |
| check_fixtures | OK (131 documented hashes, 167 tracked .mpp); paths OK (214 files); oracle provenance OK (8) |
| app_qt ctest | 32/32 |
| app/ ctest | 22/22 |

Independent review (review.md): Critical 0, High 4, Medium 4, Low 6.  R29-1
(Critical) is closed; R29-3, R29-4 and R29-7 closed; R29-2, R29-5, R29-6 partly.  New
Highs: a link added after a queued automatic outline edit bypasses the linked-file
refusal (R30-1); the automatic outline save accepts files unlike its pairs (R30-2);
percent/date edits with progress off the edited chain (R30-3); Fill Down enabled on a
Start writer with no committed synthetic Project pair, and it fills summary rows
(R30-4; its evidence file is tracked, but it is a corpus probe).  They are wave 31's
G0.  Not merged into main: CI green on all three OSs, but the review has Highs.
