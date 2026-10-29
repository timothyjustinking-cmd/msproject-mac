# Standing rules for every wave (from wave 27)

_Adopted 24 October 2026 on the operator's approval of the workflow review
(`assessment-2026-10-24.md` section 6).  Every wave brief inherits these; a brief may
add rules but not relax one.  The driver (`tools/deepseek/run-wave.ps1`) tells each
session to read this file._

## 1. Save paths are measured against Project

No edit that changes the saved `.mpp` is enabled until the same edit has been made in
MS Project 2021 through COM (`scripts/oracle_lock.py`, on a copy under a neutral
path), saved, and its streams diffed against the port's save of the same edit.  The
Project-saved file (or the extracted streams) is committed as a fixture and the test
compares the port's bytes with it, field by field.  A test that re-reads the port's
save through the port's own reader is a round-trip test, not evidence; it never
enables a save path on its own.  Until measured, the command is disabled with the
exact decision-9 tooltip.

## 2. Keep the working context small

- Read logs and large files with `tail`, `grep` or `sed -n`, never whole; quote only
  the lines you need.
- Do not paste build output into the conversation: redirect it to a log and read the
  summary lines.
- The launcher compacts at about 200k tokens; plan work so a group fits in one session.

## 3. Gate cadence

- **Per commit:** incremental builds of the affected trees and the tests those trees
  own, plus the client-name scan and `scripts/check_fixtures.py`.  The commit message
  quotes the numbers from a log named for the parent commit and the item.
- **Per group:** the full fresh-tree set (core `ctest -E plan_recalc`, `plan_recalc_tests`
  with `MPP_NO_CORPUS=1`, M2 332/332, the engine suite lane, app and app_qt ctest,
  fixtures), logged and quoted in the group's completion row.
- Builds stay under `build/`, which is excluded from Dropbox sync; never build into a
  synced folder.

## 4. CI every wave

Push the wave branch at the end of the CLOSE group and dispatch both workflows
(`gh workflow run qt.yml --ref waveNN` and `gh workflow run ci.yml --ref waveNN`); a
red run is triaged, fixed and dispatched once more with the reason recorded.  The
local pre-push guard refuses Project screenshots, corpus files and client names; never
work round it.

## 5. One independent review per wave

After CLOSE, the verifier session runs fresh-tree gates and one independent review of
the wave's diff.  Only Critical or High findings go to the front of the next wave;
everything else is queued in the plan.  The watcher reports only BLOCKED, STALLED and
done.

## 6. Commits

One finding id per commit; a fixture commit carries fixtures only; no amending or
rewriting of commits; LF line endings; no client task names, project titles, people's
names or company paths anywhere - refer to rows by ID.
