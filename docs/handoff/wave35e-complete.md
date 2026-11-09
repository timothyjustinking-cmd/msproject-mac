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
