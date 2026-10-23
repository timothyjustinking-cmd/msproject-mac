# Wave 24 complete — the verify-and-fix work order, fixed and deferred

_Written 2026-10-24 by the wave-24 coordinator (DeepSeek) on branch `wave24`,
from local `wave23` = `d236f91`.  The work order is
`docs/handoff/wave24-brief.md`; it answers the verifier's
`docs/handoff/wave23-verification/` (R1..R18, V-notes).  **Nothing is pushed and
nothing is merged to `main`; the publication hold (brief section 0) is still in
force.**

**Branch state.** `wave24` was cut from local `wave23` = `d236f91`; before this
session it carried the brief and two tools commits (`552aaf4`, `394d69a`,
`763bbc5`).  This session added five commits on top (`ca9e7d7`..`39d11b4`),
plus this record's final docs-only commit:

| commit | what |
|---|---|
| `ca9e7d7` | fixtures and oracles (Project-built save pairs, per-row Pre/Suc golden, bar-visible reference, oracle provenance) |
| `ff22c7b` | the fixes: uid routing, file view settings, flags, stock style fallback, privacy scrub |
| `cc4ce8a` | CI coverage, suite pin by name, workflow_run, colour gate, pin kind |
| `d5d8126` | wave-24 close docs, wave-23 errata, engine docstring |
| `39d11b4` | the independent review's fixes (short-name scrub, MPP12 flags, gate holes) and this record |

`origin/wave24` does not exist (no push).  `wave23` is still unmerged; this wave
does not touch it or its two local backup branches.

## 1. Every W24 item

| item | finding | status | commit(s) |
|---|---|---|---|
| W24-1 | R1 (Critical) / R5 / R6 / R7 — RecordSet row vs plan index | **FIXED** | `ca9e7d7` (fixture), `ff22c7b` |
| W24-2 | R2 (High) — the project-summary row is stored | **FIXED** | `ca9e7d7`, `ff22c7b` |
| W24-3 | R3 (High) — Timeline visibility: measure | **FIXED** (it IS file state) | `ca9e7d7`, `ff22c7b` |
| W24-4 | R4 (High) — CI lost the corpus-free plan_recalc checks | **FIXED** | `cc4ce8a` |
| W24-5 | R8 — ID-order sort untested in CI | **FIXED** | `ca9e7d7`, `ff22c7b` |
| W24-6 | V — double-click opens Task Information | **FIXED** | `ff22c7b` |
| W24-7 | R10 / V — engine lane counts only; provenance | **FIXED** | `cc4ce8a` |
| W24-8 | V — Pre/Suc golden is one hash over 83 rows | **FIXED** | `ca9e7d7`, `ff22c7b` |
| W24-9 | R13 / R11 — tautologies | **FIXED** | `ff22c7b` |
| W24-10 | R9 / V — workflow_run | **FIXED** (see §5 for the default-branch limit) | `cc4ce8a` |
| W24-11 | V — reference capture without bars | **FIXED** | `ca9e7d7`, `cc4ce8a` |
| W24-12 | R15 / V — ribbon kind; View drops-downs | **PARTIAL** — kind asserted; drop-downs deferred | `ff22c7b` |
| W24-13 | R12 / V — text styles | **PARTIAL** — stock fallback measured; file text styles deferred | `ff22c7b` |
| W24-14 | V — Custom Fields definition dialog | **DEFERRED** — see §3 | — |
| W24-15 | V — Flag10 generically | **FIXED** | `ca9e7d7`, `ff22c7b` |
| W24-16 | R18 / V — claims | **FIXED** — §6 erratum and this record | this file |
| W24-17 | W23-3 timescale tiers | **DEFERRED** | — |
| W24-18 | W23-4 bar styles | **PARTIAL** — stock palette measured; file bar-style records deferred | `ff22c7b` |
| W24-19 | W23-5 file font/size, highlighted rows | **DEFERRED** | — |
| W24-20 | W23-14 distinct icons + uniqueness gate | **DEFERRED** | — |
| W24-21 | W23-15 control kinds | **DEFERRED** | — |
| W24-22 | W23-16 backstage/Options/B-14 dialogs | **DEFERRED** | — |
| W24-23 | W23-18 dark theme | **DEFERRED** | — |
| W24-24 | W23-20 dialogs and split view | **DEFERRED** | — |
| W24-25 | W23-21/22 writer/macOS/sf248/clipboard | **DEFERRED** | — |
| W24-26 | section 0 / R14 — forward privacy scrub | **FIXED** | `ff22c7b` |

