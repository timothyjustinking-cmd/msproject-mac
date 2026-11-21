# START HERE: MS Project to native port

_Updated 22 November 2026, wave 36 at its CLOSE on branch `wave36` (cut from `main` at `b2c0b19`).
Keep this file under 8 KB and current at every wave close; it is the only file a new session must
read first.  Wave history is in `docs/handoff/history-waves34-35.md`: read it only to investigate
a decision.  The writer inventory (which edits save, which are refused), the local Qt path and the
house rules of wave 35g are the last section of that file, "Standing rules and standing facts"._

## State

* **`main` = `b2c0b19`**: waves 23 to 35g are merged, CI green on all seven jobs, 0 Critical and
  0 High open, 4(e) 7 checked and 3 not checked (`docs/handoff/wave35g-verification/`).  Ids in the
  wave 23 to 32 records are pre-rewrite: map one through
  `docs/handoff/wave32-verification/rewrite-sha-map.txt` (left old, right current).
* **Wave 36 (the writer wave)** puts the Project 2021 trace's rules into the writers.  Brief
  `docs/handoff/wave36-brief.md`, record `docs/handoff/wave36-complete.md`, builder Claude Sonnet
  5.5, nothing pushed.  Done: G0 to G0f (the trace gate and the records), G1 and G1b (the font item),
  G2 (Props; closes R35cG0-4; the Props insert is framing decoded, not lifted: Project's reopen shows no
  baseline and no note, W36b-0.1) and CLOSE (the G0f review's R36G0f-1 to -3 and -8, fixed in `de97271`,
  `1a581b2`, `79109bb`, `429f241`); the markers are in `wave36-complete.md`.  G3 (Insert Task,
  Milestone, Summary) is not started: no rule predicts a held-out pair (`trace-insert-rules-r6.md`;
  the five `wave36-r7-insert-*` recordings are committed but no note derives or checks a rule on them).
* **Trace notes** (`trace-*.md`, `trace-*.txt`, the runbook) live in the private repo beside this one,
  `../msproject-mac-trace` (D-8, Sun 22 Nov 2026); a citation by file name refers there.  The one public
  note is `spec/writers/evidence/trace-gate-fields-g0b.md`, the gate's field evidence.
* **The trace gate** `tools/trace/trace_gate.py` compares a traced save with the reference outside named
  noise fields only; each mask names a field and checks its value; a pair whose control or edit differs in
  an undecoded field FAILs and is OPEN in the test (22 of 202 control-nulls, 107 rows pinned).  A creation
  stamp is masked only in a row the control lacks, so the VM recordings that rewrite a shared row's stamp
  FAIL as data (R36G0f-2).  `tools/trace/test_trace_gate.py` (ctest `trace_gate_guards`),
  `test_gate_guards_meta.py` (ctest `trace_gate_meta`, about 75 minutes: removes each guard and expects red;
  CI job `trace-gate-meta`; the matrix and asan jobs pass `-LE trace_gate_meta`) and `test_host_skip.ps1`
  (ctest `trace_host_scripts`, needs `pwsh`) guard it; CI jobs `pip install olefile`, not yet run on a
  runner.  The VM still runs the frozen gate and `host.ps1`: the coordinator copies the new ones in and
  re-gates (R36G0b-8 needs a real recording).  Queued, not fixed: R36G0f-4 to -7 and -9 (the VM's newer
  saves lie above three spans: 108 of 210 PASS).
* **Open, no Critical or High:** the Lows R35gG0-1 to -4, R35gG1-1 to -5 and R35gW-1 to -5;
  partly fixed or open R35dG2-1, R35cG0-2, R35cG0-4 (wave 36 G2), R35bW-1, R35bW-2, R35bG2-2,
  R35bG3-1, R35bG0-9, R35bG1-10, R35bG2-9.  All are queued in `docs/handoff/plan-waves27-30.md`.
  Only the coordinator's review closes an id.
* **Not in wave 36 (wave 37):** Notes, Set Baseline, linked, lag and constraint Calculate.
* **Engine stays 7/8** (`constraint-mso` is deliberately expected-failing).

## Reading order

**Coordinator or builder starting a group or wave:**

1. this file;
2. `docs/handoff/standing-rules.md` (binds every wave);
3. the current brief, `docs/handoff/wave36-brief.md`, and its record `wave36-complete.md`;
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
Read docs/START-HERE.md, docs/handoff/standing-rules.md and docs/handoff/wave36-brief.md, then
work on branch wave36 as the coordinator for your group only.  Run the three name scans and
check_markers before every commit.  Push nothing.
```
