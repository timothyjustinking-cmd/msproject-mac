# START HERE — MS Project → native port

_Updated 2026-10-23, wave 23 closed on branch `wave23` (not merged).  Keep this
file current at every wave close; it is the only file a new session must read
first._

## The 30-second state

* **Wave 24 is running on branch `wave24`** (cut from local `wave23` d236f91),
  from `docs/handoff/wave24-brief.md`.  It exists because the verifier found
  wave 23 **not ready to merge**: a Critical row-index bug (Custom Fields edits and
  saves the task below the selection), the project summary row forced on although
  the file stores it (214/Props 0x03400009), CI dropping the corpus-free
  plan_recalc checks - see `docs/handoff/wave23-verification/`.  **Nothing is
  pushed until Tim rules on the client data already on the public repo** (brief
  section 0).
* `main` = **488481f** (wave-22 docs close); the wave-23 code is on **`wave23`**,
  **not merged**.  Remote `wave23` = `63cd087` (qt-shell green, core red on a
  GCC comment since fixed); the review-fix and docs-close commits are LOCAL
  only because the push was held by the client-name policy classifier — see
  wave23-complete §6 "Publication hold".  **This first needs Tim's decision.**
  Then he merges after reading `docs/handoff/wave23-complete.md` (the gate).
* Wave 23 answered the audit: **ID-order rows with the outline and project
  row 0** (B-1), **SUMMARY decoded** (Flag10 = FixedMeta +25 bit 0x04), the
  **link text pinned from Project's COM strings**, an **independent ribbon
  pin**, a **read-only recalc golden**, **CI covering the core lane**, and a
  **parity gate measured against Project's own capture** (numbers-only, per the
  operator's no-screenshots ruling).
* **Deliberately not done** (see wave23-complete §1): the stored timescale and
  bar-style decode (3/4), distinct icon glyphs (14), ribbon control kinds (15),
  the 11-page Options and B-14 dialogs (16), live dark chrome (18), split view
  and the remaining dialogs (20), the new-task writer/macOS/sf248 (21), and the
  clipboard display-order limit (22).
* Engine stays **7/8**: `constraint-mso` is deliberately left failing;
  `mppwrite::save_plan` is test-only (no app/, app_qt/ or tools/ caller).
* One CI dispatch per workflow on `wave23`: qt-shell **39945118772**, core
  **39945123565** (plus the workflow_run ci pass recorded in the close).

## Reading order

**Coordinator starting wave 24** (≈5 files):

1. this file;
2. `docs/handoff/wave23-complete.md` — the gate: fixed/deferred table, gates,
   the live comparison, the re-scored 16 decisions;
3. `docs/handoff/wave23-brief.md` (the work order) and
   `docs/handoff/audit-w22-findings.md` (what it answered);
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
| `docs/parity.md` | UI measurement contract and the P1 harness numbers |
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
* **Fixtures:** never open a committed fixture in place (Project rewrites files
  it opens); oracle scripts use `oracle_lock.working_copy()`.  Run
  `python scripts/check_fixtures.py` before every commit.  Nothing under
  `tests/data` is written by a lane.
* **`plan_recalc`** never rewrites the golden by default; refreshing needs
  `PLAN_RECALC_UPDATE_GOLDEN=1`, and the gate runs `ctest -E plan_recalc` or
  `MPP_NO_CORPUS=1`.
* **Oracle/COM** is coordinator-only, under `scripts/oracle_lock.py`, with
  `FileClose(0)` and the post-close file as the only evidence.
* **`corpus/`** and `decompiled/` are git-ignored: nothing from them is
  committed, and the operator's captures are never quoted for client data.
* **Builds:** serial only; local Qt 6.5.3 in `build/Qt/6.5.3/6.5.3/msvc2019_64`.
  Qt ctest now prepends its own DLL directory, so no PATH setup is needed.
* **CI:** one dispatch per milestone; red runs are triaged, fixed and dispatched
  again with the reason recorded.  Push with `-c http.sslBackend=openssl`,
  `GIT_ASKPASS=build/gh-askpass.bat`, `GIT_TOKEN=$(gh auth token)`.
* **Commits:** one lane per commit, honest numbers; no "8/8" from a harness
  patch, no test claimed unless run.

## Start a session with this (paste)

```
Read docs/START-HERE.md and docs/handoff/wave23-complete.md.  Merge wave23 to
main if Tim asks; otherwise start the next wave from the deferred list in
wave23-complete §1, beginning with the bar-style/timescale decode and the
distinct icon set.  Keep every gate in wave23-complete §2 green before each
commit.
```
