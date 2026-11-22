# Review of the W36c G0 change set (`git diff 1f9f135 039b821`, worktree `(the verify worktree)`, 039b821)

Read-only. Probes in `(the reviewer's scratch folder)` (`shards.py`, `base.py`, `memcsv.py`). Linux was not available (no WSL), so the POSIX path is reviewed by reading only.

Findings: 0 Critical, 0 High, 2 Medium, 6 Low.

---

## R36cG0-1 (Medium) The baseline run is not under the memory limit, and a limit-only failure is classified red, not CRASH

`tools/trace/test_gate_guards_meta.py:944-945`
```
base = pool.submit(subprocess.run, [sys.executable, TEST, "--fail-fast"],
                   capture_output=True, text=True, cwd=HERE)
```
Every mutant goes through `run_limited` (line 813); the baseline "the unmodified gate: passes" goes through plain `subprocess.run`. If a normal run of `test_trace_gate.py` (and the gate CLI processes it starts, which inherit the limit) ever needed more than 2 GiB on the runner, the baseline would still say "passes" while every mutant ran under a different regime.

How a limit death is classified (`run_mutant`, lines 813-824): return code 0 is GREEN; a first `FAIL` line containing "gate raised" is `red (raise)`; any other `FAIL` line is `red`; no `FAIL` line at all is `CRASH`. A MemoryError inside a gate call (in-process, or in the CLI subprocess the test reports as "gate raised on the command line") is caught by the case and printed as a FAIL line, so it is red. Only a death outside any case is a CRASH (counted loudly, `d_crash`).

Evidence (probe `base.py`, Windows, job object): the unmutated gate under the real 2 GiB limit passes (rc 0, 76 s), so on Windows the limit has large headroom. Under the same wrapper with the unmutated gate and a deliberately tiny limit, `run_mutant` returned:
- 60 MiB: `('red (raise)', 'gate raised MemoryError in compare: ')`
- 150, 300, 600 MiB: GREEN (so the normal run needs less than 150 MiB of commit here).

That is, a limit starvation of the real gate is reported as a red mutant, the state the run wants. The only thing that would reveal it is a stale REDUNDANT entry in a shard that holds one (every shard holds at least one: 1, 2, 3, 4... entries by `shards.py`, 16 in all), which turns the run red with "red when removed: take it off the list". So the failure is not unseen, but it is seen through a side effect, with a misleading message, and the baseline line stays green. On Linux the RLIMIT_AS measure is virtual size, not commit; I could not measure it. Python's virtual size at start is tens of MiB, so the 13x headroom measured on Windows very probably carries over, but the record itself says the Linux path was not run (wave36c-complete.md:113-116).

Fix: run the baseline through `run_limited` too (one line), so a limit-only failure shows as "the unmodified gate: FAILS".

## R36cG0-2 (Medium) Nothing goes red if `run_mutant` goes back to plain `subprocess.run`

`grep -rn "run_limited\|check_limit\|MUTANT_MEMORY" .` finds the limit used in exactly two places: `check_limit` (lines 799-800, which calls `run_limited` directly with `limit=limit`) and `run_mutant` (line 813). `--check-limit` (ctest `trace_gate_meta_limit`) therefore proves that `run_limited` works, not that the real run applies it. Put the call at line 813 back to `subprocess.run(...)` (or pass `limit=None`) and: `trace_gate_meta_limit` passes, `trace_gate_meta_shards` passes, the shards run as before, and the only observable change is that the runaway candidate allocates 36 GB again on the runner. The record's red row for the limit ("the limit switched off (`None`)", w36c-0.3-limit-red.log) switches the limit off inside `check_limit`, not at the call site.

Fix: a test that runs `run_mutant` (or a small helper it shares with `check_limit`) on a mutant that allocates 1.5 x the limit and expects `red (raise)`/MemoryError; or make `run_mutant` take the limit from one constant that `check_limit` also exercises through `run_mutant`.

## R36cG0-3 (Low) POSIX path: `preexec_fn` from a multi-threaded process, and a hard limit below 2 GiB aborts the whole run

