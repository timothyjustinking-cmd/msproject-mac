# Wave 36c G0: coordinator verification (22 November 2026)

Tip 039b821 (WAVE-36c-G0-COMPLETE, 11:56). Bundled off the machine (14.9 MB).
A fresh worktree, detached at 039b821, clean.

## Gates (fresh tree, build/verify-w36c/g0/)
- scan_client_names HEAD 0 hits; guard_names 1f9f135..HEAD exit 0; check_markers range OK; check_fixtures OK; diff --check clean.
- Configure and build (VS 2022, warnings as errors) exit 0.
- Core ctest (without plan_recalc and trace_gate_meta): 70/70. plan_recalc (MPP_NO_CORPUS=1): 1/1.
- trace_host_scripts, trace_gate_meta_shards, trace_gate_meta_limit: 3/3.
- ctest -N -L trace_gate_meta lists named plus derived_1of4 to 4of4 (5).

## Re-red (build/verify-w36c/g0/rered\)
| id | planted | result |
|---|---|---|
| m1 | host.ps1 back to $env:TEMP | red, "Cannot bind argument to parameter 'Path' because it is null" |
| m2 | old fake (Set-Kvp drops cmd, id from Run's clock) | red, the forced-boundary case FAILs |
| m3 | partition drops one candidate from shard 1 | red, 327 of 328, 1 problem |
| m4 | check_union misses doubles | red, "reports a dropped and a doubled candidate: NO" |
| m5 | run_limited ignores the limit | red, "over the limit: NOT refused" |
| m6 | run_mutant call site back to subprocess.run | GREEN: no test pins the call site (gap, R36cG0-2) |
Controls after restore: host, shards, limit all exit 0; tree clean.
Real call site: the heavy candidate through run_mutant at 2 GiB ends `red (raise)` with MemoryError in 2 s.

## Review
Independent Sonnet review: review-g0.md, 0 Critical, 0 High, 2 Medium (R36cG0-1 baseline not under the limit;
R36cG0-2 call site unpinned), 6 Low (R36cG0-3 to -8). Queued for wave 37, not fixed in 36c.

## Open for CI
The POSIX RLIMIT_AS path and the runner timings are untested here; the CI run on wave36c is the evidence.

## CLOSE (050eefc, WAVE-36c-COMPLETE 14:16)
- Bundled off the machine. `git diff 039b821 050eefc` touches only docs/START-HERE.md, docs/handoff/plan-waves27-30.md,
  docs/handoff/wave36c-complete.md and memory/coordinator.md: no code, test, script, CMake or CI file, so the G0 build
  and test results above stand for the tip.  The range gates were run again on the tip with this record (below).
- The builder's fresh-tree gates (core 70/70, plan_recalc 1/1, M2 recalc 332/332, app 34/34, Qt 52/52, the scans) and
  its timed meta run (named 1501 s; derived shards 1160, 997, 1111 s and the fourth, all exit 0) are its own; the
  coordinator's evidence for the meta run and for Linux is the CI run on wave36c.
- CLOSE.1 queues R36bG0-1 to -7 in the plan; CLOSE.2 adds the START-HERE line on the 22 November history rewrite.

## Found at CLOSE (queued, not fixed)
- **R36cC-1 (Low):** the START-HERE rewrite line says the host's node is "gone from text"; it is gone from the notes'
  prose and the messages but stays in spaced GUID byte dumps and recorded data (R36bG0-1).  Fix: say so in the line.

## Merge decision
0 Critical, 0 High open from wave 36c (G0 review: 2 Medium, 6 Low; CLOSE: 1 Low), all queued in the plan.  Merge into
main when CI is green on every job of qt.yml, ci.yml and app-macos.yml for the wave36c tip.

## CI and merge (22 November 2026)
Runs on `2052d36`: qt.yml (macos-qt, linux-qt, windows-qt), ci.yml (windows-msvc 583 s, ubuntu-gcc 786 s, ubuntu-asan
516 s, trace-gate-meta-named 3597 s, trace-gate-meta-derived 1 to 4 of 4: 1299, 1220, 1470, 1442 s), app-macos.yml
(macos-appkit): all 12 jobs success.  The 2 GiB hold and the shards ran on Linux.  `main` fast-forwarded `1dabff4` to
`2052d36` and pushed.  Trace gate at `2052d36`: 2009 PASS, 0 FAIL; positive sweep 202 control-nulls, 180 PASS, 22 OPEN;
negative 191 edits, 181 FAIL, 10 PASS (the pinned no-ops).
