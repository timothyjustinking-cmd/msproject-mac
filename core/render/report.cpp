/*
 * report.cpp -- sheet and Gantt reports on top of PdfWriter.
 *
 * The layout rules are here and nowhere else.  The DATA is not re-derived:
 * every cell string is SheetModel::cell_text(), every bar rectangle and link
 * polyline is core::gantt::build_gantt(), every non-working band is
 * GanttLayout::nonworking_spans().  This file only decides where a page starts
 * and where a rectangle goes.
 *
 * PAGINATION
 *   rows_per_page = floor((body_bottom - body_top - header_height) / row_height)
 *   with a floor of 1, so an absurdly small page still makes progress.  The
 *   title block, column header and legend repeat on every page, and the footer
 *   carries "Page i of n".  The page count is computed before the first page is
 *   drawn, so the footer can print the total.
 *
 * COLUMN WIDTHS (decision)
 *   A column starts at the wider of its header and its widest cell, capped by an
 *   authored SheetColumn::width (characters * metrics.char_width) and by 60 % of
 *   the page.  When the total exceeds the page the columns are scaled
 *   proportionally and then the widest are shaved until the table fits.  The
 *   result is measured with PdfWriter::text_width, so the table does not rely on
 *   a fixed character width.
 *
 * TRUNCATION
 *   Cell text that does not fit is cut at a UTF-8 code point boundary and ends
 *   with "...", so a name is never split mid-sequence and never silently
 *   disappears.  The full value stays in the RecordSet; only the printed text is
 *   short.
 *
 * GANTT TIMESCALE (decision)
 *   With auto_timescale, the unit is day for a span of 45 days or less, week up
 *   to 260 days and month beyond that, and pixels_per_unit is chosen so the
 *   whole project fits the chart width once.  The scale is the same on every
 *   page (pages are row slices), so bars line up vertically across pages.
 */

