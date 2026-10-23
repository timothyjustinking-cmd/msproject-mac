# Wave 26 brief - parity first (for DeepSeek, coordinator role)

_Written 2026-10-24 by the Opus verifier session.  Baseline: branch `wave26`, cut
from local `wave25` = `3b4da5f` (its final commit; fresh-tree gates all green, see
`docs/handoff/wave25-verification/gates.md`).  `wave23`, `wave24`, `wave25` are not
merged; nothing is pushed.  Tim's ruling for this wave (2026-10-24): **parity work
comes first; only Critical or High findings may be put ahead of it.**  Waves 23-25
each deferred the same parity items behind a new fix list; this wave reverses that.
This file is the one document the fix session reads._

## 0. Publication hold - unchanged (see wave24-brief section 0)

No push, no history rewrite, no force-push, CI only if Tim lifts the hold; the
client-name scan must report 0 hits on every commit.  Captures of Project stay
local (`corpus/reference/...` or `build/`); commit only numbers measured from them.

## 1. How this wave is run (new rules)

1. **Groups, one fresh session each.**  The work is split into groups G0..G6 below.
   The driver starts a NEW DeepSeek session for each group and tells it which group
   it owns.  Do only your group's items, in order.
2. **Finish line per group.**  When every item in your group is DONE or DEFERRED
   (rule 4), append the exact line `WAVE-26-<group>-COMPLETE` to
   `docs/handoff/wave26-complete.md` (create it if absent, with a table row per item)
   and commit that.  Writing the file early is fine; only the marker ends a group.
3. **Small items, measured acceptance.**  Every item names the capture or the Project
   measurement it must match.  "Matches" means the gate reads the live widget or the
   rendered pixels and compares with numbers taken from Project, never with the
   port's own constants.
4. **Deferral needs an attempt.**  An item may be DEFERRED only with evidence of a
   real attempt: a commit on the branch, or a measurement/probe log under
   `build/w26/` that shows what blocked it, cited by path in the table.  A deferral
   without that evidence counts as not done, and the group is not complete.
5. **Findings you discover:** if Critical or High (corrupts or loses data on save,
   crashes on open, wrong schedule numbers, a gate that passes without testing),
   fix it first, in its own commit.  Anything lower goes into
   `wave26-complete.md` under "Found, not fixed" and you carry on with parity.
6. Everything in `wave25-brief.md` section 1 still applies: one finding id per
   commit, a fixture commit carries fixtures only, gate logs per commit under
   `build/w26/gates-<parent sha>-<item>.log` with the numbers quoted from them, no
   gate flipped to the port's output, neutral fixture metadata and paths.

## 2. Groups and items

The captures are in `corpus/reference/2026-10-23-msproject/` (clean ribbon strips,
backstage, the clean loaded Gantt) and `corpus/reference/2026-10-23-audit/` (the
backstage pages, all 11 Options pages, Task Information's six tabs, Project
Information, Change Working Time, Custom Fields, Links Between Projects, Update
Project, the WBS and Set Baseline menus, both dark themes); README in each.  The
independent ribbon reading is `docs/handoff/audit-w22/A2-fable-readback.md`.

### G0 - wave-25 verifier findings (Critical/High only)

The independent review of `wave24..wave25` (`docs/handoff/wave25-verification/`)
found 0 Critical and 1 High.  Its Mediums and Lows are queued for after this wave
and are NOT part of it.

