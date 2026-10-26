# Wave-29 verification gates (Opus, fresh trees, 2026-10-26 11:26-12:05)

Tip verified: wave29 = c3ae1cf (pushed; its last code commit 7e14aaa).  CI on 7e14aaa:
`ci` ubuntu-gcc + windows-msvc green (run 40248193087); `qt-shell` windows-qt,
linux-qt, macos-qt green (run 40248190323).  The first dispatch (40245429060,
40245417658) was red on unused functions/constants and was fixed in CLOSE (W29-C1, C3).

| gate | result |
|---|---|
| core / qt / app build (fresh trees `build/vs-verify29*`, warnings as errors) | qt exit 0; core and app first hit transient MSB6003 `.tlog` locks (7 in core, 1 in app); incremental rebuild of each exit 0, 0 compiler warnings |
| core ctest -E plan_recalc | 52/52 after the rebuild (first run: 51/52, `mppwrite_roundtrip_assignments` Not Run - its exe was the locked link) |
| plan_recalc_tests MPP_NO_CORPUS=1 | 266 checks passed |
| engine_diff_tests self-test | 265 checks, 0 failures |
| engine --suite | 7 pass / 1 fail (constraint-mso, the known expected failure), 0 unrecorded |
| M2 recalc --compare | 332/332 (268/268 and 64/64) |
| check_fixtures | OK (98 documented hashes, 101 tracked .mpp); paths OK (148 files); oracle provenance OK (8) |
| app_qt ctest | 27/27 |
| app/ ctest | 20/20 after the rebuild |

Independent review (review.md): Critical 1, High 6, Medium 4, Low 8.  G0 closed
R28-1's masking for the actuals and outline paths (the null-save mask hides no edit
effect in the 22 enabled pairs) but the enabled shapes are wider than the pairs
measured: indent can delete a real zero-work assignment (R29-1, Critical); linked
files, progressed subtrees, the 0->0 date update, the summary recompute scope, the
contour rule and Add Link's falsified byte rule (R29-2..R29-7, High).  They are wave
30's G0.  Not merged into main: CI is green on all three OSs, but the review has a
Critical and Highs.
