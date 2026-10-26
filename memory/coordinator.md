# memory — coordinator (2026-10-26, wave 30 closed on branch)

**Entry point:** `docs/START-HERE.md`.  Wave gate:
`docs/handoff/wave30-complete.md`.  History:
`memory/archive/coordinator-through-wave20-full.md`.

## Where the branches are

`main` = **fcb1c10** = `origin/main`.  **`wave23`** (`e1d40b8` =
`origin/wave23`), **`wave24`** (`6dbf126`), **`wave25`** (`c012813`),
**`wave26`** (`1299cc7` = `origin/wave26`), **`wave27`** (`bc52cb8` =
`origin/wave27`), **`wave28`** (`21646f4` = `origin/wave28`) and **`wave29`**
(`2a7fa4b` = `origin/wave29`) are closed and **not merged**.

**Wave 30 is closed on `wave30`, cut from `wave29` (`2a7fa4b`), pushed and not
merged**; `origin/wave30` = the CLOSE commit, final code tree `0aca9d2`.
Wave 30 commits: brief `063fcae`; G0 W30-0.0 `8314f6c`, 0.1 `bd83a6b`, 0.2
`022f68c`, 0.3 `6599e33`, 0.4 `660d72e`, 0.5 `048ed72`, 0.6 `4a774c5`, 0.7
`b2517bc`, G0 record `b077409`; G1 1.0 pairs `0d0770e` + writer `d51bcc0`, 1.1
`70b757a`, 1.2 `2a99c98`, 1.3 `ac008fa`, 1.4 `8c1861b`, 1.5 Notes `9ee38f6` +
Details `2e79aec`, 1.6 `b2c3366`, 1.7 `2b131ec`, 1.8 `daaf3a1`, 1.9 `36298e9`,
1.10 `5bbcf17`, 1.11 `3aa4549`, G1 record `3dcc5a7` (temporary `0e31238` reset,
identical tree); G2 2.0 `363645c`, 2.1 `eff96fd`, 2.2 `b2e957e`, 2.3 `f8086c0`,
2.4 `8cdc56a`, 2.5 `913f81b`, 2.6 `cd85fd3`, G2 record `2d22bbb`; G3 3.0
`e6b50e5` + LF `ec0138d`, 3.1 `fb89190`, 3.2 `df0c484`, 3.3 `5a87d6d`, 3.4
`ada5e66`, 3.5 `7c189b7`, G3 record `3f935e0`; CLOSE fixes W30-C1 `0770e94`
(backlog Resource Details), C2 `f24ad25` (Set Baseline backlog row), C3
`2db8df2` (`memory/writer.md`), C4 `ec29e31` (unused symbols), C5 `0aca9d2`
(physical assignment rows/skipped links/raw resource word); then the CLOSE
docs commit.  Every group marker (`WAVE-30-G0-COMPLETE` ..
`WAVE-30-G3-COMPLETE`, then `WAVE-30-COMPLETE`) is in `wave30-complete.md`.

## Numbers (wave 30 close, fresh trees build/w30-close-fresh{,-app,-qt}, final code tree 0aca9d2)

