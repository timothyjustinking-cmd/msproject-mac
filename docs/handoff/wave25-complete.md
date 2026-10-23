# Wave 25 complete — verify-and-fix, then the carried parity work

_Written 2026-10-24 by the wave-25 coordinator (DeepSeek) on branch `wave25`,
cut from local `wave24` = `2561a51`.  The work order is
`docs/handoff/wave25-brief.md`; it answers the verifier's
`docs/handoff/wave24-verification/` (S1..S14) and the items wave 24 deferred.
**Nothing is pushed and nothing is merged to `main`; the publication hold
(brief section 0) is still in force.**

**Branch state.** `wave25` was cut from `2561a51` (the wave-24 tip) and carried
the brief in `2fd23f5`.  This session's commits, one finding id each, are listed
in §1.  No frontier ref exists on the remote (`origin/wave25` does not exist);
the wave-24 backup branches are untouched.

## 1. Every W25 item

| item | finding | status | commit(s) |
|---|---|---|---|
| W25-1 | S1 (High) — row 0 on the shared and macOS paths | **FIXED** | `b73fd26` |
| W25-2 | S2 (High) — "stock" bar colours are manual-vs-auto | **FIXED** | `5f3a12d` (fixture/reference), `9ec15a8` (code) |
| W25-3 | S3/S4 (Medium) — the privacy scrub, done properly | **FIXED** in the tree (golden-TSV name hashes remain; §5) | `7a64baf` |
| W25-4 | S5 (Medium) — honesty of the record | **FIXED** — errata in §2 | `f96cfcf` |
| W25-5 | S6 (Medium) — stored_table PROJECT_A text-style assertion | **FIXED** (Project's truth restored as 2 named expected failures until W25-12) | `cbf80ef` |
| W25-6 | S7 (Medium) — scenario oracle provenance | **FIXED** + a check that reproduces it | `a77779f` |
| W25-7 | S9 — ribbon `kind` reads the live widgets | **FIXED** | `33217e2` |
| W25-8 | S10 — record-count pin and the Flag block fails when missing | **FIXED** | `02ecf98` |
| W25-9 | S11 — Flag11..17 single-bit controls + generator | **FIXED** | `c21dd0e` (fixtures/generator), `3dddcd0` (test) |
| W25-10 | S12 — both halves of the Timeline pair | **FIXED** | `65a270f` (fixture), `97433ce` (test) |
| W25-11 | S13/S14/R16/R17 — names, comments, PDF subtitle, dispositions | **FIXED**; R16/R17 dispositions in §2.3.  S13 changes the status text (two warnings where one was) though the counts are unchanged | `b6df460`, `4c28039` |
| W25-12..21 | carried parity streams (W24-12/13/14/17/18/19/20/21/22/23/24/25) | **DEFERRED** — reasons in §3 | — |

The independent review (§11) added six follow-up commits, one finding id each:
`a4ac887` (W25-1), `d61af79` (W25-2), `a35a01f` (W25-3), `b04b22e` (W25-6),
`85e03d3` (W25-8), `4c28039` (W25-11).  A SECOND independent pass then found
eight live issues (no Critical/High); its fixes are one response commit,
`336ec0c` (W25-REV2), because they overlap files.  This docs commit is W25-4.

Every commit carries one finding id (W25-9/W25-10 and W25-2 use a fixture-only
commit followed by the code/test commit, as the brief requires).  Commit order
is not strictly the brief's item order: W25-4's docs commit (`f96cfcf`) precedes
the final W25-3 commit (`7a64baf`), because the scrub's writer-oracle
regeneration and its LF normalisation were still being finished when the errata
were written.  The content and the finding ids are unaffected.

## 2. Errata for wave-24 claims (W25-4)

The wave-24 commits are frozen under the publication hold; these corrections are
recorded here rather than by rewriting them.

### 2.1 Gate numbers with no supporting log on the commit they name

| claim | where | what the record shows | erratum |
|---|---|---|---|
| "app_qt ctest 18/18" | commit `ca9e7d7` message; wave24-complete §2 | `ca9e7d7:app_qt/CMakeLists.txt` registers **17** tests (`git show ca9e7d7:app_qt/CMakeLists.txt \| grep -c "add_test(NAME"` = 17; the 18th, `qt_uid_routing`, appears at `ff22c7b`). The same commit rewrites `project_a_preds_golden.sha256` to the per-row format while `task_order_test.cpp` at that commit still takes the first line as one aggregate digest, so `qt_task_order_corpus` cannot pass on that tree. | The claim is false for `ca9e7d7`; no app_qt run can have produced 18/18 there. |
| "full fresh set" | commit messages `ff22c7b` and `cc4ce8a`, both timed 00:09 | `build/w24` holds no log for either tree; the earliest qt log is 00:43, the earliest final-code logs are 00:43-00:46. | Two commits a minute apart cannot both have run the whole set; treat both messages' numbers as unsupported. |
| "fresh trees of the final code (39d11b4)" | wave24-complete §2 | `build/w24/fresh-app-ctest.log` and `fresh-engine-suite.log` are dated **00:22**, before the 00:41 rebuild of the working tree that BECAME `39d11b4` (the commit is 00:47). `fresh-ctest.log` (00:46), `fresh-plan-recalc.log` (00:46) and `fresh-qt-ctest.log` (00:46) are post-`39d11b4`; the final-code **app** evidence is `build/w24/gate-app-ctest.log` (00:43), not the "fresh" file. | The "fresh" label applies only to core/plan_recalc/qt; app was run on the final tree but not on a fresh configure. |
| reader 1063 / 383 | wave24-complete §2 | no wave-24 log records the reader counts. | Reproducible from the test but not part of the logged evidence; require a log next time. |
| parity 10/10 and pixel score 16.08 % | wave24-complete §2 | no wave-24 log records either; only the numbers were transcribed. | The committed reference JSON exists and wave 25 re-ran the gate (§9); the wave-24 number itself is unlogged. |
| client-name scan "0 files / 0 hits" | wave24-complete §2 | no scan log under `build/w24`; the scanner's `MIN_LEN` change (a comment in the untracked `build/audit-w22` copy, not git history) shows the pre-`39d11b4` rule could not see the nine-character name that was still present. | The only defensible statement is the post-`39d11b4` scan; wave 25 logs its own scans per commit (see §11 for the review's log-completeness finding). |
| fixtures "OK (13 documented hashes, 12 tracked .mpp)" | wave24-complete §2 | correct at the tip by content (`scripts/check_fixtures.py` at `2561a51` documents 13 paths; the tree tracks 12 `.mpp` including `analysis/test_project.mpp`), but the only log in `build/w24` is `gate-fixtures.log` at 00:13 saying **11 / 10**, which is the pre-`39d11b4` tree. | The number is right; the log beside it is not the tip's.  A tip fixtures run was never logged. |

