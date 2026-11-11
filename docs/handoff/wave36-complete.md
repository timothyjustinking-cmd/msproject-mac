# Wave 36 record: the writer wave

Brief `docs/handoff/wave36-brief.md`; branch `wave36`, cut from `main` at `b2c0b19`; builder Claude Sonnet 5.5.
Nothing is pushed.  Gates are per commit under `build/w36/` (git-ignored), each log named for the commit it was run on.

## G0: the gate and the records

Items, one finding id per commit, in order:

| item | id | commit | what |
|---|---|---|---|
| W36-0.1 | A-2 | `a20874d` | the noise allow-list moves into `tools/trace/trace_gate.py`: exact per-stream windows and byte ceilings taken from the trace null run, no by-name whole-stream waiver; four narrow masks (the GUID counter bytes, the creation stamps, the font word's top byte at payload +43, the per-file Props identity records); `runone6.ps1` records the exit code |
| W36-0.2 | A-4 | `028fb51` | no wrong-length mask: refused when the control and the reference differ in length; fixed-stride streams masked per record (Task FixedData 202 from 48, Task Fixed2Data 64, Assn FixedData 110, Assn Fixed2Data 48, and the smaller Fixed2Data streams) |
| W36-0.3 | A-3 | `f0c3113` | an absent control null, or the control passed as its own null, is a FAIL named "no null"; `runone6.ps1` passes a path that does not exist instead of the control |
| W36-0.4 | A-6 | `cebf445` | ctest `trace_gate_guards` (`tools/trace/test_trace_gate.py`): one flipped byte in every non-empty stream of a committed pair is a FAIL, with `checked > 0` and an equal stream set; a gate that checked nothing is a FAIL "nothing checked" |
| W36-0.5 | A-11 | `4ec0562` | `r5b-skip.ps1` invokes "Skip for now" only in a dialog whose title starts "Sign in"; `kvp/host.ps1` `vmx` throws on a MISMATCH and on a reply whose length is not the one announced; ctest `trace_host_scripts` (`tools/trace/test_host_skip.ps1`, only when `pwsh` is found) |
| W36-0.6 | A-9 | `2ecf42b` | `docs/START-HERE.md` 28605 to 6267 bytes; the wave history moves to `docs/handoff/history-waves34-35.md`; `memory/coordinator.md` 18672 to 5265 bytes and current; `INDEX.md` |

**Gate line.**  At `4ec0562`, core tree `build/w35g-close-fresh` (incremental, built and reconfigured at 0.5): build exit 0,
`ctest -E plan_recalc -j4` 67/67 passed, including `trace_gate_guards` and `trace_host_scripts`
(`build/w36/cebf445-W36-0.5-core-ctest.log`); `MPP_NO_CORPUS=1 ctest -R plan_recalc` passed
(`build/w36/2ecf42b-W36-G0-plan_recalc.log`); `check_fixtures.py` OK.  Every commit: `check_fixtures` 0, `check_markers` 0,
`guard_names` 0, `scan_client_names` 0, the `client-string-[0-9a-f]{8}` grep none, `git diff --check` clean,
no CR; each commit carries both trailers.  This wave's G0 changes no C++ source, so no new fresh-tree build was
run (the coordinator's fresh-tree gates follow the marker).

### Guards seen red

Each red was run on the previous commit's tool (or the old copy), then the green on the new one.

| item | red (the old tool) | green |
|---|---|---|
| 0.1 | 200 flipped bytes in `214/CEdl/Var2Data` and in `Props14` of `wave30-calculate`: `96 streams, 94 checked, 0 problems` (the old gate skipped both by name) | both are problems (`build/w36/0e8c9d4-W36-0.1-red.log`, `-green.log`) |
| 0.2 | `wave34-assign-add-second` with one byte flipped at offset 4 of `114/TBkndAssn/FixedMeta` (a control/null noise offset; control 152 bytes, reference 186): `96 streams, 96 checked, 0 problems` | the stream is a problem (`build/w36/a20874d-W36-0.2-red.log`, `-green.log`) |
| 0.3 | a nonexistent null path: a traceback, no verdict; the control as its own null: `96 streams, 96 checked, 0 problems` | both print "no null" and exit 1 (`build/w36/028fb51-W36-0.3-red.log`, `-green.log`) |
| 0.4 | the "nothing checked" case failed on the `f0c3113` gate (`FAIL W36-0.4 nothing checked: a problem 'nothing checked' -- ['size-noise Props14 556->566']`, `1 failed`); `ctest` on the core tree before registration: `No tests were found!!!` | 105 checks pass; `66/66 Test #3: trace_gate_guards ... Passed` (`build/w36/f0c3113-W36-0.4-red.log`, `-red-ctest.log`, `-core-ctest.log`) |
| 0.5 | the HEAD copies: `FAIL host.ps1 sha MISMATCH: throws`, `FAIL host.ps1 reply length differs from out_meta: throws`, `FAIL r5b-skip.ps1 Invoke() is under a title StartsWith(Sign in) test`, `3 failed` | all 6 pass; `67/67 Test #4: trace_host_scripts ... Passed` (`build/w36/cebf445-W36-0.5-red.log`, `-green.log`, `-core-ctest.log`) |
| 0.6 | START-HERE.md 28605 bytes (limit 8192), coordinator.md 18672 bytes, both stale (main `5e1c8ea`, "nothing of wave 35g is pushed") | 6267 and 5265 bytes, true at `b2c0b19` on `wave36` |

### Deferred

None.  Every G0 item is fixed.

### Found, not fixed

* **CI has no `olefile`.**  `trace_gate_guards` exits 77 and ctest reports SKIPPED on a runner without it, so CI does not yet run the guard.
  Direction: `pip install olefile` in the workflows that run ctest.  Workflow files are outside G0.
* **Path-bearing streams tolerate up to their ceiling.**  `214/CEdl/Var2Data`, `Props14` and `SummaryInformation` carry file paths, so the
  gate allows a size delta (128, 96, 16) and up to 64, 64 and 40 differing bytes at equal size.  One flipped byte there is undetectable by design;
  the test flips ceiling plus one.  The path-stream bytes are not counted as checked.
* **The windows are tuned to the trace null run and the 99 committed control pairs** (`tests/data`).  A recording made on another machine can carry
  noise the null run did not show, and the coordinator's re-gate of the 109 recordings may FAIL on it.  Such a FAIL is a finding by design: widen a
  window only with a measured offset, never by stream name.  Same-machine noise outside the null run's list (Props tail bytes, Assn and Rsc FixedMeta
  and Fixed2Meta, Cal Fixed2Data above its null-run ceiling, `CEdl/VarMeta` 2 over 1, `CV_iew/Var2Data` 6 over 4) is left to the control mask.
* **The scripts of 0.5 were not run against the VM.**  The wave rules bar touching the VM, so `trace_host_scripts` stubs the Hyper-V calls and checks
  `r5b-skip.ps1` by AST.  The first real VM run of the two scripts is the coordinator's.
* **The trace-agents' frozen copy of the tools and `build/trace-2021/gs6.ps1`** were not read or changed (wave rule).  The VM runs the frozen copy of
  `trace_gate.py` until the coordinator copies the new one in.
* **`python -I` cannot import `olefile`** here (it lives in the user site-packages), so the trace tests run without `-I`; they read only committed files.

WAVE-36-G0-COMPLETE
