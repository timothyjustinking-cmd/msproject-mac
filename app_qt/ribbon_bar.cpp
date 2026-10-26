// app_qt/ribbon_bar.cpp -- see ribbon_bar.h.

#include "ribbon_bar.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayoutItem>
#include <QMargins>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPen>
#include <QPoint>
#include <QPolygon>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "command_status.h"
#include "project_dialogs.h"  // app::wbs_menu_items / baseline_menu_items
#include "ribbon_glyphs.h"
#include "shell_icons.h"
#include "shell_style.h"

namespace msqt {
namespace {

// ---------------------------------------------------------------------------
// Measured geometry (wave 26)
// ---------------------------------------------------------------------------
// The clean ribbon strips (local, git-ignored) put the group band at 89 px with
// three 22-px stack rows whose first top is 5 px below the band's top; a large
// button spans all three rows (its 64-px body sits one pixel inside the 66-px
// span).  The page keeps the wave-20 group height (95) and its 4-px top margin,
// so the band box is 91 px: 5 px offset + 66 px rows + a 6-px gap + the 14-px
// caption.  The numbers are pinned in
// tests/data/reference/w26-ribbon.measures.json and the inventory gate compares
// the live widget geometry with them.
constexpr int kStackRowHeight = 22;
constexpr int kStackRows = 3;
constexpr int kStackTopOffset = 5;
constexpr int kControlsHeight = kStackTopOffset + kStackRows * kStackRowHeight;
constexpr int kGroupBandHeight = kRibbonGroupHeight - 4;
constexpr int kLargeHeight = kRibbonButtonHeight;
constexpr int kGroupSeparatorWidth = 1;
constexpr int kLauncherSize = 14;
constexpr const char* kNotImplemented = "not implemented in this port";

// The 16 Gantt Chart Style bar colours, sampled from the clean Gantt Chart
// Format strip's gallery (one sample per swatch cell).  Only numbers are
// committed; the capture stays local.
constexpr std::array<std::uint32_t, 16> kGanttStylePalette = {
    0x74ccd4u, 0xd1368du, 0xab59a4u, 0x999999u, 0x72c1d3u, 0x4abda2u,
    0xa4c839u, 0x5db833u, 0x139aa1u, 0x258230u, 0xeeb221u, 0x587b1fu,
    0x1f5da9u, 0x705a9eu, 0x443f63u, 0x5875a3u,
};

enum class Kind {
    button,   // large command button (32-px icon over a two-line label)
    small,    // one 22-px stack row (small button or icon-only button)
    split,    // QToolButton with a menu (large or one-row shape)
    combo,    // QComboBox
    checkbox, // QCheckBox
    field,    // RibbonField (label + date field)
    gallery,  // RibbonGallery (16 swatches + expander)
};

struct ControlSpec {
    const char* label = "";             // display text ('\n' = two lines)
    app::MenuAction action = app::MenuAction::none;
    RibbonCommand command = RibbonCommand::none;
    Kind kind = Kind::button;
    int width = 40;
    int row = 0;                        // stack row (0..2); large spans all
    int col = 0;
    int row_span = 3;
    int col_span = 1;
    const char* items = nullptr;        // combo: '|'-separated values
    int checked = 0;                    // checkbox: 0/1, -1 = document state
    bool enabled = false;
    bool icon_only = false;             // small button: no visible text
    // W26-2: the pin's icon key (ribbon_pin.tsv, icon column).  Every command
    // control carries one; the icon gate hashes the live widget's rendered
    // glyph and the inventory gate compares this key with the pin.
    const char* icon = nullptr;
};

struct GroupSpec {
    const char* title = "";
    int width = 0;                      // measured group rect, separator included
    std::vector<ControlSpec> controls;
    const char* launcher = nullptr;     // dialog launcher, bottom right
};

struct TabSpec {
    const char* title = "";
    bool contextual = false;
    std::vector<GroupSpec> groups;
};

GroupSpec& add_group(TabSpec& tab, const char* title, int width) {
    tab.groups.push_back(GroupSpec{});
    GroupSpec& group = tab.groups.back();
    group.title = title;
    group.width = width;
    return group;
}

ControlSpec& add(GroupSpec& group, const char* label, Kind kind, int width, int row, int col,
                 int row_span, int col_span, app::MenuAction action, RibbonCommand command,
                 const char* items, int checked, bool enabled, bool icon_only,
                 const char* icon = nullptr) {
    group.controls.push_back(ControlSpec{});
    ControlSpec& control = group.controls.back();
    control.label = label;
    control.kind = kind;
    control.width = width;
    control.row = row;
    control.col = col;
    control.row_span = row_span;
    control.col_span = col_span;
    control.action = action;
    control.command = command;
    control.items = items;
    control.checked = checked;
    control.enabled = enabled;
    control.icon_only = icon_only;
    control.icon = icon;
    return control;
}

GroupSpec& large(GroupSpec& group, const char* label, app::MenuAction action, int width, int col,
                 bool enabled = false, const char* icon = nullptr,
                 RibbonCommand command = RibbonCommand::none) {
    add(group, label, Kind::button, width, 0, col, 3, 1, action, command, nullptr, 0,
        enabled, false, icon);
    return group;
}

GroupSpec& split(GroupSpec& group, const char* label, int width, int row, int col,
                 int row_span = 3, const char* icon = nullptr, bool enabled = false,
                 RibbonCommand command = RibbonCommand::none) {
    add(group, label, Kind::split, width, row, col, row_span, 1, app::MenuAction::none,
        command, nullptr, 0, enabled, false, icon);
    return group;
}

GroupSpec& small(GroupSpec& group, const char* label, app::MenuAction action, int width, int row,
                 int col, bool enabled = false, int col_span = 1, bool icon_only = false,
                 const char* icon = nullptr, RibbonCommand command = RibbonCommand::none) {
    add(group, label, Kind::small, width, row, col, 1, col_span, action, command,
        nullptr, 0, enabled, icon_only, icon);
    return group;
}

GroupSpec& small_cmd(GroupSpec& group, const char* label, RibbonCommand command, int width,
                     int row, int col, bool enabled = false, bool icon_only = false,
                     const char* icon = nullptr) {
    add(group, label, Kind::small, width, row, col, 1, 1, app::MenuAction::none, command,
        nullptr, 0, enabled, icon_only, icon);
    return group;
}

GroupSpec& combo(GroupSpec& group, const char* label, const char* value, int width, int row,
                 int col, bool enabled = false) {
    add(group, label, Kind::combo, width, row, col, 1, 1, app::MenuAction::none,
        RibbonCommand::none, value, 0, enabled, false);
    return group;
}

GroupSpec& check(GroupSpec& group, const char* label, RibbonCommand command, int width, int row,
                 int col, bool enabled = false, int checked = 0) {
    add(group, label, Kind::checkbox, width, row, col, 1, 1, app::MenuAction::none, command,
        nullptr, checked, enabled, false);
    return group;
}

GroupSpec& field(GroupSpec& group, const char* label, int width, int row,
                                 int col) {
    add(group, label, Kind::field, width, row, col, 1, 1, app::MenuAction::none,
        RibbonCommand::none, "NA", 0, false, false);
    return group;
}

GroupSpec& gallery(GroupSpec& group, int width, int col) {
    add(group, "Gantt Chart Style", Kind::gallery, width, 0, col, 3, 1, app::MenuAction::none,
        RibbonCommand::none, "16", 0, false, false);
    return group;
}

GroupSpec& launcher(GroupSpec& group, const char* label) {
    group.launcher = label;
    return group;
}

// ---------------------------------------------------------------------------
// The ribbon structure: measured group order, measured group widths and the
// controls Project shows (docs/handoff/audit-w22/A2-fable-readback.md plus the
// clean loaded capture for the Task tab).  Only commands this port implements
// are enabled; every other control is disabled with the exact decision-9
// tooltip.  The `enabled` set is the independent pin's.
// ---------------------------------------------------------------------------
const std::vector<TabSpec>& tab_specs() {
    static const std::vector<TabSpec> kTabs = [] {
        std::vector<TabSpec> tabs;

        // --- Task -----------------------------------------------------------
        TabSpec task;
        task.title = "Task";
        GroupSpec& task_view = add_group(task, "View", 58);
        large(task_view, "Gantt\nChart", app::MenuAction::none, 50, 0, false, "view.gantt");
        GroupSpec& clipboard = add_group(task, "Clipboard", 171);
        large(clipboard, "Paste", app::MenuAction::paste, 38, 0, true, "clipboard.paste");
        large(clipboard, "Cut", app::MenuAction::cut, 36, 1, true, "clipboard.cut");
        large(clipboard, "Copy", app::MenuAction::copy, 38, 2, true, "clipboard.copy");
        large(clipboard, "Format\nPainter", app::MenuAction::none, 48, 3, false,
             "clipboard.painter");
        GroupSpec& font = add_group(task, "Font", 177);
        // W26-1.1: the font value displays are the FILE's default text style
        // when decoded (G3-3.1), else Project's new-project default.  The text
        // style records are not decoded yet, so the captured new-project
        // default (Calibri 11, the UI contract's own reference typography) is
        // shown; the values are displays, not an edit of the file.
        combo(font, "font name", "Calibri", 66, 0, 0);
        combo(font, "font size", "11", 34, 0, 1);
        // W26-2.1: B/I/U are icon-only, like the letters Project shows: the
        // glyph is the letter, so the label is the accessible name only.
        small(font, "B", app::MenuAction::none, 20, 0, 2, false, 1, true, "font.bold");
        small(font, "I", app::MenuAction::none, 20, 0, 3, false, 1, true, "font.italic");
        small(font, "U", app::MenuAction::none, 20, 0, 4, false, 1, true, "font.underline");
        split(font, "highlight colour", 26, 1, 0, 1, "font.highlight");
        split(font, "font colour", 26, 1, 1, 1, "font.color");
        launcher(font, "Font");
        GroupSpec& schedule = add_group(task, "Schedule", 282);
        // The five percentage buttons are icon-only like the readback's [s]
        // marks; each glyph is a ring filled to its percentage.  W28-1.2: they
        // are ENABLED on the measured actuals writer
        // (app::make_percent_edit -> mppwrite::write_percent_actuals), which
        // reproduces Project's own save pairs byte for byte
        // (tests/data/wave28-percent-*.mpp): ActualDuration, RemainingDuration,
        // ActualStart, the measured dates, the progress flag, the project row's
        // roll-up and the Var2Data steps -- not the old one-word +92 write.
        small_cmd(schedule, "0%", RibbonCommand::percent_0, 18, 0, 0, true, true,
                  "schedule.percent_0");
        small_cmd(schedule, "25%", RibbonCommand::percent_25, 20, 0, 1, true, true,
                  "schedule.percent_25");
        small_cmd(schedule, "50%", RibbonCommand::percent_50, 20, 0, 2, true, true,
                  "schedule.percent_50");
        small_cmd(schedule, "75%", RibbonCommand::percent_75, 20, 0, 3, true, true,
                  "schedule.percent_75");
        small_cmd(schedule, "100%", RibbonCommand::percent_100, 26, 0, 4, true, true,
                  "schedule.percent_100");
        split(schedule, "Mark on Track", 64, 0, 5, 3, "schedule.mark_on_track",
              true, RibbonCommand::mark_on_track);
        small(schedule, "Respect Links", app::MenuAction::none, 96, 0, 6, false, 1, false,
              "schedule.respect_links");
        small(schedule, "Inactivate", app::MenuAction::none, 96, 1, 6, false, 1, false,
              "schedule.inactivate");
        // W28-2.1: Indent/Outdent are ENABLED on the measured outline writer
        // (app::make_outline_edit -> mppwrite::write_outline_edits), which
        // reproduces Project's own indent/summary/outdent save pairs.  The
        // handler refuses the shapes Project's pairs did not measure (a first
        // top-level row, a summary outdent) with a status message; it never
        // queues a write for them.
        small_cmd(schedule, "outdent", RibbonCommand::outdent, 18, 1, 0, true, true,
                  "schedule.outdent");
        small_cmd(schedule, "indent", RibbonCommand::indent, 20, 1, 1, true, true,
                  "schedule.indent");
        small_cmd(schedule, "link tasks", RibbonCommand::link_tasks, 20, 1, 2, true, true,
                  "schedule.link");
        small_cmd(schedule, "unlink tasks", RibbonCommand::unlink_tasks, 20, 1, 3, true, true,
                  "schedule.unlink");
        small_cmd(schedule, "split task", RibbonCommand::split_task, 26, 1, 4, false, true,
                  "schedule.split");
        GroupSpec& tasks = add_group(task, "Tasks", 269);
        // W30-1.0: Manually Schedule and Mode are ENABLED on the measured
        // automatic -> manual task-mode writer (app::make_task_mode_edit ->
        // mppwrite::write_task_mode), which reproduces Project's own save pair
        // (tests/data/wave30-task-mode-*.mpp) byte for byte.  The handler
        // refuses every shape the pair did not measure.  Auto Schedule stays
        // disabled with decision 9's tooltip: the reverse pair shows Project
        // also reschedules the row, and this port has no scheduler.
        large(tasks, "Manually\nSchedule", app::MenuAction::none, 52, 0, true,
              "tasks.manual", RibbonCommand::manually_schedule);
        large(tasks, "Auto\nSchedule", app::MenuAction::none, 50, 1, false, "tasks.auto");
        large(tasks, "Inspect", app::MenuAction::none, 52, 2, false, "tasks.inspect");
        large(tasks, "Move", app::MenuAction::none, 38, 3, false, "tasks.move");
        large(tasks, "Mode", app::MenuAction::none, 40, 4, true, "tasks.mode",
              RibbonCommand::task_mode);
        GroupSpec& insert = add_group(task, "Insert", 240);
        large(insert, "Task", app::MenuAction::insert_task, 48, 0, true, "insert.task");
        large(insert, "Summary", app::MenuAction::none, 56, 1, false, "insert.summary");
        large(insert, "Milestone", app::MenuAction::none, 58, 2, false, "insert.milestone");
        large(insert, "Deliverable", app::MenuAction::none, 58, 3, false, "insert.deliverable");
        GroupSpec& properties = add_group(task, "Properties", 204);
        large(properties, "Information", app::MenuAction::task_fields, 58, 0, true,
              "properties.information");
        small(properties, "Notes", app::MenuAction::none, 130, 0, 1, false, 1, false,
              "properties.notes");
        // W30-1.5: Project's Details opens the Task Form details pane, which
        // this port already has (decision 16).  The button SHOWS the pane;
        // the View tab's check box stays the toggle.
        small(properties, "Details", app::MenuAction::none, 130, 1, 1, true, 1,
              false, "properties.details", RibbonCommand::task_details);
        small(properties, "Add to Timeline", app::MenuAction::none, 130, 2, 1, false, 1, false,
              "properties.add_to_timeline");
        GroupSpec& editing = add_group(task, "Editing", 140);
        // W30-1.7: the sheet scroll + chart pan; a view command (no file write).
        large(editing, "Scroll\nto Task", app::MenuAction::none, 66, 0, true,
              "editing.scroll_to_task", RibbonCommand::scroll_to_task);
        small(editing, "Find", app::MenuAction::show_filter, 60, 0, 1, true, 1, false,
              "editing.find");
        small(editing, "Clear", app::MenuAction::clear_filter, 60, 1, 1, true, 1, false,
              "editing.clear");
        small(editing, "Fill", app::MenuAction::none, 60, 2, 1, false, 1, false,
              "editing.fill");
        tabs.push_back(std::move(task));

        // --- Resource -------------------------------------------------------
        TabSpec resource;
        resource.title = "Resource";
        GroupSpec& resource_view = add_group(resource, "View", 66);
        large(resource_view, "Team\nPlanner", app::MenuAction::none, 55, 0, false,
              "view.team_planner");
        GroupSpec& assignments = add_group(resource, "Assignments", 129);
        large(assignments, "Assign\nResources", app::MenuAction::none, 58, 0, false,
              "assignments.assign");
        large(assignments, "Resource\nPool", app::MenuAction::none, 58, 1, false,
              "assignments.pool");
        GroupSpec& resource_insert = add_group(resource, "Insert", 85);
        large(resource_insert, "Add\nResources", app::MenuAction::insert_resource, 74, 0, false,
              "insert.add_resources");
        GroupSpec& resource_properties = add_group(resource, "Properties", 172);
        large(resource_properties, "Information", app::MenuAction::none, 58, 0, false,
              "properties.resource_information");
        small(resource_properties, "Notes", app::MenuAction::none, 100, 0, 1, false, 1, false,
              "properties.notes");
        small(resource_properties, "Details", app::MenuAction::none, 100, 1, 1, false, 1, false,
              "properties.resource_details");
        GroupSpec& level = add_group(resource, "Level", 307);
        large(level, "Level\nSelection", app::MenuAction::none, 60, 0, false,
              "level.selection");
        large(level, "Level\nResource", app::MenuAction::none, 56, 1, false,
              "level.resource");
        large(level, "Level\nAll", app::MenuAction::none, 46, 2, false, "level.all");
        small(level, "Leveling Options", app::MenuAction::none, 100, 0, 3, false, 1, false,
              "level.options");
        small(level, "Clear Leveling", app::MenuAction::none, 100, 1, 3, false, 1, false,
              "level.clear");
        small(level, "Next Overallocation", app::MenuAction::none, 100, 2, 3, false, 1, false,
              "level.next_overallocation");
        tabs.push_back(std::move(resource));

        // --- Report ---------------------------------------------------------
        TabSpec report;
        report.title = "Report";
        // W29-4.1: both open the captured dialogs; neither engine exists, so the
        // dialogs are laid out and inert (Close/Cancel only).
        GroupSpec& report_project = add_group(report, "Project", 70);
        large(report_project, "Compare\nProjects", app::MenuAction::none, 60, 0, true,
              "project.compare", RibbonCommand::compare_projects);
        GroupSpec& view_reports = add_group(report, "View Reports", 453);
        large(view_reports, "New\nReport", app::MenuAction::none, 48, 0, false, "reports.new");
        large(view_reports, "Dashboards", app::MenuAction::none, 56, 1, false,
              "reports.dashboards");
        large(view_reports, "Resources", app::MenuAction::none, 50, 2, false,
              "reports.resources");
        large(view_reports, "Costs", app::MenuAction::none, 40, 3, false, "reports.costs");
        large(view_reports, "In\nProgress", app::MenuAction::none, 48, 4, false,
              "reports.in_progress");
        large(view_reports, "Getting\nStarted", app::MenuAction::none, 50, 5, false,
              "reports.getting_started");
        large(view_reports, "Custom", app::MenuAction::none, 44, 6, false, "reports.custom");
        large(view_reports, "Recent", app::MenuAction::none, 44, 7, false, "reports.recent");
        GroupSpec& report_export = add_group(report, "Export", 61);
        large(report_export, "Visual\nReports", app::MenuAction::none, 52, 0, true,
              "export.visual_reports", RibbonCommand::visual_reports);
        tabs.push_back(std::move(report));

        // --- Project --------------------------------------------------------
        TabSpec project;
        project.title = "Project";
        GroupSpec& project_insert = add_group(project, "Insert", 77);
        large(project_insert, "Subproject", app::MenuAction::none, 64, 0, false,
              "insert.subproject");
        GroupSpec& add_ins = add_group(project, "Add-ins", 135);
        small(add_ins, "Get Add-ins", app::MenuAction::none, 126, 0, 0, false, 1, false,
              "addins.get");
        small(add_ins, "My Add-ins", app::MenuAction::none, 126, 1, 0, false, 1, false,
              "addins.my");
        GroupSpec& project_properties = add_group(project, "Properties", 341);
        add(project_properties, "Project\nInformation", Kind::button, 60, 0, 0, 3, 1,
            app::MenuAction::none, RibbonCommand::project_information, nullptr, 0, true,
            false, "properties.project_information");
        large(project_properties, "Custom\nFields", app::MenuAction::custom_fields, 50, 1, true,
              "properties.custom_fields");
        add(project_properties, "Links Between\nProjects", Kind::button, 68, 0, 2, 3, 1,
            app::MenuAction::none, RibbonCommand::links_between_projects, nullptr, 0, true,
            false, "properties.links_between");
        split(project_properties, "WBS", 40, 0, 3, 3, "properties.wbs", true,
              RibbonCommand::wbs_menu);
        add(project_properties, "Change\nWorking Time", Kind::button, 68, 0, 4, 3, 1,
            app::MenuAction::none, RibbonCommand::change_working_time, nullptr, 0, true,
            false, "properties.change_working_time");
        GroupSpec& project_schedule = add_group(project, "Schedule", 175);
        large(project_schedule, "Calculate\nProject", app::MenuAction::none, 52, 0, false,
              "schedule.calculate");
        split(project_schedule, "Set\nBaseline", 48, 0, 1, 3, "schedule.set_baseline", true,
              RibbonCommand::set_baseline_menu);
        add(project_schedule, "Move\nProject", Kind::button, 46, 0, 2, 3, 1,
            app::MenuAction::none, RibbonCommand::move_project, nullptr, 0, true, false,
            "schedule.move");
        GroupSpec& status = add_group(project, "Status", 133);
        field(status, "Status Date", 126, 0, 0);
        small_cmd(status, "Update Project", RibbonCommand::update_project, 100, 1, 0, true,
                  false, "status.update");
        GroupSpec& proofing = add_group(project, "Proofing", 63);
        large(proofing, "Spelling", app::MenuAction::none, 52, 0, false, "proofing.spelling");
        tabs.push_back(std::move(project));

        // --- View -----------------------------------------------------------
        TabSpec view;
        view.title = "View";
        GroupSpec& task_views = add_group(view, "Task Views", 264);
        large(task_views, "Gantt\nChart", app::MenuAction::none, 64, 0, false, "view.gantt");
        large(task_views, "Task\nUsage", app::MenuAction::view_task_usage, 58, 1, true,
              "view.task_usage");
        small(task_views, "Network Diagram", app::MenuAction::none, 130, 0, 2, false, 1, false,
              "view.network_diagram");
        small(task_views, "Calendar", app::MenuAction::none, 130, 1, 2, false, 1, false,
              "view.calendar");
        small(task_views, "Other Views", app::MenuAction::none, 130, 2, 2, false, 1, false,
              "view.other");
        GroupSpec& resource_views = add_group(view, "Resource Views", 214);
        large(resource_views, "Team\nPlanner", app::MenuAction::none, 64, 0, false,
              "view.team_planner");
        small(resource_views, "Resource Usage", app::MenuAction::view_resource_usage, 138, 0, 1,
              true, 1, false, "view.resource_usage");
        small(resource_views, "Resource Sheet", app::MenuAction::view_resource_sheet, 138, 1, 1,
              true, 1, false, "view.resource_sheet");
        small(resource_views, "Other Views", app::MenuAction::none, 138, 2, 1, false, 1, false,
              "view.other");
        GroupSpec& data = add_group(view, "Data", 323);
        large(data, "Sort", app::MenuAction::none, 50, 0, false, "data.sort");
        large(data, "Outline", app::MenuAction::none, 50, 1, false, "data.outline");
        large(data, "Tables", app::MenuAction::none, 50, 2, false, "data.tables");
        combo(data, "Highlight", "No Highlight", 156, 0, 3);
        combo(data, "Filter", "No Filter", 156, 1, 3);
        combo(data, "Group by", "No Group", 156, 2, 3);
        GroupSpec& zoom = add_group(view, "Zoom", 248);
        combo(zoom, "Timescale", "Days", 96, 0, 0);
        large(zoom, "Zoom", app::MenuAction::none, 46, 1, false, "zoom.zoom");
        large(zoom, "Entire\nProject", app::MenuAction::none, 42, 2, false, "zoom.entire");
        large(zoom, "Selected\nTasks", app::MenuAction::none, 50, 3, false, "zoom.selected");
        GroupSpec& split_view = add_group(view, "Split View", 185);
        check(split_view, "Timeline", RibbonCommand::toggle_timeline, 64, 0, 0, true, -1);
        combo(split_view, "Timeline options", "Timeline", 112, 0, 1);
        check(split_view, "Details", RibbonCommand::toggle_details, 64, 1, 0, true, -1);
        combo(split_view, "Details options", "", 112, 1, 1);
        GroupSpec& window = add_group(view, "Window", 198);
        large(window, "New\nWindow", app::MenuAction::none, 62, 0, false, "window.new");
        small(window, "Switch\nWindows", app::MenuAction::none, 128, 0, 1, false, 1, false,
              "window.switch");
        small(window, "Arrange All", app::MenuAction::none, 128, 1, 1, false, 1, false,
              "window.arrange");
        small(window, "Hide", app::MenuAction::none, 128, 2, 1, false, 1, false, "window.hide");
        GroupSpec& macros = add_group(view, "Macros", 60);
        large(macros, "Macros", app::MenuAction::none, 50, 0, false, "macros.macros");
        tabs.push_back(std::move(view));

        // --- Help -----------------------------------------------------------
        TabSpec help;
        help.title = "Help";
        GroupSpec& help_group = add_group(help, "Help", 156);
        large(help_group, "Help", app::MenuAction::none, 48, 0, false, "help.help");
        large(help_group, "Feedback", app::MenuAction::none, 50, 1, false, "help.feedback");
        large(help_group, "Show\nTraining", app::MenuAction::none, 48, 2, false,
              "help.training");
        tabs.push_back(std::move(help));

        // --- Gantt Chart Format (contextual) -------------------------------
        TabSpec format;
        format.title = "Gantt Chart Format";
        format.contextual = true;
        GroupSpec& format_group = add_group(format, "Format", 153);
        // W29-2.1: both open live dialogs.  Text Styles and Bar Styles apply to
        // the view for the session; Gridlines is laid out and says so.
        large(format_group, "Text\nStyles", app::MenuAction::none, 48, 0, true,
              "format.text_styles", RibbonCommand::text_styles);
        large(format_group, "Gridlines", app::MenuAction::none, 48, 1, true,
              "format.gridlines", RibbonCommand::gridlines);
        large(format_group, "Layout", app::MenuAction::none, 44, 2, false, "format.layout");
        GroupSpec& columns = add_group(format, "Columns", 198);
        large(columns, "Insert\nColumn", app::MenuAction::none, 56, 0, false, "columns.insert");
        small_cmd(columns, "align left", RibbonCommand::none, 24, 0, 1, false, true,
                  "columns.align_left");
        small_cmd(columns, "align centre", RibbonCommand::none, 24, 0, 2, false, true,
                  "columns.align_centre");
        small_cmd(columns, "align right", RibbonCommand::none, 24, 0, 3, false, true,
                  "columns.align_right");
        small_cmd(columns, "wrap text", RibbonCommand::none, 24, 0, 4, false, true,
                  "columns.wrap");
        small(columns, "Column Settings", app::MenuAction::none, 102, 1, 1, false, 4, false,
              "columns.settings");
        small(columns, "Custom Fields", app::MenuAction::custom_fields, 102, 2, 1, true, 4, false,
              "properties.custom_fields");
        GroupSpec& bar_styles = add_group(format, "Bar Styles", 307);
        // The split's main button opens the Bar Styles dialog (Project's own
        // click); its drop-down menu has no measured items in this port.
        split(bar_styles, "Format", 50, 0, 0, 3, "bar_styles.format", true,
              RibbonCommand::bar_styles);
        check(bar_styles, "Critical Tasks", RibbonCommand::toggle_critical_tasks, 96, 0, 1,
              true);
        check(bar_styles, "Slack", RibbonCommand::none, 96, 1, 1);
        check(bar_styles, "Late Tasks", RibbonCommand::none, 96, 2, 1);
        large(bar_styles, "Task Path", app::MenuAction::none, 46, 2, false,
              "bar_styles.task_path");
        large(bar_styles, "Baseline", app::MenuAction::none, 46, 3, false,
              "bar_styles.baseline");
        large(bar_styles, "Slippage", app::MenuAction::none, 46, 4, false,
              "bar_styles.slippage");
        GroupSpec& style = add_group(format, "Gantt Chart Style", 933);
        gallery(style, 920, 0);
        launcher(style, "Gantt Chart Style");
        GroupSpec& show_hide = add_group(format, "Show/Hide", 161);
        check(show_hide, "Outline Number", RibbonCommand::none, 150, 0, 0);
        check(show_hide, "Project Summary Task", RibbonCommand::toggle_project_summary, 150, 1, 0,
              true, -1);
        check(show_hide, "Summary Tasks", RibbonCommand::none, 150, 2, 0, false, 1);
        GroupSpec& drawings = add_group(format, "Drawings", 65);
        split(drawings, "Drawing", 54, 0, 0, 3, "drawings.drawing");
        tabs.push_back(std::move(format));

        return tabs;
    }();
    return kTabs;
}

QFont ribbon_font(int point_size, bool bold = false) {
    QFont font;
    font.setPointSize(point_size);
    font.setBold(bold);
    return font;
}

// A small square glyph for the dialog launcher (an expand arrow), drawn by us.
QIcon launcher_icon() {
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(QColor(0x44, 0x44, 0x44), 1));
    painter.drawLine(2, 10, 9, 3);
    painter.drawLine(9, 3, 9, 7);
    painter.drawLine(9, 3, 5, 3);
    return QIcon(pixmap);
}

// A gallery swatch: a bar in the measured colour with its darker outline, the
// shape Project's Gantt Chart Style gallery uses.
QIcon swatch_icon(const QColor& colour) {
    QPixmap pixmap(52, 30);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(colour.darker(130), 1));
    painter.setBrush(colour);
    painter.drawRect(QRect(2, 9, 42, 9));
    QPolygon diamond;
    diamond << QPoint(8, 5) << QPoint(13, 13) << QPoint(8, 21) << QPoint(3, 13);
    painter.drawPolygon(diamond);
    return QIcon(pixmap);
}

