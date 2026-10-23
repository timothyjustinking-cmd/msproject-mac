// app/main.mm -- the macOS shell v3: one window, one scroll view, a real sheet on
// the left and a real Gantt on the right, both fed by the portable core, with a
// real menu bar, a Preferences panel, PDF export, a filter dialog, a clipboard,
// two-way selection, in-cell editing, undo/redo, zoom and outline collapse.
// Objective-C++ / AppKit, deliberately thin: everything that can be plain C++
// lives in layout_model.cpp (mapping), selection_model.cpp (selection/collapse
// geometry), document.cpp (the save path), prefs.cpp (preferences), menu_model.cpp
// (the menu bar as data), filter_model.cpp (the filter dialog's model) and
// export_pdf.cpp (the report), each proved by a headless test on Windows as well
// as macOS.  This file is the only ObjC++ in the repository.
//
// WHAT RUNS
//   main() -> NSApplication -> AppDelegate -> loads a .mpp: argv[1] if given,
//   else the first fixture under corpus/probe (git-ignored probes), else
//   tests/data/synthetic-base.mpp.  File > Open (Cmd-O) replaces it.  The load
//   builds app::PlanInput -> RecordSet + SheetModel + RowMap -> GanttSource ->
//   build_gantt(), then shows a PlanView (the document view) in one NSScrollView.
//   The menu bar is built by walking app::menu_specs(); every item's action is
//   one app::MenuAction tag dispatched by performMenuAction:.
//
// INTERACTION
//   * sheet click selects a cell; shift-click extends; double-click edits
//   * a click on a summary row's disclosure box (Name column, left gutter)
//     collapses/expands it; so do Cmd-Left / Cmd-Right
//   * typing a printable character starts an edit (replacing the cell text, as
//     Project does); Return commits; Esc cancels; Tab commits and moves right;
//     Backspace edits the text, and with no edit open it clears the cell first
//   * arrows move the selection through the DISPLAYED rows (app::row_after);
//     Space toggles between one cell and the whole row
//   * Cmd-Z / Cmd-Shift-Z undo/redo through SheetModel (menu items)
//   * clicking a Gantt bar (or a dependency) selects the sheet row; selecting a
//     sheet row highlights its bar and scrolls it into view.  SheetModel is the
//     single source of truth for the selection -- see selection_model.h
//   * Cmd-C / Cmd-V copy/paste through the general pasteboard; Cut is greyed out
//     (SheetModel has no cut operation)
//   * Cmd-S writes the edited task starts (document.h) to the last save target,
//     asking for a path the first time; Cmd-Shift-S always asks.  A pending
//     View > Table Date Format or Edit > Custom Field Formula edit rides the same
//     container write (app::Document::save, wave 16).  Closing or quitting with
//     pending edits prompts Save / Don't Save / Cancel.
//   * Cmd-F filters the displayed rows (app/filter_model.h); Cmd-Shift-F clears it
//   * Cmd-1/2/3 and Cmd-=/Cmd-- set the Gantt timescale; Cmd-Shift-D toggles the
//     working-day shading; both persist in NSUserDefaults (app/prefs.h)
//   * Cmd-Shift-E / Cmd-Option-E export a PDF sheet / Gantt report
//   * Cmd-P prints the table + Gantt through app/page_layout.h; Cmd-Shift-P
//     saves that same print job as a PDF (both routes share build_print_job)
//   * Preferences (Cmd-,) sets the date format, duration format, timescale and
//     shading, applied live
//
// DRAWING (PlanView, flipped so y grows downwards like core::gantt)
//   left:  sheet header, row text through SheetModel (or app::cell_text when the
//          duration preference overrides it), summary band, critical red,
//          outline indent + disclosure boxes, selection highlight, and the
//          in-progress edit text with a caret.
//   right: two header tiers (months over days), non-working shading (unless the
//          preference turns it off), one bar per displayed row
//          (normal/summary/milestone), dependency polylines with arrowheads, the
//          selected bar outlined, the status-date marker.
//   footer: two lines -- counts + filter + preferences + the file, then the last
//          action (red when it was a rejected edit or a failed save/open).
//
// WHAT IT STILL DOES NOT DO (see app/README.md)
//   No cut, no indent/outdent, no row move, no column resize/reorder, no multiple
//   windows, no IME/composition in the in-cell editor (see README).  Wave 12 adds
//   Insert Task / Delete Task (pending structural edits applied by the writer on
//   Save), the stored custom-field columns and the file's own formula columns.
//
// BUILD.  This file is compiled only on APPLE (app/CMakeLists.txt).  It is
// Objective-C++20 under ARC.  AppKit APIs used here are non-deprecated as of the
// macOS 14/15 SDKs so the CI job can build with -Werror; anything that needs an
// availability check (the panels' content types) is guarded with @available.

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "columns.h"
#include "date.h"
#include "document.h"
#include "edit_model.h"
#include "field_link_edits.h"
#include "field_value.h"
#include "fields_model.h"
#include "export_pdf.h"
#include "filter_model.h"
#include "gantt.h"
#include "layout_model.h"
#include "menu_model.h"
#include "page_layout.h"
#include "prefs.h"
#include "reader_adapter.h"
#include "selection_model.h"
#include "sheet.h"
#include "smoke.h"
#include "start_edit_warning.h"
#include "usage.h"
#include "view_model.h"

namespace {

// Geometry.  kRowHeight must equal default_sheet_metrics().row_height (the
// headless test asserts that they match); kHeaderHeight is the two Gantt header
// tiers and is also the sheet header band height.
constexpr double kRowHeight = 20.0;
constexpr double kHeaderHeight = 32.0;
constexpr double kHeaderTierHeight = 16.0;
constexpr double kFooterHeight = 44.0;
constexpr double kSheetDivider = 1.0;
constexpr double kGanttLeftPad = 10.0;
constexpr double kRightMargin = 180.0;
constexpr double kMinGanttWidth = 480.0;
constexpr double kBottomPadding = 12.0;
constexpr double kLabelFontSize = 9.0;
constexpr double kTextInset = 3.0;
// The outline disclosure box: reserved at the start of the Name cell for every
// row, so children line up whether or not they have one.
constexpr double kDisclosureSize = 9.0;
constexpr double kDisclosureGap = 3.0;

// Everything the window draws from, in one place.  Heap-allocated (see main) and
// intentionally never freed: the process exits right after the run loop.
struct PlanState {
    // The plan as loaded: the baseline that `document.h` diffs against, and the
    // source of the title, project dates and links.
    app::PlanInput loaded;
    // The plan as it is now: `loaded` with the task fields re-read from `records`
    // (so an edit, an undo or a cleared cell is reflected in the Gantt and the
    // status line).
    app::PlanInput view;
    core::view::RecordSet records{core::model::RecordType::Task};
    std::unique_ptr<core::sheet::SheetModel> sheet;
    core::sheet::SheetMetrics metrics;
    core::sheet::SheetFormat format;
    std::vector<core::sheet::ColumnSegment> columns;
    app::RowMap row_map;
    core::gantt::GanttLayout gantt;
    double sheet_width = 0.0;
    double gantt_width = 0.0;
    std::string path;
    std::string status_text;
    std::string message;  // last action, or the last rejection/failure
    bool message_is_error = false;
    std::string error;
    std::size_t warnings = 0;
    std::size_t skipped_links = 0;

    // The filter (app/filter_model.h).  A filter restricts the VISIBLE ROW LIST
    // -- app::RowMap -- never the RecordSet, so every edit still goes to the one
    // model the save diff compares.  Cleared on every load.
    bool filter_active = false;
    core::view::FilterDefinition filter{};
    std::string filter_description;
    std::vector<std::size_t> filter_rows;  // RecordSet rows that pass

    // --- wave 11: the resource/usage views and the computed columns --------
    app::ViewMode view_mode = app::ViewMode::task_sheet;
    core::usage::Period usage_period = core::usage::Period::day;
    // Built once per load from the reader's assignment table through
    // core::usage::usage_input_from_mpp (see reader_adapter.h); the calendar is
    // the documented stock week (app/view_model.h), not the file's calendar.
    core::usage::UsageInput usage_input;
    core::sched::Calendar calendar = app::standard_calendar();
    core::usage::UsageProjection usage;  // projected for `usage_period`
    app::UsageView usage_view;           // labels + the copied table
    // The non-task grid (resource sheet or usage table).  It owns its RecordSet
    // because core::sheet::SheetModel holds a reference to one.  Rebuilt when
    // the view mode or usage period changes; empty in the task views.
    core::view::RecordSet secondary_records{core::model::RecordType::Unknown};
    std::unique_ptr<core::sheet::SheetModel> secondary_sheet;
    std::vector<core::sheet::ColumnSegment> secondary_columns;
    double secondary_width = 0.0;
    // The task sheet's computed columns, owned per load so a formula can never
    // carry a field id from a previous document.  `computed_error` is "" unless
    // a formula failed to parse.
    core::formula::ComputedColumns computed;
    std::string computed_error;

    // --- wave 12: the editor and the file's own custom fields ---------------
    // The plan the editor is built from: `loaded` with the pending structural
    // edits applied (app/edit_model.h).  `records` is rebuilt from it whenever an
    // edit is pushed, undone or redone.
    app::PlanInput base;
    // Pending Insert/Delete Task edits.  They reach a file only on Save.
    app::StructuralEdits edits;
    // The formulas the file's `114/Props` stores (core/props W50) and the sheet's
    // formula column set (app/fields_model.h).  Both are rebuilt per load.
    app::FormulaColumnSet file_formulas;
    app::FormulaColumnSet formula_set;
    // True when this app itself wrote `path` (a structural save reloads the
    // target and adopts it), so saving over it again is allowed.  False for a file
    // the user opened: that one is never overwritten (app/document.h).
    bool owns_path = false;

    // --- wave 16: the pending non-table save operations ---------------------
    // One app::Document per load holds the two operations W64 added: a
    // `214/CTable` table date-format edit and a `114/Props` custom-field formula.
    // Save applies them through Document::save() in the SAME container write as
    // the start pins.  Recreated by load_plan(), so a load drops them.
    std::unique_ptr<app::Document> document;
};

PlanState* g_plan = nullptr;
NSWindow* g_window = nil;
int g_argc = 0;
char** g_argv = nullptr;

// The shell's preferences (app/prefs.h).  Loaded from NSUserDefaults before the
// window exists and saved on every change; the date format and the zoom are
// applied to the sheet/Gantt through apply_preferences_to_plan().
app::Preferences g_prefs;

// The last path File > Save wrote (so Save reuses it and Save As always asks).
// Empty until the first save; never the opened file (document.cpp refuses that).
std::string g_target_path;

// Set when the user has already answered the "save changes before closing?"
// prompt, so applicationShouldTerminate does not ask a second time.
bool g_close_confirmed = false;

// --- wave 15: `--smoke` ---------------------------------------------------
// The self-smoke mode.  main() sets these from `--smoke <fixture.mpp> --out <dir>`;
// when g_smoke is true the delegate runs app::smoke_plan() plus the AppKit steps
// and exits with the report's verdict instead of opening the interactive shell
// (see app/smoke.h and docs/handoff/w63.md).  g_smoke also makes show_alert() a
// no-op, because a modal dialog on a CI runner would hang the job instead of
// failing it.
bool g_smoke = false;
std::string g_smoke_fixture;
std::string g_smoke_out;

// The Preferences panel and its controls, built lazily and kept for the life of
// the process (one window, like the main window).
NSWindow* g_prefs_window = nil;
NSPopUpButton* g_date_style_popup = nil;
NSPopUpButton* g_duration_style_popup = nil;
NSPopUpButton* g_timescale_popup = nil;
NSButton* g_shading_checkbox = nil;

// The View > Columns submenu (its items are created from the sheet's own
// columns, so it is the one part of the menu bar that is not in menu_specs()).
NSMenu* g_columns_menu = nil;

// Wave 11: the visible view switcher and usage-period control above the
// document view (built in applicationDidFinishLaunching; target = the
// delegate).  They are the "visible control" beside the View menu items.
NSSegmentedControl* g_view_control = nil;
NSPopUpButton* g_usage_period_popup = nil;

// Defined further down (with the rest of the sheet geometry); ensure_selection_visible
// below runs before that point, so it needs the declaration here.
std::size_t name_column_of(const PlanState& plan);
// Defined with the load path below; the delegate's Insert/Delete handlers use
// them, so they need the declarations here.
void build_editor(PlanState& plan);
void apply_structural_change(PlanState& plan);

// The TaskRow a pending edit/display plan holds for `uid`, or nullptr.  Used by
// the two wave-13 dialogs to prefill from the plan the editor is built from.
const app::TaskRow* plan_task_row(const app::PlanInput& plan, std::uint32_t uid) {
    for (const app::TaskRow& row : plan.tasks) {
        if (row.uid == uid) {
            return &row;
        }
    }
    return nullptr;
}

// One line naming every kind of pending edit, for the status line and the close
// prompt: "1 insert(s), 0 delete(s), 2 field edit(s), 1 link edit(s)".
std::string pending_edit_summary(const app::StructuralEdits& edits) {
    return std::to_string(edits.inserted_count()) + " insert(s), " +
           std::to_string(edits.deleted_count()) + " delete(s), " +
           std::to_string(edits.field_edit_count()) + " field edit(s), " +
           std::to_string(edits.link_edit_count()) + " link edit(s)";
}

// ---------------------------------------------------------------------------
// Small AppKit helpers
// ---------------------------------------------------------------------------

NSString* to_ns(const std::string& text) {
    NSString* string = [[NSString alloc] initWithBytes:text.data()
                                               length:text.size()
                                             encoding:NSUTF8StringEncoding];
    return string != nil ? string : @"";
}

std::string to_std(NSString* string) {
    if (string == nil) {
        return {};
    }
    const char* utf8 = [string UTF8String];
    return utf8 != nullptr ? std::string(utf8) : std::string();
}

NSDictionary* make_attributes(NSFont* font, NSColor* color, NSTextAlignment alignment) {
    NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
    [paragraph setAlignment:alignment];
    [paragraph setLineBreakMode:NSLineBreakByClipping];
    return @{
        NSFontAttributeName : font,
        NSForegroundColorAttributeName : color,
        NSParagraphStyleAttributeName : paragraph
    };
}

void draw_label(NSString* text, NSRect rect, NSDictionary* attributes) {
    if ([text length] == 0 || rect.size.width <= 0.0) {
        return;
    }
    [text drawInRect:rect withAttributes:attributes];
}

void fill_rect(NSRect rect, NSColor* color) {
    [color setFill];
    [NSBezierPath fillRect:rect];
}

void stroke_rect(NSRect rect, NSColor* color, CGFloat width) {
    NSBezierPath* path = [NSBezierPath bezierPathWithRect:rect];
    [path setLineWidth:width];
    [color setStroke];
    [path stroke];
}

void stroke_line(NSPoint from, NSPoint to, NSColor* color, CGFloat width) {
    NSBezierPath* path = [NSBezierPath bezierPath];
    [path moveToPoint:from];
    [path lineToPoint:to];
    [path setLineWidth:width];
    [color setStroke];
    [path stroke];
}

NSTextAlignment alignment_for(core::sheet::CellKind kind) {
    switch (kind) {
        case core::sheet::CellKind::Integer:
        case core::sheet::CellKind::Number:
        case core::sheet::CellKind::Percent:
        case core::sheet::CellKind::Cost:
        case core::sheet::CellKind::Duration: return NSTextAlignmentRight;
        default: return NSTextAlignmentLeft;
    }
}

NSTextAlignment alignment_for(core::sheet::CellAlign align) {
    switch (align) {
        case core::sheet::CellAlign::Right: return NSTextAlignmentRight;
        case core::sheet::CellAlign::Center: return NSTextAlignmentCenter;
        default: return NSTextAlignmentLeft;
    }
}

void show_alert(NSString* title, NSString* information) {
    // --smoke must never block on a modal dialog: a CI runner has nobody to click
    // it, and the run has to fail (non-zero) rather than hang.  The text still
    // reaches the job log, and whichever check caused it is recorded as a failure.
    if (g_smoke) {
        std::fprintf(stderr, "msproject-app: smoke: alert suppressed: %s -- %s\n",
                     to_std(title).c_str(), to_std(information).c_str());
        return;
    }
    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:title];
    [alert setInformativeText:information];
    [alert setAlertStyle:NSAlertStyleWarning];
    [alert addButtonWithTitle:@"OK"];
    [alert runModal];
}

// --smoke: an artifact's size on disk, 0 when it is missing (so "the PDF is not
// empty" is one check instead of a crash).
std::size_t path_size(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    return error ? 0 : static_cast<std::size_t>(size);
}

// ---------------------------------------------------------------------------
// Load and rebuild: .mpp -> PlanState -> the two views
// ---------------------------------------------------------------------------

// --- wave-11 view helpers -------------------------------------------------

bool showing_task_view(const PlanState& plan) {
    return plan.view_mode == app::ViewMode::task_sheet;
}

// Rows the document view draws, whichever grid is showing.
std::size_t active_row_count(const PlanState& plan) {
    if (showing_task_view(plan)) {
        return plan.row_map.size();
    }
    return plan.secondary_sheet != nullptr ? plan.secondary_sheet->row_count() : 0;
}

std::string make_status_text(const PlanState& plan) {
    if (!showing_task_view(plan)) {
        std::string text;
        if (plan.view_mode == app::ViewMode::resource_sheet) {
            text = std::to_string(plan.loaded.resources.size()) + " resources, " +
                   std::to_string(plan.loaded.assignments.size()) + " assignments";
            if (plan.secondary_sheet != nullptr) {
                text += "  |  " + std::to_string(plan.secondary_sheet->row_count()) + " row(s)";
            }
        } else {
            text = std::to_string(plan.usage_view.table.rows.size()) + " rows, " +
                   std::to_string(plan.usage_view.table.periods.size()) + " " +
                   core::usage::period_name(plan.usage_period) + " period(s)";
            text += "  |  total " +
                    app::format_duration(plan.usage_view.grand_total_minutes *
                                             core::view::kTicksPerMinute,
                                         plan.format, g_prefs.duration_style);
            if (plan.usage.unplaced_assignments != 0) {
                text += ", " + std::to_string(plan.usage.unplaced_assignments) +
                        " unplaced assignment(s)";
            }
        }
        text += "  |  view: ";
        text += app::view_mode_title(plan.view_mode);
        text += "  |  " + app::preferences_summary(g_prefs);
        text += "  |  " + std::to_string(plan.warnings) + " reader warnings";
        if (!plan.path.empty()) {
            text += "  |  " + plan.path;
        }
        return text;
    }

    const app::PlanSummary summary = app::summarize(plan.view);
    std::string text = std::to_string(summary.tasks) + " tasks, " +
                       std::to_string(summary.links) + " links";
    if (plan.skipped_links != 0) {
        text += " (" + std::to_string(plan.skipped_links) + " skipped)";
    }
    text += ", " + std::to_string(summary.summaries) + " summaries, " +
            std::to_string(summary.milestones) + " milestones, " +
            std::to_string(summary.critical) + " critical";
    if (!core::sched::is_na(summary.first_start) && !core::sched::is_na(summary.last_finish)) {
        text += "  |  plan " + core::sheet::format_date_text(
                                    app::date_value_from_date32(summary.first_start), plan.format);
        text += " - " + core::sheet::format_date_text(
                            app::date_value_from_date32(summary.last_finish), plan.format);
    }
    if (plan.sheet != nullptr) {
        const std::size_t edited =
            app::collect_start_edits(plan.records, plan.loaded).size();
        if (edited != 0) {
            text += "  |  " + std::to_string(edited) + " start(s) edited";
        }
    }
    if (plan.filter_active) {
        text += "  |  filter: " + plan.filter_description + " (" +
                std::to_string(plan.filter_rows.size()) + " of " +
                std::to_string(plan.records.row_count()) + " shown)";
    }
    if (!plan.edits.empty()) {
        text += "  |  pending: " + pending_edit_summary(plan.edits);
    }
    if (plan.document != nullptr) {
        // Wave 16: the two document-level edits have no row, so they get their own
        // fragment (a date-format byte and a formula are not a StructuralEdit).
        std::string pending;
        if (plan.document->view_state_edits().set_date_format) {
            pending = "table date format " + std::to_string(static_cast<unsigned>(
                                                 plan.document->view_state_edits().date_format));
        }
        if (plan.document->custom_formula().any()) {
            if (!pending.empty()) {
                pending += ", ";
            }
            pending += plan.document->custom_formula().operation ==
                               app::CustomFormulaEdit::Operation::clear
                           ? "clear custom-field formulas"
                           : "custom-field formula";
        }
        if (!pending.empty()) {
            text += "  |  pending: " + pending;
        }
    }
    if (!plan.formula_set.columns.empty()) {
        text += "  |  " + std::to_string(plan.formula_set.columns.size()) + " formula column(s)";
    }
    text += "  |  " + app::preferences_summary(g_prefs);
    text += "  |  " + std::to_string(plan.warnings) + " reader warnings";
    if (!plan.path.empty()) {
        text += "  |  " + plan.path;
    }
    return text;
}

double gantt_origin_x(const PlanState& plan) {
    return plan.sheet_width + kSheetDivider + kGanttLeftPad;
}

// The selection has to be on a row that is actually displayed: applying a filter
// can hide the selected row, and the arrow keys then have nowhere to move from.
void ensure_selection_visible(PlanState& plan) {
    if (plan.sheet == nullptr || plan.row_map.size() == 0) {
        return;
    }
    if (plan.sheet->has_selection() &&
        plan.row_map.ordinal_of(plan.sheet->anchor().row) != core::sheet::kNoIndex) {
        return;
    }
    plan.sheet->select_cell(plan.row_map.rows.front(), name_column_of(plan));
}

// Re-evaluates the task sheet's computed columns into the RecordSet and
// refreshes the sheet's cached per-row/column facts.  Read-only: nothing here
// can become a save edit (document.h diffs task starts only).
void refresh_computed_columns(PlanState& plan) {
    if (plan.sheet == nullptr || plan.formula_set.columns.empty()) {
        return;
    }
    plan.computed.set_format(plan.format);
    plan.computed.evaluate(plan.records);
    plan.sheet->refresh();
}

// Builds the editor -- the RecordSet, the file's computed columns and the
// SheetModel -- from `plan.base`.  Called by the load path and, after a pending
// structural edit, by apply_structural_change(): core::sheet::SheetModel has no
// row insert/delete, so a structural change is a rebuild.
//
// The old sheet's column visibility is carried over by FIELD id for the ordinary
// columns (the custom fields and formula columns are preference state and are
// applied from g_prefs).  A structural edit therefore does not reset which
// identity columns the user hid.
void build_editor(PlanState& plan) {
    std::vector<std::pair<core::view::FieldId, bool>> visibility;
    if (plan.sheet != nullptr) {
        visibility.reserve(plan.sheet->column_count());
        for (std::size_t index = 0; index < plan.sheet->column_count(); ++index) {
            visibility.emplace_back(plan.sheet->column(index).field,
                                    plan.sheet->column_visible(index));
        }
    }

    plan.records = app::make_record_set(plan.base);
    plan.computed = core::formula::ComputedColumns{};
    plan.computed_error.clear();
    if (!app::build_computed_columns(plan.computed, plan.formula_set.columns,
                                     plan.computed_error)) {
        // The error is reported in the status line; the columns that registered
        // still work, exactly as a parse failure did in wave 11.
    }
    plan.computed.set_format(plan.format);
    plan.computed.evaluate(plan.records);

    plan.sheet = std::make_unique<core::sheet::SheetModel>(
        plan.records, app::task_editor_columns(plan.formula_set));
    plan.sheet->set_format(plan.format);
    app::apply_edit_policy(*plan.sheet);
    app::apply_formula_read_only(*plan.sheet, plan.formula_set.columns);
    app::apply_column_visibility(*plan.sheet, plan.formula_set, g_prefs);
    for (const std::pair<core::view::FieldId, bool>& entry : visibility) {
        if (app::is_custom_column(plan.formula_set, entry.first)) {
            continue;  // preference state, applied above
        }
        const std::size_t column = plan.sheet->column_of_field(entry.first);
        if (column != core::sheet::kNoIndex) {
            plan.sheet->set_column_visible(column, entry.second);
        }
    }
}

// Rebuilds the resource sheet or the usage grid for the current mode and usage
// period.  A no-op in the Task Sheet view, whose sheet stays live.
void rebuild_secondary_view(PlanState& plan) {
    plan.secondary_records.clear();
    plan.secondary_sheet.reset();
    plan.secondary_columns.clear();
    plan.secondary_width = 0.0;
    plan.usage = core::usage::UsageProjection{};
    plan.usage_view = app::UsageView{};
    if (showing_task_view(plan)) {
        return;
    }
    plan.usage = core::usage::project_usage(plan.usage_input, plan.calendar, plan.usage_period);
    if (plan.view_mode == app::ViewMode::resource_sheet) {
        plan.secondary_records =
            app::make_resource_view_records(plan.base, &plan.usage.resources);
        plan.secondary_sheet = std::make_unique<core::sheet::SheetModel>(
            plan.secondary_records, app::resource_sheet_columns());
    } else {
        const bool resource = plan.view_mode == app::ViewMode::resource_usage;
        plan.usage_view = app::make_usage_view(plan.usage, resource, plan.base, plan.format);
        plan.secondary_records = app::make_usage_record_set(plan.usage_view);
        plan.secondary_sheet = std::make_unique<core::sheet::SheetModel>(
            plan.secondary_records, app::usage_sheet_columns(plan.usage_view));
    }
    plan.secondary_sheet->set_format(plan.format);
    // These views are read-only this wave: no column may start an edit.
    for (std::size_t column = 0; column < plan.secondary_sheet->column_count(); ++column) {
        plan.secondary_sheet->set_column_read_only(column, true);
    }
    plan.secondary_columns = plan.secondary_sheet->layout_columns(plan.metrics, 0);
    plan.secondary_width =
        static_cast<double>(plan.secondary_sheet->sheet_width_points(plan.metrics, 0));
}

