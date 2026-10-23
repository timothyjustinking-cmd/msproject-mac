# A2 - qt_ribbon_inventory: what the gate pins, and whether it is a tautology

Audit of wave 22 lane R6 (docs/handoff/r6.md; wave22-complete.md "Decision 1").
Read-only; nothing built or run.  Repo HEAD e252041.  R6's commit is a60453b
(the only commit touching app_qt/tests/ribbon_inventory_test.cpp).

Files:
* gate:        app_qt/tests/ribbon_inventory_test.cpp (593 lines)
* CTest entry: app_qt/CMakeLists.txt:299-306 (`qt_ribbon_inventory`, args
  `QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1 <exe> tests/data/synthetic-base.mpp`)
* app source table: app_qt/ribbon_bar.cpp:72-245 (`tab_specs()`, `kTabs`)
* accessors read by the gate: app_qt/ribbon_bar.cpp:650-718
  (tab_titles 650, group_titles 658, set_active_tab 681, group_button_labels 695,
  group_button_action 708), declared app_qt/ribbon_bar.h:47-87
* widget construction: app_qt/ribbon_bar.cpp:477-581 (build_page); layout /
  overflow: 583-623 (layout_page_groups)
* helpers: app_qt/tests/gate_util.h (check :22, inside :60, finish :65)
* constants: app_qt/shell_style.h:35-36 (900x560), :42-43 (1920x1032),
  :91 (kRibbonButtonHeight = 64), :92 (kRibbonIconPx = 32)

## (a) The pinned inventory, tab -> group -> buttons (ribbon order)

Source: ribbon_inventory_test.cpp:87-256 (`pinned_tabs()`).  Markers:
* `[none]`  = MenuAction::none -> button built disabled with tooltip
  "not implemented in this port" (ribbon_bar.cpp:532-536)
