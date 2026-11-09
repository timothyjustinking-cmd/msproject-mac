# Wave 35e CLOSE, and the merge of waves 34 to 35e: coordinator verification (Tue 10 Nov 2026)

Wave 35e is `2ea87e5..c0c81e5`, 23 commits:
- 1 coordinator commit: `43fee2a`, the brief and a status update;
- 22 builder commits for G0 to CLOSE.

The CLOSE commit `c0c81e5` (`WAVE-35e-COMPLETE`) changes docs only.  The driver ran from 05:31 to
07:19 on 10 November 2026 and exited.

## The groups

| group | gated commit | gates | mutations | review |
|---|---|---|---|---|
| G0 | `5d733ae` | green, 0 warnings (65 / 52 / 34) | 2/2 red in all 3 lanes | 0C / 0H / 0M / 3L |
| G1 | `07c4ef6` | green, 0 warnings (65 / 52 / 34) | 2/2 red in their lane | 0C / 0H / 3M / 3L |
| G2 | `2dbb5d9` | green, 0 warnings (65 / 52 / 34) | 4/4 red in their lane | 0C / 0H / 1M / 9L |

G0 closed R35dG0-1, the last open High, together with R35dW-1.  The details are in
`coord-gates-g0.md` to `-g2.md` and `review-g0.md` to `-g2.md`, all in this folder.

## Range scans (`2ea87e5..c0c81e5`)

- `guard_names`: 0.
- `scan_client_names`: 0 files, 0 name hits.
- `check_markers`: 0.
- `.mpp` blobs added: 0.
- Every builder commit carries `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35e)`.

## CI (pushed `wave35e` at `c0c81e5`, new branch, no force)

| run | job | result |
|---|---|---|
| ci 42692005590 | windows-msvc | success |
| | ubuntu-gcc | success |
| | ubuntu-asan | success |
| qt-shell 42692012108 | linux-qt | success |
| | windows-qt | success |
| | macos-qt | success |
| app-macos 42692007216 | macos-appkit | success (compiles `app/main.mm`, item 2.7) |

Wave 35d's CI (`3d22c39`) was also green on all seven jobs, on its third attempt after GitHub's
Actions incident.  That green run is what proved R35cW-CI1 and R35cW-CI2.

## 4(e): Project's reopen of the enabled saves

- **Inputs:** the port saves from the verify worktree's app test output at `2dbb5d9`, whose code
  equals `c0c81e5`.  `app` ctest was re-run first, 34/34.
- **Result: 9 pairs pass, with 0 fields differing between the port's reopen and Project's.**
  - The 9: Calculate Project under manual calculation, Sort x2, Show Level x3, and the
    assignment add and remove.
  - **Vacuous:** Sort Name ascending (by design) and Bold (not readable through COM).
- The same holds at 35b, 35c and 35d.
- The only `core/mppwrite` changes in 35e are two test seams, which do nothing at their
  defaults.

## Whole-wave review (`review-wave.md`)

0 Critical, 0 High, 0 Medium, 4 Low.  It found no route where File > Save writes something other
than what is shown, or an edit `push()` or the builder would refuse.
- **Every commit path clears the structural redo list:** the cell gate, Cut, Paste, Fill Down,
  Task Information and the Task Form (partial apply included), and AppKit's cell writers.
- **After a Redo,** the rebuild re-applies the queued level or sort.
- **Every remaining exclusive-kind clash** ends in a tested save-time refusal.

The Lows:
- R35eW-1: Calculate alone refuses a format or formula edit early; the other kinds rely on the
  save refusing.
- R35eW-2: one CLOSE gate note.
- R35eW-3: the Qt call site of 2.1's mapping cannot be reached.
- R35eW-4: 2.7's gate is `macos-appkit`, not `macos-qt`.

## The merge

The merge rule holds for waves 34, 34b, 35, 35b, 35c, 35d and 35e:
- CI is green on all three OSs at `c0c81e5`;
- no Critical or High is open;
- 4(e) passes for every enabled save.

`main` (`4fd9b90`) is an ancestor of `wave35e`, so `main` is fast-forwarded to this commit, as
waves 33 and 33b were.  Before the push, `guard_names` was run over the whole merged range
`4fd9b90..` this commit.

## Open after the merge, for a later wave (no Critical or High)

- **Mediums:**
  - R35eG1-1: a session-only Level is refused under a session Sort, with a false message.
  - R35eG1-2: four partly fixed 35b ids are missing from START-HERE and the plan.
  - R35eG1-3: the 1.1 record is wrong in two places.
  - R35eG2-1: the Custom Fields dialog's Formula button has no Calculate guard.
- **Lows:** R35eG0-1 to -3, R35eG1-4 to -6, R35eG2-2 to -10, R35eW-1 to -4.
- **Records:** START-HERE, `memory/coordinator.md` and `plan-waves27-30.md` still say R35dG0-1
  stays open until the coordinator's review.  That review closed it.