// Rebuilds everything derived from the RecordSet: the computed columns, the
// visible-row map (which a filter restricts), the sheet geometry, the Gantt
// (from the VISIBLE rows, which is how collapse and filtering work), the
// resource/usage grid when that view is showing, the status line and the window
// title.
void rebuild_view(PlanState& plan) {
    if (plan.sheet == nullptr) {
        return;
    }
    refresh_computed_columns(plan);
    plan.view = app::sync_plan_from_records(plan.base, plan.records);
    if (!showing_task_view(plan)) {
        // Selection applies to task views only: the resource/usage grids are
        // read-only and have no outline or Gantt.
        plan.row_map = app::RowMap{};
        plan.columns.clear();
        plan.sheet_width = 0.0;
        plan.gantt = core::gantt::GanttLayout{};
        plan.gantt_width = 0.0;
        rebuild_secondary_view(plan);
        plan.status_text = make_status_text(plan);
        return;
    }
    plan.row_map = app::build_row_map(*plan.sheet);
    if (plan.filter_active) {
        // A filter is a restriction of the displayed row list, never of the
        // RecordSet: the same rows stay editable and the save diff is unchanged.
        app::RowMap filtered;
        filtered.rows = app::restrict_to_filter(plan.row_map.rows, plan.filter_rows);
        filtered.uids.reserve(filtered.rows.size());
        for (const std::size_t row : filtered.rows) {
            filtered.uids.push_back(plan.sheet->row_uid(row));
        }
        plan.row_map = std::move(filtered);
    }
    ensure_selection_visible(plan);
    plan.columns = plan.sheet->layout_columns(plan.metrics, 0);
    plan.sheet_width = static_cast<double>(plan.sheet->sheet_width_points(plan.metrics, 0));

    // Wave 25 (W25-1/S1): the displayed rows are RecordSet rows; the source
    // resolves each through its uid (the file may show the project row, which
    // plan.view does not carry).
    core::gantt::GanttSource source = app::make_gantt_source_for_displayed_rows(
        plan.view, plan.records, plan.row_map.rows, {}, app::today_date32());
    core::gantt::GanttStyle style = app::default_gantt_style();
    // A real font metric, so "does this label fit inside the bar?" is decided by
    // the font actually drawn (gantt.h leaves this to the shell).
    style.measure_text = [](std::string_view text) -> double {
        NSString* string = [[NSString alloc] initWithBytes:text.data()
                                                    length:text.size()
                                                  encoding:NSUTF8StringEncoding];
        if (string == nil) {
            return static_cast<double>(text.size()) * 7.0;
        }
        const NSSize size = [string sizeWithAttributes:@{
            NSFontAttributeName : [NSFont systemFontOfSize:kLabelFontSize]
        }];
        return static_cast<double>(size.width);
    };
    plan.gantt = core::gantt::build_gantt(
        source, style, app::timescale_for_zoom(plan.view, app::gantt_zoom(g_prefs.zoom)));
    plan.gantt_width = std::max(plan.gantt.content_width + kRightMargin, kMinGanttWidth);
    plan.status_text = make_status_text(plan);
}

// Rebuilds the editor and the views after a structural edit changed `base`.  The
// sheet's cell-edit history is cleared with the rebuild (a new SheetModel), which
// is what keeps the two undo stacks chronological: this wave's rule is that a
// structural edit ends the cell-edit history, so Cmd-Z can never undo a cell edit
// "past" a later structural change.
void apply_structural_change(PlanState& plan) {
    // Fold the CURRENT cell values back into the plan before the rebuild: the
    // sheet's edits (a Start, say) are still pending file changes, and building
    // from `loaded` alone would silently drop them.  without_structural_edits()
    // recovers "the file plus those edits", so apply() then re-applies this edit
    // list on top without double-counting the earlier ones.
    const app::PlanInput edited =
        plan.edits.without_structural_edits(plan.view, plan.loaded);
    plan.base = plan.edits.apply(edited);
    plan.usage_input = app::usage_input_from_plan(plan.base);
    build_editor(plan);
    rebuild_view(plan);
}

bool load_plan(PlanState& plan, const std::string& path) {
    plan = PlanState{};
    plan.path = path;
    // Wave 16: the pending table date-format / formula holder for this file.
    plan.document = std::make_unique<app::Document>(path);
    g_target_path.clear();
    g_close_confirmed = false;
    try {
        const mpp::Document document = mpp::load_file(path);
        plan.warnings = document.warnings.size();
        plan.loaded = app::plan_input_from_document(document);
        plan.loaded.source_name = std::filesystem::path(path).filename().string();
        plan.skipped_links = plan.loaded.skipped_links;

        plan.format = core::sheet::SheetFormat{};
        app::apply_preferences(g_prefs, plan.format);
        plan.metrics = app::default_sheet_metrics();
        // Wave-11 view state comes from the preferences; the usage input comes
        // straight from the reader's measured assignment bridge.
        plan.view_mode = g_prefs.view;
        plan.usage_period = g_prefs.usage_period;
        plan.usage_input = app::usage_input_from_document(document);
        // Wave 12: the file's own formulas (`114/Props` through core/props and
        // core/formula) and its stored custom fields.  A file without a Props
        // stream simply has none; a formula that does not parse is reported and
        // skipped, never evaluated as a partial guess.
        plan.file_formulas = app::read_file_formulas(app::props_stream_from_file(path));
        plan.formula_set = app::formula_columns(plan.file_formulas);
        plan.base = plan.edits.apply(plan.loaded);
        build_editor(plan);

        // Start with the first row selected: the Gantt opens with a bar
        // highlighted and the keyboard has somewhere to go.
        const std::size_t name_column = [&plan]() -> std::size_t {
            for (std::size_t index = 0; index < plan.sheet->column_count(); ++index) {
                if (plan.sheet->column(index).field == app::name_field_id()) {
                    return index;
                }
            }
            return 0;
        }();
        if (plan.sheet->visible_row_count() != 0) {
            plan.sheet->select_cell(plan.sheet->visible_row(0), name_column);
        }

        rebuild_view(plan);
        plan.message = "loaded " + std::to_string(plan.loaded.tasks.size()) + " task(s), " +
                       std::to_string(plan.loaded.resources.size()) + " resource(s), " +
                       std::to_string(plan.loaded.assignments.size()) + " assignment(s)";
        plan.message_is_error = false;
        if (!plan.computed_error.empty()) {
            plan.message += "  |  computed columns: " + plan.computed_error;
            plan.message_is_error = true;
        }
        if (!plan.formula_set.columns.empty()) {
            plan.message += "  |  " + std::to_string(plan.formula_set.columns.size()) +
                            " formula column(s) from the file";
        }
        if (!plan.formula_set.warnings.empty()) {
            plan.message += "  |  " + std::to_string(plan.formula_set.warnings.size()) +
                            " formula warning(s)";
            plan.message_is_error = true;
        }
        return true;
    } catch (const std::exception& error) {
        plan.error = error.what();
        plan.message = "load failed: " + plan.error;
        plan.message_is_error = true;
        return false;
    }
}

std::string choose_fixture() {
    if (g_argc > 1 && g_argv != nullptr && g_argv[1] != nullptr && g_argv[1][0] != '\0') {
        return std::string(g_argv[1]);
    }
#if defined(APP_FIXTURE_DIR)
    {
        const std::filesystem::path probe{APP_FIXTURE_DIR};
        for (const char* name : {"w3-o1.mpp", "holiday1.mpp", "base.mpp"}) {
            const std::filesystem::path candidate = probe / name;
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error)) {
                return candidate.string();
            }
        }
    }
#endif
#if defined(APP_TEST_DATA_DIR)
    {
        const std::filesystem::path data{APP_TEST_DATA_DIR};
        const std::filesystem::path fixture = data / "synthetic-base.mpp";
        std::error_code error;
        if (std::filesystem::is_regular_file(fixture, error)) {
            return fixture.string();
        }
    }
#endif
    return {};
}

double content_width(const PlanState& plan) {
    if (plan.sheet == nullptr) {
        return 900.0;
    }
    if (!showing_task_view(plan)) {
        return std::max(plan.secondary_width + kGanttLeftPad + kRightMargin, 800.0);
    }
    return std::max(plan.sheet_width + kSheetDivider + kGanttLeftPad + plan.gantt_width, 800.0);
}

double content_height(const PlanState& plan) {
    const double body =
        plan.sheet != nullptr ? static_cast<double>(active_row_count(plan)) * kRowHeight : 0.0;
    return std::max(kHeaderHeight + body + kFooterHeight + kBottomPadding, 360.0);
}

