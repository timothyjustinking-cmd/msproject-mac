# Wave-32 verification gates (Opus, fresh trees, 2026-10-27 16:43-16:59)

Tip verified: wave32 = 92baa32 (pushed; last code commit 7727760, and 92baa32 changes
docs only).  CI on 7727760, first dispatch: `ci` ubuntu-gcc + windows-msvc green
(run 40456051113); `qt-shell` windows-qt, linux-qt, macos-qt green (run 40456047971).

| gate | result |
|---|---|
| core / qt / app build (fresh trees `build/vs-verify32*`, warnings as errors) | exit 0, no MSB6003 locks |
| core ctest -E plan_recalc | 52/52 |
| plan_recalc_tests MPP_NO_CORPUS=1 | 266 checks passed |
| engine_diff_tests self-test | 265 checks, 0 failures |
| engine --suite | 7 pass / 1 fail (constraint-mso, the known expected failure), 0 unrecorded |
| M2 recalc --compare | 332/332 |
| check_fixtures | OK (139 documented hashes, 173 tracked .mpp); paths OK (222 files); oracle provenance OK (8) |
| app_qt ctest | 37/37 |
| app/ ctest | 25/25 |

Independent review (review.md): Critical 0, High 0, Medium 2, Low 5.  R31-1 and R31-2
closed in both shells; R31-3 closed in the Qt shell (the macOS shell's filter still acts
in file order, R32-2 Medium, but Start is now the only sheet edit either shell saves
and it is read-only, which the verifier confirmed: `collect_start_edits` is the only
sheet-edit collector); R31-4 closed for sort.  The reviewer's own fresh app and app_qt
builds also passed (25/25, 37/37).

**Merged into main** (fast-forward, fcb1c10 -> wave32): CI green on all three OSs and
the review has no Critical/High.  This brings waves 27-32 onto main.  The Medium/Low
findings are queued in `plan-waves27-30.md` ("Queued from the wave-32 review").
