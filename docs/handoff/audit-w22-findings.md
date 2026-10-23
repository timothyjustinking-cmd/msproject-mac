# Wave-22 audit findings (adversarial + parity), 2026-10-23

_Auditor: Fable (Claude), coordinator session on the Windows box.  Baseline: docs
`e252041`, code `abeafd2` (CI run 39912313201); the two differ only in docs/ and
memory/ (verified with `git diff --stat abeafd2 e252041`).  Tree clean at the start.
Operator decisions for this audit are in `wave23-brief.md` (the work order).  The
detailed per-lane evidence files are committed under `docs/handoff/audit-w22/`
(A2, A3, A4, A5, A7, A8); the raw gate logs and the capture set stay local in
`build/audit-w22/` and `corpus/reference/2026-10-23-audit/` (git-ignored)._

Severity scale (product): **Critical** = corrupts or loses .mpp data on save, or
crashes on open; **High** = wrong schedule numbers, or a gate that passes without
testing its claim, or a loaded file shown materially wrong; **Medium** = a visible
UI/behaviour difference from Project, or a process rule not holding; **Low** =
hygiene and documentation.  Every finding has a re-runnable check.

## 1. What the audit re-ran (all on fresh trees `build/vs-audit*`, Release, `/WX`)

| gate | claimed | measured | note |
|---|---|---|---|
| core `ctest -E plan_recalc` | 50/50 | **50/50** | 0 build warnings |
| `plan_recalc_tests` (`MPP_NO_CORPUS=1`) | 266 | **266 checks, 0 failures** | |
| `engine_diff_tests` self-test | 265 | **265 checks, 0 failures** | |
| engine `--suite` | 7/8 | **FAILS as documented**; 7/8 only with `--apply-bin build/vs-audit/Release/engine_diff_tests.exe` | the documented command resolves a stale `build/e1` exe which cannot apply `task-calendar` (A-4) |
| M2 `recalc --compare` | 332/332 | **332/332** (268 leaves, 64 summaries) | |
| `check_fixtures.py` | OK | **OK** | counts misleading (A-8) |
| app_qt ctest (local) | 15/15 | **0/15 then 15/15** | 0/15 until the Qt `bin` dir is on PATH (0xc0000135); undocumented precondition (A-10) |
| app/ headless ctest | 15/15 | **15/15** | |
| CI 39912313201 | green x3, 14/14 | **confirmed**: sha abeafd2, `workflow_dispatch`, qt-shell only, 14/14 each OS | the core workflow did not run on that sha (A-9) |
| UI parity 9/9 | 9/9 | reproduced from the CI artifacts locally | measures almost nothing (A-1) |
| CI dispatch by the audit | n/a | run **39926506405** on `e252041`: **success** on all three OSs | the one dispatch the operator allowed; proves the docs sha, not only abeafd2 |

## 2. Adversarial findings (A)

