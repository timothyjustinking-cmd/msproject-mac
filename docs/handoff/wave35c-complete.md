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

## G2: the Lows

Builder record: `wave35c-verification/gates-g2.md`. Commits: `4e957ae` (2.1), `ecc51d1` (2.2),
`8e41da3` (2.3), `21b61d6` (2.4), `cb40334` (2.5), `fc9cae3` (2.6), `d8cfb6c` (2.7), `6f95c27`
(2.8), `36c1492` (2.9), `aab8a26` (2.10), `d776901` (2.11), `d829206` (2.12), `6f28860` (2.13),
`cea97e1` (2.14), `d0100dd` (2.15), `0222978` (2.16), `8aa61f5` (2.17), `c235973` (2.18),
`dbef0ef` (2.19), `e9e4657` (2.20), `f074f1c` (2.21), `b588f05` (2.22), `f8be7ba` (2.23),
`f4065f2` (2.24), `cd7dc10` (2.25), `4d3c943` (2.26), `9a38f2e` (2.27). The trailer is on each.

| Item | Finding | Status |
|---|---|---|
| 2.1 | R35bG0-6 | fixed `4e957ae` |
| 2.2 | R35bG0-7 | fixed `ecc51d1` |
| 2.3 | R35bG0-8 | fixed `8e41da3` in the writer; `push()` has no file, so the save refuses a hand-built edit |
| 2.4 | R35bG2-2 | fixed `21b61d6` (tests only; also closed incidentally earlier) |
| 2.5 | R35bG2-5 | fixed `cb40334` (statements and a comment; also closed incidentally earlier) |
| 2.6 | R35bG2-6 | partly fixed `fc9cae3`: the app test asserts the digest refusal; the save-time dry run's second suggestion is unreachable through the public API and recorded as defence in depth |
| 2.7 | R35bG2-7 | fixed `d8cfb6c` |
| 2.8 | R35bG4-2 | fixed `6f95c27` |
| 2.9 | R35bG4-3 | fixed `36c1492` |
| 2.10 | R35bG4-4 | fixed `aab8a26` (defence in depth: not reachable today) |
| 2.11 | R35bG4-6 | fixed `d776901`; mutation G7c turns it red |
| 2.12 | R35bG1-4 | fixed `d829206` as a refusal (the pin), not a record |
| 2.13 | R35bG1-5 | fixed `6f28860` |
| 2.14 | R35bG1-6 | fixed `cea97e1` |
| 2.15 | R35bG1-9 | fixed `d0100dd` |
| 2.16 | R35bG3-3 | fixed `0222978` |
| 2.17 | R35bG3-5 | recorded `8aa61f5`: the second-session Bold pair is a measuring-wave item; no pair taken |
| 2.18 | R35bG4-5 | fixed `c235973` |
| 2.19 | R35bG1-7 | fixed `dbef0ef` (one backlog row; no guard can go red) |
| 2.20 | R35bG1-8 | fixed `e9e4657` for the wording; which fields Save writes was not re-traced |
| 2.21 | R35bG2-3 | fixed `f074f1c` for the wording; the plan layer's name trim differs from the writer's, recorded and not changed |
| 2.22 | R35bG2-8 | fixed `b588f05` (README rows) |
| 2.23 | R35bG3-4 | fixed `f8be7ba` (record) |
| 2.24 | R35bW-4 | fixed `f4065f2` (record) |
| 2.25 | R35bW-5 | fixed `cd7dc10` (record) |
| 2.26 | R35bG2-4 | recorded `4d3c943`; no history rewrite |
| 2.27 | R35bW-6 | recorded `9a38f2e` as an accepted defect; no history rewrite |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; last row
`4d3c943-W35c-2.27-*`): core build 0 with 0 warnings; `ctest -E plan_recalc` 65/65; app 34/34;
app_qt 52/52 on the ten items that touch app, Qt or linked core source (2.1, 2.2, 2.5, 2.6, 2.10,
2.12, 2.14, 2.18, 2.20, 2.21; the last code change is 2.21); `check_fixtures` 0;
`check_markers --range main..HEAD` 0; `guard_names` 0; `scan_client_names` 0; client-string grep
none. The other rows did not build app_qt.

**Guards seen red:** 2.1, 2.2, 2.3, 2.4 (two mutations), 2.6 to 2.16, 2.18, 2.20 (two runs) and
2.21, each against the code or guard without the fix, restored after (`build/w35c/2.*-run.log`).
No red run, stated in each commit: 2.5 (statements and a comment), 2.17, 2.19 (no check pins a
menu row's gap text), 2.22 (the evidence is the fixture read) and 2.23 to 2.27 (records).

**Deferred:** nothing.

**Found, not fixed:**
- The 24 `wave35-g3-*` fixtures store project start 2026-01-04 13:00 (Props `781ef13b`), and their
  README rows (602 to 605 among them) still say 2026-01-05 08:00; only the viewstate rows were in
  R35bG2-8.
- The plan layer trims the Props calendar name and the writer does not (2.21); the writer is the
  stricter layer and the builder's dry run reaches it.
- The "asks to save or undo" labels in the sort and show_level test names are stale.
- No guard pins a menu row's gap text (2.19).
- 2.20 did not re-trace which fields Save writes.
- R35bG2-6's second suggestion is unreachable (defence in depth).
- Not this wave's: R35bW-3 (answered by the coordinator's map); R35bG2-9 is watch-only and green
  on macOS at `e95c6ca`.

No writer added, no refusal lifted, no pair measured, no COM, nothing pushed, no workflow
dispatched, no history rewrite.

WAVE-35c-G2-COMPLETE
