# memory — coordinator (2026-10-23, wave 23 closed on branch)

**Entry point:** `docs/START-HERE.md`.  Wave gate:
`docs/handoff/wave23-complete.md`.  History:
`memory/archive/coordinator-through-wave20-full.md`.

## Where the branches are

`main` = **488481f** (wave-22 docs close).  **`wave23`** is pushed and NOT
merged: 19 commits, one per finding plus two fixture commits and the rebuilt
reference commit.  The local `wave23-with-screenshot` branch is an unpushed
backup whose history contains a Project screenshot — **never push it** (the
pre-push guard refuses it; the operator's 2026-10-23 ruling is numbers-only).

## Numbers (wave 23 on `wave23`)

* core ctest **51/51** (50 + engine_diff_suite); M2 **332/332**; plan_recalc
  **266** no-corpus / **512** with corpus (golden read-only); app **15/15**;
  app_qt **17/17** local / 15 CI; reader **1025**; writer 1693.
* Engine **7/8** (`constraint-mso` deliberately left failing; `save_plan` is
  test-only).  Fixtures OK (6 hashes, 5 tracked `.mpp`).
* parity vs Project's measured capture: **9/9**; pixel score **15.89 %**
  (report only).
* CI: qt-shell **39945118772**, ci **39945123565** on `wave23`.

## Wave 23 result

* **B-1 fixed**: rows in the file's ID order, stored IDs in the gutter, project
  row 0, outline levels/indentation/collapse, summary shading.
* **Flag10 decoded**: Task FixedMeta +25 bit 0x04 -> Task::summary_flag; the
  stored SUMMARY column renders Yes/No (and is NOT Task.Summary -- the capture's
  summary rows 1/2/6 read No).
* **A-2 fixed**: Pre/Suc text pinned to Project's COM strings (hashed golden)
  with per-unit lag labels read from `114/Props`.
* **A-1/A-5/A-7/A-9/A-10/A-6**: independent pin files, capture-literal gates,
  report-only pixel score, read-only golden, core CI coverage, E7 evidence
  committed.
* **B-9/B-10/B-13**: five status views, light bar, file-name title, Task
  Information wired to the action and double-click (Task Fields moved to
  Custom Fields).
* **B-2 premise overturned**: Timeline visibility is application state, not
  file state (COM CurrentView is "Gantt Chart" for every file; fresh captures
  show the strip).  Default shown.

## Deferred (wave 24 candidates)

1. Bar-style and timescale decode (`0x22400008`; storage located, encoding not).
2. Distinct icon glyphs (B-5) and ribbon control kinds (B-6).
3. 11-page Options + B-14 dialogs, split view, live dark chrome.
4. New-task writer acceptance fixture, clipboard display order, macOS/sf248.

## Traps

* Pixel beats prose; re-read the capture.
* Flag10 is the file's SUMMARY field, not Task.Summary.
* Timeline visibility is app state; a capture without the strip is not a stored
  byte.
* Screenshots never enter the repo; commit `*.measures.json` only.
* CI has no corpus: app_qt 15 there, 17 here.
* Serial builds, newline="\n", plan_recalc never with corpus by default.

## Next three actions

1. Tim reads `wave23-complete.md` and merges `wave23` if it holds.
2. Wave 24: decode the Gantt view blob (bars + timescale together).
3. Then the icon set and ribbon control kinds; Options/dialogs after.
