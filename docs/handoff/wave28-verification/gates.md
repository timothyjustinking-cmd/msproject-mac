# Wave-28 verification gates (Opus, fresh trees, 2026-10-25 00:53-01:09)

Tip verified: wave28 = 69a4aac (not pushed: the pre-push guard refuses the
intermediate `tests/data/synthetic-calendars.mpp` blob added in 076438f, which holds
the operator's name; d1b5a8a cleaned the tip).  No CI run exists for wave 28.

| gate | result |
|---|---|
| core / qt / app build (fresh trees `build/vs-verify28*`, warnings as errors) | exit 0, no MSB6003 locks this run |
| core ctest -E plan_recalc | 52/52 |
| plan_recalc_tests MPP_NO_CORPUS=1 | 266 checks passed |
| engine_diff_tests self-test | 265 checks, 0 failures |
| engine --suite | 7 pass / 1 fail (constraint-mso, the known expected failure), 0 unrecorded |
| M2 recalc --compare | 332/332 (268/268 and 64/64) |
| check_fixtures | OK (72 documented hashes, 52 tracked .mpp); paths OK (94 files); oracle provenance OK (8) |
| app_qt ctest | 24/24 |
| app/ ctest | 20/20 |

Independent review (review.md): Critical 0, High 4, Medium 3, Low 5.  The Highs are
all standing rule 1: the save-pair tests compare unwritten streams with the control
rather than with Project's file (R28-1); the percent writer, Mark on Track / Update
Project and Indent/Outdent are enabled for row shapes no Project pair measured
(R28-2, R28-3, R28-4).  They are wave 29's G0.  Not merged into main: no CI, and the
review has High findings.
