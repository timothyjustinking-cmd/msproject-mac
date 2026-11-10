# Wave 35f G0 (the Mediums): coordinator verification (Tue 10 Nov 2026)

The gated commit is `79d24be` (`WAVE-35f-G0-COMPLETE`), the end of items 0.1 to 0.4.  That is
5 builder commits, `fd457b4` to `79d24be`, on `wave35f` from `d884b57`.  The builder's own
record is `gates-g0.md`.

Gates ran in the verify worktree from 21:53 to 22:17, below normal priority because the trace
VM had priority.  The run used:
- the worktree detached at `79d24be`;
- fresh build trees;
- Release, with `CORE_WARNINGS_AS_ERRORS=ON`;
- the corpus junctioned in read-only.

An earlier launch through `cmd start /BELOWNORMAL` hung on Git Bash's quote escaping before
building anything.  It was stopped and relaunched through `run-low.ps1`.

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 65/65 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; exit 0 |
| M2 `recalc --compare` | exit 0 |
| app_qt | 52/52, 0 warnings |
| app | 34/34, 0 warnings |
| fixtures; markers | OK; OK |

## Mutations (`build/verify-w35f/mutate-g0.py`, `g0/mutations.log`)

2 mutations.  **Both are red in their lanes.**  The worktree was clean afterwards, and every
lane was green again.

| id | guard switched off | result |
|---|---|---|
| X1 | 0.2: Custom Fields refused beside a queued Calculate | Qt calculate gate red (3) |
| X2 | 0.1: a Level under a shown session Sort is not queued | outline persist red (3); sort persist red (8) |

## Independent review (`review-g0.md`)

A read-only agent reviewed `d884b57..79d24be`: 0 Critical, 0 High, **1 Medium**, 8 Low.

**What holds:**
- **0.1:** no route where File > Save writes a Level the display does not show.  The new check
  sits after the read-only and builder-failure branches, so only a Level that would be pushed
  is held back.
- **0.2:** every Qt formula and format surface is now guarded.
- **0.3:** reaches its guard.
- **0.4:** the open-queue lists match `wave35e-verification/coord-gates-close.md`.

**R35fG0-1 (Medium):** `wave35e-verification/gates-g1.md` still says bypass (b) could not be
reached on the five-task file.
- It could.  A Start sort stays session-only there.
- The (b) tests run on the six-task outline only.
- **Fix:** correct the sentence, and add a five-task (b) case or state the coverage.

**Lows:**
- R35fG0-2: stale "stays open" lines in the 35c, 35d and 35e records.
- R35fG0-3: the G0 record says the 0.1 edit is "not built".
- R35fG0-4: clearing the sort leaves a session-only Level showing, with only "sort cleared"
  said.
- R35fG0-5: the read-only case is untested.
- R35fG0-6 to -9: needles and hygiene.

## Merge rule

G0 adds no Critical or High.  **No Critical or High is open.**
