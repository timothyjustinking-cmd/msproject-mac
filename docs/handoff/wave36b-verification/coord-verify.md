# Wave 36b: the coordinator's verification, the history rewrite and the first CI run

_Opus coordinator, Sun 22 Nov 2026._  Commit ids in this note are the ones after the rewrite (section 3).

## 1. G0 and CLOSE on a fresh worktree

A worktree detached at the G0 record, configured from nothing, Release, `CORE_WARNINGS_AS_ERRORS=ON`,
`MPP_REQUIRE_OLEFILE=1`:

| gate | result |
|---|---|
| core configure, build | exit 0, exit 0 |
| core `ctest -E plan_recalc -LE trace_gate_meta -j4` | 68/68 |
| `MPP_NO_CORPUS=1 ctest -R plan_recalc` | 1/1 |
| `ctest -N -L trace_gate_meta` | 2 tests (`trace_gate_meta_named`, `trace_gate_meta_derived`) |
| Qt tree configure, build, ctest | exit 0, exit 0, 50/50 |
| `trace_gate_meta_named`, at the CLOSE tip | Passed, 1446 s |
| `trace_gate_meta_derived`, at the CLOSE tip | Passed, 3045 s (other work ran on the machine meanwhile) |

CLOSE changes four record files and no source; its tree is the tree these gates ran on.

## 2. Guards put back to red

- **R36W-1 (the review's zoom case).**  The trace gate on the review's six wrong files against the committed
  references: the three unit-word files, the dates-wrong file, the row-stamp file and the rows-kind file all exit 1.
  The dates-wrong file exited 0 at the wave 36 CLOSE.  The five zoom, calculate and mark-auto references exit 0
  against themselves.
- **W36b-0.1 (the Props insert refusal).**  The refusal switched off in `save_props_edit` (a `continue` planted
  before the `count == 0` throw), `mppwrite_roundtrip_props` rebuilt: 1825 checks, 27 failures.  Restored and
  rebuilt: passed, and the tree clean.
- The other rows of "Guards seen red" were read against their logs and not re-planted.

## 3. Independent review

`review-g0.md` (R36bG0-1 to -7): no Critical, no High; one Medium, five Low, one Info.  The Medium (R36bG0-1): the
host's node is in spaced GUID dumps in 19 notes and in recorded bytes; the W36-C.4 check's message claims more than
the tip holds.  Queued for wave 37 with the Lows.

## 4. The history rewrite (operator's word, Sun 22 Nov 2026)

`main`, `wave36` and `wave36b` were rewritten from 8 Nov 2026 (`f84e586` and older untouched):
`spec/writers/evidence/trace-*` (except `trace-gate-fields-g0b.md`), `docs/handoff/trace-runbook.md`,
`tools/trace/mapdiff.py` and `tools/trace/r6_ins4.py` are gone from every commit, and the VM's node and the host's
node are gone from note text and commit messages in the rewritten range.  Checked before the push: commit counts
unchanged on all three branches; the `wave36b` tip tree byte-identical; `main`'s tip differs by the 44 removed notes
and one scrubbed line; the name scans and `check_markers` clean on the range.  The host's node is still in recorded
bytes (`analysis/*.tsv`, `core/mppwrite/assignment_growth.cpp`, spaced dumps, binary fixtures) and in history before
8 Nov 2026; a rewrite cannot remove it without changing data.  **Commit ids quoted in records written before this
date name the commits before the rewrite**; the coordinator holds the old-to-new map privately.

## 5. The first CI run (dispatched on `wave36b`)

| workflow | job | result |
|---|---|---|
| qt-shell | linux-qt, macos-qt, windows-qt | success |
| app-macos | macos-appkit | success |
| ci | trace-gate-meta-named | success |
| ci | ubuntu-gcc, ubuntu-asan | failure: `trace_host_scripts` 67/68 |
| ci | windows-msvc | failure: `trace_host_scripts` 67/68 |
| ci | trace-gate-meta-derived | failure after 87 minutes, no log |

- **R36bCI-1 (High, merge-blocking).**  On Linux `tools/trace/kvp/host.ps1:52` builds its temp folder from
  `$env:TEMP`, which is not set there: "Cannot bind argument to parameter 'Path' because it is null", so
  `test_host_skip.ps1` fails before any case (ubuntu-gcc, ubuntu-asan).  The test had never run on a runner.
- **R36bCI-2 (High, merge-blocking).**  On the Windows runner one case of `test_host_skip.ps1` fails: "host.ps1 good
  reply (CRLF dropped by the XML parse): returns the text, no throw"; the other 18 pass, and the first case took
  29 seconds.  It passes locally.  Hypothesis, not checked: the fake `Get-GuestKvp` answers with an id stamped
  `HHmmss` in `Run` before `vmx` makes its own, so a second boundary between the two makes the ids differ.
- **R36bCI-3 (High, merge-blocking).**  `trace-gate-meta-derived` ended after 87 minutes (limit 150) with the
  annotation "The hosted runner lost communication with the server ... starves it for CPU/Memory", and no log was
  uploaded.  The shard's load (workers and their copies) is more than a hosted runner holds.

These are the wave 36c brief.
