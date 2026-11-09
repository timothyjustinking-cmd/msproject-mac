# Wave 35d G0 (CI and the three wave 35c Highs): coordinator verification (Tue 10 Nov 2026)

The gated commit is `86dbee9` (`WAVE-35d-G0-COMPLETE`).  It covers items 0.1 to 0.5: 6 builder
commits, `23de6e2` to `86dbee9`, on `wave35d` from `e17ddb7`.  The builder's own record is
`gates-g0.md`.

Gates ran in the verify worktree from 02:57 to 03:18, with:
- the worktree detached at `86dbee9`;
- fresh build trees;
- Release, with `CORE_WARNINGS_AS_ERRORS=ON`;
- the corpus junctioned in read-only.

Logs are in `build/verify-w35d/g0/` (git-ignored).

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 65/65 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; exit 0 |
| M2 `recalc --compare` | exit 0 |
| app_qt | 52/52, 0 warnings |
| app | 34/34, 0 warnings |
| fixtures; markers | OK; OK |

- **0.1 (GCC) and 0.2 (macOS) are not proved here:** CI after CLOSE is their gate.
- **The reviewer's read of both:**
  - No designated initializer is left anywhere in `app/`, `app_qt/`, `tests/` or `core/`.
  - The About case is identical to the version before `c235973`.

## Mutations (`build/verify-w35d/mutate-g0.py`, `g0/mutations.log`)

9 mutations, one at a time, all anchors count 1.  **7 of the 9 are red.**  The worktree was clean
afterwards, and every lane was green again.

| id | guard switched off | result |
|---|---|---|
| **P3** | 0.3 Props record count (fewer): **green in wave 35c** | core red (2) |
| P5 | 0.3 Props header words | core red (4) |
| P6 | 0.3 Props record count (more) | core red (2) |
| S1 | 0.4 `apply_sort_choice`: the redo-top Sort cleared | sort persist red (4) |
| **S2** | **0.4 `apply_session_sort`: the redo-top Sort cleared** | **green** |
| S3 | 0.4 `clear_session_sort`: the redo-top Sort cleared | sort persist red (2) |
| **S4** | **0.4 the rebuild re-applies a queued Sort** | **green** |
| C1 | 0.5 Cut: the structural redo list cleared | red in both lanes (4, 3) |
| C2 | 0.5 Paste: the structural redo list cleared | red in both lanes (4, 4) |

- **P3 is red now, so R35cG0-1 is closed.**
- **S2 and S4 confirm the reviewer's R35dG0-4.**
  - On the measured routes, both sit behind S1 and S3.
  - Undo keeps the session sort, so the re-apply never changes what is shown.
  - They are recorded as redundant, not as the only guard against a bad save.

## Independent review (`review-g0.md`)

A read-only agent reviewed `e17ddb7..86dbee9`: 0 Critical, **1 High**, 1 Medium, 3 Low.

**R35dG0-1 (High):** a Task Information or Task Form edit that only partly applies keeps the
structural redo list.  The coordinator confirmed this by reading `apply_task_information` at
`86dbee9`.
- The steps:
  1. Sort, then Ctrl+Z.
  2. Enter a new Name and a Duration that does not parse.
  3. The Name commits and the Duration fails, so `ok` is false.
  4. Neither `clear_redo()` nor `after_cell_change()` runs, and the committed Name is not rolled
     back.
  5. After the Undo the Sort is on the redo list, not queued, so the queued-view guard does not
     fire.
  6. Ctrl+Y re-queues the Sort over the renamed plan, and File > Save writes the file's own
     names.
- The same holds for a withdrawn Show Level.
- The defect was already there in 35c: it is the R35cG1-2 route through the dialog.
- **Fix:** clear the redo list whenever any field committed with `outcome.changed`, whatever
  `ok` is.  Add that case to both persist tests.

**R35dG0-2 (Medium):** `gates-g0.md` and `wave35d-complete.md` call 0.5 "fixed", and
`gates-g0.md` says Task Information "already clears".  By the brief's rule, 0.5 is "partly fixed:
Task Information partial apply (R35dG0-1)".

**Lows:**
- R35dG0-3: the re-applied Sort's description maps the field id as Name or Duration by the
  writer's id.  It is wrong for Duration, but no path can queue a Duration Sort today.
- R35dG0-4: the `apply_session_sort` clear and the rebuild re-apply are untested (S2 and S4
  confirm).
- R35dG0-5: `check_props_pin`'s tail-length and scalar-count branches have no needle test.

0.1 to 0.4 close their findings in the layers each names.  The Show Level Undo-first route does
not diverge, because Redo always re-shows the level; the builder's conclusion is correct.

## Merge rule

G0 closes R35cG0-1 and R35cG1-1 in source, and R35cW-CI1 and R35cW-CI2 pending CI.  It closes
R35cG1-2 only in part, and adds **R35dG0-1 (High)**, which goes to the front of the next fix
wave.  G1 is running and is not interrupted.
