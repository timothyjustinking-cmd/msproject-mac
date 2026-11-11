# Wave 35g G0: coordinator gates, mutations and review

_Coordinator (Opus 5.5), Wed 11 Nov 2026.  Tip gated: `fe61064` (WAVE-35g-G0-COMPLETE), in the verify worktree,
fresh build trees, below normal priority, while the trace VM was off (host memory rule: the VM plus one builder)._

## Fresh-tree gates (`build/verify-w35g/g0/`)

The first run (17:12) was stopped by the coordinator in the Qt build, when Fable's work order set the memory rule.
This is the rerun, 17:43 to 18:05:
- core: build 0, `ctest -E plan_recalc` 65/65;
- `plan_recalc_tests`: no-corpus 266 checks, and corpus, both exit 0;
- engine self-test 0, and the engine suite 7 pass, 1 expected-fail (constraint-mso);
- M2 exit 0;
- `check_fixtures` 0 (333 tracked .mpp/.mpt);
- Qt: build 0, ctest 52/52;
- app: build 0, ctest 34/34.
- **`check_markers` exit 1:** one hit, the operator's first name in `spec/writers/evidence/trace-captures-r5.md`,
  which arrived from `main` at `5863054`, the base of `wave35g`.  It is fixed on `main` at `052b026`.  It is not
  a wave 35g fault; the merge brings the fix in.

## Mutations (`g0/mutations.log`): 5 of 5 red, all restored green

| Id | Guard | Red in |
|---|---|---|
| Y1 | 0.1, the Formula... callback refusal | qt_sort_persist (2 FAIL), qt_task_mode_document_edit_gate (2) |
| Y2 | 0.1, the after-exec refusal message | qt_sort_persist (2), qt_task_mode_document_edit_gate (2) |
| Y3 | 0.1, the bug restored (refuse before the dialog opens) | qt_sort_persist (4), qt_task_mode_document_edit_gate (4) |
| Y4 | the kept W35f-0.2 pre-open Calculate guard | qt_calculate_gate (3) |
| Y5 | 0.2, the redo guard narrowed to Sort only | qt_sort_persist (21), qt_outline_persist (4) |

Restored: all four tests exit 0, and the worktree is clean.

## Review (`build/verify-w35g/review-g0.md`, independent, read-only)

0 Critical, 0 High, 0 Medium, 4 Low:
- R35gG0-1: no Formula...-then-Cancel test;
- R35gG0-2: two assertions narrower than their labels;
- R35gG0-3: the input-dialog helper keeps re-arming timers after it returns;
- R35gG0-4: a refused Formula... is silent until the dialog closes.

Under the work order's fixed point, these go to the plan queue under "Queued from wave 35g", not to a wave 35h.

**G0 verdict:** R35fG1-1 and R35fG0-1 are closed by `284c3cb` and `24a5c1d`.
