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
//     asking for a path the first time; Cmd-Shift-S always asks.  Closing or
//     quitting with pending start edits prompts Save / Don't Save / Cancel.
//   * Cmd-F filters the displayed rows (app/filter_model.h); Cmd-Shift-F clears it
//   * Cmd-1/2/3 and Cmd-=/Cmd-- set the Gantt timescale; Cmd-Shift-D toggles the
//     working-day shading; both persist in NSUserDefaults (app/prefs.h)
//   * Cmd-Shift-E / Cmd-Option-E export a PDF sheet / Gantt report
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
//   No cut, no row insert/delete, no indent/outdent, no column resize/reorder, no
//   multiple windows, no IME/composition in the in-cell editor (see README).
//
// BUILD.  This file is compiled only on APPLE (app/CMakeLists.txt).  It is
// Objective-C++20 under ARC.  AppKit APIs used here are non-deprecated as of the
// macOS 14/15 SDKs so the CI job can build with -Werror; anything that needs an
// availability check (the panels' content types) is guarded with @available.

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "date.h"
#include "document.h"
#include "export_pdf.h"
#include "filter_model.h"
#include "gantt.h"
#include "layout_model.h"
#include "menu_model.h"
#include "prefs.h"
#include "reader_adapter.h"
#include "selection_model.h"
#include "sheet.h"

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

// Defined further down (with the rest of the sheet geometry); ensure_selection_visible
// below runs before that point, so it needs the declaration here.
std::size_t name_column_of(const PlanState& plan);

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
    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:title];
    [alert setInformativeText:information];
    [alert setAlertStyle:NSAlertStyleWarning];
    [alert addButtonWithTitle:@"OK"];
    [alert runModal];
}

// ---------------------------------------------------------------------------
// Load and rebuild: .mpp -> PlanState -> the two views
// ---------------------------------------------------------------------------