### W24-1, in detail (the Critical)

Every command now resolves the selection to a **UID** and asks
`PlanDocument::task_by_uid()`; no call site indexes `plan().tasks` with a
RecordSet row.  The new `qt_uid_routing` gate uses `tests/data/wave24-order.mpp`
(project row shown, stored order != ID order, ID 1 auto, ID 2 manual with a 4-day
duration) so every assertion below fails under the old `row`-as-index arithmetic:

* Task Information on row 1 reads ID 1, not the task below;
* Custom Fields queues an edit, Save writes it, and a re-read finds it on the
  SELECTED task and nowhere else;
* Delete names the selected task in its confirmation;
* the row menu's Auto/Manual label follows the selected UID (auto row offers
  Manually Schedule; manual row offers Auto Schedule);
* the printed Gantt has one bar per printed sheet row, in the same order, with
  the row's uid (uid 0 draws the project bar);
* Insert Task's pending row copies the last STORED row's duration/start (4 days,
  `TaskRow::file_row`), and a save-and-reopen shows the same values — the writer
  clones the same stored row, so pending and written now agree.

The project row (uid 0) is refused by Task Information/apply with an explicit
status message; `SheetTableModel` already refused its cell edits.

### W24-2/W24-3, the two stored settings (measurements)

* **Project-summary row.**  `214/Props` record `0x03400009` (int16): 0 hidden,
  1 shown.  Re-measured on a fresh Project 2021 pair (neutral path): off = 0,
  on = 1, reopen agrees; a new project is 0; every committed synthetic fixture
  is 0; Project's saves of PROJECT_A are 1.  The reader decodes it, the shell
  follows the file, and the five wave-23 gates are file-driven again.
* **Timeline strip.**  `214/Props` record `0x03400008` (UTF-16): `"Time&line"`
  when shown, empty when hidden.  A shown/hidden pair saved from ONE session
  differed semantically only in that record plus the per-save GUID; two FRESH
  Project sessions reopened the pair shown and hidden.  It is file state, not
  application state — wave 23's "no 214 part differs" diff never decoded the
  Props framing.  PROJECT_A/B store empty (hidden, matching the operator's
  capture); synthetic-base and wave23-outline store `"Time&line"`.  The shell
  now follows the file, so the brief's project_a acceptance is restored.

### W24-15, in detail

All twenty Flag bits were measured one at a time (a Project save per flag on a
copy of wave23-outline, control all false, `Task.FlagN = True` on ID 3 changes
exactly one FixedMeta byte):

```
Flag1..7   byte +24  bits 0x02 0x04 0x08 0x10 0x20 0x40 0x80
Flag8..10  byte +25  bits 0x01 0x02 0x04
Flag11..17 byte +33  bits 0x02 0x04 0x08 0x10 0x20 0x40 0x80
Flag18..20 byte +34  bits 0x01 0x02 0x04
```

`Task::flags[0..19]`, with `Task::flag10 = flags[9]` (Project's SUMMARY
column).  The old `summary_flag` name is gone.  `wave24-flags20.mpp` sets all
twenty on ID 3 and none elsewhere; `wave23-flags.mpp` is the single-bit control.

## 2. Gates (exact numbers, branch `wave24`)

All gates were run on the final code tree (`39d11b4`), locally and serially; the
core lane, `plan_recalc`, app and app_qt were ALSO re-run on fresh trees
(`build/w24-fresh`, `build/w24-fresh-app`, `build/w24-fresh-qt`, configured from
scratch, core with `CORE_WARNINGS_AS_ERRORS=ON`): **51/51**, **266 checks**,
**15/15** and **18/18**.

