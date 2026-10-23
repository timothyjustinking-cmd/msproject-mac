# Wave 26 - the completion record

One table row per brief item.  A group ends only when its exact
`WAVE-26-<group>-COMPLETE` line is appended below (wave-26-brief section 1 rule
2).  Deferrals need the attempt evidence wave-26-brief section 1 rule 4 names.

## G0 - wave-25 verifier findings (Critical/High only)

The independent review of `wave24..wave25` found 0 Critical and 1 High; its
Mediums and Lows are queued for after this wave and are not G0's work.

| item | finding | status | evidence |
|---|---|---|---|
| W26-0.1 | High: macOS Print / Print-to-PDF drops the last task when the project row is shown (`app/main.mm` built the print row list from `plan.view.tasks.size()`, but those are RecordSet rows) | **DONE** | commit `28a9f9b`; gates `build/w26/gates-a5dff41-W26-0.1.log` |

W26-0.1 detail:

- The one row-list decision now lives in the shared layer:
  `app::print_row_list()` (`app/page_layout.h/cpp`) returns the filter's rows
  when a filter is active (an empty zero-match list prints nothing) and every
  RecordSet row otherwise.  `make_print_request()` in `app/main.mm` uses it for
  both the Gantt and sheet print requests, and `app_qt::print_rows()` delegates
  to it, so the Qt and macOS paths cannot drift.  The task count is still the
  subtitle's "N of N" numerator.
- Falsifying test in the headless `app_layout` ctest: on
  `tests/data/wave24-order.mpp` (project row shown, five tasks) the print row
  list is six rows, ends at the last RecordSet row and carries every task uid.
  Under the old `0 .. tasks.size()-1` arithmetic it is five rows and the
  highest-ID task is missing; the assertions fail.
- The review's Medium 3 (the smoke half of W25-1's acceptance) is closed by
  the same commit: `app::smoke_row_shown()` runs the row-addressed sequences on
  `wave24-order.mpp` (load identity, the row's sheet cell, the uid-0 edit
  guard, the Gantt source by uid, the print row list and a printed page,
  matching and zero-match filters, a core/render export).  `app_smoke` runs it
  on this machine; `app/main.mm`'s AppKit half now also loads and prints the
  row-shown copy through the real paths and no longer asserts `bars == tasks`.
- macOS `main.mm` is not compiled on this machine (no toolchain).  The shared
  function and the portable sequence are the tested half; the AppKit additions
  are written for CI, not claimed as run here.

Gates on the commit's tree (all numbers from
`build/w26/gates-a5dff41-W26-0.1.log`, parent `a5dff41`): core ctest
`-E plan_recalc` 51/51; `plan_recalc` 512 checks with corpus / 266 with
`MPP_NO_CORPUS=1`; M2 `recalc --compare` 332/332 (268 leaves, 64 summaries);
reader 1100 with corpus / 420 without; app ctest 15/15 (`app_layout` 279
checks, `app_smoke` 121); app_qt ctest 18/18; `check_fixtures.py` OK (23
documented hashes, 21 tracked `.mpp`) and oracle provenance OK (8); parity
wave23-outline 10 comparable / 0 failing and wave25-auto `colour.task_bar`
1 / 0; client-name scan 0 hits on the parent, on the working-tree content and
on `28a9f9b` including its commit message.

## Found, not fixed

Nothing new in G0.  The wave-25 review's Medium/Low list stays queued for after
this wave, as the brief says.

WAVE-26-G0-COMPLETE

## G1 - ribbon controls (the kinds Project shows)

Every item is DONE; nothing in G1 is deferred.  The control model was introduced
by W26-1.1 and every later item adds its own wiring, pin enablement and
functional gate checks in its own commit.

| item | finding | status | evidence |
|---|---|---|---|
| W26-1.1 | Task > Font: font-name/size combos, B/I/U, highlight/font-colour split buttons, dialog launcher | **DONE** | commit `3142c60`; gates `build/w26/gates-853e2d3-W26-1.1.log` |
| W26-1.2 | Task > Schedule: 0/25/50/75/100 % (functional), Mark on Track, Respect Links, Inactivate, five small icons | **DONE** | commit `e803933`; gates `build/w26/gates-3142c60-W26-1.2.log` |
| W26-1.3 | View > Data combos, Zoom Timescale combo, Split View check boxes/combos; Timeline toggles the strip | **DONE** | commit `3660091`; gates `build/w26/gates-e803933-W26-1.3.log` |
| W26-1.4 | Gantt Chart Format check boxes, four align/wrap buttons, 16-swatch gallery + expander + launcher | **DONE** | commit `0708ea4`; gates `build/w26/gates-3660091-W26-1.4.log` |
| W26-1.5 | Project > Status date field; Resource > Level and every stacked group in Project's small rows | **DONE** | commit `b868aff`; gates `build/w26/gates-0708ea4-W26-1.5.log` |

