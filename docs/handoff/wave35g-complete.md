# Wave 35g: completion record

## G0: the Mediums

Builder record: `wave35g-verification/gates-g0.md`. Commits: `284c3cb` (0.1), `24a5c1d` (0.2). The
trailer is on each, checked by script.

| Item | Finding | Status |
|---|---|---|
| 0.1 | R35fG1-1 | fixed `284c3cb`; Project > Custom Fields... opens beside a queued exclusive edit, and only Formula... is refused (a queued Calculate still refuses before the dialog, W35f-0.2 kept); tested in `qt_sort_persist` and `qt_task_mode_document_edit_gate` |
| 0.2 | R35fG0-1 | fixed `24a5c1d`; the 1.1 record's bypass (b) sentence corrected in `wave35e-verification/gates-g1.md` and `wave35e-complete.md`; the five-task Level, Ctrl+Z, Start sort, Ctrl+Y test added to `qt_sort_persist` |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; 0.1
`1735250-W35g-0.1-*`, 0.2 `284c3cb-W35g-0.2-*`): core build 0 with 0 warnings; `ctest -E
plan_recalc` 65/65; app 34/34; app_qt 52/52; `check_fixtures` 0; `guard_names` 0;
`scan_client_names` 0; client-string grep none. `check_markers --range main..HEAD` reports 1 hit
in `spec/writers/evidence/trace-captures-r5.md` (from `5863054`, not this wave; see "Found, not
fixed").

**Guards seen red:**
- 0.1: the pre-open refusal at the parent, 6 FAILs (`build/w35g/0.1-red-run.log`); the callback
  refusal mutated so it cannot fire, 5 new FAILs and knock-ons (`0.1-red2-run.log`).
- 0.2: `redo_top_is_view_edit` narrowed to a Sort only, 2 new FAILs and knock-ons
  (`0.2-red-run.log`).

**Deferred:** nothing.

**Found, not fixed:**
- `check_markers.py` reports a marker hit in `spec/writers/evidence/trace-captures-r5.md`
  (arrived in `5863054`); that directory is barred to this wave, so it is left for the coordinator
  or the trace agent.
- A queued Calculate Project still stops Custom Fields... opening, as the brief says to keep
  W35f-0.2's guard (`gates-g0.md`).

No writer added, no refusal that guards a save lifted, no pair measured, no COM, no VM or trace
touched, nothing pushed, no workflow dispatched.

WAVE-35g-G0-COMPLETE

## G1: the Lows

Builder record: `wave35g-verification/gates-g1.md`. Commits: `1663aaf` to `0c35d05` (16 commits,
one finding id each; R35fG0-9 took two). The trailer is on each, checked by script.

| Item | Finding | Status |
|---|---|---|
| 1.1 | R35fG0-5 | fixed `1663aaf`; test: 0.1's read-only case in `qt_outline_persist` (a file with unreferenced sectors) |
| 1.2 | R35fG0-6 | fixed `3518bc4`; test: 0.3 asserts the session Level is shown |
| 1.3 | R35fG0-7 | fixed `78804c8`; test: 0.2's status needle tells the Custom Fields row from the row before it |
| 1.4 | R35fG0-4 | fixed `33e15c0`; clearing the sort says a Level still shown is session only |
| 1.5 | R35fG1-2 | fixed `17430f3`; a document-edit refusal beside a Sort or Show Level advises a step it accepts (`app/main.mm` has no such wording; no mirror) |
| 1.6 | R35fG1-4 | fixed `91627f3`; the partial-apply status counts the fields, since Undo takes back one |
| 1.7 | R35fW-3 | fixed `26b5697`; the status says a dropped Percent or custom-field change was not queued |
| 1.8 | R35fG1-3 | fixed `60d23a6`; tests for the untested parts of W35f-1.8, the coverage limit corrected |
| 1.9 | R35fG0-2 | fixed `83ac22c` (records) |
| 1.10 | R35fG0-3 | fixed `345cbd9` (records) |
| 1.11 | R35fG0-8 | fixed `e43c1c0` (record) |
| 1.12 | R35fW-1 | fixed `f831008` (records) |
| 1.13 | R35fW-2 | fixed `0bf4c96` (record) |
| 1.14, 1.15 | R35fG0-9 | fixed `5a7c43a`, `1dade53` (rewrap; style only) |
| 1.16 | R35fG1-5 | fixed `0c35d05` (rewrap; dead setup dropped) |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; logs
`build/w35g/<parent-sha>-W35g-<label>-*`): every source and test commit, core build 0 with 0
warnings, `ctest -E plan_recalc` 65/65, app 34/34, app_qt 52/52, `check_fixtures` 0, `guard_names`
0, `scan_client_names` 0, client-string grep none; the records commits run the scans and
`git diff --check`. `check_markers` reports 1 hit in `spec/writers/evidence/trace-captures-r5.md`
(from `5863054`, not this wave). No out-of-memory build error occurred.

**Guards seen red** (mutated or at the parent, restored, then green): 1.1 1 FAIL, 1.2 2 FAILs, 1.3
1 FAIL, 1.4 one FAIL for each of two mutations, 1.5 3 FAILs in 2 tests, 1.6 1 FAIL, 1.7 1 FAIL,
1.8 three reds (5, 3 and 2 FAILs); the log of each is `build/w35g/<label>-red-run.log` and the
quoted FAIL lines are in each commit message and in `gates-g1.md`. 1.9 to 1.13 are records and
1.14 to 1.16 are rewraps, so none has a red.

**Deferred:** nothing.

**Found, not fixed:**
- `check_markers.py` still reports the one hit in `spec/writers/evidence/trace-captures-r5.md`
  (barred directory), as in G0.
- R35fG0-9: `docs/handoff/status-2026-11-10.md:54` has no line over 100 columns at this tree, so
  nothing was done there. Lines over 100 columns that the finding did not name are untouched.
- The commit message of `0c35d05` misattributes two `qt_sort_persist` lines to "W35g-1.2"; they
  are W35g-1.8's. Not amended (no history rewrite).

No writer added, no refusal that guards a save lifted, no pair measured, no COM, no VM or trace
touched, nothing pushed, no workflow dispatched. `app/main.mm` unchanged, so no id here is gated
on CI. No id is called closed here; the coordinator's review closes them.

WAVE-35g-G1-COMPLETE
