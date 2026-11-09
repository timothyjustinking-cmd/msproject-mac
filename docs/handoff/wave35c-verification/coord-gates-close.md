# Wave 35c CLOSE: coordinator verification (Tue 10 Nov 2026)

Wave 35c is `2a97272..edb3051`, 47 commits:
- 2 coordinator commits: `47e26a5` (the brief) and `2ceb810` (the trace notes);
- 45 builder commits for G0 to CLOSE.

The CLOSE commit `edb3051` (`WAVE-35c-COMPLETE`) changes docs only: START-HERE and
`wave35c-complete.md`.  The driver finished at 02:10:44 on 10 November 2026 and exited.

## The groups

| group | gated commit | gates | mutations | review |
|---|---|---|---|---|
| G0 | `b538646` | green, 0 warnings (65 / 52 / 34) | 7/8 red; P3 green | 0C / **1H** / 2M / 3L |
| G1 | `ea54226` | green; core 64/65 on the first run (a process-start failure), the test passing alone | 25/27 red; Paste green, Fill Down green but redundant | 0C / **2H** / 2M / 3L |
| G2 | `319d187` | green, 0 warnings (65 / 52 / 34) | 11/11 red (M11 after a rerun) | 0C / 0H / 1M / 7L |

The details are in `coord-gates-g0.md`, `-g1.md` and `-g2.md`, and in `review-g0.md`, `-g1.md`
and `-g2.md` (all in this folder).

## Range scans (`2a97272..edb3051`)

- `guard_names`: 0.
- `scan_client_names` at `edb3051`: 0 files, 0 name hits.
- `check_markers`: 0.
- `.mpp` blobs added: 0.
- Every builder commit carries `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35c)`.

## CI (pushed `wave35c` at `edb3051`, new branch, no force)

| run | job | result |
|---|---|---|
| ci 42642217605 | windows-msvc | success |
| | ubuntu-gcc | **failure** |
| | ubuntu-asan | success |
| qt-shell 42642224485 | linux-qt | success |
| | windows-qt | success |
| | macos-qt | **failure**, 49/50 |
| app-macos 42642202349 | macos-appkit | success |

- **W35c-0.1 is proved.**  `linux-qt` was red in 35b on GCC 13 `-Werror=dangling-reference`
  (R35bW-CI1); it is green now.
- **R35cW-CI1 (ubuntu-gcc):** `-Werror=missing-field-initializers` at
  `app/app_edits_assignment_tests.cpp:638`, on `app::ResourceRow{.uid = row.uid, .id = row.id}`.
  - The line came in with wave 35b's `7cfb65d`.  In 35b the core build broke first, so the app
    suite was never compiled on GCC.
  - It is test-only.  **Fix:** a default-built row with `uid` and `id` assigned after it, so no
    initializer list is left partial.
- **R35cW-CI2 (macos-qt):** `qt_backstage_gate` fails 2 About-box checks on macOS only
  ("the About button opens a modal About box", "the About box is the port's own").
  - It regressed in 2.18 (`c235973`), which replaced the macOS-hardened top-level widget walk
    with `gate::PromptAnswerer`.  The comment it removed recorded that `activeModalWidget()`
    returns null for this box on macOS offscreen (qt-shell 40054734014).
  - It is test-only.  **Fix:** restore the top-level walk for the About case.

## 4(e): Project's reopen of the enabled saves

- **Method:** `build/verify-w35c/reopen_4e.py` and `reopen_4e_b.py`, copied from wave 35b.
- **Inputs:** the port saves from the verify worktree's app test output.  `app` ctest was re-run
  first, 34/34, so every save comes from the clean `319d187` build, whose code equals `edb3051`.
- **Result: 9 pairs pass, with 0 fields differing between the port's reopen and Project's.**
  - The 9: Calculate Project under manual calculation, Sort Name descending and Duration
    descending, Show Level x3, and the assignment add and remove.
  - **Vacuous:** Sort Name ascending (by design, since an ascending sort leaves the order) and
    Bold (Bold cannot be read through COM).
- This is the same as wave 35b.
- Evidence (git-ignored): `build/verify-w35c/reopen-4e/`.

## Whole-wave review (`review-wave.md`)

0 Critical, 0 High, 2 Medium, 3 Low.

- **R35cW-1 (Medium):** none of the per-group findings was in the repo, and START-HERE reads as
  if no High is open.
  - This commit puts the reviews and coordinator records in this folder.
  - START-HERE, `memory/coordinator.md` and `plan-waves27-30.md` still need the open R35c queue,
    which is the next fix wave's first item.
- **R35cW-2 (Medium):** the CLOSE id table marks four ids "fixed" that the code closes only in
  part.
  - R35bW-2: see R35cG1-1.
  - R35bW-1: see R35cG1-2 and R35cG1-3.
  - R35bG2-2: see R35cG2-1.
  - R35bG3-1: see R35cG1-4.
- **Lows:**
  - R35cW-3: 2.2 closed the writer half of R35cG0-2 without a record saying so.
  - R35cW-4: the queued-view guard does not cover a queued Calculate Project.  The saved file is
    still right; only the display is wrong.
  - R35cW-5: the Props pin runs before the unvaried-parameter checks, so the writer names the pin
    as the reason for every real file.

The reviewer confirmed by reading that every 0.2, 1.5, 0.3 and 1.7 test still reaches its own
guard first, with G2's checks ahead of them.  The G0 and G1 mutations were not re-run at
`edb3051`.

## Open queue for the next fix wave (Highs and CI first)

1. **R35cW-CI1** (ubuntu-gcc) and **R35cW-CI2** (macos-qt): CI on three OSs is a merge gate.
2. **R35cG0-1 (High):** three `check_props_pin` branches untested (mutation P3).
3. **R35cG1-1 (High):** Undo, then a refused or cleared Sort, then Redo, re-queues the withdrawn
   Sort.
4. **R35cG1-2 (High):** Cut and Paste do not clear the structural redo list.
5. **Mediums:** R35cG0-2, R35cG0-3, R35cG1-3, R35cG1-4, R35cG2-1, R35cW-1, R35cW-2.
6. **Lows:** R35cG0-4 to -6, R35cG1-5 to -7, R35cG2-2 to -8, R35cW-3 to -5.

## Merge rule

**Waves 34 to 35c are not merged.**  CI is red on two jobs, and three Highs are open.  4(e)
passes.