`test_gate_guards_meta.py:777-782`
```
def hold():
    resource.setrlimit(resource.RLIMIT_AS, (limit, limit))
return subprocess.run(cmd, preexec_fn=hold, **kw)
```
It is called from `ThreadPoolExecutor` workers (line 940), and Python's documentation warns that `preexec_fn` is unsafe in the presence of threads (it can deadlock in the child if another thread held a lock at fork). `hold` imports nothing and calls one C function, so the practical risk is small, and `resource` is imported before. Two further effects: (a) `preexec_fn` forces the fork path instead of vfork/posix_spawn, slower per mutant; (b) if the container's hard RLIMIT_AS is below 2 GiB, `setrlimit` raises in the child and `subprocess` raises `SubprocessError`, which propagates out of `fut.result()` and ends the run (and the pool shutdown then waits for every queued candidate before the error is shown). A `prlimit`/`ulimit -v` wrapper or `resource.setrlimit` inside a tiny `python -c` launcher avoids all three. Not reproduced (no Linux).

## R36cG0-4 (Low) Windows path details

`test_gate_guards_meta.py:755-767, 784-791`
- Race: `AssignProcessToJobObject` runs after `Popen` returns, so the child is already running (the code comment says so). Grandchildren spawned before the assignment would escape the job. Python start-up takes tens of milliseconds and the test starts its first gate subprocess later, so this is not a real failure today; a `CREATE_SUSPENDED` start or a job passed at creation closes it, and the comment is the only guard.
- Handles: the job is closed in `finally` (good; KILL_ON_JOB_CLOSE then also reaps any survivors). Popen's handle is closed by `with`. If `SetInformationJobObject` fails (line 766) the job handle from `CreateJobObjectW` is leaked (raise before return). `CloseHandle` (line 791) has no `argtypes`, so a handle value above 2^31 would raise ArgumentError on 64-bit; real handles are small, so it is theoretical.
- `p._handle` is a private attribute; `int(p._handle)` works on CPython (the run in the record and my probe passed), but it is not a public API.
- `capture_output`/`text`: honoured. `capture_output` is dropped from the kwargs and `stdout=PIPE, stderr=PIPE` set, and `text=True` stays in `**kw` and goes to `Popen`, so both streams are decoded as text with the locale encoding exactly as in `subprocess.run(..., text=True)`; my probe received `str` for `r.stdout`. Differences: pipes are forced even when `capture_output` was not asked for; `timeout`, `input` and `check` are not supported (none is passed). An exception from `communicate()` does not kill the child (the `with` waits for it).
- Struct layout and constants are right: `JOBOBJECT_EXTENDED_LIMIT_INFORMATION` is 144 bytes on x64, flags 0x100 and 0x2000, info class 9.

## R36cG0-5 (Low) The record cites a log that ran no candidate for the "about two seconds" claim

`docs/handoff/wave36c-complete.md:74-75`: "The candidate above now ends in the same red result in about two seconds (`build/w36c/mem/cand136-capped.*`)". `build/w36c/mem/cand136-capped.out` reads: `meta run: 0 named guards, 0 derived candidates, 1 workers` ... `candidates derived 328 (run 0)`, and `cand136-capped.summary` is `exit=0 seconds=2 peak_procs=1`. It ran zero candidates (the pick did not match a key), so the 2 s is the start-up of an empty run. The claim that the heavy candidate ends red under the limit is supported elsewhere: `build/w36c/mem/shard-4of4.progress.log:33` `[32/82] red (raise) _canon_summary:if-always:pid in (SI_AUTHOR, ...) -> gate raised on the command line: MemoryError`, and shard 4's peak commit of 300 MB. The "about two seconds" has no log behind it (the progress lines carry no times).

Also in the record: "for N = 1 to 5 (`w36c-0.3-union-green.log`)" (wave36c-complete.md:104); that log holds one run, N = 3 only (110/109/109). I re-ran `check_union(keys, partition(keys, n))` for N = 1 to 8 (`shards.py`): no problems, so the claim is true, but not from the cited log.

