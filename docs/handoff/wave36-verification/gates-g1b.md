# Wave 36 G1b: the builder's gates

Builder Claude Sonnet 5.5, 11 November 2026, branch `wave36`.  G1b narrows the font writer's Bold rule to the cells a committed pair measures and closes the G1
review's R36G1-1 to -7 (`review-g1.md`).  Logs are under `build/w36/` (git-ignored), each named for the parent commit of the item it gates.  The builder
used no COM, did not touch the VM or `build/trace-2021/`, and pushed nothing.

## Per commit

| item | id | commit | parent (log prefix) | red then green | doc gates |
|---|---|---|---|---|---|
| W36-1b.1 | R36G1-1 | `5cb0f36` | `9fe128a-W36-1b.1-*` (`red`, `red2`, `green`) | core `viewstream` 15 to 16 FAIL lines and app `cell_bold` 6 failures on the old writer; core 1/1, app 1/1, Qt 2/2 on the new | 0 / 0 / 0 / 0 / none / clean, SOURCE CHANGED (`9fe128a-W36-W1b.1-green-gates.status`) |
| W36-1b.2 | R36G1-2 | `166734a` | `5cb0f36-W36-W1b.2-*` | records only | same (`5cb0f36-W36-W1b.2-gates.status`) |
| W36-1b.3 | R36G1-3 | `a0f3d32` | `166734a-W36-1b.3-*` (`red`, `green`, `green2`) | 9 FAIL lines on a row the Task table does not hold; core `viewstream` 1/1, `view|style|bold|font` 8/8, app `cell_bold|view|edit` 17/17 | same, SOURCE CHANGED (`166734a-W36-W1b.3-gates.status`) |
| W36-1b.4 | R36G1-6 | `f87f271` | `a0f3d32-W36-1b.4-*` | 26 FAIL lines from two mutations of `main_window.cpp`; Qt `cell_bold|qt_fat_slack` 2/2 | same, SOURCE CHANGED (`a0f3d32-W36-W1b.4-gates.status`) |
| W36-1b.5 | R36G1-7 | `3cc5a9d` | `f87f271-W36-1b.5-*` | app 3 FAILURE(S) with the function stubbed, Qt 4 FAIL lines with the shell change backed out; app 15/15, Qt 2/2 | same, SOURCE CHANGED (`f87f271-W36-W1b.5-gates.status`) |
| W36-1b | R36G1-4 | `f803924` | `3cc5a9d-W36-W1b.6-*` | records only | same (`3cc5a9d-W36-W1b.6-gates.status`) |
| W36-1b.1 (record) | R36G1-1 | `e749d81` | `f803924-W36-W1b.7-*` | records only | same (`f803924-W36-W1b.7-gates.status`) |

R36G1-5 has no commit: it stays open as a record line in the G1b section of `wave36-complete.md`.

Doc gates, in order: `check_fixtures` exit, `check_markers` (range) exit, `guard_names` exit, `scan_client_names` exit, the `client-string-[0-9a-f]{8}` grep
(none), `git diff --check`.

## Per group, at `f803924`

Logs `build/w36/f803924-W36-G1b-group-*`.  `e749d81` follows and changes records only.

| gate | result |
|---|---|
| core build (`build/w35g-close-fresh`, Release) | exit 0 |
| core `ctest -E plan_recalc -j4` | 66/67; `trace_gate_guards` fails with `FileNotFoundError tests/data/wave23-flags-control.mpp` (`f803924-W36-G1b-trace_gate_guards-rerun.log`) |
| `MPP_NO_CORPUS=1 plan_recalc` | 1/1 |
| M2 recalc `--compare` | exit 0; 332/332, 268/268, 64/64 fields |
| `check_fixtures` | exit 0 |
| app tree ctest (`build/w35g-close-fresh-app`) | 34/34 |
| Qt tree ctest (`build/w35g-close-fresh-qt`) | 52/52 |

The one failure is outside G1b.  Commit `3193808` added ten `*-control-null.mpp` fixtures whose control is not `<stem>-control.mpp`; `5939f6d` merged them into
`wave36` after G0b's 67/67.  No G1b commit touches `tools/` or `tests/data`.  The fix is a pair-to-control map in `tools/trace/test_trace_gate.py` and
belongs to G0b's tool.

`app/main.mm` is changed by W36-1b.5 and cannot be built on this machine; CI on macOS is its gate.
