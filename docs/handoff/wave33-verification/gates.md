# Wave 33 verification: fresh-tree gates

_Run 30 Oct 2026 06:54-07:15 by the Opus coordinator on commit `617fc8d` (the replayed
`wave33` tip plus the sha map; tree identical to CLOSE's `22a3a96` apart from
`rewrite-sha-map.txt`).  Fresh build trees `build/vs-verify33`, `build/vs-verify33-qt`,
`build/vs-verify33-app` (none existed before the run), VS 2022 x64 Release, Qt 6.5.3
msvc2019_64, `CORE_WARNINGS_AS_ERRORS=ON` on all three.  Runner
`build/verify-w33/run-gates.cmd` (the wave 32 runner plus a `check_markers.py` step); logs
local under `build/verify-w33/` (git-ignored)._

| gate | result |
|---|---|
| core configure / build | exit 0 / exit 0, 0 warnings |
| core ctest (`-E plan_recalc`) | 55/55 passed |
| plan_recalc_tests, `MPP_NO_CORPUS=1` | 266 checks passed (corpus-gated cases SKIP by design) |
| engine_diff_tests self-test | 265 checks, 0 failures |
| engine suite `--suite` | 8 recorded, 7 pass, 1 fail: `constraint-mso` (task 3 duration_ticks 9600 vs 12000), the known expected failure since wave 32 |
| engine suite `--suite --expect-fail constraint-mso` | exit 0, "expected failing set reached" |
| M2 (`recalc.exe` real file vs Project's recalculated copy) | 332/332 fields match (268/268 and 64/64 in the two families), unexplained: none |
| `check_fixtures.py` | exit 0: 223 tracked test-data files, 174 `.mpp/.mpt` property sets clean, 8 scenario oracles' provenance OK |
| `check_markers.py` | exit 0: 1302 tracked files, 9 markers, 0 commit-message hits on `origin/main..HEAD` |
| app_qt configure / build | exit 0 / exit 0, 0 warnings |
| app_qt ctest | 41/41 passed |
| app configure / build (`APP_BUILD_TESTS=ON`) | exit 0 / exit 0, 0 warnings |
| app ctest | 25/25 passed |
| lane B property-set scan (`build/audit-w32/laneB_mpp_props.py`) | 174 `.mpp/.mpt` scanned, 0 without property sets, 0 unreadable, 0 flagged |

## The replay

The operator approved (30 Oct 2026) one replay of the unpushed range `17f5782..wave33`
before the first push.  Old tip `22a3a96` (kept locally as `wave33-pre-replay`); new tip
`877d012`; 44 commits each way; map in `rewrite-sha-map.txt` ("old new" per line).
Checked after the replay, before moving the branch:

- final trees identical (`035cd79e`);
- per-commit tree comparison old vs new: exactly one commit differs, `3b53b9d`, in
  exactly one path, `tests/data/wave33-fat-slack.mpp` (blob `bc7446d8` -> `a2ee86c8`,
  964594d's neutralised copy);
- author, committer, dates and messages compared for the whole range: one line differs,
  in `964594d`'s message ("for CLOSE and" + the operator's first name -> "for CLOSE and
  the operator"), which the CLOSE review found the CI marker check would refuse;
- `guard_names.py 17f5782..<new tip>` exit 0; `check_markers.py --range
  17f5782..<new tip>` 0 file hits, 0 message hits.

## Not covered here

- CI: dispatched after this record is pushed; results are recorded in the coordinator's
  report and the wave 33b brief (section 2, W33b-0.2).
- The brief's section 4 RED-on-`17f5782` runs for W33-0.1, W33-0.2 and W33-1.1 were not
  re-run by the coordinator; the wave's own RED/GREEN logs exist under `build/w33/`
  (`W33-0.1-red.log`, `W33-0.1-atomic-green.log`, `W33-0.2-*`, `W33-1.1-*`).
- macOS and Linux GCC builds of `app/`: CI only.