// Name column index (the column a selection is anchored on), 0 when absent.
std::size_t name_column_of(const PlanState& plan) {
    if (plan.sheet == nullptr) {
        return 0;
    }
    for (std::size_t index = 0; index < plan.sheet->column_count(); ++index) {
        if (plan.sheet->column(index).field == app::name_field_id()) {
            return index;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// The x where a row's disclosure box starts: the Name column's left edge plus the
// row's outline indent.  The text is drawn to the right of the reserved box, so
// this arithmetic is shared by the drawing and the hit test (see
// disclosure_gutter_hit).
double disclosure_x(const PlanState& plan, double column_x, std::size_t row) {
    return column_x + static_cast<double>(plan.sheet->indent_points(row, plan.metrics));
}

double text_x_for_name(const PlanState& plan, double column_x, std::size_t row) {
    return disclosure_x(plan, column_x, row) + kDisclosureSize + kDisclosureGap;
}

bool disclosure_gutter_hit(const PlanState& plan, double x, std::size_t row) {
    if (plan.sheet == nullptr || row >= plan.sheet->row_count() || !plan.sheet->is_summary(row)) {
        return false;
    }
    const std::size_t name_column = name_column_of(plan);
    if (name_column >= plan.columns.size()) {
        return false;
    }
    const double left = disclosure_x(plan, static_cast<double>(plan.columns[name_column].x), row);
    return x >= left - 2.0 && x <= left + kDisclosureSize + 2.0;
}

void draw_sheet(PlanState& plan, NSRect bounds, NSFont* body, NSFont* bold, NSFont* header_font,
                NSFont* small, NSColor* text_color, NSColor* red, NSColor* grid, NSColor* header_bg,
                NSColor* summary_bg, NSColor* dim_text, NSColor* selected_bg, NSColor* selected_cell,
                NSColor* focus_color, NSColor* edit_bg) {
    const double sheet_width = plan.sheet_width;
    const double body_top = kHeaderHeight;
    // The DISPLAYED rows: RowMap is the visible outline rows, restricted by the
    // filter, so the sheet, the Gantt and the keyboard all iterate the same list.
    const std::vector<std::size_t>& displayed = plan.row_map.rows;
    const std::size_t row_count = displayed.size();
    const double body_bottom = body_top + static_cast<double>(row_count) * kRowHeight;
    const app::SelectedTask selected = app::selected_task(*plan.sheet, plan.row_map);

    if (row_count > 0) {
        // Summary rows get the light band MS Project draws them with.
        for (std::size_t ordinal = 0; ordinal < row_count; ++ordinal) {
            const std::size_t row = displayed[ordinal];
            if (row == core::sheet::kNoIndex) {
                break;
            }
            if (plan.sheet->is_summary(row)) {
                fill_rect(NSMakeRect(0.0, body_top + static_cast<double>(ordinal) * kRowHeight,
                                     sheet_width, kRowHeight),
                          summary_bg);
            }
        }
        // The selected row's band, then the selected cells, then the focus cell.
        if (selected.has_selection && selected.ordinal != core::sheet::kNoIndex) {
            fill_rect(NSMakeRect(0.0, body_top + static_cast<double>(selected.ordinal) * kRowHeight,
                                 sheet_width, kRowHeight),
                      selected_bg);
        }
        for (std::size_t ordinal = 0; ordinal < row_count; ++ordinal) {
            const std::size_t row = displayed[ordinal];
            if (row == core::sheet::kNoIndex) {
                break;
            }
            for (const core::sheet::ColumnSegment& segment : plan.columns) {
                if (!plan.sheet->is_selected(row, segment.column)) {
                    continue;
                }
                fill_rect(NSMakeRect(static_cast<double>(segment.x),
                                     body_top + static_cast<double>(ordinal) * kRowHeight,
                                     static_cast<double>(segment.width), kRowHeight),
                          selected_cell);
            }
        }
        if (selected.has_selection && selected.ordinal != core::sheet::kNoIndex) {
            // The focus cell (where the caret would be) gets a border.  The focus
            // is a MODEL column index, so find its laid-out segment rather than
            // indexing the visible-column vector with it.
            const std::size_t focus_column = plan.sheet->focus().column;
            for (const core::sheet::ColumnSegment& segment : plan.columns) {
                if (segment.column != focus_column) {
                    continue;
                }
                stroke_rect(NSMakeRect(static_cast<double>(segment.x),
                                       body_top + static_cast<double>(selected.ordinal) * kRowHeight,
                                       static_cast<double>(segment.width), kRowHeight),
                            focus_color, 2.0);
                break;
            }
        }
    }

    // Header band and its bottom border.
    fill_rect(NSMakeRect(0.0, 0.0, sheet_width, kHeaderHeight), header_bg);
    stroke_line(NSMakePoint(0.0, kHeaderHeight - 0.5),
                NSMakePoint(sheet_width, kHeaderHeight - 0.5), grid, 1.0);

    // Column separators + header titles.
    for (const core::sheet::ColumnSegment& segment : plan.columns) {
        const double x = static_cast<double>(segment.x);
        const double width = static_cast<double>(segment.width);
        stroke_line(NSMakePoint(x, 0.0), NSMakePoint(x, body_bottom), grid, 1.0);
        const core::sheet::SheetColumn& column = plan.sheet->column(segment.column);
        draw_label(to_ns(plan.sheet->column_title(segment.column)),
                   NSMakeRect(x + kTextInset, kHeaderHeight / 2.0 - 8.0, width - 2.0 * kTextInset,
                              16.0),
                   make_attributes(header_font, dim_text, alignment_for(column.kind)));
    }

    // Row lines across the whole document so the two halves read as one grid.
    for (std::size_t row = 0; row <= row_count; ++row) {
        const double y = body_top + static_cast<double>(row) * kRowHeight;
        stroke_line(NSMakePoint(0.0, y), NSMakePoint(bounds.size.width, y), grid, 1.0);
    }

    // Cell text through the sheet model: it owns date/duration formatting.
    const std::size_t name_column = name_column_of(plan);
    double name_left = 0.0;  // the Name column's laid-out x (model column index)
    for (const core::sheet::ColumnSegment& segment : plan.columns) {
        if (segment.column == name_column) {
            name_left = static_cast<double>(segment.x);
        }
    }
    for (std::size_t ordinal = 0; ordinal < row_count; ++ordinal) {
        const std::size_t row = displayed[ordinal];
        if (row == core::sheet::kNoIndex) {
            break;
        }
        const double y = body_top + static_cast<double>(ordinal) * kRowHeight;
        for (const core::sheet::ColumnSegment& segment : plan.columns) {
            const bool is_name = segment.column == name_column;
            const bool editing_here = plan.sheet->editing() &&
                                      plan.sheet->edit_target().row == row &&
                                      plan.sheet->edit_target().column == segment.column;
            // app::cell_text() is the sheet's own text except for a Duration cell
            // under the hours/days preference (app/prefs.h).
            const std::string text =
                editing_here ? plan.sheet->edit_text()
                             : app::cell_text(*plan.sheet, row, segment.column, g_prefs);
            const double left = static_cast<double>(segment.x);
            const double x = is_name ? text_x_for_name(plan, name_left, row) : left + kTextInset;
            if (editing_here) {
                fill_rect(NSMakeRect(left, y, static_cast<double>(segment.width), kRowHeight),
                          edit_bg);
                stroke_rect(NSMakeRect(left + 0.5, y + 0.5,
                                       static_cast<double>(segment.width) - 1.0, kRowHeight - 1.0),
                            focus_color, 1.0);
            }
            if (text.empty() && !editing_here) {
                continue;
            }
            const core::sheet::CellStyle style = plan.sheet->cell_style(row, segment.column);
            draw_label(to_ns(editing_here ? text + "|" : text),
                       NSMakeRect(x, y + 3.0,
                                  static_cast<double>(segment.width) - (x - left) - kTextInset,
                                  15.0),
                       make_attributes(style.bold ? bold : body, style.red ? red : text_color,
                                       alignment_for(style.align)));
        }
        // Outline disclosure box: a small bordered square with - or +.
        if (plan.sheet->is_summary(row) && plan.sheet->has_children(row)) {
            const double left = disclosure_x(plan, name_left, row);
            const double top = y + (kRowHeight - kDisclosureSize) / 2.0;
            stroke_rect(NSMakeRect(left, top, kDisclosureSize, kDisclosureSize), dim_text, 1.0);
            draw_label(plan.sheet->is_collapsed(row) ? @"+" : @"-",
                       NSMakeRect(left, top - 1.0, kDisclosureSize, kDisclosureSize),
                       make_attributes(small, text_color, NSTextAlignmentCenter));
        }
    }
}

void draw_gantt(PlanState& plan, NSRect bounds, NSFont* header_font, NSFont* label_font,
                NSColor* text_color, NSColor* dim_text, NSColor* grid, NSColor* header_bg,
                NSColor* weekend_bg, NSColor* bar_color, NSColor* bar_fill, NSColor* critical_color,
                NSColor* critical_fill, NSColor* summary_color, NSColor* link_color,
                NSColor* status_color, NSColor* selected_bg, NSColor* focus_color,
                bool working_day_shading) {
    const double gantt_x = gantt_origin_x(plan);
    const double gantt_width = plan.gantt_width;
    const double body_top = kHeaderHeight;
    const std::size_t row_count = plan.row_map.size();
    const double body_bottom = body_top + static_cast<double>(row_count) * kRowHeight;
    const app::SelectedTask selected = app::selected_task(*plan.sheet, plan.row_map);

    const std::vector<core::gantt::HeaderCell> major =
        plan.gantt.header(core::gantt::HeaderTier::major, 0.0, gantt_width);
    const std::vector<core::gantt::HeaderCell> minor =
        plan.gantt.header(core::gantt::HeaderTier::minor, 0.0, gantt_width);

    // Non-working shading over the row band (weekends; holidays when supplied).
    // The preference turns it off; the spans still exist in the layout (they
    // never move a bar), the shell simply does not draw them.
    if (working_day_shading) {
        for (const core::gantt::NonworkingSpan& span :
             plan.gantt.nonworking_spans(0.0, gantt_width)) {
            fill_rect(NSMakeRect(gantt_x + span.x, body_top, span.width, body_bottom - body_top),
                      weekend_bg);
        }
    }

    // The selected row's band, so the sheet and the chart agree at a glance.
    if (selected.has_selection && selected.ordinal != core::sheet::kNoIndex) {
        fill_rect(NSMakeRect(gantt_x, body_top + static_cast<double>(selected.ordinal) * kRowHeight,
                             gantt_width, kRowHeight),
                  selected_bg);
    }

    // Header: band, weekend day cells, tier separators, labels.
    fill_rect(NSMakeRect(gantt_x, 0.0, gantt_width, kHeaderHeight), header_bg);
    if (working_day_shading) {
        for (const core::gantt::HeaderCell& cell : minor) {
            if (cell.weekday == 0 || cell.weekday == 6) {
                fill_rect(NSMakeRect(gantt_x + cell.x, kHeaderTierHeight, cell.width,
                                     kHeaderTierHeight),
                          weekend_bg);
            }
        }
    }
    stroke_line(NSMakePoint(gantt_x, kHeaderTierHeight),
                NSMakePoint(gantt_x + gantt_width, kHeaderTierHeight), grid, 1.0);
    stroke_line(NSMakePoint(gantt_x, kHeaderHeight - 0.5),
                NSMakePoint(gantt_x + gantt_width, kHeaderHeight - 0.5), grid, 1.0);

    for (const core::gantt::HeaderCell& cell : major) {
        draw_label(to_ns(cell.label),
                   NSMakeRect(gantt_x + cell.x + 2.0, 1.0, cell.width - 4.0, 14.0),
                   make_attributes(header_font, dim_text, NSTextAlignmentCenter));
    }
    for (const core::gantt::HeaderCell& cell : minor) {
        draw_label(to_ns(cell.label),
                   NSMakeRect(gantt_x + cell.x, kHeaderTierHeight + 1.0, cell.width, 14.0),
                   make_attributes(header_font,
                                   cell.weekday == 0 || cell.weekday == 6 ? dim_text : text_color,
                                   NSTextAlignmentCenter));
    }

    // Day grid lines in the body.
    for (const core::gantt::HeaderCell& cell : minor) {
        const double x = gantt_x + cell.x;
        stroke_line(NSMakePoint(x, body_top), NSMakePoint(x, body_bottom), grid, 1.0);
    }

    // Dependency routes, then bars, then the arrowheads on top so a tip that
    // lands exactly on a bar edge stays visible.
    for (const core::gantt::LinkRoute& route : plan.gantt.links) {
        if (route.points.size() < 2) {
            continue;
        }
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path moveToPoint:NSMakePoint(gantt_x + route.points.front().x,
                                      body_top + route.points.front().y)];
        for (std::size_t index = 1; index < route.points.size(); ++index) {
            [path lineToPoint:NSMakePoint(gantt_x + route.points[index].x,
                                          body_top + route.points[index].y)];
        }
        [path setLineWidth:1.0];
        [(route.critical ? critical_color : link_color) setStroke];
        [path stroke];
    }

    for (const core::gantt::Bar& bar : plan.gantt.bars) {
        const bool bar_selected = selected.has_selection && bar.task_uid == selected.uid;
        const double x = gantt_x + bar.rect.x;
        const double y = body_top + bar.rect.y;
        const double width = bar.rect.w;
        const double height = bar.rect.h;
        if (bar.kind == core::gantt::BarKind::milestone) {
            const double centre_x = x + width / 2.0;
            const double centre_y = y + height / 2.0;
            NSBezierPath* diamond = [NSBezierPath bezierPath];
            [diamond moveToPoint:NSMakePoint(centre_x, y)];
            [diamond lineToPoint:NSMakePoint(x + width, centre_y)];
            [diamond lineToPoint:NSMakePoint(centre_x, y + height)];
            [diamond lineToPoint:NSMakePoint(x, centre_y)];
            [diamond closePath];
            [(bar.critical ? critical_color : summary_color) setFill];
            [diamond fill];
            if (bar_selected) {
                [diamond setLineWidth:2.0];
                [focus_color setStroke];
                [diamond stroke];
            }
        } else if (bar.kind == core::gantt::BarKind::summary) {
            fill_rect(NSMakeRect(x, y, width, height), summary_color);
            // Approximate the product's bracket ends with two short ticks down.
            fill_rect(NSMakeRect(x, y + height, 2.0, 4.0), summary_color);
            fill_rect(NSMakeRect(x + width - 2.0, y + height, 2.0, 4.0), summary_color);
            if (bar_selected) {
                stroke_rect(NSMakeRect(x - 1.0, y - 1.0, width + 2.0, height + 2.0), focus_color,
                            2.0);
            }
        } else {
            fill_rect(NSMakeRect(x, y, width, height), bar.critical ? critical_fill : bar_fill);
            if (bar.fill.w > 0.0) {
                fill_rect(NSMakeRect(x, y, bar.fill.w, height),
                          bar.critical ? critical_color : bar_color);
            }
            stroke_rect(NSMakeRect(x, y, width, height),
                        bar_selected ? focus_color : (bar.critical ? critical_color : bar_color),
                        bar_selected ? 2.0 : 1.0);
        }
        if (bar.label.visible && !bar.label_text.empty()) {
            draw_label(to_ns(bar.label_text),
                       NSMakeRect(gantt_x + bar.label.rect.x, body_top + bar.label.rect.y,
                                  bar.label.rect.w + 8.0, kRowHeight),
                       make_attributes(label_font, text_color, NSTextAlignmentLeft));
        }
    }

    for (const core::gantt::LinkRoute& route : plan.gantt.links) {
        NSBezierPath* head = [NSBezierPath bezierPath];
        [head moveToPoint:NSMakePoint(gantt_x + route.arrow.tip.x, body_top + route.arrow.tip.y)];
        [head lineToPoint:NSMakePoint(gantt_x + route.arrow.wing[0].x,
                                      body_top + route.arrow.wing[0].y)];
        [head lineToPoint:NSMakePoint(gantt_x + route.arrow.wing[1].x,
                                      body_top + route.arrow.wing[1].y)];
        [head closePath];
        [(route.critical ? critical_color : link_color) setFill];
        [head fill];
    }

    // Status-date marker (Project's status line): dashed, full height.
    if (plan.gantt.has_status_line() && std::isfinite(plan.gantt.status_x)) {
        const double x = gantt_x + plan.gantt.status_x;
        NSBezierPath* line = [NSBezierPath bezierPath];
        [line moveToPoint:NSMakePoint(x, 0.0)];
        [line lineToPoint:NSMakePoint(x, body_bottom)];
        const CGFloat pattern[2] = {4.0, 3.0};
        [line setLineDash:pattern count:2 phase:0.0];
        [line setLineWidth:1.0];
        [status_color setStroke];
        [line stroke];
    }

    // Divider between the sheet and the Gantt.
    stroke_line(NSMakePoint(plan.sheet_width + 0.5, 0.0),
                NSMakePoint(plan.sheet_width + 0.5, bounds.size.height), grid, 1.0);
}

void draw_footer(const PlanState& plan, NSRect bounds, NSFont* font, NSColor* dim_text,
                 NSColor* red, NSColor* grid) {
    const double row_count = static_cast<double>(active_row_count(plan));
    const double y = kHeaderHeight + row_count * kRowHeight;
    stroke_line(NSMakePoint(0.0, y + 0.5), NSMakePoint(bounds.size.width, y + 0.5), grid, 1.0);
    draw_label(to_ns(plan.status_text), NSMakeRect(8.0, y + 5.0, bounds.size.width - 16.0, 14.0),
               make_attributes(font, dim_text, NSTextAlignmentLeft));
    if (!plan.message.empty()) {
        draw_label(to_ns(plan.message),
                   NSMakeRect(8.0, y + 23.0, bounds.size.width - 16.0, 14.0),
                   make_attributes(font, plan.message_is_error ? red : dim_text,
                                   NSTextAlignmentLeft));
    }
}

// The resource sheet and the two usage grids: one flat grid across the whole
// window, no Gantt, no selection and no outline.  A usage grid's last row is its
// totals row and is drawn bold on the summary band.
void draw_plain_grid(PlanState& plan, NSRect bounds, NSFont* body, NSFont* bold,
                     NSFont* header_font, NSColor* text_color, NSColor* grid,
                     NSColor* header_bg, NSColor* summary_bg, NSColor* dim_text) {
    if (plan.secondary_sheet == nullptr) {
        return;
    }
    core::sheet::SheetModel& sheet = *plan.secondary_sheet;
    const double width = plan.secondary_width;
    const std::size_t rows = sheet.row_count();
    const bool usage = app::view_mode_is_usage(plan.view_mode);
    const double body_top = kHeaderHeight;
    const double body_bottom = body_top + static_cast<double>(rows) * kRowHeight;

    if (usage && rows > 0) {
        fill_rect(NSMakeRect(0.0,
                             body_top + static_cast<double>(rows - 1) * kRowHeight,
                             width, kRowHeight),
                  summary_bg);
    }
    fill_rect(NSMakeRect(0.0, 0.0, width, kHeaderHeight), header_bg);
    stroke_line(NSMakePoint(0.0, kHeaderHeight - 0.5),
                NSMakePoint(width, kHeaderHeight - 0.5), grid, 1.0);

    for (const core::sheet::ColumnSegment& segment : plan.secondary_columns) {
        const double x = static_cast<double>(segment.x);
        const double column_width = static_cast<double>(segment.width);
        stroke_line(NSMakePoint(x, 0.0), NSMakePoint(x, body_bottom), grid, 1.0);
        const core::sheet::SheetColumn& column = sheet.column(segment.column);
        draw_label(to_ns(sheet.column_title(segment.column)),
                   NSMakeRect(x + kTextInset, kHeaderHeight / 2.0 - 8.0,
                              column_width - 2.0 * kTextInset, 16.0),
                   make_attributes(header_font, dim_text, alignment_for(column.kind)));
    }

    for (std::size_t row = 0; row <= rows; ++row) {
        const double y = body_top + static_cast<double>(row) * kRowHeight;
        stroke_line(NSMakePoint(0.0, y), NSMakePoint(bounds.size.width, y), grid, 1.0);
    }

    for (std::size_t row = 0; row < rows; ++row) {
        const bool total_row = usage && rows > 0 && row + 1 == rows;
        const double y = body_top + static_cast<double>(row) * kRowHeight;
        for (const core::sheet::ColumnSegment& segment : plan.secondary_columns) {
            const std::string text = app::cell_text(sheet, row, segment.column, g_prefs);
            if (text.empty()) {
                continue;
            }
            const core::sheet::CellStyle style = sheet.cell_style(row, segment.column);
            draw_label(to_ns(text),
                       NSMakeRect(static_cast<double>(segment.x) + kTextInset, y + 3.0,
                                  static_cast<double>(segment.width) - 2.0 * kTextInset, 15.0),
                       make_attributes(total_row || style.bold ? bold : body, text_color,
                                       alignment_for(style.align)));
        }
    }
}

void draw_plan(PlanState& plan, NSRect bounds) {
    NSColor* white = [NSColor colorWithSRGBRed:1.0 green:1.0 blue:1.0 alpha:1.0];
    fill_rect(bounds, white);

    NSFont* body = [NSFont systemFontOfSize:10.0];
    NSFont* bold = [NSFont boldSystemFontOfSize:10.0];
    NSFont* header_font = [NSFont systemFontOfSize:9.0];
    NSFont* label_font = [NSFont systemFontOfSize:kLabelFontSize];
    NSFont* footer_font = [NSFont systemFontOfSize:9.0];

    NSColor* text_color = [NSColor colorWithSRGBRed:0.10 green:0.10 blue:0.10 alpha:1.0];
    NSColor* dim_text = [NSColor colorWithSRGBRed:0.38 green:0.38 blue:0.38 alpha:1.0];
    NSColor* red = [NSColor colorWithSRGBRed:0.72 green:0.10 blue:0.10 alpha:1.0];
    NSColor* grid = [NSColor colorWithSRGBRed:0.87 green:0.87 blue:0.87 alpha:1.0];
    NSColor* header_bg = [NSColor colorWithSRGBRed:0.94 green:0.94 blue:0.94 alpha:1.0];
    NSColor* weekend_bg = [NSColor colorWithSRGBRed:0.925 green:0.925 blue:0.925 alpha:1.0];
    NSColor* summary_bg = [NSColor colorWithSRGBRed:0.955 green:0.955 blue:0.955 alpha:1.0];
    NSColor* bar_color = [NSColor colorWithSRGBRed:0.22 green:0.42 blue:0.68 alpha:1.0];
    NSColor* bar_fill = [NSColor colorWithSRGBRed:0.60 green:0.73 blue:0.87 alpha:1.0];
    NSColor* critical_color = [NSColor colorWithSRGBRed:0.72 green:0.16 blue:0.16 alpha:1.0];
    NSColor* critical_fill = [NSColor colorWithSRGBRed:0.90 green:0.62 blue:0.62 alpha:1.0];
    NSColor* summary_color = [NSColor colorWithSRGBRed:0.16 green:0.16 blue:0.16 alpha:1.0];
    NSColor* link_color = [NSColor colorWithSRGBRed:0.36 green:0.38 blue:0.42 alpha:1.0];
    NSColor* status_color = [NSColor colorWithSRGBRed:0.86 green:0.24 blue:0.10 alpha:1.0];
    NSColor* selected_bg = [NSColor colorWithSRGBRed:0.90 green:0.94 blue:0.99 alpha:1.0];
    NSColor* selected_cell = [NSColor colorWithSRGBRed:0.78 green:0.87 blue:0.98 alpha:1.0];
    NSColor* focus_color = [NSColor colorWithSRGBRed:0.12 green:0.33 blue:0.66 alpha:1.0];
    NSColor* edit_bg = [NSColor colorWithSRGBRed:1.0 green:1.0 blue:0.93 alpha:1.0];

    if (plan.sheet == nullptr) {
        NSString* message = plan.error.empty() ? @"No project loaded. Use File > Open."
                                               : to_ns(plan.error);
        draw_label(message, NSMakeRect(12.0, 12.0, bounds.size.width - 24.0, 18.0),
                   make_attributes(bold, red, NSTextAlignmentLeft));
        return;
    }

    if (!showing_task_view(plan)) {
        draw_plain_grid(plan, bounds, body, bold, header_font, text_color, grid, header_bg,
                        summary_bg, dim_text);
        draw_footer(plan, bounds, footer_font, dim_text, red, grid);
        return;
    }

    draw_sheet(plan, bounds, body, bold, header_font, label_font, text_color, red, grid, header_bg,
               summary_bg, dim_text, selected_bg, selected_cell, focus_color, edit_bg);
    draw_gantt(plan, bounds, header_font, label_font, text_color, dim_text, grid, header_bg,
               weekend_bg, bar_color, bar_fill, critical_color, critical_fill, summary_color,
               link_color, status_color, selected_bg, focus_color, g_prefs.working_day_shading);
    draw_footer(plan, bounds, footer_font, dim_text, red, grid);
}

// ---------------------------------------------------------------------------
// Model actions shared by the view (keys/clicks) and the delegate (menu/save).
// ---------------------------------------------------------------------------

void note(PlanState& plan, std::string message, bool is_error) {
    plan.message = std::move(message);
    plan.message_is_error = is_error;
}

// Commits the open edit session.  A rejected commit keeps the session open (so
// the user can correct the text) and puts EditOutcome::error in the status line.
bool commit_open_edit(PlanState& plan) {
    core::sheet::SheetModel& sheet = *plan.sheet;
    if (!sheet.editing()) {
        return false;
    }
    // commit_edit() clears the edit target on success, so capture the column name
    // first (otherwise the status line would name column 0).
    const std::string column_title = sheet.column_title(sheet.edit_target().column);
    const core::sheet::EditOutcome outcome = sheet.commit_edit(false);
    if (!outcome.ok) {
        note(plan, "rejected: " + outcome.error, true);
        return false;
    }
    if (!outcome.changed) {
        note(plan, "value unchanged", false);
        return true;
    }
    // A cell edit is chronologically after every pending structural edit, so a
    // redo of one of those must not survive it.
    plan.edits.clear_redo();
    note(plan, "edited " + column_title, false);
    return true;
}

bool begin_edit_at(PlanState& plan, std::size_t row, std::size_t column) {
    // commit_open_edit() has already run if an edit was open.
    // Wave 25 (W25-1/S1, the R5 guard): app::begin_cell_edit refuses the
    // project-summary row (uid 0) and a declared read-only column.  Every macOS
    // edit path (double-click, Return, typing, Backspace) enters here.
    std::string error;
    if (!app::begin_cell_edit(*plan.sheet, row, column, error)) {
        note(plan, error, true);
        return false;
    }
    note(plan, "editing " + plan.sheet->column_title(column), false);
    return true;
}

// Arrow-key movement: rows through the visible list, columns through the visible
// columns.  A selection is always on a visible cell.
void move_selection(PlanState& plan, int rows, int columns) {
    core::sheet::SheetModel& sheet = *plan.sheet;
    if (!sheet.has_selection()) {
        if (sheet.visible_row_count() != 0) {
            sheet.select_cell(sheet.visible_row(0), name_column_of(plan));
        }
        return;
    }
    const std::size_t row = sheet.anchor().row;
    const std::size_t column = sheet.anchor().column;
    if (rows != 0) {
        const std::size_t moved = app::row_after(plan.row_map, row, rows);
        if (moved != core::sheet::kNoIndex) {
            sheet.select_cell(moved, column);
        }
    }
    if (columns != 0) {
        const std::size_t display = sheet.display_column(column);
        if (display != core::sheet::kNoIndex && sheet.visible_column_count() != 0) {
            const long long last = static_cast<long long>(sheet.visible_column_count()) - 1;
            long long target = static_cast<long long>(display) + columns;
            target = std::max(0LL, std::min(target, last));
            const std::size_t next = sheet.visible_column(static_cast<std::size_t>(target));
            if (next != core::sheet::kNoIndex) {
                sheet.select_cell(row, next);
            }
        }
    }
}

// Cmd-Left / Cmd-Right (and the disclosure box): collapse/expand the selected
// summary row.  The model clamps the selection if it hid the selected cell, and
// the caller rebuilds the visible-row map and the Gantt afterwards.
bool toggle_selected_collapse(PlanState& plan) {
    const app::SelectedTask selected = app::selected_task(*plan.sheet, plan.row_map);
    if (!selected.has_selection) {
        return false;
    }
    if (!plan.sheet->is_summary(selected.row)) {
        note(plan, "only a summary row can be collapsed", true);
        return false;
    }
    if (!app::toggle_row(*plan.sheet, selected.row)) {
        return false;
    }
    note(plan, plan.sheet->is_collapsed(selected.row) ? "collapsed subtasks"
                                                      : "expanded subtasks",
         false);
    return true;
}

// The sheet cell under a point, or kNoIndex.  These three live here (plain C++
// in the file's anonymous namespace, not methods) so the view's event handling
// stays a thin translation of AppKit events into model calls.
std::size_t sheet_row_at(const PlanState& plan, double y) {
    if (plan.sheet == nullptr || y < kHeaderHeight) {
        return core::sheet::kNoIndex;
    }
    const double ordinal = std::floor((y - kHeaderHeight) / kRowHeight);
    if (ordinal < 0.0) {
        return core::sheet::kNoIndex;
    }
    return plan.row_map.row_at(static_cast<std::size_t>(ordinal));
}

std::size_t sheet_column_at(const PlanState& plan, double x) {
    for (const core::sheet::ColumnSegment& segment : plan.columns) {
        const double left = static_cast<double>(segment.x);
        if (x >= left && x < left + static_cast<double>(segment.width)) {
            return segment.column;
        }
    }
    return core::sheet::kNoIndex;
}

// Printable input: the UTF-8 text of the event, when it is a real character.
std::string printable_text(NSEvent* event) {
    NSString* characters = [event characters];
    if (characters == nil || [characters length] == 0) {
        return {};
    }
    const unichar first = [characters characterAtIndex:0];
    if (first < 0x20 || first == 0x7F) {
        return {};
    }
    return to_std(characters);
}

// ---------------------------------------------------------------------------
// Preferences, menu state, export and clipboard: plain functions so the view and
// the delegate share one implementation (and so main.mm stays mostly mechanical).
// ---------------------------------------------------------------------------

bool plan_is_dirty(const PlanState& plan) {
    if (plan.sheet == nullptr) {
        return false;
    }
    // Wave 12: a pending Insert/Delete Task is a pending file change too.
    // Wave 16: so is a pending table date-format or custom-field formula edit.
    return !plan.edits.empty() ||
           (plan.document != nullptr && plan.document->has_pending_edits()) ||
           !app::collect_start_edits(plan.records, plan.loaded).empty();
}

void load_preferences_from_defaults() {
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    NSString* stored =
        [defaults stringForKey:to_ns(std::string(app::kPreferencesDefaultsKey))];
    if (stored != nil) {
        app::parse_preferences(to_std(stored), g_prefs);
    }
}

void save_preferences_to_defaults() {
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    [defaults setObject:to_ns(app::serialize_preferences(g_prefs))
                 forKey:to_ns(std::string(app::kPreferencesDefaultsKey))];
}

// Applies the date format and the timescale to the open document and redraws.
void apply_preferences_to_plan(PlanState& plan) {
    app::apply_preferences(g_prefs, plan.format);
    if (plan.sheet == nullptr) {
        return;
    }
    if (plan.sheet->editing()) {
        plan.sheet->cancel_edit();
    }
    plan.sheet->set_format(plan.format);
    rebuild_view(plan);
}

app::MenuState current_menu_state() {
    app::MenuState state;
    if (g_plan == nullptr || g_plan->sheet == nullptr) {
        return state;
    }
    state.has_document = true;
    state.task_view = app::view_mode_is_task(g_plan->view_mode);
    state.editing = g_plan->sheet->editing();
    state.has_selection = g_plan->sheet->has_selection();
    // Either stack may hold the next step: a structural edit clears the sheet's
    // history, so the sheet's stack only ever holds steps made after it.
    state.can_undo = g_plan->sheet->can_undo() || g_plan->edits.can_undo();
    state.can_redo = g_plan->sheet->can_redo() || g_plan->edits.can_redo();
    state.can_copy = state.has_selection;
    state.can_paste = state.has_selection;
    state.dirty = plan_is_dirty(*g_plan);
    state.filter_active = g_plan->filter_active;
    for (std::size_t row = 0; row < g_plan->sheet->row_count(); ++row) {
        if (!g_plan->sheet->is_summary(row) || !g_plan->sheet->has_children(row)) {
            continue;
        }
        if (g_plan->sheet->is_collapsed(row)) {
            state.any_collapsed = true;
        } else {
            state.any_expanded = true;
        }
    }
    return state;
}

// Walks the installed menu bar and applies app::menu_item_enabled() to every
// item the menu model created (tag == MenuAction), plus the dynamic Columns
// submenu's check marks.  Called after anything that can change the state.
void refresh_menu_bar() {
    NSMenu* main_menu = [NSApp mainMenu];
    if (main_menu == nil) {
        return;
    }
    const app::MenuState state = current_menu_state();
    for (NSMenuItem* top in [main_menu itemArray]) {
        NSMenu* submenu = [top submenu];
        if (submenu == nil) {
            continue;
        }
        // W26-6.1: recurses into the View drop-downs.
        refresh_menu_items(submenu, state);
    }
    if (g_columns_menu != nil) {
        for (NSMenuItem* item in [g_columns_menu itemArray]) {
            if ([item isSeparatorItem] || [item tag] < 0) {
                continue;
            }
            const NSInteger column = [item tag];
            bool known = false;
            bool visible = false;
            if (g_plan != nullptr && g_plan->sheet != nullptr &&
                static_cast<std::size_t>(column) < g_plan->sheet->column_count()) {
                visible = g_plan->sheet->column_visible(static_cast<std::size_t>(column));
                known = true;
            }
            [item setState:(known && visible) ? NSControlStateValueOn : NSControlStateValueOff];
            // Column visibility describes the task sheet; the resource/usage
            // views have their own fixed column sets.
            [item setEnabled:(known && state.task_view)];
        }
    }
}

void set_zoom_level(PlanState& plan, app::ZoomLevel level) {
    if (g_prefs.zoom == level) {
        note(plan, std::string("timescale is already ") + app::zoom_level_name(level), false);
        return;
    }
    g_prefs.zoom = level;
    save_preferences_to_defaults();
    rebuild_view(plan);
    note(plan, std::string("timescale: ") + app::zoom_level_name(level), false);
}

void step_zoom(PlanState& plan, int delta) {
    const int next = static_cast<int>(g_prefs.zoom) + delta;
    if (next < 0) {
        note(plan, "already at the finest timescale (day)", false);
        return;
    }
    if (next > 2) {
        note(plan, "already at the coarsest timescale (month)", false);
        return;
    }
    set_zoom_level(plan, static_cast<app::ZoomLevel>(next));
}

// Wave 11: switch the window between the four views.  The choice persists and
// the derived state (row map / Gantt / secondary grid) is rebuilt.
void set_view_mode(PlanState& plan, app::ViewMode mode) {
    if (plan.view_mode == mode) {
        note(plan, std::string("already showing the ") + app::view_mode_title(mode), false);
        return;
    }
    if (plan.sheet != nullptr && plan.sheet->editing()) {
        plan.sheet->cancel_edit();
    }
    plan.view_mode = mode;
    g_prefs.view = mode;
    save_preferences_to_defaults();
    rebuild_view(plan);
    note(plan, std::string("view: ") + app::view_mode_title(mode), false);
}

void set_usage_period(PlanState& plan, core::usage::Period period) {
    if (plan.usage_period == period) {
        note(plan, std::string("usage period is already ") + core::usage::period_name(period),
             false);
        return;
    }
    plan.usage_period = period;
    g_prefs.usage_period = period;
    save_preferences_to_defaults();
    if (app::view_mode_is_usage(plan.view_mode)) {
        rebuild_view(plan);
    }
    note(plan, std::string("usage period: ") + core::usage::period_name(period), false);
}

void clear_filter(PlanState& plan) {
    if (!plan.filter_active) {
        note(plan, "no filter is active", false);
        return;
    }
    plan.filter_active = false;
    plan.filter = core::view::FilterDefinition{};
    plan.filter_description.clear();
    plan.filter_rows.clear();
    rebuild_view(plan);
    note(plan, "filter cleared", false);
}

void toggle_column_visibility(PlanState& plan, std::size_t column) {
    if (plan.sheet == nullptr || column >= plan.sheet->column_count()) {
        return;
    }
    const bool visible = plan.sheet->column_visible(column);
    const std::string title = plan.sheet->column_title(column);
    plan.sheet->set_column_visible(column, !visible);
    const core::view::FieldId field = plan.sheet->column(column).field;
    if (app::is_custom_column(plan.formula_set, field)) {
        // Wave 12: a stored custom-field column or a formula column is preference
        // state, per column (app/prefs.h ColumnVisibility).  Ordinary columns are
        // not.
        if (app::set_column_visible(g_prefs, field, !visible,
                                    app::custom_column_default_visible(field, g_prefs))) {
            save_preferences_to_defaults();
        }
    }
    note(plan, (visible ? "hid " : "showed ") + title, false);
}

std::string selection_label(const PlanState& plan) {
    if (plan.sheet == nullptr) {
        return {};
    }
    const app::SelectedTask selected = app::selected_task(*plan.sheet, plan.row_map);
    if (!selected.has_selection) {
        return {};
    }
    std::string name = plan.sheet->cell_text(selected.row, name_column_of(plan));
    if (name.empty()) {
        name = "task uid " + std::to_string(selected.uid);
    }
    return name + " (uid " + std::to_string(selected.uid) + ")";
}

// Copy/paste through the general pasteboard, using core/sheet's ClipboardBlock
// text form (tab-separated, so it also exchanges with a spreadsheet).
void copy_selection(PlanState& plan) {
    if (plan.sheet == nullptr) {
        return;
    }
    const core::sheet::ClipboardBlock block = plan.sheet->copy_selection();
    if (block.empty()) {
        note(plan, "nothing to copy: select a cell first", true);
        return;
    }
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];
    [pasteboard setString:to_ns(block.to_text()) forType:NSPasteboardTypeString];
    note(plan, "copied " + std::to_string(block.rows) + "x" +
                   std::to_string(block.columns) + " cell(s)",
         false);
}

void paste_from_pasteboard(PlanState& plan) {
    if (plan.sheet == nullptr) {
        return;
    }
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    NSString* text = [pasteboard stringForType:NSPasteboardTypeString];
    if (text == nil) {
        note(plan, "nothing to paste", true);
        return;
    }
    const core::sheet::ClipboardBlock block =
        core::sheet::ClipboardBlock::from_text(to_std(text));
    if (block.empty()) {
        note(plan, "the clipboard does not hold tabular text", true);
        return;
    }
    // Wave 25 review: SheetModel::paste writes the RecordSet directly; the
    // uid-0 project row would show the paste but the save diff skips it.
    if (plan.sheet->row_uid(plan.sheet->anchor().row) == 0) {
        note(plan, "the project summary row is read-only", true);
        return;
    }
    const core::sheet::PasteResult result = plan.sheet->paste(block);
    if (!result.ok) {
        note(plan, "paste refused: " + result.error, true);
        return;
    }
    plan.edits.clear_redo();
    note(plan, "pasted " + std::to_string(result.cells) + " cell(s), " +
                   std::to_string(result.changed) + " changed",
         false);
}

// File > Export PDF.  The request is the window's own state: the filtered row
// list, the visible columns, the preference formats and the current selection
// (named in the footer -- core/render has no selection concept).
void run_export_panel(PlanState& plan, app::ReportKind kind) {
    if (plan.sheet == nullptr) {
        note(plan, "no project to export", true);
        return;
    }
    if (!app::view_mode_is_task(plan.view_mode)) {
        // Both reports are built from the task sheet and the Gantt; the
        // resource/usage grids are not render sources this wave.
        note(plan, "PDF export is available in the Task Sheet view", true);
        return;
    }
    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setCanCreateDirectories:YES];
    [panel setNameFieldStringValue:to_ns(app::export_default_name(plan.path, kind))];
    if (!plan.path.empty()) {
        [panel setDirectoryURL:[NSURL
                                  fileURLWithPath:to_ns(std::filesystem::path(plan.path)
                                                            .parent_path()
                                                            .string())]];
    }
    if (@available(macOS 11.0, *)) {
        UTType* pdf_type = [UTType typeWithIdentifier:@"com.adobe.pdf"];
        if (pdf_type != nil) {
            [panel setAllowedContentTypes:@[ pdf_type ]];
        }
    }
    if ([panel runModal] != NSModalResponseOK) {
        note(plan, "export cancelled", false);
        return;
    }
    NSURL* url = [panel URL];
    if (url == nil) {
        return;
    }

    app::ExportRequest request;
    request.kind = kind;
    request.records = &plan.records;
    if (plan.filter_active) {
        request.rows = plan.filter_rows;
        request.rows_are_filtered = true;  // an empty zero-match filter prints nothing
    }
    for (const std::size_t column : plan.sheet->visible_columns()) {
        if (column < plan.sheet->column_count()) {
            request.columns.push_back(plan.sheet->column(column));
        }
    }
    request.format = plan.format;
    request.metrics = plan.metrics;
    request.preferences = g_prefs;
    request.title = plan.loaded.title.empty() ? "Project report" : plan.loaded.title;
    // Wave 25 (W25-11/S14 review): the project-summary row (uid 0) is a row,
    // not a task; the macOS PDF must say "N of N" like Print and the Qt shell.
    // A filtered list is counted exactly (an empty zero-match filter is 0).
    const std::size_t total = app::task_row_count(plan.records);
    const std::size_t shown =
        plan.filter_active ? app::task_row_count(plan.records, plan.filter_rows) : total;
    request.subtitle =
        app::export_subtitle(kind, plan.filter_active ? plan.filter_description : std::string(),
                             shown, total);
    request.footer = app::export_footer(g_prefs, selection_label(plan));
    for (const app::LinkRow& row : plan.view.links) {
        core::gantt::LinkInput link;
        link.predecessor_uid = row.predecessor_uid;
        link.successor_uid = row.successor_uid;
        link.type = row.type;
        link.critical = row.critical;
        request.links.push_back(link);
    }
    request.timescale = app::timescale_for_zoom(plan.view, app::gantt_zoom(g_prefs.zoom));
    request.auto_timescale = true;
    request.working_day_shading = g_prefs.working_day_shading;

    const app::ExportOutcome outcome = app::export_report(request, to_std([url path]));
    if (!outcome.ok) {
        note(plan, "export failed: " + outcome.error, true);
        show_alert(@"Could not export the PDF", to_ns(outcome.error));
        return;
    }
    note(plan, "exported " + std::to_string(outcome.rows) + " row(s), " +
                   std::to_string(outcome.pages) + " page(s) to " + outcome.path,
         false);
    std::printf("msproject-app: exported %s\n", outcome.path.c_str());
    std::fflush(stdout);
}

