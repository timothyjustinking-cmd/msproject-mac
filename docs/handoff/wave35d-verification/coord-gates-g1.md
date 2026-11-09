# Wave 35d G1 (the Mediums): coordinator verification (Tue 10 Nov 2026)

The gated commit is `2ebcfd8` (`WAVE-35d-G1-COMPLETE`), the end of items 1.1 to 1.6.  That is
7 builder commits, `e967858` to `2ebcfd8`, on `wave35d` from `86dbee9`.  The builder's own
record is `gates-g1.md`.

Gates ran in the verify worktree from 03:27 to 03:48, with:
- the worktree detached at `2ebcfd8`;
- fresh build trees;
- Release, with `CORE_WARNINGS_AS_ERRORS=ON`;
- the corpus junctioned in read-only.

Logs are in `build/verify-w35d/g1/` (git-ignored).

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 65/65 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; exit 0 |
| M2 `recalc --compare` | exit 0 |
| app_qt | 52/52, 0 warnings |
| app | 34/34, 0 warnings |
| fixtures; markers | OK; OK |

## Mutations (`build/verify-w35d/mutate-g1.py`, `g1/mutations.log`)

5 mutations, one at a time, all anchors count 1.  **All 5 are red in at least one lane.**

| id | guard switched off | result |
|---|---|---|
| **Q10** | 1.1 Paste refused under a queued view edit: **green in wave 35c** | red in both lanes (2, 2) |
| V1 | 1.2 a session Sort refused under a queued Show Level | outline persist red (3) |
| V2 | 1.2 a session Level refused under a queued Sort | sort persist red (4) |
| F1 | 1.3 `save_calculate`'s appends written to the target, not the scratch | core red (4) |
| F2 | 1.3 the scratch not removed on failure | core red (7) |

- **Q10 is red now, so R35cG1-3 is closed.**  F1 is red now, so R35cG2-1 is closed for the
  appends (the reviewer's R35dG1-4 notes the Props write is outside the seam).
- **The restore needed one clean-up.**  F2 left
  `%TEMP%\w35-calculate-fault-target.mpp.calculate.tmp` behind.
  - The 2.7 existence check then refused that scratch name, so the restored
    `mppwrite_roundtrip_calculate` read exit 8.
  - That is the 2.7 rule working as designed.  The leftover was the mutation's own artefact.
  - Removed, the test passed: 415 checks, 0 failures (`g1/calc-rerun.log`).  The worktree was
    clean throughout.
- **For the harness:** a mutation that breaks cleanup leaves temp files behind.  The harness
  should list and remove the test temp names after each mutation.

## Independent review (`review-g1.md`)

A read-only agent reviewed `86dbee9..2ebcfd8`: 0 Critical, 0 High, **3 Medium**, 4 Low.

- **R35dG1-1 (Medium):** the 1.2 rule can be bypassed twice:
  - (a) a session Sort first, then a queued Level, because `apply_outline_level` never checks
    for a session sort;
  - (b) a queued Level, Ctrl+Z, a Sort, then Ctrl+Y.  The redo list is ended only for a
    `sort_view` top, and the rebuild shows the Level again over the sort.
  - Both end with sorted rows shown while Save writes the Level over the file's order.  The
    file written is the measured Show Level shape, and the sort is labelled session-only, so it
    is rated Medium.
- **R35dG1-2 (Medium):** `calculate.md`'s new Scope paragraph says the var-record writers still
  match the pin.
  - That is wrong for the Notes and Text1 removals, which move the Task freed counter
    `0x01000002`.
  - Its list of refused port saves also leaves out the percent-complete and indent/outdent
    saves.
- **R35dG1-3 (Medium):** the 1.6 lists still show ids wave 35c closed.
  - `memory/coordinator.md` and `plan-waves27-30.md` list R35bG0-1 and -2 as open Highs.
  - The summaries contradict 1.5's "partly fixed" rows.
  - They also read as if no High is open.  R35dG0-1 (High) opened after the builder's G0, so the
    builder could not have listed it.
- **Lows:**
  - R35dG1-4: the fault seam covers only the five appends, and it is a production global with no
    scope-guard reset.
  - R35dG1-5: the clear-sort-under-a-queued-Level case is tested only with no sort showing.
  - R35dG1-6: three Sort-branch refusals were never run red, and a `view-state.md` sentence
    contradicts its table.
  - R35dG1-7: R35cW-CI1 and -CI2 are recorded "fixed" before CI.

The reviewer found no new defect pattern, no names, and the trailer on all 7 commits.

## Merge rule

G1 closes R35cG1-3, R35cG1-4 (partly; see R35dG1-1), R35cG2-1, R35cG0-3, R35cW-2 and R35cW-1
(partly; see R35dG1-3).  It adds no Critical or High.  The open High is still R35dG0-1.