// The live widget classes the inventory gate classifies.  They are real
// QWidget/QToolButton subclasses (no Q_OBJECT, so no AUTOMOC): control_kind()
// reads the class, never a name or a spec field.
class RibbonSmallButton final : public QToolButton {
  public:
    using QToolButton::QToolButton;
};

class RibbonSplitButton final : public QToolButton {
  public:
    using QToolButton::QToolButton;
};

class RibbonLauncher final : public QToolButton {
  public:
    using QToolButton::QToolButton;
};

class RibbonGallery final : public QWidget {
  public:
    using QWidget::QWidget;
};

class RibbonField final : public QWidget {
  public:
    using QWidget::QWidget;
};

// The small stack button: Project's one-row command shape.  Icon-only commands
// carry their pin label as the accessible name (the gate compares that); a text
// command shows the label beside a 16-px glyph.
void build_small_button(RibbonSmallButton* button, const ControlSpec& spec, QWidget* parent,
                        const QColor& ink) {
    button->setObjectName(QStringLiteral("ribbonSmallButton"));
    button->setFont(ribbon_font(kRibbonPointSize));
    button->setFixedHeight(kStackRowHeight);
    button->setFixedWidth(spec.width);
    button->setFocusPolicy(Qt::NoFocus);
    button->setAutoRaise(true);
    button->setAccessibleName(QString::fromUtf8(spec.label));
    // W26-2: every command's icon is the pin's key resolved through the
    // ribbon glyph library; an unknown/empty key draws the neutral placeholder,
    // which the icon gate rejects by pixel hash.
    const QString icon_key = QString::fromUtf8(spec.icon != nullptr ? spec.icon : "");
    if (spec.icon_only) {
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIcon(shell_ribbon_icon(icon_key, 16, ink));
        button->setIconSize(QSize(16, 16));
        button->setText(QString());
    } else {
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIcon(shell_ribbon_icon(icon_key, 16, ink));
        button->setIconSize(QSize(16, 16));
        button->setText(QString::fromUtf8(spec.label));
    }
    button->setEnabled(spec.enabled);
    const QString tip = spec.enabled ? QString::fromUtf8(spec.label)
                                     : QString::fromUtf8(kNotImplemented);
    button->setToolTip(tip);
    button->setParent(parent);
}

}  // namespace