// Printing (wave 14): the request is the report the window is showing.  The
// Gantt report is the combined table + chart, which is also what File > Export
// PDF (Gantt) writes; a filter restricts the rows exactly as it does for the
// screen and the export.  The date is formatted here (once) and the page
// geometry comes from app/page_layout.h, so this function never reads a clock
// after it returns and the layout stays pure.
app::PrintRequest make_print_request(const PlanState& plan) {
    app::PrintRequest request;
    request.kind = app::PrintKind::gantt;
    request.project_name = plan.loaded.title;
    if (request.project_name.empty()) {
        request.project_name = plan.loaded.source_name;
    }
    request.report_title = "Gantt report";
    // Wave 24 (W24-1 review): the subtitle counts TASKS.  The RecordSet
    // includes the synthetic project row (uid 0) when the file shows it, so
    // counting records would say "6 of 6" where the Qt shell says "5 of 5".
    // WAVE 26 (W26-0.1): `total` is the TASK count and feeds the subtitle only.
    // The printed rows are RecordSet rows, so they come from the RecordSet --
    // the old `0 .. total-1` loop printed the project row plus all but the last
    // task when row 0 was shown.  The list is built in app/page_layout.h so the
    // headless app_layout test can fail that arithmetic on wave24-order.mpp.
    const std::size_t total = plan.view.tasks.size();
    request.rows = app::print_row_list(plan.records.row_count(), plan.filter_rows,
                                       plan.filter_active);
    std::size_t shown = 0;
    for (const std::size_t row : request.rows) {
        if (row < plan.records.row_count() && plan.records.uid(row) != 0) {
            ++shown;
        }
    }
    request.subtitle = app::export_subtitle(
        app::ReportKind::gantt,
        plan.filter_active ? plan.filter_description : std::string(), shown, total);
    request.date_text = core::sheet::format_date_text(
        app::date_value_from_date32(app::today_date32()), plan.format);
    request.spec = app::PageSpec::a4_landscape();
    return request;
}

void show_about() {
    show_alert(@"About MS Project Shell",
               @"A native macOS shell over the portable MS Project core.\n\n"
                @"Open a .mpp, browse the sheet and Gantt, edit cells, filter the "
                @"task list, and save edited task start dates with File > Save. "
                @"Start dates are the only edit the file writer persists; every "
                @"other edit stays in memory (see the README).\n\n"
                @"Version 0.4 (wave 11, stream W47).  The View menu and the "
                 @"control bar switch between the task sheet, the resource sheet and "
                 @"the task/resource usage grids; the computed columns are read-only.");
}

void show_shortcuts() {
    show_alert(@"Keyboard Shortcuts",
               @"Arrows: move through visible rows and columns\n"
                @"Tab / Shift-Tab: next / previous column\n"
                @"Return: start or commit an edit\n"
                @"Esc: cancel an edit\n"
                @"Space: select the whole row, or one cell again\n"
                @"Double-click: edit a cell\n"
                @"Backspace: edit the text, or clear the cell\n"
                @"Cmd-Z / Cmd-Shift-Z: undo / redo\n"
                @"Cmd-C / Cmd-V: copy / paste\n"
                @"Cmd-F / Cmd-Shift-F: filter / clear filter\n"
                @"Cmd-Shift-D: working-day shading\n"
                @"Cmd-1 / Cmd-2 / Cmd-3: day / week / month timescale\n"
                @"Cmd-Option-1..4: Task Sheet / Resource Sheet / Task Usage / Resource Usage\n"
                @"Cmd-= / Cmd--: zoom in / out\n"
                @"Cmd-S / Cmd-Shift-S: save / save as\n"
                @"Cmd-Shift-E / Cmd-Option-E: export PDF (sheet / Gantt)\n"
                @"Cmd-P / Cmd-Shift-P: print / save the printout as PDF");
}

}  // namespace

// ---------------------------------------------------------------------------
// The document view: everything is drawn by draw_plan above; the view only turns
// AppKit events into calls on the model and the pure helpers.
// ---------------------------------------------------------------------------

@interface PlanView : NSView
- (void)refreshAfterChange;
- (void)selectionChanged;
@end

@implementation PlanView

- (BOOL)isFlipped {
    return YES;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    if (g_plan != nullptr) {
        draw_plan(*g_plan, [self bounds]);
    }
}

// Rebuild the derived state after anything that changes rows or values (an edit,
// an undo, a collapse, a filter), resize the document to the new content, update
// the window's edited dot and the menu, and redraw.
- (void)refreshAfterChange {
    if (g_plan != nullptr && g_plan->sheet != nullptr) {
        rebuild_view(*g_plan);
        [self setFrameSize:NSMakeSize(content_width(*g_plan), content_height(*g_plan))];
        [g_window setDocumentEdited:plan_is_dirty(*g_plan)];
    }
    refresh_menu_bar();
    [self selectionChanged];
}

// Redraw and scroll the selected sheet row into view.
- (void)selectionChanged {
    [self setNeedsDisplay:YES];
    refresh_menu_bar();
    if (g_plan == nullptr || g_plan->sheet == nullptr) {
        return;
    }
    const app::SelectedTask selected = app::selected_task(*g_plan->sheet, g_plan->row_map);
    if (!selected.has_selection) {
        return;
    }
    // Scroll by DISPLAYED ordinal, not by the outline ordinal: a filter can
    // hide rows above the selection, so the outline's own y would be too low.
    const std::size_t ordinal = g_plan->row_map.ordinal_of(selected.row);
    if (ordinal == core::sheet::kNoIndex) {
        return;
    }
    const NSRect visible = [self visibleRect];
    [self scrollRectToVisible:NSMakeRect(visible.origin.x,
                                         kHeaderHeight + static_cast<double>(ordinal) * kRowHeight,
                                         visible.size.width, kRowHeight)];
}

- (void)mouseDown:(NSEvent*)event {
    [[self window] makeFirstResponder:self];
    if (g_plan == nullptr || g_plan->sheet == nullptr) {
        return;
    }
    PlanState& plan = *g_plan;
    if (!app::view_mode_has_selection(plan.view_mode)) {
        // The resource and usage grids are read-only and have no cell selection
        // (wave 11): a click is not an edit.  The view switch is the control bar.
        [self setNeedsDisplay:YES];
        return;
    }
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    const bool shift = ([event modifierFlags] & NSEventModifierFlagShift) != 0;

    // A rejected commit keeps its session open (the error is in the status line),
    // so a click elsewhere must not silently move the selection out from under
    // it: Esc cancels, and the session is otherwise finished before the click.
    if (plan.sheet->editing() && !commit_open_edit(plan)) {
        [self setNeedsDisplay:YES];
        return;
    }

    if (point.y >= kHeaderHeight && point.x < plan.sheet_width) {
        const std::size_t row = sheet_row_at(plan, point.y);
        const std::size_t column = sheet_column_at(plan, point.x);
        if (row == core::sheet::kNoIndex || column == core::sheet::kNoIndex) {
            return;
        }
        // The disclosure box is a control, not a cell: select the row, then
        // toggle its outline state (the model refuses a non-summary row).
        if (disclosure_gutter_hit(plan, point.x, row)) {
            app::select_cell(*plan.sheet, row, column);
            toggle_selected_collapse(plan);
            [self refreshAfterChange];
            return;
        }
        if ([event clickCount] >= 2) {
            if (app::select_cell(*plan.sheet, row, column)) {
                begin_edit_at(plan, row, column);
            }
            [self selectionChanged];
            return;
        }
        if (shift) {
            plan.sheet->extend_to(row, column);
            note(plan, "selection extended", false);
        } else if (app::select_cell(*plan.sheet, row, column)) {
            note(plan, plan.sheet->column_title(column) + " selected", false);
        }
        [self selectionChanged];
        return;
    }

    if (point.x >= gantt_origin_x(plan)) {
        const double x = point.x - gantt_origin_x(plan);
        const double y = point.y - kHeaderHeight;
        const std::size_t row = app::select_from_gantt(*plan.sheet, plan.row_map, plan.gantt,
                                                       x, y, name_column_of(plan));
        if (row == core::sheet::kNoIndex) {
            note(plan, "nothing under the pointer", false);
        } else {
            note(plan, "selected from the Gantt", false);
        }
        [self selectionChanged];
    }
}

- (void)keyDown:(NSEvent*)event {
    if (g_plan == nullptr || g_plan->sheet == nullptr) {
        [super keyDown:event];
        return;
    }
    PlanState& plan = *g_plan;
    if (!app::view_mode_has_selection(plan.view_mode)) {
        // Navigation and editing belong to the task sheet; in the read-only
        // grids the arrow keys do nothing (the scroll view still scrolls).
        [super keyDown:event];
        return;
    }
    core::sheet::SheetModel& sheet = *plan.sheet;
    const unsigned short code = [event keyCode];
    const NSEventModifierFlags flags = [event modifierFlags];
    const bool shift = (flags & NSEventModifierFlagShift) != 0;
    const bool command = (flags & NSEventModifierFlagCommand) != 0;

    // --- an edit session owns the keyboard, except for the menu's Cmd keys ---
    if (sheet.editing()) {
        if (code == 53) {  // Esc
            sheet.cancel_edit();
            note(plan, "edit cancelled", false);
            [self setNeedsDisplay:YES];
            return;
        }
        if (code == 36 || code == 76) {  // Return / keypad Enter
            if (commit_open_edit(plan)) {
                [self refreshAfterChange];
            } else {
                [self setNeedsDisplay:YES];
            }
            return;
        }
        if (code == 48) {  // Tab commits and moves one column
            if (!commit_open_edit(plan)) {
                [self setNeedsDisplay:YES];  // the session stays open on a rejection
                return;
            }
            move_selection(plan, 0, shift ? -1 : 1);
            [self refreshAfterChange];
            return;
        }
        if (code == 51 || code == 117) {  // Backspace / forward delete
            std::string text = sheet.edit_text();
            if (text.empty()) {
                sheet.cancel_edit();
                note(plan, "edit cancelled", false);
            } else {
                // Drop one UTF-8 code point (the last byte sequence's lead byte
                // and continuations).
                std::size_t cut = text.size() - 1;
                while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
                    --cut;
                }
                text.resize(cut);
                sheet.set_edit_text(text);
            }
            [self setNeedsDisplay:YES];
            return;
        }
        const std::string typed = printable_text(event);
        if (!typed.empty() && !command) {
            sheet.set_edit_text(sheet.edit_text() + typed);
            [self setNeedsDisplay:YES];
            return;
        }
        [super keyDown:event];
        return;
    }

    // --- no edit session: navigation, collapse, and edit entry ---
    switch (code) {
        case 123:  // Left
            if (command) {
                if (toggle_selected_collapse(plan)) {
                    [self refreshAfterChange];
                }
            } else {
                move_selection(plan, 0, -1);
                [self selectionChanged];
            }
            return;
        case 124:  // Right
            if (command) {
                if (toggle_selected_collapse(plan)) {
                    [self refreshAfterChange];
                }
            } else {
                move_selection(plan, 0, 1);
                [self selectionChanged];
            }
            return;
        case 125:  // Down
            move_selection(plan, 1, 0);
            [self selectionChanged];
            return;
        case 126:  // Up
            move_selection(plan, -1, 0);
            [self selectionChanged];
            return;
        case 36:
        case 76: {  // Return starts an edit on the selected cell
            const app::SelectedTask selected = app::selected_task(sheet, plan.row_map);
            if (selected.has_selection && begin_edit_at(plan, selected.row, sheet.anchor().column)) {
                [self setNeedsDisplay:YES];
            }
            return;
        }
        case 48:  // Tab moves right without editing
            move_selection(plan, 0, shift ? -1 : 1);
            [self selectionChanged];
            return;
        case 49: {  // Space toggles between the cell and the whole row
            const bool whole_row = app::toggle_row_selection(sheet);
            note(plan, whole_row ? "whole row selected" : "one cell selected", false);
            [self selectionChanged];
            return;
        }
        case 51:
        case 117: {  // Backspace/delete with no edit open clears the cell
            const app::SelectedTask selected = app::selected_task(sheet, plan.row_map);
            if (selected.has_selection &&
                begin_edit_at(plan, selected.row, sheet.anchor().column)) {
                sheet.set_edit_text(std::string());
                [self setNeedsDisplay:YES];
            }
            return;
        }
        default: break;
    }

    const std::string typed = printable_text(event);
    if (!typed.empty() && !command) {
        // Typing replaces the cell, as Project does.
        const app::SelectedTask selected = app::selected_task(sheet, plan.row_map);
        if (selected.has_selection && begin_edit_at(plan, selected.row, sheet.anchor().column)) {
            sheet.set_edit_text(typed);
            [self setNeedsDisplay:YES];
        }
        return;
    }
    [super keyDown:event];
}

@end

// ---------------------------------------------------------------------------
// Wave 14: printing.  One offscreen view draws one page per -rectForPage:, and
// every band (title, rules, body top/bottom, footer, rows per page) comes from
// app/page_layout.h -- the AppKit code drives that model, never the reverse.
// Print... and Save as PDF... both build the same app::PrintJob and run it
// through this view, so the two routes cannot paginate differently.
// ---------------------------------------------------------------------------

struct PrintViewState {
    PlanState* plan = nullptr;
    app::PrintJob job;
    core::gantt::GanttLayout gantt;
    double table_width = 0.0;
};

struct PrintTableColumn {
    std::size_t model_column = core::sheet::kNoIndex;
    double x = 0.0;
    double width = 0.0;
};

namespace {

std::size_t print_model_column(const PlanState& plan, std::string_view field_name) {
    const core::view::FieldId field = app::task_field_id(field_name);
    if (field == 0 || plan.sheet == nullptr) {
        return core::sheet::kNoIndex;
    }
    for (std::size_t index = 0; index < plan.sheet->column_count(); ++index) {
        if (plan.sheet->column(index).field == field) {
            return index;
        }
    }
    return core::sheet::kNoIndex;
}

// The five columns a printed table shows, in order: identity, name, the two
// dates and the duration.  Widths fit the printable width; Name takes the rest.
std::vector<PrintTableColumn> print_table_columns(const PlanState& plan, double width) {
    struct Spec {
        const char* name;
        double width;
    };
    const Spec specs[] = {
        {"ID", 34.0}, {"Name", 0.0}, {"Start", 78.0}, {"Finish", 78.0}, {"Duration", 62.0},
    };
    double fixed = 0.0;
    for (const Spec& spec : specs) {
        if (spec.width > 0.0) {
            fixed += spec.width;
        }
    }
    const double name_width = std::max(60.0, width - fixed);
    std::vector<PrintTableColumn> columns;
    double x = 0.0;
    for (const Spec& spec : specs) {
        const std::size_t model = print_model_column(plan, spec.name);
        if (model == core::sheet::kNoIndex) {
            continue;
        }
        PrintTableColumn column;
        column.model_column = model;
        column.x = x;
        column.width = spec.width > 0.0 ? spec.width : name_width;
        columns.push_back(column);
        x += column.width;
    }
    return columns;
}

}  // namespace

@interface PrintView : NSView {
@public
    PrintViewState* state;
}
- (instancetype)initWithPlan:(PlanState*)plan job:(const app::PrintJob&)job;
- (void)drawPage:(std::size_t)page origin:(NSPoint)origin;
@end

@implementation PrintView

- (BOOL)isFlipped {
    return YES;
}

- (instancetype)initWithPlan:(PlanState*)plan job:(const app::PrintJob&)job {
    const double page_height = job.spec.height;
    const double view_height =
        std::max(page_height, page_height * static_cast<double>(job.pages.size()));
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, job.spec.width, view_height)];
    if (self == nil) {
        return nil;
    }
    state = new PrintViewState();
    state->plan = plan;
    state->job = job;
    const double printable = job.spec.printable_width();
    if (job.kind == app::PrintKind::gantt) {
        state->table_width = std::min(340.0, printable * 0.55);
        app::PlanInput printed;
        printed.title = plan->loaded.title;
        printed.project_start = plan->view.project_start;
        printed.project_finish = plan->view.project_finish;
        // Wave 24 (W24-1/R6): the plan has no synthetic project row, so a
        // RecordSet row index is not a task index when row 0 is shown.  Map
        // every printed row through its uid; uid 0 prints the project bar.
        for (const std::size_t row : job.rows) {
            const std::uint32_t uid =
                row < plan->records.row_count() ? plan->records.uid(row) : 0;
            if (uid == 0) {
                printed.tasks.push_back(app::project_summary_row(plan->view));
                continue;
            }
            for (const app::TaskRow& task : plan->view.tasks) {
                if (task.uid == uid) {
                    printed.tasks.push_back(task);
                    break;
                }
            }
        }
        printed.links = plan->view.links;
        core::gantt::GanttStyle style = app::default_gantt_style();
        style.row_height = job.spec.row_height;
        style.bar_height = std::max(6.0, job.spec.row_height - 4.0);
        style.summary_height = std::max(4.0, job.spec.row_height - 7.0);
        style.milestone_size = std::max(6.0, job.spec.row_height - 4.0);
        state->gantt = core::gantt::build_gantt(
            app::make_gantt_source(printed), style,
            app::timescale_for_zoom(printed, app::gantt_zoom(g_prefs.zoom)));
    } else {
        state->table_width = printable;
    }
    return self;
}

- (void)dealloc {
    delete state;
}

- (void)drawPage:(std::size_t)page_index origin:(NSPoint)origin {
    PrintViewState& s = *state;
    const app::PrintJob& job = s.job;
    const app::PageLayout& layout = job.layout;
    const app::PageSpec& spec = job.spec;
    const app::PrintPage& page = job.pages[page_index];
    const double left = origin.x + spec.margin;
    const double right = origin.x + spec.width - spec.margin;
    const double page_top = origin.y;

    NSFont* title_font = [NSFont boldSystemFontOfSize:11.0];
    NSFont* body_font = [NSFont systemFontOfSize:8.0];
    NSFont* bold_font = [NSFont boldSystemFontOfSize:8.0];
    NSFont* small_font = [NSFont systemFontOfSize:7.0];
    NSColor* ink = [NSColor blackColor];
    NSColor* dim = [NSColor grayColor];
    NSColor* rule = [NSColor lightGrayColor];
    NSColor* band = [NSColor colorWithCalibratedWhite:0.93 alpha:1.0];
    NSColor* weekend = [NSColor colorWithCalibratedWhite:0.94 alpha:1.0];
    NSColor* critical_color = [NSColor redColor];
    NSColor* bar_color =
        [NSColor colorWithCalibratedRed:0.25 green:0.38 blue:0.70 alpha:1.0];
    NSColor* done_color =
        [NSColor colorWithCalibratedRed:0.16 green:0.26 blue:0.52 alpha:1.0];

    draw_label(to_ns(page.header),
               NSMakeRect(left, page_top + spec.margin - 10.0,
                          std::max(40.0, right - left - 100.0), 14.0),
               make_attributes(title_font, ink, NSTextAlignmentLeft));
    draw_label(@"MS Project Shell",
               NSMakeRect(right - 100.0, page_top + spec.margin - 8.0, 100.0, 11.0),
               make_attributes(small_font, dim, NSTextAlignmentRight));
    stroke_line(NSMakePoint(left, page_top + layout.rule_y),
                NSMakePoint(right, page_top + layout.rule_y), rule, 0.6);
    if (!job.subtitle.empty()) {
        draw_label(to_ns(job.subtitle),
                   NSMakeRect(left, page_top + layout.subtitle_baseline - 8.0,
                              std::max(40.0, right - left), 11.0),
                   make_attributes(small_font, dim, NSTextAlignmentLeft));
    }

    stroke_line(NSMakePoint(left, page_top + layout.footer_rule_y),
                NSMakePoint(right, page_top + layout.footer_rule_y), rule, 0.5);
    draw_label(to_ns(page.footer),
               NSMakeRect(left, page_top + layout.footer_top,
                          std::max(40.0, right - left), 11.0),
               make_attributes(small_font, dim, NSTextAlignmentRight));

    const double body_top = page_top + layout.body_top;
    const std::vector<PrintTableColumn> columns =
        print_table_columns(*s.plan, s.table_width);

    fill_rect(NSMakeRect(left, body_top, s.table_width, spec.header_height), band);
    for (const PrintTableColumn& column : columns) {
        const core::sheet::SheetColumn& model = s.plan->sheet->column(column.model_column);
        draw_label(to_ns(s.plan->sheet->column_title(column.model_column)),
                   NSMakeRect(left + column.x + 2.0, body_top + 3.0,
                              std::max(4.0, column.width - 4.0), 11.0),
                   make_attributes(bold_font, ink, alignment_for(model.kind)));
    }
    const double table_bottom =
        body_top + spec.header_height + spec.row_height * static_cast<double>(page.row_count);
    stroke_line(NSMakePoint(left, body_top + spec.header_height),
                NSMakePoint(left + s.table_width, body_top + spec.header_height), rule, 0.6);
    for (const PrintTableColumn& column : columns) {
        stroke_line(NSMakePoint(left + column.x, body_top),
                    NSMakePoint(left + column.x, table_bottom), rule, 0.4);
    }

    const double rows_top = body_top + spec.header_height;
    for (std::size_t ordinal = 0; ordinal < page.row_count; ++ordinal) {
        const std::size_t row = job.rows[page.first_row + ordinal];
        if (row >= s.plan->records.row_count()) {
            continue;
        }
        const double y = rows_top + static_cast<double>(ordinal) * spec.row_height;
        for (const PrintTableColumn& column : columns) {
            const std::string text =
                app::cell_text(*s.plan->sheet, row, column.model_column, g_prefs);
            if (text.empty()) {
                continue;
            }
            const core::sheet::CellStyle cell_style =
                s.plan->sheet->cell_style(row, column.model_column);
            draw_label(to_ns(text),
                       NSMakeRect(left + column.x + 2.0, y + 2.0,
                                  std::max(4.0, column.width - 4.0),
                                  std::max(6.0, spec.row_height - 3.0)),
                       make_attributes(cell_style.bold ? bold_font : body_font,
                                       cell_style.red ? critical_color : ink,
                                       alignment_for(cell_style.align)));
        }
        stroke_line(NSMakePoint(left, y + spec.row_height),
                    NSMakePoint(left + s.table_width, y + spec.row_height), rule, 0.3);
    }

    if (job.kind != app::PrintKind::gantt) {
        return;
    }

    const double chart_x = left + s.table_width + 6.0;
    const double chart_width = std::max(20.0, right - chart_x);
    const double chart_bottom = page_top + layout.body_bottom;
    const double header_bottom = body_top + spec.gantt_header_height;
    const std::vector<core::gantt::HeaderCell> major =
        s.gantt.header(core::gantt::HeaderTier::major, 0.0, chart_width);
    const std::vector<core::gantt::HeaderCell> minor =
        s.gantt.header(core::gantt::HeaderTier::minor, 0.0, chart_width);

    fill_rect(NSMakeRect(chart_x, body_top, chart_width,
                         std::max(2.0, spec.gantt_header_height)),
              band);
    for (const core::gantt::HeaderCell& cell : major) {
        draw_label(to_ns(cell.label),
                   NSMakeRect(chart_x + cell.x + 2.0, body_top + 1.0,
                              std::max(4.0, cell.width - 4.0), 11.0),
                   make_attributes(small_font, dim, NSTextAlignmentCenter));
    }
    for (const core::gantt::HeaderCell& cell : minor) {
        draw_label(to_ns(cell.label),
                   NSMakeRect(chart_x + cell.x, body_top + 12.0,
                              std::max(4.0, cell.width), 11.0),
                   make_attributes(small_font, ink, NSTextAlignmentCenter));
    }
    stroke_line(NSMakePoint(chart_x, header_bottom),
                NSMakePoint(chart_x + chart_width, header_bottom), rule, 0.6);

    const double row_top = static_cast<double>(page.first_row) * spec.row_height;
    const double row_bottom = row_top + static_cast<double>(page.row_count) * spec.row_height;
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:NSMakeRect(chart_x, body_top, chart_width,
                                                 std::max(2.0, chart_bottom - body_top))]
        addClip];
    if (g_prefs.working_day_shading) {
        for (const core::gantt::NonworkingSpan& span :
             s.gantt.nonworking_spans(0.0, chart_width)) {
            if (span.partial) {
                continue;
            }
            fill_rect(NSMakeRect(chart_x + span.x, body_top, span.width,
                                 std::max(2.0, chart_bottom - body_top)),
                      weekend);
        }
    }
    for (const core::gantt::HeaderCell& cell : minor) {
        stroke_line(NSMakePoint(chart_x + cell.x, body_top),
                    NSMakePoint(chart_x + cell.x, chart_bottom), rule, 0.3);
    }
    const double content_top = header_bottom;
    const double offset = content_top - row_top;
    for (const core::gantt::LinkRoute& route : s.gantt.links) {
        if (route.points.size() < 2 || route.bounds.bottom() < row_top ||
            route.bounds.y > row_bottom) {
            continue;
        }
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path moveToPoint:NSMakePoint(chart_x + route.points.front().x,
                                      offset + route.points.front().y)];
        for (std::size_t index = 1; index < route.points.size(); ++index) {
            [path lineToPoint:NSMakePoint(chart_x + route.points[index].x,
                                          offset + route.points[index].y)];
        }
        [path setLineWidth:0.7];
        [(route.critical ? critical_color : dim) setStroke];
        [path stroke];
    }
    for (const core::gantt::Bar& bar : s.gantt.bars) {
        if (bar.row < static_cast<int>(page.first_row) ||
            bar.row >= static_cast<int>(page.first_row + page.row_count)) {
            continue;
        }
        const double x = chart_x + bar.rect.x;
        const double y = offset + bar.rect.y;
        const double width = std::max(1.0, bar.rect.w);
        const double height = std::max(1.0, bar.rect.h);
        if (bar.kind == core::gantt::BarKind::milestone) {
            NSBezierPath* diamond = [NSBezierPath bezierPath];
            [diamond moveToPoint:NSMakePoint(x + width / 2.0, y)];
            [diamond lineToPoint:NSMakePoint(x + width, y + height / 2.0)];
            [diamond lineToPoint:NSMakePoint(x + width / 2.0, y + height)];
            [diamond lineToPoint:NSMakePoint(x, y + height / 2.0)];
            [diamond closePath];
            [(bar.critical ? critical_color : ink) setFill];
            [diamond fill];
        } else if (bar.kind == core::gantt::BarKind::summary) {
            fill_rect(NSMakeRect(x, y, width, height), ink);
            fill_rect(NSMakeRect(x, y + height, 2.0, 4.0), ink);
            fill_rect(NSMakeRect(x + width - 2.0, y + height, 2.0, 4.0), ink);
        } else {
            fill_rect(NSMakeRect(x, y, width, height),
                      bar.critical ? critical_color : bar_color);
            if (bar.fill.w > 0.0) {
                fill_rect(NSMakeRect(x, y, bar.fill.w, height),
                          bar.critical ? bar_color : done_color);
            }
            stroke_rect(NSMakeRect(x, y, width, height),
                        bar.critical ? critical_color : bar_color, 0.8);
        }
    }
    [NSGraphicsContext restoreGraphicsState];
}

