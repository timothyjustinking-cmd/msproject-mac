# Wave 29 brief - rule 1 for wave 28's commands, then the last visible differences and the missing dialogs (for DeepSeek, coordinator role)

_Written 2026-10-25 by the Opus verifier session.  Baseline: branch `wave29`, cut
from `wave28` (its verification commit).  Part of the unattended run of waves 27-31
Tim approved (`plan-waves27-30.md`).  `docs/handoff/standing-rules.md` binds this
wave.  Sources: the plan, and `wave28-verification/review.md` (its four High
findings are G0; it found no Critical)._

## 0. Publication

`wave28` is not on the remote: the pre-push guard refuses the intermediate
`tests/data/synthetic-calendars.mpp` blob added in `076438f` (the operator's name;
`d1b5a8a` cleaned the tip).  Whether that commit is rewritten is Tim's decision and
the verifier's job, not this wave's.  So in CLOSE: run the guard over the push range
and record its result; attempt the push and the two dispatches only if the guard
passes; otherwise record the refusal exactly as wave 28 did and finish CLOSE.  Never
rewrite or force-push history; never work round the guard; never edit
`.git/hooks` or `build/audit-w22/`.

## 1. Rules for this wave

`standing-rules.md` plus the group rules of waves 26-28: groups in order, one fresh
session each, the marker line `WAVE-29-<group>-COMPLETE` appended to
`docs/handoff/wave29-complete.md` and committed when the group is done, deferral
only with evidence of an attempt (a commit or a Project measurement log under
`build/w29/`), Critical/High findings you discover fixed first, the rest under
"Found, not fixed".

**Rule 1, restated for G0.**  A command that writes the `.mpp` is enabled only for
the row shapes a committed Project save pair measured.  A field Project changes in
the pair that the port does not produce is a FAILING check in the save-pair test
(compare against Project's file, never against the control), unless the port
genuinely writes it.  "Project's reopen of the port's save" is acceptance only when
the reopen was actually run and its JSON is under `build/w29/`; otherwise do not
claim it in code comments or records.

**The measurement recipe** is wave 28's: a neutral copy of a synthetic fixture
(neutral Author/Manager/Company/user name, saved under `C:\w29\`), Project 2021
makes the edit through COM (`scripts/oracle_lock.py`), saves; the before/after pair
is committed as fixtures in its own commit; `scripts/check_fixtures.py` and the name
scan are clean before the commit.

## 2. Groups

### G0 - wave-28 verifier findings (High only)

From `docs/handoff/wave28-verification/review.md` (0 Critical, 4 High).  Do G0.0
first: it makes the tree honest at once; the rest then widens what is enabled, one
measured shape at a time.

- **W29-0.0  Narrow the enabled commands to the measured shapes now.**  Before any
  new measurement, make the edit model refuse (with the exact decision-9 style
  reason in the tooltip/status, and no write) every shape R28-2, R28-3 and R28-4
  list as unmeasured: a percent edit on a leaf under a summary, on a task with
  work-bearing assignments, on a milestone, or to a value other than the measured
  0/40/100 transitions unless a pair covers it; Mark on Track / Update Project on a
  row that already has progress (% > 0, ActualStart or a progress block); Update
  Project's "Selected tasks" scope (disable the radio); indent/outdent of any row
  that is not manually scheduled, and indent of a row that would become a summary
  while it carries progress, work or a pending actuals edit.  A test per refusal.
  Correct `outline.h:110` and the reopen claims in `actuals.h:58` / `outline.h:119`.
- **W29-0.1  (R28-1, High)  The save-pair tests stop masking Project's changes.**
  Rewrite the actuals and outline save-pair tests so every stream is compared with
  PROJECT's file: the owned streams field by field, and for every other stream
  either "port == Project" or a named failing check.  Then close the gaps rule 1
  needs: measure and write the Task Fixed2Meta byte, the FixedMeta `+44` flag and
  the `+8..23` blob bytes Project changes on an actuals save, and the TBkndAssn
  actual-work update; for indent/outdent the assignment tombstone (indent/summary)
  and the re-created assignment (outdent).  Whatever cannot be written this group
  stays a failing check AND its command stays disabled for that shape.  Run
  Project's COM reopen of the port's actuals, outline and new-task-mode saves and
  commit the reopen JSON summary to the record.
- **W29-0.2  (R28-2, High)  The percent writer on real schedules.**  Measure, each
  as its own Project pair: a leaf under a summary (the summary's % Complete, actual
  dates and Actual/Remaining Duration roll-up), a task with a work-bearing
  assignment (% Work Complete, task actual work, the assignment's actual work), a
  milestone at 0 -> 100, and one mid-to-mid edit (25 -> 60) for the duration
  truncation.  Implement each to match and lift its W29-0.0 refusal only when its
  pair passes field by field.
- **W29-0.3  (R28-3, High)  Mark on Track and Update Project on rows with
  progress.**  Measure Project's Mark on Track and Update Project on a row already
  at 60 % with an ActualStart, with an update date both after and before the
  recorded progress, and the "Selected tasks" scope on a two-of-three selection.
  If Project never lowers recorded progress, the port must not either.  Lift the
  W29-0.0 refusals only for measured shapes.
- **W29-0.4  (R28-4, High)  Indent and outdent on auto-scheduled rows.**  Measure
  Project's indent, summary-making indent and outdent on automatically scheduled
  rows (their +164/+174 and duration words differ from the manual pair's), and the
  outdent from a manual parent the header mentions.  Implement and lift the
  refusal per measured shape.

### G1 - PROJECT_A gaps (decision 7)

- **W29-1.1** Stored sheet column widths: 8 columns as the file stores them (the
  port shows 5 at 1920), decoded and applied.
- **W29-1.2** The yellow Highlighted row fill, as stored per row.
- **W29-1.3** Bar-label coverage: every label the stored bar styles call for.
- **W29-1.4** Timescale scroll origin and zoom as stored.
Each against the measured PROJECT_A reference values already in the repo; where a
reference is missing, measure it through COM and record it (no screenshots of
client files are committed; rule 4).

### G2 - Format dialogs (decision 14)

- **W29-2.1** Text Styles, Gridlines and Bar Styles dialogs, laid out from the
  captures and functional over the styles decoded in wave 26.  OK applies to the
  view; a save of a changed style is a write path, so it is enabled only with a
  Project pair (otherwise the dialog applies for the session and the save keeps the
  file's bytes, said in the dialog).

### G3 - Page Setup

- **W29-3.1** Page Setup, wired to the shared print layout (margins, orientation,
  header/footer, legend, scaling); print preview reflects it.

### G4 - Visual Reports, Compare Projects

- **W29-4.1** Both dialogs laid out from captures; primary actions disabled with the
  exact decision-9 tooltip.

### G5 - Chrome finish (decision 10 and 3)

- **W29-5.1** Dark-theme sheet/chart header bands and ribbon ink measured against
  Project's Dark Gray and Black themes.
- **W29-5.2** Menu bar, QAT and status-bar artwork in the wave-26 icon style.

### CLOSE

Fresh-tree gates on the final commit, logged; the name scan clean; a review pass
over `git diff wave28..wave29`; the push and dispatch as section 0 says;
`wave29-complete.md`, START-HERE, INDEX, `memory/coordinator.md`; the 16 decisions
re-scored.  Then append `WAVE-29-COMPLETE`.

## 3. Queued (not this wave unless a group touches it)

The wave-28 review's Medium/Low findings are queued in `plan-waves27-30.md`
("Queued from the wave-28 review").  Fix one here only if a group's own work already
edits that code.