// ---------------------------------------------------------------------------
// RibbonBar
// ---------------------------------------------------------------------------

RibbonBar::RibbonBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("ribbon"));
    setFixedHeight(kRibbonHeight + 1);
    // Ignored horizontally: the ribbon's natural width is the full 1920-px
    // reference's group row, far wider than the 900-px test window.  Ignoring
    // the size hint keeps the WINDOW at 900x560; groups that do not fit are
    // dropped whole by layout_page_groups().
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setMinimumWidth(0);
    setAutoFillBackground(false);

    // One stylesheet: tabs look like tabs, group captions are small and grey,
    // and the buttons are flat until hovered.  The wave-26 small row has its own
    // object name so the legacy large-button gates cannot mistake it for the
    // 64-px shape.
    //
    // WAVE 26 (W26-4.1, audit B-7): Project 2021 draws "File" as a PLAIN tab on
    // the tab row (the clean ribbon strips), not as a filled accent tile.  The
    // old dark-blue block is gone; the tab uses the same measured padding and
    // colours as Task..Help.
    //
    // W26-6.2: the colours now come from chrome_palette(theme_); the stylesheet
    // is (re)built by apply_theme_style() at the end of the constructor and on
    // every theme change, so the chrome repaints live.

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    tab_row_ = new QWidget(this);
    tab_row_->setFixedHeight(kRibbonTabHeight);
    auto* tab_layout = new QHBoxLayout(tab_row_);
    tab_layout->setContentsMargins(8, 0, 8, 0);
    tab_layout->setSpacing(0);
    outer->addWidget(tab_row_);

    rule_ = new QFrame(this);
    rule_->setFrameShape(QFrame::HLine);
    rule_->setFrameShadow(QFrame::Plain);
    rule_->setFixedHeight(1);
    outer->addWidget(rule_);

    page_ = new QWidget(this);
    page_->setFixedHeight(kRibbonGroupHeight);
    page_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    page_layout_ = new QHBoxLayout(page_);
    page_layout_->setContentsMargins(8, 4, 8, 0);
    page_layout_->setSpacing(0);
    outer->addWidget(page_);
    outer->addStretch(0);

    // The group-row overflow chevron: one ">>" at the right edge instead of
    // wrapping the groups onto a second row.
    overflow_ = new QToolButton(page_);
    overflow_->setObjectName(QStringLiteral("ribbonOverflow"));
    overflow_->setText(QStringLiteral(">>"));
    overflow_->setToolTip(QStringLiteral("More commands"));
    overflow_->setFixedSize(30, 24);
    overflow_->setFocusPolicy(Qt::NoFocus);
    overflow_->hide();
    connect(overflow_, &QToolButton::clicked, this, [this](bool) {
        QMenu menu(this);
        for (const GroupWidgets& group : page_groups_) {
            if (group.box != nullptr && !group.box->isVisible()) {
                QAction* action = menu.addAction(QStringLiteral("More"));
                action->setEnabled(false);
                (void)action;
            }
        }
        menu.exec(mapToGlobal(QPoint(width() - 40, kRibbonTabHeight + 6)));
    });

    tab_overflow_ = new QToolButton(tab_row_);
    tab_overflow_->setObjectName(QStringLiteral("ribbonTabOverflow"));
    tab_overflow_->setText(QStringLiteral(">>"));
    tab_overflow_->setToolTip(QStringLiteral("More tabs"));
    tab_overflow_->setFixedSize(30, kRibbonTabHeight - 4);
    tab_overflow_->setFocusPolicy(Qt::NoFocus);
    tab_overflow_->hide();

    build_tabs();
    build_page(0);
    apply_theme_style();
}

