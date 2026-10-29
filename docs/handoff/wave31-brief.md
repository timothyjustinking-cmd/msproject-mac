# Wave 31 brief - the View tab, and Resource Sheet, Task Usage and Resource Usage at Gantt fidelity (for DeepSeek, coordinator role)

_Written 2026-10-26 by the Opus verifier session.  Baseline: branch `wave31`, cut
from `wave30` (its verification commit).  The last wave of the unattended run of
waves 27-31 the operator approved (`plan-waves27-30.md`: wave 31 is "commands part 2", the
View tab, plus the second view family at Gantt fidelity).
`docs/handoff/standing-rules.md` binds this wave.  Sources: the plan,
`docs/ui-command-backlog.md`, and `wave30-verification/review.md` (its
Critical/High findings are G0).  The repo lives at `D:\dev\msproject-mac`; the
neutral root for Project saves is `C:\w31\`._

## 0. Publication

Push `wave31` and dispatch both CI workflows in CLOSE; fix and re-dispatch on red
(check for unused functions/constants GCC and Clang reject before the first push).
Never rewrite or force-push history; never work round the pre-push guard.

## 1. Rules for this wave

`standing-rules.md` plus the group rules of waves 26-30: groups in order, one fresh
session each, the marker line `WAVE-31-<group>-COMPLETE` appended to
`docs/handoff/wave31-complete.md` and committed when the group is done, deferral
only with evidence of an attempt (a commit or a Project measurement log under
`build/w31/`), Critical/High findings you discover fixed first, the rest under
"Found, not fixed".

Rule 1 as waves 29-30 applied it: a save path is enabled only for the shape a
Project pair measured; the edit model refuses every other shape up front, with a
test each; save-pair tests compare with Project's file through
`tests/save_pair_noise.h`.  Most of this wave is view state.  **A view change that
Project persists in the file** (sort order, applied table, outline level shown,
zoom, window arrangement) is a write: either measure it and write it, or apply it
for the session only and keep the file's bytes, and say which in the record.

## 2. Groups

### G0 - wave-30 verifier findings (Critical/High only)

From `docs/handoff/wave30-verification/review.md` (0 Critical, 4 High).  Refuse
first (in the edit model, with a test each), then measure only if cheap.

- **W31-0.1  (R30-1, High)  A link added after a queued automatic outline edit.**
  The linked-file refusal runs only when the outline edit is queued; adding a link
  afterwards (Task > Predecessors, Task Information, the sheet) is not checked by
  `validate_link_add` or `push(add_link)`, so the save writes a link plus the
  unlinked slack/late dates.  Refuse a link add while an automatic outline edit is
  pending (and the reverse order stays refused), with a test for each entry point.
- **W31-0.2  (R30-2, High)  The automatic outline save on files unlike the pairs.**
  It rewrites slack and late dates on every live automatic row but accepts files
  with completed or progressed rows, deadlines or constraints anywhere; all three
  pairs are the three-row base at 0 %.  Refuse those files (file-wide check), or
  measure one more pair with a progressed row and a constraint elsewhere.
- **W31-0.3  (R30-3, High; the wave-30 record's C5)  Percent/date edits with
  progress off the edited chain.**  The project row's Resume/Stop then come from the
  chain or fall back to the project start, which no pair measured; the wave's own
  test (e) saves this shape without comparing it with Project.  Refuse it, or
  measure that pair and compare test (e) with Project's file.
- **W31-0.4  (R30-4, High)  Fill Down is an unmeasured write.**  Its Start writer's
  only Project evidence is a corpus probe (`docs/analysis/oracle/manual-start-edit.json`),
  not a committed synthetic Project pair compared through `tests/save_pair_noise.h`,
  and it also fills summary rows.  Measure Project's Fill Down of Start over two
  leaf rows on a synthetic fixture (pair + null save committed), compare through
  the noise mask, refuse summary rows and any shape the pair does not cover; until
  then Fill Down is disabled with the exact tooltip.

### G1 - View tab: data commands

Sort (by the commonest keys: ID, Start, Finish, Priority, Name; the Sort dialog),
Outline (show level 1..9, all subtasks), Tables (switch between the stored tables;
the table list comes from the file).  Each one either measured as a persisted
write or session-only, as section 1 says.  Filter, Clear Filter, Collapse/Expand
All are already implemented; make Group by and Highlight work the same way if the
backlog lists them disabled.

### G2 - View tab: zoom and window commands

Zoom (the Zoom dialog), Entire Project and Selected Tasks (timescale fit, measured
against Project's resulting timescale on two fixtures), New Window, Switch Windows,
Arrange All and Hide (within the port's single-document shell: implement what the
shell can honestly do; the rest stays disabled with the exact tooltip).  Macros
stays disabled with the exact tooltip (no VBA).

### G3 - Resource Sheet at Gantt fidelity

The Resource Sheet already opens; bring it to the Gantt view's fidelity: its stored
table and column widths, the stored row heights and fonts, overallocation
highlighting (red) as stored/computed by Project, the indicators column, and a live
Qt gate measured against Project's own rendering facts (COM: column widths, visible
fields, the resource rows' values) on a synthetic fixture.

### G4 - Task Usage and Resource Usage at Gantt fidelity

Both views already open; bring the time-phased grid to fidelity: the stored
details shown (Work by default; Actual Work, Cost when the file says so), the
timescale tiers and period widths as stored, the assignment rows under each task
or resource, the totals, and the work values per period measured against Project's
own time-phased values (COM `TimeScaleData`) on a synthetic fixture with a
resource-loaded task.

### G5 - Network Diagram, Calendar and Other Views (decide, do not build)

These views are out of this run's scope.  Confirm each stays disabled with the
exact tooltip and the backlog says why; nothing else.

### CLOSE

Fresh-tree gates on the final commit, logged; the name scan clean; a review pass
over `git diff wave30..wave31`; push and dispatch as section 0 says;
`wave31-complete.md`, START-HERE, INDEX, `memory/coordinator.md`, the regenerated
`docs/ui-command-backlog.md`; the 16 decisions re-scored.  Then append
`WAVE-31-COMPLETE`.  This is the last wave of the run: the record's final section
lists what remains (the measured-and-deferred commands from wave 30 and the shared
writers they need).

## 3. Queued

The wave-30 review's Medium/Low findings are queued in `plan-waves27-30.md`
("Queued from the wave-30 review").
