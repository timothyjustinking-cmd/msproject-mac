# START HERE — MS Project → native port

> **Interim note, 29 Oct 2026 (audit C-1):** the branch state below is STALE.  Waves 23-32
> were merged into `main` on 27 Oct 2026 as `17f5782` after a history rewrite; GitHub
> holds only `main`.  Wave 33 works on branch `wave33` cut from it.  Trust `git` and
> `docs/handoff/assessment-2026-10-29.md` until wave 33 G4 (W33-4.1) rewrites this file.

_Updated 2026-10-27, wave 32 closed on branch `wave32` (a short fix-only wave
that closes the wave-31 verifier's three High findings; pushed; CI recorded in
the record's Push and CI section).  Keep this file current at every wave close;
it is the only file a new session must read first._

## The 30-second state

* **Wave 32 is closed on branch `wave32`** (cut from `wave31`'s verification
  commit `3a18e64`) from `docs/handoff/wave32-brief.md`.  It ran as G0 + CLOSE,
  one fresh session each; the record is
  **`docs/handoff/wave32-complete.md`** (the gate).  Every item is DONE; none was
  deferred.  The final code tree is `7727760`; the CLOSE fixes are W32-C1..W32-C8
  (three stale-record fixes and the independent review's High and four Mediums)
  and the CLOSE docs commit after them changes docs only.
* **G0 closed the wave-31 verifier's three Highs, refuse-first.**  W32-0.1
  (R31-1): an automatic outline edit and a queued percent, actuals or Start edit
  refuse each other in both orders, on every row, and `save_structural_edits`
  repeats the refusal before the first write.  W32-0.2 (R31-2): Start is
  read-only at every shell entry (the sheet column, Task Information, the Task
  Form, a pasted or cut block); the save funnel and `push` refuse a collected or
  writable Start edit; the writer stays in the tree for a later measured wave.
  W32-0.3 (R31-3, with R31-4 folded in): while a sort reorders the rows or a
  filter is active, a range Copy/Cut, a multi-row Paste and Shift-click range
  extension are disabled and refused whole; Indent, Outdent and Insert are
  refused under a reordering sort ("clear the sort first").  Single-cell edits
  stay, mapped from the displayed row to its record.
* **No writer was added and no Project pair was measured.**  This wave refuses
  or disables only; the Start writer, Fill and the session-only view state are
  unchanged.  Wave 31 added no writer either.
* **CLOSE fixed three stale records** (W32-C1..W32-C3): the wave-31 queued-
  findings note in `plan-waves27-30.md` is LF again after `b16a9fa` rewrote the
  file with CRLF; the regenerated backlog now states the six refused commands'
  session refusals and gives Start editing its own field-refusal row (Fill keeps
  its W31-1 measurement reason); and the Task Information hint, the `dialogs.h`
  comment and the wave-19 `docs/parity.md` record no longer say Start is
  written.
* **The independent review found 0 Critical, 1 High, 4 Medium, 6 Low.**  The
  High (W32-C4) was a `size_t`/`int` mismatch in `save_reopen_test.cpp` that
  would have failed all three `qt.yml` jobs under warnings-as-errors; the local
  CLOSE run had hidden it because app/app_qt were configured without the flag,
  which the fresh runner now does.  The four Mediums it marked required are
  fixed: the order-1 real-actuals test (W32-C5), the macOS save-funnel Start
  refusal and the stale Start-save texts in both shells (W32-C6), the ribbon
  Insert/Indent/Outdent handler clicks under the sort (W32-C7), and the
  recorded reason the save-level percent/actuals defence cannot be reached
  through `push()` (W32-C8).  The six Lows are queued with directions in the
  record.
* **Fresh-tree gates on `7727760`** (log `build/w32/W32-CLOSE-fresh-gates.log`,
  three trees configured from scratch with `CORE_WARNINGS_AS_ERRORS=ON` on
  core, app and app_qt as CI does; the CLOSE docs commit after it changes docs
  only): core configure/build exit 0; core ctest `-E plan_recalc` **52/52**;
  `plan_recalc` **512** with corpus / **266** without; engine selftest **53
  checks, 0 failures** and the suite **7 pass + `constraint-mso` expected**, 8
  recorded, 0 unrecorded; reader **1136** with corpus / **456** without; M2
  **332/332**, **268/268** and **64/64**; app **25/25** and
  `app_edits_outline_tests` **3230 checks**; app_qt **37/37**; fixtures **OK**
  (139 documented hashes, 173 tracked `.mpp`), paths OK (222), oracle
  provenance **OK (8)**.  The earlier run on `64fa2fc` is preserved as
  `build/w32/W32-CLOSE-fresh-gates-64fa2fc.log`.
* **Privacy**: the line-based `scan_client_names.py HEAD --messages
  wave31..HEAD` reports **0 files / 0 hits** and **0 commit messages**; the
  whitespace-normalised `guard_names.py` is **0 hits, exit 0** on
  `wave31..HEAD` and on the push range `HEAD --not --remotes=origin`; the hex
  grep has no matches (exit 1, expected).  Wave 32 adds no fixture.  The two
  pre-wave fixtures and 69 tracked text files still carry the operator's name,
  the 13 unsalted hashes remain in older history, and R31-11 (one wave-31 brief
  line) stays queued — the operator's decision.  **Run BOTH scans.**
* **Push and CI**: the code tree is pushed without force after a clean
  preflight, and both workflows are dispatched explicitly on `wave32` (the
  `workflow_run` trigger only fires from `main`); the run ids and the final
  status are in `wave32-complete.md`'s Push and CI section.  The CLOSE docs
  commit after the dispatch changes docs only and is pushed the same way.
* **Still open** (wave-32 CLOSE "Found, not fixed"): the wave-31 review's
  R31-5..R31-12 remain queued in `plan-waves27-30.md` (R31-4 is closed by
  W32-0.3); the G0 per-commit logs did not capture exit lines and the G0 guard
  log is empty (errata in the record); the independent review's six Lows
  (L1..L6: forced-repaste and range-cut assertions, the Task Information order,
  the filter wording, the brief's own path, the Qt funnel refusal) are queued
  with directions; and the wave-31 "Found, not fixed" list stands — the four core review Lows, the G3/G4 presentation gaps, the carried
  wave-30/29 queues and the after-run list (Team Planner, Network Diagram,
  Calendar and Report views, the `constraint-mso` engine case, macOS
  launch/notarisation, the hashed task names, the remaining review Lows).
* **Branch state**: `main` = **fcb1c10** = `origin/main`.  `wave23`
  (`e1d40b8` = `origin/wave23`), `wave24` (`6dbf126`), `wave25` (`c012813`),
  `wave26` (`1299cc7` = `origin/wave26`), `wave27` (`bc52cb8` =
  `origin/wave27`), `wave28` (`21646f4` = `origin/wave28`), `wave29`
  (`2a7fa4b` = `origin/wave29`), `wave30` (`ca9fd50` = `origin/wave30`) and
  `wave31` (`origin/wave31` = its CLOSE docs commit, final tree `072773b`) are
  closed and **not merged**.  **`wave32` is closed, pushed and not merged**
  (`origin/wave32` = the CLOSE docs commit, final code tree `7727760`).  The
  next step is the operator's: verify and merge wave23..wave32, then choose the
  next scope.
* Engine stays **7/8** (`constraint-mso` deliberately left failing;
  `mppwrite::save_plan` is test-only).

## Reading order

**Coordinator starting the next wave** (≈5 files):

1. this file;
2. `docs/handoff/wave32-complete.md` — the gate: G0 + CLOSE, the CLOSE fixes,
   the errata, the fresh-tree gates, both privacy scans, the independent review,
   the push and the CI runs, "Found, not fixed" and what the end of the run
   leaves;
3. `docs/handoff/wave32-brief.md` (the work order);
4. `docs/ui-parity-spec.md` — the binding 16-decision UI contract;
5. `docs/ui-reference-set.md` — the local captures and palettes (git-ignored);
6. `memory/coordinator.md` — the compact current memory.

**Lane agent** (≈4 files):

1. this file;
2. the current wave's brief (`docs/handoff/waveNN-brief.md`);
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

## Standing rules (from wave 27)

`docs/handoff/standing-rules.md` binds every wave: save paths measured against
Project before they are enabled, small working context, per-commit incremental
gates with full fresh-tree gates per group, CI every wave, one independent review
per wave, one finding id per commit.  `build/` is excluded from Dropbox sync.

## House rules that are easy to get wrong

* **Project screenshots stay local** (operator ruling 2026-10-23; a pre-push
  guard enforces it).  Commit only the numbers measured from them:
  `tests/data/reference/*.measures.json`.
* **Never write a client task/resource/project/person name** into any file,
  comment, test or commit message.  Refer to rows by ID and strings by opaque
  label (`client-name-NN`) or structural relation — never by an unsalted hash,
  which a guess inverts.  Run
  `python -I build/audit-w22/scan_client_names.py HEAD` and
  `python -I build/audit-w22/guard_names.py <range>` before every commit and
  expect 0 hits; also `git grep -nE "client-string-[0-9a-f]{8}"` expect none.
* **Fixtures:** never open a committed fixture in place (Project rewrites files
  it opens); oracle scripts use `oracle_lock.working_copy()`.  Run
  `python scripts/check_fixtures.py` before every commit (it checks the
  scenario oracle provenance and fails on an embedded drive/user-profile path).
  Nothing under `tests/data` is written by a lane.  New fixtures are built
  through Project on a NEUTRAL path (`C:/w28`, `C:/w29`, `C:/w30`, `C:/w31`)
  with neutral Author/Manager/Company/UserName/Title.
* **Displayed rows resolve through their uid** (wave 25): `app::print_row_list`
  takes RecordSet rows; never index `plan.tasks` with a RecordSet row; uid 0 is
  the project row and refuses task edits.
* **Saveable structural edits** (waves 26-30): Priority is measured against
  Project and enabled; PercentComplete, Mark on Track, Update Project and the
  Task Form percent field run on the measured actuals writer; outline
  Indent/Outdent run on the measured outline writer; the new-task mode runs on
  the measured `114/Props` writer; Manually Schedule/Mode run on the measured
  task-mode writer.  A percent edit and a date update need the loaded plan at
  `push()`; step 5a refuses one stale percent edit instead of failing the save.
  The file-side shape harness (`PlanInput::assignment_rows`,
  `unreadable_links`, `resource_uid_raw`) refuses up front the shapes no
  Project pair measured (W30-C5).  Every enabled new edit path needs a
  save-and-reopen test AND the standing-rule-1 Project measurement.  Split Task
  is measured but NOT enabled.  The wave-29 dialogs are session-only over the
  open view; Studio style writes do not exist, so Page Setup is layout-only
  and no Style dialog writes the stored style records.  Wave 31 added NO writer:
  Sort, Outline, Tables and Zoom are in-memory session state only.  Wave 32
  added NO writer either: it refuses or disables (Start is read-only at every
  entry, the automatic-outline cross-refusals, the sorted/filtered range
  refusals), and the Start writer stays in the tree for a later measured wave.
* **`plan_recalc`** never rewrites the golden by default; refreshing needs the
  exact value `PLAN_RECALC_UPDATE_GOLDEN=1`, and the local/CI gate runs
  `ctest -E plan_recalc` plus the corpus-free binary/step.
* **Oracle/COM** is coordinator-only, under `scripts/oracle_lock.py`, with
  `FileClose(0)` and the post-close file as the only evidence.
* **`corpus/`** and `decompiled/` are git-ignored: nothing from them is
  committed, and the operator's captures are never quoted for client data.
* **Builds:** serial only; local Qt 6.5.3 in `build/Qt/6.5.3/6.5.3/msvc2019_64`.
  Qt ctest prepends its own DLL directory.  Transient Dropbox/MSBuild file locks
  are common; retry the build rather than changing CMake.  Text files are UTF-8
  with LF line endings (173 grandfathered CRLF files remain, `git grep -Il
  $'\r'`; do not add more).
* **CI:** one dispatch per milestone; red runs are triaged, fixed and dispatched
  again with the reason recorded.  Wave 32 dispatches once on the final code tree
  (`7727760`) after the single push; the record's Push and CI section carries the
  run ids and the final status, and the CLOSE docs commit after the dispatch
  changes docs only.
* **Commits:** one finding id per commit; honest numbers; a gate number must
  belong to the tree it is written in and name its log under `build/w32/`; no
  test claimed unless run.  A known gap is an expected failure **by name**, never
  an assertion of the wrong output.  The committed
  `scripts/check_command_backlog.py` (ctest `qt_command_backlog`) keeps the
  generated backlog and the ribbon pin in step both ways.

## Start a session with this (paste)

```
Read docs/START-HERE.md and docs/handoff/wave32-complete.md.  Wave 32 is closed
on branch wave32 (pushed; G0 + CLOSE; the wave-31 verifier's three Highs are
closed refuse-first: queued percent/actuals/Start edits cross-refuse with an
automatic outline edit, Start is read-only at every shell entry, and a sorted or
filtered display refuses range Copy/Cut/Paste plus Indent/Outdent/Insert with
"clear the sort first"; the independent review's High and four required
Mediums are fixed in W32-C4..W32-C8; CI runs on the final code tree 7727760 are
recorded in the record's Push and CI section).  The next step is the operator's: verify and
merge the unmerged branches (wave23 through wave32), then choose the next scope
from the record's "Found, not fixed" and the after-run list (Team Planner,
Network Diagram, Calendar and Report views, the constraint-mso engine case,
macOS launch/notarisation, the hashed task names, the remaining review Lows).
Keep every gate in wave32-complete CLOSE green before each commit and run BOTH
the line-based and the whitespace-normalised client-name scans.
```
