# Wave 35e: completion record

## G0: the High

Builder record: `wave35e-verification/gates-g0.md`. Commit: `b8ee455` (0.1). The trailer is on it,
checked by script.

| Item | Finding | Status |
|---|---|---|
| 0.1 | R35dG0-1 (High), with R35dW-1 | fixed `b8ee455`; a partly applied Task Information or Task Form edit ends the structural redo list; tested in `qt_sort_persist`, `qt_outline_persist` and `qt_calculate_gate`; the sweep of the other cell-commit paths found none (Cut, Paste and Fill Down are one transaction) |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`;
`43fee2a-W35e-0.1-*`): core build 0 with 0 warnings; `ctest -E plan_recalc` 65/65; app 34/34;
app_qt 52/52; `check_fixtures` 0; `check_markers --range main..HEAD` 0; `guard_names` 0;
`scan_client_names` 0; client-string grep none. The measured pairs still write and match with
effects on.

**Guards seen red:**
- 0.1: the `if (!ok && applied)` clear off, 9 FAILs, three in each of the three tests
  (`build/w35e/0.1-red-run.log`).

**Deferred:** nothing.

**Found, not fixed:**
- Nothing new. A partly applied edit still leaves its committed fields in place (no rollback), by
  the brief's fix; the status line names the failing field and Undo takes the committed cells back.

No writer added, no refusal lifted, no pair measured, no COM, nothing pushed, no workflow
dispatched.

WAVE-35e-G0-COMPLETE

## G1: the Mediums

Builder record: `wave35e-verification/gates-g1.md`. Commits: `39c8e37` (1.1), `1da3997` (1.2),
`02f1900` (1.3), `f244d02` (1.4). The trailer is on each, checked by script.

| Item | Finding | Status |
|---|---|---|
| 1.1 | R35dG1-1 | fixed `39c8e37`; (a) a Level 1 or deeper is refused while a session Sort is shown, (b) a sort choice ends a Show Level on the redo list; both tested in `qt_outline_persist` on the six-task outline |
| 1.2 | R35dG1-2, with R35dW-4 | fixed `1da3997`; `calculate.md`'s Scope paragraph corrected (the Notes and Text1 removals, the percent-complete and indent and outdent saves, and the pin as the last check) |
| 1.3 | R35dG1-3, R35dW-2, R35dW-3 | fixed `02f1900`; the open queue is in START-HERE, `memory/coordinator.md` and `plan-waves27-30.md` from `wave35d-verification/coord-gates-close.md`; R35cG1-2, R35cG1-4, R35cW-1, R35cG0-3 and R35cW-4 read "partly fixed"; R35cW-CI1 and R35cW-CI2 read "fixed in source; CI is its gate"; the Show Level sentence is corrected |
| 1.4 | R35dG0-2 | fixed `f244d02`; 0.5's record reads "partly fixed: Task Information partial apply", closed by W35e-0.1 `b8ee455` |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; 1.1
`5d733ae-W35e-1.1-*`, the group's final tree `f244d02-W35e-G1-record-*`): core build 0 with 0
warnings; `ctest -E plan_recalc` 65/65; app 34/34; app_qt 52/52; `check_fixtures` 0;
`check_markers --range main..HEAD` 0; `guard_names` 0; `scan_client_names` 0; client-string grep
none. 1.2 to 1.4 are records only (scans, fixtures and `git diff --check`, 0). The measured pairs
still write and match with effects on.

**Guards seen red:**
- 1.1: the shown-Sort Level refusal and the Show Level redo clear off together, 6 FAILs in
  `qt_outline_persist` (`build/w35e/1.1-red-run.log`); not run one at a time, each needle belongs
  to one guard.

**Deferred:** nothing.

**Found, not fixed:**
- A session-only Level chosen while a Sort sits on the redo list leaves that Sort there; Ctrl+Y
  would re-queue it over the session Level. Not reproduced and not in the brief (`gates-g1.md`).

No writer added, no refusal of a persisted shape lifted, no pair measured, no COM, nothing pushed,
no workflow dispatched.

WAVE-35e-G1-COMPLETE