#include "report.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace core::render {
namespace {

using core::gantt::BarKind;
using core::gantt::GanttLayout;
using core::gantt::GanttSource;
using core::gantt::GanttStyle;
using core::gantt::HeaderCell;
using core::gantt::HeaderTier;
using core::gantt::LinkRoute;
using core::gantt::TaskBarInput;
using core::sched::date32;
using core::sched::kDateNa;
using core::sheet::CellAlign;
using core::sheet::CellKind;
using core::sheet::CellStyle;
using core::sheet::SheetColumn;
using core::sheet::SheetMetrics;
using core::sheet::SheetModel;
using core::view::DateValue;
using core::view::FieldId;
using core::view::RecordSet;
using core::view::Value;

constexpr double kMinColumnWidth = 22.0;
constexpr double kMaxColumnFraction = 0.60;
constexpr double kGanttHeaderHeight = 26.0; /* major band 11 pt + minor band 15 pt */

double clamp_value(double value, double low, double high) {
    return value < low ? low : (value > high ? high : value);
}

FieldId task_field(const char* name) {
    FieldId field = 0;
    if (!core::view::field_id_by_name(core::model::RecordType::Task, name, field)) {
        return 0;
    }
    return field;
}

bool value_true(const RecordSet& records, std::size_t row, FieldId field) {
    if (field == 0) {
        return false;
    }
    const Value* value = records.get(row, field);
    if (value == nullptr || value->is_null()) {
        return false;
    }
    if (value->kind == core::view::ValueKind::Bool) {
        return value->boolean;
    }
    if (value->kind == core::view::ValueKind::Integer) {
        return value->integer != 0;
    }
    return false;
}

double percent_value(const RecordSet& records, std::size_t row, FieldId field) {
    if (field == 0) {
        return 0.0;
    }
    const Value* value = records.get(row, field);
    if (value == nullptr || value->is_null()) {
        return 0.0;
    }
    if (value->kind == core::view::ValueKind::Integer) {
        return clamp_value(static_cast<double>(value->integer) / 100.0, 0.0, 1.0);
    }
    if (value->kind == core::view::ValueKind::Number) {
        return clamp_value(value->number / 100.0, 0.0, 1.0);
    }
    return 0.0;
}

/* A row's baseline for vertically centred text in a row of `row_height`. */
double row_baseline(double top, double row_height, double font_size) {
    return top + (row_height + font_size * 0.70) / 2.0;
}

std::size_t utf8_char_length(unsigned char lead) {
    if (lead < 0x80u) {
        return 1;
    }
    if ((lead & 0xE0u) == 0xC0u) {
        return 2;
    }
    if ((lead & 0xF0u) == 0xE0u) {
        return 3;
    }
    if ((lead & 0xF8u) == 0xF0u) {
        return 4;
    }
    return 1;
}

/* Fits `text` into `max_width`, appending "..." when it has to cut.  Cuts only
 * between UTF-8 code points (a WinAnsi byte is produced later by the writer). */
std::string fit_text(const PdfWriter& pdf, std::string_view text, PdfFont font, double size,
                     double max_width) {
    if (max_width <= 0.0 || text.empty()) {
        return std::string();
    }
    /* 1/1000 pt of tolerance: a column's width is content + 2 * padding and the
     * fit test subtracts the padding again, so exact equality is one rounding
     * step away -- not a real 1/1000 pt of text. */
    if (pdf.text_width(text, font, size) <= max_width + 0.001) {
        return std::string(text);
    }
    const std::string ellipsis = "...";
    const double ellipsis_width = pdf.text_width(ellipsis, font, size);
    if (ellipsis_width > max_width) {
        return std::string();
    }
    std::string out;
    out.reserve(text.size());
    std::size_t index = 0;
    while (index < text.size()) {
        std::size_t length = utf8_char_length(static_cast<unsigned char>(text[index]));
        if (index + length > text.size()) {
            length = 1;
        }
        std::string candidate = out;
        candidate.append(text.substr(index, length));
        if (pdf.text_width(candidate, font, size) + ellipsis_width > max_width + 0.001) {
            break;
        }
        out.swap(candidate);
        index += length;
    }
    return out + ellipsis;
}

double aligned_x(const PdfWriter& pdf, std::string_view text, PdfFont font, double size,
                 double left, double width, double padding, CellAlign align) {
    const double text_width = pdf.text_width(text, font, size);
    switch (align) {
        case CellAlign::Right:
            return left + width - padding - text_width;
        case CellAlign::Center:
            return left + (width - text_width) / 2.0;
        case CellAlign::Left:
        default:
            return left + padding;
    }
}

void draw_text(PdfWriter& pdf, double x, double baseline, std::string_view text, PdfFont font,
               double size, PdfRgb colour) {
    if (text.empty()) {
        return;
    }
    pdf.set_fill_rgb(colour);
    pdf.text(x, baseline, text, font, size);
}

void draw_text_right(PdfWriter& pdf, double right_x, double baseline, std::string_view text,
                     PdfFont font, double size, PdfRgb colour) {
    if (text.empty()) {
        return;
    }
    const double width = pdf.text_width(text, font, size);
    draw_text(pdf, right_x - width, baseline, text, font, size, colour);
}

struct ColumnPlan {
    std::size_t column = 0;
    std::string title;
    double x = 0.0;
    double width = 0.0;
    CellAlign align = CellAlign::Left;
};

CellAlign column_align(const SheetModel& model, std::size_t column,
                       const std::vector<std::size_t>& rows) {
    const core::view::ColumnAlign configured = model.column(column).align;
    if (configured == core::view::ColumnAlign::Right) {
        return CellAlign::Right;
    }
    if (configured == core::view::ColumnAlign::Center) {
        return CellAlign::Center;
    }
    if (configured == core::view::ColumnAlign::Left) {
        return CellAlign::Left;
    }
    if (!rows.empty()) {
        return model.cell_style(rows.front(), column).align;
    }
    return CellAlign::Left;
}

std::vector<ColumnPlan> plan_columns(const PdfWriter& pdf, const SheetModel& model,
                                     const std::vector<std::size_t>& rows,
                                     const ReportStyle& style, const SheetMetrics& metrics,
                                     double available) {
    std::vector<ColumnPlan> plans;
    const std::vector<std::size_t> visible = model.visible_columns();
    std::vector<double> widths;
    widths.reserve(visible.size());

    for (const std::size_t column : visible) {
        ColumnPlan plan;
        plan.column = column;
        plan.title = model.column_title(column);
        plan.align = column_align(model, column, rows);

        double content_width =
            pdf.text_width(plan.title, PdfFont::HelveticaBold, style.header_size);
        for (const std::size_t row : rows) {
            const std::string text = model.cell_text(row, column);
            if (!text.empty()) {
                content_width = std::max(
                    content_width,
                    pdf.text_width(text, PdfFont::Helvetica, style.cell_size));
            }
        }
        double width = content_width + 2.0 * style.cell_padding;
        const int authored = model.column(column).width;
        if (authored > 0 && metrics.char_width > 0) {
            width = std::min(
                width, static_cast<double>(authored * metrics.char_width) +
                           2.0 * style.cell_padding);
        }
        width = clamp_value(width, kMinColumnWidth, available * kMaxColumnFraction);
        plans.push_back(plan);
        widths.push_back(width);
    }

    double total = 0.0;
    for (const double width : widths) {
        total += width;
    }
    if (total > available && total > 0.0) {
        const double scale = available / total;
        total = 0.0;
        for (double& width : widths) {
            width = std::max(kMinColumnWidth, width * scale);
            total += width;
        }
        /* Proportional scaling can still overflow because of the floor; shave the
         * widest column until it fits. */
        while (total > available) {
            std::size_t widest = 0;
            for (std::size_t i = 1; i < widths.size(); ++i) {
                if (widths[i] > widths[widest]) {
                    widest = i;
                }
            }
            if (widths[widest] <= kMinColumnWidth) {
                break;
            }
            const double cut = std::min(4.0, widths[widest] - kMinColumnWidth);
            widths[widest] -= cut;
            total -= cut;
        }
    }

    double x = style.margin;
    for (std::size_t i = 0; i < plans.size(); ++i) {
        plans[i].x = x;
        plans[i].width = widths[i];
        x += widths[i];
    }
    return plans;
}

struct PageLayout {
    double title_baseline = 0.0;
    double subtitle_baseline = 0.0;
    double rule_y = 0.0;
    double body_top = 0.0;
    double body_bottom = 0.0;
    double footer_top = 0.0;
    double footer_rule_y = 0.0;
    std::size_t rows_per_page = 1;
};

PageLayout plan_page(const ReportStyle& style, bool has_subtitle) {
    PageLayout layout;
    layout.title_baseline = style.margin + style.title_size;
    double below = layout.title_baseline + style.title_gap;
    if (has_subtitle) {
        layout.subtitle_baseline = below + style.subtitle_size;
        below = layout.subtitle_baseline + 2.0;
    }
    layout.rule_y = below + 2.0;
    layout.body_top = layout.rule_y + 4.0;
    layout.footer_rule_y = style.page.height - style.margin - style.footer_height;
    layout.footer_top = layout.footer_rule_y + 4.0;
    layout.body_bottom = layout.footer_rule_y - 4.0;
    const double area = layout.body_bottom - layout.body_top - style.header_height;
    layout.rows_per_page = 1;
    if (area > 0.0 && style.row_height > 0.0) {
        layout.rows_per_page =
            std::max<std::size_t>(1, static_cast<std::size_t>(area / style.row_height));
    }
    return layout;
}

void draw_page_frame(PdfWriter& pdf, const ReportRequestBase& request, const PageLayout& layout,
                     std::size_t page_index, std::size_t page_count) {
    const ReportStyle& style = request.style;
    draw_text(pdf, style.margin, layout.title_baseline, request.title, PdfFont::HelveticaBold,
              style.title_size, style.text_color);
    if (!request.subtitle.empty()) {
        draw_text(pdf, style.margin, layout.subtitle_baseline, request.subtitle,
                  PdfFont::Helvetica, style.subtitle_size, style.muted_color);
    }
    pdf.set_stroke_rgb(style.rule_color);
    pdf.set_line_width(0.6);
    pdf.line(style.margin, layout.rule_y, style.page.width - style.margin, layout.rule_y);

    pdf.set_stroke_rgb(style.rule_color);
    pdf.set_line_width(0.5);
    pdf.line(style.margin, layout.footer_rule_y, style.page.width - style.margin,
             layout.footer_rule_y);

    const double footer_baseline = layout.footer_top + style.footer_size;
    if (!request.footer.empty()) {
        const double max_width = style.page.width - 2.0 * style.margin - 90.0;
        const std::string footer =
            fit_text(pdf, request.footer, PdfFont::Helvetica, style.footer_size, max_width);
        draw_text(pdf, style.margin, footer_baseline, footer, PdfFont::Helvetica,
                  style.footer_size, style.muted_color);
    }
    const std::string page_text = "Page " + std::to_string(page_index + 1) + " of " +
                                  std::to_string(page_count);
    draw_text_right(pdf, style.page.width - style.margin, footer_baseline, page_text,
                    PdfFont::Helvetica, style.footer_size, style.muted_color);
}

std::size_t error_page(PdfWriter& pdf, const ReportRequestBase& request, std::string_view message) {
    PageLayout layout = plan_page(request.style, !request.subtitle.empty());
    pdf.add_page(request.style.page);
    draw_page_frame(pdf, request, layout, 0, 1);
    draw_text(pdf, request.style.margin, layout.body_top + request.style.cell_size * 1.5,
              message, PdfFont::Helvetica, request.style.cell_size + 1.0,
              request.style.muted_color);
    return 1;
}

void draw_table_header(PdfWriter& pdf, const std::vector<ColumnPlan>& plans, double top,
                       double height, const ReportStyle& style) {
    if (plans.empty()) {
        return;
    }
    double left = plans.front().x;
    double right = plans.back().x + plans.back().width;
    pdf.set_fill_rgb(style.header_fill);
    pdf.rect(left, top, right - left, height, true, false);

    const double baseline = row_baseline(top, height, style.header_size);
    for (const ColumnPlan& plan : plans) {
        const std::string title =
            fit_text(pdf, plan.title, PdfFont::HelveticaBold, style.header_size,
                     plan.width - 2.0 * style.cell_padding);
        const double x = aligned_x(pdf, title, PdfFont::HelveticaBold, style.header_size,
                                   plan.x, plan.width, style.cell_padding, plan.align);
        draw_text(pdf, x, baseline, title, PdfFont::HelveticaBold, style.header_size,
                  style.text_color);
    }
    pdf.set_stroke_rgb(style.rule_color);
    pdf.set_line_width(0.6);
    pdf.line(left, top + height, right, top + height);
}

void draw_table_grid(PdfWriter& pdf, const std::vector<ColumnPlan>& plans, double top,
                     double bottom, double header_height, const ReportStyle& style,
                     std::size_t row_count) {
    if (plans.empty() || !style.show_grid) {
        return;
    }
    pdf.set_stroke_rgb(style.grid_color);
    pdf.set_line_width(0.4);
    for (const ColumnPlan& plan : plans) {
        pdf.line(plan.x, top, plan.x, bottom);
    }
    const ColumnPlan& last = plans.back();
    pdf.line(last.x + last.width, top, last.x + last.width, bottom);
    for (std::size_t i = 0; i < row_count; ++i) {
        const double y = top + header_height + style.row_height * (i + 1);
        if (y <= bottom) {
            pdf.line(plans.front().x, y, last.x + last.width, y);
        }
    }
}

void draw_table_cells(PdfWriter& pdf, const SheetModel& model,
                      const std::vector<ColumnPlan>& plans, const std::vector<std::size_t>& rows,
                      std::size_t first, std::size_t count, double top, const ReportStyle& style,
                      const SheetMetrics& metrics, FieldId name_field) {
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t ordinal = first + i;
        if (ordinal >= rows.size()) {
            break;
        }
        const std::size_t row = rows[ordinal];
        const double y = top + style.row_height * static_cast<double>(i);
        const double baseline = row_baseline(y, style.row_height, style.cell_size);
        for (const ColumnPlan& plan : plans) {
            std::string text = model.cell_text(row, plan.column);
            if (text.empty()) {
                continue;
            }
            const CellStyle cell_style = model.cell_style(row, plan.column);
            const PdfFont font =
                cell_style.bold ? PdfFont::HelveticaBold : PdfFont::Helvetica;
            const PdfRgb colour =
                cell_style.red ? style.critical_color : style.text_color;
            double indent = 0.0;
            if (name_field != 0 && model.column(plan.column).field == name_field) {
                indent = static_cast<double>(model.indent_points(row, metrics));
            }
            const double max_width = plan.width - 2.0 * style.cell_padding - indent;
            text = fit_text(pdf, text, font, style.cell_size, max_width);
            const double x = aligned_x(pdf, text, font, style.cell_size, plan.x, plan.width,
                                       style.cell_padding + indent, plan.align);
            draw_text(pdf, x, baseline, text, font, style.cell_size, colour);
        }
    }
}

