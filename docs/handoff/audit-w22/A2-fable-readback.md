# A2 - independent re-read of the ribbon strips (Fable, 2026-10-23)

Read directly from corpus/reference/2026-10-23-msproject/*.png BEFORE seeing the
gate's pinned list.  [v] = dropdown arrow, [L] = large button, [s] = small
icon-only button, [cb] = checkbox, [combo] = combo/field, [DL] = dialog launcher.
No client content copied.

## Resource (project-ribbon-resource.png, full window 1910x986)
View: Team Planner [L][v]
Assignments: Assign Resources [L], Resource Pool [L][v]
Insert: Add Resources [L][v]
Properties: Information [L], Notes [L], Details [L]
Level: Level Selection [L], Level Resource [L], Level All [L], Leveling Options, Clear Leveling, Next Overallocation
=> 5 groups / 13 labelled buttons.  Note ui-reference-set.md says Assignments has
3 commands incl. Add Resources: WRONG, Add Resources is the Insert group.
Status bar in this capture: "Ready" + "New Tasks : Manually Scheduled" (blank Project1).
Title/QAT row is NOT in this capture (starts at the tab row).

## Report (project-ribbon-report.png 1912x316)
Project: Compare Projects [L]
View Reports: New Report [L][v], Dashboards [L][v], Resources [L][v], Costs [L][v], In Progress [L][v], Getting Started [L][v], Custom [L][v], Recent [L][v]
Export: Visual Reports [L]
=> 3 / 10.

## Project (project-ribbon-project.png 1914x408)
Insert: Subproject [L]
Add-ins: Get Add-ins, My Add-ins [v]   (two small rows; group is 2 commands)
Properties: Project Information [L], Custom Fields [L], Links Between Projects [L], WBS [L][v], Change Working Time [L]
Schedule: Calculate Project [L], Set Baseline [L][v], Move Project [L]
Status: "Status Date:" [combo/field showing NA with calendar icon], Update Project
Proofing: Spelling [L]
=> 6 / 14 (Status Date counted as one control).

## View (project-ribbon-view.png 1911x402)
Task Views: Gantt Chart [L][v], Task Usage [L][v], Network Diagram [v], Calendar [v], Other Views [v]
Resource Views: Team Planner [L][v], Resource Usage [v], Resource Sheet [v], Other Views [v]
Data: Sort [L][v], Outline [L][v], Tables [L][v], Highlight: [combo "[No Highlight]"], Filter: [combo "[No Filter]"], Group by: [combo "[No Group]"]
  (order top-to-bottom in the stack: Highlight, Filter, Group by)
Zoom: Timescale: [combo "Days"], Zoom [L][v], Entire Project [L], Selected Tasks [L]
Split View: [cb checked] Timeline + [combo "Timeline"], [cb unchecked] Details + [combo, disabled]
Window: New Window [L], Switch Windows [v], Arrange All, Hide [v]   (order: New Window; then stack Switch Windows / Arrange All / Hide)
Macros: Macros [L][v]
=> 7 / 26 counting combos as controls.

## Help (project-ribbon-help.png 1900x474)
Help: Help [L], Feedback [L], Show Training [L]
=> 1 / 3.

## Gantt Chart Format (project-ribbon-gantt-format.png 1908x402)
Format: Text Styles [L], Gridlines [L][v], Layout [L]
Columns: Insert Column [L], [s] align left, [s] align centre, [s] align right, [s] wrap text, Column Settings [v], Custom Fields
Bar Styles: Format [L][v], [cb] Critical Tasks, [cb] Slack, [cb] Late Tasks, Task Path [L][v], Baseline [L][v], Slippage [L][v]
Gantt Chart Style: 16 colour swatches (gallery) + gallery expander [v] + [DL] dialog launcher at the group's bottom-right
Show/Hide: [cb] Outline Number, [cb] Project Summary Task, [cb checked] Summary Tasks
Drawings: Drawing [L][v]
=> 6 groups.  Labelled buttons: 3 + 3 + 7 + 0 + 3 + 1 = 17; PLUS 4 icon-only
alignment/wrap buttons in Columns and the Gantt Chart Style dialog launcher,
which a "17" count omits.

## Task (from project-gantt-loaded-clean.png, 1910x1027, real file loaded)
View: Gantt Chart [L][v]
Clipboard: Paste [L][v], Cut, Copy [v], Format Painter
Font: [combo "Arial"], [combo "8"], B, I, U, highlight colour [v], font colour [v], [DL]
Schedule: [s] 0%, 25%, 50%, 75%, 100%; Mark on Track [v], Respect Links, Inactivate; [s] outdent, [s] indent, [s] link tasks, [s] unlink tasks, [s] split task (icons only, second row)
Tasks: Manually Schedule [L], Auto Schedule [L], Inspect [L][v], Move [L][v], Mode [L][v]
Insert: Task [L][v], Summary [L], Milestone [L], Deliverable [L][v] (greyed)
Properties: Information [L], Notes, Details, Add to Timeline
Editing: Scroll to Task [L], Find [v], Clear [v], Fill [v]
=> 8 groups.  Labelled controls ~30 if icon-only buttons are excluded; the five
percent buttons, five small schedule icons and the Font dialog launcher are the
ones a "30" count may omit.
Window chrome: title bar carries QAT (save, undo [v], redo [v], customise [v]),
file name + "Project Professional", account initials bubble; then the tab row
with the Tell-me box; the Gantt Chart Format contextual tab label is green.
Status bar: Ready | "New Tasks : Auto Scheduled" | five view buttons at right
(not four: Gantt, Task Usage, Team Planner, Resource Sheet, Report) | zoom - slider +.
A "New tasks are created in Auto Scheduled mode / Change new tasks' default mode here"
callout is open above the status bar.
Timescale: three tiers (year 2026 / quarters / months) at this zoom, not days.
