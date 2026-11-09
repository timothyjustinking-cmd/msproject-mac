# Wave 35b G4 (the Lows): coordinator verification (Mon 9 Nov 2026)

The gated commit is `90d8b4e` (`WAVE-35b-G4-COMPLETE`).  It covers the G4 Low ids, 25 builder
commits from `51b3ef8` to `90d8b4e`.  The builder's own G4 record is `gates-g4.md` in this
folder; the coordinator's record is this file and `review-g4.md`.

Gates ran in the verify worktree, detached at `90d8b4e`, from 16:58 to 17:33, with:
- fresh build trees;
- Release, with `CORE_WARNINGS_AS_ERRORS=ON`;
- the main corpus junctioned in read-only.

Logs are in `build/verify-w35b/g4/` (git-ignored).  The commit ids are the builder's; `wave35b`
is rebuilt onto the new `wave35` after CLOSE (`gates-g0.md`).

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 65/65 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; expected failing set reached (`constraint-mso`) |
| M2 `recalc --compare` | exit 0 |
| app_qt | 52/52 on the first run, 0 warnings |
| app | 34/34, 0 warnings |
| fixtures; markers | OK; OK |

- `qt_assign_resources_gate` passed on the first run again (its second clean run after G2's
  failure).  R35bG2-9 stays open until macOS CI-4 is seen green.
- **Not compiled here:** `app/main.mm`.  No G4 change touches it; CI's macOS jobs are its only
  compile gate.

## Fixtures, names and trailers

- G4 adds or changes no `.mpp` blob (`af78097..90d8b4e`: 0).
- `guard_names.py` on the range exits 0, and `scan_client_names.py` finds 0 hits at `90d8b4e`.
- `check_markers` on the range: 0 hits.
- **Trailers:** all 25 commits in the range carry
  `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35b)`.
- **Process note:** `ec07ab7` (R35G0-12) also carries the coordinator's G3 records,
  `coord-gates-g3.md` and `review-g3.md`.  The coordinator staged them in the shared tree while
  the builder was committing; their content is the coordinator's, byte for byte, and no history
  is rewritten for it.  The coordinator now stages and commits in one step, and only while no
  builder runs.

## Mutations (`build/verify-w35b/mutate-g4.py`, `g4/mutations.log`)

12 guards were switched off with a non-constant false (or a constant changed), one at a time.
Each guard's lanes were rebuilt and run by exact ctest name, then the file was restored.  The
worktree was clean afterwards, and all 7 lanes were green again.

**10 of 12 are red in their own lane.**

| id | guard switched off | own lane | other lanes |
|---|---|---|---|
| R7a, R7b | RW35-R7: Collapse All / Expand All refused under a queued Show Level | Qt outline red, each | |
| R7c | RW35-R7: the queued-Sort undo message | Qt sort red | |
| G7a | R35G0-7: `push()` refuses a task whose Type block was not read | app red | |
| G7b | R35G0-7: the reader adapter never sets `type_block_known` | app red | Qt assign red |
| **G7c** | R35G0-7: the `TaskRow` default `type_block_known = false` changed to `true` | **app green** | |
| G7d | R35G0-7: the `ResourceRow` default `calendar_base_uid = -1` changed to `1` | app red | |
| G6 | R35G0-6: the no-calendar-row refusal | app red | |
| P5 | R35G0-10: the 114/Props measured old length | core props red | Calculate green (its writes are measured transitions) |
| **F1** | R35G1-12: `check_unvaried_parameters` back to the positional FixedMeta formula | **core Calculate green** | |
| F2 | R35G1-12: `read_calculate_shape` back to the positional formula | core Calculate red | |
| N1 | R35G1-10: the pair-name substitution in `read_task_file_shape` | core Calculate red | app Calculate green (the app's own texts) |

The two greens in an own lane are untested guards, not a second layer:
- **G7c (R35bG4-6, coordinator, Low):** the task case in `app_edits_assignment_tests.cpp` sets
  `row.type_block_known = false` by hand; no test builds a `TaskRow` from its defaults, as the
  resource case does with `ResourceRow{}`.  The default can be flipped with every test green.
- **F1** confirms the reviewer's R35bG4-3: the order-varied FixedMeta test reaches only the
  `read_calculate_shape` site.

## Independent review (`review-g4.md`)

A read-only agent reviewed `af78097..90d8b4e`: 0 Critical, **0 High**, 1 Medium, 4 Low.
Every id in `gates-g4.md` is closed or accurately recorded, apart from the partial closes below.

**R35bG4-1 (Medium):** three of R35bG3-1's paths remain.
- RW35-R7 closes Collapse All and Expand All, which is what it asked for.
- These paths still rebuild the sheet with every row shown while the level stays queued, and
  File > Save writes the level:
  - Redo after Undo of a queued Show Level (Redo re-adds the edit without `push()`);
  - Redo after All Subtasks or a refused replacement level (the withdrawal calls `undo()` with
    no `clear_redo()`);
  - the project-summary toggle.
- No test covers redo or the toggle.

**Lows:**
- R35bG4-2: a refusal thrown as an exception inside `read_task_file_shape` returns before the
  pair-name swap, so Calculate can still name the task-mode save.  The coordinator confirmed
  this in the diff.
- R35bG4-3: Fixed2Meta's active flag is still read by position, and only one of the two
  FixedMeta sites is reached by the order-varied test (mutation F1).
- R35bG4-4: Calculate's task checks still read open defaults (`active`, `baseline_stored`,
  `type_block_stored`).  Not reachable today: pending rows are refused first, and the writer
  re-reads the file.
- R35bG4-5: the RW35-R8 reason for leaving `backstage_gate_test` on one-look timers does not
  hold; its About timer is the pattern behind CI-4.

**Clean:**
- FixedMeta by row offset matches `task_mode`'s first-match rule.
- The six 114/Props "from" sizes match the pair fixtures, and no measured caller is newly
  refused.
- Every file row is reader-built; the substitution loop terminates and skips nothing.
- The R7 needles belong to the new refusals only, and the R8 migration lost no coverage.
- The README hashes match.
- Hygiene.

## Merge rule

G4 adds no new High.  Open:
- R35bG0-1 and -2 (Calculate Project, the next fix wave);
- the `main.mm` compile and CI-4 on macOS CI;
- the 4(e) Project reopen check.

R35bG4-1 joins the next fix wave's Mediums.  It is to be fixed together with R35bG3-1, as one
guard over every collapse-state change, `clear_redo()` on withdrawal, and the level re-applied
on rebuild.  R35bG4-2 to -6 join its Lows.
