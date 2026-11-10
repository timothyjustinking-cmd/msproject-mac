# Wave 35f CLOSE, and its merge into main: coordinator verification (Tue 10 to Wed 11 Nov 2026)

Wave 35f is `47665e1..9c2fb1c`, 26 commits:
- 1 coordinator commit: `d884b57`, the brief;
- 25 builder commits.

The CLOSE commit `a20f493` (`WAVE-35f-COMPLETE`) is docs only.  So is the builder's record
commit after it, `9c2fb1c`, which updates START-HERE.  The driver ran from 21:03 to 23:43 on
10 November 2026, below normal priority while the trace VM ran, and exited.

## The groups

| group | gated commit | gates | mutations | review |
|---|---|---|---|---|
| G0 | `79d24be` | green, 0 warnings (65 / 52 / 34) | 2/2 red | 0C / 0H / 1M / 8L |
| G1 | `135d253` | green, 0 warnings (65 / 52 / 34) | 5/5 red in at least one lane | 0C / 0H / 1M / 4L |

The details are in `coord-gates-g0.md`, `coord-gates-g1.md`, `review-g0.md` and `review-g1.md`,
in this folder.

## Range scans (`47665e1..9c2fb1c`)

- `guard_names`: 0.
- `scan_client_names`: 0 files, 0 name hits.
- `check_markers`: 0.
- `.mpp` blobs added: 0.
- Every builder commit carries `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35f)`.

## CI (pushed `wave35f`, new branch, no force; every run at `9c2fb1c`)

| run | job | result |
|---|---|---|
| ci 42818309656 | windows-msvc | success |
| | ubuntu-gcc | success |
| | ubuntu-asan | success |
| qt-shell 42818314895 | linux-qt | success |
| | windows-qt | success |
| | macos-qt | success |
| app-macos 42818310625 | macos-appkit | success (the only compile of `app/main.mm`, item 1.10) |

## 4(e)

The port saves came from the verify worktree's app test output at `135d253`, whose code equals
CLOSE.  `app` ctest was re-run first, 34/34.

**9 pairs pass, with 0 fields differing between the port's reopen and Project's.**  Sort Name
ascending and Bold are vacuous, as before.  The whole-wave review also confirms that no writer's
output bytes changed: the only `core/mppwrite` change is a test seam that does nothing at its
default.

## Whole-wave review (`review-wave.md`)

0 Critical, 0 High, 0 Medium, 3 Low.

It found no route, in either shell, where File > Save writes something other than what is
shown:
- every path that commits an edit ends the structural redo list before Redo could re-queue an
  exclusive edit;
- a Show Level reaches the redo list only from the file's order;
- every remaining exclusive-kind clash ends in a tested save-time refusal.

`app/main.mm` reads clean for AppleClang `-Werror`, and CI confirms it.

The Lows:
- R35fW-1: stale "in progress" lines in `memory/coordinator.md` and the plan.
- R35fW-2: one 1.9 record sentence.
- R35fW-3: the partial-apply status omits a dropped Percent or custom-field change.

## The merge

The merge rule holds:
- CI is green on three OSs;
- no Critical or High is open;
- 4(e) passes.

`main` (`47665e1`) is an ancestor of `wave35f`, so `main` is fast-forwarded to this commit.

## Open after the merge (no Critical or High)

- **Mediums:**
  - R35fG0-1: a record sentence about bypass (b) on the five-task file.
  - R35fG1-1: Custom Fields... cannot be opened beside any queued exclusive edit.
- **Lows:** R35fG0-2 to -9, R35fG1-2 to -5, R35fW-1 to -3.
- **Records:** two rows read "fixed" that a later review reopened in part: R35eG1-3 (see
  R35fG0-1) and R35eW-1 (see R35fG1-1).
