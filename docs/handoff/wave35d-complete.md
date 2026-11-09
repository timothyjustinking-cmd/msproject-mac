# Wave 35d: completion record

## G0: the two CI failures and the three wave 35c Highs

Builder record: `wave35d-verification/gates-g0.md`. Commits: `23de6e2` (0.1), `171831b` (0.2),
`abc8a60` (0.3), `61a451b` (0.4), `6458203` (0.5). The trailer is on each, checked by script.

| Item | Finding | Status |
|---|---|---|
| 0.1 | R35cW-CI1 | fixed `23de6e2`; the GCC compile is CI's (MSVC cannot report `-Wmissing-field-initializers`) |
| 0.2 | R35cW-CI2 | fixed `171831b`; macos-qt is CI's (macOS cannot be run here) |
| 0.3 | R35cG0-1 (High) | fixed `abc8a60`; each of the three `check_props_pin` branches has a test with its own needle |
| 0.4 | R35cG1-1 (High) | fixed `61a451b`, both halves; the Show Level route was run and does not diverge |
| 0.5 | R35cG1-2 (High) | fixed `6458203`; Cut and Paste end the structural redo list |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; last row
`61a451b-W35d-0.5-*`): core build 0 with 0 warnings; `ctest -E plan_recalc` 65/65; app 34/34;
app_qt 52/52; `check_fixtures` 0; `check_markers --range main..HEAD` 0; `guard_names` 0;
`scan_client_names` 0; client-string grep none. The measured pairs still write and match with
effects on.

**Guards seen red:**
- 0.3: each `check_props_pin` branch switched off in turn; the new case fails on its needle
  (`build/w35d/0.3-red-*-run.log`). A later guard refuses each shape with other text, so no file
  is written.
- 0.4: the redo clear off, 5 FAILs in `qt_sort_persist` (`0.4-red-run.log`); the clears off with
  the re-apply on, 7 (`0.4-red-noclear-run.log`).
- 0.5: the two `clear_redo()` calls off, 11 FAILs (`0.5-red-run.log`).
- 0.1 and 0.2: none; CI is their gate.

**Deferred:** nothing.

**Found, not fixed:**
- Show Level does not diverge on the 0.4 route: Redo of a withdrawn level after an intervening
  All Subtasks or session level re-queues the level and the rebuild shows it. Whether Redo should
  end the list for a level, as it now does for a Sort, is the coordinator's decision.
- The queued-Sort re-apply in `after_structural_change` is unreachable from the public routes
  while the three clears stand; only the mutation run exercises it.
- A Paste that changes no cell leaves the redo list alone by design; a Cut of already-empty cells
  clears it, since its transaction is recorded.
- 0.1 and 0.2 are unconfirmed until CI compiles with GCC and runs macOS.

No writer added, no refusal lifted, no pair measured, no COM, nothing pushed, no workflow
dispatched.

WAVE-35d-G0-COMPLETE
