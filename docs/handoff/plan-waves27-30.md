# Plan for the next rounds - waves 27 to 30 (24 October 2026)

_Proposed by the Opus verifier session after the wave-26 assessment
(`assessment-2026-10-24.md`).  Each wave is a DeepSeek work order run in groups, a
fresh session per group, then verified by fresh-tree gates and one independent
review.  Only Critical or High findings jump the queue; everything else goes to the
wave it belongs to.  Not yet approved by Tim._

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
| G1 | **CI:** push `wave26`/`wave27`; fix whatever the first macOS and Linux runs find (`app/main.mm` has not been compiled since wave 24). |
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

## After wave 30 (not planned in detail)

Team Planner, Network Diagram, Calendar and Report views; the constraint-mso engine
case; macOS launch and notarisation; the hashed task names in
`tests/data/project_a_golden.tsv`; the remaining review Lows.

## Decisions needed from Tim before wave 27

1. Lift the publication hold so waves are pushed and CI runs.
2. Move builds out of Dropbox sync (Dropbox-ignore `build/`).
3. Approve this order, in particular: disable the % buttons in wave 27 and bring
   them back with the actuals writer in wave 28.
