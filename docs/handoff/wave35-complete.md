# Wave 35 completion record

Branch `wave35`.  Nothing here is pushed; the coordinator pushes and dispatches CI.  No COM
and no `oracle_*` script was run.  Every commit carries its gate result in
`build/w35/<sha>-W35-<item>-*` (git-ignored scratch).

## Group G0

One finding id per commit, in the brief's order.

| Item | Commit(s) | State |
|---|---|---|
| W35-0.1 R34b-1 | eceb4f5 | done |
| W35-0.2 R34b-2 | 3003681 | done; the effort-driven part is deferred with evidence (below) |
| W35-0.2 R34b-3 | 432a825 | done (finding below) |
| W35-0.2 R34b-4 | de17e55 | done |
| W35-0.2 R34b-5 | 91429d3 | done |
| W35-0.2 R34b-6 | c3ba932 | done |
| W35-0.2 RW34b-2, RW34b-3 | 8409061 | done |
| W35-0.3 CI-4 | b0bbd5f | done; cause unproven (below) |
| W35-0.4 RC-W1 | 42d51f7 | done |
| W35-0.4 RC-W2 | 5b9e00a | done; brief premise corrected (below) |
| W35-0.4 RC-W6 | 19a7d21 | done |
| W35-0.4 RC-W7 | 48bb765 | done |
| W35-0.4 RC-W8 | 2b30b23 | done |

Full gates (core, app and Qt builds and ctests, fixtures, markers, guard_names,
scan_client_names) passed at each of the RC-W1, RC-W2, RC-W6, RC-W7 and RC-W8 commits.  Counts
at the last full run: core 62, app 31, Qt 49 tests.

### Deferred, with evidence of the attempt

**R34b-2, effort-driven.**  The corpus was searched for a `.mpp` that pairs with a COM export
showing the effort-driven flag.  `w3-o1` and `w3-o4-last` are all type 0 and not
effort-driven; the varying oracle exports `w3-o2` and `w3-o2b` have no committed `.mpp`.  With
no pair, no bit can be decoded.  The writer still refuses a task FixedMeta entry that differs
from the measured one, so an effort-driven task is refused there; the edit model refuses a
task that stores a type block (the reader flag added in 3003681).

### Findings

- **R34b-3, the task-row word at +128.**  It is not per-save noise.  Across 38 committed
  controls and their no-edit saves every row of a file carries one value, each null save equals
  its control, and the assign add and delete pairs equal their controls.  It differs only
  between fixture families.  It stays compared and the refusal now names it.  Consequence:
  Assign Resources refuses a real file whose stamp differs from the fixtures'.
- **CI-4 (macOS `qt_assign_resources_gate`, "an assigned task is offered the removal").**  The
  cause is unproven.  The shell has no platform branch; the one timing-dependent step is the
  test hook's single look at the first event-loop turn.  The three prompt gates now share a
  polling answerer and a failed removal check prints the visible widgets.  `qt_prompt_hook`
  pins late dialogs (red-checked).  CI is the only macOS check.
- **RC-W2 premise.**  The brief names the third pair as the one that measures a lower uid.  It
  is the second (`wave34-assign-add-second`, a lower uid on one assigned resource); the third
  measures a higher uid on two.  The writer accepts exactly those two orders.

### Notes on tests and process

- **No edit-model test** for RC-W6, RC-W7 and RC-W8: the shell reaches those writers only with
  measured keys, record ids, row uid and field id (the rename and WBS wrappers, the WBS and
  field-flag record ids, and `kBoldMeasuredTaskUid` / `kBoldMeasuredFieldId`), so a refusal
  cannot be driven from the shell.  Each has writer tests, red-checked.
- **RC-W2** is reachable from the shell only through a stale plan, which its edit-model test
  uses.
- **Red runs.**  Each writer or edit-model refusal was shown accepted (a file written, or the
  old wording) with its gate disabled by a non-constant condition (MSVC C4127 is an error on a
  constant one), then green.  The R34b-2 plan tests were red-checked after the fact: with the
  stored-type, units and work refusals disabled, `app_edits_assignment` fails 12 checks (assign
  and removal on a stored type, removal at 50 % units, removal at 24 hours of work, each at the
  builder, in `push()`, and the nothing-queued check); restored and green.
