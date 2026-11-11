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