void draw_sheet_legend(PdfWriter& pdf, const ReportStyle& style, const PageLayout& layout) {
    if (!style.show_legend) {
        return;
    }
    const double baseline = layout.footer_top + style.legend_size + 1.0;
    double x = style.margin;
    pdf.set_fill_rgb(style.critical_color);
    pdf.rect(x, baseline - style.legend_size + 1.0, style.legend_size, style.legend_size, true,
             false);
    x += style.legend_size + 3.0;
    draw_text(pdf, x, baseline, "Critical / overdue", PdfFont::Helvetica, style.legend_size,
              style.muted_color);
    x += pdf.text_width("Critical / overdue", PdfFont::Helvetica, style.legend_size) + 14.0;
    draw_text(pdf, x, baseline, "Bold = summary", PdfFont::HelveticaBold, style.legend_size,
              style.muted_color);
}

std::vector<std::size_t> plan_rows(const SheetModel& model) { return model.visible_rows(); }

/* ---- Gantt helpers ------------------------------------------------------ */

date32 to_date32(const DateValue& date) {
    if (!date.valid()) {
        return kDateNa;
    }
    return core::sched::from_ymd(date.year, date.month, date.day, date.hour, date.minute);
}

struct CompiledGantt {
    GanttSource source;
    FieldId name_field = 0;
    FieldId start_field = 0;
    FieldId finish_field = 0;
    FieldId percent_field = 0;
    FieldId critical_field = 0;
    FieldId milestone_field = 0;
    FieldId summary_field = 0;
    FieldId duration_field = 0;
    date32 earliest = kDateNa;
    date32 latest = kDateNa;
};

