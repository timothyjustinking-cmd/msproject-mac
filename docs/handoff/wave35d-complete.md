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

## G1: the Mediums

Builder record: `wave35d-verification/gates-g1.md`. Commits: `e967858` (1.1), `e2eca38` (1.2),
`41f7a2c` (1.3), `6552f11` (1.4), `454521d` (1.5), `4410935` (1.6). The trailer is on each,
checked by script.

| Item | Finding | Status |
|---|---|---|
| 1.1 | R35cG1-3 | fixed `e967858`; Paste's guard has a test in both persist tests, and mutation Q10 turns it red |
| 1.2 | R35cG1-4 | fixed `e2eca38`; one rule under both kinds, in `spec/writers/view-state.md`, every kept refusal tested (Expand All under a queued Sort is disabled, and tested as such) |
| 1.3 | R35cG2-1 | fixed `41f7a2c`; a test-only fault seam reaches the `save_calculate` scratch chain after appends 1, 3 and 5 and the target is byte-identical; R35bG2-2 corrected to "partly fixed" |
| 1.4 | R35cG0-3 | fixed `6552f11` as a record and a refusal text; Calculate Project is enabled for the measured control's values only, and the port saves now refused are named; no refusal lifted |
| 1.5 | R35cW-2 | fixed `454521d`; R35bW-1, R35bW-2, R35bG2-2 and R35bG3-1 read "partly fixed", each with its remaining R35c id |
| 1.6 | R35cW-1, R35cG2-8 | fixed `4410935`; the open queue is in START-HERE, `memory/coordinator.md` and `plan-waves27-30.md`, Highs first, and the ids 35c closed are gone from their lists |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; last source row
`41f7a2c-W35d-1.4-*`): core build 0 with 0 warnings; `ctest -E plan_recalc` 65/65; app 34/34;
app_qt 52/52; `check_fixtures` 0; `check_markers --range main..HEAD` 0; `guard_names` 0;
`scan_client_names` 0; client-string grep none. The 1.5 and 1.6 rows are records only and ran the
scans without a build. The measured pairs still write and match with effects on.

**Guards seen red:**
- 1.1: Paste's guard removed (Q10), both new cases FAIL (`1.1-red-run.log`).
- 1.2: the two new call sites removed, five checks FAIL (`1.2-red-run.log`).
- 1.3: the append calls writing the target, 3 FAILs (`1.3-red-run.log`); the scratch cleanup
  removed, "no scratch file left" FAILs for all three (`1.3-red-scratch-run.log`).
- 1.4: the old refusal text, 2 FAILs (`1.4-red-run.log`).
- 1.5 and 1.6: none; records.

**Deferred:** nothing.

**Found, not fixed:**
- `qt_resource_add_gate` failed once in 0.65 s at `-j2` during 1.2, with no output; it passed alone
  and in every later full run. Not reproduced; watch.
- 1.4 names the port saves now refused from the Props records each writer touches; none was
  re-saved through Project (no COM this wave).
- The G0 question stands: whether Redo should end the list for a Show Level, as it does for a Sort.

No writer added, no refusal lifted, no pair measured, no COM, nothing pushed, no workflow
dispatched.

WAVE-35d-G1-COMPLETE
