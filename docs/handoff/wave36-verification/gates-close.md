# Wave 36 CLOSE: the builder's gates

Builder Claude Sonnet 5.5, branch `wave36`.  CLOSE is the G0f review's findings that let a wrong file PASS (W36-C.1 to C.4), the queue of the review's Mediums and Lows (W36-C.5), the G3 precondition
re-check, and the fresh-tree gates.  Logs are under `build/w36/` (git-ignored), each named for the parent commit of the item it gates.  The builder used no COM, did not touch the VM, `vmconnect`,
`build/trace-2021/` or `build/r7/`, and pushed nothing.

## Per item

Doc gates for each commit, in order: `check_fixtures`, `check_markers`, `guard_names`, `scan_client_names`, the `client-string-[0-9a-f]{8}` grep (none), `git diff --check`: all clean at C.2, C.3, C.4 and C.5
(`de97271-W36-C.2-gates.status`, `1a581b2-W36-C.3-gates.status`, `79109bb-W36-C.4-gates.status`, `429f241-W36-C.5-gates.status`) and at C.1 (`c0cf117-W36-C.1-scan.log`).

| item | id | commit | red | green |
|---|---|---|---|---|
| W36-C.1 | R36G0f-1 | `de97271` | the new cases against the gate at `c0cf117`: 60 FAIL, 1637 PASS (`c0cf117-W36-C.1-red.log`) | `trace_gate_guards` 1833 PASS, 0 FAIL (1739 before); named pane guards 14 listed, 14 red; derived of the changed functions 31 selected, 30 red, 1 redundant |
| W36-C.2 | R36G0f-2 | `1a581b2` | against the gate at `de97271`: 18 FAIL, 1861 PASS (`de97271-W36-C.2-red.log`) | 1879 PASS, 0 FAIL; named stamp guards 14 listed, 14 red; derived of `noise_windows` and `_task_uids` 58 selected, 56 red, 2 redundant, 0 green |
| W36-C.3 | R36G0f-3 | `79109bb` | against the gate at `1a581b2`: 14 FAIL, 1885 PASS (`1a581b2-W36-C.3-red.log`) | 1899 PASS, 0 FAIL; named row-record guards 5 listed, 5 red; derived of `_canon_cedl_var2` 23 selected, 22 red, 1 redundant |
| W36-C.4 | R36G0f-8 | `429f241` | the check for the 12 hex digits against the three notes as they were: 2 FAIL, 1899 PASS (`79109bb-W36-C.4-red.log`) | 1901 PASS, 0 FAIL; no gate guard changes |
| W36-C.5 | R36G0f-4 to -7, -9 | `8f7786f` | none: a note in the plan, no code | no gate or test changes |

The counts of derived and named runs per item are in each commit's message and in `build/w36/<parent>-W36-C.N-meta-{named,derived}.log`.  Derived candidates after C.1: 308; after C.2: 312 (299 at G2).  A candidate
`_task_uids:if-never:data is None` was green at C.2 and is red by a raise since a case with a control that has no Task stream was added.

## Per group, at `8f7786f`

Logs `build/w36/8f7786f-W36-CLOSE-group-*` (`groupgates.sh`, the guard meta-test excluded with `-LE trace_gate_meta` because it runs on its own below).  The trees are the wave 35g CLOSE trees, rebuilt in place.

| gate | result |
|---|---|
| core build (`build/w35g-close-fresh`, Release) | exit 0 |
| core `ctest -E plan_recalc -LE trace_gate_meta -j4` | 67/67 |
| `MPP_NO_CORPUS=1 plan_recalc` | 1/1 |
| M2 recalc `--compare` | exit 0; 332/332, 268/268, 64/64 fields |
| `check_fixtures` | exit 0 |
| app tree ctest (`build/w35g-close-fresh-app`) | 34/34 |
| Qt tree ctest (`build/w35g-close-fresh-qt`) | 52/52 |
| `trace_gate_meta` (below) | passes, 58 minutes (3488 s) |

CLOSE changes no C++ source and no `CMakeLists.txt`; the macOS-only CI jobs have nothing new to gate.  The engine stays 7/8 (`constraint-mso` expected-failing).

## The meta-test, one full run

One full `ctest -C Release -L trace_gate_meta` on a fresh configure of `8f7786f` (`build/w36/8f7786f-W36-CLOSE-meta-full.log`, six gate copies at a time; nothing else CPU-heavy ran during it):

**Exit 0 in 3488 s, 58 minutes** (the ctest line says 3487.53 s; started 02:06, ended 03:04 AUSEDT, 22 November 2026).

* `named guards listed 194, red 194, not red 0` (184 at G2: the C.1 to C.3 guards are in, the two old pane guards are out)
* `candidates derived 312 (run 312): removed red 296 (90 of them by a case that asserts the gate does not raise), listed redundant 16, green 0, crashed 0` (299 derived at G2, 283 red, 16 redundant)
* `the unmodified gate: passes`

It is one process, so the closing loop over `REDUNDANT` ran to the end without error.  The run was faster than the 75 minutes of G2 because nothing else ran beside it.

## G3: the precondition re-checked

The brief asks the coordinator's builder to re-check the G3 precondition against `spec/writers/insert.md`'s held-out pairs and any round 7 one-factor pairs before CLOSE.

* `spec/writers/evidence/trace-insert-rules-r6.md` states a rule for the new row's FixedMeta entry that is byte-exact for the four wave35-g3 insert pairs, and says it was derived from those same four: none is held out.
  It states no rule that predicts a held-out pair.
* `spec/writers/insert.md` still says that items 3 and 5 are not decoded.
* Round 7 one-factor recordings `tests/data/wave36-r7-insert-*` are committed (control-null, control, milestone-first, task-first, task-last, task-level2, two-milestones).  Only
  `scripts/oracle_r7_insert.py` references them (`git grep`); no note derives a rule from them or checks the four-pair rule against them.
* So the precondition fails: no rule predicts a held-out pair.  G3 is recorded "not started: no held-out insert pair (trace-insert-rules-r6.md)".  Deriving the rule on the round 7 pairs and
  testing it on the wave35-g3 pairs (or the reverse) is the work that would meet it.
