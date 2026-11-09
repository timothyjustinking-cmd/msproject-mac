WAVE-35b-G0-COMPLETE
WAVE-35b-G1-COMPLETE
WAVE-35b-G2-COMPLETE
WAVE-35b-G3-COMPLETE
WAVE-35b-G4-COMPLETE

## CLOSE

### CLOSE gates

Fresh trees `build/w35b-close-fresh`, `-app` and `-qt` at `90d8b4e` (the last code change is
`425019a`; `90d8b4e` is a record), configured from scratch with `CORE_WARNINGS_AS_ERRORS=ON`;
script `build/w35b/W35b-CLOSE-fresh-gates.ps1`, log `build/w35b/W35b-CLOSE-fresh-gates.log`.

- core build exit 0 with warnings as errors; `ctest -E plan_recalc` 65/65;
- `plan_recalc_tests` 512 checks with the corpus, 266 with `MPP_NO_CORPUS=1`;
- engine self-test 265 checks, 0 failures; `engine_diff.py --selftest` 53 checks; engine suite
  8 scenarios, 7 pass and the expected `constraint-mso` failure (1 of 247 fields);
- reader tests 1469 checks with the corpus, 789 without; M2 `recalc --compare` 332 of 332
  fields;
- app build exit 0 and ctest 34/34, with and without `MPP_NO_CORPUS=1`;
- app_qt build exit 0 and ctest 52/52;
- `check_fixtures.py`, `check_markers.py` (self-test 19 checks, worktree, `main..HEAD`),
  `check_command_backlog.py` all exit 0; `scan_client_names.py` on the head and on the
  `main..HEAD` messages 0 hits; 0 CR in the LF-pinned types; `git diff --check main..HEAD` exit 0.
- **`guard_names.py main..HEAD` exits 1** and names exactly two blobs, both added by `9fa599b`
  (`tests/data/wave35-g3-notes-generator-control.mpp` and `wave35-g3-notes-generator.mpp`).
  This is the wave 35 history defect the brief (section 0) leaves to the operator and the
  coordinator; `wave35` itself was rebuilt without it, `wave35b` still carries the original
  commit.  No wave 35b commit adds a fixture.  The branch cannot pass the pre-push guard until
  that is settled; nothing was worked round.
- The gcc, clang and AppleClang jobs cannot run here.  `app/main.mm` (W35b-1.7) cannot be
  compiled here: **CI is its only compile gate**, and CI is also the only proof of W35b-0.1
  (macos-qt) and W35b-1.8 (linux-qt and macos-qt builds).

### Ids, with the commit that closes each

| Item | Ids | Result |
|---|---|---|
| 0.1 | R35G0-1, CI-4 | fixed `6472da3` (proof is the coordinator's CI run) |
| 0.2 | R35G1-1 | fixed `efd30c6` |
| 0.3 | R35G1-2 | fixed `8695ea8` (but see R35bG0-1, open) |
| 0.4 | RW35-W2 | fixed `cc9b581` |
| 0.5 | R35G1-3 | fixed `15b9efd` (but see R35bG0-2, open) |
| 1.1 | R35G2-1 | fixed `114861b` (but see R35bG1-3: one shared guard serves both layers) |
| 1.2 | R35G2-2 | fixed `84a24d1` |
| 1.3 | R35G2-3 | fixed `b47f343` (but see R35bG1-3: one shared guard serves both layers) |
| 1.4 | R35G2-4 | fixed `7bd9c5c` |
| 1.5 | RW35-R2 | fixed `e4a380d` |
| 1.6 | R35G3-1, RW35-W1 | fixed `e813c11` (but see R35bG1-2: the guard reads FixedMeta byte 12 only; and R35bG1-3) |
| 1.7 | R35G4-1 (and R35G4-4) | fixed `48630a4`; macOS part compiled by CI only |
| 1.8 | R35G4-2, RW35-R1 | fixed `97afeee` |
| 2.1 to 2.12 | R35G1-4 to -9, RW35-W3, R35G0-2 to -4, R35G3-6, coordinator M5 | fixed (RW35-W3: but see R35bG2-1, the tie and swapped-order refusals are writer-only; RW35-R5 is closed in 2.5, now `f1a3f5b`, and 3.3, and was in no row) `a69943c`, `4a491df`, `38a6c92`, `eb5aa96`, `5284d58`, `e860427`, `93b9524`, `4315ef7`, `b304545`, `06eff3f`, `96d1cd4`, `b179918` |
| 3.1 to 3.8 | R35G2-5 to -8, R35G3-2 to -5, -7, RW35-W4 to -W7, RW35-R3, -R4, coordinator M13 | fixed or recorded (R35G2-6: but see R35bG3-1 and R35bG4-1, other paths still change the outline; R35G3-10 was recorded in 3.5, now `9cd003e`, and was in no row): `961b311`, `8d55b5c`, `980c6fb`, `d3a9fad`, `0476516`, `d3a0a72`, `7b2ad9a`, `6d3a1bd`, `90a3ca9`, `5dd4c10`, `40ab7dd`, `92d2555`, `ea2eb34`, `7519e01`, `2a11fcf`, `64e8572` |
| G4 | the Lows | table by id in `wave35b-verification/gates-g4.md` (R35G1-10 and R35G1-12: but see R35bG4-2 and R35bG4-3, partly closed); every id fixed with a commit except R35G3-8 (recorded: no history rewrite), RW35-W13 (left, as the row allows), RW35-R6 and RW35-R9 (confirmed as intended) |

Nothing in this wave is deferred.  No writer was added, no refusal lifted, no Project pair
measured, no COM or `oracle_*` script run.  The wave 35 Mediums and Lows are closed by the
rows above; `gates-g0.md` to `gates-g4.md` are the builder's per-group records (for G3 and G4 they are the builder's, not the coordinator's: the coordinator's are `coord-gates-g3.md`, `coord-gates-g4.md` and `coord-gates-close.md`).  The commit ids in the table are the pre-rebuild ones (R35bW-3, `history-rebuild-wave35b.map`).  This table was corrected by W35c-2.25 (R35bW-5): the "but see" notes, RW35-R5, R35G3-10 and the record names.

