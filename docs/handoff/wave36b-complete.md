# Wave 36b record: group G0, the fix-only items

Brief `docs/handoff/wave36b-brief.md` section 2; branch `wave36b`, cut from `wave36`; builder Claude Sonnet 5.5.
Nothing is pushed and no workflow is dispatched.  Gates are per commit under `build/w36b/` (git-ignored), each log
named for the commit it was run on (the parent of the item's commit).  This record covers G0 only; CLOSE is not done.

## G0: items

One finding id per commit, in the brief's order.

| item | ids | commit | what |
|---|---|---|---|
| W36b-0.1 | R36W-2, R36G2-C1, R36G2-4, R36G2-5 (High) | `9e5fb70` | the Props insert is refused again (`save_props_edit` refuses any record whose id is not already in `114/Props`); the framing stays as `mppwrite::props_insert_framing`, which tests call and no save does |
| W36b-0.2 | R36W-1 (High) | `2f1c174` | a CV_iew view date word (+1176, +1182, +1186) is noise only where the pair's control and null hold two different in-span dates in that word of that record; the zoom window dates are compared elsewhere |
| W36b-0.3 | R36W-3 (High) | `8cb3e52` | no note under `spec/` or `docs/` prints the VM's network address (the tip only; history stays); the W36-C.4 check finds an address by hash, in any case and with `:` or `-` separators |
| W36b-0.4 | R36W-4, R36G2-3 (Medium) | `f0b55b1` | the framing refuses a measured record already in the stream, by id, for both measured ids, and appends the second id beside the first (test only) |
| W36b-0.5 | R36W-5 (Medium) | `e02a030` | `MPP_REQUIRE_OLEFILE=1` (set for every CI job) turns the missing-olefile skip into exit 1 in `test_trace_gate.py` and `test_gate_guards_meta.py`; ctest `trace_gate_require_olefile` tests the switch |
| W36b-0.6 | R36W-6 (Medium) | `c3a4be8` | the meta run is two ctests (`trace_gate_meta_named`, `trace_gate_meta_derived`) and two CI jobs with limits set from local timings; the REDUNDANT end-of-run check no longer dies under `--only` |

### Gate line

Every commit: `check_fixtures` 0, `check_markers --worktree` 0 and `--range main..HEAD` 0, `guard_names main..HEAD` 0,
`scan_client_names HEAD` 0, the `client-string-[0-9a-f]{8}` grep none, `git diff --check` clean, no CR in the message,
both trailers present (`build/w36b/<parent>-W36b-<item>-gates.status`: `e1b2ee5` for 0.1, `9e5fb70` for 0.2,
`2f1c174` for 0.3, `8cb3e52` for 0.4, `f0b55b1` for 0.5, `e02a030` for 0.6).

Build and test, core tree `build/w35g-close-fresh` (incremental, reconfigured at 0.5 and 0.6): at `e1b2ee5`/0.1
`ctest -E plan_recalc -LE trace_gate_meta` 67/67; at 0.4 68/68 (the new `trace_gate_require_olefile`); at the tip
`c3a4be8` build exit 0, 68/68, `MPP_NO_CORPUS=1 ctest -R plan_recalc` 1/1
(`build/w36b/c3a4be8-W36b-final-core.status`).  The matrix and asan jobs' `-LE trace_gate_meta` leaves both shards out
(`ctest -N -LE trace_gate_meta` lists 69 tests, `-L trace_gate_meta` the two shards).

Full `trace_gate_meta` run, each shard alone through the real ctest, one after the other, six workers on eleven cores,
on the tree of `e02a030` with the 0.6 changes of the script and CMake (the script's logic is as committed in
`c3a4be8`; only its docstring changed after): **named 24.2 minutes (1449 s) passed; derived 41.0 minutes (2457 s)
passed; 65.2 minutes in all** (`build/w36b/e02a030-W36b-0.6-shard-named.log`, `-shard-derived.log`,
`-shard-times.txt`).  Each shard also ran the unmodified gate as its baseline.

Not a fresh-tree build: the tree is the incremental `build/w35g-close-fresh`.  The coordinator's fresh-worktree gates
follow the marker.

## Guards seen red

| item | red | green |
|---|---|---|
| 0.1 | the refusal switched off for the two measured ids at the call site (planted `continue`): `roundtrip_props` 1821 checks, **24** failures, 9 pairs x 2 ("save_props_edit refuses the insert", "left no change and no file") = 18, and 6 in the refusal list (the absent record with a measured id and the measured baseline record, 3 each) (`build/w36b/e1b2ee5-W36b-0.1-red.log`) | 1821 checks, 0 failures (`-green.log`) |
| 0.2 | the new test against the gate of `9e5fb70`: stops at the new `canonical_all` signature (`9e5fb70-W36b-0.2-red.log`); with only the pair clause planted out of the new gate, 86 checks FAIL, among them the zoom review case, the five zoom shapes and the pair-agreeing cases (`-red2.log`); the meta: 31 `cview-*` named guards red (15 new or re-anchored), the derived candidates of the touched functions red except one listed redundant (`-meta2.log`, `-meta3.log`) | 1993 PASS, all passed (`-green.log`; 1901 before); sweeps identical before and after: 202 control-nulls 180 PASS 22 OPEN, 191 edits 181 FAIL 10 PASS, no pair moved |
| 0.3 | against the tip of `2f1c174`: "no note under spec/ or docs/ prints the host's or the VM's network address" FAILs naming the evidence note and `review-g0f.md`, and "the evidence note names the VM's node" FAILs (`2f1c174-W36b-0.3-red.log`) | all passed (`-green.log`); the stored host digest was shown to catch the host's node once, found by hash in a recorded-bytes file and planted in memory (`-host.log`); the VM digest is shown by the red run, where the real note failed it |
| 0.4 | the already-in-the-stream refusal switched off at the call site in a scratch build (not committed): 1825 checks, 3 failures, "no exception was thrown" (`9e5fb70-W36b-0.4-red.log`) | 1825 checks, 0 failures (`-green.log`) |
| 0.5 | the new test against the scripts before the switch: 2 failures, one per script, "exit 77" where 1 is wanted (`8cb3e52-W36b-0.5-red.log`); through the real ctest, once, with the shim and `MPP_REQUIRE_OLEFILE=1`: `trace_gate_guards` and `trace_gate_meta` **Failed**, `trace_gate_require_olefile` Passed | 8 checks, all passed (`-green.log`); with `MPP_REQUIRE_OLEFILE=0` the same two are **Skipped** (`f0b55b1-W36b-0.5-ctest.log`) |
| 0.6 | the end-of-run check of the REDUNDANT list with the mutant runs stubbed, in the named shard: `KeyError` on a key that did not run (`e02a030-W36b-0.6-shard-red.log`) | both shards main returned 0 (`-shard-green.log`, `-shard-green2.log`); the two real shard runs above passed |

## Deferred

Nothing in G0 is deferred.

## Found, not fixed

- **The 0.1 commit message undercounts its red list.**  It says "plus the 2 for the baseline record in the refusal
  list"; the log has 6 there (the absent measured record and the measured baseline record, 3 each), so 24 = 18 + 6.
  History stays, so this line is the correction.
- **A hash of a 48-bit address is a tip guard, not a secret.**  Anyone can enumerate every 12-hex window.  The two
  digests in `test_trace_gate.py` stop a note printing the address by accident; they do not hide it.  The host's node
  also survives in recorded-bytes files outside the check (`analysis/*.tsv`, `core/mppwrite/assignment_growth.cpp`),
  which the brief puts out of scope; the older commits keep the VM's address in the notes, as history stays.
- **The planted-address cases use a synthetic address**, by its own digest, because planting the real two would print
  them.
- **CI is not run.**  No group dispatches a workflow.  The two shard jobs' limits (90 and 150 minutes; ctest
  `TIMEOUT` 4500 s and 7500 s) are three times the local minutes, for a hosted runner with fewer cores; no hosted
  runner was measured, so the coordinator's CI run is the check.
