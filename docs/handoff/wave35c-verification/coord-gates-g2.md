# Wave 35c G2 (the Lows): coordinator verification (Tue 10 Nov 2026)

The gated commit is `319d187` (`WAVE-35c-G2-COMPLETE`), the end of items 2.1 to 2.28.  That is
28 builder commits, `4e957ae` to `319d187`, on `wave35c` from `ea54226`.  The builder's own
record is `gates-g2.md`.

Gates ran in the verify worktree from 01:45 to 02:07, with:
- the worktree detached at `319d187`;
- fresh build trees;
- Release, with `CORE_WARNINGS_AS_ERRORS=ON`;
- the corpus junctioned in read-only.

Logs are in `build/verify-w35c/g2/` (git-ignored).

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 65/65 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; exit 0 |
| M2 `recalc --compare` | exit 0 |
| app_qt | 52/52, 0 warnings |
| app | 34/34, 0 warnings |
| fixtures; markers | OK; OK |

The scans are taken from the builder's gate line for now.  The reviewer confirmed the trailer on
all 28 commits.  The coordinator scans run on the whole range at CLOSE.

## Mutations (`build/verify-w35c/mutate-g2.py`, `g2/mutations.log`, `g2/mutations-M11.log`)

11 mutations, one at a time, all anchors count 1.  **All 11 are red in their own lane.**  The
worktree was clean afterwards, and every lane was green again.

| id | guard switched off | own lane | other lane |
|---|---|---|---|
| M1 | 2.3 writer: the project summary task's deadline | core red | |
| M2 | 2.3 writer: the project summary task's constraint | core red | |
| M3 | 2.2 writer: the second date copies (+104/+108) equal to Start and Finish | core red | |
| M4 | 2.2 writer: remaining duration equal to duration | core red | |
| M5 | 2.2 shared helper: the FixedMeta byte 9 check | core red | app green |
| M6 | 2.2 shared helper: FixedMeta bytes 8 to 11 skipped again | core red | app green |
| M7 | 2.4 writer: the existing scratch path refusal | core red | |
| M8 | plan: a task row the reader did not build (`type_block_known`) | app red | |
| M9 | view block: the second text style refused unless measured | core red | app green |
| M10 | outline helper: the second style always allowed | core red | app green |
| M11 | task mode: the caller's pair name substituted into refusals | core red | |

- **M11 needed a rerun.**  Its first form left `pair` unused, and warnings-as-errors failed the
  build (C4100), which is not evidence.  With `(void)pair;` added, it is red with 5 needle
  failures (`mutations-M11.log`).
- **The app-lane greens (M5, M6, M9, M10)** come from the plan layer sharing the core helper.  No
  app test pins FixedMeta bytes 8 to 11 or byte 9, or drives the second text style through the
  builder.  The writer's tests guard both today.  Should the plan layer ever stop calling the
  shared helper, nothing would catch it; this is the same gap as R35cG2-4.
- **M7 and R35cG2-1 agree.**  With the 2.7 existence check switched off, the squat test reaches
  the scratch chain and fails there ("is a directory").  With the check on, no test reaches the
  chain after the first append, which is the reviewer's point.

## Independent review (`review-g2.md`)

A read-only agent reviewed `ea54226..319d187`: 0 Critical, 0 High, **1 Medium**, 7 Low.

**R35cG2-1 (Medium):** since 2.7, no test reaches the `save_calculate` scratch chain after its
first append, so R35bG2-2 is open again.
- `gates-g2.md`, `wave35c-complete.md` and the test comment at
  `tests/roundtrip_calculate.cpp:1163` all say it is closed.
- **Fix:** a test-only fault seam that throws after the first append, then assert the existing
  target is byte-identical.  Correct the comment and the two record rows.

**Lows:**
- R35cG2-2: the 2.3 row-0 constraint refusal runs before the Props pin.  33 of 332 fixtures
  store 1 there without being constraint pairs, so the refusal can name a constraint nobody set.
- R35cG2-3: 2.9 reads Fixed2Meta at the FixedMeta entry's index without checking that entry's row
  word, so a message's uid can be wrong.  Accept and refuse are unaffected.
- R35cG2-4: 2.12 pins the second text style by outline shape alone, an inference the spec does
  not mark.  No app test drives the sort-flat file.
- R35cG2-5: `require_measured_view_block`'s new parameter defaults to accepting the second style.
  No caller relies on the default, so it can be dropped.
- R35cG2-6: when `symlink_status` fails for another reason, the 2.7 refusal still says "already
  exists".  That branch is untested.
- R35cG2-7: a fixture-count note is wrong (36 and 15, not 24), though the 2.22 README rows are
  right.
- R35cG2-8: START-HERE, `memory/coordinator.md` and `plan-waves27-30.md` list ids as open that
  G2 closed.  The two records also disagree on 2.6 ("fixed" against "partly fixed").

The other items (2.1 to 2.3, 2.5, 2.6, 2.8, 2.10 to 2.28) close their findings by reading.  The
reviewer found no dangling reference, cleared-vector read or uninitialised state.

## Merge rule

G2 adds no Critical or High.  The blockers are still R35cG0-1, R35cG1-1 and R35cG1-2, and
R35cG2-1 joins the next fix wave's Mediums.
