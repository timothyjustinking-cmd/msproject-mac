# START HERE — MS Project → native port

_Updated 2026-10-23 ~17:55, wave 22 closed.  Keep this file current at
every wave close; it is the only file a new session must read first._

## The 30-second state

* `main` = **abeafd2** (the code CI validated; the docs close follows), pushed.
  **Wave 22 is closed** — `docs/handoff/wave22-complete.md` is the gate.
* CI run **39912313201** green on Windows, macOS and Linux (14/14 each) with the
  three packages and 9/9 parity on all three renders.
* Wave 22 landed: the **measured seven-tab ribbon**, the **stored columns'
  values** (Pre/Suc/Resource Names/% Complete) and the **new-task scheduling
  mode** decoded from props `0x024013c8`.
* Engine stays **7/8** by design until the MSO save-time word reaches the
  production save path.
* **Wave 22 was audited on 2026-10-23** (`docs/handoff/audit-w22-findings.md`):
  the scheduling numbers reproduce, but the loaded file renders in the wrong
  task order with no outline (B-1), "9/9 UI parity" and S6's "independent
  oracle" do not test what they claim (A-1, A-2), and the 16-decision scoreboard
  is re-scored there.  Read it before trusting any number above.
* **Wave 23 = the audit work order:** `docs/handoff/wave23-brief.md`.  It is
  the only brief the wave-23 coordinator reads; it carries the fix list in order,
  the verifier corrections, the gate, and the wave-22 ranked items at the end.

## Reading order

**Coordinator starting wave 23** (≈5 files, not more):

1. this file;
2. `docs/handoff/wave23-brief.md` — the work order (wave 23's scope is here), then
   `docs/handoff/audit-w22-findings.md` — the audit it comes from;
   `docs/handoff/wave22-complete.md` is the last gate, read for history;
3. `docs/ui-parity-spec.md` — the binding 16-decision UI contract;
4. `docs/ui-reference-set.md` — the clean per-tab captures and palettes;
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
Read docs/START-HERE.md, then docs/handoff/wave23-brief.md and run it as the
wave-23 coordinator on branch wave23: the W23 items in order, each committed with
its finding id, every gate in section 1 green before each commit, one CI dispatch
on the branch at the end, no merge to main.  Write docs/handoff/wave23-complete.md
with the fixed/deferred table, the live same-file comparison and the re-scored
16 decisions.  Use docs/handoff/INDEX.md instead of reading all handoffs.
```
