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