- **W26-0.1  High  macOS print drops the last task when the project row is shown.**
  `app/main.mm` (around line 1866) builds the print request's row list as
  `0 .. plan.view.tasks.size()-1`, but those numbers are RecordSet rows; on a file
  that shows the project row (PROJECT_A, `tests/data/wave24-order.mpp`) Print and
  Print-to-PDF show the project row plus tasks 1..N-1, the last task is missing from
  the table and the chart, and the subtitle still says "N of N".  Fix it by taking
  the rows from the RecordSet (all rows, or the filter's rows) exactly as the Qt
  print path does, and move the row-list construction into the shared `app/` layer
  so the headless `app/` ctest can run it on `wave24-order.mpp` and fail under the
  old arithmetic (macOS cannot be compiled on this machine, so the shared function
  is the only place this can be tested here).  Also make the smoke paths run on a
  fixture that SHOWS the project row (the review's W25-1 acceptance gap that let this
  through: `smoke.cpp` around lines 359/374/380/619, `main.mm` around 4335).

### G1 - ribbon controls (the kinds Project shows)

- **W26-1.1** Task > Font: font-name and font-size **combos** (values: the file's
  default text style when decoded, else Project's new-project default as captured),
  B/I/U toggle buttons, highlight-colour and font-colour **split buttons**, the
  group's dialog launcher.
- **W26-1.2** Task > Schedule: the 0/25/50/75/100 % buttons (functional: set
  % Complete on the selection through the existing edit path), Mark on Track split
  button, Respect Links, Inactivate, and the five small icon buttons (outdent,
  indent, link, unlink, split); functional where the engine already supports the
  edit (outdent/indent/link/unlink), disabled with the exact tooltip otherwise.
- **W26-1.3** View > Data: Highlight, Filter, Group by **combos** with Project's
  default texts; View > Zoom: Timescale **combo**; View > Split View: Timeline and
  Details **checkboxes** with their combos.  The Timeline checkbox toggles the strip
  (the file-stored setting from wave 24 is its initial state).
- **W26-1.4** Gantt Chart Format: Critical Tasks, Slack, Late Tasks, Outline Number,
  Project Summary Task, Summary Tasks **checkboxes** (Critical Tasks drives the
  existing critical highlight; Project Summary Task toggles row 0), the four column
  align/wrap buttons, the 16-swatch Gantt Chart Style gallery with expander and
  dialog launcher.
- **W26-1.5** Project > Status: the Status Date **date field**; Resource > Level and
  every other stacked group laid out in Project's small-button stacks.
Acceptance: the ribbon pin lists each control's kind and the gate reads live widget
classes (W25-7); a 1920 render's group rectangles and stack positions are compared
with the clean strips' measured positions; `ribbon_pin_unmodelled.tsv` shrinks to
empty or to items deferred under rule 4.

### G2 - icons

- **W26-2.1 .. 2.7** One item per tab (Task, Resource, Report, Project, View, Help,
  Gantt Chart Format): an original monochrome glyph for every command on that tab,
  at Project's two sizes (32 px large, 16 px small), Office-like weight, reusing a
  glyph only where Project reuses one.
Acceptance per tab: no command shows the placeholder; a uniqueness gate hashes every
glyph on the tab and fails on an unexpected duplicate; a human-review contact sheet
(PNG of the tab's glyphs) is written to `build/w26/` for Tim.

### G3 - render what the file stores (controlled-save decoding, the flag method)

- **W26-3.1** Text styles: save variants of a neutral fixture in Project changing ONE
  text-style item at a time (milestone italic, critical colour, font family, font
  size, highlight fill) and decode the records; render them.  PROJECT_A's two named
  expected failures (W25-5) must turn green and be removed from the expected list.
- **W26-3.2** Bar styles: the same method on the `0x22400008` view blob (one bar-style
  colour/shape change per save); render the file's palette, falling back to the
  measured stock palette.  PROJECT_A must render its own summary/critical/complete
  colours as in `project-gantt-loaded-clean.png`.
- **W26-3.3** Timescale: the six `TimescaleEdit` variants in `build/w23/` plus new ones
  as needed; decode tier units and counts; render PROJECT_A's year/quarter/month
  tiers.
Acceptance: the live same-file comparison table rows for text style, bar palette and
timescale say "match".

### G4 - backstage and Options

- **W26-4.1** File tab and rail styled as the Account capture (plain "File" tab,
  light rail, back arrow), with Account under Close and Options pinned.
- **W26-4.2** Account page: the port's identity, version and About (About becomes
  mouse-reachable); theme chooser offering Project's five Office Theme names (only
  the ones the port implements enabled).
- **W26-4.3** New page: Blank Project and New from existing project, plus the Recent
  list (local files only; no online templates, disabled search box with a note).
- **W26-4.4** Open and Save As pages: Recent, This PC, Browse (no OneDrive/SharePoint;
  those entries laid out and disabled).
- **W26-4.5** Print page: copies, printer list, settings (dates, pages, orientation,
  paper) and a live preview pane using the shared print layout.
- **W26-4.6** Options: the 11-page dialog (General, Display, Schedule, Proofing, Save,
  Language, Advanced, Customize Ribbon, Quick Access Toolbar, Add-ins, Trust Center)
  laid out from the captures; General and the Schedule page's new-task mode
  functional; every other control laid out and disabled with the exact tooltip.

### G5 - Project dialogs

- **W26-5.1** Project Information (functional where the engine has the field: start,
  finish, schedule from, status date, calendar, priority; Statistics button laid out).
- **W26-5.2** Change Working Time (calendar picker, legend, month grid, Exceptions and
  Work Weeks tables from the file's calendars; Details/Options laid out).
- **W26-5.3** Custom Fields as the field-definition dialog (type, rename, formula,
  lookup, graphical indicators); per-task values move to Task Information's Custom
  Fields tab.
- **W26-5.4** Update Project, Move Project (with Project's "tasks with actual values"
  prompt), Set Baseline / Clear Baseline menu, WBS Define Code / Renumber, Links
  Between Projects - laid out from the captures, primary action functional only where
  the engine supports it.

### G6 - views and theme

- **W26-6.1** View > Task Views / Resource Views drop-downs with Project's entries
  (add the submenu axis to `app::MenuSpec` that W25-15 named as the obstacle).
- **W26-6.2** Dark theme: Dark Gray and Black darken the chrome only, applied live;
  a gate renders both and asserts the chrome palette changed and the sheet/chart
  pixels did not (tolerance 0).
- **W26-6.3** Split view: View > Details docks the Task Form below the Gantt,
  synchronised with the selection, resizable splitter.

### CLOSE - the wave record (last session)

Fresh-tree gates on the final commit with logs; the name scan clean; a separate
review pass over `git diff wave25..wave26` (findings: Critical/High fixed, the rest
listed); the live same-file comparison against Project on a copy of
`corpus/project_a.mpp`; START-HERE, INDEX, `memory/coordinator.md`; the 16 decisions
re-scored.  Then append `WAVE-26-COMPLETE`.  If the only thing left is the
publication hold, end with `BLOCKED: publication hold`.
