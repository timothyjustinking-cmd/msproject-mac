# Wave 30 brief - the save-path findings, then the commands of the Task, Resource and Project tabs (for DeepSeek, coordinator role)

_Written 2026-10-26 by the Opus verifier session.  Baseline: branch `wave30`, cut
from `wave29` (its verification commit).  Part of the unattended run of waves 27-31
the operator approved (`plan-waves27-30.md`; wave 30 is "commands part 1": the Task,
Resource and Project tabs; wave 31 takes the View tab and the second view family).
`docs/handoff/standing-rules.md` binds this wave.  Sources: the plan,
`docs/ui-command-backlog.md`, and `wave29-verification/review.md` (1 Critical and 6
High - they are G0)._

**The project has moved.**  The repo now lives at `D:\dev\msproject-mac` (out of
Dropbox, 26 Oct 2026).  Every path you write, log or quote uses that root; nothing
may embed a local path in a fixture (the neutral root for Project saves is
`C:\w30\`).

## 0. Publication

Push `wave30` and dispatch both CI workflows in CLOSE, as the standing rules say;
fix and re-dispatch on red (wave 29 needed two dispatches: GCC and Clang reject
unused functions and constants that MSVC accepts - check for those before the first
push).  Never rewrite or force-push history; never work round the pre-push guard.

## 1. Rules for this wave

`standing-rules.md` plus the group rules of waves 26-29: groups in order, one fresh
session each, the marker line `WAVE-30-<group>-COMPLETE` appended to
`docs/handoff/wave30-complete.md` and committed when the group is done, deferral
only with evidence of an attempt (a commit or a Project measurement log under
`build/w30/`), Critical/High findings you discover fixed first, the rest under
"Found, not fixed".

**Rule 1 as wave 29 applied it, and its lesson.**  A save path is enabled only for
the shape a Project pair measured, and the edit model must refuse every other shape
**up front** (not the writer at save time).  Wave 29's review found the refusals
were narrower than the measurements in six places: the measured fixtures had no
links, one assignment that was Project's placeholder, one progressed leaf, one
duration.  So for every pair you add, write down the fixture's conditions (links,
assignments and their kind, progress anywhere in the file, calendars, durations)
and make the edit model refuse any edit whose file or rows differ in one of them,
with a test per refusal.  The save-pair test compares with Project's file through
`tests/save_pair_noise.h` (null-save mask), never with the control.

**The measurement recipe** is wave 29's (neutral copy under `C:\w30\`, COM via
`scripts/oracle_lock.py`, the pair and its null save committed as fixtures in their
own commit, `scripts/check_fixtures.py` and the name scan clean first).

## 2. Groups

### G0 - wave-29 verifier findings (Critical/High only)

From `docs/handoff/wave29-verification/review.md`.  Do W30-0.0 first.

- **W30-0.0  Refuse first.**  Before any new measurement, make the edit model refuse
  (reason in the status line, no write, a test each) every shape named below that no
  pair measured.  Fail-at-save refusals that the edit model accepted (R29-8) are
  moved up front at the same time for the commands this G0 touches.
- **W30-0.1  (R29-1, Critical)  Indent deletes a real assignment.**  The indent
  writer tombstones the new summary's single assignment
  (`core/mppwrite/outline.cpp:1478-1495`, `:1640`), but the edit model refuses only
  resource assignments with work > 0 (`app/edit_model.cpp:138-146, 267-299`).  A
  real zero-work assignment (a cost or material resource, or a work resource at
  zero work) is lost on save.  Refuse an indent unless the assignment is exactly
  Project's placeholder (resource uid 0 / the unassigned resource - check how the
  fixtures mark it); then, if worth it, measure Project's indent of a task with a
  real zero-work assignment and implement that shape.
- **W30-0.2  (R29-2, High)  Automatic indent/outdent on linked files.**  The
  slack/late-date rewrite (`app/edit_model.cpp:312-405`) only holds without links;
  every outline pair has none.  Refuse when the file has any link, or measure a
  linked pair and implement the CPM backward pass the bytes show.
- **W30-0.3  (R29-3, High)  Outline moves over progress.**  Refuse indent/outdent
  when the moved row or subtree has progress, resource work or a queued actuals
  edit (the outline writer has no progress roll-up), or measure and implement it.
- **W30-0.4  (R29-4, High)  The 0 -> 0 date update.**  Mark on Track / Update
  Project through a date before a 0 % row's start writes the 100 -> 0 reset words
  (`core/mppwrite/actuals.cpp:1157, 1287-1330, 1396`).  Make it a no-op for that row
  (nothing to write), or measure Project's save of it.
- **W30-0.5  (R29-5, High)  Summary recompute scope.**  Every actuals save
  recomputes every summary with a progressed leaf, not only the edited row's
  ancestors (`actuals.cpp:643-822`); its Resume/Stop rule was measured with one
  progressed leaf.  Restrict the recompute to the edited rows' ancestors and refuse
  when any other summary is affected, or measure a two-progressed-leaf pair.
- **W30-0.6  (R29-6, High)  The resource-loaded contour rule.**  One record per
  4800 ticks comes from one data point (`actuals.cpp:1608-1621`).  Refuse other
  durations, units and calendars, or measure two more points (a different duration
  and 50 % units) and generalise only what both show.
- **W30-0.7  (R29-7, High)  Add Link's "later link = 0x07" byte.**  Wave 29's own
  measurement falsified the rule `core/mppwrite/add_delete.cpp:1832-1846` still
  writes (its record's C3).  Write what the measurement shows, with a pair test, or
  refuse the shape that the rule covers.

### G1 - Task tab commands

The disabled Task-tab commands in `docs/ui-command-backlog.md`, highest use first:
Manually Schedule / Auto Schedule / Mode (task mode write - measure); Insert
Summary and Insert Milestone (writes - measure; reuse Insert Task's measured
writer); Inactivate (write - measure); Respect Links (reschedule - measure or keep
disabled); Move (date moves - measure the commonest two options); Notes and Details
(Task Information's notes tab / the Task Form details - notes are a write, measure);
Add to Timeline (a view-state write - measure); Scroll to Task (view only); Fill
(the selected cells - only for columns whose write is already measured); Font B/I/U
and colours, Format Painter (text-style writes - measure one, or keep disabled);
Inspect (read-only pane from the scheduler's facts); Deliverable (Project Online -
stays disabled with the exact tooltip).  Each writing command lists its pair in the
record; a command not measured stays disabled with the exact tooltip.

### G2 - Resource tab commands

Add Resources (work resource row - measure), Information and Notes (resource
writes - measure), Details (Resource Form - view), Assign Resources (assignment
create/remove - measure one assign and one remove; this is the biggest item),
Level All / Level Selection / Level Resource / Clear Leveling / Leveling Options /
Next Overallocation (levelling: measure Level All on a small overallocated fixture
and decide from the bytes, as wave 28 did for Split; Next Overallocation is
read-only and can come first), Resource Pool (shared pools - stays disabled), Team
Planner (a view - wave 31 or later, stays disabled).

### G3 - Project tab commands

Calculate Project (recalculate and save - the engine exists; measure Project's
save after F9 on a file with a pending change), Set Baseline (baseline fields -
measure Set Baseline 0 for the entire project), WBS (code definition - measure, or
keep disabled with evidence), Spelling (read-only check over task names; no
dictionary data from Project), Subproject (stays disabled: inserted projects are
out of scope), Get Add-ins / My Add-ins (online - stay disabled with the exact
tooltip).

### CLOSE

Fresh-tree gates on the final commit, logged; the name scan clean; a review pass
over `git diff wave29..wave30`; push and dispatch as section 0 says;
`wave30-complete.md`, START-HERE, INDEX, `memory/coordinator.md`, the regenerated
`docs/ui-command-backlog.md`; the 16 decisions re-scored.  Then append
`WAVE-30-COMPLETE`.

## 3. Queued

The wave-29 review's Medium/Low findings are queued in `plan-waves27-30.md`
("Queued from the wave-29 review").  Fix one here only if a group's own work
already edits that code.