W26-1.1 detail -- the control model and Task > Font:

- `app_qt/ribbon_bar.{h,cpp}` now models the real control kinds: the 64-px large
  button, the 22-px one-row small button, a split button, a combo, a check box,
  a date field (`RibbonField`), the `RibbonGallery` (16 swatches + expander) and
  a dialog launcher.  `RibbonBar::control_kind()` classifies the LIVE widget
  (subclass/QWidget class), and the inventory gate compares the pin's kind with
  that, never with the spec.
- The groups are laid out at the clean strips' measured rectangles, and the
  three stack rows at the measured 5/27/49 px baselines.  The numbers are
  committed in `tests/data/reference/w26-ribbon.measures.json` (group
  left/right, row offsets, the 16 gallery colours); the captures stay local.
- `app_qt/ribbon_pin.tsv` is now 9 columns (tab, contextual, group, kind, label,
  enabled, icon, row, state) and pins every control on the seven tabs;
  `app_qt/ribbon_pin_unmodelled.tsv` is header-only.  The backlog generator
  (`app_qt/tools/command_backlog.py`) learned the new spec builders; the
  generated `docs/ui-command-backlog.md` is regenerated.
- The Font group carries combos showing the new-project default (Calibri 11;
  the FILE's text style is G3-3.1's decode), B/I/U, the two colour splits and
  the launcher.  No engine edit exists for any of them, so they are disabled
  with the exact decision-9 tooltip.

W26-1.2 detail -- Schedule:

- The % buttons set PercentComplete through a new
  `app::StructuralKind::set_percent` edit (`app/edit_model.{h,cpp}`): last edit
  wins in `apply()`, undo/redo see it, and `save_structural_edits()` writes it
  with one `mppwrite::TaskTable::set_percent_complete` + `store_into` + container
  write (step 5a).  The headless `app_edit` test saves 40 % and re-reads 40 from
  the saved file.  This is deliberate: the stored table's % Complete column is
  presentation-only/read-only in this port (`app/view_model.h
  task_column_editable`), so a cell edit would be refused and never silently
  dropped; the ribbon therefore uses the saveable structural path.
- Link opens the existing Predecessors dialog; Unlink deletes the selected task's
  saved predecessor links through `app::make_link_delete` (gate: 3 links -> 2).
- Outdent, indent and Split Task are disabled with the exact tooltip: the shared
  sheet model has no outline-level or split edit (`core/sheet/sheet.h`: "No row
  insert/delete/move or indent/outdent").  The gate asserts the disabled state
  and exact tooltip.
- Mark on Track, Respect Links and Inactivate have no engine operation and are
  disabled with the exact tooltip.

W26-1.3 detail -- View:

- Data carries Highlight/Filter/Group by combos showing `No Highlight`,
  `No Filter`, `No Group`; Zoom carries the Timescale combo showing `Days`.
  They are display values with no engine behind them, so they are disabled with
  the exact tooltip (decision 9); the filter dialog remains reachable from
  Task > Editing > Find and the menu.
- The Timeline check box starts from the file's stored setting (wave-24 view
  props) and shows/hides the live `TimelineStrip`; the gate opens both
  `wave24-timeline-hidden.mpp` and `wave24-timeline-shown.mpp` and clicks the box
  twice on the hidden one.  The Details box/combo belong to W26-6.3 and are
  disabled.

W26-1.4 detail -- Gantt Chart Format:

- Critical Tasks drives the existing `GanttCanvas::set_critical_highlight()`;
  Project Summary Task toggles RecordSet row 0 through PlanDocument's
  presentation override (initial state = the file's stored setting; the
  override survives a structural rebuild; nothing is written).
- The 16-swatch gallery is built from the sampled clean-strip palette; the gate
  compares every swatch's pixel colour with the 16 committed RGB triples and
  checks the expander.  Slack, Late Tasks, Outline Number, Summary Tasks and the
  four align/wrap buttons have no engine and stay disabled with the exact
  tooltip.
- The gate drives both toggles: row 0 added/removed on the summary-on/off
  fixtures and the critical highlight on/off.

W26-1.5 detail:

- Project > Status is the Status Date field (label + `QDateEdit` showing NA) and
  the Update Project one-row button; the field is disabled with the exact
  tooltip because the port has no project status date.
