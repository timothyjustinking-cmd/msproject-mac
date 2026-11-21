# Wave 36b brief: fix-only, the wave 36 review's three Highs and three Mediums (for the Sonnet builder)

_Written Sun 22 Nov 2026 by the Opus coordinator from the wave 36 verification._

**Baseline.**  Branch `wave36b`, cut from `wave36` at its tip (wave 36 CLOSE `977d833`, the coordinator's D-8 and
W-5 commits, and the verification records).  `docs/handoff/standing-rules.md` binds this wave, and the lift rule of
`wave36-brief.md` section 0 still binds.

**Sources**, all in `docs/handoff/wave36-verification/`:
- `review-wave.md` (R36W-n), the whole-wave review;
- `review-g2.md` (R36G2-n) and `coord-4e-g2.md` (R36G2-C1, Project's reopen of the Props inserts: 0 of 9);
- `coord-close-c123.md` (the C.1 residual, the same as R36W-1).

Every finding below has a **Fix:** paragraph in its review.  Read it before the item.

**This wave adds no writer and lifts no refusal.**  Every change is a refusal, a gate tightening, a test, a CI
wiring fix or a record.

**Machine.**  Never start, stop or touch the Hyper-V VM, never run COM or Project, never read or write
`build/trace-2021/` or `build/r7/`.  The trace notes are no longer in this repo; nothing in this wave needs them.

## 0. Publication

No group, and not CLOSE, pushes, dispatches a workflow or rewrites history.  The coordinator pushes and dispatches
CI after CLOSE, on the operator's word.

## 1. Rules for this wave

- Items in order, one finding id per commit, the gate line of the standing rules per commit, each guard seen red
  before green and recorded under "Guards seen red".
- History stays: an address already committed is removed from the tip only.
- Deferral only with evidence of an attempt (the command, the error, the log path).

## 2. G0: the fixes

- **W36b-0.1 (R36W-2 and R36G2-C1, High):** the W36-2.2 Props insert is not lifted.  Project's reopen of the
  port's nine insert saves shows no baseline and no note (`coord-4e-g2.md`): the port writes the Props record only,
  not the task-table baseline fields or the note text.  So lift rule (c) fails.
  - Restore the refusal: `save_props_edit` refuses any record whose id is not already in `114/Props`, as it did
    before `7961a6e`.  The decoded framing stays, as a function the tests call to build and compare the record, and
    no save path calls it.  This also closes R36G2-4 (two inserts in one save) and R36G2-5 (unmeasured note shapes).
  - `test_insert_pairs` keeps its framing checks against Project's appended bytes, through that function, and adds
    one check per pair that `save_props_edit` refuses the insert.
  - Records: a dated correction section at the end of `wave36-complete.md` (no edit above it): W36-2.2 is "framing
    decoded, not lifted", with the 4(e) result.  Correct `props_edit.h`'s insert paragraph and START-HERE's line.
  - Test: the refusal switched off makes the new check FAIL.  Red, then green.
- **W36b-0.2 (R36W-1, High):** the Gantt zoom window's date words (+1176, +1182, +1186 of the CV_iew Gantt state
  record) are masked only where the pair's control and its control-null differ at that word.  Everywhere else they
  are compared.  The evidence row in `trace-gate-fields-g0b.md` changes with the gate.
  - Test: the review's case (zoom-custom with +1176/+1182 moved 30 days inside the span) FAILs; build it in the test
    from the committed reference, as the review's `cli_findings.py` does.  The five zoom references still pass
    against themselves.  The acceptance sweeps' counts are recorded before and after; any pair that moves is listed.
- **W36b-0.3 (R36W-3, High):** no network address in the tip.
  - `trace-gate-fields-g0b.md` names "the VM's node" and prints no address.
  - The W36-C.4 check in `test_trace_gate.py` holds no address: it compares the SHA-256 of each 12-hex-digit window
    of every note under `spec/` and `docs/` with stored digests of the host's and the VM's nodes, and also FAILs on
    any 12 hex digits that follow the word "node".  Recorded file bytes (`.cpp`, `.tsv`, `.mpp`) stay out of scope.
  - Test: a note with either address planted FAILs; the tip passes.
- **W36b-0.4 (R36W-4 and R36G2-3, Medium):** a test for the "record already present" refusal
  (`props_edit.cpp`, the already-in-the-stream branch).  Seen red with the refusal switched off at the call site.
- **W36b-0.5 (R36W-5, Medium):** in CI a missing `olefile` FAILs instead of skipping: the jobs set an environment
  variable that turns the skip return code into a failure in `trace_gate_guards`, `trace_gate_meta` and every other
  test that skips on a missing `olefile`.  Locally the skip stays.  Test: with the variable set and `olefile` hidden
  (a `PYTHONPATH` shim that raises ImportError), ctest reports the test failed.
- **W36b-0.6 (R36W-6, Medium):** the `trace-gate-meta` CI job fits a hosted runner: split the meta run into two
  shards (named guards, derived candidates) in two jobs, each with its own timeout set from a local timing of that
  shard, or another split with the same effect.  Record each shard's local minutes in the commit.

Done when: each item is committed with its guard seen red, and the gate line passes on a fresh tree.  Marker:
`WAVE-36b-G0-COMPLETE`.

## 3. CLOSE

- The wave record in `wave36b-complete.md`: per item the commit, the guard seen red, the gates; "Deferred"; "Found,
  not fixed".
- `docs/START-HERE.md` (under 8 KB) and `memory/coordinator.md` brought to the wave's end.
- The gate line on a fresh tree at the tip, plus one full `trace_gate_meta` run (both shards), timed.
- Marker: `WAVE-36b-COMPLETE`.

## 4. The coordinator's part

After each marker: a fresh worktree, the gates there, every "Guards seen red" claim put back to red, and an
independent review of the wave diff.  Then, on the operator's word: push, CI on every job, and the merge of `wave36`
and `wave36b` into `main` if CI is green and no Critical or High is open.
