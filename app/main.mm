// app/main.mm -- the first macOS shell: one window, one scroll view, a real
// sheet on the left and a real Gantt on the right, both fed by the portable
// core.  Objective-C++ / AppKit, deliberately small: everything that can be
// plain C++ lives in layout_model.cpp (mapping) and in the core.
//
// WHAT RUNS
//   main() -> NSApplication -> AppDelegate -> loads a .mpp (argv[1], else a
//   fixture under corpus/probe, else tests/data/synthetic-base.mpp), builds
//   app::PlanInput -> RecordSet + SheetModel + GanttSource -> build_gantt(),
//   then puts a PlanView (the document view) inside one NSScrollView.
//
// DRAWING (PlanView, flipped so y grows downwards like core::gantt)
//   left:  sheet header (UID / ID / Outline / Name / Start / Finish / Duration),
//          row text through core::sheet::SheetModel (so date and duration text
//          are the model's, not ours), summary rows bold, critical rows red,
//          child rows indented, grid lines.
//   right: two header tiers (months over days), weekend/holiday shading, one
//          bar per task (normal fill by percent, summary bar, milestone
//          diamond), dependency polylines + arrowheads, the status-date marker,
//          and bar labels measured with a real font metric
//          (GanttStyle::measure_text).
//   footer: a status line with counts, the project date span, pixels per day,
//          the reader's warning count and the file name.
//
// WHAT IT DOES NOT DO (see app/README.md)
//   No editing, no selection, no scrolling sync split, no collapse/expand, no
//   real project calendar (weekends only, no file holidays), no zoom, no
//   printing.
//
// BUILD.  This file is compiled only on APPLE (app/CMakeLists.txt).  It is
// Objective-C++20 under ARC.  AppKit APIs used here are non-deprecated as of the
// macOS 14/15 SDKs so the CI job can build with -Werror.

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "date.h"
#include "gantt.h"
#include "layout_model.h"
#include "reader_adapter.h"
#include "sheet.h"