| id | sev | finding | evidence / re-run |
|---|---|---|---|
| A-1 | **High** | **"9/9 UI parity" proves almost nothing.** The nine comparables are row pitch, header band height, nine grid-column widths, splitter fraction, day-cell pitch, tier count and three flat fill colours, measured on a light, selection-less render of a 3-task fixture with no summary/milestone/critical rows, against one Project screenshot (`corpus/bench/msproject-gantt.png`). Chrome, bars, cell text, selection, timeline, status bar, theme and font are excluded. All eight regressions tried in review pass unchanged (bar colours, stored-table values, selection `#DCE9F5` vs `#A2D3A0`, ribbon reorder, status text, timeline missing, dark theme broken, font fallback). The "pixel-difference score" wave 20 said exists does not exist anywhere. CI never runs `parity_report.py`; the three OS renders differ by 2.4-3.2 % of pixels and nothing checks it. `qt_parity_geometry` compares widgets to the same `shell_style.h` symbols production reads. | `audit-w22/A8-ui-parity-gate.md` Q1-Q7; `python scripts/parity_report.py` on `build/ci-w22-final/*` |
| A-2 | **High** | **S6's "independent mpp::load_file oracle" is the app's own path**: same reader, same adapter (`plan_input_from_document`), same formatter (`msqt::predecessors_text`), same `SheetFormat`. The 83/83 sweep proves wiring only. Independent evidence = 15 of the 21 pinned literals (the 4 lagged pins encode the port's "days" where Project prints "d"; task 42's pin is below the capture's fold, yet `plan_document.cpp:67` says the capture shows it), the hand-built `%` strings, and one constructed-plan unit test. Lag units (`edays`, hours, weeks, %), FF/SF letters, negative lag and a uid reorder are uncovered. Resource Names has never rendered non-empty on any file. CI's copy of the gate is blank-vs-blank. | `audit-w22/A3-s6-independence.md`; `app_qt/tests/stored_table_test.cpp:416-447, 782-857` |
| A-3 | **High** | **The per-row gates run in the wrong row order on both sides** (see B-1): the port lists tasks in uid order with renumbered IDs and no outline; the S6 oracle iterates the same order, so 83/83 cannot see it. | open `corpus/project_a.mpp` in the port and in Project; compare row 1-4 |
| A-4 | **Medium** | **Engine 7/8 is an honest count but the wording and the gate are not.** `--suite` has no exclusion list: constraint-mso is a hard FAIL (`1/247`, exit 1) - "deliberately not scored" is wrong, "deliberately left failing" is right. `--suite` is not a ctest or CI lane. "Production save path writes 9600" is a statement about `mppwrite::save_plan`, which has only two callers, both tests; no app/, app_qt/ or tools/ code calls it. The MSO self-test's nine-row table is self-fit (only row 1's 12000 is a recorded Project value); the oracle JSONs carry no recorded-at/version/hash provenance; both sides of every comparison are dumped by the same `core/mpp` reader. The documented `python scripts/engine_diff.py --suite` fails today because it resolves a stale `build/e1` exe. | `audit-w22/A4-engine-score.md`; `build/audit-w22/15-engine-suite.log` vs `15b-engine-suite-auditbin.log` |
| A-5 | **Medium** | **The ribbon inventory gate is a duplicate copy, not an independent reading.** The test literal (`ribbon_inventory_test.cpp:87-256`) and the app table (`ribbon_bar.cpp:72-245`) are content-identical (0 differences in 163 data lines, same commit a60453b), so a transcription error pasted twice passes. The enabled-iff-implemented sub-check compares against the same function the builder used. A trailing group silently hidden at 1920 passes on every tab but Task; the 900 px pass does not re-check the inventory. The audit's own re-read of the strips (`audit-w22/A2-fable-readback.md`) confirms labels and order on all seven tabs, and shows what the counts omit: control kinds (combos, checkboxes, date field, gallery, dialog launchers) and icon-only buttons (4 align/wrap, 5 percent, 5 schedule icons). R6 also removed live commands (Save as PDF, Export PDF, Task Sheet, Resource Sheet/Usage) from the ribbon; menu_model keeps them. `ui-reference-set.md` misplaces Add Resources (it is the Insert group, not Assignments). | `audit-w22/A2-ribbon-pinned.md`, `A2-fable-readback.md` |
| A-6 | **Medium** | **E7's evidence is not reproducible from a clean clone.** (i) the property-name table (`QNEWTASKSAREMANUAL` index 64) is a grep over git-ignored `decompiled/part_0000.c`, and the function named in e7.md is a label, not the function; (ii) "MSPDI 10/10" has no script, only 5 XMLs in git-ignored `corpus/probe` carry the element, and the mpp-to-xml pairing is prose; (iii) the 133-file scan script lives uncommitted in `build/e7-w22/`. The scan's output was re-tallied in this audit (111 kind=2 lines; 102/9/16/6 split as claimed), so the conclusion stands; the evidence trail does not. | `audit-w22/A5-e7-commands.md` |
| A-7 | **Medium** | **A committed golden can be rewritten by the documented ctest line, and the committed fixture is opened in Project in place.** `plan_recalc_tests.cpp:1276` writes `tests/data/project_a_recalc_golden.tsv` unconditionally whenever the corpus exists and `MPP_NO_CORPUS` is unset; `ci.yml:98` runs ctest with no `-E`. `scripts/oracle_w11.py`, `oracle_w12.py`, `oracle_w14.py` and `msproj_bench.py` open `tests/data/synthetic-base.mpp` in Project with `ReadOnly=False` (this is the mechanism behind W57 F3). | `audit-w22/A7-fixtures.md` A7-1, A7-2 |
| A-8 | **Low** | "3 documented hashes, 3 tracked .mpp" = 2 .mpp + the recalc golden, and the third .mpp is `analysis/test_project.mpp` with no pinned hash. `tests/data/README.md` documents 3 of 19 tracked files and still says synthetic-base is "the only .mpp that may be committed". `check_fixtures.py` is run by no workflow. W57 F1 is fixed (`kMaxFormulaDepth = 64`) but `tests/fuzz/README.md`, `tests/fuzz/CMakeLists.txt` and `docs/audit-findings.md` still say 160/open. | `audit-w22/A7-fixtures.md` A7-3, A7-4 |
| A-9 | **Medium** | **"CI green on all three" covers the Qt shell only.** `ci.yml` (core) triggers on `workflow_dispatch` and `v*` tags; it did not run on abeafd2. The "one dispatch per milestone" rule did not hold: 15 qt-shell dispatches on 2026-10-23, 9 red. | `gh run list --limit 15`; `.github/workflows/ci.yml:32-35` |
| A-10 | **Low** | The local app_qt gate needs `build/Qt/6.5.3/6.5.3/msvc2019_64/bin` on PATH; nothing documents it, and a fresh tree reports 0/15 (`0xc0000135`). The 13/14/15 counts: 14 in CI (no corpus), 15 locally; "13" appears in no current gate. | `build/audit-w22/22-qt-ctest.log` vs `22b-qt-ctest-withpath.log` |
| A-11 | **Low** | `docs/ui-reference-set.md` says 27 files / 21 Project captures; the folder and manifest hold 28 / 22. | `python -c` over `manifest.json` |
| A-12 | **Low** | The spec's "Options all 13 pages" (decision 14) is wrong: Project 2021 has 11 (General, Display, Schedule, Proofing, Save, Language, Advanced, Customize Ribbon, Quick Access Toolbar, Add-ins, Trust Center). | audit capture set, group 4 |

