# Wave 36 G0b: the builder's gates

Builder Claude Sonnet 5.5, 11 November 2026, branch `wave36`.  G0b fixes the G0 review's one Critical and three High findings in
`tools/trace/trace_gate.py` (`review-g0.md`).  Logs are under `build/w36/` (git-ignored), each named for the parent commit of the item
it gates.  The builder used no COM and did not touch the VM.

## Acceptance counts

The committed ctest `trace_gate_guards` (`tools/trace/test_trace_gate.py`) runs the sweep over every pair in `tests/data`; the same
sweep as a script is `build/w36/explore/sweep.py`, and its output at W36-0b.8 is `build/w36/6279a9e-W36-0b.8-sweep.log`.

| sweep | TRACED | REFERENCE | result |
|---|---|---|---|
| negative | the control's null save | the pair's edit, for every edit that differs from the control-null outside `SummaryInformation` | **87 of 89 FAIL** at `c1a5cd5` (W36-0b.8, the pairs the sweep script then listed); **183 of 185** at the G0b tip `cea3c6d` (the committed file set, R36G0b-5, -7); the 2 that PASS are named below.  After W36-0c.5 (`9d930c6`): 191 of 191 edits FAIL |
| positive | the control's null save | the control | **97 of 99 PASS** at `c1a5cd5`; **189 of 196** at `cea3c6d`.  After W36-0c.5 (`9d930c6`): 146 of 202 control-nulls PASS and 56 are OPEN (a field nobody decoded; the G0c record) |

The four named exceptions, each checked by hand and listed by name in the test:

* Negative, the two that PASS: `wave28-percent-auto100` and `wave28-percent-auto40`.  They are no-op edits: their bytes equal the control
  outside the noise (the test checks that against the control), so there is nothing for the gate to find.
* Positive, the two that FAIL: `wave30-inactivate` and `wave35-g3-assign-remove-port-saved`.  Their control and null save are not one
  state (`214/CEdl/Var2Data` is 384 and 422 bytes in one pair and 486 and 422 in the other: a 0x2640003d record one has and the other lacks,
  and a 0x26400024 table of 128 against 64 bytes).  The gate is stricter there and no mask was widened.

At the start of G0b (the gate as G0 left it) the negative sweep let 8 of 89 edits PASS (W36-0b.2 red).

## Per commit

| item | id | commit | parent (log prefix) | red then green | doc gates |
|---|---|---|---|---|---|
| W36-0b.1 | R36G0-1, -6 | `916ac5b` | `16752d4-W36-0b.1-*` | the view-state edits PASS on the old gate (`96 streams, 94 checked, 0 problems`), FAIL on the new | 0 / 0 / 0 / 0 / none / clean |
| W36-0b.2 | R36G0-2 | `6082441` | `916ac5b-W36-0b.2-*` | 8 of 89 edits PASS on the old gate, 2 on the new (the two no-ops) | same |
| W36-0b.3 | R36G0-3 | `e2602fb` | `6082441-W36-0b.3-*` | a flip in `?DocumentSummaryInformation` PASSes on the old gate, FAILs on the new | same |
| W36-0b.4 | R36G0-4 | `1a7a4d8` | `e2602fb-W36-0b.4-*` | five data-byte cases inside the old regions PASS on the old gate, FAIL on the new | same |
| W36-0b.5 | R36G0-5 | `0e96af7` | `1a7a4d8-W36-0b.5-*` | a save with every named field changed FAILs on the old gate (ten `over-ceiling` lines), PASSes on the new | same |
| W36-0b.6 | R36G0-7 | `6b7f690` | `0e96af7-W36-0b.6-*` | 6 FAIL lines on the old `host.ps1` with the real reply shape; all pass on the new | same |
| W36-0b.7 | R36G0-8 | `6279a9e` | `6b7f690-W36-0b.7-*` (`red1`, `red2`, `red3`) | a window over all of a stream but byte 0: the old test passes, the new test FAILs; the new test on the old gate hangs | same |
| W36-0b.8 | R36G0-9 | `c1a5cd5` | `6279a9e-W36-0b.8-*` | the old gate masks the wrong row after a middle insert (same row FAIL, control position PASS); the new gate the reverse | same |
| W36-0b.9 | R36G0-10, -11 | `285da21` | `c1a5cd5-W36-0b.9-*` | records only | same |
| W36-0b.10 | CI | `712b40d` | `285da21-W36-0b.10-*` | CI installs `olefile` (not runnable here) | same, SOURCE CHANGED (`CMakeLists.txt`) |

Doc gates, in order: `check_fixtures` exit, `check_markers` (range and worktree) exit, `guard_names` exit, `scan_client_names` exit,
the `client-string-[0-9a-f]{8}` grep (none), `git diff --check`.

## Per group, at `712b40d`

| gate | result |
|---|---|
| core build (`build/w35g-close-fresh`, Release) | exit 0 |
| core `ctest -E plan_recalc -j4` | 67/67, including `trace_gate_guards` (15.4 s, 219 checks) and `trace_host_scripts` (`build/w36/285da21-W36-0b.10-core-ctest.log`) |
| `ci.yml` | parses; the two ctest jobs carry the `setup-python` and `pip install olefile` steps before Configure |

G0b changes no C++ source.  The CI run of the new steps needs a push and is the coordinator's.