- **`MPP_REQUIRE_OLEFILE` covers the two tests with `SKIP_RETURN_CODE 77`**, which are all of them in this tree
  (`git grep SKIP_RETURN_CODE`); a future test that skips on a missing olefile must read the variable too.
- **`docs/START-HERE.md` line 32 still names the one CI job `trace-gate-meta`**, and `wave36-complete.md`, the
  wave 36 briefs and reviews name the single `trace_gate_meta` test.  The records are history; START-HERE is the
  CLOSE group's file.
- **0.4's scratch red build** needed the call-site condition written as a runtime comparison (C4127 under
  warnings-as-errors); the scratch change is not in the tree.

WAVE-36b-G0-COMPLETE

## CLOSE: the records and the fresh-tree gates

Brief `docs/handoff/wave36b-brief.md` section 3.  Builder Claude Sonnet 5.5, 22 November 2026.  Nothing is pushed and no
workflow is dispatched.  CLOSE changes no C++ source and no `CMakeLists.txt`.

| item | commit | what |
|---|---|---|
| W36b-CLOSE.1 | `ec1c0d9` | the whole-wave review's four Lows (R36W-7 to -10) queued in `plan-waves27-30.md`, "Queued from wave 36b (CLOSE)"; not fixed, the brief names six items |
| W36b-CLOSE.2 | `8fb0b87` | `docs/START-HERE.md` (8178 bytes, under 8 KB) and `memory/coordinator.md` brought to the end of the wave |

