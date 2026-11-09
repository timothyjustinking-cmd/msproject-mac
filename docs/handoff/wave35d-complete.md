# Wave 35d: completion record

## G0: the two CI failures and the three wave 35c Highs

Builder record: `wave35d-verification/gates-g0.md`. Commits: `23de6e2` (0.1), `171831b` (0.2),
`abc8a60` (0.3), `61a451b` (0.4), `6458203` (0.5). The trailer is on each, checked by script.

| Item | Finding | Status |
|---|---|---|
| 0.1 | R35cW-CI1 | fixed in source `23de6e2`; CI is its gate (the GCC compile is CI's: MSVC cannot report `-Wmissing-field-initializers`) |
| 0.2 | R35cW-CI2 | fixed in source `171831b`; CI is its gate (macos-qt is CI's: macOS cannot be run here) |
| 0.3 | R35cG0-1 (High) | fixed `abc8a60`; each of the three `check_props_pin` branches has a test with its own needle |
| 0.4 | R35cG1-1 (High) | fixed `61a451b`, both halves; the Sort route is closed.  **Correction (W35e-1.3, R35dG1-1):** this row said the Show Level route was run and does not diverge, which is wrong: a queued Level, Ctrl+Z, a session-only Sort and Ctrl+Y re-queued the level under the sorted display (R35dG1-1(b), closed by W35e-1.1 `39c8e37`) |
| 0.5 | R35cG1-2 (High) | **partly fixed** `6458203`: Cut and Paste end the structural redo list; remainder R35dG0-1 with R35dW-1, a partly applied Task Information or Task Form edit kept it (closed by W35e-0.1 `b8ee455`) |

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
- **Corrected (W35e-1.3, R35dG1-1(b)):** the claim that Show Level does not diverge on the 0.4
  route holds only for an intervening All Subtasks or session level, and is false for an
  intervening session-only Sort, which re-queued the level under the sorted display (closed by
  W35e-1.1 `39c8e37`: a sort choice now ends a Show Level on the redo list).  Redo of a withdrawn
  level after an intervening All Subtasks or session level re-queues the level and the rebuild
  shows it. Whether Redo should
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
| 1.2 | R35cG1-4 | **partly fixed** `e2eca38`: remainder R35dG1-1, a session Sort shown before a Level, and an Undo Level, Sort, Redo route bypassed the rule (closed by W35e-1.1 `39c8e37`); one rule under both kinds, in `spec/writers/view-state.md`, every kept refusal tested (Expand All under a queued Sort is disabled, and tested as such) |
| 1.3 | R35cG2-1 | fixed `41f7a2c`; a test-only fault seam reaches the `save_calculate` scratch chain after appends 1, 3 and 5 and the target is byte-identical; R35bG2-2 corrected to "partly fixed" |
| 1.4 | R35cG0-3 | **partly fixed** `6552f11` as a record and a refusal text; remainder R35dG1-2 and R35dW-4, the Scope paragraph's list of refused port saves and its "at the pin" claim were wrong (closed by W35e-1.2 `1da3997`); Calculate Project is enabled for the measured control's values only, and the port saves now refused are named; no refusal lifted |
| 1.5 | R35cW-2 | fixed `454521d`; R35bW-1, R35bW-2, R35bG2-2 and R35bG3-1 read "partly fixed", each with its remaining R35c id |
| 1.6 | R35cW-1, R35cG2-8 | R35cW-1 **partly fixed** `4410935`: remainder R35dG1-3 and R35dW-2, the lists still carried ids 35c closed, R35bG0-1 and R35bG0-2 among them, and the reviews' open ids were not in them (closed by W35e-1.3); R35cG2-8 fixed; the open queue is in START-HERE, `memory/coordinator.md` and `plan-waves27-30.md` |

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
| 2.11 | R35cW-4 | **partly fixed** `4dcfcd7`: a queued Calculate refuses later cell edits, Cut, Paste, Fill and task-form edits, and 0.5 closed the Cut and Paste Redo route; remainder R35dW-1, the Redo route through a partly applied Task Information edit (closed by W35e-0.1 `b8ee455`) |
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

## CLOSE

### CLOSE gates

Fresh trees `build/w35d-close-fresh`, `-app` and `-qt` at `d335791` (the last code change is
`a85d8b3`, 2.14; every later commit is a record), configured from scratch with
`CORE_WARNINGS_AS_ERRORS=ON`; script `build/w35d/W35d-CLOSE-fresh-gates.ps1`, log
`build/w35d/W35d-CLOSE-fresh-gates.log` (summary: `build/w35d/close_log_summary.py`). The
START-HERE, `memory/coordinator.md` and `plan-waves27-30.md` state lines changed after that run;
they are documents and no gate reads them.

- core build exit 0 with warnings as errors; `ctest -E plan_recalc` 65/65;
- `plan_recalc_tests` 512 checks with the corpus, 266 with `MPP_NO_CORPUS=1`;
- engine self-test 265 checks, 0 failures; `engine_diff.py --selftest` 53 checks; engine suite 8
  scenarios, 7 pass and the expected `constraint-mso` failure (1 of 247 fields);
- reader tests 1469 checks with the corpus, 789 without; M2 `recalc --compare` 332 of 332 fields;
- app build exit 0 and ctest 34/34, with and without `MPP_NO_CORPUS=1`;
- app_qt build exit 0 and ctest 52/52; no run reported "No tests were found";
- `check_fixtures.py`, `check_markers.py` (self-test 19 checks, worktree, `main..HEAD`) and
  `check_command_backlog.py` (114 pins, 114 rows) exit 0; `gen_calculate_props_pin.py --check` up
  to date; `scan_client_names.py` on the head and on the `main..HEAD` messages 0 hits;
  `guard_names.py main..HEAD` exit 0; the client-string grep finds none; 0 CR in the LF-pinned
  types; `git diff --check main..HEAD` exit 0.
- The gcc, clang and AppleClang jobs cannot run here. **CI is the only proof of W35d-0.1** (the GCC
  `-Wmissing-field-initializers`) **and of W35d-0.2** (macos-qt's About box). `app/main.mm` is not
  changed in `wave35c..HEAD`.
- Trailer `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35d)`: present on all 29 builder
  commits in `wave35c..HEAD` (checked by script at CLOSE). The one commit without it, `e17ddb7`, is
  the coordinator's wave 35d brief.

### Ids, with the commit that closes each

| Group | Ids | Result |
|---|---|---|
| G0 | R35cW-CI1, R35cW-CI2 | fixed in source `23de6e2`, `171831b`; CI is the gate for both, and each stays unproved until the coordinator's CI run is green |
| G0 | R35cG0-1 (High) | fixed `abc8a60` |
| G0 | R35cG1-1 (High) | fixed `61a451b`, both halves |
| G0 | R35cG1-2 (High) | **partly fixed** `6458203`: remainder R35dG0-1 with R35dW-1 (closed by W35e-0.1 `b8ee455`) |
| G1 | R35cG1-3 | fixed `e967858` |
| G1 | R35cG1-4 | **partly fixed** `e2eca38`: remainder R35dG1-1 (closed by W35e-1.1 `39c8e37`) |
| G1 | R35cG2-1 | fixed `41f7a2c` |
| G1 | R35cG0-3 | **partly fixed** `6552f11` (a record and a refusal text; no refusal lifted): remainder R35dG1-2 and R35dW-4 (closed by W35e-1.2 `1da3997`) |
| G1 | R35cW-2, R35cG2-8 | fixed `454521d`, `4410935` (records) |
| G1 | R35cW-1 | **partly fixed** `4410935` (records): remainder R35dG1-3 and R35dW-2 (closed by W35e-1.3) |
| G2 | R35cG0-2, R35cW-3 | **partly fixed** `6f5cc18`: the plan half remains (the plan reads Start word +104 only; the writer half is closed and refused) |
| G2 | R35cG0-4 | **partly fixed** `caa6cb1`: the plan has no Props digest, a recorded gap; the save is refused at the writer |
| G2 | R35cG0-5, R35cG0-6 | fixed `ec2429c`, `7d908c6` |
| G2 | R35cG2-2, -3, -6 | fixed `fc12254`, `f3c38d9`, `51c474b` |
| G2 | R35cW-5 | fixed `492f10c` |
| G2 | R35cG2-4, R35cG2-5 | fixed `bb4a398`, `e415fe2` |
| G2 | R35cW-4 | **partly fixed** `4dcfcd7`: remainder R35dW-1 (closed by W35e-0.1 `b8ee455`) |
| G2 | R35cG1-6 | fixed `f9771b6` (one gap recorded: a row with no FixedMeta entry) |
| G2 | R35cG1-5, R35cG1-7, R35cG2-7 | fixed `c250aef`, `a85d8b3`, `6652ded` |

Every id in `wave35c-verification/` appears in this file. Seven are only partly fixed: R35cG0-2
and R35cG0-4 (each keeps its remainder in its row and in `spec/writers/calculate.md`), and
R35cG1-2, R35cG1-4, R35cW-1, R35cG0-3 and R35cW-4 (corrected by W35e-1.3, R35dW-3: each row names
the R35d id that remained, and the wave 35e commit that closed it).  R35cW-CI1 and R35cW-CI2 are
fixed in source and CI is their gate.  Nothing in this wave is deferred. The wave 35c ids that 35c
recorded as partly fixed (R35bW-1, R35bW-2, R35bG2-2, R35bG3-1) had their 35c remainders worked by
35d, but R35bW-1 and R35bG3-1 are partly fixed again through R35cG1-2 and R35cG1-4 (see the 35c
CLOSE table).

**Guards seen red:** per group in the G0, G1 and G2 rows above; the logs are
`build/w35d/*-red-*.log`. CLOSE adds no guard.

**Found, not fixed** (carried from the group rows; nothing new at CLOSE):
- Whether Redo should end the list for a Show Level, as it does for a Sort (the 0.4 route does not
  diverge for a level), is the coordinator's decision.
- The queued-Sort re-apply in `after_structural_change` is reachable only by a mutation run.
- `qt_resource_add_gate` failed once in 0.65 s at `-j2` during 1.2 and passed alone and in every
  later full run, CLOSE included; watch.
- 1.4 names the port saves now refused from the Props records each writer touches; none was
  re-saved through Project (no COM).
- 2.12: a resource row with no FixedMeta entry has no session byte.
- 2.11: Cut, Paste, Fill and the task form share the Calculate guard but only the cell gate has a
  Calculate-queued test.
- The plan half of R35cG0-2 and the plan-side Props digest of R35cG0-4 (above).

**For the coordinator, after CLOSE:** push `wave35d` and dispatch `qt.yml` and `ci.yml` (0.1 and
0.2 are gated there); mutations at each new guard, including re-runs of P3 (0.3) and Q10 (1.1); an
independent review of `wave35c..wave35d`. The 4(e) reopen check is a rerun only if a writer's
output changed; this wave adds no writer. Merging waves 34 to 35d waits on CI green on all three
OSs and no open Critical or High.

No writer added, no refusal lifted, no pair measured, no COM, nothing pushed, no workflow
dispatched, no history rewrite.

WAVE-35d-COMPLETE
