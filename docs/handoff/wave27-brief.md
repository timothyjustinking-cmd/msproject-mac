# Wave 27 brief - make the save safe, get CI back (for DeepSeek, coordinator role)

_Written 2026-10-24 by the Opus verifier session.  Baseline: branch `wave27`, cut
from `wave26` = `1299cc7` (pushed).  The operator approved the plan (`plan-waves27-30.md`) and
asked for waves 27-31 to run back to back.  `docs/handoff/standing-rules.md` binds
this wave; read it first.  Sources: `wave26-verification/review.md` (its two High
findings are G0), the first CI runs on `wave26`, and the queued review items._

## 0. Publication

The publication hold is LIFTED (history scrubbed 24 Oct 2026).  Push `wave27` and
dispatch CI as the standing rules say.  The local pre-push guard still refuses
Project screenshots, corpus files and client names; never work round it.  Never
rewrite or force-push history.

## 1. Rules for this wave

Everything in `standing-rules.md`, plus the wave-26 run rules (groups, one fresh
session each, the marker line `WAVE-27-<group>-COMPLETE` appended to
`docs/handoff/wave27-complete.md` and committed when the group is done; deferral only
with evidence of an attempt; Critical/High findings you discover are fixed first, the
rest listed under "Found, not fixed").

## 2. Groups

### G0 - the wave-26 Highs (save safety)

- **W27-0.1  High  % Complete saves one value of the eight Project writes.**  Project's
  own 50 % edit (`corpus/probe/base.mpp` -> `corpus/probe/var-pct.mpp`) changes the
  task row's Actual Duration (+80), Remaining Duration (+88), % Complete (+92), Actual
  Start (+120), two date words (+132/+136) and a flag (+168), the project row's
  roll-up (+80/+88/+92), and adds Var2Data field 0x0B40015E; the port changes only
  +92.  **Disable the five % buttons** (exact decision-9 tooltip: they return with the
  actuals writer in wave 28) and remove any other UI route to `set_percent`.  Keep the
  edit kind and its tests, marked as not yet enabled.
- **W27-0.2  High  % Complete is accepted on summary tasks.**  Refuse summary rows (and
  uid 0) in the edit model itself, not only in the shell, with a test.
- **W27-0.3  Medium (from the same review)  Project Priority save is unmeasured.**  Have
  Project set the project summary task's Priority to 750 on a neutral copy through COM,
  save, and diff against the port's save (standing rule 1).  Keep the save only if the
  bytes Project changes are the bytes the port changes; otherwise disable it like the
  % buttons.  Commit the Project-saved file as the fixture the test compares with.

### G1 - CI green on all three OSs

- **W27-1.1** `app_qt/ribbon_bar.cpp:535` `widest_line` is unused: an error under GCC
  and Clang (`-Werror=unused-function`), so the Linux and macOS Qt builds stop.
  Remove it or use it.
- **W27-1.2** The oracle-provenance sub-check in `scripts/check_fixtures.py` needs
  history; CI checks out one commit.  Give the checkout used by the fixtures step
  `fetch-depth: 0` in both workflows (preferred), or skip that sub-check on a shallow
  clone with an explicit printed reason.
- **W27-1.3** Push, dispatch both workflows on `wave27`, and fix whatever the first
  complete macOS and Linux runs report (`app/main.mm` has not been compiled since
  wave 24).  Repeat until both workflows are green on all three OSs or a failure needs
  the operator (then BLOCKED with the run id).

### G2 - commit the decoding evidence

- **W27-2.1** The one-change-per-save Project files behind the wave-26 text-style,
  bar-style and timescale decoders live only under `build/w26/`.  Commit them (or the
  extracted 0x22400008 records) as fixtures, neutral metadata, one test per change
  that asserts the decoder reads exactly the changed field.

### G3 - the cheap queued review items

- **W27-3.1** A % edit on a pending inserted task makes every save fail (wave-26 review
  item 9) - with the buttons disabled, make sure no path can still queue it, and fix
  the save so a refused edit cannot block later saves.
- **W27-3.2** Deleting a task discards its pending edits and undo does not restore them.
- **W27-3.3** Unlink: measure Project's Unlink Tasks on a task with both a predecessor
  and a successor (COM `UnlinkTasks`, count `TaskDependencies`) and match it.
- **W27-3.4** The new save tests in `app/app_edit_tests.cpp` (around lines 494 and 521)
  pass when the load fails; make a failed load fail the test.
- **W27-3.5** `app_qt/tests/task_order_test.cpp` pins 13 unsalted FNV-1a hashes of
  PROJECT_A display names, including the project title (invertible).  Replace with
  ID-and-structure checks against the corpus read at test time.
- **W27-3.6** Two files are wholly CRLF against `docs/build.md`'s LF rule
  (`app_qt/project_dialogs_qt.cpp`, `app_qt/tests/project_dialog_gate_test.cpp`).

### CLOSE

Fresh-tree gates on the final commit, logged; the name scan clean; a review pass over
`git diff wave26..wave27`; push and dispatch both workflows; `wave27-complete.md`,
START-HERE, INDEX, `memory/coordinator.md`; the 16 decisions re-scored.  Then append
`WAVE-27-COMPLETE`.
