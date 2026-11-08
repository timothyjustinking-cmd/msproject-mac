# Wave 35 G3 (the wave-34 deferrals): coordinator verification (Mon 9 Nov 2026)

The gated commit is `2468736` (`WAVE-35-G3-COMPLETE`).  Gates ran in the verify worktree,
detached at `2468736`, with fresh build trees, Release, `CORE_WARNINGS_AS_ERRORS=ON` and the
main corpus junctioned in read-only, from 06:26 to about 06:57.  Logs are in
`build/verify-w35/g3/` (git-ignored).

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 65/65 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; expected failing set reached (`constraint-mso`) |
| M2 `recalc --compare` | exit 0 |
| app_qt / app | 52/52 / 34/34, 0 warnings |
| fixtures; markers | OK; OK |

## Fixtures, and a name in the history

At the tip, the 51 new or regenerated `tests/data/*.mpp` files scan clean in ASCII and
UTF-16LE: 0 marker hits.  The only embedded paths are under the neutral `C:\w35\` and `C:\w30\`
roots, plus Project's internal `gbui://mainpage.htm`.

Every `.mpp` blob added or changed in `daf46f8..2468736` was also scanned, version by version:
56 blobs.  Two carry the operator's name in the document's last-author field.  Both are in
commit `9fa599b` (W35-3.3): `wave35-g3-notes-generator.mpp` and its `-control.mpp`.  The
builder found it, regenerated both under the neutral identity in `9564ddd`, and made the
generator neutralise before every save.  The old blobs remain in the unpushed history, so the
pre-push guard refuses `wave35` past `daf46f8`.  Nothing reached GitHub: the 108 blobs in the
pushed range scan clean.  Clearing the history is the operator's decision.

## CI

Not run.  The branch cannot be pushed until the history decision above is made.

## Mutations (coordinator, `build/verify-w35/mutate-g3.py`)

| mutation | result |
|---|---|
| M1: Bold off, one-cell-item check off | red (the next guard catches it, with that guard's needle) |
| M2: Bold off, item-is-last check off | **green**: the guard is untested (R35G3-4) |
| M3: Bold off, measured-Bold-item check off | red (Italic item; another row's item) |
| M4: positive control, Bold-off flags 3 to 1 | red, including the pair compare at byte 85,292 |
| M5: the byte-42678 noise mask removed | red in core, app and Qt, but **not evidence**: the pinned digest is computed with that byte zeroed, so removing the mask breaks every Bold test whatever the noise claim |

## Independent review (`review-g3.md`)

A read-only agent reviewed `daf46f8..2468736`, excluding the coordinator's `2427613`: 0
Critical, **1 High**, 6 Medium, 5 Low.  The coordinator confirmed the High at source.

- **R35G3-1 (High), an already-enabled save:** the assignment-removal writer checks the flag
  byte of resource row 0 only (`core/mppwrite/assignment_growth.cpp:2395`).  On a port-saved
  file that Project has resaved, the target resource's FixedMeta flag is 0x5c, not 0x1c, and
  Project's removal then clears `Rsc Fixed2Meta` byte 279, which the port never does.  The 3.5
  test's hybrid file proves the writer accepts that file.  The exposure dates from W34-4.3.
- **Mediums R35G3-2 to -7:**
  - the 3.5 "four named gaps" is a lower bound;
  - the byte-42678 mask widens the enabled Bold-on writer to any session's view block;
  - four `unbold_cell` guards are untested;
  - the record treats the font word's top byte two ways;
  - the edit-model pin for the 3.5 refusal uses a generic needle;
  - the Underline and range deferrals rest on harness errors.
- **Lows R35G3-8 to -12:**
  - six commits lack the `Generated-By` trailer;
  - the fixture README has no hashes;
  - byte 42678 is a date-dependent float32, not noise;
  - the oracle's working copies live in `build/`;
  - one KnownGap signature names no differing byte.
- **Clean:**
  - `unbold_cell` is unreachable from both shells, and its proofs are real;
  - the new digest reproduces;
  - the deferral evidence matches the fixtures;
  - `neutralise()` now runs before every save;
  - `check_fixtures.py` covers all 51 fixtures;
  - the harness headers are unchanged.

## Merge rule

G3 does not meet the rule: one High is open (R35G3-1, against a save already enabled in wave
34), CI has not run, and the history holds the two blobs above.  The High leads wave 35b as
W35b-0.9.