CompiledGantt compile_gantt(const SheetModel& model, const GanttReportRequest& request) {
    CompiledGantt compiled;
    compiled.name_field = task_field("Name");
    compiled.start_field = task_field("Start");
    compiled.finish_field = task_field("Finish");
    compiled.percent_field = task_field("PercentComplete");
    compiled.critical_field = task_field("Critical");
    compiled.milestone_field = task_field("Milestone");
    compiled.summary_field = task_field("Summary");
    compiled.duration_field = task_field("Duration");

    const RecordSet& records = model.records();
    const std::vector<std::size_t> rows = model.visible_rows();
    compiled.source.tasks.reserve(rows.size());

    for (const std::size_t row : rows) {
        TaskBarInput task;
        task.uid = model.row_uid(row);
        if (compiled.name_field != 0) {
            const std::size_t column = model.column_of_field(compiled.name_field);
            if (column != core::sheet::kNoIndex) {
                task.label = model.cell_text(row, column);
            }
        }
        if (compiled.start_field != 0) {
            const Value* start = records.get(row, compiled.start_field);
            if (start != nullptr && start->kind == core::view::ValueKind::Date) {
                task.start = to_date32(start->date);
            }
        }
        if (compiled.finish_field != 0) {
            const Value* finish = records.get(row, compiled.finish_field);
            if (finish != nullptr && finish->kind == core::view::ValueKind::Date) {
                task.finish = to_date32(finish->date);
            }
        }
        task.percent_complete = percent_value(records, row, compiled.percent_field);
        const bool summary = model.is_summary(row) ||
                             value_true(records, row, compiled.summary_field);
        bool milestone = value_true(records, row, compiled.milestone_field);
        if (!milestone && compiled.duration_field != 0) {
            /* The reader's fallback: a zero-duration task is a milestone when the
             * file carries no stored milestone flag. */
            const Value* duration = records.get(row, compiled.duration_field);
            milestone = duration != nullptr && duration->kind == core::view::ValueKind::Duration &&
                        duration->integer == 0;
        }
        task.kind = summary ? BarKind::summary
                            : (milestone ? BarKind::milestone : BarKind::normal);
        task.critical = value_true(records, row, compiled.critical_field) ||
                        model.is_critical(row);
        if (!core::sched::is_na(task.start) &&
            (core::sched::is_na(compiled.earliest) ||
             core::sched::day_of(task.start) < core::sched::day_of(compiled.earliest))) {
            compiled.earliest = task.start;
        }
        const date32 effective_finish =
            core::sched::is_na(task.finish) ? task.start : task.finish;
        if (!core::sched::is_na(effective_finish) &&
            (core::sched::is_na(compiled.latest) ||
             core::sched::day_of(effective_finish) > core::sched::day_of(compiled.latest))) {
            compiled.latest = effective_finish;
        }
        compiled.source.tasks.push_back(std::move(task));
    }
    compiled.source.links = request.links;
    if (request.calendar) {
        compiled.source.calendar = request.calendar;
    }
    compiled.source.nonworking_days = request.nonworking_days;
    return compiled;
}