### 2.2 START-HERE corrections

* "fixtures **OK** (11 hashes, 10 `.mpp`)" was the **00:13** pre-review-fix count,
  not the wave-24 tip's.  At `2561a51` it is **13 documented hashes, 12 tracked
  `.mpp`**.  Wave 25 adds the auto-scheduled fixture + reference JSON, the
  seven Flag11..17 controls and the Timeline shown half: the close count is
  **23 documented hashes, 21 tracked `.mpp`** (the count was 15/13 after
  W25-2 and 23/21 from W25-10 on).
* "the tree is clean as of `ff22c7b`" is false: `ff22c7b` re-added a
  nine-character client resource name in `app_qt/sheet_views.cpp`, and
  `39d11b4` is the commit that removed it.  The clean baseline is `39d11b4`.
  Both statements are corrected in `docs/START-HERE.md` in this wave.

### 2.3 R16 and R17 dispositions (W25-11/S13/S14)

The wave-24 review found that R16 and R17 were never mapped to a W24 item and
had no disposition.  They are mapped here.

| finding | what it is | disposition |
|---|---|---|
| R16a | the frozen row-number gutter paints "0" on a pending inserted row | **DEFERRED to W25-21 (cosmetic, no data loss).** Cause: a pending row has ID 0, and `SheetTableModel::row_number_text()` returns the non-empty `field_text` "0" before its display-ordinal fallback. The fix is for the model to return empty when `id == 0` and `row_uid != 0` (the project row is uid 0 and must keep its "0") and for `SheetRowHeader::paintSection` to paint nothing rather than fall back to the ordinal; a Qt test must pin the blank. Not attempted here because it changes the header contract in the same wave as the row-routing work and needs the pending-row fixture. |
| R16b | Insert Task while the project row is selected appends the new row at the end | **DEFERRED to W25-21.** `MainWindow::insert_task()` sets `anchor_uid = selected_uid`; uid 0 means "no anchor", which the edit model treats as append. Project inserts at the top of the plan (the project row is not a task). The fix needs an anchor rule ("project row selected" -> anchor at the first level-0 task) plus a `qt_uid_routing` assertion. |
| R17 | macOS duplicate menu label | **DEFERRED to W25-21.** The AppKit menu builder in `app/main.mm` is macOS-only and cannot be compiled or run on this development machine; the shared `app::menu_model` data has no duplicate title within a menu by inspection, and no test compares the AppKit menu to it. Reproduce on a Mac, then either remove the duplicate item or give it a distinct title; add the missing macOS menu-shape check at the same time. |