- A first red attempt that did not build or matched no test was never counted as evidence.
- No amend and no history rewrite.

WAVE-35-G0-COMPLETE

## Group G1

The scheduler save path, stage 1.  One finding id per commit, in the brief's order.  Unlike G0,
G1 did run COM: `scripts/oracle_w35_calculate.py` produced the four Calculate Project pairs
(the Windows `gen_py` cache was found corrupt along the way).

| Item | Commit(s) | State |
|---|---|---|
| W35-1.1 decode note | 18cfb08 | done (`spec/writers/calculate.md`) |
| W35-1.2 fixtures | 15d8bb5 | done: four pairs and their null saves |
| W35-1.2 evidence | 2f0ae13 | done: oracle script and four anatomy reports |
| W35-1.2 writer | 82a7d92 | done: `mppwrite::save_calculate`, unlinked two-leaf shape |
| W35-1.3 Calculate Project | 1d04674 | done for the measured shape only |
| W35-1.3 spec errata | 1bd09ee | done (`spec/writers/calculate.md` section 8) |

Gates for each code commit are in `build/w35/<parent>-W35-<item>-*` and quoted in its message.
At 1d04674: core 63, app 32 and Qt 50 tests pass; fixtures, markers, guard_names and
scan_client_names report 0.  `app_edits_calculate` runs 403 checks and `qt_calculate_gate`
passes.

### Deferred, with evidence of the attempt

**Linked, lag and SNET Calculate Project.**  The pairs were measured
(`spec/writers/evidence/wave35-calculate-{linked,lag,snet}.md`).  They differ from the
unlinked pair beyond the slack rule: the Fixed2 leaf stamp is not the same constant (for
example 0x28 against 0x30), and the pairs carry link rows the writer does not model.
The writer therefore refuses them (`roundtrip_calculate`, each with a needle), and the edit
model refuses them up front (`app_edits_calculate` runs the linked, lag and SNET pairs as
refusals).  Respect Links, Move Project, Auto Schedule and Level All stay disabled or
unchanged.

### Findings

- **Hash count.**  The `15d8bb5` message says "209 documented hashes".  The figure
  `check_fixtures.py` reports is 197.  Corrected in `2f0ae13`; `15d8bb5` is not amended.
- **Overtime appends.**  The decode note said nine; every wave-35 pair appends exactly five
  12-byte uid 0 blocks (0x0B4000A4, A5, A8, A9, AA).  Section 8 of the spec records it.
- **Scope of the rule.**  Only the project row plus two unlinked automatic leaves with a
  pending recalculation is written.  The engine-derived slack and late dates equal Project's
  own result words in the committed pair, which is the evidence for the rule.

### Notes on tests and process

- **Red runs for W35-1.3** (`build/w35/red13-*`): the link refusal off fails
  `app_edits_calculate` ("a plan with a link"); leaf slack plus one tick fails the free, start
  and total slack checks for both leaves; the cascade's exclusivity check off fails the
  start-date edit refusal.  A first slack mutation on the builder's dry-run request copy stayed
  green, correctly, because the save builds its request from `edit.slack`; it was redone on the
  right line and is not counted as evidence.  Each run was restored and rebuilt green.
- No amend and no history rewrite.  G0's commits on the same branch were left untouched.

WAVE-35-G1-COMPLETE

## Group G2

View-state persistence.  One finding id per commit, in the brief's order.  G2 did run COM:
`scripts/oracle_w35_viewstate.py` (through `scripts/oracle_lock.py`, neutral root `C:/w35`,
`neutralise()`) produced the sort, table, outline and zoom pairs, each with a null save.