RibbonBar::~RibbonBar() = default;

void RibbonBar::set_theme(Theme theme) {
    if (theme_ == theme) {
        return;
    }
    theme_ = theme;
    apply_theme_style();
    // W29-5.1: the one-ink glyphs follow the theme too.  The icons were built
    // with the previous theme's ink, so the active page's icon-bearing controls
    // are re-rendered in the new one (live, no page rebuild).
    apply_icon_ink();
}

void RibbonBar::apply_icon_ink() {
    const QColor ink = chrome_palette(theme_).ribbon_ink;
    for (std::size_t group = 0; group < page_controls_.size() && group < page_icons_.size();
         ++group) {
        const std::vector<QWidget*>& controls = page_controls_[group];
        const std::vector<QString>& keys = page_icons_[group];
        for (std::size_t index = 0; index < controls.size() && index < keys.size(); ++index) {
            if (keys[index].isEmpty()) {
                continue;  // combos, fields, check boxes, gallery, launchers
            }
            auto* button = qobject_cast<QToolButton*>(controls[index]);
            if (button == nullptr) {
                continue;
            }
            const int pixels = button->iconSize().width() >= 24 ? 32 : 16;
            button->setIcon(shell_ribbon_icon(keys[index], pixels, ink));
        }
    }
}

void RibbonBar::apply_theme_style() {
    const ChromePalette& c = chrome_palette(theme_);
    // Every colour below is one of the palette fields (see theme.h).  The tab
    // row and the group row are separate surfaces in the dark captures, so they
    // get separate label colours; light keeps both at the measured #232323.
    const QString tab_text = c.ribbon_tab_text.name();
    const QString tab_active = c.ribbon_tab_active.name();
    const QString border = c.ribbon_border.name();
    const QString contextual = c.ribbon_contextual_text.name();
    const QString tab_hover = c.ribbon_tab_background.lighter(112).name();
    const QString hover = c.ribbon_hover.name();
    const QString text = c.ribbon_text.name();
    const QString muted = c.ribbon_muted.name();
    const QString field = c.ribbon_field_background.name();
    const QString field_border = c.ribbon_field_border.name();
    setStyleSheet(
        QStringLiteral(
            "QToolButton#ribbonFileTab { border: none; padding: 2px 10px;"
            " color: %1; background: transparent; }"
            "QToolButton#ribbonFileTab:hover { background: %2; }"
            "QToolButton#ribbonTab { border: none; padding: 2px 10px; color: %1;"
            " background: transparent; }"
            "QToolButton#ribbonTab:hover { background: %2; }"
            "QToolButton#ribbonTab:checked { background: %3; border: 1px solid %4;"
            " border-bottom: 1px solid %3; }"
            "QToolButton#ribbonTabContextual { border: none; padding: 2px 10px; color: %5;"
            " background: transparent; }"
            "QToolButton#ribbonTabContextual:checked { background: %3;"
            " border: 1px solid %4; border-bottom: 1px solid %3; }"
            "QToolButton#ribbonButton { border: 1px solid transparent; padding: 1px 4px;"
            " color: %6; background: transparent; }"
            "QToolButton#ribbonButton:hover { background: %7; border: 1px solid %7; }"
            "QToolButton#ribbonSmallButton { border: 1px solid transparent; padding: 0px 2px;"
            " color: %6; background: transparent; }"
            "QToolButton#ribbonSmallButton:hover { background: %7;"
            " border: 1px solid %7; }"
            "QToolButton#ribbonSplitButton { border: 1px solid transparent; padding: 1px 4px;"
            " color: %6; background: transparent; }"
            "QToolButton#ribbonSwatch { border: 1px solid %4; background: %8;"
            " padding: 0px; }"
            "QToolButton#ribbonGalleryExpander { border: none; color: %6; }"
            "QToolButton#ribbonLauncher { border: none; }"
            "QToolButton#ribbonOverflow { color: %6; }"
            "QToolButton#ribbonTabOverflow { color: %1; }"
            "QLabel#ribbonGroupLabel { color: %9; }"
            "QComboBox#ribbonCombo { border: 1px solid %10; background: %8;"
            " color: %6; padding-left: 3px; }"
            "QCheckBox#ribbonCheck { color: %6; }"
            "QAbstractButton { border-radius: 2px; }")
            .arg(tab_text, tab_hover, tab_active, border, contextual, text, hover, field,
                 muted, field_border));
    if (rule_ != nullptr) {
        rule_->setStyleSheet(QStringLiteral("background: %1;").arg(border));
    }
    for (const GroupWidgets& group : page_groups_) {
        if (group.separator != nullptr) {
            group.separator->setStyleSheet(QStringLiteral("background: %1;").arg(border));
        }
    }
    update();
}