Doc gates for each: `build/w36b/706078b-W36b-CLOSE.1-gates.status` and `ec1c0d9-W36b-CLOSE.2-gates.status`: `check_fixtures` 0,
`check_markers` 0 (worktree and range), `guard_names` 0, `scan_client_names` 0, the `client-string` grep none, `git diff --check`
clean; each commit carries both trailers.  Neither commit has a guard: they are records.

### Gate line on fresh trees at the tip

Three trees configured from nothing at `706078b` (the G0 record; the two CLOSE commits after it change docs only), `build/w36b-close-fresh`
(core), `-app` and `-qt`, Release, `CORE_WARNINGS_AS_ERRORS=ON` (`build/w36b/closegates.sh`; logs `build/w36b/706078b-W36b-CLOSE-*`, status
`706078b-W36b-CLOSE.status`):

| gate | result |
|---|---|
| core configure and build | exit 0, exit 0 |
| core `ctest -E plan_recalc -LE trace_gate_meta -j4` | 68/68 |
| `MPP_NO_CORPUS=1 ctest -R plan_recalc` | 1/1 |
| M2 recalc `--compare` | exit 0; 332/332, 268/268, 64/64 fields |
| app tree (configure, build, ctest) | exit 0, exit 0; 34/34 |
| Qt tree (configure, build, ctest) | exit 0, exit 0; 52/52 |
| `check_fixtures`, name scans, markers, `git diff --check` | all 0 or none (at the tip of CLOSE.2 again before the record's commit, see below) |

The engine stays 7/8 (`constraint-mso` expected-failing).

### The full `trace_gate_meta` run, both shards, timed

On the fresh core tree, through the real ctest, one shard after the other, nothing else running (07:19 end, 05:52 start for the whole script):

* `trace_gate_meta_named` **Passed, 1430 s (23.8 minutes)**, limit 4500 s;
* `trace_gate_meta_derived` **Passed, 2453 s (40.9 minutes)**, limit 7500 s;
* **64.7 minutes in all** (`build/w36b/706078b-W36b-CLOSE-meta-named.log`, `-meta-derived.log`; the times are in the status file).

These match the G0 local times (24.2 and 41.0 minutes), so the 90 and 150 minute CI limits are still three times local, for a hosted runner
no one has measured.  `--output-on-failure` prints nothing for a pass, so the guard counts are not in these logs; the G0 shard logs hold them.

### Deferred

Nothing in CLOSE is deferred.  R36W-7 to -10 are queued by the commit above, not deferred from this brief.

### Found, not fixed

* **The CLOSE gate set ran at `706078b`**, before the two docs commits; those change no source, CMake or workflow file (`git diff --stat 706078b..HEAD`
  lists `plan-waves27-30.md`, `START-HERE.md`, `memory/coordinator.md`).  The doc gates were run again on each.
* **No CI job has run.**  The two shard jobs, `MPP_REQUIRE_OLEFILE=1` and `pip install olefile` are unproven on a runner; that is the coordinator's
  dispatch.
* **Still the coordinator's:** a fresh worktree and the gates there, every "Guards seen red" row put back to red, the independent review of the wave
  diff, then on the operator's word the push, CI on every job and the merge of `wave36` and `wave36b` into `main`.  The G0 "Found, not fixed" list
  above stands (the 0.1 message's undercount, the hash as a guard and not a secret, the VM-address history, the `SKIP_RETURN_CODE` coverage).

WAVE-36b-COMPLETE