- (BOOL)knowsPageRange:(NSRangePointer)range {
    range->location = 1;
    range->length = state->job.pages.size();
    return YES;
}

- (NSRect)rectForPage:(NSInteger)page {
    const double page_height = state->job.spec.height;
    return NSMakeRect(0.0, static_cast<double>(page - 1) * page_height,
                      state->job.spec.width, page_height);
}

- (void)drawRect:(NSRect)dirtyRect {
    if (state == nullptr || state->job.pages.empty() || state->job.spec.height <= 0.0) {
        return;
    }
    const double page_height = state->job.spec.height;
    const double first_y = std::max(0.0, dirtyRect.origin.y);
    const double last_y = std::max(0.0, dirtyRect.origin.y + dirtyRect.size.height - 0.5);
    const std::size_t first =
        static_cast<std::size_t>(std::floor(first_y / page_height));
    const std::size_t last = static_cast<std::size_t>(std::floor(last_y / page_height));
    for (std::size_t page = first; page <= last && page < state->job.pages.size(); ++page) {
        [self drawPage:page
                origin:NSMakePoint(0.0, static_cast<double>(page) * page_height)];
    }
}

@end

// Both print routes land here; the only difference is the route tag on the job
// and whether NSPrintOperation shows the panel or saves to `save_url`.
BOOL run_print_view(PlanState& plan, const app::PrintJob& job, NSURL* save_url) {
    if (job.pages.empty()) {
        return NO;
    }
    PrintView* view = [[PrintView alloc] initWithPlan:&plan job:job];
    NSPrintInfo* info = [[NSPrintInfo sharedPrintInfo] copy];
    [info setPaperSize:NSMakeSize(job.spec.width, job.spec.height)];
    [info setTopMargin:0.0];
    [info setBottomMargin:0.0];
    [info setLeftMargin:0.0];
    [info setRightMargin:0.0];
    [info setHorizontallyCentered:NO];
    [info setVerticallyCentered:NO];
    // NSPrintingPaginationModeClip, not the NSClipPagination constant: the old
    // spelling is deprecated since macOS 11 and this target builds -Werror.
    [info setHorizontalPagination:NSPrintingPaginationModeClip];
    [info setVerticalPagination:NSPrintingPaginationModeClip];
    if (save_url != nil) {
        [info setJobDisposition:NSPrintSaveJob];
        [[info dictionary] setObject:save_url forKey:NSPrintJobSavingURL];
    }
    NSPrintOperation* operation = [NSPrintOperation printOperationWithView:view
                                                                 printInfo:info];
    [operation setShowsPrintPanel:(save_url == nil)];
    [operation setShowsProgressPanel:NO];
    return [operation runOperation] ? YES : NO;
}

// ---------------------------------------------------------------------------
// Application
// ---------------------------------------------------------------------------

@interface AppDelegate
    : NSObject <NSApplicationDelegate, NSWindowDelegate, NSMenuDelegate,
              NSMenuItemValidation>
- (void)installMenu;
- (void)refreshMenus;
- (void)loadPath:(const std::string&)path;
- (void)updateWindowTitle:(const std::string&)path title:(const std::string&)title;
- (void)openDocument:(id)sender;
- (BOOL)saveDocumentWithPanel:(BOOL)forcePanel;
- (BOOL)saveDocument:(id)sender;
- (void)saveDocumentAs:(id)sender;
- (void)saveAs:(id)sender;
- (void)undo:(id)sender;
- (void)redo:(id)sender;
- (void)copy:(id)sender;
- (void)paste:(id)sender;
- (void)selectAll:(id)sender;
- (void)performMenuAction:(id)sender;
- (void)insertTask:(id)sender;
- (void)deleteTask:(id)sender;
- (void)taskFields:(id)sender;
- (void)editPredecessors:(id)sender;
- (void)tableDateFormat:(id)sender;
- (void)customFieldFormula:(id)sender;
- (void)rebuildColumnsMenu;
- (BOOL)saveStructuralEdits:(const std::vector<app::StartEdit>&)edits
                     target:(const std::string&)target;
- (void)viewModeChanged:(id)sender;
- (void)usagePeriodChanged:(id)sender;
- (void)syncViewControls;
- (void)toggleColumn:(id)sender;
- (void)showPreferences:(id)sender;
- (void)preferencesChanged:(id)sender;
- (void)showFilterDialog:(id)sender;
- (void)clearFilterAction:(id)sender;
- (void)exportSheetReport:(id)sender;
- (void)exportGanttReport:(id)sender;
- (void)printDocument:(id)sender;
- (void)saveAsPDF:(id)sender;
// Wave 15: the `--smoke` sequence; returns the process exit code (0 = pass).
- (int)runSmoke;
- (void)cut:(id)sender;
- (BOOL)confirmClose;
@end

static AppDelegate* g_delegate = nil;
static PlanView* g_view = nil;

// W26-6.1: one recursive item builder.  menu_specs() can now carry a drop-down
// (View > Task Views / Resource Views), and a child must go through exactly the
// same tag/validation path as a top-level command.  A container item has no
// action; its children are NSMenuItems targeting performMenuAction:.
static void append_menu_items(NSMenu* menu, const std::vector<app::MenuItemSpec>& items,
                              id target) {
    for (const app::MenuItemSpec& item_spec : items) {
        if (item_spec.separator_before) {
            [menu addItem:[NSMenuItem separatorItem]];
        }
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:to_ns(item_spec.title)
                                                     action:nil
                                              keyEquivalent:@""];
        [item setTarget:target];
        [item setTag:static_cast<NSInteger>(item_spec.action)];
        if (item_spec.is_submenu()) {
            NSMenu* child_menu = [[NSMenu alloc] initWithTitle:to_ns(item_spec.title)];
            [child_menu setAutoenablesItems:NO];
            [child_menu setDelegate:(id<NSMenuDelegate>)target];
            append_menu_items(child_menu, item_spec.children, target);
            [item setSubmenu:child_menu];
            [menu addItem:item];
            continue;
        }
        [item setAction:@selector(performMenuAction:)];
        [item setKeyEquivalent:to_ns(item_spec.key)];
        NSEventModifierFlags mask = 0;
        if (item_spec.command_key) {
            mask |= NSEventModifierFlagCommand;
        }
        if (item_spec.shift) {
            mask |= NSEventModifierFlagShift;
        }
        if (item_spec.option) {
            mask |= NSEventModifierFlagOption;
        }
        [item setKeyEquivalentModifierMask:mask];
        if (!app::menu_item_supported(item_spec.action)) {
            [item setToolTip:to_ns(app::menu_disabled_reason(item_spec.action))];
        }
        [menu addItem:item];
    }
}

// W26-6.1: the enablement/check walk must descend into the View drop-downs;
// otherwise their items would keep the NSMenu default (enabled) state.
static void refresh_menu_items(NSMenu* menu, const app::MenuState& state) {
    for (NSMenuItem* item in [menu itemArray]) {
        NSMenu* child = [item submenu];
        if (child != nil) {
            refresh_menu_items(child, state);
            continue;
        }
        if ([item action] != @selector(performMenuAction:)) {
            continue;
        }
        const app::MenuAction action = static_cast<app::MenuAction>([item tag]);
        [item setEnabled:app::menu_item_enabled(action, state)];
        // Check marks are view state: the four views, the usage period and
        // the two display toggles (shading, Gantt zoom).  if/else rather than
        // a switch so a new MenuAction cannot silently get a stale mark.
        BOOL checked = NO;
        if (g_plan != nullptr) {
            if (action == app::MenuAction::view_task_sheet) {
                checked = g_plan->view_mode == app::ViewMode::task_sheet;
            } else if (action == app::MenuAction::view_resource_sheet) {
                checked = g_plan->view_mode == app::ViewMode::resource_sheet;
            } else if (action == app::MenuAction::view_task_usage) {
                checked = g_plan->view_mode == app::ViewMode::task_usage;
            } else if (action == app::MenuAction::view_resource_usage) {
                checked = g_plan->view_mode == app::ViewMode::resource_usage;
            } else if (action == app::MenuAction::usage_day) {
                checked = g_plan->usage_period == core::usage::Period::day;
            } else if (action == app::MenuAction::usage_week) {
                checked = g_plan->usage_period == core::usage::Period::week;
            } else if (action == app::MenuAction::usage_month) {
                checked = g_plan->usage_period == core::usage::Period::month;
            }
        }
        if (action == app::MenuAction::toggle_shading) {
            checked = g_prefs.working_day_shading;
        } else if (action == app::MenuAction::zoom_day) {
            checked = g_prefs.zoom == app::ZoomLevel::day;
        } else if (action == app::MenuAction::zoom_week) {
            checked = g_prefs.zoom == app::ZoomLevel::week;
        } else if (action == app::MenuAction::zoom_month) {
            checked = g_prefs.zoom == app::ZoomLevel::month;
        }
        [item setState:checked ? NSControlStateValueOn : NSControlStateValueOff];
    }
}

@implementation AppDelegate

- (void)installMenu {
    NSMenu* mainMenu = [[NSMenu alloc] initWithTitle:@"MainMenu"];

    // The whole menu bar is DATA (app/menu_model.h): this loop is the only place
    // that creates items, so the headless menu test covers every menu, title, key
    // equivalent and enablement rule.  Each item's tag is its MenuAction.
    for (const app::MenuSpec& spec : app::menu_specs()) {
        NSMenuItem* top = [[NSMenuItem alloc] initWithTitle:to_ns(spec.title)
                                                     action:nil
                                              keyEquivalent:@""];
        NSMenu* submenu = [[NSMenu alloc] initWithTitle:to_ns(spec.title)];
        [submenu setAutoenablesItems:NO];
        [submenu setDelegate:self];
        // W26-6.1: the helper also creates the View > Task Views / Resource
        // Views drop-downs, so the AppKit menu is the same data as the Qt one.
        append_menu_items(submenu, spec.items, self);
        if (std::string(spec.title) == "View") {
            // The Columns submenu is document-shaped: wave 12's column set depends
            // on the file's own formulas, so it is rebuilt per load by
            // rebuildColumnsMenu() rather than filled from a static list here.
            [submenu addItem:[NSMenuItem separatorItem]];
            NSMenuItem* columns_item = [[NSMenuItem alloc] initWithTitle:@"Columns"
                                                                  action:nil
                                                           keyEquivalent:@""];
            g_columns_menu = [[NSMenu alloc] initWithTitle:@"Columns"];
            [g_columns_menu setAutoenablesItems:NO];
            [columns_item setSubmenu:g_columns_menu];
            [submenu addItem:columns_item];
        }
        [top setSubmenu:submenu];
        [mainMenu addItem:top];
    }

    [NSApp setMainMenu:mainMenu];
    [self rebuildColumnsMenu];
    refresh_menu_bar();
}

// Rebuilds the View > Columns submenu from the sheet's CURRENT column set.  Each
// item's tag is the model column index, so a load whose formula set adds or
// removes columns must rebuild rather than reuse the previous tags.
- (void)rebuildColumnsMenu {
    if (g_columns_menu == nil) {
        return;
    }
    [g_columns_menu removeAllItems];
    std::vector<core::sheet::SheetColumn> columns;
    if (g_plan != nullptr && g_plan->sheet != nullptr) {
        columns.reserve(g_plan->sheet->column_count());
        for (std::size_t index = 0; index < g_plan->sheet->column_count(); ++index) {
            core::sheet::SheetColumn column;
            column.field = g_plan->sheet->column(index).field;
            column.title = g_plan->sheet->column_title(index);
            columns.push_back(std::move(column));
        }
    } else {
        columns = app::task_editor_columns(app::FormulaColumnSet{});
    }
    for (std::size_t index = 0; index < columns.size(); ++index) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:to_ns(columns[index].title)
                                                      action:@selector(toggleColumn:)
                                               keyEquivalent:@""];
        [item setTarget:self];
        [item setTag:static_cast<NSInteger>(index)];
        [g_columns_menu addItem:item];
    }
}

- (void)refreshMenus {
    refresh_menu_bar();
}

// NSMenuDelegate: the state can change without a menu being opened (a keyboard
// edit, a save), so re-validate when a menu is about to be displayed too.
- (void)menuNeedsUpdate:(NSMenu*)menu {
    (void)menu;
    refresh_menu_bar();
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
    if ([item action] == @selector(performMenuAction:)) {
        return app::menu_item_enabled(static_cast<app::MenuAction>([item tag]),
                                      current_menu_state())
                   ? YES
                   : NO;
    }
    return YES;
}

// One dispatch point for every menu command.  The switch is exhaustive over
// app::MenuAction so a new action cannot be added without a handler.
- (void)performMenuAction:(id)sender {
    const app::MenuAction action = static_cast<app::MenuAction>([sender tag]);
    if (g_plan == nullptr) {
        return;
    }
    switch (action) {
        case app::MenuAction::none:
            return;
        case app::MenuAction::about:
            show_about();
            return;
        case app::MenuAction::preferences:
            [self showPreferences:nil];
            return;
        case app::MenuAction::hide_app:
            [NSApp hide:nil];
            return;
        case app::MenuAction::quit_app:
            [NSApp terminate:nil];
            return;
        case app::MenuAction::keyboard_shortcuts:
            show_shortcuts();
            return;
        case app::MenuAction::open:
            [self openDocument:nil];
            return;
        case app::MenuAction::save:
            [self saveDocument:nil];
            return;
        case app::MenuAction::save_as:
            [self saveDocumentAs:nil];
            return;
        case app::MenuAction::export_pdf_sheet:
            [self exportSheetReport:nil];
            return;
        case app::MenuAction::export_pdf_gantt:
            [self exportGanttReport:nil];
            return;
        case app::MenuAction::print:
            [self printDocument:nil];
            return;
        case app::MenuAction::save_as_pdf:
            [self saveAsPDF:nil];
            return;
        case app::MenuAction::close_window:
            [g_window performClose:nil];
            return;
        case app::MenuAction::undo:
            [self undo:nil];
            return;
        case app::MenuAction::redo:
            [self redo:nil];
            return;
        case app::MenuAction::cut:
            [self cut:nil];
            return;
        case app::MenuAction::copy:
            [self copy:nil];
            return;
        case app::MenuAction::paste:
            [self paste:nil];
            return;
        case app::MenuAction::select_all:
            [self selectAll:nil];
            return;
        case app::MenuAction::zoom_in:
            if (g_plan->sheet != nullptr) {
                step_zoom(*g_plan, -1);
                [g_view refreshAfterChange];
            }
            return;
        case app::MenuAction::zoom_out:
            if (g_plan->sheet != nullptr) {
                step_zoom(*g_plan, 1);
                [g_view refreshAfterChange];
            }
            return;
        case app::MenuAction::zoom_day:
            if (g_plan->sheet != nullptr) {
                set_zoom_level(*g_plan, app::ZoomLevel::day);
                [g_view refreshAfterChange];
            }
            return;
        case app::MenuAction::zoom_week:
            if (g_plan->sheet != nullptr) {
                set_zoom_level(*g_plan, app::ZoomLevel::week);
                [g_view refreshAfterChange];
            }
            return;
        case app::MenuAction::zoom_month:
            if (g_plan->sheet != nullptr) {
                set_zoom_level(*g_plan, app::ZoomLevel::month);
                [g_view refreshAfterChange];
            }
            return;
        case app::MenuAction::toggle_shading:
            g_prefs.working_day_shading = !g_prefs.working_day_shading;
            save_preferences_to_defaults();
            note(*g_plan, std::string("working-day shading ") +
                              (g_prefs.working_day_shading ? "on" : "off"),
                 false);
            [g_view setNeedsDisplay:YES];
            refresh_menu_bar();
            return;
        case app::MenuAction::show_filter:
            [self showFilterDialog:nil];
            return;
        case app::MenuAction::clear_filter:
            [self clearFilterAction:nil];
            return;
        case app::MenuAction::collapse_all:
            if (g_plan->sheet != nullptr) {
                g_plan->sheet->collapse_all();
                note(*g_plan, "collapsed every summary", false);
                [g_view refreshAfterChange];
            }
            return;
        case app::MenuAction::expand_all:
            if (g_plan->sheet != nullptr) {
                g_plan->sheet->expand_all();
                note(*g_plan, "expanded every summary", false);
                [g_view refreshAfterChange];
            }
            return;
        case app::MenuAction::view_task_sheet:
            set_view_mode(*g_plan, app::ViewMode::task_sheet);
            [self syncViewControls];
            [g_view refreshAfterChange];
            return;
        case app::MenuAction::view_resource_sheet:
            set_view_mode(*g_plan, app::ViewMode::resource_sheet);
            [self syncViewControls];
            [g_view refreshAfterChange];
            return;
        case app::MenuAction::view_task_usage:
            set_view_mode(*g_plan, app::ViewMode::task_usage);
            [self syncViewControls];
            [g_view refreshAfterChange];
            return;
        case app::MenuAction::view_resource_usage:
            set_view_mode(*g_plan, app::ViewMode::resource_usage);
            [self syncViewControls];
            [g_view refreshAfterChange];
            return;
        case app::MenuAction::usage_day:
            set_usage_period(*g_plan, core::usage::Period::day);
            [self syncViewControls];
            [g_view refreshAfterChange];
            return;
        case app::MenuAction::usage_week:
            set_usage_period(*g_plan, core::usage::Period::week);
            [self syncViewControls];
            [g_view refreshAfterChange];
            return;
        case app::MenuAction::usage_month:
            set_usage_period(*g_plan, core::usage::Period::month);
            [self syncViewControls];
            [g_view refreshAfterChange];
            return;
        case app::MenuAction::insert_task:
            [self insertTask:nil];
            return;
        case app::MenuAction::delete_task:
            [self deleteTask:nil];
            return;
        case app::MenuAction::task_fields:
            [self taskFields:nil];
            return;
        case app::MenuAction::custom_fields:
            // Wave 23 (B-13): the Task Fields dialog moved to the Custom Fields
            // command.  The macOS shell keeps its own Task Information wiring;
            // this case restores the existing dialog under its Project home.
            [self taskFields:nil];
            return;
        case app::MenuAction::predecessors:
            [self editPredecessors:nil];
            return;
        case app::MenuAction::table_date_format:
            [self tableDateFormat:nil];
            return;
        case app::MenuAction::custom_field_formula:
            [self customFieldFormula:nil];
            return;
        case app::MenuAction::insert_resource:
            show_alert(@"Insert Resource",
                       to_ns(std::string(
                           app::menu_disabled_reason(app::MenuAction::insert_resource))));
            return;
    }
}

// Insert Task (wave 12): ask for a name, record a pending insert anchored on the
// selected row, and rebuild the sheet/Gantt from the display plan.  Nothing is
// written until Save; the writer appends the row at the end of the Task table, so
// a reopened file lists it last (the save message says so).
- (void)insertTask:(id)sender {
    (void)sender;
    if (g_plan == nullptr || g_plan->sheet == nullptr ||
        !app::view_mode_is_task(g_plan->view_mode)) {
        return;
    }
    PlanState& plan = *g_plan;
    if (plan.sheet->editing()) {
        plan.sheet->cancel_edit();
    }

    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Insert Task"];
    [alert setInformativeText:@"The task is shown in the sheet now and written to the .mpp "
                              @"when you save."];
    [alert addButtonWithTitle:@"Insert"];
    [alert addButtonWithTitle:@"Cancel"];
    NSTextField* name_field =
        [[NSTextField alloc] initWithFrame:NSMakeRect(0.0, 0.0, 320.0, 24.0)];
    [name_field setStringValue:@"New Task"];
    [alert setAccessoryView:name_field];
    if ([alert runModal] != NSAlertFirstButtonReturn) {
        note(plan, "insert cancelled", false);
        [g_view setNeedsDisplay:YES];
        return;
    }
    std::string name = to_std([name_field stringValue]);
    std::size_t begin = 0;
    while (begin < name.size() && (name[begin] == ' ' || name[begin] == '\t')) {
        ++begin;
    }
    std::size_t end = name.size();
    while (end > begin && (name[end - 1] == ' ' || name[end - 1] == '\t')) {
        --end;
    }
    name = name.substr(begin, end - begin);
    if (name.empty()) {
        show_alert(@"Insert Task", @"A task needs a name.");
        return;
    }

    const app::SelectedTask selected = app::selected_task(*plan.sheet, plan.row_map);
    app::StructuralEdit edit;
    edit.kind = app::StructuralKind::insert_task;
    edit.name = name;
    edit.uid = plan.edits.next_placeholder_uid(plan.base);
    edit.anchor_uid = selected.has_selection ? selected.uid : 0;
    if (edit.uid == 0 || !plan.edits.push(edit)) {
        show_alert(@"Insert Task", @"The task could not be added.");
        return;
    }
    apply_structural_change(plan);
    // Select the new row so the user can type a Start at once.
    const int row = plan.records.row_of_uid(edit.uid);
    if (row >= 0) {
        plan.sheet->select_cell(static_cast<std::size_t>(row), name_column_of(plan));
    }
    note(plan, "inserted \"" + name + "\" (pending; Save writes it)", false);
    [g_view refreshAfterChange];
    refresh_menu_bar();
}

// Delete Task (wave 12): confirm first, naming how many dependent rows the
// writer's cascade will tombstone.  A pending insert is simply dropped.
- (void)deleteTask:(id)sender {
    (void)sender;
    if (g_plan == nullptr || g_plan->sheet == nullptr ||
        !app::view_mode_is_task(g_plan->view_mode)) {
        return;
    }
    PlanState& plan = *g_plan;
    if (plan.sheet->editing()) {
        plan.sheet->cancel_edit();
    }
    const app::SelectedTask selected = app::selected_task(*plan.sheet, plan.row_map);
    if (!selected.has_selection) {
        note(plan, "select a task to delete", true);
        [g_view setNeedsDisplay:YES];
        return;
    }
    if (plan.edits.is_pending_insert(selected.uid)) {
        if (plan.edits.drop_insert(selected.uid)) {
            apply_structural_change(plan);
            note(plan, "removed the pending inserted task", false);
            [g_view refreshAfterChange];
        }
        return;
    }
    const std::size_t dependants = app::count_delete_dependants(plan.base, selected.uid);
    const std::string name = plan.sheet->cell_text(selected.row, name_column_of(plan));
    std::string details = "Delete \"" +
                          (name.empty() ? "task uid " + std::to_string(selected.uid) : name) +
                          "\"?";
    if (dependants != 0) {
        details += "\n\nThe delete cascades: " + std::to_string(dependants) +
                   " dependent link/assignment row(s) will be tombstoned in the saved file.";
    }
    details += "\n\nThe change is pending until you save; Cmd-Z undoes it before then.";
    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Delete Task"];
    [alert setInformativeText:to_ns(details)];
    [alert setAlertStyle:NSAlertStyleWarning];
    [alert addButtonWithTitle:@"Delete"];
    [alert addButtonWithTitle:@"Cancel"];
    if ([alert runModal] != NSAlertFirstButtonReturn) {
        note(plan, "delete cancelled", false);
        [g_view setNeedsDisplay:YES];
        return;
    }
    app::StructuralEdit edit;
    edit.kind = app::StructuralKind::delete_task;
    edit.uid = selected.uid;
    edit.dependants = dependants;
    if (!plan.edits.push(edit)) {
        note(plan, "that task already has a pending delete", true);
        return;
    }
    apply_structural_change(plan);
    note(plan, "deleted task uid " + std::to_string(selected.uid) + " (pending; Save writes it)",
         false);
    [g_view refreshAfterChange];
}

