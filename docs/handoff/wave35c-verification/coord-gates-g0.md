# Wave 35c G0 (the Linux build break and the Calculate Project Highs): coordinator verification (Mon 9 Nov 2026)

The gated commit is `b538646` (`WAVE-35c-G0-COMPLETE`).  It covers items 0.1 to 0.3: 4 builder
commits, `9a2c079` to `b538646`, on `wave35c` from `2ceb810`.  The builder's own record is
`gates-g0.md`.

Gates ran in the verify worktree, detached at `b538646`, from 22:17 to 22:39, with:
- fresh build trees;
- Release, with `CORE_WARNINGS_AS_ERRORS=ON`;
- the corpus junctioned in read-only.

Logs are in `build/verify-w35c/g0/` (git-ignored).

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 65/65 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; expected failing set reached |
| M2 `recalc --compare` | exit 0 |
| app_qt | 52/52 on the first run, 0 warnings |
| app | 34/34, 0 warnings |
| fixtures; markers | OK; OK |

- **Scans** (`2ceb810..b538646`): 0 `.mpp` blobs, `guard_names` 0, 0 name hits, 0 marker hits.
  All 4 commits carry `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35c)`.
- **0.1 (GCC 13) is unproven here.**  MSVC cannot report `-Wdangling-reference`; CI after CLOSE
  is its gate.

## Mutations (`build/verify-w35c/mutate-g0.py`, `g0/mutations.log`)

8 mutations, one at a time.  **7 of the 8 are red in their own lane.**  The worktree was clean
afterwards, and every lane was green again.

| id | guard switched off | own lane | other lanes |
|---|---|---|---|
| A1 | 0.2 plan: leaf Start against the Props project start | app red | Qt green (the Qt gate runs only the measured file, which passes 0.2) |
| W1 | 0.2 writer: the same | core red | |
| H1 | 0.2 helper: the first working instant set to 09:00 | core red, app red | |
| R1 | 0.2 reader: `props_start_stored` never set | app red | |
| P1 | 0.3 writer: the `check_props_pin` call removed | core red | Qt green (as A1) |
| P2 | 0.3 writer: every Props payload treated as free | core red | |
| **P3** | **0.3 writer: the record-count check (`index != kCount`)** | **core green** | |
| P4 | 0.3 writer: the tail hash check | core red | |

**P3 is an untested guard.**  It independently confirms the reviewer's R35cG0-1 (below).

## Independent review (`review-g0.md`)

A read-only agent reviewed `2ceb810..b538646`: 0 Critical, **1 High**, 2 Medium, 3 Low.

**R35cG0-1 (High):** three branches of `check_props_pin` are the only check for their shape,
and no test turns them red.  Mutation P3 confirms it.
- The branches are at `calculate.cpp:281, 296, 318`:
  - the header words;
  - a file holding more than the 161 measured records;
  - a file holding fewer.
- An option stored only when it is not the default would produce exactly these shapes.
- **Fix:** add three tests, each with its own needle.

**Mediums:**
- **R35cG0-2:** the two layers check different Start words.  The writer checks FixedData +96;
  the plan checks the reader's +104.  Both run through the app, so no bad file is written today.
  Fix with R35bG0-7: the writer also refuses +104 ≠ +96.
- **R35cG0-3:** the pin also fixes per-file identity records: the title `0x02400008`, dates,
  and a timestamp the null save leaves alone.
  - In practice this enables Calculate Project for the measured control file only, which the
    record does not say.
  - A second null-save pair cannot lift it.
  - The refusal text blames "calculation options".
  - Files the port itself saved after a WBS or growth edit are now refused.

**Lows:**
- R35cG0-4: the writer-only reasoning in `calculate.md` section 8 has two errors.
- R35cG0-5: a time word of 24:00 or later passes 0.2; this is no regression.
- R35cG0-6: test gaps, including the Props start kind-4 check and an accepted moved start such as
  Saturday 00:00.

## Merge rule

G0 closes R35bG0-1 and R35bG0-2 in source, but it adds **R35cG0-1 (High)**.  It joins the next
fix wave's front, with R35cG0-2 and R35cG0-3 among its Mediums.  The running groups G1 and G2
are not interrupted.
