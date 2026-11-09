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
