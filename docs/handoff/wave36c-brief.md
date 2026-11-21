# Wave 36c brief: fix-only, the three CI failures of the first wave 36b run (for the Sonnet builder)

_Written Sun 22 Nov 2026 by the Opus coordinator from the first CI run of `wave36b`._

**Baseline.**  Branch `wave36c`, cut from `wave36b` at its CLOSE record (after the history rewrite of Sun 22 Nov
2026; the rewrite is described in `wave36b-verification/coord-verify.md` section 4).  `docs/handoff/standing-rules.md`
binds this wave, and the lift rule of `wave36-brief.md` section 0 still binds.

**Source:** `docs/handoff/wave36b-verification/coord-verify.md` section 5 (R36bCI-1 to -3), with the failing lines
quoted.  The CI logs are not in the repo; the quoted lines are the evidence.

**This wave adds no writer and lifts no refusal.**  Every change is a portability fix in a test or a host script,
a CI resource fix, or a record.

**Machine.**  Never start, stop or touch the Hyper-V VM, never run COM or Project, never read or write
`build/trace-2021/` or `build/r7/`.  `tools/trace/kvp/host.ps1` is tested only through `test_host_skip.ps1`, which
stubs every Hyper-V call.

## 0. Publication

No group, and not CLOSE, pushes, dispatches a workflow or rewrites history.  The coordinator pushes and dispatches
CI after CLOSE.

## 1. Rules for this wave

- Items in order, one finding id per commit, the gate line of the standing rules per commit, each guard seen red
  before green and recorded under "Guards seen red".
- A runner-only failure is seen red locally by reproducing its cause (an unset variable, a forced clock boundary,
  a lowered memory or worker limit), never by reasoning alone.  Record how.
- Deferral only with evidence of an attempt (the command, the error, the log path).

## 2. G0: the fixes

- **W36c-0.1 (R36bCI-1, High):** `tools/trace/kvp/host.ps1` and every other script `trace_host_scripts` loads work
  where `TEMP` is not set (Linux and macOS runners): the temp folder comes from `[IO.Path]::GetTempPath()` (or an
  equivalent that is never null).  `git grep -n 'env:TEMP' -- tools/` lists every use; fix each the test reaches and
  list the others in "Found, not fixed".
  - Test: `test_host_skip.ps1` run under `pwsh` with `TEMP` removed from the environment passes every case; with the
    old line put back it fails as on the runner ("Cannot bind argument to parameter 'Path' because it is null").
- **W36c-0.2 (R36bCI-2, High):** the case "host.ps1 good reply (CRLF dropped by the XML parse)" fails on the Windows
  runner and passes locally.  First reproduce it, then fix the cause.
  - Hypothesis to test first: the fake `Get-GuestKvp` answers with `$script:lastid`, stamped `HHmmss` in `Run`
    before `vmx` makes its own id, so a second boundary between the two makes them differ and `vmx` waits or throws.
    Reproduce by forcing the boundary (for example a `Get-Date` stub that advances one second between the two
    calls).  If that is the cause, the fake answers with the id `vmx` actually wrote through `Set-Kvp`, not one it
    guessed.  If it is not, find the cause and record the evidence; do not loosen the check.
  - Test: the forced-boundary run fails with the old fake and passes with the new one; all 19 cases pass.
- **W36c-0.3 (R36bCI-3, High):** `trace-gate-meta-derived` fits a hosted runner (`ubuntu-latest`, 4 vCPU,
  16 GB): the CI job ended after 87 minutes with "The hosted runner lost communication with the server ... starves
  it for CPU/Memory" and no log.
  - Measure the derived shard's peak memory locally (whole process tree) at the default workers and at 2; record it.
  - Make the shard fit with room to spare: a `--jobs` the CI job passes, a further split into shards each with its
    own job, or both.  Every derived candidate still runs in exactly one shard: a test lists the union of the shards
    against the full candidate list and FAILs on a missing or doubled candidate (this also closes R36bG0-4's union
    point).
  - The run writes its progress as it goes (one flushed line per candidate), so a killed job leaves a log.
  - Each job's `timeout-minutes` and ctest `TIMEOUT` come from a local timing at the CI worker count, times three.
  - Test: the union test red with one candidate dropped from a shard, green restored.

Done when: each item is committed with its guard seen red, and the gate line passes on a fresh tree.  Marker:
`WAVE-36c-G0-COMPLETE`.

## 3. CLOSE

- The wave record in `wave36c-complete.md`: per item the commit, the guard seen red, the gates; "Deferred"; "Found,
  not fixed".
- `docs/START-HERE.md` (under 8 KB) and `memory/coordinator.md` brought to the wave's end, including the line on the
  history rewrite (commit ids in records before Sun 22 Nov 2026 name pre-rewrite commits).
- R36bG0-1 to -7 (`wave36b-verification/review-g0.md`) queued in `plan-waves27-30.md`, not fixed.
- The gate line on a fresh tree at the tip, plus one full `trace_gate_meta` run (every shard), timed.
- Marker: `WAVE-36c-COMPLETE`.

## 4. The coordinator's part

After each marker: a fresh worktree, the gates there, every "Guards seen red" claim put back to red, and an
independent review of the wave diff.  Then push, CI on every job, and the merge of `wave36`, `wave36b` and `wave36c`
into `main` if CI is green and no Critical or High is open.
