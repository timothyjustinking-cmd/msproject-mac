# Wave 35g G1: coordinator gates, mutations and review

_Coordinator (Opus 5.5), Wed 11 Nov 2026.  Tip gated: `a0fb695` (WAVE-35g-G1-COMPLETE), in the verify worktree,
fresh build trees, below normal priority, with the trace VM off._

## Fresh-tree gates (`build/verify-w35g/g1/`, 18:09 to 18:33)

- core: build 0, `ctest -E plan_recalc` 65/65;
- `plan_recalc_tests`: no-corpus and corpus, exit 0;
- engine self-test 0, and the engine suite 7 pass, 1 expected-fail;
- M2 0;
- `check_fixtures` 0;
- Qt: build 0, ctest 52/52;
- app: build 0, ctest 34/34;
- `check_markers` exit 1: the same base-commit hit as G0.  It was cleared by the merge of `main` (`97f6a70`).

## Mutations (`g1/mutations.log`, `g1/mutations-Z6.log`): 6 of 6 red, all restored green

| Id | Guard | Red in |
|---|---|---|
| Z1 | 1.5, the Show Level wording branch | qt_outline_persist (2 FAIL) |
| Z2 | 1.5, the Sort wording branch | qt_sort_persist (4) |
| Z3 | 1.6, the one-field-per-Undo wording | qt_calculate_gate (2) |
| Z4 | 1.7, the not-queued note | qt_calculate_gate (2) |
| Z5 | 1.4, the session-only Level note | qt_outline_persist (2) |
| Z6 | `document_edit_refusal` never refuses (the guard 1.8's tests cover) | qt_task_mode_document_edit_gate (7), qt_sort_persist (32), qt_calculate_gate (7) |

- Z6's first plant (`true ||`) did not compile (C2220: the code after it became unreachable, and warnings are
  errors), so it is not evidence.  Its rerun with a volatile always-true condition is the result above.
- qt_task_information_apply and qt_resource_information_gate stayed green under every mutation planted at their
  lane.  The guards are held by the tests named in the table.

## Review (`review-g1.md`)

0 Critical, 0 High, 0 Medium, 5 Low (R35gG1-1 to -5), queued in the plan.
