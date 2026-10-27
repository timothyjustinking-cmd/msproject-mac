# Plan for the next rounds - waves 27 to 30 (24 October 2026)

_Proposed by the Opus verifier session after the wave-26 assessment
(`assessment-2026-10-24.md`).  Each wave is a DeepSeek work order run in groups, a
fresh session per group, then verified by fresh-tree gates and one independent
review.  Only Critical or High findings jump the queue; everything else goes to the
wave it belongs to.  The workflow changes below were approved and implemented by
Tim's instruction on 24 October 2026 (`standing-rules.md`); the wave order still
awaits his go-ahead._

## How every wave runs (changes from wave 26)

- **Write-path rule.** No save path is enabled until the same edit has been made in
  Project through COM, saved, and its streams diffed against the port's save.  The
  diff is committed as the test's expectation.
- **Lighter sessions.** DeepSeek's auto-compact window drops from 786k to about 200k
  tokens; logs are read with `tail`/`grep`, never whole.
- **Gates.** Incremental gates per commit; the full fresh-tree set once per group,
  with the log named for the exact commit.
- **CI every wave** once the publication hold is lifted: push the wave branch, run
  both workflows on all three OSs.
- **Watching.** The watcher wakes only on BLOCKED, STALLED or done.
- **Builds outside Dropbox sync** (Tim's decision).

## Wave 27 - make the save safe, get CI back (small; then merge)

| group | items |
|---|---|
| G0 | **% Complete:** refuse summary tasks; disable the five buttons until wave 28's actuals writer lands (tooltip says why).  **Priority:** measure the project-row Priority save against Project; keep it only if the bytes match. |
| G1 | **CI:** the first runs on `wave26` (24 Oct 2026, qt-shell 40043956720, ci 40043959284) failed on two things to fix first: (a) `app_qt/ribbon_bar.cpp:535` `widest_line` is unused - an error under GCC and Clang, so the Linux and macOS builds stop there; (b) the oracle-provenance check needs history, but CI checks out one commit - give the fixtures step `fetch-depth: 0` (or skip that sub-check on a shallow clone with an explicit message).  Then dispatch both workflows again and fix whatever the first complete macOS and Linux runs find (`app/main.mm` has not been compiled since wave 24). |
| G2 | **Evidence:** commit the one-change-per-save Project files behind the text-style, bar-style and timescale decoders, with a test per change. |
| G3 | **Queued review fixes, the cheap ones:** the unsaved % edit on a pending inserted task, the delete-discards-pending-edit, Unlink both directions (measure first), the silent `if (load ok)` test guards. |
| CLOSE | gates, review, live check |

Exit: all gates green on all three OSs.  **Then merge `wave26`+`wave27` into
`main`.**  Estimate: 3-4 hours.

## Wave 28 - the editing core (medium)

| group | items |
|---|---|
| G1 | **Actuals writer:** % Complete, Actual Start/Finish, Actual and Remaining Duration and the summary/project roll-up, written as Project writes them.  Re-enables the % buttons, Mark on Track and Update Project. |
| G2 | **Outline editing:** indent and outdent (level, parent, summary roll-up), measured against Project. |
| G3 | **New-task mode:** written to the file and used by Insert Task; Options > Schedule takes effect. |
| G4 | **Task Form editable** through the same save paths as the sheet. |
| G5 | **Split task**, if the measurement shows it is tractable; otherwise deferred with the measurement as evidence. |

Estimate: 6-8 hours.

## Wave 29 - the last visible differences, the missing dialogs (medium)

| group | items |
|---|---|
| G1 | **PROJECT_A gaps:** stored sheet column widths (8 columns, not 5), the yellow Highlighted row fill, bar-label coverage, timescale scroll origin and zoom. |
| G2 | **Format dialogs:** Text Styles, Gridlines, Bar Styles - functional over the styles decoded in wave 26. |
| G3 | **Page Setup**, wired to the shared print layout. |
| G4 | **Visual Reports, Compare Projects:** laid out from captures, primary action disabled. |
| G5 | **Chrome finish:** dark-theme header bands and ribbon ink; menu, QAT and status-bar artwork in the wave-26 icon style. |

Estimate: 6-8 hours.

## Wave 30 - make the commands work (large; may span two waves)

| group | items |
|---|---|
| G1-G4 | The 83 disabled ribbon commands, highest use first: Task tab (Move, Inspect, Deliverable), Resource tab (Assign Resources, Resource Pool, levelling), Project tab (baselines, WBS, Calculate), View tab (Sort, Outline, Filter, Group by, Highlight).  Each group lists its commands with a Project measurement for any that write. |
| G5 | Second view family at Gantt fidelity: Resource Sheet, Task Usage, Resource Usage. |

Estimate: 8-12 hours, likely split into waves 30 and 31.

## Queued from the wave-28 review (Medium/Low; `wave28-verification/review.md`)

- R28-5 Medium: `scripts/check_fixtures.py`'s user-profile pattern is built without
  `re.escape`, so the UTF-16 `\Users\` / `\Documents and Settings\` check can never
  fire (drive-path detection works).  Fix with a falsification test.
- R28-6 Medium: a percent edit plus a Start edit in one save is unmeasured; a
  percent edit on a task with its own calendar is accepted, shown, then dropped at
  save as a warning (refuse it up front).
- R28-7 Medium: `076438f` keeps the operator's name in local history; the push is
  blocked until Tim rules (verifier's job, not a wave item).
- R28-8 Low: the Task Form offers Name, Duration and Finish as editable but never
  saves them; a typed value can be dropped silently when the selection moves.
- R28-9 Low: new tests print SKIP and pass when a fixture is missing.
- R28-10 Low: two claims worded beyond what the tests show.
- R28-11 Low: `tests/data/README.md` lacks hash rows for 12 new fixtures.
- R28-12 Low: one commit's gate log skipped the core tests.
- Plus the wave-28 record's own "Found, not fixed" list (F4-F7, A1-A9 and the
  carried residuals).

## Queued from the wave-29 review (Medium/Low; `wave29-verification/review.md`)

- R29-8 Medium: refusals the edit model accepts but the writer raises at save fail
  the whole save, and every later Save until undo (move them up front).
- R29-9 Medium: several W29-0.0 refusals have no test.
- R29-10 Medium: the new-task-mode pair test still compares with the control.
- R29-11 Medium: R28-5 still open - the user-profile check in
  `scripts/check_fixtures.py` cannot fire on backslash paths.
- The eight Lows in the review.

## Queued from the wave-30 review (Medium/Low; `wave30-verification/review.md`)

- R30-5 Medium: the task-mode edit should be alone in its save; a queued view-state
  or formula edit is not refused, and a Start edit or Fill on another row fails only
  at save.
- R30-6 Medium: outdent's parent-assignment shape is refused only by the writer (a
  warning), so the edit silently drops from the saved file (W30-C5 fixed indent only).
- R30-7 Medium: the resource-loaded rule does not check the project or resource
  calendar or the contour; the work-not-9600 refusal has no test.
- R30-8 Medium (unverified): the task-mode shape does not check constraint,
  deadline, duration format or task type.
- R30-9..R30-14 Low: SKIP-and-pass tests on a missing fixture; 33 of 66 new
  fixtures without a pinned hash in `check_fixtures.py`; six doc lines naming the
  operator by first name; commit-message counts contradicted by logs; the outline
  progress check sees only percent and work; the R29-11 UTF-16 profile regex.

## Queued from the wave-31 review (Medium/Low; `wave31-verification/review.md`)

- R31-4 Medium: Indent, Outdent and Insert ignore the session sort (folded into
  wave 32's W32-0.3 as a refusal).
- R31-5 Medium: the automatic-outline file check does not pin task calendars,
  inactive rows, elapsed or estimated durations, or manual rows outside the edit.
- R31-6 Medium (performance): `usage_grid.cpp` `column_work` scans columns
  linearly per day bucket; walk both sorted lists together.
- R31-7..R31-12 Low: overallocation is a port-computed approximation; the Resource
  Sheet gate's COM facts are not a committed JSON; four SKIP-and-pass tests on
  missing fixtures; commit-message counts not in the cited logs; one brief line
  naming the operator; the outside-progress test sees only percent, and the usage
  gate accepts a longer or absent series.

## Queued from the wave-32 review (Medium/Low; `wave32-verification/review.md`)

- R32-1 Medium: Task Information queues custom-field edits before the percent push,
  so a refused percent leaves the field edits queued with only the refusal shown.
- R32-2 Medium: the macOS shell's filter still does range select, copy, multi-row
  paste and Indent/Outdent in file order (no saved field is reachable today).
- R32-3..R32-7 Low: `Document::save`/`save_start_edits` still write a Start if handed
  one; a one-row paste into a pre-sort range lands on the first file row; Indent,
  Outdent and Insert under a filter; three weak tests (paste-refusal column, a filter
  that hides nothing, no ID-ascending case); `sort_reorders_rows()` re-sorts on every
  menu update.

## After wave 30 (not planned in detail)

Team Planner, Network Diagram, Calendar and Report views; the constraint-mso engine
case; macOS launch and notarisation; the hashed task names in
`tests/data/project_a_golden.tsv`; the remaining review Lows.

## Decisions needed from Tim before wave 27

1. ~~Lift the publication hold so waves are pushed and CI runs.~~ Done 24 Oct 2026.
2. ~~Move builds out of Dropbox sync (Dropbox-ignore `build/`).~~ Done 24 Oct 2026.
3. Approve this order, in particular: disable the % buttons in wave 27 and bring
   them back with the actuals writer in wave 28.