### Open at CLOSE (queued, not for this wave)

The per-group reviews (`wave35b-verification/review-g0.md` to `review-g4.md`; the CLOSE text first said
`review-g3.md`, corrected by W35c-2.24, R35bW-4) found, after the
fixes above, **two Highs, both in G0 and both on Calculate Project**:

- **R35bG0-1 (High)**: item 0.3's "leaf starts at the project start" compares against row 0's
  stale roll-up, not the `114/Props` project start (`0x02400002`) the writer stamps.
- **R35bG0-2 (High)**: the project's calculation options in `114/Props` (multiple critical
  paths, critical-slack limit, minutes per day) are not pinned; both layers accept them.

Calculate Project is therefore **not mergeable** until both are closed.  The Mediums R35bG0-3
to -5, R35bG1-1 to -3, R35bG2-1 and R35bG3-1, and the Lows R35bG0-6 to -8, R35bG1-4 to -9,
R35bG2-2 to -8, R35bG3-2 to -5 are queued in `plan-waves27-30.md` ("Queued from wave 35b"),
with the coordinator mutations that stayed green (G0 W11; G1 S1, S4, A6, Q1, Q2).  The wave 35b
reviews were of each group's diff; no separate CLOSE review was run in this session, so the
coordinator's independent review of `wave35..wave35b` remains to do.

Also open, found by the G4 review and the CLOSE review after this section was written (added by W35c-2.24, R35bW-4): R35bW-CI1 (the GCC 13 `dangling-reference` build break); the Mediums R35bG4-1 (Redo and the project-summary toggle still show every row while Save writes the queued Show Level), R35bW-1 (a grid edit made after a Sort or Show Level is queued is accepted) and R35bW-2 (replacing or clearing a queued Sort leaves it on the redo list); the Lows R35bG4-2 to -5, R35bG4-6 (the `TaskRow` `type_block_known` default is untested; mutation G7c stayed green), R35bG2-9 (watch: the intermittent `qt_assign_resources_gate`, green on macOS now), R35bG0-9 and R35bG1-10 (the untested guards recorded in `gates-g0.md` and `gates-g1.md`) and R35bW-3 to -6.

### Process defects, recorded and not fixed

- **Missing trailer on eleven G2 builder commits (R35bG2-4, Low; recorded by W35c-2.26).**
  Brief section 1 of wave 35b requires `Generated-By: Claude Sonnet 5.5 via Claude Code (wave
  35b)` on every commit.  The eleven G2 builder commits carry only `Co-Authored-By: Claude
  Sonnet 5.5`: the ten item commits 2.3 to 2.12 (`6035aff`, `98ef680`, `f1a3f5b`, `34b88bc`,
  `11ffe7b`, `29adb21`, `87b5ed2`, `1db1aa8`, `8696150`, `a5ac1c9`) and the group's completion
  commit `1505896`.  The model is named correctly; the standing form is missing.  G1's nine
  commits and every G3 and G4 builder commit carry it, so it is restored from G3 on (checked
  by script over `main..HEAD` on 10 November 2026).  The ids are the current ones; the
  pre-rebuild ones are in `wave35b-verification/history-rebuild-wave35b.map`.  **No history
  rewrite**: wave 35b brief section 0 and the wave 35c brief (G2, Records) forbid it, and a rewrite
  would change every later id.  Wave 35c's own commits carry the trailer.

### Push and CI

Push and CI: by the coordinator, once the operator settles `9fa599b`.  Nothing is pushed and no
workflow was dispatched.  The brief section 4(e) Project reopen check (Calculate Project, Sort,
Show Level, the wave 35 G0 assignment changes) is the coordinator's, by COM.

WAVE-35b-COMPLETE