// Task Fields... (wave 13): one modal form for the six measured custom fields
// (Text1, Number1, Number2, Date1, Duration1, Flag1).  The values are prefilled
// from the plan's stored custom fields, parsed by app::make_task_field_edits
// through the sheet's own parser (the writer's types are checked there) and
// queued as pending edits; nothing is written until Save.  The grid columns stay
// read-only, so this dialog is the one place a stored value is edited.
- (void)taskFields:(id)sender {
    (void)sender;
    if (g_plan == nullptr || g_plan->sheet == nullptr ||
        !app::view_mode_is_task(g_plan->view_mode)) {
        return;
    }
    PlanState& plan = *g_plan;
    if (plan.sheet->editing()) {
        plan.sheet->cancel_edit();
    }
    const app::SelectedTask selected = app::selected_task(*plan.sheet, plan.row_map);
    if (!selected.has_selection) {
        note(plan, "select a task to edit its custom fields", true);
        [g_view setNeedsDisplay:YES];
        return;
    }
    if (plan.edits.is_pending_insert(selected.uid)) {
        show_alert(@"Task Fields",
                   @"That task is a pending insert: save it before editing its fields.");
        return;
    }
    const app::TaskRow* row = plan_task_row(plan.base, selected.uid);
    if (row == nullptr) {
        note(plan, "the selected task is not in the plan", true);
        [g_view setNeedsDisplay:YES];
        return;
    }
    const std::vector<app::TaskFieldSpec>& specs = app::task_field_specs();
    for (;;) {
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Task Fields"];
        [alert setInformativeText:
                   @"Values are queued and written to the .mpp when you save. "
                   @"A blank field clears the stored value."];
        [alert addButtonWithTitle:@"OK"];
        [alert addButtonWithTitle:@"Cancel"];
        const CGFloat row_height = 26.0;
        const CGFloat label_width = 100.0;
        const CGFloat field_width = 300.0;
        NSView* accessory = [[NSView alloc]
            initWithFrame:NSMakeRect(0.0, 0.0, label_width + field_width + 12.0,
                                     row_height * static_cast<CGFloat>(specs.size()) +
                                         8.0)];
        std::vector<NSTextField*> fields;
        fields.reserve(specs.size());
        for (std::size_t index = 0; index < specs.size(); ++index) {
            const CGFloat y = row_height * static_cast<CGFloat>(specs.size() - 1 - index) + 4.0;
            NSTextField* label = [NSTextField labelWithString:to_ns(specs[index].title)];
            [label setFrame:NSMakeRect(0.0, y, label_width, 20.0)];
            [accessory addSubview:label];
            NSTextField* field =
                [[NSTextField alloc] initWithFrame:NSMakeRect(label_width + 6.0, y,
                                                              field_width, 24.0)];
            [field setStringValue:to_ns(
                                      app::field_value_text(*row, specs[index].field_id,
                                                            plan.format))];
            [accessory addSubview:field];
            fields.push_back(field);
        }
        [alert setAccessoryView:accessory];
        if ([alert runModal] != NSAlertFirstButtonReturn) {
            note(plan, "task fields cancelled", false);
            [g_view setNeedsDisplay:YES];
            return;
        }
        std::vector<app::TaskFieldInput> inputs;
        inputs.reserve(specs.size());
        for (std::size_t index = 0; index < specs.size(); ++index) {
            app::TaskFieldInput input;
            input.field_id = specs[index].field_id;
            input.text = to_std([fields[index] stringValue]);
            inputs.push_back(std::move(input));
        }
        std::vector<app::StructuralEdit> form;
        std::string error;
        if (!app::make_task_field_edits(*row, inputs, plan.format, form, error)) {
            show_alert(@"Task Fields", to_ns(error));
            continue;  // the form stays open so the value can be corrected
        }
        if (form.empty()) {
            note(plan, "no custom-field value changed", false);
            [g_view refreshAfterChange];
            return;
        }
        std::size_t pushed = 0;
        for (const app::StructuralEdit& edit : form) {
            if (plan.edits.push(edit)) {
                ++pushed;
            }
        }
        if (pushed != form.size()) {
            show_alert(@"Task Fields", @"The field edits could not all be queued.");
            return;
        }
        apply_structural_change(plan);
        std::string fields_named;
        for (const app::StructuralEdit& edit : form) {
            const app::TaskFieldSpec* spec = app::task_field_spec(edit.field_id);
            if (!fields_named.empty()) {
                fields_named += ", ";
            }
            fields_named += spec != nullptr ? spec->title
                                            : std::to_string(edit.field_id);
            if (edit.kind == app::StructuralKind::clear_field) {
                fields_named += " (clear)";
            }
        }
        note(plan, "queued " + std::to_string(pushed) + " custom-field edit(s) for task uid " +
                       std::to_string(selected.uid) + ": " + fields_named +
                       " (pending; Save writes them)",
             false);
        [g_view refreshAfterChange];
        refresh_menu_bar();
        return;
    }
}

// Predecessors... (wave 13): list the selected task's incoming links and let the
// user add one (predecessor ID, type, lag) and/or delete one from the file.  Add
// and Delete in the same OK both queue; the save runs the deletes first, so
// replacing a relation works.  All validation is app::validate_link_add /
// app::make_link_add, so a bad entry keeps the form open with the reason.
- (void)editPredecessors:(id)sender {
    (void)sender;
    if (g_plan == nullptr || g_plan->sheet == nullptr ||
        !app::view_mode_is_task(g_plan->view_mode)) {
        return;
    }
    PlanState& plan = *g_plan;
    if (plan.sheet->editing()) {
        plan.sheet->cancel_edit();
    }
    const app::SelectedTask selected = app::selected_task(*plan.sheet, plan.row_map);
    if (!selected.has_selection) {
        note(plan, "select a task to edit its predecessors", true);
        [g_view setNeedsDisplay:YES];
        return;
    }
    if (plan.edits.is_pending_insert(selected.uid)) {
        show_alert(@"Predecessors",
                   @"That task is a pending insert: save it before adding links.");
        return;
    }
    const std::uint32_t successor = selected.uid;
    for (;;) {
        const std::vector<app::PredecessorListItem> items =
            app::predecessors_of(plan.base, plan.edits, successor);
        std::string listing;
        for (const app::PredecessorListItem& item : items) {
            listing += item.predecessor_id + "  " + item.predecessor_name + "  " +
                       item.type_name + "  lag " + item.lag_text;
            if (item.pending) {
                listing += "  (pending)";
            }
            listing += "\n";
        }
        if (listing.empty()) {
            listing = "No predecessor links.\n";
        }

        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Predecessors"];
        [alert setInformativeText:
                   @"Links are queued and written to the .mpp when you save. "
                   @"Lag accepts 0, 2d, -1d, 4h."];
        [alert addButtonWithTitle:@"OK"];
        [alert addButtonWithTitle:@"Cancel"];

        const CGFloat width = 520.0;
        NSView* accessory = [[NSView alloc] initWithFrame:NSMakeRect(0.0, 0.0, width, 200.0)];
        NSTextField* list_label =
            [[NSTextField alloc] initWithFrame:NSMakeRect(0.0, 128.0, width, 66.0)];
        [list_label setEditable:NO];
        [list_label setBezeled:NO];
        [list_label setDrawsBackground:NO];
        [list_label setSelectable:NO];
        [(NSTextFieldCell*)[list_label cell] setWraps:YES];
        [list_label setStringValue:to_ns(listing)];
        [accessory addSubview:list_label];

        NSTextField* predecessor_label = [NSTextField labelWithString:@"Predecessor ID:"];
        [predecessor_label setFrame:NSMakeRect(0.0, 98.0, 110.0, 20.0)];
        [accessory addSubview:predecessor_label];
        NSTextField* predecessor_field =
            [[NSTextField alloc] initWithFrame:NSMakeRect(112.0, 96.0, 70.0, 24.0)];
        [predecessor_field setStringValue:@""];
        [accessory addSubview:predecessor_field];

        NSTextField* type_label = [NSTextField labelWithString:@"Type:"];
        [type_label setFrame:NSMakeRect(200.0, 98.0, 44.0, 20.0)];
        [accessory addSubview:type_label];
        NSPopUpButton* type_popup =
            [[NSPopUpButton alloc] initWithFrame:NSMakeRect(246.0, 94.0, 90.0, 26.0)
                                       pullsDown:NO];
        [type_popup addItemsWithTitles:@[ @"FS", @"SS", @"FF", @"SF" ]];
        [type_popup selectItemAtIndex:0];
        [accessory addSubview:type_popup];

        NSTextField* lag_label = [NSTextField labelWithString:@"Lag:"];
        [lag_label setFrame:NSMakeRect(354.0, 98.0, 36.0, 20.0)];
        [accessory addSubview:lag_label];
        NSTextField* lag_field =
            [[NSTextField alloc] initWithFrame:NSMakeRect(392.0, 96.0, 110.0, 24.0)];
        [lag_field setStringValue:@"0"];
        [accessory addSubview:lag_field];

        NSTextField* delete_label = [NSTextField labelWithString:@"Delete link:"];
        [delete_label setFrame:NSMakeRect(0.0, 56.0, 90.0, 20.0)];
        [accessory addSubview:delete_label];
        NSPopUpButton* delete_popup =
            [[NSPopUpButton alloc] initWithFrame:NSMakeRect(92.0, 52.0, 410.0, 26.0)
                                       pullsDown:NO];
        [delete_popup addItemWithTitle:@"Do not delete a link"];
        std::vector<std::size_t> deletable;
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (items[index].pending) {
                continue;  // a pending add is removed by undo, not by the writer
            }
            deletable.push_back(index);
            const std::string title = items[index].predecessor_id + " -> " +
                                      items[index].type_name + " (lag " +
                                      items[index].lag_text + ")";
            [delete_popup addItemWithTitle:to_ns(title)];
        }
        [delete_popup selectItemAtIndex:0];
        [accessory addSubview:delete_popup];

        NSTextField* hint_label = [NSTextField
            labelWithString:@"Blank predecessor ID adds nothing; Delete and Add may be set "
                            @"together."];
        [hint_label setFrame:NSMakeRect(0.0, 24.0, width, 20.0)];
        [accessory addSubview:hint_label];
        [alert setAccessoryView:accessory];

        if ([alert runModal] != NSAlertFirstButtonReturn) {
            note(plan, "predecessors cancelled", false);
            [g_view setNeedsDisplay:YES];
            return;
        }

        std::vector<app::StructuralEdit> queued;
        std::string error;
        app::StructuralEdits trial = plan.edits;
        const NSInteger delete_index = [delete_popup indexOfSelectedItem];
        if (delete_index > 0 &&
            static_cast<std::size_t>(delete_index - 1) < deletable.size()) {
            app::StructuralEdit edit;
            if (!app::make_link_delete(items[deletable[static_cast<std::size_t>(
                                              delete_index - 1)]],
                                       successor, edit, error) ||
                !trial.push(edit)) {
                if (error.empty()) {
                    error = "the link delete could not be queued";
                }
                show_alert(@"Predecessors", to_ns(error));
                continue;
            }
            queued.push_back(edit);
        }

        std::string predecessor_text = to_std([predecessor_field stringValue]);
        while (!predecessor_text.empty() &&
               (predecessor_text.front() == ' ' || predecessor_text.front() == '\t')) {
            predecessor_text.erase(predecessor_text.begin());
        }
        while (!predecessor_text.empty() &&
               (predecessor_text.back() == ' ' || predecessor_text.back() == '\t')) {
            predecessor_text.pop_back();
        }
        if (!predecessor_text.empty()) {
            std::size_t consumed = 0;
            unsigned long predecessor = 0;
            bool parsed = true;
            try {
                predecessor = std::stoul(predecessor_text, &consumed, 10);
            } catch (const std::exception&) {
                parsed = false;
            }
            if (!parsed || consumed != predecessor_text.size() || predecessor == 0 ||
                predecessor > 0xFFFFFFFFul) {
                show_alert(@"Predecessors",
                           @"The predecessor ID must be a positive whole number.");
                continue;
            }
            const core::gantt::LinkType types[] = {
                core::gantt::LinkType::finish_to_start,
                core::gantt::LinkType::start_to_start,
                core::gantt::LinkType::finish_to_finish,
                core::gantt::LinkType::start_to_finish};
            const NSInteger type_index = [type_popup indexOfSelectedItem];
            const core::gantt::LinkType type =
                (type_index >= 0 && type_index < 4) ? types[type_index]
                                                    : types[0];
            // Validate against the display plan with the pending delete already
            // applied, so "delete this relation, then add it back" is accepted.
            const app::PlanInput trial_plan = trial.apply(plan.loaded);
            app::StructuralEdit edit;
            if (!app::make_link_add(trial_plan, trial,
                                    static_cast<std::uint32_t>(predecessor), successor, type,
                                    to_std([lag_field stringValue]), edit, error) ||
                !trial.push(edit)) {
                if (error.empty()) {
                    error = "the link add could not be queued";
                }
                show_alert(@"Predecessors", to_ns(error));
                continue;
            }
            queued.push_back(edit);
        }

        if (queued.empty()) {
            note(plan, "no predecessor change", false);
            [g_view refreshAfterChange];
            return;
        }
        for (const app::StructuralEdit& edit : queued) {
            plan.edits.push(edit);
        }
        apply_structural_change(plan);
        std::size_t link_adds = 0;
        std::size_t link_deletes = 0;
        for (const app::StructuralEdit& edit : queued) {
            if (edit.kind == app::StructuralKind::delete_link) {
                ++link_deletes;
            } else {
                ++link_adds;
            }
        }
        note(plan, "queued " + std::to_string(queued.size()) + " link edit(s) for task uid " +
                       std::to_string(successor) + ": " + std::to_string(link_adds) +
                       " add(s), " + std::to_string(link_deletes) +
                       " delete(s) (pending; Save writes them)",
             false);
        [g_view refreshAfterChange];
        refresh_menu_bar();
        return;
    }
}

// View > Table Date Format... (wave 16).  The table's date-format byte lives in
// `214/CTable` (table payload +7).  The two codes offered are the PjDateFormat
// values this project has evidence for: 5 is the code Project wrote for
// `DateFormat=5` (w56.md 8.2, the only one measured as a written byte); 4's
// meaning comes from the type library's PjDateFormat enum, not from a reopened
// save.  The edit reaches the file only when Save runs.
- (void)tableDateFormat:(id)sender {
    (void)sender;
    if (g_plan == nullptr || g_plan->sheet == nullptr) {
        return;
    }
    PlanState& plan = *g_plan;
    if (plan.sheet->editing()) {
        plan.sheet->cancel_edit();
    }
    if (plan.document == nullptr) {
        return;
    }
    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Table Date Format"];
    [alert setInformativeText:
               @"Sets the active table's date-format byte in the .mpp. It is written "
               @"when you save, in the same write as the task start dates."];
    [alert addButtonWithTitle:@"Set Format"];
    [alert addButtonWithTitle:@"Cancel"];
    NSPopUpButton* popup =
        [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0.0, 0.0, 320.0, 25.0) pullsDown:NO];
    struct FormatChoice {
        const char* title;
        std::uint8_t code;
    };
    const FormatChoice choices[] = {
        {"4 - mmm dd hh:mm AM", 4},
        {"5 - mmm dd 'yy (measured)", 5},
    };
    for (const FormatChoice& choice : choices) {
        [popup addItemWithTitle:to_ns(std::string(choice.title))];
        [[popup lastItem] setTag:static_cast<NSInteger>(choice.code)];
    }
    [alert setAccessoryView:popup];
    if ([alert runModal] != NSAlertFirstButtonReturn) {
        note(plan, "table date format cancelled", false);
        [g_view setNeedsDisplay:YES];
        return;
    }
    NSMenuItem* selected = [popup selectedItem];
    if (selected == nil) {
        return;
    }
    // Keep any other pending view-state operation (a column replacement) and set
    // only the byte this command owns.
    app::ViewStateEdits edits = plan.document->view_state_edits();
    edits.set_date_format = true;
    edits.date_format = static_cast<std::uint8_t>([selected tag]);
    edits.table_index = 0;  // the measured fixture's only table (the Entry table)
    plan.document->request_view_state_edits(edits);
    note(plan, "table date format " +
                   std::to_string(static_cast<unsigned>(edits.date_format)) +
                   " is pending; Save writes it",
         false);
    [g_view refreshAfterChange];
}

// Edit > Custom Field Formula... (wave 16).  Sets one Task custom field's formula
// or clears the whole formula table through the W64 API
// (Document::apply_custom_formula / clear_custom_formula); Save writes it.  An
// empty formula is refused exactly as the writer refuses it; "Clear Formulas" is
// the measured empty-table form (core/props has no per-field clear).
- (void)customFieldFormula:(id)sender {
    (void)sender;
    if (g_plan == nullptr || g_plan->sheet == nullptr ||
        !app::view_mode_is_task(g_plan->view_mode)) {
        return;
    }
    PlanState& plan = *g_plan;
    if (plan.sheet->editing()) {
        plan.sheet->cancel_edit();
    }
    if (plan.document == nullptr) {
        return;
    }
    // The Task custom fields this shell already names, through the shared registry
    // (core/mpp/field_value.h), not hand-copied ids.
    struct FieldChoice {
        const char* title;
        std::uint32_t field;
    };
    const FieldChoice choices[] = {
        {"Text1", mpp::custom_field_id(mpp::FieldKind::text, 1)},
        {"Text2", mpp::custom_field_id(mpp::FieldKind::text, 2)},
        {"Number1", mpp::custom_field_id(mpp::FieldKind::number, 1)},
        {"Number2", mpp::custom_field_id(mpp::FieldKind::number, 2)},
        {"Duration1", mpp::custom_field_id(mpp::FieldKind::duration, 1)},
        {"Date1", mpp::custom_field_id(mpp::FieldKind::date, 1)},
        {"Flag1", mpp::custom_field_id(mpp::FieldKind::flag, 1)},
    };
    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Custom Field Formula"];
    [alert setInformativeText:
               @"Sets or clears a formula stored in the project's 114/Props stream. "
               @"It is written when you save. An empty formula is refused; use Clear "
               @"Formulas to remove every stored formula."];
    [alert addButtonWithTitle:@"Set Formula"];
    [alert addButtonWithTitle:@"Clear Formulas"];
    [alert addButtonWithTitle:@"Cancel"];
    const CGFloat width = 360.0;
    NSView* accessory = [[NSView alloc] initWithFrame:NSMakeRect(0.0, 0.0, width, 62.0)];
    NSTextField* field_label = [NSTextField labelWithString:@"Field:"];
    [field_label setFrame:NSMakeRect(0.0, 36.0, 60.0, 20.0)];
    [accessory addSubview:field_label];
    NSPopUpButton* field_popup = [[NSPopUpButton alloc]
        initWithFrame:NSMakeRect(64.0, 32.0, width - 64.0, 25.0) pullsDown:NO];
    for (const FieldChoice& choice : choices) {
        [field_popup addItemWithTitle:to_ns(std::string(choice.title))];
        [[field_popup lastItem] setTag:static_cast<NSInteger>(choice.field)];
    }
    [accessory addSubview:field_popup];
    NSTextField* formula_label = [NSTextField labelWithString:@"Formula:"];
    [formula_label setFrame:NSMakeRect(0.0, 4.0, 60.0, 20.0)];
    [accessory addSubview:formula_label];
    NSTextField* formula_field =
        [[NSTextField alloc] initWithFrame:NSMakeRect(64.0, 2.0, width - 64.0, 24.0)];
    [formula_field setPlaceholderString:@"e.g. [Duration]/240"];
    [accessory addSubview:formula_field];
    const app::CustomFormulaEdit pending = plan.document->custom_formula();
    if (pending.operation == app::CustomFormulaEdit::Operation::set) {
        [formula_field setStringValue:to_ns(pending.formula)];
        for (NSMenuItem* item in [field_popup itemArray]) {
            if (static_cast<std::uint32_t>([item tag]) == pending.field_id) {
                [field_popup selectItem:item];
                break;
            }
        }
    }
    [alert setAccessoryView:accessory];
    const NSModalResponse response = [alert runModal];
    if (response == NSAlertThirdButtonReturn) {
        note(plan, "custom field formula cancelled", false);
        [g_view setNeedsDisplay:YES];
        return;
    }
    if (response == NSAlertSecondButtonReturn) {
        plan.document->clear_custom_formula();
        note(plan,
             "clearing every stored custom-field formula is pending; Save writes it",
             false);
        [g_view refreshAfterChange];
        return;
    }
    NSMenuItem* selected = [field_popup selectedItem];
    if (selected == nil) {
        return;
    }
    const std::uint32_t field_id = static_cast<std::uint32_t>([selected tag]);
    std::string error;
    if (!plan.document->apply_custom_formula(field_id, to_std([formula_field stringValue]),
                                             &error)) {
        show_alert(@"Custom Field Formula", to_ns(error));
        [g_view setNeedsDisplay:YES];
        return;
    }
    note(plan, "custom-field formula for field " + std::to_string(field_id) +
                   " is pending; Save writes it",
         false);
    [g_view refreshAfterChange];
}

// The visible view switcher (wave 11): the same code path as the View menu.
- (void)viewModeChanged:(id)sender {
    (void)sender;
    if (g_plan == nullptr) {
        return;
    }
    const NSInteger index = [g_view_control selectedSegment];
    if (index < 0 || index >= static_cast<NSInteger>(app::kViewModeCount)) {
        return;
    }
    set_view_mode(*g_plan, static_cast<app::ViewMode>(index));
    [self syncViewControls];
    [g_view refreshAfterChange];
}

- (void)usagePeriodChanged:(id)sender {
    (void)sender;
    if (g_plan == nullptr) {
        return;
    }
    const NSInteger index = [g_usage_period_popup indexOfSelectedItem];
    if (index < 0 || index > 2) {
        return;
    }
    set_usage_period(*g_plan, static_cast<core::usage::Period>(index));
    [self syncViewControls];
    [g_view refreshAfterChange];
}

// Keeps the control bar and the persisted preferences showing the same state.
- (void)syncViewControls {
    const app::ViewMode mode =
        (g_plan != nullptr && g_plan->sheet != nullptr) ? g_plan->view_mode : g_prefs.view;
    if (g_view_control != nil) {
        [g_view_control setSelectedSegment:static_cast<NSInteger>(mode)];
    }
    if (g_usage_period_popup != nil) {
        const core::usage::Period period =
            (g_plan != nullptr && g_plan->sheet != nullptr) ? g_plan->usage_period
                                                            : g_prefs.usage_period;
        [g_usage_period_popup selectItemAtIndex:static_cast<NSInteger>(period)];
        [g_usage_period_popup setEnabled:app::view_mode_is_usage(mode) ? YES : NO];
    }
}

- (void)toggleColumn:(id)sender {
    if (g_plan == nullptr || g_plan->sheet == nullptr) {
        return;
    }
    const NSInteger tag = [sender tag];
    if (tag < 0) {
        return;
    }
    toggle_column_visibility(*g_plan, static_cast<std::size_t>(tag));
    [g_view refreshAfterChange];
}

- (void)updateWindowTitle:(const std::string&)path title:(const std::string&)title {
    std::string text = title.empty() ? std::string("MS Project Shell") : title;
    if (!path.empty()) {
        text += " - " + path;
    }
    [g_window setTitle:to_ns(text)];
    if (!path.empty()) {
        [g_window setRepresentedFilename:to_ns(path)];
    }
}

