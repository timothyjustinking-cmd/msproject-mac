# START HERE: MS Project to native port

_Updated 22 November 2026, wave 36c at its CLOSE on branch `wave36c` (cut from `wave36b`; `wave36b` from
`wave36`; `wave36` from `main` at `b2c0b19`).
Keep this file under 8 KB and current at every wave close; it is the only file a new session must
read first.  Wave history is in `docs/handoff/history-waves34-35.md`: read it only to investigate
a decision; its last section, "Standing rules and standing facts", holds the writer inventory (which
edits save, which are refused), the local Qt path and the house rules of wave 35g._

## State

* **`main` = `b2c0b19`**: waves 23 to 35g merged, CI green on all seven jobs, 0 Critical and 0 High
  open, 4(e) 7 checked and 3 not (`docs/handoff/wave35g-verification/`).  Ids in the wave 23 to 32
  records are pre-rewrite: map one through `docs/handoff/wave32-verification/rewrite-sha-map.txt`.
* **History rewrite, Sun 22 Nov 2026:** `main`, `wave36` and `wave36b` were rewritten from 8 Nov 2026
  (the trace notes and the VM's and host's node gone from text; `wave36b-verification/coord-verify.md`
  section 4).  **Commit ids in records before that date name pre-rewrite commits.**
* **Wave 36 (the writer wave, `wave36-complete.md`)** done: the trace gate (G0 to G0f), the font item
  (G1, G1b), Props (G2; the insert is framing decoded, not lifted) and CLOSE.  G3 (Insert Task,
  Milestone, Summary) is not started: no rule predicts a held-out pair.
* **Wave 36b (fix-only, `wave36b-complete.md`)** fixed the wave 36 review's three Highs and three
  Mediums: `save_props_edit` refuses an insert again, no note prints a network address, a missing
  `olefile` fails in CI, the Gantt zoom dates are noise only where a control and its null differ.
* **Wave 36c (fix-only)** fixed the three failures of `wave36b`'s first CI run (`wave36c-brief.md`,
  `wave36c-complete.md`, markers `WAVE-36c-G0-COMPLETE` and `WAVE-36c-COMPLETE`): `host.ps1` takes its
  temp folder from `GetTempPath()`; the fake guest answers with the id `vmx` wrote; the derived meta
  run is four shards, one CI job each, every mutant held to 2 GiB (one candidate took 36 GB), one
  progress line per candidate.  Nothing is pushed: the coordinator verifies, pushes, dispatches CI and
  merges `wave36`, `wave36b`, `wave36c` on the operator's word.
* **Trace notes** (`trace-*.md`, `trace-*.txt`, the runbook) live in the private repo
  `../msproject-mac-trace` (D-8, 22 Nov 2026); a citation by file name refers there.  The one public
  note is `spec/writers/evidence/trace-gate-fields-g0b.md`.
* **The trace gate** `tools/trace/trace_gate.py` compares a traced save with the reference outside named
  noise fields only; a pair differing in an undecoded field FAILs and is OPEN in the test (22 of 202
  control-nulls).  Guards: `test_trace_gate.py` (ctest `trace_gate_guards`),
  `test_host_skip.ps1` (`trace_host_scripts`, needs `pwsh`) and the guard-removal run
  `test_gate_guards_meta.py`: `trace_gate_meta_named` (24 min), four derived shards
  `trace_gate_meta_derived_KofN` (3 workers, about 30 min each), `trace_gate_meta_shards` (the union) and
  `trace_gate_meta_limit`; label `trace_gate_meta`, five CI jobs; the matrix and asan jobs pass
  `-LE trace_gate_meta`.  CI sets `MPP_REQUIRE_OLEFILE=1`; no limit has run on a runner.  The VM runs
  the frozen gate and `host.ps1`: the coordinator copies the new ones in.  Queued, not fixed:
  R36G0f-4 to -7 and -9, R36W-7 to -10, R36bG0-1 to -7 (`plan-waves27-30.md`).
* **Open, no Critical or High:** the Lows R35gG0-1 to -4, R35gG1-1 to -5, R35gW-1 to -5; partly fixed
  or open R35dG2-1, R35cG0-2, R35cG0-4, R35bW-1, R35bW-2, R35bG2-2, R35bG3-1, R35bG0-9, R35bG1-10,
  R35bG2-9.  All are queued in `plan-waves27-30.md`; only the coordinator's review closes an id.
* **Wave 37:** Notes, Set Baseline, linked, lag and constraint Calculate.  **Engine stays 7/8**
  (`constraint-mso` is expected-failing).

