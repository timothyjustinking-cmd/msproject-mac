# Wave 35f G1 (the Lows): coordinator verification (Tue 10 Nov 2026)

The gated commit is `135d253` (`WAVE-35f-G1-COMPLETE`), the end of items 1.1 to 1.16.  That is
18 builder commits, `9e37503` to `135d253`, on `wave35f` from `79d24be`.  The builder's own
record is `gates-g1.md`.

Gates ran in the verify worktree from 23:16 to about 23:40, below normal priority because the
trace VM had priority.  The run used:
- the worktree detached at `135d253`;
- fresh build trees;
- Release, with `CORE_WARNINGS_AS_ERRORS=ON`;
- the corpus junctioned in read-only.

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 65/65 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; exit 0 |
| M2 `recalc --compare` | exit 0 |
| app_qt | 52/52, 0 warnings |
| app | 34/34, 0 warnings |
| fixtures; markers | OK; OK |

`app/main.mm` (1.10) cannot be compiled here.  `macos-appkit` is its gate.

## Mutations (`build/verify-w35f/mutate-g1.py`, `g1/mutations.log`)

5 mutations, all anchors count 1.  **All 5 are red in at least one lane.**  The worktree was
clean afterwards, and every lane was green again.

| id | change | result |
|---|---|---|
| Y1 | 1.8: `has_exclusive_edit` never true, so no document-edit refusal | Qt calculate gate red (7); document-edit gate red (3) |
| Y2 | 1.8 reverse: no refusal beside a pending document edit | document-edit gate red (3); calculate gate green |
| Y3 | 1.5: a queued formula keeps the structural redo list | Qt calculate gate red (3) |
| Y4 | 1.1: the untouched-field skip compares the sheet's own text again | Task Information apply red (2) |
| Y5 | 1.8: a Sort beside a pending document edit is queued, not session-only | sort persist red (3); document-edit gate green |

## Independent review (`review-g1.md`)

A read-only agent reviewed `79d24be..135d253`: 0 Critical, 0 High, **1 Medium**, 4 Low.

**R35fG1-1 (Medium):** 1.8's refusal stops Project > Custom Fields... from opening at all while
any exclusive edit is queued.
- Before 1.8, it fired only beside a queued Calculate.
- So after a queued Name sort, the stored formulas cannot even be viewed, although nothing
  would be queued.
- **Fix:** move the check into the formula-editor callback, or after `exec()` when
  `has_formula_edit()`.

**Lows:**
- R35fG1-2: the reused Sort and Level refusal texts advise steps that are refused again.
- R35fG1-3: the widened refusals at Custom Field Formula and Custom Fields have no test of
  their own.  The save-time refusal stands behind them.
- R35fG1-4: "Undo takes it back" is wrong when two fields commit.
- R35fG1-5: hygiene.

**Confirmed by the reviewer:**
- 1.8 covers every exclusive kind and builder entry in both directions, matching the save-time
  refusals one for one.
- No Redo route is left beside a pending document edit, in either shell.
- 1.1's renderings match for every field.
- `app/main.mm` reads clean for AppleClang `-Werror`.
- The seams are correct.
- All 18 commits carry the trailer.

## Merge rule

G1 adds no Critical or High.  **No Critical or High is open.**  CI (macos-appkit for
`main.mm`) comes after CLOSE.
