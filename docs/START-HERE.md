# START HERE — MS Project → native port

_Updated 2026-10-23 ~17:05, wave 22 in progress.  Keep this file current at
every wave close; it is the only file a new session must read first._

## The 30-second state

* `main` = **2911c40**, pushed.  **Wave 22 is in progress** — no
  `docs/handoff/wave22-complete.md` yet.  Brief:
  `docs/handoff/wave22-brief.md`.
* Wave 21 is closed (`docs/handoff/wave21-complete.md`): stored table, operator
  fixes, CFB slack, parallel Linux; CI run 39904570421 green on all three with
  the three packages and 9/9 parity.
* Wave 22 lanes: **R6** (the seven ribbon tab inventories, decision 1),
  **S6** (stored-column values: Pre/Suc/Resource Names/% Complete/SUMMARY/Yes),
  **E7** (the project's new-task mode, reader + status bar).
* Engine stays **7/8** by design until the MSO save-time word reaches the
  production save path.

## Reading order

**Coordinator for wave 22** (≈5 files, not more):

1. this file;
2. `docs/handoff/wave22-brief.md` — the current wave's scope, owners and gates;
3. `docs/ui-parity-spec.md` — the binding 16-decision UI contract;
4. `docs/ui-reference-set.md` — the clean per-tab captures R6 measures from;
5. `memory/coordinator.md` — the compact current memory.

**Lane agent** (≈4 files):

1. this file;
2. `docs/handoff/wave21-brief.md`;
3. your lane's `docs/handoff/<lane>.md` and `memory/<lane>.md`;
4. the code you own.

**Do not read all of `docs/handoff/`** (63 files, 770 KB).  Use
`docs/handoff/INDEX.md`: current wave first, recent waves next, history only when
investigating a specific decision.

## On demand (and what it is for)

| file | use it when |
|---|---|
| `docs/engine-parity.md` | engine scores and measured rules; read only the "Wave 20/21" sections unless the task is engine work |
| `docs/parity.md` | UI measurement contract and the P1 harness numbers (sections 1–3, 8) |
| `docs/ui-qt-port.md` | the three-platform fidelity contract (Fusion, fonts, print, layout) |
| `docs/ui-reference-set.md` | the operator's local MS Project captures (`corpus/reference/…`, git-ignored) |
| `docs/handoff/INDEX.md` | which handoff covers what, and which are historical |
| `docs/roadmap.md` | effort-weighted progress (computed sum, currently ~78 %) |
| `docs/status.md`, `docs/scope.md` | what is built vs planned, at the stage level |
| `docs/build.md`, `CMakeLists.txt` | build options and targets |
| `docs/import-corpus.md` | the real-.mpp corpus, mutations, robustness |
| `docs/coordination.md` | oracle rules (COM is coordinator-only, the lock, FileClose) |
| `docs/history-note.md` | why old commit hashes in old handoffs are dangling |
| `memory/README.md` | which per-stream memory to read for a given area |

Archives (only for history): `docs/archive/coordinator-handoff-through-wave20.md`,
`docs/archive/WAVES-coordination-board-wave7.md`,
`memory/archive/coordinator-through-wave20-full.md`.

## House rules that are easy to get wrong

* **Fixtures:** never open a committed fixture in place (Project rewrites files
  it opens); copy into `build/`.  `python scripts/check_fixtures.py` before every
  commit.  Nothing under `tests/data` is written by a lane.
* **`plan_recalc`** is never run casually: it rewrites
  `tests/data/project_a_recalc_golden.tsv`.  Use
  `plan_recalc_tests` with `MPP_NO_CORPUS=1`, or leave it to the coordinator.
* **Oracle/COM** is coordinator-only, under `scripts/oracle_lock.py`, with
  `FileClose(0)` and the post-close file as the only evidence.
* **`corpus/`** and `corpus/imported/` are git-ignored: nothing from them is
  committed, and the operator's screenshots are never quoted for client data.
* **Builds:** serial only (`cmake --build … *> file`); `--parallel`/MSBuild `/m`
  and piping the build fail in the sandbox.  Local Qt 6.5.3 is in
  `build/Qt/6.5.3/6.5.3/msvc2019_64`.
* **CI:** one dispatch per milestone; red runs are triaged, fixed and dispatched
  again with the reason recorded.  Push with
  `-c http.sslBackend=openssl` + `GIT_ASKPASS=build/gh-askpass.bat` +
  `GIT_TOKEN=$(gh auth token)`.
* **Commits:** one lane per commit, honest numbers; no "8/8" from a harness
  patch, no test claimed unless run.

## Start a session with this (paste)

```
Read docs/START-HERE.md, then continue wave 22 from the current state.
Wave 22 is in progress (R6 ribbon inventories, S6 stored-column values, E7
new-task mode); do not close it until every lane is committed, the gates are
green, one CI dispatch is green on Windows/macOS/Linux, and
docs/handoff/wave22-complete.md is written.  Use docs/handoff/INDEX.md instead
of reading all handoffs.
```