* core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` exit 0 / exit 0;
  core ctest `-E plan_recalc` **52/52**; plan_recalc **512** with corpus /
  **266** without; reader **1100** with corpus / **420** without; M2
  **332/332**, **268/268** and **64/64**; app **22/22**; app_qt **32/32**;
  fixtures OK (**131** documented hashes, **167** tracked `.mpp`), paths OK
  (214), oracle provenance OK (8).  The log is
  `build/w30/W30-CLOSE-fresh-gates.log`.
* Engine **7/8** (`constraint-mso` deliberately expected-failing;
  `mppwrite::save_plan` is test-only); selftest 53 checks, suite 8 recorded
  (7 pass + 1 expected), 0 unrecorded, exit 0.
* Parity: **wave23-outline 10/10** and **wave25-auto colour 1/0**, re-run on
  the fresh Qt shell at 0aca9d2 (`build/w30/W30-CLOSE-parity.log`); every
  measure PASS.  The pixel score is not re-run (no reference PNG locally).
* CI: **green** on `0aca9d2`, the first dispatch, with no re-dispatch:
  qt.yml **40307814439** (`windows-qt`, `macos-qt`, `linux-qt`, both parity
  gates) and ci.yml **40307818793** (`windows-msvc`, `ubuntu-gcc`); snapshots
  `build/w30/close-ci-{qt,ci}-status.json`.
* Privacy: line scan **0 files / 0 hits** on HEAD and **0 commit messages** on
  `wave29..HEAD`; normalised `guard_names.py` **0 hits, exit 0** on
  `wave29..HEAD` and `origin/wave29..HEAD`; no `client-string-<8 hex>`.  The
  66 new fixtures use the neutral `C:/w30` root and neutral metadata.
  Residuals (Tim's decision): two pre-wave fixtures (`synthetic-base.mpp`,
  `wave23-outline.mpp`) carry the operator's name three times each, 69 tracked
  text files (historical docs) carry it, and the 13 unsalted FNV-1a name
  hashes removed in wave 27 remain in older wave-26 commits plus
  `tests/data/project_a_golden.tsv`.  **Run BOTH scans.**  The 156-name
  scanner still cannot see skipped-row names (print uid/row only).

## Wave 30 result (what a later session must not re-break)

* **G0's measured-shape rule is now up-front everywhere it touches.**  The edit
  model refuses, with a status-line reason and a test, a real (even zero-work)
  assignment on an indent's new summary, an automatic indent/outdent on any
  linked file, an outline move over progress/resource work/queued actuals, the
  0 % -> 0 % date-update target, a second progressed leaf in a summary's
  recompute chain (or a manual summary), every contour shape but the measured
  9600-tick / 100 % / no-calendar one, and Add Link's second-incoming-link and
  progressed-successor shapes.  The writer keeps its checks as last-line
  defence.  **The old "0x0f first link / 0x07 later ones" rule is FALSIFIED**
  (the byte is the successor's driving state) and is corrected in code, tests
  and `memory/writer.md`.
* **The file-side shape harness (W30-C5)**: `TaskRow::assignment_rows` carries
  the PHYSICAL TBkndAssn row count (live + skipped rows naming the task),
  `AssignmentRow::resource_uid_raw` keeps the +8 word, and
  `PlanInput::unreadable_links` carries reader-skipped links a pending edit has
  not dropped.  Actuals/indent refuse a physical/live mismatch, raw 0 (only
  `0xFFFF0001` was measured) and skipped links; Add Link refuses the same
  skipped links.  A pending delete's dropped link is not counted (the measured
  delete-and-re-add stays accepted).
* **G1 enabled (write/read):** Manually Schedule and Mode's manual entry (the
  measured task-mode writer, save-pair + reopen tested); Details (shows the
  Task Form; the View check box stays the toggle); Scroll to Task (view state
  only, `scroll_gantt_to`); Fill (Start column only, one undo step, every other
  column refused); Inspect (read-only pane of 15 plan facts by uid).  Insert
  Summary/Milestone, Inactivate, Respect Links, Move, Notes, Add to Timeline
  and the text-style group are DEFERRED with committed pairs; Deliverable
  stays disabled with the exact tooltip.
* **G2 enabled:** Details opens a READ-ONLY Resource Form in the shared
  Details splitter (one form per view, followed by UID).  Add Resources,
  Information/Notes, Assign Resources (add + remove) and Level All are DEFERRED
  with committed pairs; Resource Pool and Team Planner stay disabled with
  named live checks.
* **G3 enabled:** Spelling, a read-only report over task names with the port's
  OWN 1867-word list and suffix rules; no Microsoft dictionary, no correction,
  no write.  Calculate Project (F9), Set Baseline 0 for the whole project and
  the WBS code definition are DEFERRED with committed pairs; Subproject and
  both Add-ins commands stay disabled with named live checks.
* **The wave's pairs are fixture triples** (control, control-null, edit) with
  the conditions written in `tests/data/README.md`; the save-pair tests
  compare every owned stream field by field under
  `tests/save_pair_noise.h`, never with the control.  Wave 30 added 66
  fixtures (101 -> 167 tracked `.mpp`), five app_qt gate targets
  (`qt_scroll_to_task_gate`, `qt_fill_gate`, `qt_inspect_gate`,
  `qt_resource_details_gate`, `qt_spelling_gate`; `qt_split_view_gate`
  extended) and two app test targets (`app_edits_task_mode`, `app_spelling`).
* **The backlog is 95 ribbon commands with 69 disabled** (77 at wave 29), 26
  implemented (18), and 51 menu commands with 7 disabled; regenerate it with
  `python app_qt/tools/command_backlog.py` (it writes LF explicitly).
* **The CLOSE review is binding**: 0 Critical / 4 High / 7 Medium / 10 Low
  (C1-C6 save paths, U1-U7 UI, A1-A8 record audit).  All Highs fixed
  (C1/C2 `ec29e31`, C3 `0aca9d2`, A1 by record correction); U2 fixed
  `0770e94`; C4/C5/C6/U1/U3-U7 queued with fix directions; the record audit
  A2-A8 corrected in the record.  A1's process item: 11 measurement commits
  carry oracle/hash scripts although their messages said "fixtures only";
  history is not rewritten.

## Next

Wave 31 continues the plan's wave-30 work
(`docs/handoff/plan-waves27-30.md`): the View tab's commands (Sort, Outline,
Filter, Group by, Highlight) and the second view family (Resource Sheet, Task
Usage, Resource Usage) at Gantt fidelity.  Every command that
writes needs its own Project measurement before it is enabled (rule 1) and a
save-and-reopen test.  Behind it: wave 30's "Found, not fixed" list (C4/C5,
U1/U3-U7, the reader's fractional ordinal and `Task.Active`, the save-pair mask
and the missing VarMeta/214/DSI/baseline/scheduler writers, the inert Add to
Timeline build, the fresh-file save instability, the shared writer gaps) and
the carried wave-26/27/28/29 residuals.  Keep the CLOSE gates green before
every commit and run BOTH name scans.
