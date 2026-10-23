# START HERE — MS Project → native port

_Updated 2026-10-24, wave 24 closed on branch `wave24` (not merged, not
pushed).  Keep this file current at every wave close; it is the only file a new
session must read first._

## The 30-second state

* **Wave 24 is closed on branch `wave24`** from `docs/handoff/wave24-brief.md`;
  the record is **`docs/handoff/wave24-complete.md`** (the gate).  Three local
  commits on top of local `wave23` = `d236f91`: `ca9e7d7` (fixtures/oracles),
  `ff22c7b` (fixes), `cc4ce8a` (CI/harness).  **Nothing is pushed.**
* `main` = **488481f** (wave-22 docs close).  `wave23` is **not merged**;
  `origin/wave23` = `63cd087`.  Remote `wave24` does not exist.
* **Publication hold still in force:** client data is already on the public
  repo; do not push anything and do not rewrite history until Tim rules (brief
  section 0).  CI was therefore **not dispatched** for wave 24; the local gates
  are the evidence (wave24-complete §5).
* Wave 24 fixed the verifier's Critical/High findings: **uid routing for every
  RecordSet row** (Custom Fields/Delete/menu/print/Insert now act on the
  selected task), **the project-summary row and Timeline strip are FILE state**
  (`214/Props` `0x03400009` / `0x03400008`), **CI runs the corpus-free
  plan_recalc checks**, the engine lane pins **`constraint-mso` by name**, the
  ID sort is exercised by a stored-order fixture, the Pre/Suc golden is
  per-row, the reader decodes **Flag1..Flag20**, and the tracked tree is
  **scrubbed of client strings** (scan: 0 hits).
* Gates at the close: core **51/51**, plan_recalc **266** no-corpus / **512**
  with corpus, M2 **332/332**, engine **7 pass + constraint-mso expected**,
  app **15/15**, app_qt **18/18**, fixtures **OK** (11 hashes, 10 `.mpp`),
  parity **10/10**, reader **1054** with corpus / **377** without.
* Engine stays **7/8**: `constraint-mso` is deliberately left failing;
  `mppwrite::save_plan` is test-only.
* Still deferred (wave24-complete §3): the FILE's own text/bar styles
  (W24-13/18 partial), the Custom Fields definition dialog (W24-14), View >
  Task Views / Resource Views drop-downs (W24-12 partial), and the carried
  W24-17/19/20/21/22/23/24/25 streams.

## Reading order

**Coordinator starting wave 25** (≈5 files):

1. this file;
2. `docs/handoff/wave24-complete.md` — the gate: fixed/deferred table, gates,
   the live comparison, the 16 decisions;
3. `docs/handoff/wave24-brief.md` (the work order) and
   `docs/handoff/wave23-verification/` (what it answered);
4. `docs/ui-parity-spec.md` — the binding 16-decision UI contract;
5. `docs/ui-reference-set.md` — the local captures and palettes (git-ignored);
6. `memory/coordinator.md` — the compact current memory.

**Lane agent** (≈4 files):

1. this file;
2. `docs/handoff/wave21-brief.md`;
3. your lane's `docs/handoff/<lane>.md` and `memory/<lane>.md`;
4. the code you own.

**Do not read all of `docs/handoff/`.**  Use `docs/handoff/INDEX.md`: current
wave first, recent waves next, history only when investigating a decision.

## On demand (and what it is for)

| file | use it when |
|---|---|
| `docs/engine-parity.md` | engine scores; `constraint-mso` is deliberately left failing |
| `docs/parity.md` | UI measurement contract and the parity harness numbers |
| `docs/ui-qt-port.md` | the three-platform fidelity contract |
| `docs/build.md` | build commands, the Qt ctest PATH (now automatic), the golden opt-in |
| `docs/import-corpus.md` | the real-.mpp corpus, mutations, robustness |
| `docs/coordination.md` | oracle rules (COM is coordinator-only, the lock, FileClose) |
| `docs/history-note.md` | why old commit hashes in old handoffs are dangling |
| `memory/README.md` | which per-stream memory to read for an area |
| `analysis/project_property_names.tsv` | derived Project property-name table (QNEWTASKSAREMANUAL = index 64) |
| `docs/analysis/newtasks-scan-summary.md` | the committed E7 scan counts |

Archives (only for history): `docs/archive/coordinator-handoff-through-wave20.md`,
`docs/archive/WAVES-coordination-board-wave7.md`,
`memory/archive/coordinator-through-wave20-full.md`.

## House rules that are easy to get wrong

* **Project screenshots stay local** (operator ruling 2026-10-23; a pre-push
  guard enforces it).  Commit only the numbers measured from them:
  `tests/data/reference/*.measures.json`.
* **Never write a client task/resource/project/person name** into any file,
  comment, test or commit message.  Refer to rows by ID and strings by hash.
  The tree is clean as of `ff22c7b`; run
  `python -I build/audit-w22/scan_client_names.py HEAD` before every commit and
  expect 0 hits.
* **Fixtures:** never open a committed fixture in place (Project rewrites files
  it opens); oracle scripts use `oracle_lock.working_copy()`.  Run
  `python scripts/check_fixtures.py` before every commit.  Nothing under
  `tests/data` is written by a lane.  New fixtures are built through Project on
  a NEUTRAL path (`C:/w24`) with neutral Author/Manager/Company/UserName.
* **`plan_recalc`** never rewrites the golden by default; refreshing needs
  the exact value `PLAN_RECALC_UPDATE_GOLDEN=1`, and the local/CI gate runs
  `ctest -E plan_recalc` plus the corpus-free binary/step.
* **Oracle/COM** is coordinator-only, under `scripts/oracle_lock.py`, with
  `FileClose(0)` and the post-close file as the only evidence.
* **`corpus/`** and `decompiled/` are git-ignored: nothing from them is
  committed, and the operator's captures are never quoted for client data.
* **Builds:** serial only; local Qt 6.5.3 in `build/Qt/6.5.3/6.5.3/msvc2019_64`.
  Qt ctest prepends its own DLL directory.
* **CI:** one dispatch per milestone; red runs are triaged, fixed and dispatched
  again with the reason recorded.  **Wave 24's end-of-wave CI was not run
  because of the publication hold.**
* **Commits:** honest numbers; no "8/8" from a harness patch, no test claimed
  unless run.

## Start a session with this (paste)

```
Read docs/START-HERE.md and docs/handoff/wave24-complete.md.  Wave 24 is closed
on branch wave24 (not merged, not pushed; publication hold).  If Tim has ruled
on the client data, merge/handle the branches as he directs; otherwise continue
from wave24-complete §3 deferred list, beginning with the FILE's own text and
bar styles (W24-13/18), then the Custom Fields definition dialog (W24-14) and
the timescale tiers (W24-17).  Keep every gate in wave24-complete §2 green before
each commit and the client-name scan at 0 hits.
```