## Reading order

**Coordinator or builder starting a group or wave:**

1. this file;
2. `docs/handoff/standing-rules.md` (binds every wave);
3. the current brief, `docs/handoff/wave36c-brief.md`, and its record `wave36c-complete.md`;
4. `memory/coordinator.md` (the compact current memory);
5. `spec/README.md` (the writer decode notes) and `docs/ui-parity-spec.md` when the group needs
   them.

**Lane agent:** this file, the wave brief, your lane's `docs/handoff/<lane>.md` and
`memory/<lane>.md`, the code you own.  **Do not read all of `docs/handoff/`**: use
`docs/handoff/INDEX.md`.

On demand: `docs/build.md` (build commands, the Qt ctest PATH), `docs/coordination.md` (oracle
rules), `docs/engine-parity.md`, `docs/parity.md`, `docs/ui-qt-port.md`, `docs/import-corpus.md`,
`docs/history-note.md`, `memory/README.md`.

## Commands

Run before every commit (all must be 0 hits or green):

```
python -I build/audit-w22/scan_client_names.py HEAD
python -I build/audit-w22/guard_names.py <range>
python scripts/check_markers.py --worktree          (or --range <range>)
python scripts/check_fixtures.py
git grep -nE "client-string-[0-9a-f]{8}"             (expect none)
git diff --check
```

Build and test (serial; `cmake`, `ctest` and `cl` are not on PATH, see `docs/build.md`):

```
cmake --build <tree> --config Release
ctest --test-dir <tree> -C Release -E plan_recalc -j4
MPP_NO_CORPUS=1 ctest --test-dir <tree> -C Release -R plan_recalc
```

`plan_recalc` never rewrites the golden by default (`PLAN_RECALC_UPDATE_GOLDEN=1` does).  CI is
the only gate for `app/main.mm`, `macos-appkit` and the GCC, clang and asan jobs.  One CI
dispatch per milestone; `app-macos.yml` runs on push.

## House rules that are easy to get wrong

* **Never write a client task, resource, project or person name** into any file, comment, test or
  commit message.  Refer to rows by id and strings by an opaque label (`client-name-NN`), never by
  an unsalted hash.  Project screenshots stay local; commit only the numbers
  (`tests/data/reference/*.measures.json`).
* **Fixtures:** never open a committed fixture in place (Project rewrites it); oracle scripts use
  `oracle_lock.working_copy()`.  Nothing under `tests/data` is written by a lane.  New fixtures go
  through Project on a neutral path (`C:/wNN`) with neutral Author, Manager, Company, UserName and
  Title.
* **Oracle and COM are coordinator-only**, under `scripts/oracle_lock.py`, with `FileClose(0)` and
  the post-close file as the only evidence.  Builders use no COM.  Never start, stop or touch the
  trace VM, `vmconnect` or anything under `build/trace-2021/`.
* **Every save path is measured against Project before it is enabled** (standing rule 1), with a
  save-and-reopen test.  A refusal is lifted only by the three-part lift rule in the wave 36
  brief, section 0.  Port tests mask by exact field (record id and offset), never by stream name.
* **Writes:** every `.mpp` write goes through `mppwrite::write_file_atomically`.  Displayed rows
  resolve through their uid (`app::print_row_list`); uid 0 is the project row.
* **Commits:** one finding id per commit; gates per commit under `build/wNN/`; red then green,
  both quoted; a known gap is an expected failure by name, never an assertion of wrong output.
  Trailer `Generated-By: Claude Sonnet 5.5 via Claude Code (wave NN)` on a builder's commits.
  Never rewrite history; push nothing unless the coordinator says.
* **Text files** are UTF-8 with LF (`.gitattributes` pins `*.md`, `*.py`, `*.yml`, `*.cpp`,
  `*.h`; CI fails on a CR).  `corpus/` and `decompiled/` are git-ignored.  `build/` is excluded
  from Dropbox sync.  The shell hook blocks backslashes in heredocs and inline Python: Write a
  script file and run it by path.
* **Privacy residuals the operator owns:** the pre-scrub public history, the unsalted name hashes
  in older history and `tests/data/project_a_golden.tsv`, and `6056f15`'s commit message.

## Start a session with this (paste)

```
Read docs/START-HERE.md, docs/handoff/standing-rules.md and the current brief, then work on
the current wave branch as the coordinator for your group only.  Run the three name scans and
check_markers before every commit.  Push nothing.
```
