# START HERE — MS Project → native port

_Updated 2026-10-24, wave 25 closed on branch `wave25` (not merged, not
pushed).  Keep this file current at every wave close; it is the only file a new
session must read first._

## The 30-second state

* **Wave 25 is closed on branch `wave25`** (cut from local `wave24` =
  `2561a51`) from `docs/handoff/wave25-brief.md`; the record is
  **`docs/handoff/wave25-complete.md`** (the gate).  It fixed the verifier's
  two High findings and every Medium/Low gate hole (W25-1..W25-11); the
  carried parity streams (W25-12..W25-21) are **deferred**, listed with reasons
  in wave25-complete §3.  The publication hold still applies: nothing pushed.
* **W25-1**: every displayed Gantt row now resolves through its uid (the
  RecordSet row is never a plan index); uid 0 (the project row) is refused by a
  shared edit-entry guard on both shells.  The new `app_layout` test on
  `tests/data/wave24-order.mpp` fails five checks under the old arithmetic.
* **W25-2**: the "stock" bar palette is re-measured per scheduling mode.  Auto
  task = (138,187,237), manual task = (116,204,212), auto summary (84,84,84),
  manual summary (43,163,173), milestone black; Critical Tasks ON (not the
  default and not wired) = (247,138,136), links (221,37,34).  A committed
  auto-scheduled parity reference fails a port that calls the manual fill
  "task".
* **W25-3**: every `client-string-<hex8>` label and every person-name hash pin
  is gone from the tree; the writer oracle is regenerated from a synthetic
  fixture.  The second review found and the wave removed the one remaining
  plaintext client name (a dead row's orphaned name block in the reader test;
  the scanner only sees live rows).  The name scan and the 8-hex grep are clean
  from the scrub commit onward.  **Residual:** `tests/data/project_a_golden.tsv` still carries an
  unsalted 16-hex hash of each task name (the reader's oracle) -- wave25-complete
  §5 records why it was not replaced and that it is Tim's call with the hold.
* **W25-4..W25-11**: wave-24 errata recorded, the PROJECT_A text-style gap is
  two named expected failures, oracle provenance is reproducible and checked,
  the ribbon-kind gate reads live widgets, the corpus row count is pinned, the
  Flag11..17 single-bit fixtures and the generator are committed, the Timeline
  shown/hidden pair is complete, and the mis-renames/stale comment/PDF subtitle
  are fixed with R16/R17 dispositions.
* **Wave 24 is closed on branch `wave24`**; the verifier found it not ready to
  merge (wave25 answers it).  `main` = **488481f**.  `wave23`/`wave24` are not
  merged; `origin/wave23` = `63cd087`; remote `wave24`/`wave25` do not exist.
* **Publication hold still in force:** client data is already on the public
  repo; do not push anything and do not rewrite history until Tim rules (brief
  section 0).  CI was therefore **not dispatched** for wave 25; the local
  fresh-tree gates are the evidence (wave25-complete §9).
* Gates at the wave-25 close (fresh trees, final commit): core **51/51**,
  plan_recalc **512** with corpus / **266** without, M2 **332/332**, engine
  **7 pass + `constraint-mso` expected**, app **15/15**, app_qt **18/18**,
  reader **1100** with corpus / **420** without, fixtures **OK** (23 documented
  hashes, 21 tracked `.mpp`), parity **10/10** plus the auto-scheduled
  colour-only **PASS**, client-name scan **0 hits** on every wave-25 commit.
* Engine stays **7/8** (`constraint-mso` deliberately left failing;
  `mppwrite::save_plan` is test-only).
* Parity still deferred (wave25-complete §3): the FILE's text styles (W25-12),
  bar styles (W25-13), timescale tiers (W25-14), View drop-downs (W25-15),
  Custom Fields dialog (W25-16), icons (W25-17), ribbon kinds (W25-18),
  backstage/Options/B-14 (W25-19), dark theme (W25-20), remaining dialogs and
  the split view (W25-21).

## Reading order

**Coordinator starting wave 26** (≈5 files):

1. this file;
2. `docs/handoff/wave25-complete.md` — the gate: fixed/deferred table, the
   wave-24 errata, R16/R17 dispositions, the live comparison, the fresh-tree
   gates, the 16 decisions;
3. `docs/handoff/wave25-brief.md` (the work order) and
   `docs/handoff/wave24-verification/` (what it answered);
4. `docs/ui-parity-spec.md` — the binding 16-decision UI contract;
5. `docs/ui-reference-set.md` — the local captures and palettes (git-ignored);
6. `memory/coordinator.md` — the compact current memory.

**Lane agent** (≈4 files):

1. this file;
2. `docs/handoff/wave25-brief.md`;
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
  comment, test or commit message.  Refer to rows by ID and strings by opaque
  label (`client-name-NN`) or structural relation -- never by an unsalted hash,
  which a guess inverts.  Run
  `python -I build/audit-w22/scan_client_names.py HEAD` before every commit and
  expect 0 hits; also `git grep -nE "client-string-[0-9a-f]{8}"` expect none.
* **Fixtures:** never open a committed fixture in place (Project rewrites files
  it opens); oracle scripts use `oracle_lock.working_copy()`.  Run
  `python scripts/check_fixtures.py` before every commit (it also checks the
  scenario oracle provenance).  Nothing under `tests/data` is written by a lane.
  New fixtures are built through Project on a NEUTRAL path (`C:/w25`) with
  neutral Author/Manager/Company/UserName.
* **`plan_recalc`** never rewrites the golden by default; refreshing needs
  the exact value `PLAN_RECALC_UPDATE_GOLDEN=1`, and the local/CI gate runs
  `ctest -E plan_recalc` plus the corpus-free binary/step.
* **Oracle/COM** is coordinator-only, under `scripts/oracle_lock.py`, with
  `FileClose(0)` and the post-close file as the only evidence.
* **`corpus/`** and `decompiled/` are git-ignored: nothing from them is
  committed, and the operator's captures are never quoted for client data.
* **Builds:** serial only; local Qt 6.5.3 in `build/Qt/6.5.3/6.5.3/msvc2019_64`.
  Qt ctest prepends its own DLL directory.  Two transient Dropbox/MSBuild file
  locks hit again this wave; retry the build rather than changing CMake.
* **CI:** one dispatch per milestone; red runs are triaged, fixed and dispatched
  again with the reason recorded.  **Wave 25's end-of-wave CI was not run
  because of the publication hold.**
* **Commits:** one finding id per commit; honest numbers; a gate number must
  belong to the tree it is written in and name its log under `build/w25/`; no
  test claimed unless run.  A known gap is an expected failure **by name**, never
  an assertion of the wrong output.

## Start a session with this (paste)

```
Read docs/START-HERE.md and docs/handoff/wave25-complete.md.  Wave 25 is closed
on branch wave25 (not merged, not pushed; publication hold).  If Tim has ruled
on the client data, merge/handle the branches as he directs; otherwise continue
wave25-complete §3's deferred parity list, beginning with the FILE's text
styles (W25-12, which turns the two named expected failures in
qt_stored_table_corpus green), then the file bar styles (W25-13) and the
timescale tiers (W25-14).  Keep every gate in wave25-complete §9 green before
each commit and the client-name scan at 0 hits.
```