QSize RibbonBar::sizeHint() const { return QSize(900, kRibbonHeight + 1); }

QSize RibbonBar::minimumSizeHint() const { return QSize(320, kRibbonHeight + 1); }

void RibbonBar::set_action_handler(std::function<void(app::MenuAction)> handler) {
    handler_ = std::move(handler);
}

void RibbonBar::set_command_handler(
    std::function<void(RibbonCommand, bool)> handler) {
    command_handler_ = std::move(handler);
}

void RibbonBar::set_file_handler(std::function<void()> handler) {
    file_handler_ = std::move(handler);
}

QString RibbonBar::tell_me_text() const {
    return tell_me_ != nullptr ? tell_me_->text() : QString();
}

void RibbonBar::set_document_state(bool has_document, bool timeline_shown,
                                   bool project_summary_shown) {
    has_document_ = has_document;
    timeline_checked_ = timeline_shown;
    project_summary_checked_ = project_summary_shown;
    sync_document_checkboxes();
}

void RibbonBar::sync_document_checkboxes() {
    set_stack_check(QStringLiteral("Timeline"), timeline_checked_);
    set_stack_check(QStringLiteral("Project Summary Task"), project_summary_checked_);
}

void RibbonBar::set_font_default(const QString& family, int point_size) {
    font_family_ = family.isEmpty() ? QStringLiteral("Calibri") : family;
    font_size_ = point_size > 0 ? QString::number(point_size) : QStringLiteral("11");
    // The Font group lives on the Task tab.  If it is the live page, update the
    // two displays in place; otherwise they are built from the members when the
    // tab is next shown.
    if (active_tab_ != 0) {
        return;
    }
    for (const std::vector<QWidget*>& group : page_controls_) {
        for (QWidget* control : group) {
            auto* combo = qobject_cast<QComboBox*>(control);
            if (combo == nullptr) {
                continue;
            }
            const QString name = combo->accessibleName();
            const QString value = name == QLatin1String("font name")  ? font_family_
                                  : name == QLatin1String("font size") ? font_size_
                                                                       : QString();
            if (value.isEmpty()) {
                continue;
            }
            const QSignalBlocker blocker(combo);
            combo->clear();
            combo->addItem(value);
            combo->setCurrentIndex(0);
        }
    }
}

void RibbonBar::set_stack_check(const QString& accessible_name, bool checked) {
    for (std::size_t group = 0; group < page_controls_.size(); ++group) {
        for (std::size_t control = 0; control < page_controls_[group].size(); ++control) {
            if (page_controls_[group][control] == nullptr ||
                page_controls_[group][control]->accessibleName() != accessible_name) {
                continue;
            }
            auto* box = qobject_cast<QCheckBox*>(page_controls_[group][control]);
            if (box != nullptr) {
                const QSignalBlocker blocker(box);
                box->setChecked(checked);
            }
        }
    }
}

void RibbonBar::set_contextual_visible(bool visible) {
    contextual_enabled_ = visible;
    update_tab_overflow();
}

void RibbonBar::update_tab_overflow() {
    if (tab_overflow_ == nullptr || tab_row_ == nullptr) {
        return;
    }
    // Widen enough: every tab is shown.  Narrow: the last two tabs go behind the
    // chevron (the strip never wraps to a second row).
    const bool narrow = width() < 1000;
    for (std::size_t index = 0; index < tab_buttons_.size(); ++index) {
        if (tab_buttons_[index] == nullptr) {
            continue;
        }
        const bool hidden_by_context =
            tab_buttons_[index] == contextual_tab_ && !contextual_enabled_;
        const bool hide = hidden_by_context || (narrow && index + 2 >= tab_buttons_.size());
        tab_buttons_[index]->setVisible(!hide);
    }
    if (narrow) {
        tab_overflow_->setVisible(true);
        tab_overflow_->move(tab_row_->width() - tab_overflow_->width() - 8, 2);
        tab_overflow_->raise();
    } else {
        tab_overflow_->hide();
    }
}