## 3. Parity findings (B) - same file open in Project 2021 and in the port

Measured live on 2026-10-23 with `build/audit-w22/project_a-audit.mpp` (a copy of
`corpus/project_a.mpp`) in both programs, both windows maximised.  Under decision 9
("100 %, no good enough") every difference is a finding.

| id | sev | finding |
|---|---|---|
| B-1 | **Critical** | **Task order and outline are wrong.** The port lists rows in uid order with fresh 1..n IDs, no project summary row 0, no indentation, no collapse triangles. Project shows ID/outline order with the hierarchy. Every row-wise view, gate and export inherits this. |
| B-2 | **High** | The Timeline strip is shown (Start/Finish + bar); Project's stored view for this file has no timeline. The file's view state is not honoured. |
| B-3 | **High** | Timescale: the port draws week/day tiers; the file's Gantt is stored at year/quarter/month tiers. |
| B-4 | **High** | Bars: no project-summary bar (no row 0); summary bars and link lines follow the wrong row order; bar/milestone labels and the file's bar palette (navy summary, green complete, red critical) are not reproduced; weekend shading/gridlines differ. |
| B-5 | **High** | Icons: nearly every ribbon button carries the same grey placeholder circle; only Paste/Cut/Copy/Task/Information/Filter/Find/Clear/Fill have glyphs. Decision 3 "DONE: original monochrome icon set" is not what renders. |
| B-6 | **Medium** | Ribbon control kinds: Font name/size are buttons not combos; no highlight/font-colour split buttons; no 0/25/50/75/100 % buttons; no indent/outdent/link/unlink/split buttons; View > Highlight/Filter/Group by and Timescale are buttons not combos; Split View Timeline/Details are buttons not checkboxes; Status Date is a button not a date field; Format > Critical Tasks/Slack/Late Tasks and Show/Hide are buttons not checkboxes; Gantt Chart Style is an empty caption (Project: 16 swatches + gallery expander + dialog launcher); Columns lacks the four align/wrap buttons; Font group lacks its dialog launcher. Labels and order match (R6 held). |
| B-7 | **Medium** | File tab is a filled dark-blue block and the backstage rail is dark blue; Project 2021 (Colorful/system) shows "File" as a plain tab and a light rail. Backstage pages are one-paragraph stubs: New has no templates or Recent list, Open has no Recent/Places, Print has no preview or settings, Share/Export are reduced; no Account page (so About is unreachable, confirmed). |
| B-8 | **Medium** | Options is a backstage page with one combo and an "All preferences..." button; Project opens the 11-page dialog. |
| B-9 | **Medium** | Status bar: no "Ready"; four view buttons (Gantt Chart, Task Usage, Resource Sheet, Resource Usage) where Project has five (Gantt Chart, Task Usage, Team Planner, Resource Sheet, Report); the bar is black in the light theme (Project: light grey). |
| B-10 | **Medium** | Window title: "<project title> - <full path>" vs Project's "<file name>.mpp - Project Professional". |
| B-11 | **Low** | Text styles: summary bold and critical red are applied; the file's milestone italic/red and the blue highlighted summary rows are not; sheet font is Carlito where the file's text style is Arial 8; row pitch differs. |
| B-12 | **Medium** | Selecting "Office Dark" in Options leaves the live window light (no repaint; decision 10 is not demonstrable in the app). Project's Black/Dark Gray themes darken only the chrome; the sheet and chart keep the file's light colours - the port's spec text ("dark custom surfaces") should follow that. |
| B-13 | **Medium** | Task tab > Properties > Information opened a "Task Fields" dialog (six custom fields), not Task Information; double-clicking a task name cell opened nothing (main_window.cpp:287 says it should). Project opens the six-tab Task Information on both. |
| B-14 | **Low** | Project's Move Project first asks "tasks with actual values - move?"; Update Project, Links Between Projects, Custom Fields, Change Working Time (with Exceptions/Work Weeks tables and the Details/Options buttons) and the WBS / Set Baseline menus are all absent from the port's inventory of seven dialogs. Captures of each are in the audit set for the build. |