- The Resource Level stack and every other stacked group are pinned as `small`
  rows and gated at the measured 5/27/49 baselines inside their measured group
  rectangles.

Acceptance evidence (from `build/w26/g1-final-ribbon.log`, the final commit's
`qt_ribbon_inventory` run): every tab shows all its groups at 1920 (Task 8/8,
Resource 5/5, Report 3/3, Project 6/6, View 7/7, Help 1/1, Gantt Chart Format
6/6); the live control kinds are small 44, split 5, gallery 1, launcher 2,
checkbox 8, combo 8, field 1; the unmodelled backlog is 0 rows; 0 failures.
At 900 the prefix/fit/chevron contract holds (Task 4/8, Project 5/6, View 3/7,
Gantt Chart Format 3/6 show the `>>` chevron).

Gates per commit (each log is on that commit's parent tree):

| item | log | core | plan_recalc | reader | M2 | app | app_qt | fixtures | parity |
|---|---|---|---|---|---|---|---|---|---|
| W26-1.1 | `gates-853e2d3-W26-1.1.log` | 51/51 | 512 / 266 | 1100 / 420 | 332/332 | 15/15 | 18/18 | OK | 10/0, 1/0 |
| W26-1.2 | `gates-3142c60-W26-1.2.log` | 51/51 | 512 / 266 | 1100 / 420 | 332/332 | 15/15 | 18/18 | OK | 10/0, 1/0 |
| W26-1.3 | `gates-e803933-W26-1.3.log` | 51/51 | 512 / 266 | 1100 / 420 | 332/332 | 15/15 | 18/18 | OK | 10/0, 1/0 |
| W26-1.4 | `gates-3660091-W26-1.4.log` | 51/51 | 512 / 266 | 1100 / 420 | 332/332 | 15/15 | 18/18 | OK | 10/0, 1/0 |
| W26-1.5 | `gates-0708ea4-W26-1.5.log` | 51/51 | 512 / 266 | 1100 / 420 | 332/332 | 15/15 | 18/18 | OK | 10/0, 1/0 |

Every log also carries `check_fixtures.py` (23 documented hashes, 21 tracked
`.mpp`), the oracle-provenance check, the client-name scan (0 hits on HEAD and
on the working-tree stash) and the 8-hex grep (no matches).  Two transient
Dropbox/MSBuild file locks hit during the stage builds; the builds were retried,
not changed.

## Found, not fixed

No new Critical or High finding in G1.  Notes for later work, all deliberate:

- The stored table's % Complete column remains presentation-only (a typed cell
  edit is refused, never dropped); the ribbon's % buttons use the new saveable
  `set_percent` structural path instead.  A typed % Complete cell edit is
  therefore still not a file change in this port.
- The Data/Zoom combos and every control without an engine operation are
  disabled with the exact decision-9 tooltip rather than faked (see the pin's
  enabled column).
- The Details check box / split view is W26-6.3; the gallery swatches apply no
  bar style yet (bar-style decoding is G3-3.2).
- The 900-px layout drops whole groups (unchanged contract); the reference's
  group rectangles are compared at 1920 only, as the clean strips are full
  width.

WAVE-26-G1-COMPLETE

## G2 - icons

Every item is DONE; nothing in G2 is deferred.  The glyph library
(`app_qt/ribbon_glyphs.{h,cpp}`) paints each command's mark on a 32-unit grid
with ONE ink colour: a 2 px stroke at the 32 px large size and the same
drawing at half scale (1 px) at the 16 px one-row size.  Nothing is copied
from Microsoft: the shapes are our own geometric marks.  Every command
control (button/small/split) carries the pin's icon key; the ribbon resolves
the key through the library, and an empty or unknown key draws the neutral
placeholder so the gate fails on it instead of passing silently.

