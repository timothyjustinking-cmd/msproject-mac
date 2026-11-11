# Wave 35g, group G1: builder record

Branch `wave35g`. One finding id per commit, each with the trailer
`Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35g)` (checked by `build/w35g/commit.sh`
before the commit and counted after). No writer added, no refusal that guards a save lifted, no
pair measured, no COM, no VM or trace touched, no push, no workflow dispatch, no history rewrite.
`app/main.mm` was not changed (see 1.5).

Gate tooling as in `gates-g0.md`: `build/w35g/gates.sh <label> qt` for a commit that changes
source or tests, `build/w35g/docgates.sh <label>` (the scans, the fixture check and
`git diff --check`) for a records-only commit. A log is named for the PARENT commit of the item,
so item N's logs are `build/w35g/<parent-sha>-W35g-<label>-*.log` and `*-gates.status`. The red
and green runs are `build/w35g/<label>-red-run.log` and `<label>-green-run.log` (git-ignored
`build/`). Item labels 1.1 to 1.16 are the order of the commits (the subject of each commit), not
the brief's id numbers.

| Item | Finding | Commit | Status |
|---|---|---|---|
| 1.1 | R35fG0-5 | `1663aaf` | fixed (test: 0.1's read-only case) |
| 1.2 | R35fG0-6 | `3518bc4` | fixed (test: 0.3 asserts the Level is shown) |
| 1.3 | R35fG0-7 | `78804c8` | fixed (test: 0.2's status needle) |
| 1.4 | R35fG0-4 | `33e15c0` | fixed (clearing the sort says a Level still shown is session only) |
| 1.5 | R35fG1-2 | `17430f3` | fixed (refusal beside a Sort or Show Level advises a step it accepts) |
| 1.6 | R35fG1-4 | `91627f3` | fixed (partial-apply status counts the fields) |
| 1.7 | R35fW-3 | `26b5697` | fixed (the status says a dropped Percent or custom-field change was not queued) |
| 1.8 | R35fG1-3 | `60d23a6` | fixed (tests for the untested parts of W35f-1.8; coverage limit corrected) |
| 1.9 | R35fG0-2 | `83ac22c` | fixed (records) |
| 1.10 | R35fG0-3 | `345cbd9` | fixed (records) |
| 1.11 | R35fG0-8 | `e43c1c0` | fixed (records) |
| 1.12 | R35fW-1 | `f831008` | fixed (records) |
| 1.13 | R35fW-2 | `0bf4c96` | fixed (record) |
| 1.14 | R35fG0-9 | `5a7c43a` | fixed (rewrap; style only) |
| 1.15 | R35fG0-9 | `1dade53` | fixed (finishes a plan line that 1.14 left at 113 columns) |
| 1.16 | R35fG1-5 | `0c35d05` | fixed (rewrap; dead setup dropped) |

## Gate line

Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; trees `build/w35b-close-fresh`,
`-app` and `-qt`, built incrementally.

- Source and test commits (1.1 to 1.8 and the rewrap, `fe61064-W35g-1.1-*`, `1663aaf-W35g-1.2-*`,
  `3518bc4-W35g-1.3-*`, `78804c8-W35g-1.4-*`, `33e15c0-W35g-1.5-*`, `17430f3-W35g-1.6-*`,
  `91627f3-W35g-1.7-*`, `26b5697-W35g-1.8-*`, `1dade53-W35g-1.16-*`): core build 0 warnings, core
  `ctest -E plan_recalc` 65/65, app build 0 warnings, app 34/34, app_qt build 0 warnings, app_qt
  52/52, fixtures 0, guard_names 0, scan_client_names 0, client-string grep none.
- Records-only commits (`60d23a6-W35g-1.9-*`, `83ac22c-W35g-1.10-*`, `345cbd9-W35g-1.11-*`,
  `e43c1c0-W35g-1.12-*`, `f831008-W35g-1.13-*`, `0bf4c96-W35g-1.14-*`, `5a7c43a-W35g-1.15-*`):
  fixtures 0, guard_names 0, scan_client_names 0, client-string grep none, `git diff --check` 0.
  No source changed, so the build and test numbers above stand for them.
- `check_markers` reports 1 hit in every log: `spec/writers/evidence/trace-captures-r5.md` (from
  `5863054`, not this wave; that directory is barred to this wave). See "Found, not fixed".
- No out-of-memory build error occurred in any run.

## Guards seen red

Each with a temporary mutation of the source (or the parent's file, stashed), restored afterwards
and checked by `diff` against a saved copy, then a green run. Counts are the FAIL lines in the
named log.

- 1.1: `1.1-red-run.log`, 1 FAIL ("read-only: the Level's note is the read-only reason"), the
  read-only branch of `apply_outline_level` made unreachable (`level < 0 &&`). The "so the level
  is not queued" needle stayed green in that run, because the builder's own note wins on that
  file; the first needle is the discriminating one. Green: `1.1-green-run.log`. (A first mutation
  failed to build under MSVC's warnings as errors: C4127 on a constant `false &&`. The first run
  had build=1, so ctest ran the stale binary; the build log showed it and the mutation was
  changed.)
- 1.2: `1.2-red-run.log`, 2 FAILs ("Undo then Level: the session Level is shown (fewer rows than
  the sort)" in `qt_sort_persist`, and the existing "sort first: the session Level is shown ..."
  in `qt_outline_persist`), the `show_outline_level` call skipped while a sort is shown.
- 1.3: `1.3-red-run.log`, 1 FAIL ("Custom Fields beside a queued Calculate Project: the status
  line names it"), the refusal in `show_custom_fields` replaced by a silent return. That mutation
  passed before this commit.
- 1.4: two reds. `1.4-red-run.log`: the note condition made unsatisfiable, FAIL "sort first:
  clearing the sort says the Level shown stays session only". `1.4-red2-run.log`: the note always
  added, FAIL "sort first: clearing the sort with every row shown adds no Level note".
- 1.5: `1.5-red-run.log`, `main_window.cpp` at the parent (this commit's source change stashed),
  3 FAILs in 2 tests (the Formula... and Table / Date Format refusals beside a queued Sort, and
  Table / Date Format beside a queued Level, now naming the step each accepts).
- 1.6: `1.6-red-run.log`, `main_window.cpp` at the parent, 1 FAIL ("partial apply (two fields):
  the status counts the fields and says one Undo takes back one"). The assertion that one Undo
  leaves the Name committed holds on both trees. Green: `1.6-green-run.log`.
- 1.7: `1.7-red-run.log`, `main_window.cpp` at the parent, 1 FAIL ("partial apply (trial): the
  status says the percent change was not queued").
- 1.8: three reds, one per test (`1.8-red-run.log`, `1.8-red2-run.log`, `1.8-red3-run.log`): the
  Font Bold entry made to pass its helper plus the Custom Field Formula refusal reverted to
  Calculate-only, 5 FAILs in `qt_task_mode_document_edit_gate`; the same revert, 3 FAILs in
  `qt_sort_persist`; the Resource Information entry made to pass its helper, 2 FAILs in
  `qt_resource_information_gate`.

1.9 to 1.13 are records, and 1.14 to 1.16 (the rewraps and the dead setup) change no meaning, so
none has a red; the gate for 1.16 is that every suite still builds and passes.

## Items worth reading

**1.1 to 1.3 (R35fG0-5, -6, -7), tests.** `qt_outline_persist` opens `wave33-fat-slack.mpp` (a
file with unreferenced sectors, which sets `read_only_reason_`) as a sixth argument in
`app_qt/CMakeLists.txt`. Each test now sets a sentinel status before the action so a needle
cannot be satisfied by an earlier message.

**1.4 (R35fG0-4).** The cleared-sort path of `apply_sort_choice` appends "; the level shown stays
session only, choose it again to queue it" when nothing is queued, the file is not read-only and a
collapsed row is still shown. That needed `PlanDocument::has_collapsed_rows()` (loops
`sheet_->is_collapsed(row)`); a view query, no writer.

**1.5 (R35fG1-2).** `document_edit_refusal` names a step the Sort or Show Level accepts ("choose
All Subtasks or undo the level first", "clear the sort or undo it first"); the Calculate and
generic texts are unchanged. `app/main.mm` has no Sort or Show Level queue wording (searched), so
there is nothing to mirror and no CI-gated change.

**1.6 and 1.7 (R35fG1-4, R35fW-3).** The partial-apply status takes an `applied_count`: one field
reads "applied (Undo takes it back)", more read "applied; Undo takes back one field per step",
and when a Percent or custom-field trial was dropped it ends "; the percent / custom-field changes
were not queued". Wording only: the commits, the dropped trial and the redo clearing are
unchanged.

**1.8 (R35fG1-3).** No source change. The tests drive Add Resources, Assign Resources, Font Bold,
Define WBS Code and Resource Information beside a pending date format, and Custom Field Formula
beside a queued Manually Schedule and a queued Sort. The erratum in
`wave35f-verification/gates-g1.md` lifts the coverage limit it recorded; `spec/writers/calculate.md`
and the two coverage-limit rows of `wave35f-complete.md` say the same.

**1.9 (R35fG0-2).** The 35c and 35d records read "fixed by W35e-1.x; reviewed by the coordinator's
35e G1 verification", with follow-ons R35eG1-1, R35eG1-3 for R35dG1-1 and R35eG1-2 for R35dG1-3;
none for R35dG1-2 and R35dW-4. The 35e CLOSE row for R35dG0-1 reads "closed by the coordinator's G0
review". Ten wordings in three files.

**1.10 (R35fG0-3).** "Nothing is built or pushed" is now "the edit is built but not pushed". The
0.1 red run is called the narrowing against the parent's refusal; the queue-branch `sort_active()`
check is credited to 0.3's mutation.

**1.12 (R35fW-1).** The three state files and `wave35f-complete.md` are corrected for G0 and G1
closed and reviewed, with R35fG0-* and R35fG1-* open from those reviews; R35cW-CI1 and R35cW-CI2
leave the "fixed in source; CI is its gate" sentence (they were proved by the green wave 35d run).
I left the "NOT pushed" sentences alone: the review did not name them.

**1.14 and 1.15 (R35fG0-9), a miss.** `docs/handoff/status-2026-11-10.md:54` has no line over 100
columns at this tree, so there was nothing to rewrap there. The first commit's rewrap of the plan
left one line at 113 columns; `1dade53` finishes it.

**1.16 (R35fG1-5).** Wraps the lines of the three test files the finding named and the lines this
wave added over 100 columns, and drops the `target` path and its `remove` from
`task_mode_document_edit_gate_test.cpp`. The commit message of `0c35d05` attributes two of the
`qt_sort_persist` lines to "W35g-1.2 and W35g-0.1"; they belong to W35g-1.8 and W35g-0.1. The code
is right; the attribution in that message is not, and it is not amended.

## Found, not fixed

- `check_markers.py` still reports the one hit in `spec/writers/evidence/trace-captures-r5.md`
  (barred directory), as in `gates-g0.md`.
- Lines over 100 columns elsewhere in `memory/coordinator.md`, `docs/START-HERE.md` and the plan
  (long table rows and paragraphs the finding did not name) are untouched.
