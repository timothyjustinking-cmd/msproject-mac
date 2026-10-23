# Local reference set — MS Project 2021 captures (2026-10-23)

**Location:** `corpus/reference/2026-10-23-msproject/` — **git-ignored**
(`.gitignore` line 27: `corpus/`).  Nothing in it may be committed, and no client
name or project content from it may be quoted in a commit or a document.  It is
third-party UI (MS Project Professional 2021) plus screenshots of our own build,
kept locally for measurement only.

`README.md` and `manifest.json` in that directory list every file with its size,
dimensions and SHA-256.  The set is **28 files: 22 of MS Project 2021 and 6 of
the packaged Windows build** (CI run 39898703443) -- corrected in wave 23 from
the stale 27/21 count (audit A-11).

## What it covers

| family | files | use |
|---|---|---|
| clean ribbon strips (near-full width) | `project-ribbon-{resource,report,project,view,help,gantt-format}.png` | per-tab group/button inventory (decision 1); the Resource file is a full 1910x986 window, the others are ribbon-height strips |
| backstage | `project-backstage-{home,home-alt,home-alt2,account}.png` | the rail, the top-left back arrow, the Account page, Options pinned (decision 2) |
| Gantt + tables | `project-gantt-{task-tab,task-tab-alt,format-tab,format-tab-alt,row-menu}.png` | the real file's stored table, bar palette, row menu, Format tab (decisions 6/7/9/10) |
| clean loaded capture | `project-gantt-loaded-clean.png` | **1910x1027 PNG** of the real file in MS Project, no dialogs: stored table, selection/tab colours, bar colours, status bar, no menu bar (the primary measurement file) |
| dialogs | `project-task-information-{general,advanced}.png` | the six-tab Task Information target (decision 14/15) |
| views | `project-task-usage.png`, `project-team-planner.png`, `project-resource-sheet.png`, `project-report-critical-tasks.png` | the other view families and a Report Design page (decisions 1/14) |
| our build | `our-build-gantt.png`, `our-build-desktop-{1..5}.png` | the same file in the packaged build, 1914x1008, with its menus open |

The clean captures are the reliable ones for measurement.  The Gantt/Task
Information/backstage files are desktop photos (3840x1080 with the app window on
the right); they are the only same-file evidence for bar colours and the custom
stored table, so their colours are marked **provisional** where a clean capture
does not exist.

## What the set establishes (measured, not inferred)

* **Selection green is `(162, 211, 160)` / `#A2D3A0`** — sampled from the clean
  Resource-tab capture's active Indicators header and cell, and again from the
  clean loaded capture's Task Name header.  Our shell paints `#DCE9F5` (light
  blue).  Decision 10's "selection" is therefore not matching.
* **Critical bars are `#FF0000`** (pure red) in the clean loaded capture.  Our
  `kBarCritical` is `#C0392B`; that is a design value, not Project's.
* **The clean loaded capture's other bar colours are file-specific**: green
  `#00B050` on the non-critical (and 100 %-complete) leaf rows, navy `#000080`
  on a non-critical summary, red on the critical summaries/milestones.  Those
  come from the file's Gantt bar styles, not from a universal default; do NOT
  hard-code them as global constants until bar styles are decoded.  Only the
  selection and critical colours are safe to adopt now.
* **The real file's stored table** is Task Name, Dur, Start, Finish, %Com, Pre,
  Suc, SUMMARY, Yes (nine columns), with a narrow row-number gutter to its left,
  no Indicators column, and the Task Name header/selected column in green.  The
  1910x1027 capture is close enough to the 1920x1032 reference to compare
  absolute widths.
* **Its status bar** is "Ready" + "New Tasks : Auto Scheduled" (with the
  change-default callout); no classic menu bar; QAT in the title bar.
* **No classic menu bar.**  Every MS Project capture starts at the title bar (with
  the QAT in it) and then the ribbon tab row.  Operator decision: hide ours.
* **Status bar in a clean capture:** "Ready" at the left, "New Tasks : Manually
  Scheduled" beside it, nothing else.  Our loaded-file left segment carries
  diagnostics (see the wave-21 brief).
* **Backstage:** a circular back arrow at the very top-left, above the rail; the
  rail is about 120 px wide and ends with Account then Options pinned at the
  bottom.  The spec's 11 entries with Options pinned do not include Account; the
  arrow is the operator's requirement (defect in our build).
* **View tab inventory (Project):** Task Views (Gantt Chart, Task Usage, Network
  Diagram, Calendar, Other Views), Resource Views (Team Planner, Resource Usage,
  Resource Sheet, Other Views), Data (Sort, Outline, Tables, Filter, Group by,
  Highlight), Zoom (Timescale + Entire Project/Selected Tasks), **Split View
  (Timeline checkbox, Details checkbox with a drop-down)**, Window (New Window,
  Arrange All, Hide), Macros.
* **Resource tab inventory (Project):** View (Team Planner), Assignments (Assign
  Resources, Resource Pool), **Insert (Add Resources)**, Properties
  (Information, Notes, Details), Level (Level Selection, Level Resource, Level
  All, Leveling Options, Clear Leveling, Next Overallocation).  Add Resources
  is the Insert group, not Assignments (wave-23 correction, audit A-5).
* **The real file's stored table** is Task Name, Duration, Start, Finish,
  % Complete, Pre, Succ, Summary, Yes — not Entry.  Its bars: critical red,
  summary/milestone near-black, normal blue.  Its status bar says
  "New Tasks : Auto Scheduled".
* **Task Information** has six tabs (General, Predecessors, Resources, Advanced,
  Notes, Custom Fields); General carries Name, Duration + Estimated, Percent
  complete, Priority, Schedule mode (Manually/Auto), Start, Finish and the
  Display-on-Timeline Hide Bar / Rollup checkboxes; Advanced carries Constraint
  type/date, Task type, Calendar, WBS, Effort-driven, Earned-value method,
  "Mark task as milestone", "Scheduling ignores resource calendars".

## Corrections to the wave-20 scoreboard

1. **Decision 1 is PARTIAL, not DONE.**  Our seven tabs are populated, but the
   group/button inventories are not Project's.  Examples measured here: our
   Resource tab has 3 groups (View with Resource Sheet/Usage, Insert, Properties)
   against Project's 5 (Team Planner; Assignments with 3 commands; Insert;
   Properties with Details; Level with 6 commands); our View tab has Task Views,
   Zoom and Data only, against Project's Task Views, Resource Views, Data,
   Zoom, **Split View**, Window and Macros.  The wave-20 gate only asserted our
   own spec's counts.
2. **Decision 16's target is confirmed**: Project's Split View group carries the
   Timeline and Details checkboxes; our View tab has neither.
3. **Decision 10's selection colour is measured**: `#A2D3A0`.
4. **Decision 7's target for this file** is the nine stored columns above.

## Using the set

* Open files directly from this directory; they are local and ignored.
* Do not copy them into `tests/`, `docs/`, or any committed path.
* Do not paste client names, task names, dates or staff names from them into
  commits, docs, test data or prompts.
* For a new capture, ask for a **PNG at the client size** with one cell selected
  and no dialogs; a phone photo is a last resort and its colours stay
  provisional.
