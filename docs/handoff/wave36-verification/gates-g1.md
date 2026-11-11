# Wave 36 G1: the builder's gates

Builder Claude Sonnet 5.5, 11 November 2026, branch `wave36`.  Logs are under `build/w36/` (git-ignored), each named for the parent
commit of the item it gates.  The trees are `build/w35g-close-fresh` (core), `build/w35g-close-fresh-app` and `build/w35g-close-fresh-qt`,
configured fresh at the wave 35g CLOSE and built incrementally here (Release, MSVC).  The builder used no COM and did not touch the VM.

## Per commit

| item | commit | parent (log prefix) | core | app | Qt | doc gates |
|---|---|---|---|---|---|---|
| W36-1.1 | `baab011` | `53351b2-W36-1.1-*` | not rebuilt (no core change) | build 0, ctest 34/34 | build 0, ctest 52/52 | 0 / 0 / 0 / 0 / none / clean |
| W36-1.2 | `95e8195` | `baab011-W36-1.2-*` | build 0, ctest `-E plan_recalc` 67/67 | build 0, 34/34 | build 0, 52/52 | same |
| W36-1.3 | `893684f` | `95e8195-W36-1.3-*` | build 0, 67/67 | build 0, 34/34 | build 0, 52/52 | same |
| W36-1.4 | `64a185e` | `893684f-W36-1.4-*` | doc only | doc only | doc only | same |
| W36-1.5 | `1fa7338` | `64a185e-W36-1.5-*` | doc only | doc only | doc only | same |

Doc gates, in order: `check_fixtures` exit, `check_markers` (range and worktree) exit, `guard_names` exit, `scan_client_names` exit,
the `client-string-[0-9a-f]{8}` grep (none), `git diff --check`.  W36-1.3 also needed two Qt fixes before its green: `app_qt/ribbon_pin.tsv`
(`I` enabled) and the generated `docs/ui-command-backlog.md` (via `app_qt/tools/command_backlog.py`); both failures are in the record.

## Per group, at `1fa7338`

`build/w36/1fa7338-W36-G1-group*` (a full rebuild and run of every lane after the last commit):

| gate | result |
|---|---|
| core build | exit 0 |
| core `ctest -E plan_recalc -j4` | 67/67 (includes `engine_diff_harness`, `engine_diff_apply`, `engine_diff_suite`, `mppwrite_roundtrip_viewstream`) |
| `MPP_NO_CORPUS=1 ctest -R plan_recalc` | 1/1 |
| M2 `recalc corpus/project_a.mpp --compare corpus/probe/project_a-recalc.mpp` | overall 332 / 332 fields match (the other two `overall` lines, 268 and 64, are subsets) |
| `check_fixtures.py` | exit 0 |
| app build, ctest | 0, 34/34 |
| Qt build, ctest | 0, 52/52 |
| `git status` after the lanes | clean (the `plan_recalc` run left no golden change) |

## Red then green

Quoted in `docs/handoff/wave36-complete.md`, "Guards seen red".  The one red that is not the old tree's own failure is W36-1.3's app
test, which went red by a temporary probe (the italic builder made to refuse), removed before the commit.

## Not gated here

`app/main.mm` (AppKit) builds only on macOS: CI job `macos-appkit`.  The 4(e) read-back is the coordinator's, and is planned, not run
(`wave36-complete.md`, W36-1.4).  The verify-worktree fresh-clone gates, the mutations and the independent review follow the marker.