namespace {

// Geometry.  kRowHeight must equal default_sheet_metrics().row_height (the
// headless test asserts that they match); kHeaderHeight is the two Gantt header
// tiers and is also the sheet header band height.
constexpr double kRowHeight = 20.0;
constexpr double kHeaderHeight = 32.0;
constexpr double kHeaderTierHeight = 16.0;
constexpr double kPixelsPerDay = 12.0;
constexpr double kFooterHeight = 28.0;
constexpr double kSheetDivider = 1.0;
constexpr double kGanttLeftPad = 10.0;
constexpr double kRightMargin = 180.0;
constexpr double kMinGanttWidth = 480.0;
constexpr double kBottomPadding = 12.0;
constexpr double kLabelFontSize = 9.0;
constexpr double kTextInset = 3.0;

// Everything the window draws from, in one place.  Heap-allocated (see main) and
// intentionally never freed: the process exits right after the run loop.
struct PlanState {
    app::PlanInput input;
    core::view::RecordSet records{core::model::RecordType::Task};
    std::unique_ptr<core::sheet::SheetModel> sheet;
    core::sheet::SheetMetrics metrics;
    core::sheet::SheetFormat format;
    std::vector<core::sheet::ColumnSegment> columns;
    core::gantt::GanttLayout gantt;
    double sheet_width = 0.0;
    double gantt_width = 0.0;
    std::string path;
    std::string status_text;
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

// ---------------------------------------------------------------------------
// Load: .mpp -> PlanState
// ---------------------------------------------------------------------------

std::string make_status_text(const PlanState& plan) {
    const app::PlanSummary summary = app::summarize(plan.input);
    std::string text = std::to_string(summary.tasks) + " tasks, " +
                       std::to_string(summary.links) + " links";
    if (plan.skipped_links != 0) {
        text += " (" + std::to_string(plan.skipped_links) + " skipped)";
    }
    text += ", " + std::to_string(summary.summaries) + " summaries, " +
            std::to_string(summary.milestones) + " milestones, " +
            std::to_string(summary.critical) + " critical";
    if (!core::sched::is_na(summary.first_start) && !core::sched::is_na(summary.last_finish)) {
        text += "  |  " + core::sheet::format_date_text(
                               app::date_value_from_date32(summary.first_start), plan.format);
        text += " - " + core::sheet::format_date_text(
                            app::date_value_from_date32(summary.last_finish), plan.format);
    }
    text += "  |  12 px/day  |  " + std::to_string(plan.warnings) + " reader warnings";
    if (!plan.input.source_name.empty()) {
        text += "  |  " + plan.input.source_name;
    }
    if (!plan.input.title.empty()) {
        text += "  |  " + plan.input.title;
    }
    return text;
}

bool load_plan(PlanState& plan, const std::string& path) {
    plan = PlanState{};
    plan.path = path;
    try {
        const mpp::Document document = mpp::load_file(path);
        plan.warnings = document.warnings.size();
        plan.input = app::plan_input_from_document(document);
        plan.input.source_name = std::filesystem::path(path).filename().string();
        plan.skipped_links = plan.input.skipped_links;

        plan.format = core::sheet::SheetFormat{};
        plan.metrics = app::default_sheet_metrics();
        plan.records = app::make_record_set(plan.input);
        plan.sheet = std::make_unique<core::sheet::SheetModel>(plan.records,
                                                               app::default_sheet_columns());
        plan.sheet->set_format(plan.format);
        plan.columns = plan.sheet->layout_columns(plan.metrics, 0);
        plan.sheet_width = static_cast<double>(plan.sheet->sheet_width_points(plan.metrics, 0));

        core::gantt::GanttSource source =
            app::make_gantt_source(plan.input, {}, app::today_date32());
        core::gantt::GanttStyle style = app::default_gantt_style();
        // A real font metric, so "does this label fit inside the bar?" is decided
        // by the font actually drawn (gantt.h leaves this to the shell).
        style.measure_text = [](std::string_view text) -> double {
            NSString* string = [[NSString alloc] initWithBytes:text.data()
                                                       length:text.size()
                                                     encoding:NSUTF8StringEncoding];
            if (string == nil) {
                return static_cast<double>(text.size()) * 7.0;
            }
            const NSSize size =
                [string sizeWithAttributes:@{NSFontAttributeName : [NSFont systemFontOfSize:
                                                                       kLabelFontSize]}];
            return static_cast<double>(size.width);
        };
        plan.gantt = core::gantt::build_gantt(source, style,
                                              app::timescale_for_plan(plan.input, kPixelsPerDay));
        plan.gantt_width = std::max(plan.gantt.content_width + kRightMargin, kMinGanttWidth);
        plan.status_text = make_status_text(plan);
        return true;
    } catch (const std::exception& error) {
        plan.error = error.what();
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

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void draw_sheet(PlanState& plan, NSRect bounds, NSFont* body, NSFont* bold, NSFont* header_font,
                NSColor* text_color, NSColor* red, NSColor* grid, NSColor* header_bg,
                NSColor* summary_bg, NSColor* dim_text) {
    const double sheet_width = plan.sheet_width;
    const double body_top = kHeaderHeight;
    const std::size_t row_count = plan.sheet->visible_row_count();
    const double body_bottom = body_top + static_cast<double>(row_count) * kRowHeight;

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
                   NSMakeRect(x + kTextInset, kHeaderHeight / 2.0 - 8.0,
                              width - 2.0 * kTextInset, 16.0),
                   make_attributes(header_font, dim_text, alignment_for(column.kind)));
    }

    // Row lines across the whole document so the two halves read as one grid.
    for (std::size_t row = 0; row <= row_count; ++row) {
        const double y = body_top + static_cast<double>(row) * kRowHeight;
        stroke_line(NSMakePoint(0.0, y), NSMakePoint(bounds.size.width, y), grid, 1.0);
    }

    // Cell text through the sheet model: it owns date/duration formatting.
    for (std::size_t ordinal = 0; ordinal < row_count; ++ordinal) {
        const std::size_t row = plan.sheet->visible_row(ordinal);
        if (row == core::sheet::kNoIndex) {
            break;
        }
        const double y = body_top + static_cast<double>(ordinal) * kRowHeight;
        for (const core::sheet::ColumnSegment& segment : plan.columns) {
            const std::string text = plan.sheet->cell_text(row, segment.column);
            if (text.empty()) {
                continue;
            }
            const core::sheet::CellStyle style = plan.sheet->cell_style(row, segment.column);
            double x = static_cast<double>(segment.x) + kTextInset;
            if (plan.sheet->column(segment.column).field == app::name_field_id()) {
                x += static_cast<double>(plan.sheet->indent_points(row, plan.metrics));
            }
            draw_label(to_ns(text),
                       NSMakeRect(x, y + 3.0, static_cast<double>(segment.width) - 2.0 * kTextInset,
                                  15.0),
                       make_attributes(style.bold ? bold : body, style.red ? red : text_color,
                                       alignment_for(style.align)));
        }
    }
}

void draw_gantt(PlanState& plan, NSRect bounds, NSFont* header_font, NSFont* label_font,
                NSColor* text_color, NSColor* dim_text, NSColor* grid, NSColor* header_bg,
                NSColor* weekend_bg, NSColor* bar_color, NSColor* bar_fill, NSColor* critical_color,
                NSColor* critical_fill, NSColor* summary_color, NSColor* link_color,
                NSColor* status_color) {
    const double gantt_x = plan.sheet_width + kSheetDivider + kGanttLeftPad;
    const double gantt_width = plan.gantt_width;
    const double body_top = kHeaderHeight;
    const std::size_t row_count = plan.sheet->visible_row_count();
    const double body_bottom = body_top + static_cast<double>(row_count) * kRowHeight;

    const std::vector<core::gantt::HeaderCell> major =
        plan.gantt.header(core::gantt::HeaderTier::major, 0.0, gantt_width);
    const std::vector<core::gantt::HeaderCell> minor =
        plan.gantt.header(core::gantt::HeaderTier::minor, 0.0, gantt_width);

    // Non-working shading over the row band (weekends; holidays when supplied).
    for (const core::gantt::NonworkingSpan& span : plan.gantt.nonworking_spans(0.0, gantt_width)) {
        fill_rect(NSMakeRect(gantt_x + span.x, body_top, span.width, body_bottom - body_top),
                  weekend_bg);
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
                   make_attributes(header_font, cell.weekday == 0 || cell.weekday == 6 ? dim_text
                                                                                       : text_color,
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
        } else if (bar.kind == core::gantt::BarKind::summary) {
            fill_rect(NSMakeRect(x, y, width, height), summary_color);
            // Approximate the product's bracket ends with two short ticks down.
            fill_rect(NSMakeRect(x, y + height, 2.0, 4.0), summary_color);
            fill_rect(NSMakeRect(x + width - 2.0, y + height, 2.0, 4.0), summary_color);
        } else {
            fill_rect(NSMakeRect(x, y, width, height), bar.critical ? critical_fill : bar_fill);
            if (bar.fill.w > 0.0) {
                fill_rect(NSMakeRect(x, y, bar.fill.w, height),
                          bar.critical ? critical_color : bar_color);
            }
            stroke_rect(NSMakeRect(x, y, width, height),
                        bar.critical ? critical_color : bar_color, 1.0);
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
                 NSColor* grid) {
    const double row_count = static_cast<double>(plan.sheet->visible_row_count());
    const double y = kHeaderHeight + row_count * kRowHeight;
    stroke_line(NSMakePoint(0.0, y + 0.5), NSMakePoint(bounds.size.width, y + 0.5), grid, 1.0);
    draw_label(to_ns(plan.status_text), NSMakeRect(8.0, y + 6.0, bounds.size.width - 16.0, 16.0),
               make_attributes(font, dim_text, NSTextAlignmentLeft));
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

    if (plan.sheet == nullptr) {
        NSString* message = plan.error.empty() ? @"No project loaded." : to_ns(plan.error);
        draw_label(message, NSMakeRect(12.0, 12.0, bounds.size.width - 24.0, 18.0),
                   make_attributes(bold, red, NSTextAlignmentLeft));
        return;
    }

    draw_sheet(plan, bounds, body, bold, header_font, text_color, red, grid, header_bg, summary_bg,
               dim_text);
    draw_gantt(plan, bounds, header_font, label_font, text_color, dim_text, grid, header_bg,
               weekend_bg, bar_color, bar_fill, critical_color, critical_fill, summary_color,
               link_color, status_color);
    draw_footer(plan, bounds, footer_font, dim_text, grid);
}

}  // namespace

// ---------------------------------------------------------------------------
// The document view: one view, drawn entirely by draw_plan above.
// ---------------------------------------------------------------------------

@interface PlanView : NSView
@end

@implementation PlanView

- (BOOL)isFlipped {
    return YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    if (g_plan != nullptr) {
        draw_plan(*g_plan, [self bounds]);
    }
}

@end

// ---------------------------------------------------------------------------
// Application
// ---------------------------------------------------------------------------

@interface AppDelegate : NSObject <NSApplicationDelegate>
- (void)installMenu;
@end

static AppDelegate* g_delegate = nil;

@implementation AppDelegate

- (void)installMenu {
    NSMenu* mainMenu = [[NSMenu alloc] initWithTitle:@"MainMenu"];
    NSMenuItem* appItem = [[NSMenuItem alloc] initWithTitle:@"MS Project Shell"
                                                    action:nil
                                             keyEquivalent:@""];
    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"MS Project Shell"];
    [appMenu addItemWithTitle:@"Quit MS Project Shell"
                       action:@selector(terminate:)
                keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [mainMenu addItem:appItem];
    [NSApp setMainMenu:mainMenu];
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    [self installMenu];

    g_plan = new PlanState();
    const std::string path = choose_fixture();
    if (path.empty()) {
        g_plan->error =
            "No .mpp given. Run with a path: msproject_app /path/to/file.mpp "
            "(or put a fixture in corpus/probe).";
    } else {
        load_plan(*g_plan, path);
    }

    if (g_plan->sheet != nullptr) {
        std::printf("msproject-app: %s: %zu tasks, %zu rows, %zu links, %zu bars, %zu warnings\n",
                    path.c_str(), g_plan->input.tasks.size(), g_plan->sheet->row_count(),
                    g_plan->input.links.size(), g_plan->gantt.bars.size(), g_plan->warnings);
        std::fflush(stdout);
    } else {
        std::fprintf(stderr, "msproject-app: cannot load %s: %s\n", path.c_str(),
                     g_plan->error.c_str());
    }

    const double width = content_width(*g_plan);
    const double height = content_height(*g_plan);
    const NSRect frame = NSMakeRect(0.0, 0.0, std::min(width, 1500.0), std::min(height, 920.0));

    g_window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                             NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    NSString* title = @"MS Project Shell";
    if (!g_plan->input.source_name.empty()) {
        title = [NSString stringWithFormat:@"MS Project Shell - %@",
                                           to_ns(g_plan->input.source_name)];
    }
    [g_window setTitle:title];
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

    PlanView* document = [[PlanView alloc] initWithFrame:NSMakeRect(0.0, 0.0, width, height)];
    [scroll setDocumentView:document];

    [g_window setContentView:scroll];
    [g_window makeKeyAndOrderFront:nil];
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