### 2.4 Claims that stand

`gate-core-ctest.log` 51/51, `gate-plan-recalc.log` 266 checks,
`gate-app-ctest.log` 15/15, `gate-qt-ctest.log` 18/18, `gate-m2.log` 332/332
(268 leaves / 64 summaries), and `gate-engine-suite.log` (7 pass / 1 expected
fail, `constraint-mso`) all exist at 00:43 on the `39d11b4` tree.  The
wave-24-complete §2 numbers match those logs.

## 3. Parity work carried from waves 23-24 (W25-12..W25-21)

The brief orders section 2.5 **after** 2.1-2.4 and says "as capacity allows".
This wave spent its budget on the two High regressions, the privacy scrub done
properly, the record errata and the six gate holes; none of the parity streams
was started.  Each is deferred with what the live check (§4) shows and what a
correct pass needs.

| item | what | status / reason |
|---|---|---|
| W25-12 | file text styles (font family/size, milestone/critical/summary styles, highlighted rows) from the view records | **DEFERRED.** Not decoded. This is the largest visible gap: PROJECT_A's milestone rows are italic, critical rows are red and several rows carry fill highlights; the port draws the stock fallback. W25-5's two named expected failures stay listed until this lands. |
| W25-13 | file bar styles from the `0x22400008` view blob, falling back to the W25-2 palette | **DEFERRED.** The stock fallback is now correct (auto/manual/summary/milestone + Critical Tasks ON); the FILE's own bar colours and labels are still not decoded, so PROJECT_A renders in the stock palette instead of its red/green/navy scheme. |
| W25-14 | timescale tiers and units from the same blob | **DEFERRED.** The file stores quarter/month tiers; the shell still renders week/day. The live check measures the difference. |
| W25-15 | View > Task Views / Resource Views drop-downs | **DEFERRED** (carried from W24-12). `app::MenuSpec` still has no submenu axis; adding one touches both menu builders and their tests. |
| W25-16 | Custom Fields as the field-definition dialog; per-task values on the Task Information tab | **DEFERRED** (carried from W24-14). Dialog-sized; Project > Custom Fields still opens the per-task value editor. |
| W25-17 | distinct monochrome icons + uniqueness gate | **DEFERRED** (W24-20). Placeholder circles remain. |
| W25-18 | ribbon control kinds from `ribbon_pin_unmodelled.tsv` | **DEFERRED** (W24-21). The W25-7 gate now reads live widget classes, which is the prerequisite; no combo/checkbox control is built yet. |
| W25-19 | backstage pages, Account/About, the 11-page Options dialog, B-14 dialogs | **DEFERRED** (W24-22). |
| W25-20 | dark theme: chrome only, sheet/chart unchanged, live repaint | **DEFERRED** (W24-23). |
| W25-21 | remaining dialogs and the split view; W24-25 items as capacity allows | **DEFERRED** (W24-24/25), and it now also owns the three R16/R17 dispositions in §2.3. |