std::string make_status_text(const PlanState& plan) {
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

// Rebuilds everything derived from the RecordSet: the visible-row map (which a
// filter restricts), the sheet geometry, the Gantt (from the VISIBLE rows, which
// is how collapse and filtering work), the status line and the window title.
void rebuild_view(PlanState& plan) {
    if (plan.sheet == nullptr) {
        return;
    }
    plan.view = app::sync_plan_from_records(plan.loaded, plan.records);
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

    core::gantt::GanttSource source = app::make_gantt_source_for_rows(
        plan.view, plan.row_map.rows, {}, app::today_date32());
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

bool load_plan(PlanState& plan, const std::string& path) {
    plan = PlanState{};
    plan.path = path;
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
        plan.records = app::make_record_set(plan.loaded);
        plan.sheet = std::make_unique<core::sheet::SheetModel>(plan.records,
                                                               app::default_sheet_columns());
        plan.sheet->set_format(plan.format);
        app::apply_edit_policy(*plan.sheet);

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
        plan.message = "loaded " + std::to_string(plan.loaded.tasks.size()) + " task(s)";
        plan.message_is_error = false;
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
    return std::max(plan.sheet_width + kSheetDivider + kGanttLeftPad + plan.gantt_width, 800.0);
}

double content_height(const PlanState& plan) {
    const std::size_t rows = plan.sheet != nullptr ? plan.row_map.size() : 0;
    const double body = static_cast<double>(rows) * kRowHeight;
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
    const double row_count = static_cast<double>(plan.row_map.size());
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
    note(plan, "edited " + column_title, false);
    return true;
}

bool begin_edit_at(PlanState& plan, std::size_t row, std::size_t column) {
    // commit_open_edit() has already run if an edit was open.
    if (!plan.sheet->begin_edit(row, column)) {
        note(plan, "cell is read-only", true);
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
    return plan.sheet != nullptr &&
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
    state.editing = g_plan->sheet->editing();
    state.has_selection = g_plan->sheet->has_selection();
    state.can_undo = g_plan->sheet->can_undo();
    state.can_redo = g_plan->sheet->can_redo();
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
        for (NSMenuItem* item in [submenu itemArray]) {
            if ([item action] == @selector(performMenuAction:)) {
                [item setEnabled:app::menu_item_enabled(
                                     static_cast<app::MenuAction>([item tag]), state)];
            }
        }
    }
    if (g_columns_menu != nil) {
        for (NSMenuItem* item in [g_columns_menu itemArray]) {
            const NSInteger column = [item tag];
            bool known = false;
            bool visible = false;
            if (g_plan != nullptr && g_plan->sheet != nullptr && column >= 0 &&
                static_cast<std::size_t>(column) < g_plan->sheet->column_count()) {
                visible = g_plan->sheet->column_visible(static_cast<std::size_t>(column));
                known = true;
            }
            [item setState:(known && visible) ? NSControlStateValueOn : NSControlStateValueOff];
            [item setEnabled:known];
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
    const core::sheet::PasteResult result = plan.sheet->paste(block);
    if (!result.ok) {
        note(plan, "paste refused: " + result.error, true);
        return;
    }
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
    const std::size_t shown =
        plan.filter_active ? plan.filter_rows.size() : plan.records.row_count();
    request.subtitle =
        app::export_subtitle(kind, plan.filter_active ? plan.filter_description : std::string(),
                             shown, plan.records.row_count());
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

void show_about() {
    show_alert(@"About MS Project Shell",
               @"A native macOS shell over the portable MS Project core.\n\n"
                @"Open a .mpp, browse the sheet and Gantt, edit cells, filter the "
                @"task list, and save edited task start dates with File > Save. "
                @"Start dates are the only edit the file writer persists; every "
                @"other edit stays in memory (see the README).\n\n"
                @"Version 0.3 (wave 10, stream W46).");
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
                @"Cmd-= / Cmd--: zoom in / out\n"
                @"Cmd-S / Cmd-Shift-S: save / save as\n"
                @"Cmd-Shift-E / Cmd-Option-E: export PDF (sheet / Gantt)");
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
- (void)toggleColumn:(id)sender;
- (void)showPreferences:(id)sender;
- (void)preferencesChanged:(id)sender;
- (void)showFilterDialog:(id)sender;
- (void)clearFilterAction:(id)sender;
- (void)exportSheetReport:(id)sender;
- (void)exportGanttReport:(id)sender;
- (void)cut:(id)sender;
- (BOOL)confirmClose;
@end

static AppDelegate* g_delegate = nil;
static PlanView* g_view = nil;

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
        for (const app::MenuItemSpec& item_spec : spec.items) {
            if (item_spec.separator_before) {
                [submenu addItem:[NSMenuItem separatorItem]];
            }
            NSMenuItem* item =
                [[NSMenuItem alloc] initWithTitle:to_ns(item_spec.title)
                                           action:@selector(performMenuAction:)
                                    keyEquivalent:to_ns(item_spec.key)];
            [item setTarget:self];
            [item setTag:static_cast<NSInteger>(item_spec.action)];
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
            [submenu addItem:item];
        }
        if (std::string(spec.title) == "View") {
            // The Columns submenu is document-shaped, so it is built here rather
            // than in the static menu model.  Every load uses the same default
            // columns, so the model column index is a stable tag.
            [submenu addItem:[NSMenuItem separatorItem]];
            NSMenuItem* columns_item = [[NSMenuItem alloc] initWithTitle:@"Columns"
                                                                  action:nil
                                                           keyEquivalent:@""];
            g_columns_menu = [[NSMenu alloc] initWithTitle:@"Columns"];
            [g_columns_menu setAutoenablesItems:NO];
            const std::vector<core::sheet::SheetColumn> columns = app::default_sheet_columns();
            for (std::size_t index = 0; index < columns.size(); ++index) {
                NSMenuItem* column_item =
                    [[NSMenuItem alloc] initWithTitle:to_ns(columns[index].title)
                                               action:@selector(toggleColumn:)
                                        keyEquivalent:@""];
                [column_item setTarget:self];
                [column_item setTag:static_cast<NSInteger>(index)];
                [g_columns_menu addItem:column_item];
            }
            [columns_item setSubmenu:g_columns_menu];
            [submenu addItem:columns_item];
        }
        [top setSubmenu:submenu];
        [mainMenu addItem:top];
    }

    [NSApp setMainMenu:mainMenu];
    refresh_menu_bar();
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
        case app::MenuAction::insert_task:
            show_alert(@"Insert Task",
                       to_ns(std::string(app::menu_disabled_reason(app::MenuAction::insert_task))));
            return;
        case app::MenuAction::insert_resource:
            show_alert(@"Insert Resource",
                       to_ns(std::string(
                           app::menu_disabled_reason(app::MenuAction::insert_resource))));
            return;
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
        std::printf("msproject-app: %s: %zu tasks, %zu rows, %zu links, %zu bars, %zu warnings\n",
                    path.c_str(), g_plan->loaded.tasks.size(), g_plan->sheet->row_count(),
                    g_plan->loaded.links.size(), g_plan->gantt.bars.size(), g_plan->warnings);
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
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:frame];
    [scroll setHasVerticalScroller:YES];
    [scroll setHasHorizontalScroller:YES];
    [scroll setAutohidesScrollers:YES];

    g_view = [[PlanView alloc] initWithFrame:NSMakeRect(0.0, 0.0, width, height)];
    [scroll setDocumentView:g_view];

    [g_window setContentView:scroll];
    [g_window makeKeyAndOrderFront:nil];
    [g_window makeFirstResponder:g_view];

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
    if (edits.empty()) {
        note(plan, "nothing to save: no task start has been edited", false);
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

    const app::SaveOutcome outcome = app::save_start_edits(edits, plan.path, target);
    if (!outcome.ok) {
        note(plan, "save failed: " + outcome.error, true);
        [g_view refreshAfterChange];
        show_alert(@"Could not save the edited starts",
                   to_ns(outcome.error + "\n\nThe opened file was not modified."));
        return NO;
    }

    app::accept_start_edits(plan.loaded, edits);
    g_target_path = outcome.path;
    std::string message = "saved " + std::to_string(outcome.rows_written) + " start(s) to " +
                          outcome.path + " (" + outcome.api + ")";
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
    if (g_plan == nullptr || g_plan->sheet == nullptr) {
        return;
    }
    if (g_plan->sheet->editing()) {
        g_plan->sheet->cancel_edit();
    }
    if (g_plan->sheet->undo()) {
        note(*g_plan, "undo", false);
    } else {
        note(*g_plan, "nothing to undo", false);
    }
    [g_view refreshAfterChange];
}

- (void)redo:(id)sender {
    (void)sender;
    if (g_plan == nullptr || g_plan->sheet == nullptr) {
        return;
    }
    if (g_plan->sheet->editing()) {
        g_plan->sheet->cancel_edit();
    }
    if (g_plan->sheet->redo()) {
        note(*g_plan, "redo", false);
    } else {
        note(*g_plan, "nothing to redo", false);
    }
    [g_view refreshAfterChange];
}

- (void)copy:(id)sender {
    (void)sender;
    if (g_plan == nullptr) {
        return;
    }
    copy_selection(*g_plan);
    [g_view setNeedsDisplay:YES];
}

- (void)paste:(id)sender {
    (void)sender;
    if (g_plan == nullptr) {
        return;
    }
    paste_from_pasteboard(*g_plan);
    [g_view refreshAfterChange];
}

- (void)selectAll:(id)sender {
    (void)sender;
    if (g_plan == nullptr || g_plan->sheet == nullptr || g_plan->sheet->editing()) {
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

// The dirty-state prompt.  Only START-DATE edits are persistable, so only they
// make the document dirty; the prompt says so rather than implying that the
// view-only edits could be saved.
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
    if (edits.empty()) {
        g_close_confirmed = YES;
        return YES;
    }
    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Save changes before closing?"];
    [alert setInformativeText:to_ns(
        std::to_string(edits.size()) +
        " task start date(s) were edited. Only start dates are written to a .mpp; "
        "every other edit (name, finish, duration, percent) is view-only and will be "
        "lost.")];
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
        NSApplication* application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        load_preferences_from_defaults();
        g_delegate = [[AppDelegate alloc] init];
        [application setDelegate:g_delegate];
        [application run];
    }
    return 0;
}