- (void)loadPath:(const std::string&)path {
    if (path.empty()) {
        return;
    }
    if (g_plan == nullptr) {
        return;
    }
    const bool loaded = load_plan(*g_plan, path);
    if (loaded) {
        [self updateWindowTitle:path title:g_plan->loaded.title];
        [g_view refreshAfterChange];
        // Fit the window to the new plan (never bigger than the screen-ish cap).
        [g_window setContentSize:NSMakeSize(std::min(content_width(*g_plan), 1500.0),
                                            std::min(content_height(*g_plan), 920.0))];
        [[g_view window] makeFirstResponder:g_view];
        [self syncViewControls];
        [self rebuildColumnsMenu];
        std::printf("msproject-app: %s: %zu tasks, %zu resources, %zu assignments, "
                    "%zu rows, %zu links, %zu bars, %zu warnings\n",
                    path.c_str(), g_plan->loaded.tasks.size(),
                    g_plan->loaded.resources.size(), g_plan->loaded.assignments.size(),
                    g_plan->sheet->row_count(), g_plan->loaded.links.size(),
                    g_plan->gantt.bars.size(), g_plan->warnings);
        std::fflush(stdout);
        return;
    }
    std::fprintf(stderr, "msproject-app: cannot load %s: %s\n", path.c_str(),
                 g_plan->error.c_str());
    [self updateWindowTitle:path title:std::string()];
    [g_view refreshAfterChange];
    show_alert(@"Could not open the project",
               to_ns("The file could not be read as an MS Project file:\n\n" + g_plan->error));
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    [self installMenu];

    g_plan = new PlanState();

    const double width = content_width(*g_plan);
    const double height = content_height(*g_plan);
    const NSRect frame = NSMakeRect(0.0, 0.0, std::min(width, 1500.0), std::min(height, 920.0));

    g_window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                             NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [g_window setTitle:@"MS Project Shell"];
    [g_window setMinSize:NSMakeSize(720.0, 420.0)];
    [g_window setDelegate:self];
    [g_window center];

    // An NSScrollView's defaults already are: no border, white background drawn,
    // and a document view that keeps its own size (we scroll, we do not resize
    // the plan).  Keeping the API surface small keeps the AppKit deprecation
    // surface small, which matters because warnings are errors in CI.
    const CGFloat control_bar_height = 30.0;
    NSView* container = [[NSView alloc]
        initWithFrame:NSMakeRect(0.0, 0.0, frame.size.width, frame.size.height)];

    // Wave 11's visible control: the four views and the usage period, in a bar
    // above the scrolling document.  NSViewMinYMargin pins the bar to the top of
    // the window while the plan scrolls.
    NSView* control_bar = [[NSView alloc]
        initWithFrame:NSMakeRect(0.0, frame.size.height - control_bar_height,
                                 frame.size.width, control_bar_height)];
    [control_bar setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];

    g_view_control = [[NSSegmentedControl alloc]
        initWithFrame:NSMakeRect(8.0, 3.0, 360.0, 24.0)];
    [g_view_control setSegmentCount:static_cast<NSInteger>(app::kViewModeCount)];
    for (NSInteger index = 0; index < static_cast<NSInteger>(app::kViewModeCount); ++index) {
        [g_view_control setLabel:to_ns(app::view_mode_title(static_cast<app::ViewMode>(index)))
                      forSegment:index];
        [g_view_control setWidth:88.0 forSegment:index];
    }
    [g_view_control setSelectedSegment:static_cast<NSInteger>(g_prefs.view)];
    [g_view_control setTarget:self];
    [g_view_control setAction:@selector(viewModeChanged:)];
    [control_bar addSubview:g_view_control];

    NSTextField* period_label =
        [[NSTextField alloc] initWithFrame:NSMakeRect(382.0, 5.0, 90.0, 20.0)];
    [period_label setStringValue:@"Usage period"];
    [period_label setBezeled:NO];
    [period_label setDrawsBackground:NO];
    [period_label setEditable:NO];
    [period_label setSelectable:NO];
    [period_label setFont:[NSFont systemFontOfSize:11.0]];
    [control_bar addSubview:period_label];

    g_usage_period_popup = [[NSPopUpButton alloc]
        initWithFrame:NSMakeRect(474.0, 2.0, 110.0, 26.0)
            pullsDown:NO];
    [g_usage_period_popup addItemsWithTitles:@[ @"Day", @"Week", @"Month" ]];
    [g_usage_period_popup selectItemAtIndex:static_cast<NSInteger>(g_prefs.usage_period)];
    [g_usage_period_popup setTarget:self];
    [g_usage_period_popup setAction:@selector(usagePeriodChanged:)];
    [control_bar addSubview:g_usage_period_popup];

    NSScrollView* scroll = [[NSScrollView alloc]
        initWithFrame:NSMakeRect(0.0, 0.0, frame.size.width,
                                 frame.size.height - control_bar_height)];
    [scroll setHasVerticalScroller:YES];
    [scroll setHasHorizontalScroller:YES];
    [scroll setAutohidesScrollers:YES];
    [scroll setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

    g_view = [[PlanView alloc] initWithFrame:NSMakeRect(0.0, 0.0, width, height)];
    [scroll setDocumentView:g_view];

    [container addSubview:scroll];
    [container addSubview:control_bar];
    [g_window setContentView:container];
    [g_window makeKeyAndOrderFront:nil];
    [g_window makeFirstResponder:g_view];
    [self syncViewControls];

    // Wave 15: `--smoke` replaces the interactive session with the self-smoke, on
    // the window that now exists (app/smoke.h).  The sequence loads the plan
    // through -loadPath:, prints, snapshots, writes the artifacts and exits with
    // the report's verdict -- so a failed check is a red job, not a log nobody
    // reads.  No run loop, no panels, no user.
    if (g_smoke) {
        const int code = [self runSmoke];
        std::printf("msproject-app: smoke exit %d\n", code);
        std::fflush(stdout);
        std::fflush(stderr);
        std::exit(code);
    }

    const std::string path = choose_fixture();
    if (path.empty()) {
        g_plan->error =
            "No .mpp given. Use File > Open, or run with a path: "
            "msproject_app /path/to/file.mpp";
        g_plan->message = g_plan->error;
        g_plan->message_is_error = true;
        [g_view setNeedsDisplay:YES];
        show_alert(@"No project to open", to_ns(g_plan->error));
        return;
    }
    [self loadPath:path];
}

- (void)openDocument:(id)sender {
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    if (@available(macOS 11.0, *)) {
        UTType* mpp = [UTType typeWithFilenameExtension:@"mpp"];
        if (mpp != nil) {
            [panel setAllowedContentTypes:@[ mpp ]];
        }
    }
    if ([panel runModal] != NSModalResponseOK) {
        return;
    }
    NSURL* url = [panel URL];
    if (url == nil) {
        return;
    }
    [self loadPath:to_std([url path])];
}

// The one save path.  Save reuses the last target (or asks the first time);
// Save As always asks.  Returns YES when the edits were written or there was
// nothing to write, NO on a cancelled panel or a writer failure -- which is what
// the close prompt needs to know before it lets the window go.
- (BOOL)saveDocumentWithPanel:(BOOL)forcePanel {
    if (g_plan == nullptr || g_plan->sheet == nullptr) {
        return NO;
    }
    PlanState& plan = *g_plan;
    if (plan.sheet->editing()) {
        plan.sheet->cancel_edit();
    }
    const std::vector<app::StartEdit> edits =
        app::collect_start_edits(plan.records, plan.loaded);
    const bool structural = !plan.edits.empty();
    // Wave 16: a pending View > Table Date Format / Edit > Custom Field Formula
    // edit lives in the Document, not in the start-edit diff, so it is work to
    // save as well.
    const bool document_edits =
        plan.document != nullptr && plan.document->has_pending_edits();

    // Wave 14: warn BEFORE the write when a start edit cannot take effect (a
    // linked pin behind the predecessor's finish, or a manual task that the
    // writer converts to an auto-scheduled SNET pin).  Once per save: this is
    // the single place every save passes through.  The writer still stores the
    // exact dates -- the warning is about what Project will reopen, not about
    // changing the write (docs/audit-handoff.md sections 3 and 4.2).
    if (!edits.empty()) {
        const app::StartEditWarningReport warning =
            app::warn_start_edits(edits, plan.view, plan.format);
        if (warning.warnings != 0) {
            NSAlert* alert = [[NSAlert alloc] init];
            [alert setMessageText:@"Some start edits may not take effect"];
            [alert setInformativeText:to_ns(
                warning.text +
                "\n\nThe writer still stores the exact dates, but Project may reopen a "
                "linked task at a later date or reschedule a manual task.\n\nSave "
                "anyway?")];
            [alert addButtonWithTitle:@"Save Anyway"];
            [alert addButtonWithTitle:@"Cancel"];
            if ([alert runModal] != NSAlertFirstButtonReturn) {
                note(plan, "save cancelled at the start-edit warning", false);
                [g_view setNeedsDisplay:YES];
                return NO;
            }
        }
    }
    if (edits.empty() && !structural && !document_edits) {
        note(plan, "nothing to save: no task start has been edited and no insert, delete, "
                   "custom-field, link, table-format or formula edit is pending",
             false);
        [g_view refreshAfterChange];
        return YES;
    }

    std::string target = g_target_path;
    if (forcePanel || target.empty()) {
        NSSavePanel* panel = [NSSavePanel savePanel];
        [panel setCanCreateDirectories:YES];
        [panel setNameFieldStringValue:to_ns(app::default_save_name(plan.path))];
        if (!plan.path.empty()) {
            const std::filesystem::path source(plan.path);
            [panel setDirectoryURL:[NSURL fileURLWithPath:to_ns(source.parent_path().string())]];
        }
        if ([panel runModal] != NSModalResponseOK) {
            note(plan, "save cancelled", false);
            [g_view setNeedsDisplay:YES];
            return NO;
        }
        NSURL* url = [panel URL];
        if (url == nil) {
            return NO;
        }
        target = to_std([url path]);
    }

    if (structural) {
        return [self saveStructuralEdits:edits target:target];
    }

    app::SaveOptions options;
    // Saving over a file THIS APP wrote is allowed (a structural save reloads the
    // target and adopts it); the file the user opened is never overwritten.
    options.allow_source_overwrite = plan.owns_path;
    if (plan.document == nullptr) {
        plan.document = std::make_unique<app::Document>(plan.path);
    }
    // Wave 16: ONE write.  Document applies the pending `214/CTable` and
    // `114/Props` operations to the container the date writer then edits, so a
    // start pin, a table date format and a formula land in one file (w64.md 5).
    // The two stream sets are disjoint, which is why the date writer's "every
    // other stream unchanged" proof still holds; Document::save runs it that way.
    const app::SaveOutcome outcome = plan.document->save(edits, target, options);
    if (!outcome.ok) {
        note(plan, "save failed: " + outcome.error, true);
        [g_view refreshAfterChange];
        show_alert(@"Could not save the pending edits",
                   to_ns(outcome.error + "\n\nThe opened file was not modified."));
        return NO;
    }

    app::accept_start_edits(plan.loaded, edits);
    // The write carried them, so they are no longer pending.
    plan.document->discard_view_state_edits();
    plan.document->discard_custom_formula();
    g_target_path = outcome.path;
    std::string saved_parts;
    if (outcome.rows_written != 0) {
        saved_parts = std::to_string(outcome.rows_written) + " start(s)";
    }
    if (outcome.view_state_written) {
        if (!saved_parts.empty()) {
            saved_parts += " + ";
        }
        saved_parts += "the table date format";
    }
    if (outcome.formula_written) {
        if (!saved_parts.empty()) {
            saved_parts += " + ";
        }
        saved_parts += "the custom-field formula";
    }
    if (saved_parts.empty()) {
        saved_parts = "the file";
    }
    std::string message =
        "saved " + saved_parts + " to " + outcome.path + " (" + outcome.api + ")";
    if (!outcome.warnings.empty()) {
        message += ", " + std::to_string(outcome.warnings.size()) + " warning(s)";
    }
    if (!outcome.notes.empty()) {
        message += ", " + std::to_string(outcome.notes.size()) + " writer note(s)";
    }
    note(plan, message, false);
    [g_view refreshAfterChange];

    // The writer's receipts go to the console (they are per-row "re-read from
    // disk" lines, and the manual-row warning that Project will ignore the pin).
    // Only a problem the app itself detected is worth a modal dialog.
    for (const std::string& entry : outcome.notes) {
        std::printf("msproject-app: save note: %s\n", entry.c_str());
    }
    for (const std::string& warning : outcome.warnings) {
        std::fprintf(stderr, "msproject-app: save warning: %s\n", warning.c_str());
    }
    std::fflush(stdout);
    if (!outcome.warnings.empty()) {
        std::string details;
        for (const std::string& warning : outcome.warnings) {
            details += warning + "\n";
        }
        show_alert(@"Saved with warnings",
                   to_ns("Wrote " + std::to_string(outcome.rows_written) + " task start(s) to:\n" +
                         outcome.path + "\n\n" + details));
    }
    return YES;
}

// The structural save path (wave 12).  The writer applies the pending edits to
// the current document and writes the target; the app then RELOADS the target,
// because the added rows' real uids and ids are the file's: never patch a uid by
// hand after a write.
- (BOOL)saveStructuralEdits:(const std::vector<app::StartEdit>&)edits
                     target:(const std::string&)target {
    PlanState& plan = *g_plan;
    // Wave 17/B1: the pending table-format/formula edits now ride the SAME
    // container write as the structural cascade (a DocumentEditHook inside
    // save_structural_edits), so the wave-16 second-write fallback is gone.
    app::StructuralSaveResult outcome;
    if (plan.document != nullptr) {
        outcome = plan.document->save_structural(plan.edits, edits, target, plan.owns_path);
    } else {
        outcome = app::save_structural_edits(plan.edits, edits, plan.path, target,
                                             plan.owns_path);
    }
    if (!outcome.ok) {
        note(plan, "save failed: " + outcome.error, true);
        [g_view refreshAfterChange];
        show_alert(@"Could not save the pending edits",
                   to_ns(outcome.error + "\n\nThe opened file was not modified."));
        return NO;
    }
    std::string message = "saved " + std::to_string(outcome.added) + " new task(s), " +
                          std::to_string(outcome.deleted) + " deleted task(s)";
    if (outcome.links_added != 0 || outcome.links_deleted != 0) {
        message += ", " + std::to_string(outcome.links_added) + " link(s) added and " +
                   std::to_string(outcome.links_deleted) + " deleted";
    }
    if (outcome.fields_written != 0) {
        message += ", " + std::to_string(outcome.fields_written) +
                   " custom-field value(s) written";
    }
    if (outcome.start_rows_written != 0) {
        message += ", " + std::to_string(outcome.start_rows_written) + " start(s)";
    }
    if (outcome.links_tombstoned != 0 || outcome.assignments_tombstoned != 0) {
        message += ", tombstoned " + std::to_string(outcome.links_tombstoned) + " link(s) and " +
                   std::to_string(outcome.assignments_tombstoned) + " assignment(s)";
    }
    message += " to " + outcome.path + " (" + outcome.api + ")";
    if (outcome.added != 0) {
        // The writer's add clones the file's LAST live row and clears its
        // constraint; the app passes only the name.  Say so rather than implying
        // that a cell typed on the placeholder row was written.
        message += " (a new task copies the file's last row; only its name is written)";
    }
    for (const std::string& entry : outcome.notes) {
        std::printf("msproject-app: save note: %s\n", entry.c_str());
    }
    for (const std::string& warning : outcome.warnings) {
        std::fprintf(stderr, "msproject-app: save warning: %s\n", warning.c_str());
    }
    std::fflush(stdout);

    if (load_plan(plan, outcome.path)) {
        plan.owns_path = true;
        g_target_path = outcome.path;
        [self updateWindowTitle:outcome.path title:plan.loaded.title];
        [self rebuildColumnsMenu];
        [g_view refreshAfterChange];
        [g_window setContentSize:NSMakeSize(std::min(content_width(plan), 1500.0),
                                            std::min(content_height(plan), 920.0))];
        note(plan, message + "  |  reloaded the file", false);
        return YES;
    }
    const std::string reload_error = plan.error;
    note(plan, message + "  |  but the saved file could not be re-read: " + reload_error, true);
    [g_view refreshAfterChange];
    show_alert(@"Saved, but the file could not be re-read",
               to_ns(reload_error + "\n\nThe file was written. Reopen it with File > Open."));
    return YES;
}

- (BOOL)saveDocument:(id)sender {
    (void)sender;
    return [self saveDocumentWithPanel:NO];
}

- (void)saveDocumentAs:(id)sender {
    (void)sender;
    (void)[self saveDocumentWithPanel:YES];
}

- (void)saveAs:(id)sender {
    [self saveDocumentAs:sender];
}

- (void)undo:(id)sender {
    (void)sender;
    if (g_plan == nullptr || g_plan->sheet == nullptr ||
        !app::view_mode_is_task(g_plan->view_mode)) {
        return;
    }
    if (g_plan->sheet->editing()) {
        g_plan->sheet->cancel_edit();
    }
    PlanState& plan = *g_plan;
    if (plan.sheet->can_undo()) {
        if (plan.sheet->undo()) {
            note(plan, "undo", false);
        }
    } else if (plan.edits.can_undo()) {
        // A structural edit is undone by rebuilding the display plan without it;
        // the sheet's own history was cleared when the edit was pushed.
        plan.edits.undo();
        apply_structural_change(plan);
        note(plan, "undo: task insert/delete", false);
    } else {
        note(plan, "nothing to undo", false);
    }
    [g_view refreshAfterChange];
}

- (void)redo:(id)sender {
    (void)sender;
    if (g_plan == nullptr || g_plan->sheet == nullptr ||
        !app::view_mode_is_task(g_plan->view_mode)) {
        return;
    }
    if (g_plan->sheet->editing()) {
        g_plan->sheet->cancel_edit();
    }
    PlanState& plan = *g_plan;
    if (plan.sheet->can_redo()) {
        if (plan.sheet->redo()) {
            note(plan, "redo", false);
        }
    } else if (plan.edits.can_redo()) {
        plan.edits.redo();
        apply_structural_change(plan);
        note(plan, "redo: task insert/delete", false);
    } else {
        note(plan, "nothing to redo", false);
    }
    [g_view refreshAfterChange];
}

- (void)copy:(id)sender {
    (void)sender;
    if (g_plan == nullptr || !app::view_mode_is_task(g_plan->view_mode)) {
        return;
    }
    copy_selection(*g_plan);
    [g_view setNeedsDisplay:YES];
}

- (void)paste:(id)sender {
    (void)sender;
    if (g_plan == nullptr || !app::view_mode_is_task(g_plan->view_mode)) {
        return;
    }
    paste_from_pasteboard(*g_plan);
    [g_view refreshAfterChange];
}

- (void)selectAll:(id)sender {
    (void)sender;
    if (g_plan == nullptr || g_plan->sheet == nullptr || g_plan->sheet->editing() ||
        !app::view_mode_is_task(g_plan->view_mode)) {
        return;
    }
    g_plan->sheet->select_all();
    note(*g_plan, "selected every cell", false);
    [g_view selectionChanged];
}

// Cut is a menu item the sheet model cannot support (no cut operation), so it is
// greyed out; this handler only exists so the dispatch switch is exhaustive.
- (void)cut:(id)sender {
    (void)sender;
    if (g_plan == nullptr) {
        return;
    }
    note(*g_plan,
         std::string("Cut is not available: ") +
             app::menu_disabled_reason(app::MenuAction::cut),
         true);
    [g_view setNeedsDisplay:YES];
}

- (void)exportSheetReport:(id)sender {
    (void)sender;
    if (g_plan == nullptr) {
        return;
    }
    run_export_panel(*g_plan, app::ReportKind::sheet);
    [g_view setNeedsDisplay:YES];
}

- (void)exportGanttReport:(id)sender {
    (void)sender;
    if (g_plan == nullptr) {
        return;
    }
    run_export_panel(*g_plan, app::ReportKind::gantt);
    [g_view setNeedsDisplay:YES];
}

// File > Print... (wave 14).  The standard print panel, fed by the portable
// page layout; the view draws the same table + chart the window does.
- (void)printDocument:(id)sender {
    (void)sender;
    if (g_plan == nullptr || g_plan->sheet == nullptr ||
        !app::view_mode_is_task(g_plan->view_mode)) {
        return;
    }
    PlanState& plan = *g_plan;
    const app::PrintJob job =
        app::build_print_job(make_print_request(plan), app::PrintRoute::printer);
    if (!run_print_view(plan, job, nil)) {
        note(plan, "print cancelled or failed", true);
        [g_view setNeedsDisplay:YES];
        return;
    }
    note(plan, "printed " + std::to_string(job.pages.size()) + " page(s)", false);
    [g_view setNeedsDisplay:YES];
}

// File > Save as PDF... (wave 14).  The SAME PrintJob and PrintView as Print...,
// run as an NSPrintSaveJob, so the PDF paginates exactly like the printout and
// the shared app::page_layout.h governs both.  File > Export PDF still exists and
// uses the same PageSpec through core/render.
- (void)saveAsPDF:(id)sender {
    (void)sender;
    if (g_plan == nullptr || g_plan->sheet == nullptr ||
        !app::view_mode_is_task(g_plan->view_mode)) {
        return;
    }
    PlanState& plan = *g_plan;
    const app::PrintRequest request = make_print_request(plan);
    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setCanCreateDirectories:YES];
    [panel setNameFieldStringValue:to_ns(
        app::export_default_name(plan.path, app::ReportKind::gantt))];
    if (!plan.path.empty()) {
        [panel setDirectoryURL:[NSURL
                                  fileURLWithPath:to_ns(std::filesystem::path(plan.path)
                                                            .parent_path()
                                                            .string())]];
    }
    if (@available(macOS 11.0, *)) {
        UTType* pdf_type = [UTType typeWithIdentifier:@"com.adobe.pdf"];
        if (pdf_type != nil) {
            [panel setAllowedContentTypes:@[ pdf_type ]];
        }
    }
    if ([panel runModal] != NSModalResponseOK) {
        note(plan, "save as PDF cancelled", false);
        return;
    }
    NSURL* url = [panel URL];
    if (url == nil) {
        return;
    }
    const app::PrintJob job = app::build_print_job(request, app::PrintRoute::pdf);
    if (!run_print_view(plan, job, url)) {
        note(plan, "could not write the PDF", true);
        show_alert(@"Could not save the PDF",
                   @"The print operation's save job did not complete. The plan was not "
                    "modified.");
        return;
    }
    note(plan, "wrote " + to_std([url path]) + " (" +
                   std::to_string(job.pages.size()) + " page(s))",
         false);
    std::printf("msproject-app: saved PDF %s\n", to_std([url path]).c_str());
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Wave 15: the self-smoke (`--smoke <fixture.mpp> --out <dir>`).
//
// The window and the plan view already exist: -applicationDidFinishLaunching built
// them, and this runs in its place, before choose_fixture().  The portable
// sequence runs first (app/smoke.cpp, also unit-tested on Windows as `app_smoke`);
// then the AppKit paths a human would exercise:
//
//   * load the plan through the REAL load path (-loadPath:), from the COPY the
//     portable half made -- never the committed fixture;
//   * print through the REAL print path (make_print_request + build_print_job +
//     run_print_view) with a save URL instead of the print panel: the Gantt job
//     File > Print... builds, and the sheet job, so both app::PrintKind values are
//     exercised;
//   * snapshot the document view with -dataWithPDFInsideRect: to smoke-window.pdf
//     and through an NSBitmapImageRep to smoke-window.png.
//
// No panels, no dialogs (show_alert is a no-op in this mode), no interaction.  The
// return value is the process exit code: 0 only when every check passed.
// ---------------------------------------------------------------------------
- (int)runSmoke {
    // Deterministic view state: the persisted preferences belong to a human's last
    // session (a runner has none, a developer's Mac may).  This does NOT write to
    // NSUserDefaults; it only makes this run's sheet/Gantt the defaults, which is
    // what app::smoke_plan() also uses.
    g_prefs = app::Preferences{};

    app::SmokeOptions options;
    options.fixture_path = g_smoke_fixture;
    options.out_dir = g_smoke_out;
    app::SmokeReport report = app::smoke_plan(options);
    std::printf("msproject-app: %s\n", app::smoke_summary(report).c_str());
    std::fflush(stdout);

    const std::filesystem::path out(g_smoke_out);
    std::error_code file_error;
    std::filesystem::create_directories(out, file_error);
    const std::string input_path = (out / app::kSmokeInputFile).string();

    @try {
        // ---- the window the smoke runs in -------------------------------
        app::smoke_begin_step(report, "appkit.window");
        app::smoke_add_check(report, "appkit.window", "the window was created",
                             g_window != nil);
        app::smoke_add_check(report, "appkit.window", "the window has a content view",
                             g_window != nil && [g_window contentView] != nil);
        app::smoke_add_check(report, "appkit.window",
                             "the plan view is the window's document view",
                             g_view != nil && [g_view window] == g_window);

        // The real load path: reader, RecordSet, SheetModel, RowMap, Gantt, window
        // resize, the columns menu.
        if (report.error.empty() &&
            std::filesystem::is_regular_file(input_path, file_error)) {
            [self loadPath:input_path];
        } else {
            app::smoke_add_check(report, "appkit.window",
                                 "the smoke copy exists to load", false,
                                 input_path + " (" + report.error + ")");
        }
        const bool plan_ready = g_plan != nullptr && g_plan->sheet != nullptr;
        app::smoke_add_check(report, "appkit.window", "the real load path opened the plan",
                             plan_ready,
                             g_plan != nullptr ? g_plan->error : std::string("no plan state"));
        if (plan_ready) {
            const PlanState& plan = *g_plan;
            app::smoke_add_check(report, "appkit.window", "the loaded plan has three tasks",
                                 plan.loaded.tasks.size() == 3,
                                 "tasks=" + std::to_string(plan.loaded.tasks.size()));
            app::smoke_add_check(report, "appkit.window", "the sheet has three rows",
                                 plan.sheet->row_count() == 3,
                                 "rows=" + std::to_string(plan.sheet->row_count()));
            // Wave 26 (W26-0.1): these count ROWS, not tasks.  A file that
            // shows the project row has one more displayed row than it has
            // tasks, and the old `bars == tasks` check was the assertion that
            // hid the row-shown print defect.  The row-shown sequence below
            // exercises the same paths on tests/data/wave24-order.mpp.
            app::smoke_add_check(report, "appkit.window",
                                 "the sheet has one row per RecordSet row",
                                 plan.sheet->row_count() == plan.records.row_count(),
                                 "rows=" + std::to_string(plan.sheet->row_count()) +
                                     ", records=" +
                                     std::to_string(plan.records.row_count()));
            app::smoke_add_check(report, "appkit.window",
                                 "the displayed row list has one row per RecordSet row",
                                 plan.row_map.size() == plan.records.row_count(),
                                 "rows=" + std::to_string(plan.row_map.size()) +
                                     ", records=" +
                                     std::to_string(plan.records.row_count()));
            app::smoke_add_check(report, "appkit.window",
                                 "the Gantt has a bar for each displayed row",
                                 plan.gantt.bars.size() == plan.row_map.size(),
                                 "bars=" + std::to_string(plan.gantt.bars.size()) +
                                     ", rows=" + std::to_string(plan.row_map.size()));
            app::smoke_add_check(report, "appkit.window",
                                 "the sheet has laid-out columns", plan.columns.size() >= 5,
                                 "columns=" + std::to_string(plan.columns.size()));
            app::smoke_add_check(report, "appkit.window",
                                 "the document view has a non-empty frame",
                                 [g_view bounds].size.width > 0.0 &&
                                     [g_view bounds].size.height > 0.0);
            app::smoke_add_check(
                report, "appkit.window",
                "the loaded path is the smoke copy, not the committed fixture",
                plan.path == input_path, plan.path);
        }

        // ---- print through the real print path --------------------------
        if (plan_ready) {
            PlanState& plan = *g_plan;
            app::smoke_begin_step(report, "appkit.print");

            const app::PrintJob gantt_job = app::build_print_job(
                make_print_request(plan), app::PrintRoute::pdf);
            const std::filesystem::path gantt_pdf = out / app::kSmokePrintGanttPdf;
            std::filesystem::remove(gantt_pdf, file_error);
            NSURL* gantt_url = [NSURL fileURLWithPath:to_ns(gantt_pdf.string())];
            const BOOL gantt_saved = run_print_view(plan, gantt_job, gantt_url);
            app::smoke_add_check(report, "appkit.print",
                                 "run_print_view writes the Gantt print job as a PDF",
                                 gantt_saved == YES, "run_print_view returned NO");
            app::smoke_add_check(report, "appkit.print",
                                 "the Gantt print PDF exists and is not empty",
                                 path_size(gantt_pdf) > 0,
                                 "bytes=" + std::to_string(path_size(gantt_pdf)));
            app::smoke_add_artifact(report, "print-gantt-pdf", gantt_pdf.string());

            app::PrintRequest sheet_request = make_print_request(plan);
            sheet_request.kind = app::PrintKind::sheet;
            sheet_request.report_title = "Sheet report";
            const app::PrintJob sheet_job =
                app::build_print_job(sheet_request, app::PrintRoute::pdf);
            const std::filesystem::path sheet_pdf = out / app::kSmokePrintSheetPdf;
            std::filesystem::remove(sheet_pdf, file_error);
            NSURL* sheet_url = [NSURL fileURLWithPath:to_ns(sheet_pdf.string())];
            const BOOL sheet_saved = run_print_view(plan, sheet_job, sheet_url);
            app::smoke_add_check(report, "appkit.print",
                                 "run_print_view writes the sheet print job as a PDF",
                                 sheet_saved == YES, "run_print_view returned NO");
            app::smoke_add_check(report, "appkit.print",
                                 "the sheet print PDF exists and is not empty",
                                 path_size(sheet_pdf) > 0,
                                 "bytes=" + std::to_string(path_size(sheet_pdf)));
            app::smoke_add_artifact(report, "print-sheet-pdf", sheet_pdf.string());
        }

        // ---- the window snapshot ----------------------------------------
        if (plan_ready) {
            app::smoke_begin_step(report, "appkit.snapshot");
            const NSRect bounds = [g_view bounds];

            NSData* window_pdf = [g_view dataWithPDFInsideRect:bounds];
            const std::filesystem::path window_pdf_path = out / app::kSmokeWindowPdf;
            const BOOL pdf_written =
                window_pdf != nil && [window_pdf length] > 0 &&
                [window_pdf writeToFile:to_ns(window_pdf_path.string()) atomically:YES];
            app::smoke_add_check(report, "appkit.snapshot",
                                 "the window snapshot writes smoke-window.pdf",
                                 pdf_written == YES,
                                 "bytes=" + std::to_string(
                                               static_cast<unsigned long long>(
                                                   window_pdf != nil ? [window_pdf length] : 0)));
            app::smoke_add_check(report, "appkit.snapshot",
                                 "smoke-window.pdf exists and is not empty",
                                 path_size(window_pdf_path) > 0,
                                 "bytes=" + std::to_string(path_size(window_pdf_path)));
            app::smoke_add_artifact(report, "window-pdf", window_pdf_path.string());

            NSBitmapImageRep* snapshot =
                [g_view bitmapImageRepForCachingDisplayInRect:bounds];
            NSData* window_png = nil;
            if (snapshot != nil) {
                [g_view cacheDisplayInRect:bounds toBitmapImageRep:snapshot];
                window_png = [snapshot representationUsingType:NSBitmapImageFileTypePNG
                                                    properties:@{}];
            }
            const std::filesystem::path window_png_path = out / app::kSmokeWindowPng;
            const BOOL png_written =
                window_png != nil && [window_png length] > 0 &&
                [window_png writeToFile:to_ns(window_png_path.string()) atomically:YES];
            app::smoke_add_check(report, "appkit.snapshot",
                                 "the window snapshot writes smoke-window.png",
                                 png_written == YES,
                                 "bitmap rep is nil or the PNG is empty");
            app::smoke_add_check(report, "appkit.snapshot",
                                 "smoke-window.png exists and is not empty",
                                 path_size(window_png_path) > 0,
                                 "bytes=" + std::to_string(path_size(window_png_path)));
            app::smoke_add_artifact(report, "window-png", window_png_path.string());
        }

        // ---- the row-shown fixture: the REAL load and print paths --------
        // WAVE 26 (W26-0.1).  The fixture the smoke is given in CI
        // (synthetic-base) HIDES the project row, and the wave-25 review's High
        // print defect only appears when row 0 is shown.  Run the row-addressed
        // sequence and then the real load + print paths on the committed
        // row-shown fixture; smoke_row_shown() makes the copy this loads.
#if defined(APP_TEST_DATA_DIR)
        {
            app::smoke_begin_step(report, "appkit.row_shown");
            const std::filesystem::path fixture =
                std::filesystem::path(APP_TEST_DATA_DIR) / "wave24-order.mpp";
            std::error_code row_error;
            const bool fixture_present =
                std::filesystem::is_regular_file(fixture, row_error);
            app::smoke_add_check(report, "appkit.row_shown",
                                 "the committed row-shown fixture is present",
                                 fixture_present, fixture.string());
            if (fixture_present) {
                app::SmokeOptions row_options;
                row_options.fixture_path = fixture.string();
                row_options.out_dir = g_smoke_out;
                row_options.date_text = options.date_text;
                std::string row_smoke_error;
                const bool row_sequence_ok =
                    app::smoke_row_shown(row_options, report, row_smoke_error);
                app::smoke_add_check(report, "appkit.row_shown",
                                     "the row-shown sequence runs to the end",
                                     row_sequence_ok, row_smoke_error);
                if (row_sequence_ok) {
                    const std::string row_input =
                        (out / app::kSmokeRowShownInputFile).string();
                    [self loadPath:row_input];
                    const bool row_ready =
                        g_plan != nullptr && g_plan->sheet != nullptr &&
                        g_plan->loaded.show_project_summary;
                    app::smoke_add_check(report, "appkit.row_shown",
                                         "the real load path opened the row-shown plan",
                                         row_ready,
                                         g_plan != nullptr ? g_plan->error
                                                           : std::string("no plan state"));
                    if (row_ready) {
                        PlanState& row_plan = *g_plan;
                        const app::PrintRequest request = make_print_request(row_plan);
                        app::smoke_add_check(
                            report, "appkit.row_shown",
                            "the print request holds every RecordSet row",
                            request.rows.size() == row_plan.records.row_count(),
                            "rows=" + std::to_string(request.rows.size()) +
                                ", records=" +
                                std::to_string(row_plan.records.row_count()));
                        app::smoke_add_check(
                            report, "appkit.row_shown",
                            "the print request's last row is the highest-ID task",
                            !request.rows.empty() &&
                                request.rows.back() + 1 == row_plan.records.row_count(),
                            "last=" + (request.rows.empty()
                                           ? std::string("none")
                                           : std::to_string(request.rows.back())));
                        const app::PrintJob row_job =
                            app::build_print_job(request, app::PrintRoute::pdf);
                        const std::filesystem::path row_pdf =
                            out / app::kSmokeRowShownPrintPdf;
                        std::filesystem::remove(row_pdf, file_error);
                        NSURL* row_url =
                            [NSURL fileURLWithPath:to_ns(row_pdf.string())];
                        const BOOL row_saved =
                            run_print_view(row_plan, row_job, row_url);
                        app::smoke_add_check(
                            report, "appkit.row_shown",
                            "run_print_view writes the row-shown print job as a PDF",
                            row_saved == YES, "run_print_view returned NO");
                        app::smoke_add_check(
                            report, "appkit.row_shown",
                            "the row-shown print PDF exists and is not empty",
                            path_size(row_pdf) > 0,
                            "bytes=" + std::to_string(path_size(row_pdf)));
                        app::smoke_add_artifact(report, "print-rows-shown-pdf",
                                                row_pdf.string());
                    }
                }
            }
        }
#endif
    } @catch (NSException* exception) {
        app::smoke_add_check(report, "appkit", "no Objective-C exception in the AppKit steps",
                             false, to_std([exception reason]));
    }

    // ---- the report the CI artifact upload picks up ----------------------
    std::string json_error;
    const bool json_written = app::smoke_write_json(report, g_smoke_out, json_error);
    app::smoke_add_check(report, "appkit.report", "smoke.json is written", json_written,
                         json_error);
    for (const app::SmokeArtifact& artifact : report.artifacts) {
        std::printf("msproject-app: artifact %s: %s\n", artifact.name.c_str(),
                    artifact.path.c_str());
    }
    std::printf("msproject-app: %s\n", app::smoke_summary(report).c_str());
    std::fflush(stdout);

    return report.ok() ? 0 : 1;
}

// The Preferences panel: four controls, one change handler, applied live and
// written to NSUserDefaults (app/prefs.h explains why they are app-side).
- (void)showPreferences:(id)sender {
    (void)sender;
    if (g_prefs_window == nil) {
        const NSRect frame = NSMakeRect(0.0, 0.0, 440.0, 220.0);
        g_prefs_window = [[NSWindow alloc]
            initWithContentRect:frame
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [g_prefs_window setTitle:@"Preferences"];
        [g_prefs_window setReleasedWhenClosed:NO];
        [g_prefs_window center];
        NSView* content = [g_prefs_window contentView];

        NSTextField* date_label =
            [[NSTextField alloc] initWithFrame:NSMakeRect(20.0, 172.0, 120.0, 20.0)];
        [date_label setStringValue:@"Date format"];
        [date_label setBezeled:NO];
        [date_label setDrawsBackground:NO];
        [date_label setEditable:NO];
        [date_label setSelectable:NO];
        [content addSubview:date_label];

        g_date_style_popup = [[NSPopUpButton alloc]
            initWithFrame:NSMakeRect(145.0, 167.0, 275.0, 26.0)
                pullsDown:NO];
        [g_date_style_popup addItemsWithTitles:@[
            @"Tue 20/10/26 (day/month/year)", @"Tue 10/20/26 (month/day/year)",
            @"2026-10-20 (ISO)"
        ]];
        [g_date_style_popup setTarget:[NSApp delegate]];
        [g_date_style_popup setAction:@selector(preferencesChanged:)];
        [content addSubview:g_date_style_popup];

        NSTextField* duration_label =
            [[NSTextField alloc] initWithFrame:NSMakeRect(20.0, 140.0, 120.0, 20.0)];
        [duration_label setStringValue:@"Duration format"];
        [duration_label setBezeled:NO];
        [duration_label setDrawsBackground:NO];
        [duration_label setEditable:NO];
        [duration_label setSelectable:NO];
        [content addSubview:duration_label];

        g_duration_style_popup = [[NSPopUpButton alloc]
            initWithFrame:NSMakeRect(145.0, 135.0, 275.0, 26.0)
                pullsDown:NO];
        [g_duration_style_popup addItemsWithTitles:@[
            @"Days and hours (3 days, 4 hrs)", @"Hours only (24 hrs)", @"Days only (0.5 days)"
        ]];
        [g_duration_style_popup setTarget:[NSApp delegate]];
        [g_duration_style_popup setAction:@selector(preferencesChanged:)];
        [content addSubview:g_duration_style_popup];

        NSTextField* timescale_label =
            [[NSTextField alloc] initWithFrame:NSMakeRect(20.0, 108.0, 120.0, 20.0)];
        [timescale_label setStringValue:@"Timescale"];
        [timescale_label setBezeled:NO];
        [timescale_label setDrawsBackground:NO];
        [timescale_label setEditable:NO];
        [timescale_label setSelectable:NO];
        [content addSubview:timescale_label];

        g_timescale_popup = [[NSPopUpButton alloc]
            initWithFrame:NSMakeRect(145.0, 103.0, 275.0, 26.0)
                pullsDown:NO];
        [g_timescale_popup addItemsWithTitles:@[ @"Days", @"Weeks", @"Months" ]];
        [g_timescale_popup setTarget:[NSApp delegate]];
        [g_timescale_popup setAction:@selector(preferencesChanged:)];
        [content addSubview:g_timescale_popup];

        g_shading_checkbox = [[NSButton alloc]
            initWithFrame:NSMakeRect(20.0, 62.0, 300.0, 22.0)];
        [g_shading_checkbox setButtonType:NSButtonTypeSwitch];
        [g_shading_checkbox setTitle:@"Shade non-working time in the Gantt"];
        [g_shading_checkbox setTarget:[NSApp delegate]];
        [g_shading_checkbox setAction:@selector(preferencesChanged:)];
        [content addSubview:g_shading_checkbox];

        NSTextField* note_label =
            [[NSTextField alloc] initWithFrame:NSMakeRect(20.0, 12.0, 270.0, 40.0)];
        [note_label setStringValue:
                        @"Stored in this app's preferences. MS Project exposes no global "
                         @"date/duration/timescale setter, so nothing is written to a .mpp."];
        [note_label setBezeled:NO];
        [note_label setDrawsBackground:NO];
        [note_label setEditable:NO];
        [note_label setSelectable:NO];
        [note_label setFont:[NSFont systemFontOfSize:9.0]];
        [note_label setTextColor:[NSColor secondaryLabelColor]];
        [content addSubview:note_label];

        NSButton* close = [[NSButton alloc]
            initWithFrame:NSMakeRect(345.0, 18.0, 80.0, 30.0)];
        [close setTitle:@"Close"];
        [close setBezelStyle:NSBezelStyleRounded];
        [close setTarget:g_prefs_window];
        [close setAction:@selector(performClose:)];
        [content addSubview:close];
    }
    [g_date_style_popup selectItemAtIndex:static_cast<NSInteger>(g_prefs.date_style)];
    [g_duration_style_popup selectItemAtIndex:static_cast<NSInteger>(g_prefs.duration_style)];
    [g_timescale_popup selectItemAtIndex:static_cast<NSInteger>(g_prefs.zoom)];
    [g_shading_checkbox
        setState:g_prefs.working_day_shading ? NSControlStateValueOn : NSControlStateValueOff];
    [g_prefs_window makeKeyAndOrderFront:nil];
}

- (void)preferencesChanged:(id)sender {
    (void)sender;
    const NSInteger date_index = [g_date_style_popup indexOfSelectedItem];
    if (date_index >= 0 && date_index <= 2) {
        g_prefs.date_style = static_cast<app::DateStyle>(date_index);
    }
    const NSInteger duration_index = [g_duration_style_popup indexOfSelectedItem];
    if (duration_index >= 0 && duration_index <= 2) {
        g_prefs.duration_style = static_cast<app::DurationStyle>(duration_index);
    }
    const NSInteger zoom_index = [g_timescale_popup indexOfSelectedItem];
    if (zoom_index >= 0 && zoom_index <= 2) {
        g_prefs.zoom = static_cast<app::ZoomLevel>(zoom_index);
    }
    g_prefs.working_day_shading = [g_shading_checkbox state] == NSControlStateValueOn;
    save_preferences_to_defaults();
    if (g_plan != nullptr && g_plan->sheet != nullptr) {
        apply_preferences_to_plan(*g_plan);
        note(*g_plan, app::preferences_summary(g_prefs), false);
        [g_view refreshAfterChange];
    }
    refresh_menu_bar();
}

// The filter dialog: one criterion (column, operator, value) plus Apply/Clear/
// Cancel.  app/filter_model.h turns the criterion into the core's filter tree and
// reports a message the alert can show.
- (void)showFilterDialog:(id)sender {
    (void)sender;
    if (g_plan == nullptr || g_plan->sheet == nullptr) {
        return;
    }
    PlanState& plan = *g_plan;
    const std::vector<app::FilterField> fields = app::filter_fields(*plan.sheet);
    const std::vector<app::FilterOperatorChoice>& operators = app::filter_operators();
    if (fields.empty()) {
        show_alert(@"Filter", @"The sheet has no column to filter on.");
        return;
    }

    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Filter Tasks"];
    [alert setInformativeText:@"Show only the tasks that match the criterion."];
    [alert addButtonWithTitle:@"Apply"];
    [alert addButtonWithTitle:@"Clear"];
    [alert addButtonWithTitle:@"Cancel"];

    NSView* accessory = [[NSView alloc] initWithFrame:NSMakeRect(0.0, 0.0, 430.0, 96.0)];

    NSTextField* column_label =
        [[NSTextField alloc] initWithFrame:NSMakeRect(0.0, 62.0, 80.0, 20.0)];
    [column_label setStringValue:@"Column"];
    [column_label setBezeled:NO];
    [column_label setDrawsBackground:NO];
    [column_label setEditable:NO];
    [column_label setSelectable:NO];
    [accessory addSubview:column_label];

    NSPopUpButton* field_popup = [[NSPopUpButton alloc]
        initWithFrame:NSMakeRect(86.0, 57.0, 340.0, 26.0)
            pullsDown:NO];
    for (const app::FilterField& field : fields) {
        [field_popup addItemWithTitle:to_ns(field.title)];
    }
    [accessory addSubview:field_popup];

    NSPopUpButton* operator_popup = [[NSPopUpButton alloc]
        initWithFrame:NSMakeRect(86.0, 28.0, 340.0, 26.0)
            pullsDown:NO];
    for (const app::FilterOperatorChoice& choice : operators) {
        [operator_popup addItemWithTitle:to_ns(choice.title)];
    }
    [accessory addSubview:operator_popup];

    NSTextField* value_field =
        [[NSTextField alloc] initWithFrame:NSMakeRect(86.0, 0.0, 340.0, 24.0)];
    [value_field setPlaceholderString:@"value: text, 2026-10-20, 3d ..."];
    [accessory addSubview:value_field];

    [alert setAccessoryView:accessory];
    const NSModalResponse response = [alert runModal];
    if (response == NSAlertSecondButtonReturn) {
        [self clearFilterAction:nil];
        return;
    }
    if (response != NSAlertFirstButtonReturn) {
        note(plan, "filter cancelled", false);
        [g_view setNeedsDisplay:YES];
        return;
    }

    const NSInteger field_index = [field_popup indexOfSelectedItem];
    const NSInteger operator_index = [operator_popup indexOfSelectedItem];
    if (field_index < 0 || operator_index < 0) {
        note(plan, "filter cancelled: nothing selected", false);
        [g_view setNeedsDisplay:YES];
        return;
    }
    app::FilterRule rule;
    rule.field_index = static_cast<std::size_t>(field_index);
    rule.operator_index = static_cast<std::size_t>(operator_index);
    rule.value = to_std([value_field stringValue]);
    const app::FilterBuild build = app::build_filter(fields, operators, {rule}, plan.format);
    if (!build.ok) {
        show_alert(@"Filter not applied", to_ns(build.error));
        return;
    }
    plan.filter_active = true;
    plan.filter = build.definition;
    plan.filter_description = build.description;
    plan.filter_rows = app::filter_rows(build.definition, plan.records);
    [g_view refreshAfterChange];
    if (plan.filter_rows.empty()) {
        note(plan, "filter matched no task: " + build.description, true);
    } else {
        note(plan, std::to_string(plan.filter_rows.size()) + " of " +
                       std::to_string(plan.records.row_count()) + " task(s) shown",
             false);
    }
    std::printf("msproject-app: filter: %s\n", build.description.c_str());
    std::fflush(stdout);
    [g_view setNeedsDisplay:YES];
}

- (void)clearFilterAction:(id)sender {
    (void)sender;
    if (g_plan == nullptr) {
        return;
    }
    clear_filter(*g_plan);
    [g_view refreshAfterChange];
}

// The dirty-state prompt.  Start-date edits and pending Insert/Delete Task edits
// are what a save writes; the prompt names both and says the view-only edits will
// be lost rather than implying they could be saved.
- (BOOL)confirmClose {
    if (g_plan == nullptr || g_plan->sheet == nullptr) {
        g_close_confirmed = YES;
        return YES;
    }
    if (g_plan->sheet->editing()) {
        g_plan->sheet->cancel_edit();
    }
    const std::vector<app::StartEdit> edits =
        app::collect_start_edits(g_plan->records, g_plan->loaded);
    const std::size_t structural =
        g_plan->edits.inserted_count() + g_plan->edits.deleted_count();
    const std::size_t fields = g_plan->edits.field_edit_count();
    const std::size_t links = g_plan->edits.link_edit_count();
    // Wave 16: the pending table date format and custom-field formula are file
    // changes too, so the prompt must mention them instead of implying Save would
    // only write the start dates.
    const bool document_edits =
        g_plan->document != nullptr && g_plan->document->has_pending_edits();
    if (edits.empty() && structural == 0 && fields == 0 && links == 0 && !document_edits) {
        g_close_confirmed = YES;
        return YES;
    }
    std::string details;
    if (!edits.empty()) {
        details += std::to_string(edits.size()) + " task start date(s) were edited";
    }
    if (structural != 0) {
        if (!details.empty()) {
            details += " and ";
        }
        details += std::to_string(structural) + " task insert/delete edit(s) are pending";
    }
    if (fields != 0) {
        if (!details.empty()) {
            details += " and ";
        }
        details += std::to_string(fields) + " custom-field edit(s) are pending";
    }
    if (links != 0) {
        if (!details.empty()) {
            details += " and ";
        }
        details += std::to_string(links) + " link edit(s) are pending";
    }
    if (document_edits) {
        if (!details.empty()) {
            details += " and ";
        }
        details += "a table date-format or custom-field formula edit is pending";
    }
    details +=
        ". Saving writes them to a .mpp; every other edit (name, finish, duration, "
        "percent) is view-only and will be lost.";
    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Save changes before closing?"];
    [alert setInformativeText:to_ns(details)];
    [alert setAlertStyle:NSAlertStyleWarning];
    [alert addButtonWithTitle:@"Save"];
    [alert addButtonWithTitle:@"Don't Save"];
    [alert addButtonWithTitle:@"Cancel"];
    const NSModalResponse response = [alert runModal];
    if (response == NSAlertFirstButtonReturn) {
        if (![self saveDocumentWithPanel:NO]) {
            return NO;  // a cancelled panel or a writer failure keeps the window open
        }
        g_close_confirmed = YES;
        return YES;
    }
    if (response == NSAlertSecondButtonReturn) {
        g_close_confirmed = YES;
        return YES;
    }
    return NO;
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
    if (sender != g_window) {
        return YES;
    }
    return [self confirmClose];
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    (void)sender;
    if (g_close_confirmed) {
        return NSTerminateNow;
    }
    return [self confirmClose] ? NSTerminateNow : NSTerminateCancel;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

@end

int main(int argc, char** argv) {
    @autoreleasepool {
        g_argc = argc;
        g_argv = argv;

        // Wave 15: `--smoke <fixture.mpp> --out <dir>`.  Parsed here, before the
        // application starts, so every later code path can see g_smoke.  Only the
        // two flags are interpreted; anything else is left to the existing argv[1]
        // fixture rule (choose_fixture()).
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index] != nullptr ? argv[index] : "";
            if (argument == "--smoke" && index + 1 < argc && argv[index + 1] != nullptr) {
                g_smoke = true;
                g_smoke_fixture = argv[++index];
            } else if (argument == "--out" && index + 1 < argc && argv[index + 1] != nullptr) {
                g_smoke_out = argv[++index];
            }
        }
        if (g_smoke && (g_smoke_fixture.empty() || g_smoke_out.empty())) {
            std::fprintf(stderr,
                         "msproject-app: --smoke needs a fixture and an output directory\n"
                         "  usage: msproject_app --smoke <fixture.mpp> --out <dir>\n");
            return 2;
        }

        NSApplication* application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        load_preferences_from_defaults();
        g_delegate = [[AppDelegate alloc] init];
        [application setDelegate:g_delegate];
        [application run];
    }
    return 0;
}
