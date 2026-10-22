// app/main.mm -- the macOS shell v2: one window, one scroll view, a real sheet
// on the left and a real Gantt on the right, both fed by the portable core, with
// open/save, two-way selection, in-cell editing, undo/redo and outline collapse.
// Objective-C++ / AppKit, deliberately thin: everything that can be plain C++
// lives in layout_model.cpp (mapping), selection_model.cpp (selection/collapse
// geometry) and document.cpp (the save path), each proved by a headless test on
// Windows as well as macOS.  This file is the only ObjC++ in the repository.
//
// WHAT RUNS
//   main() -> NSApplication -> AppDelegate -> loads a .mpp: argv[1] if given,
//   else the first fixture under corpus/probe (git-ignored probes), else
//   tests/data/synthetic-base.mpp.  File > Open (Cmd-O) replaces it.  The load
//   builds app::PlanInput -> RecordSet + SheetModel + RowMap -> GanttSource ->
//   build_gantt(), then shows a PlanView (the document view) in one NSScrollView.
//
// INTERACTION
//   * sheet click selects a cell; shift-click extends; double-click edits
//   * a click on a summary row's disclosure box (Name column, left gutter)
//     collapses/expands it; so do Cmd-Left / Cmd-Right
//   * typing a printable character starts an edit (replacing the cell text, as
//     Project does); Return commits; Esc cancels; Tab commits and moves right;
//     Backspace edits the text, and with no edit open it clears the cell first
//   * arrows move the selection through the VISIBLE rows (app::row_after)
//   * Cmd-Z / Cmd-Shift-Z undo/redo through SheetModel (menu items)
//   * clicking a Gantt bar (or a dependency) selects the sheet row; selecting a
//     sheet row highlights its bar and scrolls it into view.  SheetModel is the
//     single source of truth for the selection -- see selection_model.h
//   * Cmd-S asks for a path and writes the edited task starts (document.h)
//
// DRAWING (PlanView, flipped so y grows downwards like core::gantt)
//   left:  sheet header, row text through SheetModel, summary band, critical red,
//          outline indent + disclosure boxes, selection highlight, and the
//          in-progress edit text with a caret.
//   right: two header tiers (months over days), weekend shading, one bar per
//          visible row (normal/summary/milestone), dependency polylines with
//          arrowheads, the selected bar outlined, the status-date marker.
//   footer: two lines -- counts + the plan span + the file, then the last action
//          (red when it was a rejected edit or a failed save).
//
// WHAT IT STILL DOES NOT DO (see app/README.md)
//   No clipboard, no column resize/reorder, no row insert/delete, no zoom, no
//   printing, no real project calendar (weekends only), no multiple windows, no
//   IME/composition in the in-cell editor (see README).
//
// BUILD.  This file is compiled only on APPLE (app/CMakeLists.txt).  It is
// Objective-C++20 under ARC.  AppKit APIs used here are non-deprecated as of the
// macOS 14/15 SDKs so the CI job can build with -Werror; anything that needs an
// availability check (NSOpenPanel's content types) is guarded with @available.

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
#include "gantt.h"
#include "layout_model.h"
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
constexpr double kPixelsPerDay = 12.0;
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
};

PlanState* g_plan = nullptr;
NSWindow* g_window = nil;
int g_argc = 0;
char** g_argv = nullptr;

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
    text += "  |  " + std::to_string(plan.warnings) + " reader warnings";
    if (!plan.path.empty()) {
        text += "  |  " + plan.path;
    }
    return text;
}

double gantt_origin_x(const PlanState& plan) {
    return plan.sheet_width + kSheetDivider + kGanttLeftPad;
}

// Rebuilds everything derived from the RecordSet: the visible-row map, the sheet
// geometry, the Gantt (from the VISIBLE rows, which is how collapse works), the
// status line and the window title.
void rebuild_view(PlanState& plan) {
    if (plan.sheet == nullptr) {
        return;
    }
    plan.view = app::sync_plan_from_records(plan.loaded, plan.records);
    plan.row_map = app::build_row_map(*plan.sheet);
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
    plan.gantt = core::gantt::build_gantt(source, style,
                                          app::timescale_for_plan(plan.view, kPixelsPerDay));
    plan.gantt_width = std::max(plan.gantt.content_width + kRightMargin, kMinGanttWidth);
    plan.status_text = make_status_text(plan);
}