| gate | result |
|---|---|
| core `ctest -E plan_recalc` | **51/51** |
| `plan_recalc_tests` (`MPP_NO_CORPUS=1`) | **266 checks, 0 failures** |
| `plan_recalc_tests` with corpus (golden read-only) | **512 checks, 0 failures** |
| app ctest | **15/15** |
| app_qt ctest (corpus present) | **18/18** (`qt_uid_routing` added) |
| M2 `recalc --compare` | **332/332** |
| engine `--suite --expect-fail constraint-mso` | **7 pass, 1 expected fail**, 0 unrecorded; a wrong `--expect-fail` name exits 1 |
| engine `--selftest` | 265 checks (unchanged) |
| `scripts/check_fixtures.py` | **OK** (13 documented hashes, 12 tracked `.mpp`) |
| reader tests | **1063** with corpus, **383** without |
| parity vs Project's recaptured reference | **10 comparable, 0 failing** (task_bar found and equal) |
| pixel score (decision 12, report only) | **16.08 % total** — title 34.94, ribbon 24.63, timeline 48.21, sheet 10.16, chart 12.53, status 20.40 |
| `PLAN_RECALC_UPDATE_GOLDEN=0` | treated as compare (golden unchanged) |
| client-name scan at the tip | **0 file(s), 0 hit(s)** on 156 names (including short multi-word resource names) and 0 commit messages; the scan rule was widened after the review found a 9-character name |

The parity render and the reference capture are local (PNGs git-ignored); only
the recaptured numbers JSON is committed.

## 3. Deferred items (reasons)

* **W24-14 Custom Fields definition dialog.** Not built.  Project > Custom
  Fields still opens the per-task value editor, and Task Information's Custom
  Fields tab is still a placeholder.  A correct pass needs the definition dialog
  (field list, type/rename/formula/lookup/indicators with unimplemented actions
  disabled) AND moving the value editor into the Task Information tab as one
  committed edit; it is a dialog-sized change, not a mechanical one.  Not
  attempted rather than half-built.
* **W24-12, View > Task Views / Resource Views drop-downs.** The ribbon pin's
  `kind` column is now asserted (a combo/checkbox row cannot pass as a button),
  but the View menu still lists Task Sheet / Resource Sheet / Resource Usage
  flat.  `app::MenuSpec` has no submenu axis; adding one touches the Qt and
  macOS builders and the menu tests.  Deferred.
* **W24-13/W24-18 file styles.** The stock fallback is now measured from a NEW
  blank Project (milestone row upright, critical rows normal text; task bar
  116,204,212; critical 138,187,237; summary 43,163,173; links
  27,173,186 / 38,124,211).  The FILE's own text styles (font family/size,
  highlighted rows) and bar-style colour records are still not decoded; they
  remain the largest visible gap against PROJECT_A's capture.
* **W24-17 timescale, W24-19 font/highlight, W24-20 icons, W24-21 control
  kinds, W24-22 backstage/Options/B-14 dialogs, W24-23 dark theme, W24-24
  dialogs/split view, W24-25 writer/macOS/sf248/clipboard.** Carried from wave
  23 unchanged; no code was touched for them.  The brief authorises them but
  they are design-sized streams, and this wave spent its budget on the
  Critical/High items and the gates.

## 4. Live same-file check (local, git-ignored)

Both programs opened a copy of `corpus/project_a.mpp`, window maximised,
2026-10-24: `build/w24/live-project.png` and `build/w24/live-port.png`.  No
client string is quoted; rows are named by ID.

