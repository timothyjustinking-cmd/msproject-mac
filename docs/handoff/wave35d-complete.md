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

## G2: the Lows and the Calculate Medium

Builder record: `wave35d-verification/gates-g2.md`. Commits: `6f5cc18` (2.1), `caa6cb1` (2.2),
`ec2429c` (2.3), `7d908c6` (2.4), `fc12254` (2.5), `f3c38d9` (2.6), `51c474b` (2.7), `492f10c`
(2.8), `bb4a398` (2.9), `e415fe2` (2.10), `4dcfcd7` (2.11), `f9771b6` (2.12), `c250aef` (2.13),
`a85d8b3` (2.14), `6652ded` (2.15). The trailer is on each, checked by script.

| Item | Finding | Status |
|---|---|---|
| 2.1 | R35cG0-2 (Medium), R35cW-3 | **partly fixed** `6f5cc18`: the writer half is closed (W35c-2.2's +104 check runs before 0.2) and said so; the plan half is open (the plan reads Start word +104 only); the stale test comment is corrected |
| 2.2 | R35cG0-4 | **partly fixed** `caa6cb1`: the plan has no Props digest, now a recorded gap; the two wrong sentences that explained it are corrected |
| 2.3 | R35cG0-5 | fixed `ec2429c` |
| 2.4 | R35cG0-6 | fixed `7d908c6` |
| 2.5 | R35cG2-2 | fixed `fc12254`; the refusal is writer-only (the plan holds no project row), recorded |
| 2.6 | R35cG2-3 | fixed `f3c38d9` |
| 2.7 | R35cG2-6 | fixed `51c474b`; a test-only seam reaches the unexaminable-path branch |
| 2.8 | R35cW-5 | fixed `492f10c` |
| 2.9 | R35cG2-4 | fixed `bb4a398` by the review's own option: the six-task acceptance's inference is stated and the five-task outline is refused by the builder under test |
| 2.10 | R35cG2-5 | fixed `e415fe2`; a signature guard, red as a compile error |
| 2.11 | R35cW-4 | fixed `4dcfcd7`; a queued Calculate refuses later cell edits, Cut, Paste, Fill and task-form edits; the Redo route was closed by 0.5 |
| 2.12 | R35cG1-6 | fixed `f9771b6`; one gap recorded: a row with no FixedMeta entry |
| 2.13 | R35cG1-5 | fixed `c250aef` (record) |
| 2.14 | R35cG1-7 | fixed `a85d8b3`; behaviour-preserving, guarded by a mutant, no timing taken |
| 2.15 | R35cG2-7 | fixed `6652ded` (record); 12 pairs at 13:00 and 5 at 21:00, from a decode |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; last source row
`c250aef-W35d-2.14-*`): core build 0 with 0 warnings; `ctest -E plan_recalc` 65/65; app 34/34;
app_qt 52/52; `check_fixtures` 0; `check_markers --range main..HEAD` 0; `guard_names` 0;
`scan_client_names` 0; client-string grep none. Rows 2.2, 2.13 and 2.15 are records only and ran
the scans without a build; 2.4 and 2.9 did not build app_qt (the change is in core and app). The
measured pairs still write and match with effects on.

**Guards seen red:** 2.3 to 2.9, 2.11, 2.12 and 2.14 against the code without the fix, and 2.10 as a
compile error (`build/w35d/2.*-red-*.log`). 2.12's red is the plan layer's own text and `push()`:
the builder still refuses through the writer's dry run. No red run, stated in each commit: 2.1,
2.2, 2.13 and 2.15 (records; 2.1 also a comment).

**Deferred:** nothing.

**Open after G2:** the plan half of R35cG0-2 (the plan reads Start word +104, never +96) and the
plan-side Props digest of R35cG0-4; both are refused at the writer or the save, so no wrong file
is reachable, and both are recorded in `spec/writers/calculate.md`.

**Found, not fixed:**
- 2.12: a resource row with no FixedMeta entry has no session byte; the plan refuses it and the
  writer reads that row by entry index.
- 2.11: Cut, Paste, Fill and the task form share the Calculate guard but only the cell gate has a
  Calculate-queued test.
- The G0 question stands: whether Redo should end the list for a Show Level, as it does for a Sort.

No writer added, no refusal lifted, no pair measured, no COM, nothing pushed, no workflow
dispatched. Every 35d fix still awaits an independent review.

WAVE-35d-G2-COMPLETE
