# Wave 36 G0c: the builder's gates

Builder Claude Sonnet 5.5, 14 November 2026, branch `wave36`.  G0c fixes the G0b review's six High findings in
`tools/trace/trace_gate.py` and its test (`review-g0b.md`, R36G0b-1 to -12).  Logs are under `build/w36/` (git-ignored), each named
for the parent commit of the item it gates.  The builder used no COM and did not touch the VM or `build/trace-2021/`.  This group changes
no C++ source (the `CMakeLists.txt` change is one comment).

## Acceptance counts

The committed ctest `trace_gate_guards` (`tools/trace/test_trace_gate.py`) runs both sweeps over every control-null in `tests/data`, the
committed file set (W36-0c.1).

| sweep | TRACED | REFERENCE | at `4352eb7` (0c.1) | at `9d930c6` (0c.5) and after |
|---|---|---|---|---|
| negative | the control's null save | the pair's edit, for every edit that differs from the control-null outside `SummaryInformation` | 191 edits: 189 FAIL, 2 PASS (the no-op percent edits) | 191 edits: **191 FAIL**, 0 PASS (the two no-ops now FAIL on their null) |
| positive | the control's null save | the control | 202 control-nulls: 189 PASS, 13 OPEN | 202 control-nulls: **146 PASS, 56 OPEN** |

An OPEN pair is one whose control and null differ in a field nobody decoded.  It is reported as OPEN, never as a PASS, and may leave the list
only by passing; it may join it only with a named "not decoded" field (`SIZE_OPEN`, `MASKED_IN`, `MASKED` in the test).  The 13 at 0c.1 are
the size differences (`214/CEdl/Var2Data` records 0x26400024 and 0x2640003d, eight pairs; five pairs of a later Project build).  The 43 added
at 0c.5 are the pairs whose control and null differ at bytes no named field covers, in 12 streams, now that the control mask excuses nothing.
No mask was widened and no field was decoded in G0c.

Counts of the earlier groups for the same sweeps (R36G0b-12): 87 of 89 and 97 of 99 at G0b's `c1a5cd5`; 183 of 185 and 189 of 196 at the G0b
tip `cea3c6d`.

## Per commit

| item | id | commit | parent (log prefix) | red | green |
|---|---|---|---|---|---|
| W36-0c.1 | R36G0b-5, -7 | `4352eb7` | `5396f56-W36-0c.1-*` | the committed test: 103 PASS lines, then `FileNotFoundError ...wave23-flags-control.mpp`, exit 1 | 221 PASS, 0 FAIL |
| W36-0c.2 | R36G0b-4 | `7a23ab7` | `4352eb7-W36-0c.2-*` | the new cases on the 0c.1 gate: 30 FAIL (every GUID zero in the traced, in the reference, one GUID zero) | 253 PASS |
| W36-0c.3 | R36G0b-3 | `4c854f1` | `7a23ab7-W36-0c.3-*` | 12 FAIL (240 zero bytes, 240 junk bytes, nonzero padding, wrong padding length, per property) | 273 PASS |
| W36-0c.4 | R36G0b-2 | `a4a7ab7` | `4c854f1-W36-0c.4-*` | 3 FAIL (junk in the 0xffffffff name, a second 0xffffffff entry, two forms) | 287 PASS |
| W36-0c.5 | R36G0b-1 | `9d930c6` | `a4a7ab7-W36-0c.5-*` | 13 FAIL (OPEN list stale for 43 pairs, mask minus named fields, the negative sweep, seven inverted-byte files, three identity cases) | 304 PASS |
| W36-0c.6 | R36G0b-6 | `5e47783` | `9d930c6-W36-0c.6-red.log` (12 mutants, `mutant-NN.log`) | 12 of 12 planted guard removals red, 1 to 9 FAIL each (table below) | 329 PASS |
| W36-0c.7 | R36G0b-9 | `a35cde2` | `5e47783-W36-0c.7-*` | the new test on the old `host.ps1`: 2 FAIL (the two `ERR` cases), exit 1 | 19 PASS (`pwsh`) |
| W36-0c.8 | R36G0b-10, -12 | `3c92b13` | `a35cde2-W36-0c.8-*` | the new test on the 0c.7 gate: 3 FAIL (missing, extra and another-size stream are 0 not checked) | 335 PASS |

Every commit: `check_fixtures` 0, `check_markers` 0, `guard_names` 0, `scan_client_names` 0, the `client-string-[0-9a-f]{8}` grep none,
`git diff --check` clean, LF only; each carries both trailers (`Generated-By` and `Co-Authored-By`).  The 0c.8 doc gates print SOURCE
CHANGED for the one `CMakeLists.txt` comment (`W36-0b.9` to `W36-0b.10`).

### The twelve planted removals (W36-0c.6)

`build/w36/mutants.py N` plants one removal in a copy of the gate (`build/w36/mut/N/trace_gate.py`) and runs the test against it with
`--gate-dir`.  The test's expectations are its own parse of the five streams and the font rows, so a removal in the gate no longer moves
them.

| N | the gate with this removed | FAIL |
|---|---|---|
| 1 | every `SummaryInformation` property replaced by a placeholder | 9 |
| 2 | any 12-byte FILETIME treated as LastSave | 4 |
| 3 | `_text_ok` always true | 5 |
| 4 | the CEdl path record waived whatever its bytes | 2 |
| 5 | the CEdl size words zeroed unconditionally | 3 |
| 6 | the `214/Props` record 0x00010007 zeroed unconditionally | 1 |
| 7 | the `Props14` path record waived | 1 |
| 8 | the `Props14` tail names waived | 2 |
| 9 | every `Props14` tail entry dropped | 2 |
| 10 | the `114/Props` GUID records masked without the version-1 and length test | 5 |
| 11 | the view GUID string masked when only one side holds one | 2 |
| 12 | the font byte taken from any bag record whose length is a multiple of 44 | 2 |

Mutants 7 and 9 are caught by one check each (the Props14 path record case; the positive sweep), so a second case there would be cheap.

## Group gates

At `3c92b13`, core tree `build/w35g-close-fresh`: build exit 0 (`build/w36/3c92b13-W36-G0c-core-build.log`); `ctest -C Release -E plan_recalc -j4`
**67/67 passed**, including `trace_gate_guards` (45 s of its 300 s) and `trace_host_scripts` (`build/w36/3c92b13-W36-G0c-core-ctest.log`).
`check_fixtures.py` OK.

## Not done here

* **R36G0b-8** is open: the positive sweep proves the committed files and a hand-built set of wrong ones, but no file the real Project wrote
  in the trace.  The coordinator's re-gate of the recordings gives it.
* **The VM** was not touched.  `host.ps1`'s `ERR` handling and the reply over 300 characters are confirmed on the host in both PowerShell
  editions, not on the VM.
