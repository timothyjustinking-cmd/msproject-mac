# Wave 35g: coordinator CLOSE, CI and merge

_Coordinator (Opus 5.5), Wed 11 Nov 2026.  Fix-only wave over wave 35f's open queue.  Under Fable's work order of
11 November 2026 (fixed point, section 2 N-2), this is the last fix-only wave._

## Range and scans

- `main..wave35g` at CLOSE is 22 commits: 21 by the builder with the trailer `Generated-By: Claude Sonnet 5.5
  via Claude Code (wave 35g)`, and the coordinator's brief `1735250`.
- The coordinator merged `main` (`1678237`: trace evidence and `tools/trace` only, including the marker fix
  `052b026`) into the branch as `97f6a70`, so that CI and the merge check run on the tree that lands on `main`.
- At `97f6a70`: `guard_names` 0, `scan_client_names` 0 hits, `check_markers --range main..HEAD` OK, and no `.mpp`
  added.

## Groups

- **G0** (`coord-gates-g0.md`): fresh-tree gates green.  Mutations: 5 of 5 red.  Review 0/0/0/4.
- **G1:** fresh-tree gates at `a0fb695` green (core 65/65, app_qt 52/52, app 34/34, plan_recalc, engine suite
  7+1 expected, M2, fixtures).  Mutations Z1 to Z6: 6 of 6 red, restored green.  Z6 was rerun: its first form
  (`true ||`) did not build (C2220, unreachable code under warnings-as-errors), so it was planted again with a
  volatile always-true condition.  Review 0/0/0/5.
- **CLOSE:** the builder's fresh-tree gates at `a0fb695` are green (`wave35g-complete.md`).  Its one red gate,
  `check_markers`, was cleared by the merge of `main`.

## CI at `97f6a70`: green on all seven jobs

| Run | Workflow | Jobs |
|---|---|---|
| 42980972985 | ci | ubuntu-asan, windows-msvc, ubuntu-gcc: success |
| 42980968833 | qt-shell | linux-qt, macos-qt, windows-qt: success |
| 42980968579 | app-macos (push) | macos-appkit: success |

## 4(e), 10 pairs: 7 checked and passing, 3 not checked

At the code of `a0fb695` (identical at `97f6a70`), Project 16.0.20527.20150 reopened the control, Project's
save and the port's save for each pair (`reopen_4e.py`, neutral root).
- **Checked, port equal to Project field by field:** sort-name-desc, sort-duration-desc, outline-level1,
  outline-deep-level1, outline-deep-level2, assign-add and assign-remove.
- **Not checked, vacuous because Project's reopen equals the control's:** calculate-unlinked, sort-name-asc and
  cell-bold.
- There is still no COM read-back of cell font state: `FontBold` is setter-only.  Wave 36 G1 owes that.
- Wave 35f's record called the same result "9 pass".  These are the honest counts (A-10).

## Reviews

- `review-g0.md`: 0 Critical, 0 High, 0 Medium, 4 Low (R35gG0-1 to -4).
- `review-g1.md`: 0/0/0, 5 Low (R35gG1-1 to -5).
- `review-wave.md`: 0/0/0, 5 Low (R35gW-1 to -5).

## Ids

- **Closed by this wave** (fixed, reviewed, CI green): R35fG1-1, R35fG0-1, R35fG0-2 to -9, R35fG1-2 to -5,
  R35fW-2 and R35fW-3.
  - **Partly fixed:** R35fW-1, remainder R35gG1-4 and R35gW-1.
  - **Closed with Lows against them:** R35fG0-4 (R35gG1-1), R35fW-3 (R35gG1-2), R35fG0-5 (R35gG1-3), R35fG1-3
    and R35fG1-1 (R35gG0-1 to -4).
- **R35dW-5 closed by the coordinator** (R35gW-2): `macos-appkit` was green at `c0c81e5` (run 42692007216), at
  `9c2fb1c` (run 42818310625), and now at `97f6a70` (run 42980968579).  That closes R35cW-4's last remainder.

## Merge

The merge rule holds:
- CI is green on all seven jobs, on three OSs;
- no Critical or High is open;
- 4(e) gives 7 checked and passing, 3 not checked.

`main` (`1678237`) is an ancestor of `wave35g`, so `main` is fast-forwarded to the record commit.

## Open after the merge (no Critical or High)

All are queued in `plan-waves27-30.md` under "Queued from wave 35g".  No wave 35h runs (work order N-2).
- **Lows:** R35gG0-1 to -4, R35gG1-1 to -5, R35gW-1 to -5.
- **Partly fixed or open, unchanged:** R35dG2-1, R35cG0-2, R35cG0-4 (wave 36 G2), the four 35b ids (R35bW-1,
  R35bW-2, R35bG2-2, R35bG3-1), R35bG0-9, R35bG1-10 and R35bG2-9.