**Known deviation not fixed here (review finding 4):** the measured palette is
applied to the Qt screen canvas.  The Qt print/PDF path (`app_qt/print_page.cpp`)
and `core/render` still use their own older blue/red palette and never read the
`manual` flag, and the AppKit drawing in `app/main.mm` has its own NSColor
palette.  `core/render` compiles from the RecordSet, which does not carry the
reader's manual bit, so a correct fix needs a model field first; the AppKit
palette needs a measured pass on a Mac.  Listed with W25-13 / the print-palette
follow-up; this record does not claim those paths use the new palette.

## 4. Live same-file check (local, git-ignored)

Both programs opened a COPY of `corpus/project_a.mpp`, window maximised /
1920x1032 render, 2026-10-24: `build/w25/live-project.png` (Project 2021,
read-only) and `build/w25/live-port.png` (Qt shell `--parity`).  No client
string is quoted; rows are named by ID.

| what | Project 2021 | the port | verdict |
|---|---|---|---|
| row order, IDs, project row 0 | project row 0 + IDs 1..83, stored order respected | same 0 + 1..83, same outline indent | **match** (W25-1 keeps it so) |
| project row's duration/% | ID 0 reads 217 d / 37 % | ID 0 reads 37 % (duration column clipped by width) | **match on the values shown** |
| Timeline strip | hidden (the file stores it hidden) | hidden | **match** |
| timescale | quarter tier ("Qtr 1 2026") over months | week labels over single-letter days | **differs** — W25-14 |
| sheet columns at this width | Task Name + Dur, Start, Finish, % Com, Pre, Suc, SUMMARY (8) | Task Name + Dur, Start, Finish, % Com (5); Pre/Suc/SUMMARY lie beyond the splitter | **differs** — stored column widths/table, W25-12/13 |
| bar palette | the FILE's styles: red/blue/green bars, many labels | stock auto light blue, black milestones, grey/teal summaries, sparse labels | **differs** — W25-13 |
| sheet text | Arial 8; italic milestones, red critical rows, highlighted fills | UI font; bold summaries only, plain rows | **differs** — W25-12 |
| title | `<file> [Read-Only] - Project Professional` | `<file> - MS Project Shell` (decision 5; the offscreen render has no title bar) | **differs by ruling** |
| status mode | New Tasks: Auto Scheduled | New Tasks: Auto Scheduled | **match** |

The port is now correct on the axis W25-1/W25-2 fixed (row-to-bar alignment,
stock auto/manual colours); every remaining difference is a FILE-style or
stored-view gap, not a stock one.  One geometric approximation remains inside
the colour fix: Project draws an auto summary as a thin (84,84,84) bracket,
while the port fills a 6 px summary bar with that measured line colour.  The
colour is Project's; the shape is a pre-existing approximation (the port has no
summary-line geometry), not a palette error.

## 5. Privacy state after W25-3

* No tracked file added or changed by wave 25 contains a client task, resource
  or project name, and no wave-25 commit introduces a `client-string-<8 hex>`
  token.  The token grep is clean on the tip and on **every commit from
  `7a64baf` (W25-3) onward**; the four earlier wave-25 commits
  (`b73fd26`, `5f3a12d`, `9ec15a8`, `f96cfcf`) precede the scrub in the branch
  order and therefore still carry the wave-24 labels they inherit -- they add
  none.  The
  brief's "clean on every wave-25 commit" is met from the scrub onward; making
  the pre-scrub commits clean would mean bundling W25-3 into unrelated commits
  or rewriting the branch, both worse than recording the inherited state.
* The name scan reports **0 files / 0 hits** on every wave-25 commit tree and
  **0 commit messages** quote a name (run over `2561a51..wave25`).