/* Chooses the timescale and returns the built layout. */
GanttLayout build_layout(CompiledGantt& compiled, const GanttReportRequest& request,
                         const ReportStyle& style, double chart_width, PdfWriter& pdf) {
    GanttStyle gantt_style = request.gantt_style;
    gantt_style.row_height = style.row_height;
    gantt_style.bar_height = std::min(6.5, std::max(3.0, style.row_height * 0.50));
    gantt_style.summary_height = std::min(5.0, std::max(2.0, style.row_height * 0.38));
    gantt_style.milestone_size = std::min(8.0, std::max(4.0, style.row_height * 0.62));
    gantt_style.label_char_width = style.cell_size * 0.55;
    gantt_style.measure_text = [&pdf](std::string_view text) {
        return pdf.text_width(text, PdfFont::Helvetica, 6.5);
    };

    core::gantt::Timescale timescale = request.timescale;
    if (request.auto_timescale && !core::sched::is_na(compiled.earliest)) {
        timescale.origin = core::sched::date_only(compiled.earliest);
        const std::int32_t span_days =
            core::sched::day_of(compiled.latest) - core::sched::day_of(compiled.earliest) + 1;
        double units = 1.0;
        if (span_days <= 45) {
            timescale.unit = core::gantt::TimescaleUnit::day;
            units = static_cast<double>(std::max<std::int32_t>(1, span_days));
        } else if (span_days <= 260) {
            timescale.unit = core::gantt::TimescaleUnit::week;
            units = static_cast<double>(span_days / 7 + 2);
        } else {
            timescale.unit = core::gantt::TimescaleUnit::month;
            const core::sched::CivilDate first = core::sched::to_civil(timescale.origin);
            const core::sched::CivilDate last = core::sched::to_civil(compiled.latest);
            units = static_cast<double>((last.year - first.year) * 12 +
                                        (last.month - first.month) + 2);
        }
        timescale.pixels_per_unit = std::max(2.0, chart_width / std::max(1.0, units));
    }
    return core::gantt::build_gantt(compiled.source, gantt_style, timescale);
}

void draw_gantt_header(PdfWriter& pdf, const GanttLayout& layout, double left, double top,
                       double width, const ReportStyle& style) {
    const double major_height = 11.0;
    const double minor_height = kGanttHeaderHeight - major_height;
    pdf.set_fill_rgb(style.header_fill);
    pdf.rect(left, top, width, major_height, true, false);
    pdf.rect(left, top + major_height, width, minor_height, true, false);

    pdf.begin_clip(left, top, width, kGanttHeaderHeight);

    for (const HeaderCell& cell : layout.header(HeaderTier::major, 0.0, width)) {
        const std::string label =
            fit_text(pdf, cell.label, PdfFont::HelveticaBold, style.legend_size,
                     cell.width - 3.0);
        if (!label.empty()) {
            draw_text(pdf, left + cell.x + 2.0, top + 8.0, label, PdfFont::HelveticaBold,
                      style.legend_size, style.text_color);
        }
    }
    for (const HeaderCell& cell : layout.header(HeaderTier::minor, 0.0, width)) {
        const std::string label =
            fit_text(pdf, cell.label, PdfFont::Helvetica, style.legend_size,
                     cell.width - 2.0);
        if (!label.empty()) {
            const double x = aligned_x(pdf, label, PdfFont::Helvetica, style.legend_size,
                                       left + cell.x, cell.width, 1.0, CellAlign::Center);
            draw_text(pdf, x, top + major_height + minor_height - 4.0, label,
                      PdfFont::Helvetica, style.legend_size, style.text_color);
        }
        pdf.set_stroke_rgb(style.grid_color);
        pdf.set_line_width(0.35);
        pdf.line(left + cell.x, top, left + cell.x, top + kGanttHeaderHeight);
    }
    pdf.end_clip();
    pdf.set_stroke_rgb(style.rule_color);
    pdf.set_line_width(0.5);
    pdf.line(left, top + kGanttHeaderHeight, left + width, top + kGanttHeaderHeight);
}

