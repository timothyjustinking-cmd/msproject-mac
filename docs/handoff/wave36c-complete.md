# Wave 36c record: group G0, the three CI failures of the first wave 36b run

Brief `docs/handoff/wave36c-brief.md` section 2; branch `wave36c`, cut from `wave36b`; builder Claude Sonnet 5.5.
Nothing is pushed and no workflow is dispatched.  Gates are per commit under `build/w36c/` (git-ignored), each log
named for its item (`w36c-0.N-*`).  This record covers G0 only; CLOSE is not done.

## G0: items

One finding id per commit, in the brief's order.

| item | id | commit | what |
|---|---|---|---|
| W36c-0.1 | R36bCI-1 (High) | `2ab6e29` | `host.ps1` takes its temp folder from `[IO.Path]::GetTempPath()`; it was the only `env:TEMP` in `tools/` (`git grep -n 'env:TEMP' -- tools/`); `test_host_skip.ps1` removes `TEMP` before it loads `host.ps1` and checks the folder exists |
| W36c-0.2 | R36bCI-2 (High) | `f39cdb5` | the fake guest answers with the id `vmx` wrote through `Set-Kvp`, not one the test stamped from the clock; a new case forces a clock boundary (every `Get-Date` one second later than the last) |
| W36c-0.3 | R36bCI-3 (High) | `eb781b5` | the derived meta run is four shards (`--shard K/N`, ctests `trace_gate_meta_derived_KofN`, a four-job CI matrix, 3 workers each), every mutant is held to 2 GiB per process, one flushed progress line per candidate, a union test (`trace_gate_meta_shards`) and a limit test (`trace_gate_meta_limit`) |

### Gate line

Every commit: `scan_client_names HEAD` 0 hits, `guard_names HEAD~1..HEAD` 0, `check_markers --worktree` OK,
`check_fixtures` OK, the `client-string-[0-9a-f]{8}` grep none, `git diff --check` clean
(`build/w36c/w36c-0.1-gates.log`, `-0.2-`, `-0.3-`, run with `build/w36c/gates.sh` on the staged tree of the item; the
parents are `29abfe8`, `2ab6e29` and `f39cdb5`).

Fresh tree `build/w36c/tree` (configured and built from nothing at `eb781b5`, Visual Studio 17 2022, Release):
build exit 0 (`build.log`); `ctest -E plan_recalc -LE trace_gate_meta` **70/70** (`ctest-core.log`; the two new tests
are in it); `MPP_NO_CORPUS=1 ctest -R plan_recalc` 1/1 (`ctest-plan-recalc.log`).  Not rerun: the M2 332/332 lane,
the engine suite lane and the app and app_qt trees: nothing in this group touches C++, the app or the engine (the
changes are one PowerShell host script, its test, a Python test script, CMake test registration and `ci.yml`).

## W36c-0.1 (R36bCI-1)

Reproduced locally: `pwsh -NoProfile -File tools/trace/test_host_skip.ps1` with `TEMP` removed from the environment
fails at `host.ps1:52`, "Cannot bind argument to parameter 'Path' because it is null", as on the Linux runner.  The
test now removes `TEMP` itself (`Remove-Item Env:TEMP`), so it runs without it on every host, and the 20 cases pass
(19 original, one new load check).  Other uses of `env:TEMP` under `tools/`: none.  `r5b-skip.ps1` is only parsed.

## W36c-0.2 (R36bCI-2)

The hypothesis of the brief is the cause, reproduced by forcing it.  The fake `Get-GuestKvp` answered with an id
stamped `HHmmss` in `Run` before `vmx` made its own.  With `Get-Date` stubbed so that every call is one second later
than the last, the ids differ, `vmx` finds no `out_meta` that starts with its id, waits out its timeout and returns
`TIMEOUT`, and the case "good reply" fails: the failure seen on the Windows runner.  `Set-Kvp` in the test now keeps what
`vmx` writes to `cmd` and the fake answers with the id in it.  The runner's own boundary was not observed (no log
beyond the one failing case), and the 29 seconds of its first case (the cold start, longest on a slow runner) are not
explained by this alone; the check was not loosened.  21 cases pass, 20 of them the same as before.

## W36c-0.3 (R36bCI-3)

### Measured first

The whole process tree of the old script, unsharded, all 328 derived candidates, sampled every 2 s
(`build/w36c/measure.ps1`, `build/w36c/mem/derived-j6.*`, `derived-j2.*`; machine 16 GB, 12 cores, commit limit 41.7 GB):

| run | wall | peak processes | peak working set | peak commit |
|---|---|---|---|---|
| default workers (6) | 3358 s (two other jobs ran at the same time) | 30 | **11 571 MB** | **36 583 MB** |
| 2 workers | stopped by the 2 h background limit at 7202 s, not finished | 12 | 9 661 MB | 36 338 MB |

The 2-worker run reached the heavy candidate at about 3830 s and held more than 5 GB of commit until 4743 s (914 s;
the six-worker run: 1316 s to 2277 s, 961 s), 36 GB at the top in both.  Memory is therefore not a matter of the
number of workers: one candidate is the cause.