* `-> x`    = kept MenuAction x; enabled iff msqt::shell_implements(x) at
  runtime.  The pin does NOT say which of these are enabled.  r6.md:60 notes
  Resource > Insert > Add Resources (insert_resource) is disabled ("no resource
  writer") despite its mapping.
* Widget kind: EVERY pinned entry is a plain QToolButton, objectName
  "ribbonButton", Qt::ToolButtonTextUnderIcon, fixed height 64, icon 32 px
  (ribbon_bar.cpp:508-520).  There are NO split buttons, NO menu buttons, NO
  checkboxes and NO combo boxes anywhere in the model or the pin.  The Font
  group's family/size combos are carried as two disabled text buttons whose
  labels are the capture's VALUES ("Arial", "8").  One group is caption-only.

Tab 1  "Task"  (8 groups, 30 buttons)  -- capture project-gantt-loaded-clean.png
  View        : Gantt Chart [none]
  Clipboard   : Paste -> paste | Cut -> cut | Copy -> copy | Format Painter [none]
  Font        : Arial [none, value display] | 8 [none, value display] |
                B [none] | I [none] | U [none]
  Schedule    : Mark on Track [none] | Respect Links [none] | Inactivate [none]
  Tasks       : Manually Schedule [none] | Auto Schedule [none] | Inspect [none] |
                Move [none] | Mode [none]
  Insert      : Task -> insert_task | Summary [none] | Milestone [none] |
                Deliverable [none]
  Properties  : Information -> task_fields | Notes [none] | Details [none] |
                Add to Timeline [none]
  Editing     : Scroll to Task [none] | Find -> show_filter |
                Clear -> clear_filter | Fill [none]
  (Deliberately NOT modelled, so NOT pinned: the five %-complete buttons,
   indent/outdent arrows, font colour / highlight pickers - r6.md:50-53.)

Tab 2  "Resource"  (5 groups, 13 buttons)  -- project-ribbon-resource.png
  View        : Team Planner [none]
  Assignments : Assign Resources [none] | Resource Pool [none]
  Insert      : Add Resources -> insert_resource
  Properties  : Information [none] | Notes [none] | Details [none]
  Level       : Level Selection [none] | Level Resource [none] | Level All [none] |
                Leveling Options [none] | Clear Leveling [none] |
                Next Overallocation [none]

Tab 3  "Report"  (3 groups, 10 buttons)  -- project-ribbon-report.png
  Project     : Compare Projects [none]
  View Reports: New Report [none] | Dashboards [none] | Resources [none] |
                Costs [none] | In Progress [none] | Getting Started [none] |
                Custom [none] | Recent [none]
  Export      : Visual Reports [none]

Tab 4  "Project"  (6 groups, 14 buttons)  -- project-ribbon-project.png
  Insert      : Subproject [none]
  Add-ins     : Get Add-ins [none] | My Add-ins [none]
  Properties  : Project Information [none] | Custom Fields [none] |
                Links Between Projects [none] | WBS [none] |
                Change Working Time [none]
  Schedule    : Calculate Project [none] | Set Baseline [none] | Move Project [none]
  Status      : Status Date [none] | Update Project [none]
  Proofing    : Spelling [none]

Tab 5  "View"  (7 groups, 26 buttons)  -- project-ribbon-view.png
  Task Views    : Gantt Chart [none] | Task Usage -> view_task_usage |
                  Network Diagram [none] | Calendar [none] | Other Views [none]
  Resource Views: Team Planner [none] | Resource Usage -> view_resource_usage |
                  Resource Sheet -> view_resource_sheet | Other Views [none]
  Data          : Sort [none] | Outline [none] | Tables [none] | Highlight [none] |
                  Filter -> show_filter | Group by [none]
                  (comment at test:202 / ribbon_bar.cpp:114: Highlight is
                   stacked above Filter above Group by in the capture; the
                   port renders them side by side - see (d))
  Zoom          : Timescale [none] | Zoom [none] | Entire Project [none] |
                  Selected Tasks [none]
  Split View    : Timeline [none] | Details [none]
  Window        : New Window [none] | Switch Windows [none] | Arrange All [none] |
                  Hide [none]
  Macros        : Macros [none]

Tab 6  "Help"  (1 group, 3 buttons)  -- project-ribbon-help.png
  Help        : Help [none] | Feedback [none] | Show Training [none]

Tab 7  "Gantt Chart Format"  (contextual; 6 groups, 17 buttons)
                                            -- project-ribbon-gantt-format.png
  Format           : Text Styles [none] | Gridlines [none] | Layout [none]
  Columns          : Insert Column [none] | Column Settings [none] |
                     Custom Fields [none]
  Bar Styles       : Format [none] | Critical Tasks [none] | Slack [none] |
                     Late Tasks [none] | Task Path [none] | Baseline [none] |
                     Slippage [none]
  Gantt Chart Style: CAPTION ONLY, zero buttons (`{"Gantt Chart Style", {}}`,
                     test:247 / ribbon_bar.cpp:237) - the 16 colour swatches +
                     preset drop-down are not modelled
  Show/Hide        : Outline Number [none] | Project Summary Task [none] |
                     Summary Tasks [none]
  Drawings         : Drawing [none]

Totals: 36 groups, 113 buttons (= command_backlog.py's "113 ribbon rows"),
11 with a kept MenuAction, 102 none.  The pinned tab titles are exactly
"Task","Resource","Report","Project","View","Help","Gantt Chart Format"; the
File tab (ribbonFileTab) is NOT part of tab_titles() or the pin.

Observation (not a gate defect, but a behaviour change the gate now locks in):
the pre-R6 table (git show 6a506cd:app_qt/ribbon_bar.cpp lines 103-132) put
Resource Sheet / Resource Usage / Task Sheet / Save as PDF / Export PDF (Sheet)
/ Export PDF (Gantt) on the ribbon with live actions.  R6 replaced those with
the capture's labels (Report > Export is now a single disabled "Visual
Reports"; Task Sheet is gone from View > Task Views).  Those commands remain
reachable only via the menu model (app/menu_model.cpp:47-48, :178).

## (b) Independent literal or shared structure?  Verdict: duplicate copy, not a shared structure

The pin side:
* ribbon_inventory_test.cpp:61-74 defines its OWN types ButtonPin / GroupPin /
  TabPin (anonymous namespace), and :87-256 `pinned_tabs()` returns a static
  `std::vector<TabPin> kPins` literal typed in the test file.
* The header comment at :76-85 says "THE PIN.  Measured from the captures,
  never from the implementation".

The app side:
* ribbon_bar.cpp:37-51 defines ButtonSpec / GroupSpec / TabSpec (also in an
  anonymous namespace, so `tab_specs()` is NOT linkable from the test), and
  :72-245 `tab_specs()` returns `static const std::vector<TabSpec> kTabs`.
* The gate never calls tab_specs().  It reads the BUILT WIDGETS:
  group_titles() (ribbon_bar.cpp:658-676) walks page_layout_ items and reads
  each QLabel#ribbonGroupLabel's text(); group_button_labels() (:695-706)
  reads QToolButton::text() from page_buttons_; group_button_action()
  (:708-718) reads page_actions_, both filled by build_page() (:541-543).
  tab_titles() (:650-656) is the one accessor that reads tab_specs() directly.

So mechanically it is NOT a tautology: there is no shared array, no include of
a common header, no generator.  Any later edit to kTabs, to build_page(), or
to the accessors that changes a title, label, order or action will fail the
gate.

BUT the two literals are content-identical.  Normalising both tables
(strip whitespace and comments, drop TabSpec's `contextual` bool, map
`app::MenuAction` -> `MenuAction`) and diffing ribbon_bar.cpp:72-245 against
ribbon_inventory_test.cpp:87-256 yields ZERO differences across all 163 data
lines (only the function/variable names and one line-wrap differ).  Even the
side comments were carried across (ribbon_bar.cpp:114-115 vs test:202;
ribbon_bar.cpp:160-161 vs test:245-246).  Both were written in the same commit
(a60453b) by the same lane.  The claim "never from the implementation" cannot
be verified from the repo: the captures it names (corpus/reference/
2026-10-23-msproject/project-ribbon-*.png, present locally: resource, report,
project, view, help, gantt-format) are git-ignored (.gitignore:27 `corpus/`),
so CI can only ever check the pin against the port, not against Project.

Consequence: the gate proves the port still matches R6's transcription; it
cannot catch a transcription error made once and pasted twice.  The only
independent cross-reference in the repo is the prose tables in
docs/handoff/r6.md:40-119, which also agree with the pin (checked by eye here,
tab by tab) and were written by the same lane.  docs/ui-command-backlog.md is
NOT an independent source: app_qt/tools/command_backlog.py:53-54 regenerates
it by regex-parsing ribbon_bar.cpp.

One sub-check IS tautological by construction: the "decision-9 rule".
build_page() sets enabled = shell_implements(action) (ribbon_bar.cpp:532-533)
and the gate asserts isEnabled() == shell_implements(action) (test:339, :362)
using the same function.  If shell_implements() changed to return true for
every action, every button would enable and the gate would still pass; the pin
holds no expected enabled/disabled state and no count of implemented commands.

## (c) What is asserted at 900 px, and dropped/reordered groups

1920x1032 pass (test:489-521), per tab after set_active_tab(i):
* active_tab_index() == i;
* check_inventory (test:286-327): group_titles() == pinned titles (exact
  QStringList equality, order included); per group, button labels == pin
  (exact, ordered); per button, MenuAction == pin;
* check_button_state (test:331-369): enabled iff shell_implements; tooltip is
  the label when enabled, else exactly "not implemented in this port"; label
  non-empty;
* check_layout (test:375-457): visible groups are a PREFIX (no visible box
  after a hidden one); every visible box inside the ribbon rect (+-1 px) and
  width >= sizeHint-1; caption text not wider than its label; every visible
  button width >= text advance+2, height == 64, inside the ribbon;
  groups_overflow() == (visible < total); overflow_button()->isVisible() ==
  that; chevron inside the ribbon when shown.  Visible counts are PRINTED as
  "note:" lines (test:454) only - the 8/8, 5/5 ... figures in r6.md are not
  asserted;
* then one global assertion: the Task tab did not overflow at 1920 (test:520).
  No other tab is required to fit at 1920 (test:23-27, :511-515).

900x560 pass (test:523-541): window resized to kWindowWidth x kWindowHeight;
per tab: tab_titles() unchanged, then check_layout only (prefix, fit, chevron
equivalence as above).  check_inventory and check_button_state are NOT re-run
at 900.  One global assertion: at least one tab overflows at 900 (test:540).
Which groups survive at 900 (r6.md says Task 4/8, Resource 4/5, Project 3/6,
View 2/7, Gantt Format 3/6) is not pinned; those are "note:" prints.

Would the gate fail if a group were silently dropped or reordered?
* Group removed from, or reordered in, kTabs / build_page(): YES at 1920 -
  group_titles() walks every layout item regardless of visibility
  (ribbon_bar.cpp:658-676 never tests isVisible), compared with == to the pin.
* Group present in the model but hidden by layout_page_groups() although it
  fits (e.g. a width-accounting bug), at 1920: only partly.  The inventory
  check still passes (visibility is ignored); check_layout passes as long as
  the hidden groups are a suffix and the chevron shows, because the gate
  accepts any visible/total ratio except for the Task tab.  So a trailing
  group of Resource/Report/Project/View/Help/Gantt Format silently hidden at
  1920 would NOT fail the gate (it would only change a "note:" line).  A hidden
  group FOLLOWED by a visible one does fail (prefix rule).
* A button hidden but present: not caught - check_inventory reads text() of
  every QToolButton in page_buttons_ (visibility ignored) and check_layout only
  measures VISIBLE buttons; there is no per-button isVisible assertion.
* Reorder confined to the widget tree (e.g. insertWidget at another index):
  caught - group_titles() reads layout order, and check_button_state reads
  captions in child order; either diverging from the pin fails.
* Overflow at 1920 is allowed for any tab except Task, so a change that widens
  buttons until View no longer fits at 1920 passes.

## (d) What the gate does NOT check

* Icons: shell_action_icon(action, 32) is set on every button
  (ribbon_bar.cpp:515-516) but no assertion touches icon(), isNull(), or the
  32 px icon size.  Every `none` button therefore shares whatever
  shell_action_icon(none) returns, unasserted.
* Sizes beyond "fits its label": no measured button widths, group widths,
  gutters (4 px margins, 2 px spacing, 12 px text padding, ribbon_bar.cpp
  :261-264, :497-506) or ribbon height are asserted by this gate (other gates
  cover the Task tab's 1920 geometry).
* Separators: the 1 px QFrame after each group (ribbon_bar.cpp:563-572) is
  never counted or checked for visibility.
* Large/small button layout: the port draws EVERY button as a 64 px
  text-under-icon tile in ONE horizontal QHBoxLayout per group
  (ribbon_bar.cpp:504-506).  Project's stacked small buttons (View > Data:
  Highlight/Filter/Group by; Task > Font B/I/U; the Properties stacks) are
  only modelled as ORDER.  The gate asserts order, not stacking, row count, or
  large-vs-small style, so the comment "stacks Highlight above Filter" is
  documentation, not an assertion.
* Group label text case / font: label strings are compared exactly (so case
  is effectively pinned for labels and captions), but the caption font, point
  size, colour (#6a6a6a) and centre alignment are not asserted.
* Keytips / accelerators: not modelled anywhere in RibbonBar; no QAction,
  shortcut or keytip overlay exists for ribbon buttons (they are QToolButtons
  wired to a lambda, ribbon_bar.cpp:537-541), so nothing is checked.
* The File tab (ribbonFileTab / backstage), the tab-overflow chevron
  ("More tabs", tab_overflow_), tab colours and the contextual tab's green
  styling are outside the pin; the contextual rule is checked only as
  contextual_tab()->isVisible() across a Resource Sheet / Task Sheet switch
  (test:543-588).
* Widget kinds: because the pin holds only labels + actions, it cannot
  detect that the Font family/size boxes are not combos, that no button is a
  split/menu button, or that the Gantt Chart Style palette and the %-complete
  / indent / colour controls are absent.
* Enabled/disabled truth: as in (b), only consistency with shell_implements()
  is checked; no expected enabled set or count (11 of 113) is pinned.
* Click behaviour: no button is clicked; the handler_ wiring
  (ribbon_bar.cpp:537-541) is not exercised by this gate.
* Agreement with the captures themselves: nothing in the repo or CI compares
  the pin to corpus/reference/2026-10-23-msproject/*.png (git-ignored).
