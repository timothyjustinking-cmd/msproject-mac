# Wave 35b G3 (the Mediums: view state, Bold and the shells): coordinator verification (Mon 9 Nov 2026)

The gated commit is `af78097` (`WAVE-35b-G3-COMPLETE`).  It covers items 3.1 to 3.8 (17 builder
commits, `961b311` to `af78097`; `70b8a3b` in the range is the coordinator's G2 record).  The
builder keeps its own G3 summary in `gates-g3.md` in this folder, so the coordinator's record is
this file and `review-g3.md`.

Gates ran in the verify worktree, detached at `af78097`, from 14:42 to 15:18, with:
- fresh build trees;
- Release, with `CORE_WARNINGS_AS_ERRORS=ON`;
- the main corpus junctioned in read-only.

Logs are in `build/verify-w35b/g3/` (git-ignored).  The commit ids are the builder's; `wave35b`
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

- `qt_assign_resources_gate` passed on the first run this time.  R35bG2-9 stays open: an
  intermittent failure is not closed by one pass, and macOS CI-4 is the same test.
- **Not compiled here:** G1 item 1.7's `app/main.mm`.  CI's macOS jobs are its only compile gate.

## Fixtures, names and trailers

- G3 adds or changes no `.mpp` blob (`733d6d3..af78097`: 0).
- `guard_names.py` on the range exits 0, and `scan_client_names.py` finds 0 hits at `af78097`.
- `check_markers` on the range: 0 hits.
- **Trailers:** all 17 builder commits carry
  `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35b)`.  The G2 gap (R35bG2-4) did not
  recur.
- The completion file again holds the marker line only (the G0 process defect).
- **Process note:** the builder created `gates-g3.md` in this folder, the name the coordinator's
  records use.  The coordinator's G3 record is therefore `coord-gates-g3.md`.

## Mutations (`build/verify-w35b/mutate-g3.py`, `g3/mutations.log`)

25 guards were switched off with a non-constant false (or a constant changed), one at a time.
Each guard's lanes were rebuilt and run by exact ctest name, then the file was restored.  The
worktree was clean afterwards, and all 13 lanes were green again.

**Every guard is red in its own lane.**

| id | guard switched off | own lane | other lanes |
|---|---|---|---|
| O1 | 3.1: `push()` checks the Show Level outline is a measured shape | red | Qt green (the shell builds through the writer's dry run; no hand-built push) |
| O2, O3 | 3.1: the writer's shape check; the shared `outline_shape_measured` forced true | red | app red (O3) |
| O4 | 3.4: `kOutlineFlagCollapsed` 0x03 changed to 0x02 (M13 on G2) | red | app red, **Qt red** (the Qt outline gate is no longer vacuous) |
| Q1 | 3.2: the hand toggle refused while a Show Level is queued | red | |
| S1 | 3.4: the sort direction word flipped in the writer only | red | **Qt red** on the saved bytes, not at queueing |
| V1 | 3.5: the byte-42678 mask widened to 42677..42679 | red | app red, Qt red |
| V2 | 3.5: the mask removed | red | app red, Qt red |
| U1, U2, U3 | 3.6: one cell format item; the item is last; the item is the measured Bold item | red, each | app green (the app tests build no such layout; the writer is the only reader of these file shapes) |
| C1, C2, C3 | 3.7 R4: `push()`'s slack recompute; its finish and duration check; its late-start compare | red, each | |
| C4 | 3.7 R4: the writer's slack and late-start recompute | red | app green (C1 stops the hand-built edit first) |
| C5, C6 | 3.7 R4: the shared calendar helper counts weekends; the late start one day early | red, each | app red, Qt red |
| P1 to P4 | 3.7 W7: the 114/Props payload pin call; the WBS mask, the 4-byte date and 00 00 pins | red, each | Calculate green (its own writes are the pinned payloads) |
| K1 to K4 | 3.7 W6: the project-row key call; uid 0 only; eight zero bytes; two WBS digits | red, each | Calculate green (as P) |

- Each green in another lane is the earlier layer or the absent shape named in the table.
- C5 and C6 confirm the calendar helper is checked against Project's own words: both sides use
  the helper, and the pair compare goes red when it is wrong.

## Independent review (`review-g3.md`)

A read-only agent reviewed `733d6d3..af78097`: 0 Critical, **0 High**, 1 Medium, 4 Low.
- Items 3.1, 3.3 to 3.6 and 3.8 are closed.
- 3.7 is closed, with the R4 parity gap below.
- 3.2 is partly closed.

**R35bG3-1 (Medium):** other commands still change the displayed outline under a queued Show
Level, and Save writes the level.
- 3.2 guards only the delegate's per-row toggle.
- Expand All and Collapse All (`app_qt/main_window.cpp`, `MenuAction::collapse_all` /
  `expand_all`) have no queue check.  The coordinator confirmed this by reading the code.
- So do these, because each rebuild makes a sheet with every row expanded while the level stays
  queued:
  - structural undo then redo;
  - redo after All Subtasks (the withdrawal leaves the level on the redo stack);
  - the project-summary toggle.
- The fix is one guard over every collapse-state change.  After a withdrawal, call
  `clear_redo()`, and re-apply the level on rebuild.

**Lows:**
- R35bG3-2: `push()` accepts a Calculate edit for a tie, or for a plan whose second leaf ends
  the project.  Only the writer refuses these, so no bad file is written.
- R35bG3-3: the 3.1 "Level 3 on a four-level outline" case stops at the level cap, not at the
  shape guard.  Level 2 on `{0,1,2,3,0}` would reach it.
- R35bG3-4: the builder's `gates-g3.md` says "Show Level 1 to 3"; the measured cap is 2.
- R35bG3-5: Bold on still accepts a second session's view block without a second-session
  pair.  The brief allowed this; a second-session Bold-on pair joins the measuring wave's list.

**Clean:**
- 3.1: covers every `show_level` push.
- 3.2: `back()` is sound because Show Level is exclusive.
- 3.3 and 3.4: specific needles; both Qt gates compare their save with Project's pair over
  every stream.
- 3.5 and 3.6: each test edits the bytes it claims.
- 3.7: the calendar arithmetic is right (day 0 is a Saturday; no off-by-one), and no measured
  caller is newly refused by the W6 and W7 pins.
- 3.8: the corrections are accurate.
- Hygiene.

## Merge rule

G3 adds no new High.  Open:
- R35bG0-1 and -2 (Calculate Project, the next fix wave);
- the main.mm compile and CI-4 on macOS CI;
- the 4(e) Project reopen check.

R35bG3-1 joins the next fix wave's Mediums, beside R35bG1-1 to -3 and R35bG2-1.  The Lows join
its queue.
