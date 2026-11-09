# Wave 35d G2 (the Lows): coordinator verification (Tue 10 Nov 2026)

The gated commit is `d335791` (`WAVE-35d-G2-COMPLETE`), the end of items 2.1 to 2.15.  That is
16 builder commits, `6f5cc18` to `d335791`, on `wave35d` from `2ebcfd8`.  The builder's own
record is `gates-g2.md`.

Gates ran in the verify worktree from 04:46 to 05:08, with:
- the worktree detached at `d335791`;
- fresh build trees;
- Release, with `CORE_WARNINGS_AS_ERRORS=ON`;
- the corpus junctioned in read-only.

Logs are in `build/verify-w35d/g2/` (git-ignored).

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 65/65 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; exit 0 |
| M2 `recalc --compare` | exit 0 |
| app_qt | 52/52, 0 warnings |
| app | 34/34, 0 warnings |
| fixtures; markers | OK; OK |

## Mutations (`build/verify-w35d/mutate-g2.py`, `g2/mutations.log`)

9 mutations, one at a time, all anchors count 1.  **All 9 are red in their own lane.**  The
worktree was clean afterwards, every lane was green again, and no scratch file was left in the
temp folder.

| id | guard switched off | own lane | other lane |
|---|---|---|---|
| T1 | 2.3 a time word of 24:00 or later | core red (5) | app green (the plan shares the helper) |
| T2 | 2.6 the Fixed2Meta entry names its row's Fixed2Data record | core red (2) | |
| T3 | 2.7 a scratch path that cannot be examined | core red (3) | |
| T4 | 2.11 a queued Calculate refuses a data change | Qt calculate gate red (5) | |
| T5 | 2.11 the cell gate back to the view-only refusal | Qt calculate gate red (5) | |
| T6 | 2.12 plan: a removal refused when a skipped row lacks 0x1C | assignment red (4) | |
| T7 | 2.12 reader: a skipped row's session byte never marked stored | assignment red (4) | |
| **T8** | 2.9 the second text style refused unless measured (35c M9 re-run) | core red (5) | **app red (4); green in 35c** |
| T9 | 2.14 reader: `var_keys` never filled (35c R2 re-run) | app red (108) | |

T8's app lane is red now, so the 35c note that no app test drove the second style through the
builder is closed (R35cG2-4).

## Independent review (`review-g2.md`)

A read-only agent reviewed `2ebcfd8..d335791`: 0 Critical, 0 High, 0 Medium, 5 Low.
- **R35dG2-1:** the queued-Calculate guard on Cut, Paste, Fill Down and Task Information has no
  test of its own; only the cell gate is tested.
- **R35dG2-2:** Table/Date Format and Custom Formula edits are accepted beside a queued
  Calculate, and Calculate Project queues beside pending document edits.  The save refuses
  both, so no wrong file is written, but `calculate.md` overstates the shell's coverage.
- **R35dG2-3:** the 2.12 "not blamed by the plan" check passes on any earlier refusal.
- **R35dG2-4:** the scratch-probe seam is a production global and can only fake
  `file_type::none`.  The `not_found`-with-error path runs only on Linux and macOS CI.
- **R35dG2-5:** two record lines misdescribe 2.4.  `calculate.md`'s "2.2 must not be dropped" is
  ambiguous between wave 35c and wave 35d.

The reviewer checked several points and found each correct:
- 2.7 is correct on MSVC, libstdc++ and libc++: a `not_found` result proceeds whatever the error
  code says.
- 2.8 (the pin last) makes no earlier test vacuous.
- 2.6's arithmetic matches the writer's.
- 2.14 still collects every key.
- All 16 commits carry the trailer, with no names.

## Merge rule

G2 adds no Critical, High or Medium.  The open High is still R35dG0-1.
