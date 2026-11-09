# Wave 35e G1 (the Mediums): coordinator verification (Tue 10 Nov 2026)

The gated commit is `07c4ef6` (`WAVE-35e-G1-COMPLETE`), the end of items 1.1 to 1.4.  That is
5 builder commits, `39c8e37` to `07c4ef6`, on `wave35e` from `5d733ae`.  The builder's own
record is `gates-g1.md`.

Gates ran in the verify worktree from 06:01 to 06:23, with:
- the worktree detached at `07c4ef6`;
- fresh build trees;
- Release, with `CORE_WARNINGS_AS_ERRORS=ON`;
- the corpus junctioned in read-only.

Logs are in `build/verify-w35e/g1/` (git-ignored).

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 65/65 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; exit 0 |
| M2 `recalc --compare` | exit 0 |
| app_qt | 52/52, 0 warnings |
| app | 34/34, 0 warnings |
| fixtures; markers | OK; OK |

## Mutations (`build/verify-w35e/mutate-g1.py`, `g1/mutations.log`)

2 mutations.  **Both are red in their own lane, `qt_outline_persist`, where both tests live.**
The worktree was clean afterwards, and every lane was green again.

| id | guard switched off | outline persist | sort persist |
|---|---|---|---|
| B1 | 1.1(b): a sort choice ends a Show Level on the redo list | red (4) | green |
| B2 | 1.1(a): a session Level refused while a session Sort is shown | red (4) | green |

## Independent review (`review-g1.md`)

A read-only agent reviewed `5d733ae..07c4ef6`: 0 Critical, 0 High, **3 Medium**, 3 Low.

- **R35eG1-1 (Medium):** 1.1(a)'s refusal runs before the queue decision.
  - With a session Sort shown, it now refuses Levels that would only be session-only: on a
    read-only file, Level 3 to 9, or a file the builder refuses.
  - Wave 31 allowed these, and none of them risks a bad save.  The message, "File > Save would
    write the file's own order", is false for them.
  - **Fix:** apply the check only on the queue branch.
- **R35eG1-2 (Medium):** R35dG1-3 is only partly fixed.  START-HERE and `plan-waves27-30.md`
  still leave out the four 35b ids that 35c only partly fixed (R35bW-1, R35bW-2, R35bG2-2,
  R35bG3-1).  Only `memory/coordinator.md` lists them.
- **R35eG1-3 (Medium):** the 1.1 builder record is wrong in two places.
  - Bypass (a) could happen on the five-task file: queue a Sort, press Ctrl+Z, and the sort
    stays shown with nothing queued.  No test drives that route, though `sort_active()` now
    closes it.
  - The record's "Found, not fixed" item describes a route that 1.1(a) already refuses.
- **Lows:**
  - R35eG1-4: the cleared-sort paths drop a Redo they need not.  The two non-cleared clears are
    redundant with each other.
  - R35eG1-5: R35cW-4's remainder leaves out R35dW-5 (AppKit).  Some records say "closed"
    before the coordinator's review.
  - R35eG1-6: one loose `calculate.md` sentence.

What the reviewer confirmed:
- Both bypasses are closed and tested.  `apply_outline_level` is the only path that queues a
  Show Level.
- The `calculate.md` paragraph matches the code.
- The five 35c ids now read "partly fixed", and the CI ids read "fixed in source; CI is its
  gate".  The trailer is on all 5 commits.

## Merge rule

G1 adds no Critical or High.  **No Critical or High is open.**