| item | tab / commands | status | evidence |
|---|---|---|---|
| W26-2.1 | Task tab: 40 commands, glyph library + gate + contact sheet | **DONE** | commit `e83e66b`; gates `build/w26/gates-d4be89a-W26-2.1.log` |
| W26-2.2 | Resource tab: 13 commands | **DONE** | commit `7729ef2`; gates `build/w26/gates-e83e66b-W26-2.2.log` |
| W26-2.3 | Report tab: 10 commands | **DONE** | commit `98602a7`; gates `build/w26/gates-7729ef2-W26-2.3.log` |
| W26-2.4 | Project tab: 13 commands | **DONE** | commit `2ade1d3`; gates `build/w26/gates-98602a7-W26-2.4.log` |
| W26-2.5 | View tab: 20 commands (the one expected same-tab reuse) | **DONE** | commit `d5a1cce`; gates `build/w26/gates-2ade1d3-W26-2.5.log` |
| W26-2.6 | Help tab: 3 commands | **DONE** | commit `de787e7`; gates `build/w26/gates-d5a1cce-W26-2.6.log` |
| W26-2.7 | Gantt Chart Format tab: 15 commands; the all-tabs assertion | **DONE** | commit `e8d0520`; gates `build/w26/gates-de787e7-W26-2.7.log` |
| W26-2.8 | correction: the capture's cross-tab reuse (Resource Information/Details, Project Information, Custom Fields redraw) | **DONE** | commit `fde3186`; gates `build/w26/gates-2d1448a-W26-2.8.log` |

Acceptance:

- **No command shows the placeholder.**  The new `qt_icon_gate`
  (`app_qt/tests/icon_gate_test.cpp`, ctest `qt_icon_gate`) walks the LIVE
  ribbon widgets of every finished tab in pin order, reads each button's own
  icon at its own icon size, and hashes the pixels.  Every command's glyph at
  32 px and at 16 px is non-empty and differs from the rendered
  `MenuAction::none` placeholder.  The final item also asserts the pin's icon
  column has no `-` left on any `button`/`small`/`split` row: 114 command
  controls across seven tabs (Task 40, Resource 13, Report 10, Project 13,
  View 20, Help 3, Gantt Chart Format 15), 0 without a key.
- **Uniqueness gate.**  The same gate hashes every glyph on the tab at both
  sizes and fails on a duplicate unless the pin lists the reuse with its exact
  count.  The only same-tab reuse is View's two `Other Views` entries
  (`view.other` x2) - Project uses one glyph in Task Views and Resource Views.
  Two DIFFERENT keys that render identical bytes also fail; no such collision
  exists.
- **Monochrome.**  The gate starts from the premultiplied render and requires
  every visible pixel to be the single ink colour blended by its alpha (a
  deviation of more than 5/255 fails).  This is measured from the pixels, not
  the drawing code.
- **Two sizes, Office-like weight.**  Large buttons carry the 32 px glyph and
  one-row buttons the 16 px one (the gate reads the live `iconSize`); every
  glyph is also rendered and checked at the other size.
- **Human-review contact sheets** (not committed; `build/` is git-ignored):
  `build/w26/icons-task.png`, `icons-resource.png`, `icons-report.png`,
  `icons-project.png`, `icons-view.png`, `icons-help.png`,
  `icons-gantt-chart-format.png` - each command's 32 px and 16 px glyph with
  its group/label/key, for Tim.

