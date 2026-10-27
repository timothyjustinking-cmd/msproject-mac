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

**Merged into main after a history rewrite (operator-approved, 2026-10-27).**  The
merge rule was met (CI green on all three OSs, no Critical/High), but the pre-push
guard refused main: the range fcb1c10..wave32 held the original blobs of seven
fixtures (`wave23-flags`, `wave23-outline` and five `wave27-*`) with the operator's
save path or name embedded.  Every commit in the range was rewritten with
`git filter-branch --index-filter`, swapping each flagged blob for its cleaned
version: the six W28-0.1 re-saves, and for `wave23-outline` (which W28-0.1 had not
cleaned) an in-place, same-length neutral replacement of the author name (3
occurrences; size and every offset unchanged).  Commit messages, authors and dates are
unchanged; `rewrite-sha-map.txt` maps the 329 old commit ids to the new ones, so the
ids quoted in the wave 23-32 records (CI runs included) are the old ones.  The tip tree
differs from the verified tip only in `wave23-outline.mpp` and its documented hash; with
the neutralised fixture the verifier re-ran core 52/52, qt 37/37, app 25/25 and the
engine suite (7 pass, constraint-mso the known failure) before the rewrite, and CI runs
again on main.  The old wave23..wave32 branches were deleted on GitHub.  Still on main
from before wave 23: `synthetic-base.mpp` carries the same author-name marker (not in
the guarded range; left for the operator's history decision).  The Medium/Low findings
are queued in `plan-waves27-30.md` ("Queued from the wave-32 review").