void draw_gantt_legend(PdfWriter& pdf, const ReportStyle& style, const PageLayout& layout) {
    if (!style.show_legend) {
        return;
    }
    const double baseline = layout.footer_top + style.legend_size + 1.0;
    const double box = style.legend_size;
    double x = style.margin;
    auto label = [&](std::string_view text, double gap) {
        draw_text(pdf, x, baseline, text, PdfFont::Helvetica, style.legend_size,
                  style.muted_color);
        x += pdf.text_width(text, PdfFont::Helvetica, style.legend_size) + gap;
    };
    pdf.set_fill_rgb(style.bar_color);
    pdf.rect(x, baseline - box + 1.0, box * 1.6, box, true, false);
    x += box * 1.6 + 3.0;
    label("Task", 12.0);
    pdf.set_fill_rgb(style.critical_color);
    pdf.rect(x, baseline - box + 1.0, box * 1.6, box, true, false);
    x += box * 1.6 + 3.0;
    label("Critical", 12.0);
    pdf.set_fill_rgb(style.summary_color);
    pdf.rect(x, baseline - box * 0.5, box * 1.6, box * 0.62, true, false);
    x += box * 1.6 + 3.0;
    label("Summary", 12.0);
    pdf.set_fill_rgb(style.milestone_color);
    const double centre_x = x + box * 0.6;
    const double centre_y = baseline - box * 0.4;
    pdf.polygon({PdfPoint{centre_x, centre_y - box * 0.7},
                 PdfPoint{centre_x + box * 0.6, centre_y},
                 PdfPoint{centre_x, centre_y + box * 0.7},
                 PdfPoint{centre_x - box * 0.6, centre_y}},
                true, false);
    x += box * 1.6 + 3.0;
    label("Milestone", 12.0);
    pdf.set_stroke_rgb(style.link_color);
    pdf.set_line_width(0.6);
    pdf.line(x, baseline - box * 0.4, x + box * 2.0, baseline - box * 0.4);
    x += box * 2.0 + 3.0;
    label("Dependency", 0.0);
}

void draw_bars(PdfWriter& pdf, const GanttLayout& layout, const ReportStyle& style,
               double chart_x, double chart_width, double content_top, std::size_t first_row,
               std::size_t row_count) {
    const double row_height = style.row_height;
    for (const core::gantt::Bar& bar : layout.bars) {
        if (bar.row < static_cast<int>(first_row) ||
            bar.row >= static_cast<int>(first_row + row_count)) {
            continue;
        }
        const double y = content_top + bar.rect.y - static_cast<double>(first_row) * row_height;
        const double x = chart_x + bar.rect.x;
        const PdfRgb colour = bar.critical ? style.critical_color : style.bar_color;
        switch (bar.kind) {
            case BarKind::summary: {
                pdf.set_fill_rgb(style.summary_color);
                pdf.rect(x, y, bar.rect.w, bar.rect.h, true, false);
                pdf.set_stroke_rgb(style.summary_color);
                pdf.set_line_width(0.8);
                pdf.line(x, y - 2.0, x, y + bar.rect.h + 2.0);
                pdf.line(x + bar.rect.w, y - 2.0, x + bar.rect.w, y + bar.rect.h + 2.0);
                break;
            }
            case BarKind::milestone: {
                const double cx = x + bar.rect.w / 2.0;
                const double cy = y + bar.rect.h / 2.0;
                const double half_w = bar.rect.w / 2.0;
                const double half_h = bar.rect.h / 2.0;
                pdf.set_fill_rgb(style.milestone_color);
                pdf.polygon({PdfPoint{cx, cy - half_h}, PdfPoint{cx + half_w, cy},
                             PdfPoint{cx, cy + half_h}, PdfPoint{cx - half_w, cy}},
                            true, false);
                break;
            }
            case BarKind::normal:
            default: {
                pdf.set_fill_rgb(colour);
                pdf.rect(x, y, bar.rect.w, bar.rect.h, true, false);
                if (bar.fill.w > 0.0) {
                    pdf.set_fill_rgb(style.bar_done_color);
                    pdf.rect(x, y, bar.fill.w, bar.rect.h, true, false);
                }
                pdf.set_stroke_rgb(colour);
                pdf.set_line_width(0.4);
                pdf.rect(x, y, bar.rect.w, bar.rect.h, false, true);
                break;
            }
        }

        /* The report always labels to the right of the bar (a printed sheet has
         * no hover); the geometry's own placement decides `visible` and text. */
        if (!bar.label_text.empty()) {
            const double label_x = x + bar.rect.w + 3.0;
            const double available = chart_x + chart_width - label_x;
            const std::string text =
                fit_text(pdf, bar.label_text, PdfFont::Helvetica, 6.5, available);
            if (!text.empty()) {
                draw_text(pdf, label_x, y + bar.rect.h - 1.0, text, PdfFont::Helvetica, 6.5,
                          style.muted_color);
            }
        }
    }
}