Glyph reuse, cross-tab (allowed only where Project reuses the mark):
`properties.notes` (Task/Resource Notes), `view.gantt` (Task/View Task Views),
`view.team_planner` (Resource/View Resource Views), `properties.custom_fields`
(Project/Gantt Chart Format).  Re-reading the clean strips before hand-off
found three of these were wrong and they are corrected in W26-2.8: Resource
Information is a person with the info badge (its own
`properties.resource_information`), Resource Details is a page with value
boxes (`properties.resource_details`), and Project Information is a
table/window with the info badge (its own drawing under
`properties.project_information`).  The Custom Fields glyph was also redrawn
as the field grid with the add badge the capture shows on both tabs.  The
gate's same-tab reuse list still contains only View's two `Other Views`
entries; cross-tab reuse is not gated (it is the capture's evidence above),
which is why W26-2.8 exists: the strips, not the gate, caught it.

Deliberate presentation decisions: B/I/U and the five %-complete buttons are
icon-only (their pin labels remain as accessible names), matching the
readback's `[s]` classification and their small fixed widths; the Task tab's
Other bands are unchanged.  `app_qt/ribbon_pin.tsv` fills the independent
pin's icon column for every command row; the live key is compared with it, so
a command cannot draw one glyph while the pin names another.
`app_qt/ribbon_pin_unmodelled.tsv` remains empty and the existing inventory
gate still passes.

Gates per commit (each log is on that commit's parent tree, quoted from the
log):

| item | log | core | plan_recalc | reader | M2 | app | app_qt | fixtures | parity |
|---|---|---|---|---|---|---|---|---|---|
| W26-2.1 | `gates-d4be89a-W26-2.1.log` | 51/51 | 512 / 266 | 1100 / 420 | 332/332 | 15/15 | 19/19 | OK | 10/0, 1/0 |
| W26-2.2 | `gates-e83e66b-W26-2.2.log` | 51/51 | 512 / 266 | 1100 / 420 | 332/332 | 15/15 | 19/19 | OK | 10/0, 1/0 |
| W26-2.3 | `gates-7729ef2-W26-2.3.log` | 51/51 | 512 / 266 | 1100 / 420 | 332/332 | 15/15 | 19/19 | OK | 10/0, 1/0 |
| W26-2.4 | `gates-98602a7-W26-2.4.log` | 51/51 | 512 / 266 | 1100 / 420 | 332/332 | 15/15 | 19/19 | OK | 10/0, 1/0 |
| W26-2.5 | `gates-2ade1d3-W26-2.5.log` | 51/51 | 512 / 266 | 1100 / 420 | 332/332 | 15/15 | 19/19 | OK | 10/0, 1/0 |
| W26-2.6 | `gates-d5a1cce-W26-2.6.log` | 51/51 | 512 / 266 | 1100 / 420 | 332/332 | 15/15 | 19/19 | OK | 10/0, 1/0 |
| W26-2.7 | `gates-de787e7-W26-2.7.log` | 51/51 | 512 / 266 | 1100 / 420 | 332/332 | 15/15 | 19/19 | OK | 10/0, 1/0 |
| W26-2.8 | `gates-2d1448a-W26-2.8.log` | 51/51 | 512 / 266 | 1100 / 420 | 332/332 | 15/15 | 19/19 | OK | 10/0, 1/0 |

Every log also carries `check_fixtures.py` (23 documented hashes, 21 tracked
`.mpp`), oracle provenance (8), the client-name scan (0 hits on HEAD and on
the working-tree content) and the 8-hex grep (no matches).  `app_qt` is 19/19
from W26-2.1 on: the new `qt_icon_gate` is the nineteenth test.  Two transient
MSBuild link-log locks hit during the stage builds; the builds were retried,
not changed.

## Found, not fixed

No new Critical or High finding in G2.  Notes for later work, all deliberate:

- The menu-bar and quick-access icons still use the older `shell_action_icon`
  drawings; G2's brief and gate cover the ribbon's commands.  A coherent
  pass over the menu/QAT artwork is not part of this group.
- The large split buttons (Mark on Track, Bar Styles Format, Drawing) remain
  disabled with the exact decision-9 tooltip, as G1 left them; the glyph does
  not change their wiring.
- The contact sheets are local review artifacts under `build/w26/` and are
  deliberately not committed (the publication hold and the git-ignored
  `build/`).

WAVE-26-G2-COMPLETE

## G3 - render what the file stores (controlled-save decoding, the flag method)

Every item is DONE; nothing in G3 is deferred.  All three decode from the
applied Gantt view's `0x22400008` record (a Props document inside the CV_iew
main block) plus the `214/Props` FONT_BASES payload, and all three render in
the Qt shell through the shared `app/view_style` seam.  The record layout is
MPXJ's MPP14 layout (`GanttChartView14`, `GanttBarStyleFactory14`), then
verified against Project 2021's own controlled saves.

| item | finding | status | evidence |
|---|---|---|---|
| W26-3.1 | text styles: font family/size, bold/italic/underline, colour, highlight fill; the two named expected failures turn green | **DONE** | fixtures `a70a0c7`, code `cd727ad`; gates `build/w26/gates-a70a0c7-W26-3.1.log`; controlled saves `build/w26/g3/trees4/t3-*` |
| W26-3.2 | bar styles: the file's palette (task/summary/critical/milestone/progress/finished); PROJECT_A's own summary/critical/complete colours | **DONE** | commit `40c1ad2`; gates `build/w26/gates-cd727ad-W26-3.2.log`; controlled saves `build/w26/g3/trees3/b2-*`, `build/w26/g3/trees4/b3-*` |
| W26-3.3 | timescale: the stored tier units/counts/formats; PROJECT_A's year/quarter/month tiers render | **DONE** | commit `f7962e7`; gates `build/w26/gates-40c1ad2-W26-3.3.log`; controlled saves `build/w26/g3/trees2/ts-*`, `build/w26/g3/trees4/ts3-*` |

### W26-3.1 detail -- text styles

- **Decode.**  `core/viewstate/gantt_style.{h,cpp}` reads 17 records of 32
  bytes from +26, stride 32: font index +0, style bits +3 (0x01 bold, 0x02
  italic, 0x04 underline, 0x08 strike), foreground RGB + automatic flag at
  +4, background at +16, pattern u16 at +28.  The font index resolves through
  the `214/Props` record `0x03400000`: u16 count, then 68-byte records (u16
  opaque, u16 point size, 64-byte UTF-16 family).  A record shorter than
  41,362 bytes is refused; a bad font index marks that one style invalid and
  warns instead of losing the view.
