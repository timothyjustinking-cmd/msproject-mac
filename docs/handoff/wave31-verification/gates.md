# Wave-31 verification gates (Opus, fresh trees, 2026-10-27 08:41-08:57)

Tip verified: wave31 = 27332b3 (pushed; last code commit 072773b).  CI on 072773b,
third dispatch: `ci` ubuntu-gcc + windows-msvc green (run 40404395191); `qt-shell`
windows-qt, linux-qt, macos-qt green (run 40404390641).  The two earlier qt-shell
dispatches were red and fixed in CLOSE (W31-C5, C7).

| gate | result |
|---|---|
| core / qt / app build (fresh trees `build/vs-verify31*`, warnings as errors) | exit 0, no MSB6003 locks |
| core ctest -E plan_recalc | 52/52 |
| plan_recalc_tests MPP_NO_CORPUS=1 | 266 checks passed |
| engine_diff_tests self-test | 265 checks, 0 failures |
| engine --suite | 7 pass / 1 fail (constraint-mso, the known expected failure), 0 unrecorded |
| M2 recalc --compare | 332/332 |
| check_fixtures | OK (139 documented hashes, 173 tracked .mpp); paths OK (222 files); oracle provenance OK (8) |
| app_qt ctest | 37/37 |
| app/ ctest | 25/25 |

Independent review (review.md): Critical 0, High 3, Medium 3, Low 6.  R30-1 and R30-4
closed, R30-3 closed for the shell, R30-2 partly.  Highs: queued edits bypass the
file-wide automatic-outline refusal (R31-1); typed or pasted Start still saves
through the writer that Fill's own Project pair shows cannot match Project
(R31-2); under the new session Sort, Shift-click ranges and multi-row Paste act in
file order, so a paste can write rows other than those shown and save them into
the wrong tasks (R31-3 - the verifier confirmed the code path: the sheet shows
`ordered_display_rows()` but `extend_to` works on core rows; arguably Critical, as
it can put values into the wrong tasks on save).  Not merged into main: CI green on
all three OSs, but the review has Highs.  This is the last wave of the 27-31 run;
nothing further was launched.