bool load_plan(PlanState& plan, const std::string& path) {
    plan = PlanState{};
    plan.path = path;
    try {
        const mpp::Document document = mpp::load_file(path);
        plan.warnings = document.warnings.size();
        plan.loaded = app::plan_input_from_document(document);
        plan.loaded.source_name = std::filesystem::path(path).filename().string();
        plan.skipped_links = plan.loaded.skipped_links;

        plan.format = core::sheet::SheetFormat{};
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
    const std::size_t rows = plan.sheet != nullptr ? plan.sheet->visible_row_count() : 0;
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
    const std::size_t row_count = plan.sheet->visible_row_count();
    const double body_bottom = body_top + static_cast<double>(row_count) * kRowHeight;
    const app::SelectedTask selected = app::selected_task(*plan.sheet, plan.row_map);

    if (row_count > 0) {
        // Summary rows get the light band MS Project draws them with.
        for (std::size_t ordinal = 0; ordinal < row_count; ++ordinal) {
            const std::size_t row = plan.sheet->visible_row(ordinal);
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
            const std::size_t row = plan.sheet->visible_row(ordinal);
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
        const std::size_t row = plan.sheet->visible_row(ordinal);
        if (row == core::sheet::kNoIndex) {
            break;
        }
        const double y = body_top + static_cast<double>(ordinal) * kRowHeight;
        for (const core::sheet::ColumnSegment& segment : plan.columns) {
            const bool is_name = segment.column == name_column;
            const bool editing_here = plan.sheet->editing() &&
                                      plan.sheet->edit_target().row == row &&
                                      plan.sheet->edit_target().column == segment.column;
            const std::string text =
                editing_here ? plan.sheet->edit_text() : plan.sheet->cell_text(row, segment.column);
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
                NSColor* status_color, NSColor* selected_bg, NSColor* focus_color) {
    const double gantt_x = gantt_origin_x(plan);
    const double gantt_width = plan.gantt_width;
    const double body_top = kHeaderHeight;
    const std::size_t row_count = plan.sheet->visible_row_count();
    const double body_bottom = body_top + static_cast<double>(row_count) * kRowHeight;
    const app::SelectedTask selected = app::selected_task(*plan.sheet, plan.row_map);

    const std::vector<core::gantt::HeaderCell> major =
        plan.gantt.header(core::gantt::HeaderTier::major, 0.0, gantt_width);
    const std::vector<core::gantt::HeaderCell> minor =
        plan.gantt.header(core::gantt::HeaderTier::minor, 0.0, gantt_width);

    // Non-working shading over the row band (weekends; holidays when supplied).
    for (const core::gantt::NonworkingSpan& span : plan.gantt.nonworking_spans(0.0, gantt_width)) {
        fill_rect(NSMakeRect(gantt_x + span.x, body_top, span.width, body_bottom - body_top),
                  weekend_bg);
    }

    // The selected row's band, so the sheet and the chart agree at a glance.
    if (selected.has_selection && selected.ordinal != core::sheet::kNoIndex) {
        fill_rect(NSMakeRect(gantt_x, body_top + static_cast<double>(selected.ordinal) * kRowHeight,
                             gantt_width, kRowHeight),
                  selected_bg);
    }

    // Header: band, weekend day cells, tier separators, labels.
    fill_rect(NSMakeRect(gantt_x, 0.0, gantt_width, kHeaderHeight), header_bg);
    for (const core::gantt::HeaderCell& cell : minor) {
        if (cell.weekday == 0 || cell.weekday == 6) {
            fill_rect(NSMakeRect(gantt_x + cell.x, kHeaderTierHeight, cell.width,
                                 kHeaderTierHeight),
                      weekend_bg);
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
    const double row_count = static_cast<double>(plan.sheet->visible_row_count());
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
               link_color, status_color, selected_bg, focus_color);
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
    return plan.sheet->visible_row(static_cast<std::size_t>(ordinal));
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
// an undo, a collapse), resize the document to the new content, and redraw.
- (void)refreshAfterChange {
    if (g_plan != nullptr && g_plan->sheet != nullptr) {
        rebuild_view(*g_plan);
        [self setFrameSize:NSMakeSize(content_width(*g_plan), content_height(*g_plan))];
        const bool edited = !app::collect_start_edits(g_plan->records, g_plan->loaded).empty();
        [g_window setDocumentEdited:edited];
    }
    [self selectionChanged];
}

// Redraw and scroll the selected sheet row into view.
- (void)selectionChanged {
    [self setNeedsDisplay:YES];
    if (g_plan == nullptr || g_plan->sheet == nullptr) {
        return;
    }
    const app::SelectedTask selected = app::selected_task(*g_plan->sheet, g_plan->row_map);
    if (!selected.has_selection) {
        return;
    }
    const app::ScrollTarget target =
        app::scroll_target(*g_plan->sheet, g_plan->metrics, selected.row);
    if (!target.valid) {
        return;
    }
    const NSRect visible = [self visibleRect];
    [self scrollRectToVisible:NSMakeRect(visible.origin.x, target.y, visible.size.width,
                                         target.height)];
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

@interface AppDelegate : NSObject <NSApplicationDelegate>
- (void)installMenu;
- (void)loadPath:(const std::string&)path;
- (void)updateWindowTitle:(const std::string&)path title:(const std::string&)title;
- (void)openDocument:(id)sender;
- (void)saveDocument:(id)sender;
- (void)undo:(id)sender;
- (void)redo:(id)sender;
@end

static AppDelegate* g_delegate = nil;
static PlanView* g_view = nil;

@implementation AppDelegate

- (void)installMenu {
    NSMenu* mainMenu = [[NSMenu alloc] initWithTitle:@"MainMenu"];

    NSMenuItem* appItem = [[NSMenuItem alloc] initWithTitle:@"MS Project Shell"
                                                    action:nil
                                             keyEquivalent:@""];
    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"MS Project Shell"];
    [appMenu addItemWithTitle:@"Hide MS Project Shell" action:@selector(hide:) keyEquivalent:@"h"];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Quit MS Project Shell"
                       action:@selector(terminate:)
                keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [mainMenu addItem:appItem];

    NSMenuItem* fileItem = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
    NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    NSMenuItem* openItem = [[NSMenuItem alloc] initWithTitle:@"Open..."
                                                     action:@selector(openDocument:)
                                              keyEquivalent:@"o"];
    [openItem setTarget:self];
    [fileMenu addItem:openItem];
    NSMenuItem* saveItem = [[NSMenuItem alloc] initWithTitle:@"Save Edited Starts..."
                                                     action:@selector(saveDocument:)
                                              keyEquivalent:@"s"];
    [saveItem setTarget:self];
    [fileMenu addItem:saveItem];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    [fileMenu addItemWithTitle:@"Close" action:@selector(performClose:) keyEquivalent:@"w"];
    [fileItem setSubmenu:fileMenu];
    [mainMenu addItem:fileItem];

    NSMenuItem* editItem = [[NSMenuItem alloc] initWithTitle:@"Edit" action:nil keyEquivalent:@""];
    NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    NSMenuItem* undoItem = [[NSMenuItem alloc] initWithTitle:@"Undo"
                                                     action:@selector(undo:)
                                              keyEquivalent:@"z"];
    [undoItem setTarget:self];
    [editMenu addItem:undoItem];
    NSMenuItem* redoItem = [[NSMenuItem alloc] initWithTitle:@"Redo"
                                                     action:@selector(redo:)
                                              keyEquivalent:@"z"];
    [redoItem setTarget:self];
    [redoItem setKeyEquivalentModifierMask:(NSEventModifierFlagCommand |
                                            NSEventModifierFlagShift)];
    [editMenu addItem:redoItem];
    [editItem setSubmenu:editMenu];
    [mainMenu addItem:editItem];

    [NSApp setMainMenu:mainMenu];
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

- (void)saveDocument:(id)sender {
    (void)sender;
    if (g_plan == nullptr || g_plan->sheet == nullptr) {
        return;
    }
    PlanState& plan = *g_plan;
    const std::vector<app::StartEdit> edits =
        app::collect_start_edits(plan.records, plan.loaded);
    if (edits.empty()) {
        note(plan, "nothing to save: no task start has been edited", false);
        [g_view setNeedsDisplay:YES];
        return;
    }

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
        return;
    }
    NSURL* url = [panel URL];
    if (url == nil) {
        return;
    }
    const std::string target = to_std([url path]);
    const app::SaveOutcome outcome = app::save_start_edits(edits, plan.path, target);
    if (!outcome.ok) {
        note(plan, "save failed: " + outcome.error, true);
        [g_view refreshAfterChange];
        show_alert(@"Could not save the edited starts",
                   to_ns(outcome.error + "\n\nThe opened file was not modified."));
        return;
    }

    app::accept_start_edits(plan.loaded, edits);
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
        g_delegate = [[AppDelegate alloc] init];
        [application setDelegate:g_delegate];
        [application run];
    }
    return 0;
}