All other numbers I checked match the logs: 36 583 MB private peak, 11 571 MB working set, 30 processes, the 1316 to 2277 s window (961 s) for six workers; 9 661 MB, 36 338 MB, 12 processes, 3829 to 4743 s (914 s) for two workers (from `derived-j6.mem.csv` and `derived-j2.mem.csv`); shard walls 1418/1777/2011/1918 s, 7 processes, 212/224/227/300 MB; 214 red + 98 red (raise) + 16 GREEN = 328 in `w36c-0.3-compare.log`; redundant per shard 1+2+... = 16 (shard tails). The doc says "peak commit" for what the csv calls `private_mb`; that is the same quantity here.

## R36cG0-6 (Low) The shard guard proves `partition`, not the `--shard` selection `main` actually uses

`check_shards` (lines 861-877) and `check_union` test `partition(keys, n)`. `main` selects with `mine = set(partition(keys, shard[1])[shard[0] - 1])` (line 927) and `derived = [... and (not shard or c[0] in mine)]` (line 931). A change that broke the selection (for example `or c[0] in mine` instead of `and`, which runs every candidate in every shard four times over, or an off-by-one on K) is not caught by `trace_gate_meta_shards`, which still passes. The shape of a break would show as a 4x run time or an empty/IndexError shard, not as a red guard. The record's own union check (w36c-0.3-compare.log) was a manual comparison. A cheap fix: make the selection a function (`select_shard(keys, k, n)`) that both `main` and `check_shards` call.

## R36cG0-7 (Low) Stale comments, and the required check names change

- `CMakeLists.txt:207-221` still says the derived candidates are one test `trace_gate_meta_derived` (`--only derived`), "derived 41.0 minutes (2457 s)", and "The TIMEOUTs are three times that". The derived test no longer exists under that name (only the label does); the named timeout alone follows that sentence now.
- `.github/workflows/ci.yml:355-363` keeps "Local timings ... derived 41.0 minutes; the job limits are about three times those" above the named job, then the new comment contradicts it for the derived jobs.
- The derived job's `name` changes from `trace-gate-meta-derived` to `trace-gate-meta-derived (K of 4)`. If a branch protection or merge rule requires the old check name, it will wait forever; there are now four checks.
- The record says "WAVE-36c-G0-COMPLETE" ... "the three CI failures"; fine, but the brief asks (section 2, 0.2) "all 19 cases pass"; the record has 21 (20 + 1 new) and says so. No misstatement.

## R36cG0-8 (Low) Small behaviours of the new options, and the cross-shard form of the cover check

- `--shard K/N` with a pick that is in another shard (or a named guard id) runs 0 candidates and exits 0 (`picks` and `mine` both filter; `ok` is true with no results). `--list` ignores `--shard`. `--check-shards 0` is falsy (`if check_n:`, line 922) and falls through to the full run. `--shard 1/x` raises ValueError (not caught). None is reachable from CMake as written.
- In the end-of-run check (lines 975-985) an entry or a cover is judged "only where it ran". I computed the shard of every REDUNDANT entry and its cover (`shards.py`): 13 of the 16 covers lie in a different shard from their entry, and 1 of 16 covers is a named guard. So the old "cover is not red when removed" test is no longer made inside the entry's shard; it holds only because the cover's own shard fails it as "GREEN, not on the redundant list". The one case that would then slip is a cover that is itself a REDUNDANT key (green and listed): no entry is a chain today (`covers that are themselves REDUNDANT keys: []`), so nothing is lost now. Entries whose candidate is in another shard are not reported stale or missing: for `key in results` false the red-when-removed branch is skipped, and `key in keys` uses the full list (`keys`, line 918), so no false stale. A key that is not a derived candidate at all is reported stale by every shard (four times); harmless.
- No per-mutant timeout exists (pre-existing, `subprocess.run` without `timeout`). The 2 GiB cap bounds memory, not time; a mutant that spins without allocating holds a worker until the ctest TIMEOUT (6100 s) kills the whole shard.

---

## Checked, no finding