void RibbonBar::build_tabs() {
    auto* layout = static_cast<QHBoxLayout*>(tab_row_->layout());
    // The File tab is chrome: dark tile, its own click handler, and deliberately
    // not one of the seven modelled command tabs (tab_count() stays 7).
    file_tab_ = new QToolButton(tab_row_);
    file_tab_->setObjectName(QStringLiteral("ribbonFileTab"));
    file_tab_->setText(QStringLiteral("File"));
    file_tab_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    file_tab_->setFont(ribbon_font(kBodyPointSize - 1));
    file_tab_->setFixedHeight(kRibbonTabHeight - 4);
    file_tab_->setFocusPolicy(Qt::NoFocus);
    connect(file_tab_, &QToolButton::clicked, this, [this](bool) {
        if (file_handler_) {
            file_handler_();
        }
    });
    layout->addWidget(file_tab_);

    const std::vector<TabSpec>& tabs = tab_specs();
    for (std::size_t index = 0; index < tabs.size(); ++index) {
        auto* button = new QToolButton(tab_row_);
        button->setObjectName(tabs[index].contextual ? QStringLiteral("ribbonTabContextual")
                                                     : QStringLiteral("ribbonTab"));
        button->setText(QString::fromUtf8(tabs[index].title));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setChecked(index == 0);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFont(ribbon_font(kBodyPointSize - 1));
        button->setFixedHeight(kRibbonTabHeight - 4);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QToolButton::clicked, this, [this, tab = static_cast<int>(index)](bool) {
            active_tab_ = tab;
            build_page(tab);
        });
        if (tabs[index].contextual) {
            contextual_tab_ = button;
        }
        tab_buttons_.push_back(button);
        layout->addWidget(button);
    }
    layout->addStretch(1);

    // The reference's right-hand "Tell me" box.  The chrome gate asserts this is
    // present and that its label is the full sentence, not an empty box.
    tell_me_ = new QLabel(QStringLiteral("Tell me what you want to do"), tab_row_);
    tell_me_->setObjectName(QStringLiteral("ribbonTellMe"));
    tell_me_->setFont(ribbon_font(kBodyPointSize - 1));
    tell_me_->setStyleSheet(QStringLiteral("color: #7a7a7a; padding-right: 8px;"));
    layout->addWidget(tell_me_);
}

void RibbonBar::clear_page() {
    page_groups_.clear();
    page_controls_.clear();
    page_actions_.clear();
    page_commands_.clear();
    page_icons_.clear();
    page_rows_.clear();
    if (page_layout_ == nullptr) {
        return;
    }
    while (QLayoutItem* item = page_layout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            // Safe here: the only caller is a tab click, and the tab buttons are
            // not children of page_.
            delete widget;
        }
        delete item;
    }
}

