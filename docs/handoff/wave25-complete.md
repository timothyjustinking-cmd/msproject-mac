# Wave 25 complete — verify-and-fix, then the carried parity work

_Written 2026-10-24 by the wave-25 coordinator (DeepSeek) on branch `wave25`,
cut from local `wave24` = `2561a51`.  The work order is
`docs/handoff/wave25-brief.md`; it answers the verifier's
`docs/handoff/wave24-verification/` (S1..S14) and the items wave 24 deferred.
**Nothing is pushed and nothing is merged to `main`; the publication hold
(brief section 0) is still in force.**

**Branch state.** `wave25` was cut from `2561a51` (the wave-24 tip) and carried
the brief in `2fd23f5`.  This session's commits, one finding id each, are listed
in §1.  No frontier ref exists on the remote (`origin/wave25` does not exist);
the wave-24 backup branches are untouched.

## 1. Every W25 item

| item | finding | status | commit(s) |
|---|---|---|---|
| W25-1 | S1 (High) — row 0 on the shared and macOS paths | **FIXED** | `b73fd26` |
| W25-2 | S2 (High) — "stock" bar colours are manual-vs-auto | **FIXED** | `5f3a12d` (fixture/reference), `9ec15a8` (code) |
| W25-3 | S3/S4 (Medium) — the privacy scrub, done properly | **FIXED** (golden-TSV hashes remain; see §6) | see §1 table below |
| W25-4 | S5 (Medium) — honesty of the record | **FIXED** — errata in §2 | this record |
| W25-5 | S6 (Medium) — stored_table PROJECT_A text-style assertion | _in progress_ | |
| W25-6 | S7 (Medium) — scenario oracle provenance | _in progress_ | |
| W25-7 | S9 -- ribbon `kind` reads the live widgets | _in progress_ | |
| W25-8 | S10 — record-count pin and the Flag block fails when missing | _in progress_ | |
| W25-9 | S11 — Flag11..17 single-bit controls + generator | _in progress_ | |
| W25-10 | S12 — both halves of the Timeline pair | _in progress_ | |
| W25-11 | S13/S14/R16/R17 — names, comments, PDF subtitle, dispositions | _in progress_ | |
| W25-12..21 | carried parity streams (W24-12/13/14/17/18/19/20/21/22/23/24/25) | _see §3_ | |

## 2. Errata for wave-24 claims (W25-4)

The wave-24 commits are frozen under the publication hold; these corrections are
recorded here rather than by rewriting them.

### 2.1 Gate numbers with no supporting log on the commit they name

| claim | where | what the record shows | erratum |
|---|---|---|---|
| "app_qt ctest 18/18" | commit `ca9e7d7` message; wave24-complete §2 | `ca9e7d7:app_qt/CMakeLists.txt` registers **17** tests (`git show ca9e7d7:app_qt/CMakeLists.txt \| grep -c "add_test(NAME"` = 17; the 18th, `qt_uid_routing`, appears at `ff22c7b`). The same commit rewrites `project_a_preds_golden.sha256` to the per-row format while `task_order_test.cpp` at that commit still takes the first line as one aggregate digest, so `qt_task_order_corpus` cannot pass on that tree. | The claim is false for `ca9e7d7`; no app_qt run can have produced 18/18 there. |
| "full fresh set" | commit messages `ff22c7b` and `cc4ce8a`, both timed 00:09 | `build/w24` holds no log for either tree; the earliest qt log is 00:43, the earliest final-code logs are 00:43-00:46. | Two commits a minute apart cannot both have run the whole set; treat both messages' numbers as unsupported. |
| "fresh trees of the final code (39d11b4)" | wave24-complete §2 | `build/w24/fresh-app-ctest.log` and `fresh-engine-suite.log` are dated **00:22**, before the 00:41 rebuild that includes `39d11b4`'s `app/layout_model.h` change. `fresh-ctest.log` (00:46), `fresh-plan-recalc.log` (00:46) and `fresh-qt-ctest.log` (00:46) are post-`39d11b4`; the final-code **app** evidence is `build/w24/gate-app-ctest.log` (00:43), not the "fresh" file. | The "fresh" label applies only to core/plan_recalc/qt; app was run on the final tree but not on a fresh configure. |
| reader 1063 / 383 | wave24-complete §2 | no wave-24 log records the reader counts. | Reproducible from the test but not part of the logged evidence; require a log next time. |
| parity 10/10 and pixel score 16.08 % | wave24-complete §2 | no wave-24 log records either; only the numbers were transcribed. | The committed reference JSON exists and wave 25 re-ran the gate (§5); the wave-24 number itself is unlogged. |
| client-name scan "0 files / 0 hits" | wave24-complete §2 | no scan log under `build/w24`; the scanner's own `MIN_LEN` history shows the pre-`39d11b4` rule could not see the nine-character name that was still present. | The only defensible statement is the post-`39d11b4` scan; wave 25 logs its own scans per commit. |
| fixtures "OK (13 documented hashes, 12 tracked .mpp)" | wave24-complete §2 | correct at the tip by content (`scripts/check_fixtures.py` at `2561a51` documents 13 paths; the tree tracks 12 `.mpp` including `analysis/test_project.mpp`), but the only log in `build/w24` is `gate-fixtures.log` at 00:13 saying **11 / 10**, which is the pre-`39d11b4` tree. | The number is right; the log beside it is not the tip's.  A tip fixtures run was never logged. |

### 2.2 START-HERE corrections

* "fixtures **OK** (11 hashes, 10 `.mpp`)" was the **00:13** pre-review-fix count,
  not the wave-24 tip's.  At `2561a51` it is **13 documented hashes, 12 tracked
  `.mpp`**; wave 25 adds one synthetic fixture and its reference JSON, so wave
  25's count is **15 / 13**.
* "the tree is clean as of `ff22c7b`" is false: `ff22c7b` re-added a
  nine-character client resource name in `app_qt/sheet_views.cpp`, and
  `39d11b4` is the commit that removed it.  The clean baseline is `39d11b4`.
  Both statements are corrected in `docs/START-HERE.md` in this wave.

### 2.3 Claims that stand

`gate-core-ctest.log` 51/51, `gate-plan-recalc.log` 266 checks,
`gate-app-ctest.log` 15/15, `gate-qt-ctest.log` 18/18, `gate-m2.log` 332/332
(268 leaves / 64 summaries), and `gate-engine-suite.log` (7 pass / 1 expected
fail, `constraint-mso`) all exist at 00:43 on the `39d11b4` tree.  The
wave-24-complete §2 numbers match those logs.
