# Wave 35b G2 (the Calculate Project and assignment Mediums): coordinator verification (Mon 9 Nov 2026)

The gated commit is `733d6d3` (`WAVE-35b-G2-COMPLETE`).  It covers items 2.1 to 2.12
(`a69943c`, `4a491df`, `38a6c92`, `eb5aa96`, `5284d58`, `e860427`, `93b9524`, `4315ef7`,
`b304545`, `06eff3f`, `96d1cd4`, `b179918`).  Gates ran in the verify worktree, detached at
`733d6d3`, with fresh build trees, Release, `CORE_WARNINGS_AS_ERRORS=ON` and the main corpus
junctioned in read-only, from 12:42 to 13:17.  Logs are in `build/verify-w35b/g2/` (git-ignored).
The commit ids are the builder's; `wave35b` is rebuilt onto the new `wave35` after CLOSE
(`gates-g0.md`).

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 65/65 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; expected failing set reached (`constraint-mso`) |
| M2 `recalc --compare` | exit 0 |
| app_qt | 0 warnings; **51/52 on the first run**, 52/52 on three full reruns (below) |
| app | 34/34, 0 warnings |
| fixtures; markers | OK; OK |

**The one Qt failure.**
- On the first run after the fresh build, `qt_assign_resources_gate` failed.
- Its only output was "unknown error".
  - The gate's own "N failure(s)" summary never printed, so no gate check reported a failure.
  - The text matches no string in the gate or in `app_qt`.
- It then passed:
  - alone under ctest;
  - in three full-suite reruns (52/52 each);
  - in 40 direct runs (`g2/42-qt-assign-stress.log`).
- The hypothesis, not traced, is that the freshly built program failed to start.
- It is recorded as **R35bG2-9 (Low)**. It is the same test as the macOS CI failure CI-4, so
  the `wave35b` CI run is read with this in mind.

**Not compiled here:** G1 item 1.7's `app/main.mm`.  CI's macOS jobs are its only compile gate.

## Fixtures, names and trailers

- G2 adds no `.mpp` blob (`e3c86fe..733d6d3`: 0 blobs).
- `guard_names.py` on the range exits 0, and `scan_client_names.py` finds 0 hits at `733d6d3`.
- `check_markers` on the range: 0 hits.
- The builder's own `guard_names` exit 1 (quoted in `06eff3f`) is the pre-rebuild G3 fixtures
  this branch still inherits from the old `ec326b6`.  The rebuild after CLOSE removes them.
- **Trailers:** 11 of the 13 builder commits carry only
  `Co-Authored-By: Claude Sonnet 5.5`, without the wave's
  `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35b)`.  The model is named correctly;
  the standing form is missing.  This is R35bG2-4 (Low).
- The completion file again holds the marker line only (the G0 process defect).

## Mutations (`build/verify-w35b/mutate-g2.py`, `g2/mutations.log`)

27 guards were switched off with a non-constant false (or a constant changed), one at a time.
Each guard's lanes were rebuilt and run by exact ctest name, then the file was restored.  The
worktree was clean afterwards, and all six lanes were green again.

**Every guard is red in its own lane.**

| id | guard switched off | own lane | other lanes |
|---|---|---|---|
| W3 | 2.1: `0x02400003` checked before any write | red | |
| W4 | W3 plus the appends written to the target (not atomic) | red | |
| W7, W8 | 2.2: late finish = project finish; the critical leaf's slack 0 and late start | red | app green (the digest, A4, stops the stale request first) |
| A4 | 2.2: the save-time source digest | red | |
| W2 | 2.3: the project row already holds an overtime record | red | app red; Qt green (no Qt case) |
| W9 to W16, W5 | 2.4: row 0 uid, FixedMeta 0x48/0xEF, missing `0x02400024`, "already equals", start anchor, late before early, late finish after project finish, missing `0x02400002`, row count | red, each | |
| W18 | 2.5: the writer's constraint (SNET) check | red | |
| A1, A6 | 2.5: the app link gate; the app constraint gate | red | Qt red (A1) |
| W17 | 2.6: leaf stamp 0x30 changed to 0x28 | red | app red, **Qt red** (the Qt gate is no longer vacuous) |
| W1, W6 | 2.7: equal leaf finishes; the critical leaf not the first leaf row | red | app red |
| A2, A3 | 2.9: the default calendar is Standard; the default week is the measured week | red | Qt green (A2: no Qt case) |
| R1 | 2.10: the reader's `calendar_base_extra_blocks` forced to 0 | red | |
| G1, G2 | 2.11: the resources already assigned are measured; the copied row's dates are the task's | red | app green (the edit model refuses every add to an assigned task first) |
| G3 | 2.12: the target resource's session byte 0x1C | red | app **red** (pinned in `app_edits` now); Qt green (generic prefix) |

- Each green in another lane is the earlier layer named in the table, by design.
- One case was not planted alone, because reading shows it cannot go red: a scratch-file
  regression with W3 still in place.  Both tests are refused before any write, so it would
  stay green.  That is R35bG2-2.

## Independent review (`review-g2.md`)

A read-only agent reviewed `e3c86fe..733d6d3`: 0 Critical, **0 High**, 1 Medium, 7 Low.  All
twelve items are closed for the shape the brief names.

- **R35bG2-1 (Medium):**
  - Item 2.7's tie and swapped-order refusals exist only in the writer's
    `read_calculate_shape`.
  - The edit model reaches them only through `make_calculate_edit`, and
    `calculate_plan_refusal` still accepts a tie.
  - Item 2.3 is the same single shared guard.  This is the R35bG1-3 pattern.
  - The 2.2 digest refuses a hand-built edit at save, so no file is mis-written.
- **Lows:**
  - R35bG2-2: the scratch-file chain is untested, and a wrong-kind `0x02400024` is still
    refused only after the appends.  The target stays safe.
  - R35bG2-3: the 2.9 refusal drops the decision-9 text, and normalises the calendar name
    differently from the writer.
  - R35bG2-4: the trailers.
  - R35bG2-5: the writer still trusts the non-critical leaf's slack and late start, which it
    could recompute.
  - R35bG2-6: the old changed-file app test stops at the digest with no needle, so the
    cascade's save-time `validate_calculate` is reached by no test.
  - R35bG2-7: `save_calculate` deletes a pre-existing `<target>.calculate.tmp`.
  - R35bG2-8 (suspected): the viewstate README rows may carry the same 11-hour date shift as
    item 2.8 corrected.
- **Clean:**
  - 2.1: no path writes the target and then fails;
  - 2.2: the digest is over the whole file and compared with a fresh read;
  - 2.3: refused at command time with its own message;
  - every 2.4, 2.5 and 2.12 needle is unique to its guard;
  - 2.6: compared stream by stream with Project's pair;
  - no measured shape is lost to 2.7, 2.9 or 2.11;
  - 2.8: a record change only, with no COM and no `corpus/` change;
  - hygiene; no new undefined behaviour.

## Merge rule

G2 adds no new High.  Open:
- R35bG0-1 and -2 (Calculate Project, the next fix wave);
- the main.mm compile and CI-4 on macOS CI;
- the 4(e) Project reopen check.

R35bG2-1 and the Lows join the next fix wave's queue, beside R35bG1-1 to -3.