* The regenerated `docs/analysis/oracle/writer-name.json` holds the synthetic
  name Project actually returned for the writer case, not a substitution.
* **Residual 1 (documented, not fixed):** `tests/data/project_a_golden.tsv`
  still stores a 16-hex unsalted hash of every PROJECT_A task name in its `name`
  column.  That is the oracle the reader test compares against; replacing it
  with opaque labels would either weaken the gate or need a keyed hash whose
  key is not committed, and re-recording needs the full export (the readable
  copy is not on this machine).  No new client-derived hash was added in wave
  25; this is wave-24 carrier state and belongs with Tim's publication decision.
* **Fixed by the second review (W25-REV2):** a dead row's orphaned client name
  was a plaintext string literal in `test_project_a`.  The name scanner only
  sees LIVE rows (it dumps tasks/resources/titles), so it could not catch an
  orphan block; this was the one real name literal left in the tree, and it is
  removed.  The scanner's live-row blind spot is noted here for the next scrub.
* **Residual 2 (Tim only):** the plaintext names remain in public git history
  (waves 23-24 commits).  The tree scrub cannot remove history; the publication
  hold and the rewrite/visibility decision remain Tim's.

## 6. Gate holes that are now closed (a summary of §1)

* S9 ribbon kind -> live QWidget class (W25-7).
* S10 corpus record count -> fixture-label pin; missing flag fixtures FAIL (W25-8).
* S11 Flag11..17 single-bit controls + committed generator (W25-9).
* S12 Timeline pair reproduces in the repo (W25-10).
* S13/S14 names, comments, PDF subtitle, R16/R17 dispositions (W25-11).
* S5/S6/S7 record honesty and expected-failure naming (W25-4/W25-5/W25-6).

## 7. CI (per brief section 0)

**CI not dispatched: publication hold.**  Tim has not lifted the hold
(`START-HERE.md` carries no lift line and there is no commit from him saying so),
so nothing was pushed and no workflow was run.  `origin/wave25` does not exist;
a `workflow_dispatch` needs the ref on the remote, which the hold forbids.  The
local fresh-tree gates in §9 are the evidence.  When the hold is lifted:
dispatch **both** workflows explicitly on the branch (`ci.yml` and `qt.yml`);
`workflow_run` only registers from the default branch's workflow file, so it
cannot fire for `wave25` before a merge.  `qt.yml` now also runs the
auto-scheduled colour-only parity gate added in W25-2.

## 8. The 16 decisions re-scored (wave-24 close as the starting point)

| # | decision | wave 24 | after wave 25 | why |
|---|---|---|---|---|
| 1 | all seven tabs | PARTIAL | **PARTIAL** | labels/order/pin pass; control kinds/icons still absent (W25-18) |
| 2 | full backstage | PARTIAL | **PARTIAL** | unchanged (W25-19) |
| 3 | original icon set | NOT DONE | **NOT DONE** | placeholder circles (W25-17) |
| 4 | measured chrome metrics | DONE (stronger) | **DONE (stronger)** | full parity 10/10 plus the colour-only auto-scheduled reference; the stock palette is now measured per scheduling mode |
| 5 | timeline strip | PARTIAL | **PARTIAL** | visibility file-driven and the shown/hidden pair now reproduces in the repo; View > Timeline toggle still missing |
| 6 | real Entry table | DONE | **DONE** | unchanged |
| 7 | render the stored table | PARTIAL | **PARTIAL** | row/ID/outline/project row/SUMMARY/stock auto-manual palette; timescale and FILE styles still differ (W25-12/13/14) |
| 8 | status bar | PARTIAL | **PARTIAL** | unchanged |
| 9 | disabled commands + backlog | DONE | **DONE** | unchanged |
| 10 | light and dark themes | PARTIAL | **PARTIAL** | dark chrome still not repainted (W25-20) |
| 11 | new-project defaults | DONE | **DONE** | unchanged |
| 12 | structural measurement | PARTIAL (stronger) | **PARTIAL (stronger)** | parity gates now include a Project-measured auto-task colour; not every screen |
| 13 | Carlito bundled | DONE | **DONE** | unchanged |
| 14 | every dialog on every tab | PARTIAL | **PARTIAL** | Custom Fields is still the value editor (W25-16) |
| 15 | dialog semantics | PARTIAL | **PARTIAL** | unchanged |
| 16 | split view | NOT DONE | **NOT DONE** | unchanged (W25-21) |

