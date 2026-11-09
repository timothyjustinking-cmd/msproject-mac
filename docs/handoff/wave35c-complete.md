# Wave 35c: completion record

## G0: the Calculate Project Highs and the Linux build break

Builder record: `wave35c-verification/gates-g0.md`. Commits: `9a2c079` (0.1), `7d923dc` (0.2),
`743ad8b` (0.3). The trailer is on each, checked by script.

| Item | Finding | Status |
|---|---|---|
| 0.1 | R35bW-CI1 | fixed `9a2c079`; the GCC 13 compile is CI's (MSVC cannot report `-Wdangling-reference`) |
| 0.2 | R35bG0-1 (High) | fixed `7d923dc`, both layers |
| 0.3 | R35bG0-2 (High) | fixed `743ad8b` in the writer; the plan layer is writer-only by a stated design (`spec/writers/calculate.md` section 8) |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; last row
`7d923dc-W35c-0.3-*`): core build 0 with 0 warnings; `ctest -E plan_recalc` 65/65; app 34/34;
app_qt 52/52; `check_fixtures` 0; `check_markers --range main..HEAD` 0; `guard_names` 0;
`scan_client_names` 0; client-string grep none; `gen_calculate_props_pin.py --check` up to
date. The measured unlinked pair still writes and matches with effects on.

**Guards seen red:**
- 0.2, the leaf Start against Props `0x02400002`: with the guards off the writer saved the
  moved-start file and `app_edits_calculate` failed 4 checks (`build/w35c/0.2-red-*.log`).
- 0.3, the Props pin: with the call off, 159 failures in `mppwrite_roundtrip_calculate`
  (156 flipped payload bytes, a changed kind, a changed tail byte, and the no-file check), all
  "expected a refusal, none thrown" (`build/w35c/0.3-red-core-*.log`).
- 0.1: none; CI is its gate.

**Deferred:** nothing.

**Found, not fixed:**
- The Props pin rests on one control and one null save (two GUID records are the only noise
  they show). A file saved elsewhere with other per-file values in `114/Props` is refused with
  "differs from the measured control" until a second null-save pair shows them. A refusal, not
  a wrong write; not measured (no COM).
- `push()` does not see the Props pin for a hand-built calculate edit; the refusal comes at the
  save (`source_digest` ties the edit to the file). Bears on R35bG0-5 (item 1.7).
- A project start inside the working day (for example Monday 10:30) is refused by 0.2; Project's
  bytes for any moved start are unmeasured.
- 0.1 is unconfirmed until CI compiles it with GCC 13.

No writer added, no refusal lifted, no pair measured, no COM, nothing pushed, no workflow
dispatched.

WAVE-35c-G0-COMPLETE

## G1: the Mediums and Lows

Builder record: `wave35c-verification/gates-g1.md`. Commits: `c408a07` (1.1), `aa1cef8` (1.2),
`e439dd2` (1.3), `37e0e1f` (1.4), `cfe2e8a` (1.5), `7853816` (1.6), `b9588b7` (1.7), `eee629d`
(1.8), `7666219` (1.9), `4a2133e` (1.10), `4144385` (1.11). The trailer is on each, checked by
script.

| Item | Finding | Status |
|---|---|---|
| 1.1 | R35bG3-1 | fixed `c408a07` |
| 1.2 | R35bG4-1 | fixed `aa1cef8` |
| 1.3 | R35bW-1 | fixed `e439dd2`; Paste and Fill Down carry the guard, no test of their own |
| 1.4 | R35bW-2 | fixed `37e0e1f` |
| 1.5 | R35bG0-3 | fixed `cfe2e8a`, with residual limits (two cancelling edits pass; a never-calculated file is refused; a critical bit outside the slack words is unmeasured) |
| 1.6 | R35bG0-4 | fixed `7853816` (tests only) |
| 1.7 | R35bG0-5 | fixed `b9588b7` for the FixedMeta entry and VarMeta keys; the tie closed by 1.11 |
| 1.8 | R35bG1-1 | fixed `eee629d`; the Release needle tests are a regression pin, the detector is a Debug build |
| 1.9 | R35bG1-2 | fixed `7666219` |
| 1.10 | R35bG1-3 | fixed `4a2133e` for Sort and the removal; partly fixed for Show Level's view state: both layers' guards share one predicate, and `push()` has no file |
| 1.11 | R35bG2-1 (also R35bG3-2, the tie of R35bG0-5) | fixed `4144385`, both layers |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; last row
`4a2133e-W35c-1.11-*`): core build 0 with 0 warnings; `ctest -E plan_recalc` 65/65; app 34/34;
app_qt 52/52; `check_fixtures` 0; `check_markers --range main..HEAD` 0; `guard_names` 0;
`scan_client_names` 0; client-string grep none. The 1.5, 1.6, 1.8 and 1.9 rows did not build
app_qt; the 1.7, 1.10 and 1.11 rows cover it.

**Guards seen red:**
- 1.1 to 1.4: the qt persist tests against the code without each fix (`build/w35c/1.[1-4]-red-run.log`).
- 1.5: 3 writer cases accepted and 8 `app_edits_calculate` failures without the free-slack checks.
- 1.6: each of the four `reader_adapter` lines deleted in turn, 2 failures each.
- 1.7: 8 failures without the plan check.
- 1.8: only the checked Debug build (the old code does not finish in 60 s); Release stays green.
- 1.9: the writer wrote the file and the app built the edit (4 failures).
- 1.10: each layer's guards off one at a time; the other layer's tests stay green except where a
  save-cascade test pins the writer (`1.10-red-run.log`).
- 1.11: both plan guards off, 6 `app_edits_calculate` failures (builder, reason and `push()` for the
  tie and for the swapped order).

**Deferred:** nothing.

**Found, not fixed:**
- Show Level's view state is guarded in both layers by one shared predicate (1.10); a change in
  it removes both, and `push()` cannot check a file.
- 1.5's residual limits (above).
- 1.8: only a Debug or sanitizer build can see the undefined behaviour; the Release gate cannot.
- 1.9: whether a real lineage reaches a file with a 0x1C target row and another 0x5C row is not
  traced; the guard refuses it either way.
- The wave 35b line for `114861b` ("push() and the save cascade call the writer again") is wrong:
  `push()`'s `show_level` case does not call the writer. Corrected in `gates-g1.md`; history is
  not rewritten.

No writer added, no refusal lifted, no pair measured, no COM, nothing pushed, no workflow
dispatched.

WAVE-35c-G1-COMPLETE
