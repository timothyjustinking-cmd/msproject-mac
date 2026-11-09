# Wave 35e G2 (the Lows): coordinator verification (Tue 10 Nov 2026)

The gated commit is `2dbb5d9` (`WAVE-35e-G2-COMPLETE`), the end of items 2.1 to 2.13.  That is
14 builder commits, `086e111` to `2dbb5d9`, on `wave35e` from `07c4ef6`.  The builder's own
record is `gates-g2.md`.

Gates ran in the verify worktree from 06:54 to 07:17, with:
- the worktree detached at `2dbb5d9`;
- fresh build trees;
- Release, with `CORE_WARNINGS_AS_ERRORS=ON`;
- the corpus junctioned in read-only.

Logs are in `build/verify-w35e/g2/` (git-ignored).

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 65/65 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; exit 0 |
| M2 `recalc --compare` | exit 0 |
| app_qt | 52/52, 0 warnings |
| app | 34/34, 0 warnings |
| fixtures; markers | OK; OK |

`app/main.mm` (2.7) cannot be compiled here.  CI's `macos-appkit` job is its gate.

## Mutations (`build/verify-w35e/mutate-g2.py`, `g2/mutations.log`)

4 mutations, all anchors count 1.  **All 4 are red in their own lane.**  The worktree was clean
afterwards, and every lane was green again.

| id | change | result |
|---|---|---|
| U1 | 2.6: Calculate refused beside a pending format or formula edit, off | Qt calculate gate red (3) |
| U2 | 2.6: a format edit refused beside a queued Calculate, off | Qt calculate gate red (3) |
| U3 | 2.6: a formula edit refused beside a queued Calculate, off | Qt calculate gate red (3) |
| U4 | 2.1: the queued Sort's display key left as the writer's field id | app sort red (3); Qt sort persist green |

## Independent review (`review-g2.md`)

A read-only agent reviewed `07c4ef6..2dbb5d9`: 0 Critical, 0 High, **1 Medium**, 9 Low.

**R35eG2-1 (Medium):** a third custom-formula surface has no Calculate guard.
- Project > Custom Fields..., Formula..., `show_custom_fields` calls `apply_custom_formula` with
  no check.
- With a Calculate queued, a formula set there is accepted.  Save then refuses the whole save,
  so no wrong file is written.
- The records call R35dG2-2 "fixed".
- **Fix:** the same guard before that dialog, and a `custom_fields` case in
  `qt_calculate_gate`.

**`app/main.mm` (2.7) was read for AppleClang `-Werror`, and nothing was found** that would break
the build:
- the helper is in an anonymous namespace;
- it adds no selector and no narrowing;
- it mirrors the Qt text at the shell's only two cell writers.

**Lows:**
- R35eG2-2: Calculate, Ctrl+Z, a date format edit, then Ctrl+Y re-queues the Calculate beside the
  format edit.
- R35eG2-3: the AppKit shell has neither half of 2.6, and no record says so.
- R35eG2-4: "save that edit first" is wrong advice for a formula.  The saved file then fails the
  Props pin.
- R35eG2-5: three records name `macos-qt` as `main.mm`'s gate; the gate is `macos-appkit`.
- R35eG2-6: "both orders tested" holds for the date format only.
- R35eG2-7: a header comment on the fault seam is wrong.
- R35eG2-8: the scratch-probe seam is reset by hand.
- R35eG2-9: two stale records.  One is the status file, which still lists R35dG0-1 as open.
- R35eG2-10: seven lines run past 100 columns.

Also confirmed:
- `has_pending_edits()` is exactly the predicate the save refuses on.
- 2.1's mapping is right for Name and Duration.
- The seams do nothing in a normal build.
- All 13 commits carry the trailer.

## Merge rule

G2 adds no Critical or High.  **No Critical or High is open.**