1. **Does RLIMIT_AS at 2 GiB risk a false red?** Not on the evidence available: the unmutated gate passes under the real 2 GiB wrapper on Windows (76 s, rc 0) and under a 150 MiB wrapper (GREEN at 150, 300, 600 MiB), and the old whole-tree peak was 300 MB commit across 7 processes (shard-4of4.summary). The test process and the gate it starts are single-threaded Python; the thread pool lives only in the meta process, which is not limited. The residual Linux risk (virtual size vs commit, glibc arenas) is not measured; see R36cG0-1.
2. **Job-object limits are inherited by the gate subprocess** (PROCESS_MEMORY applies per process of the job; children join automatically). The heavy candidate ends `red (raise) ... MemoryError` at shard-4of4.progress.log:33.
3. **Union/doubling:** `partition` plus the `mine` filter plus `picks`: with `picks` empty, shards 1 to 4 hold 82 each, 328 in all, no key twice (`shards.py`, `--check-shards 4` rc 0). The heavy candidates are in shards 4 and 1.
4. **CI "Every shard has a job":** `ctest -N` prints `  Test #1: name` and `Total Tests: 4`; `grep -c 'Test *#'` counts exactly the test lines (not the total line). `-L trace_gate_meta_derived` is an unanchored regex that matches the label `trace_gate_meta_derived` and the per-shard labels, but ctest counts tests, and `trace_gate_meta_shards` and `trace_gate_meta_limit` carry no label, so n is the number of registered shards (4). A CMake count of 3 or 5 against the matrix of 4 fails the `test "$n" = ...`; `strategy.job-total` is a valid context (4 for `shard: [1, 2, 3, 4]`). A matrix grown without CMake gives the same failure. If `grep -c` finds none, the bash `-e` default fails the step.
5. **`-L "trace_gate_meta_derived_${shard}of${total}"` selects exactly one test for N = 4:** the text before the K is `derived_`, so `1of4` cannot be matched by `11of4` or `21of4` (they contain `derived_11of4`, not `derived_1of4`). It would match `1of40`; not registered. `--no-tests=error` (CMake 3.16 or later) makes a no-match fatal.
6. **Timeouts:** ctest TIMEOUT 6100 s (101.7 min) plus checkout, Python, olefile, configure and the two short tests (seconds) fits in `timeout-minutes: 115` (6900 s) with about 13 minutes spare. The named job (4500 s in 90 min) is unchanged. The 6100 s is three times the slowest local shard, measured under load; the runner's real speed is unmeasured (the record says so).
7. **`trace_gate_meta_shards` and `trace_gate_meta_limit` run in the core jobs too** (no `trace_gate_meta` label; the matrix is Windows and Ubuntu only, no macOS where RLIMIT_AS would not be enforced). Both pass here in 0.5 s (w36c-0.3-ctest.log; my runs of `--check-shards 4` and `--check-limit`, rc 0).
8. **Progress lines:** flushed per candidate (`print(..., flush=True)`, plus `os.fsync` for `--log`), so Python's block buffering under ctest does not hold them back; `[done/total]` counts futures only, not the baseline.
9. **test_host_skip.ps1 (0.1):** the old line fails with TEMP removed (`w36c-0.1-red.log`: "Cannot bind argument to parameter 'Path' because it is null"); the new line passes. On Windows `GetTempPath()` reads TMP first, so the local test would not catch a switch to `$env:TMP`; on a Linux runner it would. `Remove-Item Env:TEMP` affects nothing later in the file (no other TEMP use; `r5b-skip.ps1` is only parsed).
10. **test_host_skip.ps1 (0.2):** with the old fake the forced-boundary case FAILs and the other 20 pass (`w36c-0.2-red.log`); with the new fake all 21 pass. In `vmx` the id is computed once and reused for the write and the match, so the stub's one-second step per call cannot make them differ; the same stub would also fail a future `vmx` that called `Get-Date` twice for the id. The stub is a pass-through (`Microsoft.PowerShell.Utility\Get-Date @PSBoundParameters`) once `tickClock` is false, supports the only parameter `vmx` passes (`-f`), and the case resets the flag after `Run` (which catches every exception), so it does not leak into later cases or into the timeout loop (a 5 s timeout with a ticking clock would expire in about 5 checks, only on a mismatch).
11. **Personal data:** none in the added lines (grep for user name, profile paths, client/company names, e-mail forms, 12-hex-digit runs and MAC-like pairs found nothing).
12. **Record numbers:** see R36cG0-5; all others agree with the logs.