## 4. The 16 decisions, re-scored against the live app

| # | decision | wave-22 said | audit says | why |
|---|---|---|---|---|
| 1 | all seven tabs | DONE | **PARTIAL** | labels/order right; control kinds and icon-only buttons missing (A-5, B-6) |
| 2 | full backstage | DONE | **PARTIAL** | rail right minus Account; pages are stubs; colours wrong (B-7) |
| 3 | original icon set | DONE | **NOT DONE** | placeholders on nearly every button (B-5) |
| 4 | measured chrome metrics | DONE | **UNVERIFIED** | the geometry gate reads the constants production reads (A-1 Q6); no independent measurement this audit |
| 5 | timeline strip | PARTIAL | **PARTIAL, and shown when the file hides it** (B-2) | |
| 6 | real Entry table | DONE | DONE (not re-measured) | |
| 7 | render the stored table | PARTIAL | **FAILING** | order/outline wrong (B-1); timescale/bars (B-3, B-4) |
| 8 | status bar | DONE | **PARTIAL** | mode text right; Ready, five buttons, colour wrong (B-9) |
| 9 | disabled commands + backlog | DONE | DONE | generated backlog present |
| 10 | light and dark themes | PARTIAL | **NOT DONE** | dark does not apply live (B-12) |
| 11 | new-project defaults | DONE | DONE (not re-measured) | |
| 12 | structural measurement | PARTIAL | **PARTIAL (weaker than stated)** | no pixel diff exists; gates largely self-referential (A-1) |
| 13 | Carlito bundled | DONE | DONE, font check is tautological (A-1 Q7) | |
| 14 | every dialog on every tab | PARTIAL | **PARTIAL** | seven dialogs; Options should be 11 pages (A-12) |
| 15 | dialog semantics | PARTIAL | **PARTIAL, with a wiring defect** (B-13) | |
| 16 | split view | NOT DONE | NOT DONE | |

## 5. What held

- The scheduling numbers: M2 332/332 and the engine suite's 7 passing scenarios
  reproduce byte-for-byte on a fresh tree; 50/50 core tests; writer 1693; reader
  1016; cal 785.
- The seven ribbon tabs' labels and order match the clean captures exactly (the
  audit's own read).
- No test or tool writes under `tests/data` or `corpus/` when run the documented
  way; every committed fixture matches its pinned hash; goldens carry no client
  strings; W57 F1 and F3 are resolved in the tree.
- CI run 39912313201 is on the sha the gate names; the 14-vs-15 difference is the
  corpus-only test, as documented.

## 6. Re-run recipe (what this audit executed)

```bat
build\audit-w22\run-gates.cmd                 :: configure+build+ctest core, qt, app; M2; suite; fixtures
build\audit-w22\run-qt-ctest.cmd              :: app_qt ctest with the Qt bin dir on PATH
python scripts\engine_diff.py --suite --apply-bin build\vs-audit\Release\engine_diff_tests.exe
```

Logs: `build/audit-w22/1*-*.log`, `2*-*.log`, `3*-*.log`.  Captures:
`corpus/reference/2026-10-23-audit/` (git-ignored, README inside).