void draw_links(PdfWriter& pdf, const GanttLayout& layout, const ReportStyle& style,
                double chart_x, double content_top, std::size_t first_row,
                std::size_t row_count) {
    const double row_height = style.row_height;
    const double offset = content_top - static_cast<double>(first_row) * row_height;
    auto map = [&](const core::gantt::Point& point) {
        return PdfPoint{chart_x + point.x, offset + point.y};
    };
    for (const LinkRoute& route : layout.links) {
        if (route.points.size() < 2) {
            continue;
        }
        bool touches_page = false;
        for (const core::gantt::Point& point : route.points) {
            const int row = layout.rows.row_at_y(point.y);
            if (row >= static_cast<int>(first_row) &&
                row < static_cast<int>(first_row + row_count)) {
                touches_page = true;
                break;
            }
        }
        if (!touches_page) {
            continue;
        }
        pdf.set_stroke_rgb(style.link_color);
        pdf.set_line_width(0.5);
        for (std::size_t i = 1; i < route.points.size(); ++i) {
            const PdfPoint previous = map(route.points[i - 1]);
            const PdfPoint next = map(route.points[i]);
            pdf.line(previous.x, previous.y, next.x, next.y);
        }
        pdf.set_fill_rgb(style.link_color);
        pdf.polygon({map(route.arrow.tip), map(route.arrow.wing[0]),
                     map(route.arrow.wing[1])},
                    true, false);
    }
}

}  // namespace

std::vector<SheetColumn> task_report_columns() {
    struct Definition {
        const char* field;
        int width;
        CellKind kind;
    };
    /* Widths are characters, the sheet's unit; the report uses them as a cap. */
    static const Definition definitions[] = {
        {"ID", 5, CellKind::Integer},
        {"Name", 40, CellKind::Text},
        {"Duration", 8, CellKind::Duration},
        {"Start", 14, CellKind::Date},
        {"Finish", 14, CellKind::Date},
        {"TotalSlack", 8, CellKind::Duration},
        {"Critical", 8, CellKind::Bool},
    };
    std::vector<SheetColumn> columns;
    for (const Definition& definition : definitions) {
        const FieldId field = task_field(definition.field);
        if (field == 0) {
            continue;
        }
        SheetColumn column;
        column.field = field;
        column.width = definition.width;
        column.kind = definition.kind;
        if (std::string_view(definition.field) == "TotalSlack") {
            column.title = "Slack";
        }
        columns.push_back(std::move(column));
    }
    return columns;
}

std::size_t render_sheet_report(PdfWriter& pdf, const SheetReportRequest& request) {
    ReportStyle style = request.style.valid() ? request.style : ReportStyle{};
    if (request.records == nullptr) {
        return error_page(pdf, request, "No records to report.");
    }
    if (!request.info.title.empty()) {
        pdf.set_info(request.info);
    }
    std::vector<SheetColumn> columns =
        request.columns.empty() ? task_report_columns() : request.columns;
    if (columns.empty()) {
        return error_page(pdf, request, "No known columns for this record set.");
    }

    SheetModel model(*request.records, columns);
    model.set_format(request.format);
    const std::vector<std::size_t> rows = plan_rows(model);
    const std::vector<std::size_t> visible_columns = model.visible_columns();
    if (visible_columns.empty()) {
        return error_page(pdf, request, "Every column is hidden.");
    }

    const double available = style.page.width - 2.0 * style.margin;
    const std::vector<ColumnPlan> plans =
        plan_columns(pdf, model, rows, style, request.metrics, available);
    const PageLayout layout = plan_page(style, !request.subtitle.empty());
    const FieldId name_field = task_field("Name");
    const std::size_t rows_per_page = std::max<std::size_t>(1, layout.rows_per_page);
    const std::size_t page_count =
        rows.empty() ? 1 : (rows.size() + rows_per_page - 1) / rows_per_page;

    for (std::size_t page = 0; page < page_count; ++page) {
        pdf.add_page(style.page);
        draw_page_frame(pdf, request, layout, page, page_count);
        draw_table_header(pdf, plans, layout.body_top, style.header_height, style);
        const std::size_t first = page * rows_per_page;
        const std::size_t count = std::min(rows_per_page, rows.size() - std::min(first, rows.size()));
        draw_table_cells(pdf, model, plans, rows, first, count,
                         layout.body_top + style.header_height, style, request.metrics,
                         name_field);
        const double table_bottom =
            layout.body_top + style.header_height + style.row_height * static_cast<double>(count);
        const double grid_bottom = rows.empty() ? layout.body_top + style.header_height
                                                : std::max(table_bottom, layout.body_top +
                                                                             style.header_height +
                                                                                 style.row_height);
        draw_table_grid(pdf, plans, layout.body_top, std::min(grid_bottom, layout.body_bottom),
                        style.header_height, style, count);
        if (rows.empty()) {
            draw_text(pdf, style.margin + style.cell_padding,
                      layout.body_top + style.header_height + style.cell_size * 1.4,
                      "(no rows)", PdfFont::Helvetica, style.cell_size, style.muted_color);
        }
        draw_sheet_legend(pdf, style, layout);
    }
    return page_count;
}