void RibbonBar::build_page(int tab_index) {
    const std::vector<TabSpec>& tabs = tab_specs();
    if (tab_index < 0 || static_cast<std::size_t>(tab_index) >= tabs.size()) {
        return;
    }
    clear_page();
    button_count_ = 0;
    active_tab_ = tab_index;

    const TabSpec& tab = tabs[static_cast<std::size_t>(tab_index)];
    for (const GroupSpec& group : tab.groups) {
        std::vector<QWidget*> group_controls;
        std::vector<app::MenuAction> group_actions;
        std::vector<RibbonCommand> group_commands;
        std::vector<QString> group_icons;
        std::vector<int> group_rows;
        auto* box = new QWidget(page_);
        box->setFixedSize(group.width - 1, kGroupBandHeight);
        auto* box_layout = new QVBoxLayout(box);
        box_layout->setContentsMargins(0, 0, 0, 0);
        box_layout->setSpacing(0);

        auto* host = new QWidget(box);
        host->setFixedHeight(kControlsHeight);
        auto* grid = new QGridLayout(host);
        grid->setContentsMargins(2, kStackTopOffset, 2, 0);
        grid->setHorizontalSpacing(2);
        grid->setVerticalSpacing(0);
        for (int row = 0; row < kStackRows; ++row) {
            grid->setRowMinimumHeight(row, kStackRowHeight);
        }

        for (const ControlSpec& spec : group.controls) {
            const QString label = QString::fromUtf8(spec.label);
            const QString icon_key = QString::fromUtf8(spec.icon != nullptr ? spec.icon : "");
            const QString tip = spec.enabled ? label : QString::fromUtf8(kNotImplemented);
            QWidget* control = nullptr;
            switch (spec.kind) {
                case Kind::button: {
                    auto* button = new QToolButton(host);
                    button->setObjectName(QStringLiteral("ribbonButton"));
                    button->setText(label);
                    button->setIcon(shell_ribbon_icon(icon_key, kRibbonIconPx,
                                                     chrome_palette(theme_).ribbon_ink));
                    button->setIconSize(QSize(kRibbonIconPx, kRibbonIconPx));
                    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
                    button->setFont(ribbon_font(kRibbonPointSize));
                    button->setAutoRaise(true);
                    button->setFixedSize(spec.width, kLargeHeight);
                    button->setFocusPolicy(Qt::NoFocus);
                    button->setEnabled(spec.enabled);
                    button->setToolTip(tip);
                    const app::MenuAction action = spec.action;
                    const RibbonCommand command = spec.command;
                    if (command != RibbonCommand::none) {
                        connect(button, &QToolButton::clicked, this, [this, command](bool) {
                            if (command_handler_) {
                                command_handler_(command, false);
                            }
                        });
                    } else {
                        connect(button, &QToolButton::clicked, this, [this, action](bool) {
                            if (handler_) {
                                handler_(action);
                            }
                        });
                    }
                    ++button_count_;
                    control = button;
                    break;
                }
                case Kind::split: {
                    auto* button = new RibbonSplitButton(host);
                    button->setObjectName(QStringLiteral("ribbonSplitButton"));
                    button->setText(label);
                    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
                    if (spec.row_span == 1) {
                        button->setIcon(shell_ribbon_icon(icon_key, 16,
                                                         chrome_palette(theme_).ribbon_ink));
                        button->setIconSize(QSize(16, 16));
                        button->setFixedSize(spec.width, kStackRowHeight);
                        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
                        button->setAccessibleName(label);
                    } else {
                        button->setIcon(shell_ribbon_icon(icon_key, kRibbonIconPx,
                                                         chrome_palette(theme_).ribbon_ink));
                        button->setIconSize(QSize(kRibbonIconPx, kRibbonIconPx));
                        button->setFixedSize(spec.width, kLargeHeight);
                    }
                    button->setFont(ribbon_font(kRibbonPointSize));
                    button->setFocusPolicy(Qt::NoFocus);
                    auto* menu = new QMenu(button);
                    menu->setToolTipsVisible(true);
                    if (spec.enabled && spec.command != RibbonCommand::none &&
                        spec.command != RibbonCommand::wbs_menu &&
                        spec.command != RibbonCommand::set_baseline_menu) {
                        // W28-1.3: Mark on Track's main button runs its
                        // command; its drop-down has no measured menu in this
                        // port, so the arrow opens one disabled "not
                        // implemented" entry.  Decision 9 is about dead clicks,
                        // and this click is live.
                        QAction* entry =
                            menu->addAction(QStringLiteral("not implemented in this port"));
                        entry->setEnabled(false);
                        entry->setToolTip(QString::fromUtf8(kNotImplemented));
                        button->setMenu(menu);
                        button->setPopupMode(QToolButton::MenuButtonPopup);
                        button->setEnabled(true);
                        button->setToolTip(label);
                        const RibbonCommand command = spec.command;
                        connect(button, &QToolButton::clicked, this, [this, command](bool) {
                            if (command_handler_) {
                                command_handler_(command, false);
                            }
                        });
                    } else if (spec.enabled &&
                               (spec.command == RibbonCommand::wbs_menu ||
                                spec.command == RibbonCommand::set_baseline_menu)) {
                        // W26-5.4: the captured drop-down menus.  Every item has
                        // no engine in this port, so each is disabled with the
                        // exact decision-9 tooltip; opening the menu is live.
                        const std::vector<std::string>& items =
                            spec.command == RibbonCommand::wbs_menu
                                ? app::wbs_menu_items()
                                : app::baseline_menu_items();
                        for (const std::string& item : items) {
                            QAction* action = menu->addAction(QString::fromStdString(item));
                            action->setEnabled(false);
                            action->setToolTip(QString::fromUtf8(kNotImplemented));
                        }
                        button->setMenu(menu);
                        button->setPopupMode(QToolButton::MenuButtonPopup);
                        button->setEnabled(true);
                        button->setToolTip(label);
                        const RibbonCommand command = spec.command;
                        connect(button, &QToolButton::clicked, this, [this, command](bool) {
                            if (command_handler_) {
                                command_handler_(command, false);
                            }
                        });
                    } else {
                        QAction* entry =
                            menu->addAction(QStringLiteral("not implemented in this port"));
                        entry->setEnabled(false);
                        button->setMenu(menu);
                        button->setEnabled(false);  // decision 9: never a dead click
                        button->setToolTip(QString::fromUtf8(kNotImplemented));
                    }
                    control = button;
                    break;
                }
                case Kind::small: {
                    auto* button = new RibbonSmallButton(host);
                    build_small_button(button, spec, host,
                                       chrome_palette(theme_).ribbon_ink);
                    const RibbonCommand command = spec.command;
                    const app::MenuAction action = spec.action;
                    if (spec.enabled) {
                        if (command != RibbonCommand::none) {
                            connect(button, &QToolButton::clicked, this,
                                    [this, command](bool) {
                                        if (command_handler_) {
                                            command_handler_(command, false);
                                        }
                                    });
                        } else if (action != app::MenuAction::none) {
                            connect(button, &QToolButton::clicked, this, [this, action](bool) {
                                if (handler_) {
                                    handler_(action);
                                }
                            });
                        }
                    }
                    control = button;
                    break;
                }
                case Kind::combo: {
                    auto* combo_box = new QComboBox(host);
                    combo_box->setObjectName(QStringLiteral("ribbonCombo"));
                    combo_box->setFont(ribbon_font(kRibbonPointSize));
                    combo_box->setFixedSize(spec.width, kStackRowHeight);
                    combo_box->setFocusPolicy(Qt::NoFocus);
                    combo_box->setAccessibleName(label);
                    QString items = QString::fromUtf8(spec.items != nullptr ? spec.items : "");
                    // W26-REV-4: the Font group's two displays follow the file's
                    // decoded default text style (the spec's Calibri/11 is only
                    // the new-project fallback).
                    if (label == QLatin1String("font name") && !font_family_.isEmpty()) {
                        items = font_family_;
                    } else if (label == QLatin1String("font size") && !font_size_.isEmpty()) {
                        items = font_size_;
                    }
                    if (!items.isEmpty()) {
                        for (const QString& item : items.split(QLatin1Char('|'))) {
                            combo_box->addItem(item);
                        }
                    } else {
                        combo_box->addItem(QString());
                    }
                    combo_box->setEnabled(spec.enabled);
                    combo_box->setToolTip(tip);
                    control = combo_box;
                    break;
                }
                case Kind::checkbox: {
                    auto* box_check = new QCheckBox(host);
                    box_check->setObjectName(QStringLiteral("ribbonCheck"));
                    box_check->setFont(ribbon_font(kRibbonPointSize));
                    box_check->setText(label);
                    box_check->setFixedSize(spec.width, kStackRowHeight);
                    box_check->setFocusPolicy(Qt::NoFocus);
                    bool checked = spec.checked == 1;
                    if (spec.checked < 0) {
                        checked = spec.command == RibbonCommand::toggle_timeline
                                      ? timeline_checked_
                                      : (spec.command == RibbonCommand::toggle_project_summary
                                             ? project_summary_checked_
                                             : (spec.command == RibbonCommand::toggle_details
                                                    ? details_checked_
                                                    : critical_checked_));
                    }
                    box_check->setChecked(checked);
                    box_check->setEnabled(spec.enabled);
                    box_check->setToolTip(tip);
                    const RibbonCommand command = spec.command;
                    connect(box_check, &QCheckBox::toggled, this, [this, command](bool on) {
                        if (command == RibbonCommand::toggle_timeline) {
                            timeline_checked_ = on;
                        } else if (command == RibbonCommand::toggle_project_summary) {
                            project_summary_checked_ = on;
                        } else if (command == RibbonCommand::toggle_details) {
                            details_checked_ = on;
                        } else if (command == RibbonCommand::toggle_critical_tasks) {
                            critical_checked_ = on;
                        }
                        if (command_handler_) {
                            command_handler_(command, on);
                        }
                    });
                    control = box_check;
                    break;
                }
                case Kind::field: {
                    auto* field_widget = new RibbonField(host);
                    field_widget->setObjectName(QStringLiteral("ribbonField"));
                    field_widget->setAccessibleName(label);
                    field_widget->setFixedSize(spec.width, kStackRowHeight);
                    auto* field_layout = new QHBoxLayout(field_widget);
                    field_layout->setContentsMargins(0, 0, 0, 0);
                    field_layout->setSpacing(2);
                    auto* field_label = new QLabel(QStringLiteral("Status Date:"), field_widget);
                    field_label->setFont(ribbon_font(kRibbonPointSize));
                    field_layout->addWidget(field_label);
                    auto* date_edit = new QDateEdit(field_widget);
                    date_edit->setObjectName(QStringLiteral("ribbonDateField"));
                    date_edit->setFont(ribbon_font(kRibbonPointSize));
                    date_edit->setDisplayFormat(QStringLiteral("M/d/yyyy"));
                    date_edit->setCalendarPopup(false);
                    date_edit->setMinimumDate(QDate(2000, 1, 1));
                    date_edit->setMaximumDate(QDate(2099, 12, 31));
                    date_edit->setDate(QDate(2000, 1, 1));
                    date_edit->setSpecialValueText(QStringLiteral("NA"));
                    date_edit->setFixedHeight(kStackRowHeight);
                    date_edit->setEnabled(false);
                    date_edit->setToolTip(QString::fromUtf8(kNotImplemented));
                    field_layout->addWidget(date_edit, 1);
                    field_widget->setEnabled(false);
                    field_widget->setToolTip(QString::fromUtf8(kNotImplemented));
                    control = field_widget;
                    break;
                }
                case Kind::gallery: {
                    auto* gallery_widget = new RibbonGallery(host);
                    gallery_widget->setObjectName(QStringLiteral("ribbonGallery"));
                    gallery_widget->setAccessibleName(label);
                    gallery_widget->setFixedSize(spec.width, kControlsHeight - kStackTopOffset);
                    auto* gallery_layout = new QHBoxLayout(gallery_widget);
                    gallery_layout->setContentsMargins(0, 0, 0, 0);
                    gallery_layout->setSpacing(2);
                    for (std::size_t swatch = 0; swatch < kGanttStylePalette.size(); ++swatch) {
                        auto* button = new QToolButton(gallery_widget);
                        button->setObjectName(QStringLiteral("ribbonSwatch"));
                        button->setFixedSize(52, 34);
                        button->setIcon(swatch_icon(QColor(kGanttStylePalette[swatch])));
                        button->setIconSize(QSize(52, 30));
                        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
                        button->setFocusPolicy(Qt::NoFocus);
                        button->setEnabled(false);
                        button->setToolTip(QString::fromUtf8(kNotImplemented));
                        button->setAccessibleName(
                            QStringLiteral("Gantt Chart Style %1").arg(swatch + 1));
                        gallery_layout->addWidget(button);
                    }
                    auto* expander = new QToolButton(gallery_widget);
                    expander->setObjectName(QStringLiteral("ribbonGalleryExpander"));
                    expander->setText(QStringLiteral("v"));
                    expander->setFixedWidth(18);
                    expander->setFont(ribbon_font(kRibbonPointSize));
                    expander->setFocusPolicy(Qt::NoFocus);
                    expander->setEnabled(false);
                    expander->setToolTip(QString::fromUtf8(kNotImplemented));
                    gallery_layout->addWidget(expander);
                    gallery_layout->addStretch(0);
                    gallery_widget->setEnabled(false);
                    gallery_widget->setToolTip(QString::fromUtf8(kNotImplemented));
                    control = gallery_widget;
                    break;
                }
            }
            if (control == nullptr) {
                continue;
            }
            grid->addWidget(control, spec.row, spec.col, spec.row_span, spec.col_span);
            group_controls.push_back(control);
            group_actions.push_back(spec.action);
            group_commands.push_back(spec.command);
            group_icons.push_back(icon_key);
            group_rows.push_back(spec.row_span == 1 ? spec.row : -1);
        }
        box_layout->addWidget(host);
        box_layout->addStretch(1);

        auto* bottom = new QWidget(box);
        bottom->setFixedHeight(kRibbonGroupLabelHeight);
        auto* bottom_layout = new QHBoxLayout(bottom);
        bottom_layout->setContentsMargins(0, 0, 0, 0);
        bottom_layout->setSpacing(0);
        auto* caption = new QLabel(QString::fromUtf8(group.title), bottom);
        caption->setObjectName(QStringLiteral("ribbonGroupLabel"));
        caption->setFont(ribbon_font(kGroupLabelPointSize));
        caption->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        bottom_layout->addWidget(caption, 1);
        if (group.launcher != nullptr) {
            auto* launcher_button = new RibbonLauncher(bottom);
            launcher_button->setObjectName(QStringLiteral("ribbonLauncher"));
            launcher_button->setFixedSize(kLauncherSize, kLauncherSize);
            launcher_button->setIcon(launcher_icon());
            launcher_button->setIconSize(QSize(12, 12));
            launcher_button->setFocusPolicy(Qt::NoFocus);
            launcher_button->setAccessibleName(QString::fromUtf8(group.launcher));
            launcher_button->setEnabled(false);
            launcher_button->setToolTip(QString::fromUtf8(kNotImplemented));
            bottom_layout->addWidget(launcher_button, 0, Qt::AlignRight | Qt::AlignBottom);
            group_controls.push_back(launcher_button);
            group_actions.push_back(app::MenuAction::none);
            group_commands.push_back(RibbonCommand::none);
            group_icons.push_back(QString());
            group_rows.push_back(-1);
        }
        box_layout->addWidget(bottom);

        page_layout_->addWidget(box);

        auto* separator = new QFrame(page_);
        separator->setFrameShape(QFrame::VLine);
        separator->setFrameShadow(QFrame::Plain);
        separator->setFixedSize(kGroupSeparatorWidth, kGroupBandHeight);
        separator->setStyleSheet(
            QStringLiteral("background: %1;")
                .arg(chrome_palette(theme_).ribbon_border.name()));
        page_layout_->addWidget(separator);

        GroupWidgets widgets;
        widgets.box = box;
        widgets.separator = separator;
        page_groups_.push_back(widgets);
        page_controls_.push_back(std::move(group_controls));
        page_actions_.push_back(std::move(group_actions));
        page_commands_.push_back(std::move(group_commands));
        page_icons_.push_back(std::move(group_icons));
        page_rows_.push_back(std::move(group_rows));
    }
    page_layout_->addStretch(1);
    layout_page_groups();
    sync_document_checkboxes();
}