## 9. Verification gates on the final tree

Fresh trees, configured from scratch and built serially on 2026-10-24:
`build/w25-fresh` (core, `-DCORE_WARNINGS_AS_ERRORS=ON`), `build/w25-fresh-app`,
`build/w25-fresh-qt` (no Qt PATH workaround).  The numbers below are the
post-review re-run on the final code commit **`336ec0c`** (they were first run
on `b6df460`; the first review fixes changed no number; the second review's
privacy/export fixes moved the reader counts to 1100/420).  Full log:
**`build/w25/final-gates-core.log`**.  The per-commit logs are
`build/w25/gates-<parent>-<item>.log` and are named in each commit message; the
six review-fix logs are `build/w25/gates-<parent>-<item>-review.log`.

Engine suite, run explicitly on the fresh tree (the ctest wrapper only reports
pass/fail): **8 recorded scenarios, 7 pass / 1 fail, 0 unrecorded**, expected
failing set `constraint-mso` reached -- appended to
`build/w25/final-gates-core.log`.

| gate | result |
|---|---|
| core `ctest -E plan_recalc` | **51/51** (includes `engine_diff_apply/harness/suite`; the suite is 7 pass / 1 expected fail, `constraint-mso`) |
| `plan_recalc_tests` with corpus | **512 checks, 0 failures** |
| `plan_recalc_tests` `MPP_NO_CORPUS=1` | **266 checks, 0 failures** |
| M2 `recalc --compare` | **332/332** (268/268 leaves, 64/64 summaries) |
| reader tests | **1100** with corpus, **420** without |
| app ctest | **15/15** (`app_layout` 273 checks) |
| app_qt ctest | **18/18**; `qt_stored_table_corpus` is 0 failures + **2 named expected failures** |
| `scripts/check_fixtures.py` | **OK** (23 documented hashes, 21 tracked `.mpp`) and oracle provenance OK (8) |
| parity wave23-outline | **10 comparable, 0 failing, 0 missing — PASS** |
| parity wave25-auto `--only colour.task_bar` | **1 comparable, 0 failing — PASS** (reference and candidate both (138,187,237)) |
| client-name scan, tip | **0 files, 0 hits** |
| client-name scan, every wave-25 commit tree | **0 files, 0 hits on all 15 trees**; **0 commit messages** on `2561a51..wave25` |
| 8-hex `client-string-` grep | clean on the tip and on every commit from `7a64baf` onward; the four pre-scrub commits carry wave-24 labels and add none (§5) |

Falsifications run by hand (recorded in the per-item logs):
* W25-1: restoring `input.tasks[row]` fails 5 of 270 checks.
* W25-2: setting `kBarTask` to the old manual teal fails the colour gate
  ((138,187,237) vs (116,204,212), delta 25 > tolerance 24).
* W25-7: changing one pin row's kind from button to combo fails the live-kind
  check with "live: button".
* W25-8: moving `wave24-flags20.mpp` out of `tests/data` fails the reader test
  (1 failed of 1051 checks) instead of skipping.

The pixel score was not re-run: the reference PNG for PROJECT_A is not
available locally (the live check §4 is a manual comparison, not a scored one).

## 10. Start the next wave with this

Read `docs/START-HERE.md`, then this file.  The next block is W25-12's file text
styles (it closes the two named expected failures in `qt_stored_table_corpus`),
then W25-13's file bar styles and W25-14's timescale tiers.  Keep every gate in
§9 green before each commit; the publication hold still governs any push.  The
R16/R17 dispositions in §2.3 belong to W25-21, and the privacy residual in §5
(golden-TSV hashes, history) is Tim's decision with the hold.