std::size_t render_gantt_report(PdfWriter& pdf, const GanttReportRequest& request) {
    ReportStyle style = request.style.valid() ? request.style : ReportStyle{};
    if (request.records == nullptr) {
        return error_page(pdf, request, "No records to report.");
    }
    if (!request.info.title.empty()) {
        pdf.set_info(request.info);
    }
    std::vector<SheetColumn> columns =
        request.columns.empty() ? task_report_columns() : request.columns;
    if (columns.empty()) {
        return error_page(pdf, request, "No known columns for this record set.");
    }

    SheetModel model(*request.records, columns);
    model.set_format(request.format);
    const std::vector<std::size_t> rows = plan_rows(model);
    const FieldId name_field = task_field("Name");

    const double available = style.page.width - 2.0 * style.margin;

    /* Left table: at most table_width, leaving room for the chart. */
    const double max_table_width = std::max(110.0, available - 160.0);
    const double wanted_table_width =
        clamp_value(request.table_width, 110.0, max_table_width);
    const std::vector<ColumnPlan> plans =
        plan_columns(pdf, model, rows, style, request.metrics, wanted_table_width);
    double table_width = 0.0;
    for (const ColumnPlan& plan : plans) {
        table_width = std::max(table_width, plan.x + plan.width - style.margin);
    }

    const double chart_x = style.margin + table_width + 6.0;
    const double chart_width = std::max(40.0, style.page.width - style.margin - chart_x);
    const PageLayout layout = plan_page(style, !request.subtitle.empty());
    const double content_top = layout.body_top + kGanttHeaderHeight;
    const double content_bottom = layout.body_bottom;
    std::size_t rows_per_page = 1;
    if (content_bottom > content_top) {
        rows_per_page = std::max<std::size_t>(
            1, static_cast<std::size_t>((content_bottom - content_top) / style.row_height));
    }

    CompiledGantt compiled = compile_gantt(model, request);
    if (rows.empty() || core::sched::is_na(compiled.earliest)) {
        PageLayout only = plan_page(style, !request.subtitle.empty());
        pdf.add_page(style.page);
        draw_page_frame(pdf, request, only, 0, 1);
        draw_table_header(pdf, plans, only.body_top, style.header_height, style);
        draw_table_cells(pdf, model, plans, rows, 0, std::min(rows.size(), rows_per_page),
                         only.body_top + style.header_height, style, request.metrics,
                         name_field);
        draw_text(pdf, style.margin + style.cell_padding,
                  only.body_top + style.header_height + style.cell_size * 1.4,
                  rows.empty() ? "(no rows)" : "(no scheduled tasks)",
                  PdfFont::Helvetica, style.cell_size, style.muted_color);
        draw_gantt_legend(pdf, style, only);
        return 1;
    }

    const GanttLayout gantt = build_layout(compiled, request, style, chart_width, pdf);
    const std::size_t page_count =
        (rows.size() + rows_per_page - 1) / rows_per_page;

    for (std::size_t page = 0; page < page_count; ++page) {
        const std::size_t first = page * rows_per_page;
        const std::size_t count =
            std::min(rows_per_page, rows.size() - std::min(first, rows.size()));
        pdf.add_page(style.page);
        draw_page_frame(pdf, request, layout, page, page_count);

        /* Left table: header band is as tall as the two-tier timescale band so
         * the first row lines up on both sides. */
        draw_table_header(pdf, plans, layout.body_top, kGanttHeaderHeight, style);
        draw_table_cells(pdf, model, plans, rows, first, count,
                         layout.body_top + kGanttHeaderHeight, style, request.metrics,
                         name_field);
        draw_table_grid(pdf, plans, layout.body_top,
                        std::min(layout.body_top + kGanttHeaderHeight +
                                     style.row_height * static_cast<double>(count),
                                 layout.body_bottom),
                        kGanttHeaderHeight, style, count);

        draw_gantt_header(pdf, gantt, chart_x, layout.body_top, chart_width, style);

        pdf.begin_clip(chart_x, content_top, chart_width, content_bottom - content_top);
        /* Non-working shading: whole-day spans only (the model's day-granularity
         * rule; partial intra-day spans belong to an hour zoom). */
        for (const core::gantt::NonworkingSpan& span :
             gantt.nonworking_spans(0.0, chart_width)) {
            if (span.partial) {
                continue;
            }
            pdf.set_fill_rgb(PdfRgb{0.93, 0.93, 0.94});
            pdf.rect(chart_x + span.x, content_top, span.width,
                     content_bottom - content_top, true, false);
        }
        /* Vertical date grid: one line per minor timescale column, so the chart
         * body lines up with the header the way the app's canvas does. */
        if (style.show_grid) {
            pdf.set_stroke_rgb(style.grid_color);
            pdf.set_line_width(0.3);
            for (const HeaderCell& cell :
                 gantt.header(HeaderTier::minor, 0.0, chart_width)) {
                pdf.line(chart_x + cell.x, content_top, chart_x + cell.x, content_bottom);
            }
        }
        /* Row rules across the chart. */
        if (style.show_grid) {
            pdf.set_stroke_rgb(style.grid_color);
            pdf.set_line_width(0.35);
            for (std::size_t i = 0; i <= count; ++i) {
                const double y = content_top + style.row_height * static_cast<double>(i);
                if (y <= content_bottom) {
                    pdf.line(chart_x, y, chart_x + chart_width, y);
                }
            }
        }
        draw_bars(pdf, gantt, style, chart_x, chart_width, content_top, first, count);
        draw_links(pdf, gantt, style, chart_x, content_top, first, count);

        if (gantt.has_status && !std::isnan(gantt.status_x)) {
            pdf.set_stroke_rgb(PdfRgb{0.55, 0.10, 0.10});
            pdf.set_line_width(0.6);
            pdf.line(chart_x + gantt.status_x, content_top, chart_x + gantt.status_x,
                     content_bottom);
        }
        pdf.end_clip();

        draw_gantt_legend(pdf, style, layout);
    }
    return page_count;
}

}  // namespace core::render