void RibbonBar::layout_page_groups() {
    if (page_layout_ == nullptr) {
        return;
    }
    const QMargins margins = page_layout_->contentsMargins();
    const int available = width() - margins.left() - margins.right();
    if (available < 200) {
        // The ribbon has not been laid out yet (construction).  Show everything
        // and let the first resize settle it; hiding here would hide the whole
        // page at width 0.
        for (GroupWidgets& group : page_groups_) {
            if (group.box != nullptr) {
                group.box->setVisible(true);
            }
            if (group.separator != nullptr) {
                group.separator->setVisible(true);
            }
        }
        return;
    }
    int used = 0;
    bool overflow = false;
    for (GroupWidgets& group : page_groups_) {
        if (group.box == nullptr || group.separator == nullptr) {
            continue;
        }
        const int wanted = group.box->width() + kGroupSeparatorWidth;
        const bool fits = !overflow && used + wanted <= available;
        group.box->setVisible(fits);
        group.separator->setVisible(fits);
        if (fits) {
            used += wanted;
        } else {
            // Ribbon order is preserved: once a group does not fit, nothing
            // after it is shown either.
            overflow = true;
        }
    }
    groups_overflow_ = overflow;
    if (overflow_ != nullptr) {
        // One chevron at the group row's right edge; the row height is unchanged
        // (no wrapping).
        overflow_->setVisible(overflow);
        if (overflow) {
            overflow_->move(width() - overflow_->width() - 6, 8);
            overflow_->raise();
        }
    }
    page_layout_->invalidate();
}

int RibbonBar::visible_group_count() const {
    int count = 0;
    for (const GroupWidgets& group : page_groups_) {
        if (group.box != nullptr && group.box->isVisible()) {
            ++count;
        }
    }
    return count;
}

void RibbonBar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    layout_page_groups();
    update_tab_overflow();
}

int RibbonBar::tab_count() const { return static_cast<int>(tab_specs().size()); }

QStringList RibbonBar::tab_titles() const {
    QStringList titles;
    for (const TabSpec& tab : tab_specs()) {
        titles.push_back(QString::fromUtf8(tab.title));
    }
    return titles;
}

QStringList RibbonBar::group_titles() const {
    QStringList titles;
    if (page_layout_ == nullptr) {
        return titles;
    }
    for (int index = 0; index < page_layout_->count(); ++index) {
        QWidget* widget = page_layout_->itemAt(index) ? page_layout_->itemAt(index)->widget()
                                                      : nullptr;
        if (widget == nullptr) {
            continue;
        }
        for (QLabel* label : widget->findChildren<QLabel*>()) {
            if (label->objectName() == QStringLiteral("ribbonGroupLabel")) {
                titles.push_back(label->text());
            }
        }
    }
    return titles;
}

int RibbonBar::button_count() const { return button_count_; }

// WAVE 22/26: the inventory read-back the ribbon gate pins.  Switching tabs goes
// through the same build_page() a tab click uses, so the checked tab button and
// the page cannot drift apart.
void RibbonBar::set_active_tab(int index) {
    if (index < 0 || index >= tab_count()) {
        return;
    }
    for (std::size_t button = 0; button < tab_buttons_.size(); ++button) {
        if (tab_buttons_[button] != nullptr) {
            tab_buttons_[button]->setChecked(static_cast<int>(button) == index);
        }
    }
    build_page(index);
}

QString RibbonBar::control_kind(const QWidget* control) {
    if (control == nullptr) {
        return QStringLiteral("none");
    }
    // The live class decides.  These four subclasses exist so a split button,
    // a small stack button, the gallery and the dialog launcher are distinct
    // from an ordinary QToolButton/QWidget without consulting the spec.
    if (dynamic_cast<const RibbonGallery*>(control) != nullptr) {
        return QStringLiteral("gallery");
    }
    if (dynamic_cast<const RibbonField*>(control) != nullptr) {
        return QStringLiteral("field");
    }
    if (dynamic_cast<const RibbonLauncher*>(control) != nullptr) {
        return QStringLiteral("launcher");
    }
    if (dynamic_cast<const RibbonSplitButton*>(control) != nullptr) {
        return QStringLiteral("split");
    }
    if (dynamic_cast<const RibbonSmallButton*>(control) != nullptr) {
        return QStringLiteral("small");
    }
    if (qobject_cast<const QComboBox*>(control) != nullptr) {
        return QStringLiteral("combo");
    }
    if (qobject_cast<const QCheckBox*>(control) != nullptr) {
        return QStringLiteral("checkbox");
    }
    if (qobject_cast<const QDateEdit*>(control) != nullptr) {
        return QStringLiteral("field");
    }
    if (qobject_cast<const QToolButton*>(control) != nullptr) {
        return QStringLiteral("button");
    }
    return QStringLiteral("unknown");
}

int RibbonBar::group_control_count(int group_index) const {
    if (group_index < 0 || static_cast<std::size_t>(group_index) >= page_controls_.size()) {
        return 0;
    }
    return static_cast<int>(page_controls_[static_cast<std::size_t>(group_index)].size());
}

QWidget* RibbonBar::group_box(int group_index) const {
    if (group_index < 0 || static_cast<std::size_t>(group_index) >= page_groups_.size()) {
        return nullptr;
    }
    return page_groups_[static_cast<std::size_t>(group_index)].box;
}

QWidget* RibbonBar::group_control(int group_index, int control_index) const {
    if (group_index < 0 || static_cast<std::size_t>(group_index) >= page_controls_.size()) {
        return nullptr;
    }
    const std::vector<QWidget*>& group =
        page_controls_[static_cast<std::size_t>(group_index)];
    if (control_index < 0 || static_cast<std::size_t>(control_index) >= group.size()) {
        return nullptr;
    }
    return group[static_cast<std::size_t>(control_index)];
}

app::MenuAction RibbonBar::group_control_action(int group_index, int control_index) const {
    if (group_index < 0 || static_cast<std::size_t>(group_index) >= page_actions_.size()) {
        return app::MenuAction::none;
    }
    const std::vector<app::MenuAction>& actions =
        page_actions_[static_cast<std::size_t>(group_index)];
    if (control_index < 0 || static_cast<std::size_t>(control_index) >= actions.size()) {
        return app::MenuAction::none;
    }
    return actions[static_cast<std::size_t>(control_index)];
}

RibbonCommand RibbonBar::group_control_command(int group_index, int control_index) const {
    if (group_index < 0 || static_cast<std::size_t>(group_index) >= page_commands_.size()) {
        return RibbonCommand::none;
    }
    const std::vector<RibbonCommand>& commands =
        page_commands_[static_cast<std::size_t>(group_index)];
    if (control_index < 0 || static_cast<std::size_t>(control_index) >= commands.size()) {
        return RibbonCommand::none;
    }
    return commands[static_cast<std::size_t>(control_index)];
}

QString RibbonBar::group_control_icon_key(int group_index, int control_index) const {
    if (group_index < 0 || static_cast<std::size_t>(group_index) >= page_icons_.size()) {
        return QString();
    }
    const std::vector<QString>& icons =
        page_icons_[static_cast<std::size_t>(group_index)];
    if (control_index < 0 || static_cast<std::size_t>(control_index) >= icons.size()) {
        return QString();
    }
    return icons[static_cast<std::size_t>(control_index)];
}

int RibbonBar::group_control_row(int group_index, int control_index) const {
    if (group_index < 0 || static_cast<std::size_t>(group_index) >= page_rows_.size()) {
        return -1;
    }
    const std::vector<int>& rows = page_rows_[static_cast<std::size_t>(group_index)];
    if (control_index < 0 || static_cast<std::size_t>(control_index) >= rows.size()) {
        return -1;
    }
    return rows[static_cast<std::size_t>(control_index)];
}

QString RibbonBar::group_control_kind(int group_index, int control_index) const {
    return control_kind(group_control(group_index, control_index));
}

void RibbonBar::paintEvent(QPaintEvent*) {
    const ChromePalette& palette = chrome_palette(theme_);
    QPainter painter(this);
    // W26-6.2: the tab row and the group row are two surfaces (the dark
    // captures show a darker tab row over the group-row surface).
    painter.fillRect(rect(), palette.ribbon_background);
    painter.fillRect(QRect(0, 0, width(), kRibbonTabHeight), palette.ribbon_tab_background);
    painter.setPen(QPen(palette.ribbon_border, 1));
    painter.drawLine(0, height() - 1, width(), height() - 1);
}

}  // namespace msqt