| Item | Commit(s) | State |
|---|---|---|
| W35-2.1 Sort | d6b396f | done: one Name or Duration key on the measured five-task shape (`sort_view` edit, shell queue) |
| W35-2.2 fixtures | ae8fdc7 | done: two `TableApply` pairs and their null saves |
| W35-2.2 Applied table | 94b0796 | deferred with evidence (decode and pair reports) |
| W35-2.3 fixtures | 75b89c0 | done: five `OutlineShowTasks` pairs and their null saves |
| W35-2.3 Outline level | 707425a | done: Show Level 1 and 2 on the two measured outlines (`save_view_outline`, `show_level` edit, shell queue) |
| W35-2.4 fixtures | 5c550ed | done: five `ZoomTimescale` pairs and their null saves |
| W35-2.4 Zoom | daf46f8 | deferred with evidence (the stored words follow the saving window width) |

Gates at daf46f8 (`build/w35/daf46f8-W35-G2-*`): core `ctest -E plan_recalc` 65 of 65, app 34
of 34 and Qt 52 of 52 pass; `plan_recalc` with `MPP_NO_CORPUS=1` passes; `check_fixtures`,
`check_markers`, `guard_names` and `scan_client_names` report 0.  The outline owners are
`mppwrite_roundtrip_viewstate_outline` (1746 checks), `app_edits_show_level` and
`qt_outline_persist`.

### Deferred, with evidence of the attempt

**Applied table (W35-2.2).**  The two pairs append a built-in table definition blob to
`CTable/Var2Data`, add a table row, and re-lay the view block around a name whose length
changes.  The only rule available is "replay these blobs", with the view-block re-layout
measured at two lengths.  `spec/writers/view-state.md` section 2 has the decode; the shell
applies a table to the session only.

**Zoom (W35-2.4).**  The same call, `ZoomTimescale("2w")`, saved from two Project window widths
gives different bytes against byte-identical null saves (the u16 at 44,938 is 0x0257 against
0x01ff, with the scaled date word after it).  The file does not store the pane width, so a port
cannot reproduce the words.  Section 4 of the spec has the table; zoom stays session only.

### Findings

- **The optional `0x2640003d` row-layout record.**  Four of the five outline controls carry it
  (4N+14 bytes, shrinking 4 bytes per hidden row).  The writer rebuilds it where present and
  never adds it.
- **The wave-33 open gap has a cause.**  The CEdl size words (`214/CEdl/VarMeta` +0x14 and the
  `214/Props` scalar tagged 0x07000100) equal the CEdl stream length, which includes the
  save-path record.  Project's null and edit saves are made under other file names than the
  control, so the size words carry path-length noise, and a difference that crosses a 256
  boundary moves VarMeta +21 and Props +11403 alone.  The outline tests normalise Project's
  size words to the control's path record; no `KnownGap` is used and `open_gap_state` in
  `tests/save_pair_noise.h` is untouched.
- **The reader does not restore view state on open.**  A persisted sort or collapse is
  written, but the plan reads back in ID order and fully expanded.
- **The Sort dialog does not offer Duration.**  A Duration sort is reachable through the edit
  model only.
- **Show Level 2 and Show All on the five-task base hide nothing.**  Project's save differs
  from its control by the marker alone.  The writer reproduces that and is tested on it; the
  edit model refuses it as nothing to persist.

### Notes on tests and process

- **Mutation checks, W35-2.1:** m1 to m8 all killed.  m4, m7 and m8 first failed to compile
  under warnings-as-errors and were rewritten and rerun; the failed attempts are not evidence.
- **Mutation checks, W35-2.3** (`build/w35/75b89c0-W35-2.3-mutations.status`): 14 of 16 killed
  (writer w1 to w8; edit model a1, a3, a5 to a8).  a2 (the level cap in `push()`) and a4 (the
  cascade's one-edit check) survive and are equivalent through the public API: a level above 2
  never hides a row on a measured shape, and `push()` refuses an exclusive combination before
  the cascade sees it.
- **A test file was overwritten, then restored.**  The Show Level app test was first written
  over `app/app_edits_outline_tests.cpp`, which holds the Indent / Outdent test.  The duplicate
  target showed at configure; the file was restored from HEAD (the backup was identical, so
  nothing was lost) and the new test lives in `app/app_edits_show_level_tests.cpp`.
- No amend and no history rewrite.  G0's and G1's commits on the same branch were left
  untouched.  Nothing is pushed.

WAVE-35-G2-COMPLETE
