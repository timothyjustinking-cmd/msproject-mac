# Wave 36 G0: the builder's gates

Builder Claude Sonnet 5.5, 11 November 2026, branch `wave36`.  Logs are under `build/w36/` (git-ignored), each named for the parent
commit of the item it gates.  The core tree is `build/w35g-close-fresh`, built incrementally at 0.4 and 0.5.  The builder used no COM and
did not touch the VM.  This file was written in W36-0b.9 (R36G0-10): the G0 record cited it, and it did not exist.

G0 changed Python and PowerShell tools and the records only; no C++ source.  Items 0.4 and 0.5 add ctest registrations in `CMakeLists.txt`,
which the doc-gate script reports as "SOURCE CHANGED", so the core build and the ctest were run for those two.

## Per commit

| item | commit | parent (log prefix) | doc gates | core build / ctest |
|---|---|---|---|---|
| W36-0.1 | `a20874d` | `0e8c9d4-W36-0.1-*` | 0 / 0 / 0 / 0 / none / clean | not needed (no source) |
| W36-0.2 | `028fb51` | `a20874d-W36-0.2-*` | same | not needed |
| W36-0.3 | `f0c3113` | `028fb51-W36-0.3-*` | same | not needed |
| W36-0.4 | `cebf445` | `f0c3113-W36-0.4-*` | same, SOURCE CHANGED (`CMakeLists.txt`) | build 0, ctest 0 |
| W36-0.5 | `4ec0562` | `cebf445-W36-0.5-*` | same, SOURCE CHANGED | build 0, ctest 0 (67/67, `trace_gate_guards` and `trace_host_scripts` included) |
| W36-0.6 | `2ecf42b` | `4ec0562-W36-0.6-*` | same | not needed |
| record | `53351b2` | `2ecf42b-W36-G0-record-*` | same | `MPP_NO_CORPUS=1 ctest -R plan_recalc` 1/1 |

Doc gates, in order: `check_fixtures` exit, `check_markers` (range and worktree) exit, `guard_names` exit, `scan_client_names` exit,
the `client-string-[0-9a-f]{8}` grep (none), `git diff --check`.

## The gate texts the record cites

* `build/w36/0e8c9d4-W36-0.1-red.log` and `-green.log`: 200 flipped bytes in `214/CEdl/Var2Data` and in `Props14` of `wave30-calculate`,
  `96 streams, 94 checked, 0 problems` on the old gate.
* `build/w36/a20874d-W36-0.2-red.log`: one byte at offset 4 of `114/TBkndAssn/FixedMeta` of `wave34-assign-add-second`,
  `96 streams, 96 checked, 0 problems`.
* `build/w36/028fb51-W36-0.3-red.log`: a nonexistent null path, a traceback; the control as its own null, `0 problems`.
* `build/w36/f0c3113-W36-0.4-red.log`, `-red-ctest.log`: the "nothing checked" case failed on the `f0c3113` gate; ctest before the
  registration, `No tests were found!!!`.
* `build/w36/cebf445-W36-0.5-red.log`: `3 failed` on the HEAD copies of `host.ps1` and `r5b-skip.ps1`; `-core-ctest.log`: 67/67.
* `build/w36/2ecf42b-W36-G0-plan_recalc.log`: `100% tests passed, 0 tests failed out of 1`.

## What these gates did not show (the G0 review, `review-g0.md`)

The G0 record claimed "exact noise windows", "no by-name waiver" and a test that "flips one byte in every stream".  The review found those
claims overstated (R36G0-1 to -8); G0b (W36-0b.1 to 0b.9) fixed the gate and corrected the claims.  The G0 gates above are what was run at
the time, not proof of those claims.
