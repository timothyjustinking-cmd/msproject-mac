# Wave 35e G0 (the High): coordinator verification (Tue 10 Nov 2026)

The gated commit is `5d733ae` (`WAVE-35e-G0-COMPLETE`), the end of item 0.1.  That is
2 builder commits, `b8ee455` and `5d733ae`, on `wave35e` from `43fee2a`.  The builder's own
record is `gates-g0.md`.

Gates ran in the verify worktree from 05:38 to 06:00, with:
- the worktree detached at `5d733ae`;
- fresh build trees;
- Release, with `CORE_WARNINGS_AS_ERRORS=ON`;
- the corpus junctioned in read-only.

Logs are in `build/verify-w35e/g0/` (git-ignored).

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 65/65 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; exit 0 |
| M2 `recalc --compare` | exit 0 |
| app_qt | 52/52, 0 warnings |
| app | 34/34, 0 warnings |
| fixtures; markers | OK; OK |

## Mutations (`build/verify-w35e/mutate-g0.py`, `g0/mutations.log`)

2 mutations, each run in 3 lanes.  **Both are red in every lane.**  The worktree was clean
afterwards, and every lane was green again.

| id | change | outline persist | sort persist | calculate gate |
|---|---|---|---|---|
| A1 | 0.1 switched off whole: the partial apply keeps the redo list and does not rebuild | red (4) | red (4) | red (4) |
| A2 | 0.1 rebuilds but keeps the redo list | red (4) | red (3) | red (3) |

R35dG0-1 is closed for Sort and Show Level, and R35dW-1 for Calculate Project.  Each has a test
that only the new block satisfies.

## Independent review (`review-g0.md`)

A read-only agent reviewed `43fee2a..5d733ae`: 0 Critical, 0 High, 0 Medium, 3 Low.

The fix holds:
- **The route is closed for all three kinds and both entries.**  The Task Information dialog and
  the Task Form share `apply_task_information`.  A committed field after the failing one is
  covered, and the custom-field `trial` is dropped on failure.
- **`applied` is exact.**  `outcome.changed` is false only when the stored value is unchanged.
- **The sweep is right.**  Cut, Paste and Fill Down validate every cell before writing any.  The
  AppKit shell has no multi-field apply.

**Lows:**
- **R35eG0-1 (older than this wave):** under the hours or days duration preference, the
  untouched-field skip compares the sheet's text with the preference rendering.  A Name-only edit
  then re-commits a rounded Duration, so 3 h becomes 3 h 2.4 min.  It shows in the session view
  only, not in Save.  It is also the only way the Task Form reaches a partial apply.
- R35eG0-2: the tests do not cover the Task Form entry, the `trial` variant, or that the error
  message is kept.
- R35eG0-3: after a partial apply, the status names only the failing field.

## Merge rule

**No Critical or High is open after G0.**  R35dG0-1 was the last High.  CI on three OSs is still
outstanding: GitHub's Actions incident stopped the wave 35d runs.
