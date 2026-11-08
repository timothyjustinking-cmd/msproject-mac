# Wave 34 G3: coordinator verification (8 Nov 2026)

Gated commit `3a8a31c` (WAVE-34-G3-COMPLETE), a fresh detached worktree with fresh build
trees, Release, `CORE_WARNINGS_AS_ERRORS=ON`; logs in `build/verify-w34g3/` (git-ignored).
The run started 11:11 and finished 11:48 (core, Qt and app trees); the corpus lane and the
mutations ran 11:50-12:2x with the main tree's `corpus/` junctioned in read-only.

| gate | result |
|---|---|
| core configure / build | exit 0 / exit 0, 0 compiler warnings |
| core ctest `-E plan_recalc` | 61/61 passed |
| `plan_recalc_tests` with `MPP_NO_CORPUS=1` / with corpus | 266 checks / 512 checks, exit 0 |
| engine self-test; `--suite --expect-fail constraint-mso` | 265 checks, 0 failures; 8 scenarios, 7 pass, 1 expected fail, 0 unrecorded |
| `mpp_reader_tests` with corpus | 1469 checks, PASS; golden comparison 83 oracle rows vs 83 reader rows, no mismatches |
| M2 `recalc --compare` | 332/332, 268/268, 64/64 fields match, 0 unexplained |
| app_qt configure / build / ctest | exit 0 / exit 0 / 39/39 (the two corpus-only Qt tests, `qt_stored_table_corpus` and `qt_task_order_corpus`, are not built without the corpus; the builder's 41/41 includes them) |
| app configure / build / ctest | exit 0 / exit 0 / 25/25 |
| `check_fixtures.py` | 268 tracked test-data files, 219 tracked `.mpp/.mpt` property sets OK, 8 oracles' provenance OK |
| `check_markers.py --range 4fd9b90..HEAD` | OK, no operator marker |

All numbers agree with the builder's G3 record except the Qt count, explained above.

## Mutations planted by the coordinator (each restored byte for byte, hash checked)

| id | where | change | result |
|---|---|---|---|
| M1 | `resource_growth.cpp`, calendar FixedMeta entry size (helper) | `cal_fixed.size()` -> `+ 4` | RED: `Cal FixedMeta entry` and `114/TBkndCal/FixedMeta equals Project's edit save` (byte 90: port 0x68, Project 0x64), both pairs |
| M2 | `resource_growth.cpp`, the `114/Props` calendar-count scalar (call site: the entry removed from the edit list) | line deleted | RED: `Calendar count` and `114/Props equals Project's edit save` (byte 62434: port 0x07, Project 0x08), both pairs |
| M3 | `assignment_growth.cpp`, first-assignment project work roll-up | `work` -> `work * 2.0` | RED: `add: project work +24` and `114/TBkndTask/FixedData equals Project's edit save` (byte 78) |
| M4 | `reader.cpp`, PreleveledStart var id | `0x0B400171` -> `0x0B400173` | RED: `level-all uid 1 PreleveledStart (control COM Start)` |
| M5 | `tests/data/project_a_golden.tsv` (W34-B5's file), uid 10733 duration | 16800 -> 16860 | RED: `MISMATCH duration uid 10733` in the golden comparison (the reader still compares the remaining fields per uid after the name column was dropped) |

After the restore: rebuilt, the three affected tests 3/3 passed.

## Independent review (`review-g3.md`, read-only agent over `db305b6..3a8a31c`)

0 Critical, 2 High, 2 Medium, 5 Low.  Both Highs were re-read at source by the coordinator
and are CONFIRMED:

- **R34G3-1 (High).**  `assignment_growth.cpp` `check_assn_row_work` accepts any positive
  finite work, and `summary_info.cpp` writes `"<n> hrs"` for any whole hour count, but every
  committed assignment pair is a 40-hour step (0->40, 40->0, 40->80, 80->120).  Brief rule 2
  ("one factor per pair") is not met for the work amount; no refusal test covers another
  work value.
- **R34G3-2 (High).**  `tests/roundtrip_assignment_growth.cpp:341-350` declares the dwords
  at +0, +16 and +32 of the appended assignment `Fixed2Data` record as normalised tail
  ranges, which `save_pair_noise.h` exempts from the mismatch check.  +16 and +32 are the
  task and resource identity references: a port save naming the wrong resource row passes
  `compare_all`.  Only hand-written checks on the port's own output (hard-coded rows) pin
  them today.
- Medium R34G3-3: `assignment_growth.cpp:367` bounds check in 32-bit arithmetic wraps for
  offsets at or above `0xFFFFFFFC` (heap out-of-bounds read on a crafted file).  Medium
  R34G3-4: the count-3->4 and no-free-placeholder refusals have no test; one refusal test's
  needle ("measured") matches almost any refusal.
- Lows R34G3-5..9 in `review-g3.md`.

Merge rule: wave 34 is NOT mergeable while R34G3-1 and R34G3-2 are open.  They go to G0 of a
fix-only `wave34b` after CLOSE, with the two Mediums; the Lows are queued in the plan.