| still differs | Project 2021 | the port |
|---|---|---|
| row order, IDs, row 0 | project row 0 + IDs 1..83 | **match** |
| Timeline strip | hidden (the file stores it hidden) | hidden — **match** (W24-3) |
| timescale | year / quarter / month | week / day |
| sheet columns in view | 8 (Name, Dur, Start, Finish, % Com, Pre, Suc, SUMMARY) | 5 visible before the pane edge (the file's 10-column table is wider) |
| bar palette / labels | the FILE's styles: navy summaries, red/green task bars, many labels | measured stock teal/light-blue; sparse labels (W24-18 partial) |
| sheet text | the file's Arial 8; highlighted rows | UI font; summaries bold only (W24-13 partial, W24-19) |
| title | `<file>.mpp - Project Professional` | `<file>.mpp - MS Project Shell` (ruling 5) |
| status mode | New Tasks: Auto Scheduled | **match** |

## 5. CI (per brief section 0)

**CI not run: publication hold.**  Tim has not lifted the hold (`START-HERE.md`
carries no lift line and there is no commit from him saying so), so no workflow
was dispatched and nothing was pushed.  The local gates above are the evidence.
When the hold is lifted, dispatch **both** workflows explicitly
(`workflow_dispatch` on `ci.yml` and `qt.yml`): `workflow_run` only registers
from the default branch's workflow file, so it cannot fire for `wave24` before
the merge.  `ci.yml` now also runs the corpus-free `plan_recalc` step and skips
the duplicate run after a tag.

## 6. Errata for wave-23 claims (W24-16)

* **Project row / Timeline** — wave23-complete §3 said "project row 0 matches"
  and §5 called both settings application state.  False: both are stored
  (`0x03400009`, `0x03400008`), as measured above.  The project row matched only
  on PROJECT_A; the committed fixtures store it OFF.
* **Reader check count** — wave23-complete §2 said 1025 and review finding 18
  could not reconcile 1016 -> 1025 (+9) with ~14 new check/equal calls.  The
  count was not reproduced from that diff; the current measured numbers are
  **1063 with corpus / 383 with MPP_NO_CORPUS=1**, and many checks are emitted
  inside per-row loops, so a constant "+N calls" is not a valid prediction.
  This record supersedes the old number.
* **"One dispatch covers the core lane"** — it never did before the merge
  (workflow_run only registers from the default branch).  Both are dispatched
  explicitly until then.
* **stored_table SUMMARY comment** — the second statement (summary rows 1/2/6
  read No) was the measured one; the stale first sentence is removed there.

## 7. The 16 decisions re-scored (wave-23 close as the starting point)

| # | decision | wave 23 | after wave 24 | why |
|---|---|---|---|---|
| 1 | all seven tabs | PARTIAL | **PARTIAL** | labels/order/pin pass; control kinds/icons still absent |
| 2 | full backstage | PARTIAL | **PARTIAL** | unchanged |
| 3 | original icon set | NOT DONE | **NOT DONE** | placeholder circles remain |
| 4 | measured chrome metrics | DONE (structural) | **DONE (stronger)** | parity 10/10 including the task-bar colour |
| 5 | timeline strip | PARTIAL | **PARTIAL** | visibility now file-driven and matches PROJECT_A; View > Timeline toggle still missing |
| 6 | real Entry table | DONE | **DONE** | unchanged |
| 7 | render the stored table | PARTIAL | **PARTIAL** | order/ID/outline/project row/SUMMARY/stock palette; timescale + FILE styles still differ |
| 8 | status bar | PARTIAL | **PARTIAL** | unchanged |
| 9 | disabled commands + backlog | DONE | **DONE** | unchanged |
| 10 | light and dark themes | PARTIAL | **PARTIAL** | dark chrome still not repainted |
| 11 | new-project defaults | DONE | **DONE** | unchanged |
| 12 | structural measurement | PARTIAL (stronger) | **PARTIAL (stronger)** | parity 10/10 and colour not-found is now a failure; not every screen |
| 13 | Carlito bundled | DONE | **DONE** | unchanged |
| 14 | every dialog on every tab | PARTIAL | **PARTIAL** | Custom Fields is still the value editor, not the definition dialog |
| 15 | dialog semantics | PARTIAL | **PARTIAL** | six-tab Task Information; Custom Fields tab still read-only |
| 16 | split view | NOT DONE | **NOT DONE** | unchanged |

## 8. Second-pass review of `git diff wave23..wave24`

An independent read-only subagent reviewed `d236f91..cc4ce8a` (plus the staged
close record).  Its findings and dispositions:

| sev | finding | disposition |
|---|---|---|
| HIGH | a NINE-character multi-word client resource name survived in an `app_qt/sheet_views.cpp` comment; the scan's `MIN_LEN = 10` never saw it, so "0 hits" was not evidence | **FIXED**: the comment no longer quotes any string; the scanner now also searches multi-word RESOURCE names down to 6 characters (generic short words are not added); re-scanned tip is 0 |
| MED | the five older tracked `.mpp` fixtures (and two of them) embed the operator name / company path; wave 24 neutralised only the new fixtures | **DEFERRED (R14 residual)**: rebuilding synthetic-base/calendars and the wave-23 pair would change their pinned bytes and the eight engine oracles recorded from them; listed for Tim's decision together with the publication hold |
| MED | MPP12 Flag11..20 were copied into the wrong slots: their ids are not contiguous with Flag10, and Number1..5 ids fell in the accepted range | **FIXED**: the MPP12 path maps through `custom_field_kind()` / `custom_field_index()`; two MPP12 assertions pin the id mapping (the available MPP12 fixture has only Flag1 set, so the Flag11 branch is asserted against the custom field, not a set bit) |
| MED | the project-row editability assertion was removed from shell_test and its replacement did not exist | **FIXED**: `qt_uid_routing` now asserts the project row's cell is not editable AND `setData` is refused |
| MED | the project-row double-click test could pass without the click landing on row 0 | **FIXED**: `indexAt` must resolve to the project cell and the `doubleClicked` signal must fire exactly once; the first real-click block now also asserts the dialog's Name field is ID 1 |
| MED | the print-subtitle check passed a hard-coded task count and never exercised `print_plan` | **FIXED**: `MainWindow::current_print_job()` is the production path (print_plan calls it); the gate asserts its subtitle "5 of 5 tasks" |
| MED | the project_a text-style gate was retargeted to the stock fallback, which is the opposite of the capture | **ACCEPTED, LISTED**: the file's styles are not decoded; the fallback is the measured stock behaviour and the test pins the fallback.  Decoding the file styles (W24-13) must update this test |
| LOW | macOS print subtitle counted the project row as a task | **FIXED**: `make_print_request` counts RecordSet rows with uid != 0 and totals `plan.view.tasks.size()` |
| LOW | the all-20 flag fixture is aggregate-only (a swapped mask still yields 20) | **FIXED**: `wave24-flag2.mpp` and `wave24-flag20.mpp` are single-bit controls for the non-contiguous groups, committed and asserted |
| LOW | stale comments (Flag2..20 "unmeasured"; a truncated sentence in reader.h) and wrong `file_row` numbering comments | **FIXED** |
| LOW | MPP12 project-row Flag10 is not decoded | **DEFERRED**: no MPP12 project-row fixture exists; the MPP14 decode and the new fixture cover the committed case.  Listed for W24-13's style work |
| LOW | `PlanDocument::task_for_record_row` had no caller | **REMOVED** |

Everything else the reviewer checked was found sound (uid-only lookups, print
uid mapping, Insert template, the MPP14 flag table and view-setting decodes,
`--expect-fail`, `PLAN_RECALC_UPDATE_GOLDEN`, the CI workflow guards, the
per-row golden, and the gate numbers).

## 9. Operator-visible deviations

* Project row and Timeline visibility now follow the FILE: committed synthetic
  fixtures (which store both OFF/empty for the row, shown for the strip) render
  differently from wave 23's forced behaviour.
* The sheet's milestone italic and critical-red text are gone: they were
  PROJECT_A's file styles, not stock.  PROJECT_A itself now renders with the
  measured stock fallback until the file's text styles are decoded.
* The bar palette changed from blue/red/black to the blank-project measurement
  (teal task, light-blue critical, dark-teal summary, teal links).
* Nothing was pushed; `main` is untouched.

## 10. Start the next wave with this

Read `docs/START-HERE.md`, then this file.  The next block is
W24-13/18's file-style decode (text styles and bar-style colour records), then
W24-14's Custom Fields definition dialog, W24-17 timescale tiers, and the
W24-19..25 carried backlog.  Keep every gate in §2 green before each commit;
the publication hold still governs any push.