- **Controlled saves (one change per save).**  The working calls are in
  `build/w26/g3/oracle_styles.py` (font family and size, first session) and
  `build/w26/g3/oracle_probe3.py` (colour, italic, highlight fill), each on a
  fresh copy of `tests/data/synthetic-base.mpp`: font family
  (`TextStyles(0, "Arial", ...)`) moves every style's font index; size
  (`TextStyles(0, "Calibri", 8, ...)`) rewrites the FONT_BASES entry the
  index names; the critical colour save (`PjColor pjBlue = 5`) writes RGB
  (0,0,255) in the three colour bytes with flag 0; the italic save sets bit
  0x02; the highlight fill (`TextStylesEx(7, ..., pjYellow, solid)`) writes
  the background colour and pattern.  Colour arguments are `PjColor` enums,
  not RGB longs (an RGB long is rejected with "argument value is not valid").
  Passing `pythoncom.Missing` for optional arguments wedged Project's modal
  state (probe 2's Guard killed it); every working call passes all arguments
  explicitly.  The saves are local-only, under git-ignored `build/w26/g3/`
  (`trees2/t-all-*`, `trees4/t3-*`, logs `oracle-styles.log`,
  `oracle-probe3.log`).
- **Rendering.**  `app/view_style.{h,cpp}` maps a row to the Text Styles item,
  most specific first: project summary, summary, milestone, critical,
  noncritical.  The precedence is measured from PROJECT_A's capture: row 3 is
  both critical and a milestone and renders with the MILESTONE style (italic
  red), and the file's critical style is upright black.  `app/view_style.cpp`
  is also the headless read seam (FONT_BASES + CV_iew + the CEdl applied-view
  name); `PlanDocument` reads it at load and `SheetTableModel` applies family,
  size, bold/italic/underline/strike, foreground and background.
- **The two W25-5 expected failures are now ordinary checks and pass**
  (`qt_stored_table_corpus`: 0 failures, 0 expected failures).  The milestone
  red is Project's stored RGB (255,0,0).  The old 0xB01C1C was a wave-23
  sampling error: the decoded record stores (255,0,0) and the clean capture
  contains 10,858 pure-red pixels and zero 0xB01C1C pixels.
- **Fixture errata.**  The committed style blob is synthetic-base's FIRST
  decodable Gantt view, "Gantt &with Timeline", not the applied view 2 as the
  `a70a0c7` message said.  The extraction selects the first Gantt view; the
  applied-view selection (CEdl `0x26400018`) is exercised by `app_view_style`
  on the same file.  The fixture bytes and SHA-256 are unchanged; the comment
  is corrected in `f7962e7` and `scripts/check_fixtures.py`.

### W26-3.2 detail -- bar styles

- **Decode.**  Count byte at +2243, then 195-byte records from +2255:
  middle shape/pattern/colour +0/+1/+2, start shape+type +15 and colour +16,
  end shape+type +28 and colour +29, ShowFor u64s +49/+57, row +65, the five
  text field ids +67..+86, id +89 and the NUL-terminated UTF-16 name +91.
  The controlled `GanttBarStyleEdit` saves changed exactly those fields:
  Summary middle/start/end colours and shapes
  (`build/w26/g3/trees4/b3-summary-green`) and Milestone start shape/colour
  (`b3-milestone-blue`).
- **Palette.**  `app::bar_palette_from` resolves roles by the file's own
  names and ShowFor bits: normal ("Task" or "Non-Critical Task"), manual
  normal ("Manual Task"), critical ("Critical Task", else first Normal +
  Critical style), summary ("Summary"), summary+critical (first Summary +
  Critical style -- PROJECT_A's unnamed 2+3 record), milestone (its start
  colour when the middle shape is none), project summary, progress and
  finished.  A stock project has no critical/summary-critical/finished style
  and keeps the wave-25 measured fallbacks.  PROJECT_A's 10 active styles are
  the capture's palette: noncritical (0,32,96), critical (255,0,0), summary
  (0,0,128), project-summary red, milestone (255,0,0), progress/finished
  (0,176,80).  Its unused light-red "Critical" (247,138,136) is never painted
  -- the capture has zero pixels of it.
- **Rendering.**  `GanttCanvas::bar_colour_for` paints the palette; the
  completion fill is the file's Progress colour (Finished when complete; a
  summary's progress is the thin top line) instead of the stock blue.
  Critical Tasks highlighting ON keeps its wave-25 measured override.
  `qt_stored_table_corpus` asserts that the rendered bars include
  (0,32,96)/(255,0,0)/(0,0,128), exclude (247,138,136) and paint the project
  row red.

### W26-3.3 detail -- timescale

- **Decode.**  Top units +41311, count +41313, format u16 +41315, align
  +41317, tick lines +41349, fiscal-year bit 0 of +41361; middle
  +1152/+1156/+1160/+1166 and bottom +1154/+1158/+1162/+1164; tick lines and
  fiscal bits in the view-flags byte +1086 (0x01/0x08 middle, 0x02/0x10
  bottom).  The stored unit byte maps through MPXJ's TimescaleUnitsHelper
  (byte+1 -> none/minutes/hours/days/weeks/thirds of months/months/quarters/
  half years/years); 255 is none.  PROJECT_A stores years/quarters/months,
  formats 0/3/10, all counts 1 -- exactly the capture's "2026" /
  "Qtr 2, 2026" / "Apr".
- **Controlled saves.**  Four TimescaleEdit variants
  (`build/w26/g3/trees2/ts-yqm, ts-ymd, ts-2tier, ts-yhq`, log
  `oracle-probe3`/`oracle-probe` results) changed the middle and bottom units
  and formats at exactly +1152/+1154/+1160/+1162.  The SIX wave-23 variants
  in `build/w23/` are byte-identical: their oracle passed the arguments by
  keyword, which the late-bound COM call rejected (the scripts logged the
  failure and saved unchanged copies), so they were not used and are recorded
  here as an errata.
- **Measurement limits (honest).**  Project 2021's TimescaleEdit did not
  change the TOP tier in any controlled save: `TopUnits=0, TierCount=3` left
  the stored top bytes untouched, and explicit TopLabel strings were rejected
  as invalid arguments.  A `MajorCount=2` save was byte-identical to count=1
  (`ts3-count2` == `ts-yqm`).  The top-tier fields and the count fields are
  therefore decoded from PROJECT_A's own saved record (a Project measurement)
  and rendered; the controlled saves prove the middle/bottom unit and format
  offsets.
- **Rendering.**  `GanttCanvas` builds one tier per stored unit and divides
  the fixed 42-px header between them: a stock file keeps its measured 2 x
  21 px week/day header, PROJECT_A gets 3 x 14 px, so the sheet and chart rows
  stay aligned.  Labels implement the measured formats (YYYY; "Qtr n, YYYY";
  short/full month names; the measured "12 Oct '26" week label; single-letter
  days), honour the stored alignment and use each tier's own text style.  The
  live render of PROJECT_A shows 2025/2026, "Qtr 4, 2025"/"Qtr 1, 2026" and
  Dec/Jan at the port's scroll position; the capture shows the same formats
  at its own scroll position.

### Live same-file comparison (PROJECT_A, local)

| row | Project 2021 (clean capture) | the port | verdict |
|---|---|---|---|
| text style | row 3 milestone italic red; summary bold blue 0x3366FF | decoded milestone slot italic + RGB(255,0,0), summary bold + RGB(51,102,255); rendered in the Qt sheet | **match** (format/colour); 0xB01C1C was never Project's colour |
| bar palette | navy summaries, red critical bars, dark-blue noncritical, green progress; 0 light-red pixels | decoded 10-style palette paints (0,0,128), (255,0,0), (0,32,96), (0,176,80); the port render contains the same roles and no (247,138,136) | **match** |
| timescale | year "2026" / quarter "Qtr 2, 2026" / month "Apr" | stored years/quarters/months formats 0/3/10 render as "2025 / Qtr 4, 2025 / Dec" at the port's scroll origin | **match** in tiers and label format; the visible date range/zoom still differs (the port keeps its own scroll origin) |

### Gates per commit (each log is on that commit's parent tree)

| item | log | core | plan_recalc | reader | M2 | app | app_qt | fixtures | parity |
|---|---|---|---|---|---|---|---|---|---|
| W26-3.1 | `gates-a70a0c7-W26-3.1.log` | 51/51 | 512 / 266 | 1100 / 420 | 332/332 | 16/16 | 19/19 | OK | 10/0, 1/0 |
| W26-3.2 | `gates-cd727ad-W26-3.2.log` | 51/51 | 512 / 266 | 1100 / 420 | 332/332 | 16/16 | 19/19 | OK | 10/0, 1/0 |
| W26-3.3 | `gates-40c1ad2-W26-3.3.log` | 51/51 | 512 / 266 | 1100 / 420 | 332/332 | 16/16 | 19/19 | OK | 10/0, 1/0 |

Every log also carries `check_fixtures.py` (25 documented hashes, 21 tracked
`.mpp`), the oracle-provenance check (8), the client-name scan (0 hits on HEAD
and on the working-tree content) and the 8-hex grep (no matches).  `app` is
16/16 from W26-3.1 on: the new `app_view_style` is the sixteenth test (29
checks at 3.1, 47 at 3.2, 54 at 3.3).  The core `viewstate` test grew from 791
checks to 820 (W26-3.1), 832 (W26-3.2) and 844 (W26-3.3).

## Found, not fixed

No new Critical or High finding in G3.  Notes for later work, all deliberate:

- **PROJECT_A's yellow row.**  The capture's row "client-text-032" has a yellow cell fill.  The file's Highlighted text-style
  record carries no background (its colour flag is 0xFF = automatic), so the
  fill is a per-row cell format that neither the style record nor the reader
  decodes.  The decoder and renderer DO support a stored highlight fill
  (synthetic-base's pale yellow (255,255,102) is in the committed fixture and
  asserted), but PROJECT_A's yellow row is not reproduced.  Its text still
  renders with the milestone style, as the capture shows.
- **TextStyles' COM Item numbering.**  The controlled saves prove the record
  layout, but Project's `TextStyles(Item=3)` wrote the style record at +506
  (MPXJ's "external" slot) rather than MPXJ's milestone slot at +186;
  `Item=7` wrote +90.  The mapping between the COM `PjTextItem` value and the
  file slot is therefore not established.  The decoder and renderer use the
  SLOT order, which the capture pins (the only italic-red slot is +186's, and
  it is the style Project draws on the milestone row); a later session should
  not "correct" the slots from the COM numbers.
- **Timescale top tier and counts.**  The controlled saves could not move the
  top tier or a count > 1 (see W26-3.3 detail); both fields are decoded from
  PROJECT_A's own record and rendered, but a COM-driven >1-count fixture does
  not exist.
- **GanttBarStyleEdit on "Task".**  Two saves naming the "Task" style (by
  index and by name) left the record byte-identical, while "Summary" and
  "Milestone" applied.  The bar record layout is proven on those two styles;
  the Task slot is the same stride and its decoded (138,187,237) is the
  colour the wave25-auto parity gate matches.
- **The port's timescale origin/zoom** still differs from the capture's
  scroll position.  G3's acceptance is the stored tiers' units, counts and
  formats, which match; the visible date range remains the zoom stream's
  work.
- The controlled-save probe scripts are local under `build/w26/g3/`
  (git-ignored) and cited by path; the committed fixtures reproduce from
  `tests/data/synthetic-base.mpp`, and the offsets are re-checkable from
  those saves.  Committing a generator script was not required for a DONE
  item and was not done.

WAVE-26-G3-COMPLETE

## G4 - backstage and Options

The measured sources are the clean `project-backstage-account.png` capture and
the audit frames in `corpus/reference/2026-10-23-audit/` (all local-only).  The
committed numbers are `tests/data/reference/w26-backstage.measures.json` and
`w26-options.measures.json`; the gate measures the LIVE widgets and the RENDERED
pixels against them.

| item | finding | status | evidence |
|---|---|---|---|
| W26-4.1 | File tab and rail styled as the Account capture: plain File tab, light rail, back arrow, Account under Close, Options pinned | **DONE** | commit `PENDING`; gates `build/w26/gates-4ca7821-W26-4.1.log` |
| W26-4.2 | Account page: identity, version, mouse-reachable About, Project's five Office Theme names (implemented ones enabled) | **DONE** | commit `PENDING`; gates `build/w26/gates-6fb6ec5-W26-4.2.log` |
| W26-4.3 | New page: Blank Project, New from existing, local Recent list, disabled online search with a note | **DONE** | commit `PENDING`; gates `build/w26/gates-fb5d37c-W26-4.3.log` |
| W26-4.4 | Open and Save As pages: Recent, This PC, Browse; OneDrive/SharePoint laid out and disabled | **DONE** | commit `PENDING`; gates `build/w26/gates-0102e8b-W26-4.4.log` |
| W26-4.5 | Print page: copies, printer list, settings (dates, pages, orientation, paper), live preview through the shared print layout | **DONE** | commit `PENDING`; gates `build/w26/gates-50204c6-W26-4.5.log` |
| W26-4.6 | Options: the 11-page dialog; General and the Schedule new-task mode functional; every other control laid out and disabled with the exact tooltip | pending | - |