## 11. Independent review of `2561a51..wave25` (verification gate 3)

A separate read-only reviewer (fresh context, not the author) reviewed the diff,
the commit messages, the gate logs and the local captures on 2026-10-24.  It ran
no git write command and quoted no client data.  It returned 20 findings; each
is fixed or listed below.  The review-fix commits are named in §1.

| # | sev | finding | disposition |
|---|---|---|---|
| 1 | High | the close record and final evidence were uncommitted; the committed tip's record was stale | **FIXED** by this docs commit |
| 2 | High | seven commit messages quoted scan numbers their named logs did not contain, and W25-3's 8-hex grep was unlogged | **FIXED**: a post-commit scan section was appended to each named log, run on that commit's own tree (0 files / 0 hits each); the scans always passed, the logs were incomplete |
| 3 | High | W25-3's quoted tree `2d000e1` was not its commit tree `4d7783f` (the amend added two Python newline edits) | **RECORDED** in `gates-f96cfcf-W25-3.log`: the delta is the two oracle scripts' newline option only, no C++ gate; 4d7783f re-scanned clean |
| 4 | Medium | the measured palette reaches the Qt screen canvas only; Qt print/PDF and the AppKit drawing still use older colours and ignore `manual` | **LISTED, DEFERRED** to the W25-13/print follow-up: `core/render` compiles from the RecordSet, which does not carry the reader's manual bit, so a correct fix needs a model field first; the AppKit palette needs a measured pass.  Not claimed as done anywhere |
| 5 | Medium | the macOS PDF subtitle still counted the project row | **FIXED** in `4c28039` using `task_row_count` (main.mm is compiled only by the macOS CI job; not compiled locally) |
| 6 | Medium | the W25-8 fixture-label pin was a pointer comparison (`const char*` against a literal) | **FIXED** in `85e03d3` (`std::string(label) == "project_a"`) |
| 7 | Medium | `kLinkCritical` was the one measured colour no test pinned | **FIXED** in `d61af79` (qt_parity_geometry asserts (221,37,34)) |
| 8 | Medium | the golden TSV still carries unsalted 16-hex task-name hashes; START-HERE overclaimed that every name-hash pin was gone | **START-HERE corrected**; the carrier is the documented residual in §5 (Tim's decision with the hold) |
| 9 | Low/Med | `task_row_count` could not tell an empty zero-match filter from no filter | **FIXED** in `4c28039` (two overloads; empty list = 0); export_tests pins it |
| 10 | Low | the smoke chart count compared the layout to the source it was built from | **FIXED** in `a4ac887` (independent fixture expectation).  The smoke is still only run on synthetic-base; running `smoke_plan` on a row-shown fixture is listed for a W25-10 follow-up |
| 11 | Low | the status marker borrowed `kBarCritical` | **FIXED** in `d61af79` (`kStatusLine`, explicitly unmeasured chrome) |
| 12 | Low | the Task Mode pin silently followed `kBarTask` | **FIXED** in `d61af79` (`kTaskModePin`, its pre-wave-25 teal, unmeasured chrome) |
| 13 | Low | `LinkRoute::manual` was inserted mid-struct | **FIXED** in `d61af79` (appended after `bounds`) |
| 14 | Low | the provenance check used `git log` without `--follow`; `a77779f`'s message named the wrong tree | **FIXED** in `b04b22e` (`--follow`); the tree typo is recorded in `gates-a77779f-W25-6.log` (actual `b3c51e2`) |
| 15 | Low | the engine 7 pass / 1 expected fail number had no wave-25 log | **FIXED**: the suite was run explicitly on the fresh tree and its output is in `final-gates-core.log` |
| 16 | Low | stale docs/hashes: app/README API name, "439 checks", the reference README hash, two tests/data table rows | **FIXED** in this docs commit |
| 17 | Low | the replacement name checks were weaker than their names (prefix / non-empty) | **FIXED** in `a35a01f` (exact trim relation via an independent helper) |
| 18 | Low | W25-11's "no behaviour change" was inaccurate: S13 splits one warning into two | **RECORDED** in the §1 W25-11 row; the counts are unchanged, the status text is not |
| 19 | Low | two errata wordings were imprecise (the 00:41 rebuild/commit time; the scanner MIN_LEN "history") | **FIXED** in §2.1 |
| 20 | Low | the four pre-scrub wave-25 commits still carry inherited labels, so the brief's literal "clean on every commit" is not met | **RECORDED** in §5 with the reason rewriting was rejected |

The reviewer's independent checks that found no defect are recorded here because
they are evidence, not because they were findings: the W25-1 routing code and
its discriminating test; the W25-2 pixel counts in both capture PNGs (auto
138,187,237 / manual 116,204,212 / critical 247,138,136 / links 38,124,211 and
27,173,186 / critical link 221,37,34) and the falsification delta 25 > 24; the
W25-3 8-hex and name scans over every tree; the writer-oracle diff (only the
Name/run_at/outline leaves changed); the W25-5 expected-failure logic; the W25-6
body comparison; the W25-7 classifier; W25-9's flag table; W25-10's pair
comparison; and the counter/field-name accuracy of the S13 split.

### 11.1 Second independent pass (after the first review's fixes)

A second read-only reviewer covered the code diff (app, app_qt, core, tools,
tests, scripts), the final records and the logs.  It found **no Critical or
High** issue and classified the first review's fixes as correct; eight live
findings remained, fixed or listed in `336ec0c` (W25-REV2) except where noted.

| # | sev | finding | disposition |
|---|---|---|---|
| 21 | Medium | an active filter that matches nothing exported every task: the subtitle said "0 of N" but the body printed all rows (`ExportRequest` could not express an empty filtered list) | **FIXED** in `336ec0c`: `rows_are_filtered` + `app::export_rows`; both shells set it; export_tests pin all/zero/one |
| 22 | Medium | Cut and Paste bypassed the uid-0 guard (`SheetModel::cut/paste` write the RecordSet directly), so a project-row edit would show but never save | **FIXED** in `336ec0c`: Qt Cut skips uid-0 cells; Qt and macOS Paste refuse when the anchor row is uid 0 |
| 23 | Low | manual-vs-critical precedence put the highlight colour before the manual colour, contradicting the measured "a manual bar does not change with highlighting ON" (latent: no shell enables the toggle) | **FIXED** in `336ec0c` (manual checked first for bars and links) |
| 24 | Low | the Timeline pair compared only sizes for links/resources/assignments and a subset of task fields | **FIXED** in `336ec0c`: every exposed task field plus every link/resource/assignment field is compared |
| 25 | Low | the golden TSV still carries unsalted name hashes (the §5 residual), and the dead row's orphaned client name was still a plaintext literal; the name scanner only sees live rows | **Orphan literal FIXED** in `336ec0c` (assert non-empty, not quoted); the golden residual is **LISTED** in §5 |
| 26 | Low | a staged-but-uncommitted oracle with a fabricated `recording_commit` passed the provenance check (empty history made the prefix test true) | **FIXED** in `336ec0c` (no history -> failure) |
| 27 | Low | only the +33 group (Flag11..17) has per-bit controls; the +24 and +34 groups still rely on the all-20 aggregate, so a within-group mask permutation can pass | **LISTED, DEFERRED**: the brief's W25-9 asks for Flag11..17 specifically; a complete per-bit sweep needs eight more fixtures and goes with the next flag stream |
| 28 | Low | `parity_report --only` kept duplicate keys, making the minimum comparable unreachable | **FIXED** in `336ec0c` (deduplicated, order kept) |

The second reviewer also confirmed the first review's fixed items live
(pointer pin, smoke count, macOS subtitle body, trim relation, `--follow`,
appended `LinkRoute::manual`, dedicated chrome colours) and re-ran the reader
and layout tests (1100 and 273 checks) plus check_fixtures (exit 0).
