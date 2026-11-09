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
| 1.1 | R35G2-1 | fixed `114861b` |
| 1.2 | R35G2-2 | fixed `84a24d1` |
| 1.3 | R35G2-3 | fixed `b47f343` |
| 1.4 | R35G2-4 | fixed `7bd9c5c` |
| 1.5 | RW35-R2 | fixed `e4a380d` |
| 1.6 | R35G3-1, RW35-W1 | fixed `e813c11` |
| 1.7 | R35G4-1 (and R35G4-4) | fixed `48630a4`; macOS part compiled by CI only |
| 1.8 | R35G4-2, RW35-R1 | fixed `97afeee` |
| 2.1 to 2.12 | R35G1-4 to -9, RW35-W3, R35G0-2 to -4, R35G3-6, coordinator M5 | fixed `a69943c`, `4a491df`, `38a6c92`, `eb5aa96`, `5284d58`, `e860427`, `93b9524`, `4315ef7`, `b304545`, `06eff3f`, `96d1cd4`, `b179918` |
| 3.1 to 3.8 | R35G2-5 to -8, R35G3-2 to -5, -7, RW35-W4 to -W7, RW35-R3, -R4, coordinator M13 | fixed or recorded: `961b311`, `8d55b5c`, `980c6fb`, `d3a9fad`, `0476516`, `d3a0a72`, `7b2ad9a`, `6d3a1bd`, `90a3ca9`, `5dd4c10`, `40ab7dd`, `92d2555`, `ea2eb34`, `7519e01`, `2a11fcf`, `64e8572` |
| G4 | the Lows | table by id in `wave35b-verification/gates-g4.md`; every id fixed with a commit except R35G3-8 (recorded: no history rewrite), RW35-W13 (left, as the row allows), RW35-R6 and RW35-R9 (confirmed as intended) |

Nothing in this wave is deferred.  No writer was added, no refusal lifted, no Project pair
measured, no COM or `oracle_*` script run.  The wave 35 Mediums and Lows are closed by the
rows above; the per-group coordinator records are `gates-g0.md` to `gates-g4.md`.

### Open at CLOSE (queued, not for this wave)

The per-group reviews (`wave35b-verification/review-g0.md` to `review-g3.md`) found, after the
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

### Push and CI

Push and CI: by the coordinator, once the operator settles `9fa599b`.  Nothing is pushed and no
workflow was dispatched.  The brief section 4(e) Project reopen check (Calculate Project, Sort,
Show Level, the wave 35 G0 assignment changes) is the coordinator's, by COM.

WAVE-35b-COMPLETE