The cause: `_canon_summary:if-always:pid in (SI_AUTHOR, SI_LASTAUTHOR, SI_REVNUMBER) and _lpstr_slot_ok(raw) and
_lpstr_text_ok(raw)` makes every property of a SummaryInformation stream take the string branch, and
`noise.update(range(lo + 4, lo + 8 + _u32(data, lo + 4)))` then adds a range of up to 4 billion offsets to a set until
the machine's memory ends in a `MemoryError` (its red result: "gate raised on the command line: MemoryError"; the only
`MemoryError` of the run).  The window of more than 5 GB commit lasted from 1316 s to 2277 s at six workers.  A
16 GB hosted runner cannot hold that: the likely reason it "lost communication".  Workers and shards alone would not
have fixed it.

### The fix

* Every mutant run is held to 2 GiB per process (`MUTANT_MEMORY_LIMIT`; `RLIMIT_AS` on POSIX, a job object with a
  per-process memory limit on Windows).  The candidate above now ends in the same red result in about two seconds
  (`build/w36c/mem/cand136-capped.*`).
* The derived candidates are dealt round robin into four shards (`partition`), each its own ctest
  `trace_gate_meta_derived_KofN` with `--jobs 3` (one vCPU of the runner's four stays free for its agent) and each its
  own CI job (a matrix of four); a step fails the job if CMake registers another number of shards than the matrix lists.
* `[done/total] state key -> detail` is printed and flushed as each candidate finishes, on stdout and to `--log`; CI runs
  the shard with `ctest -V`, which streams it (probed locally: a line every 3 s arrived every 3 s).
* The run's ctest `TIMEOUT` is 6100 s, the job's `timeout-minutes` 115.

### Timing at the CI worker count (3 workers), capped, through `measure.ps1`

| shard | candidates | wall | peak processes | peak commit |
|---|---|---|---|---|
| 1 of 4 | 82 | 1418 s | 7 | 212 MB |
| 2 of 4 | 82 | 1777 s | 7 | 224 MB |
| 3 of 4 | 82 | 2011 s | 7 | 227 MB |
| 4 of 4 | 82 | 1918 s | 7 | 300 MB |

Shard 1 ran beside the 2-worker run above; shards 2 to 4 ran at once with each other and with the end of that run, so the
times are on the slow side for a 12-core machine and about right for a 4 vCPU runner.  The slowest is 2011 s; times
three is 6033 s, rounded to 6100 s.  All four exit 0, each also ran the unmodified gate as its baseline ("passes").
The state of every one of the 328 candidates equals the uncapped, unsharded run: 214 red, 98 red (raise), 16 GREEN
(all on REDUNDANT), none missing, none doubled (`build/w36c/compare_states.py`, `w36c-0.3-compare.log`).

## Guards seen red

| item | red | green |
|---|---|---|
| 0.1 | `host.ps1` with the old line put back, `TEMP` unset: exit 1, "Cannot bind argument to parameter 'Path' because it is null" (`w36c-0.1-red.log`); cause reproduced by removing `TEMP`, not by reasoning | 20 PASS, exit 0 (`w36c-0.1-green.log`) |
| 0.2 | the forced-boundary case against the old fake: that case FAILs, the other 20 PASS, exit 1 (`w36c-0.2-red.log`) | the new fake: 21 PASS, exit 0 (`w36c-0.2-green.log`) |
| 0.3 union | `partition` made to return one fewer per shard: "candidates 328, in the shards 325, problems 3", "missing from every shard: ..." x3, exit 1 (`w36c-0.3-union-red.log`); made to add a candidate to every shard: "in shards 1,1,2,3: ...", exit 1 | the real `partition`: 328 in the shards, 0 problems, "the check reports a dropped and a doubled candidate: yes", exit 0, for N = 1 to 5 (`w36c-0.3-union-green.log`) |
| 0.3 limit | the limit switched off (`None`): "over the limit: NOT refused (exit 0: allocated)", exit 1 (`w36c-0.3-limit-red.log`) | 2 GiB: "refused with MemoryError", "under the limit: allowed", exit 0 (`w36c-0.3-limit-green.log`); ctest `trace_gate_meta_shards` and `trace_gate_meta_limit` pass |

## Deferred

Nothing in G0 is deferred.

## Found, not fixed

- **The Linux path of the memory hold (`RLIMIT_AS`) and of the shards on a runner were not run here** (no WSL, no
  Docker, no Linux machine): the Windows job object was run and seen red and green.  The CI job runs
  `trace_gate_meta_limit` before the long run, so a wrong limit on Linux fails in a second.  The coordinator's CI run
  is the evidence for the Linux path and for the runner's timings.
- **`trace_gate_meta_named` is unchanged:** it still runs at the default worker count (the runner's four vCPU) with its
  old limits; it passed on the first CI run.  It gets the memory hold too (same `run_mutant`), but no sharding.
- **The runner's own failures were not seen**, only reproduced by cause: the clock boundary of 0.2 and the
  runaway candidate of 0.3 (the runner's log was never uploaded).
- **The 2-worker memory run did not finish** (stopped at the 2 h background limit with the heavy candidate already
  passed); its peaks are of the part it ran.
- **Windows `test_host_skip.ps1` first case, 29 s on the runner,** is not explained (see 0.2).
- **Other `env:TEMP` or Windows-only paths in `tools/`:** none the test reaches; `tools/trace/*.ps1` other than
  `host.ps1` and `capture/r5b-skip.ps1` are VM-side or host tools not loaded by `trace_host_scripts`.
- **Still the coordinator's:** a fresh worktree and the gates there, every "Guards seen red" row put back to red, the
  independent review of the wave diff, then the push, CI on every job and the merge.

WAVE-36c-G0-COMPLETE
