// app_qt/print_page.cpp -- see print_page.h.

#include "print_page.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QPrintDialog>
#include <QPrinter>
#include <QRectF>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "export_pdf.h"  // app::export_subtitle, app::ReportKind
#include "gantt.h"

namespace msqt {
namespace {

const QColor kInk(0x00, 0x00, 0x00);
const QColor kDim(0x66, 0x66, 0x66);
const QColor kRule(0xb0, 0xb0, 0xb0);
const QColor kBand(0xee, 0xee, 0xee);
const QColor kWeekend(0xf0, 0xf0, 0xf0);
const QColor kCritical(0xc0, 0x39, 0x2b);
const QColor kBar(0x4a, 0x7e, 0xbb);
const QColor kDone(0x2f, 0x5f, 0x9a);

QString to_q(const std::string& text) { return QString::fromStdString(text); }

QFont print_font(double point_size, bool bold = false) {
    QFont font = QApplication::font();
    font.setPointSizeF(point_size);
    font.setBold(bold);
    return font;
}

int align_for(core::sheet::CellAlign align) {
    switch (align) {
        case core::sheet::CellAlign::Right:
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        case core::sheet::CellAlign::Center:
            return static_cast<int>(Qt::AlignHCenter | Qt::AlignVCenter);
        case core::sheet::CellAlign::Left:
            break;
    }
    return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
}

void draw_cell(QPainter& painter, const QRectF& rect, const QString& text, const QFont& font,
               const QColor& colour, int flags) {
    if (text.isEmpty()) {
        return;
    }
    painter.setFont(font);
    painter.setPen(colour);
    painter.drawText(rect, flags, text);
}

void draw_line(QPainter& painter, double x1, double y1, double x2, double y2,
               const QColor& colour, double width) {
    painter.setPen(QPen(colour, width));
    painter.drawLine(QPointF(x1, y1), QPointF(x2, y2));
}

void fill_rect(QPainter& painter, const QRectF& rect, const QColor& colour) {
    painter.fillRect(rect, colour);
}

std::size_t column_of(const core::sheet::SheetModel& sheet, const char* field_name) {
    const core::view::FieldId field = app::task_field_id(field_name);
    if (field == 0) {
        return core::sheet::kNoIndex;
    }
    for (std::size_t column = 0; column < sheet.column_count(); ++column) {
        if (sheet.column(column).field == field) {
            return column;
        }
    }
    return core::sheet::kNoIndex;
}

struct PrintColumn {
    std::size_t model_column = core::sheet::kNoIndex;
    double x = 0.0;
    double width = 0.0;
};

// The five columns the shared report prints, in order (app/main.mm's
// print_table_columns): identity, name, the two dates and the duration.  Widths
// fit the printable width; Name takes the rest.
std::vector<PrintColumn> print_columns(const core::sheet::SheetModel& sheet, double width) {
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
    std::vector<PrintColumn> columns;
    double x = 0.0;
    for (const Spec& spec : specs) {
        const std::size_t model = column_of(sheet, spec.name);
        if (model == core::sheet::kNoIndex) {
            continue;
        }
        PrintColumn column;
        column.model_column = model;
        column.x = x;
        column.width = spec.width > 0.0 ? spec.width : name_width;
        columns.push_back(column);
        x += column.width;
    }
    return columns;
}

// Builds the Gantt the printed chart draws: the same source/style mapping
// app/main.mm uses, with the shared print row height.
core::gantt::GanttLayout build_print_gantt(const app::PlanInput& plan,
                                           const std::vector<std::size_t>& rows,
                                           const app::PageSpec& spec, app::ZoomLevel zoom) {
    app::PlanInput printed;
    printed.title = plan.title;
    printed.project_start = plan.project_start;
    printed.project_finish = plan.project_finish;
    for (const std::size_t row : rows) {
        if (row < plan.tasks.size()) {
            printed.tasks.push_back(plan.tasks[row]);
        }
    }
    printed.links = plan.links;
    core::gantt::GanttStyle style = app::default_gantt_style();
    style.row_height = spec.row_height;
    style.bar_height = std::max(6.0, spec.row_height - 4.0);
    style.summary_height = std::max(4.0, spec.row_height - 7.0);
    style.milestone_size = std::max(6.0, spec.row_height - 4.0);
    return core::gantt::build_gantt(
        app::make_gantt_source(printed), style,
        app::timescale_for_zoom(printed, app::gantt_zoom(zoom)));
}

void draw_page(QPainter& painter, const app::PrintJob& job, std::size_t page_index,
               const core::sheet::SheetModel& sheet, const core::gantt::GanttLayout& gantt,
               const app::Preferences& preferences) {
    const app::PageSpec& spec = job.spec;
    const app::PageLayout& layout = job.layout;
    const app::PrintPage& page = job.pages[page_index];
    const double left = spec.margin;
    const double right = spec.width - spec.margin;

    const QFont title_font = print_font(spec.title_size, true);
    const QFont body_font = print_font(8.0);
    const QFont bold_font = print_font(8.0, true);
    const QFont small_font = print_font(7.0);

    // Header / subtitle / rules: the bands page_layout() computed.
    draw_cell(painter, QRectF(left, layout.title_baseline - spec.title_size,
                              std::max(40.0, right - left - 100.0), spec.title_size + 2.0),
              to_q(page.header), title_font, kInk, static_cast<int>(Qt::AlignLeft));
    draw_cell(painter, QRectF(right - 100.0, layout.title_baseline - 9.0, 100.0, 11.0),
              QStringLiteral("MS Project Shell"), small_font, kDim,
              static_cast<int>(Qt::AlignRight));
    draw_line(painter, left, layout.rule_y, right, layout.rule_y, kRule, 0.6);
    if (!job.subtitle.empty()) {
        draw_cell(painter, QRectF(left, layout.subtitle_baseline - 8.0,
                                  std::max(40.0, right - left), 11.0),
                  to_q(job.subtitle), small_font, kDim, static_cast<int>(Qt::AlignLeft));
    }
    draw_line(painter, left, layout.footer_rule_y, right, layout.footer_rule_y, kRule, 0.5);
    draw_cell(painter, QRectF(left, layout.footer_top, std::max(40.0, right - left), 11.0),
              to_q(page.footer), small_font, kDim, static_cast<int>(Qt::AlignRight));

    const double body_top = layout.body_top;
    const double printable = spec.printable_width();
    const double table_width =
        job.kind == app::PrintKind::gantt ? std::min(340.0, printable * 0.55) : printable;
    const std::vector<PrintColumn> columns = print_columns(sheet, table_width);

    // The table: header band, column rules, then the page's rows.
    fill_rect(painter, QRectF(left, body_top, table_width, spec.header_height), kBand);
    for (const PrintColumn& column : columns) {
        draw_cell(painter, QRectF(left + column.x + 2.0, body_top + 1.0,
                                  std::max(4.0, column.width - 4.0), spec.header_height - 2.0),
                  to_q(sheet.column_title(column.model_column)), bold_font, kInk,
                  static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter));
    }
    const double rows_top = body_top + spec.header_height;
    const double table_bottom =
        rows_top + spec.row_height * static_cast<double>(page.row_count);
    draw_line(painter, left, rows_top, left + table_width, rows_top, kRule, 0.6);
    for (const PrintColumn& column : columns) {
        draw_line(painter, left + column.x, body_top, left + column.x, table_bottom, kRule, 0.4);
    }
    for (std::size_t ordinal = 0; ordinal < page.row_count; ++ordinal) {
        const std::size_t row_index = page.first_row + ordinal;
        if (row_index >= job.rows.size()) {
            break;
        }
        const std::size_t row = job.rows[row_index];
        const double y = rows_top + static_cast<double>(ordinal) * spec.row_height;
        for (const PrintColumn& column : columns) {
            const std::string text = app::cell_text(sheet, row, column.model_column, preferences);
            const core::sheet::CellStyle style = sheet.cell_style(row, column.model_column);
            draw_cell(painter,
                      QRectF(left + column.x + 2.0, y + 1.0,
                             std::max(4.0, column.width - 4.0), spec.row_height - 2.0),
                      to_q(text), style.bold ? bold_font : body_font,
                      style.red ? kCritical : kInk, align_for(style.align));
        }
        draw_line(painter, left, y + spec.row_height, left + table_width,
                  y + spec.row_height, kRule, 0.3);
    }

    if (job.kind != app::PrintKind::gantt) {
        return;
    }

    // The chart: the same core::gantt geometry the window paints, shifted so
    // printed row N sits under the body header.
    const double chart_x = left + table_width + 6.0;
    const double chart_width = std::max(20.0, right - chart_x);
    const double chart_bottom = layout.body_bottom;
    const double header_bottom = body_top + spec.gantt_header_height;
    const std::vector<core::gantt::HeaderCell> major =
        gantt.header(core::gantt::HeaderTier::major, 0.0, chart_width);
    const std::vector<core::gantt::HeaderCell> minor =
        gantt.header(core::gantt::HeaderTier::minor, 0.0, chart_width);

    fill_rect(painter, QRectF(chart_x, body_top, chart_width, spec.gantt_header_height), kBand);
    for (const core::gantt::HeaderCell& cell : major) {
        draw_cell(painter, QRectF(chart_x + cell.x + 2.0, body_top + 1.0,
                                  std::max(4.0, cell.width - 4.0), 11.0),
                  to_q(cell.label), small_font, kDim,
                  static_cast<int>(Qt::AlignHCenter | Qt::AlignTop));
    }
    for (const core::gantt::HeaderCell& cell : minor) {
        draw_cell(painter,
                  QRectF(chart_x + cell.x, body_top + spec.gantt_header_height / 2.0,
                         std::max(4.0, cell.width), 11.0),
                  to_q(cell.label), small_font, kInk,
                  static_cast<int>(Qt::AlignHCenter | Qt::AlignTop));
    }
    draw_line(painter, chart_x, header_bottom, chart_x + chart_width, header_bottom, kRule, 0.6);

    painter.save();
    painter.setClipRect(QRectF(chart_x, body_top, chart_width,
                               std::max(2.0, chart_bottom - body_top)),
                        Qt::IntersectClip);
    if (preferences.working_day_shading) {
        for (const core::gantt::NonworkingSpan& span : gantt.nonworking_spans(0.0, chart_width)) {
            if (span.partial) {
                continue;
            }
            fill_rect(painter,
                      QRectF(chart_x + span.x, body_top, span.width,
                             std::max(2.0, chart_bottom - body_top)),
                      kWeekend);
        }
    }
    for (const core::gantt::HeaderCell& cell : minor) {
        draw_line(painter, chart_x + cell.x, body_top, chart_x + cell.x, chart_bottom, kRule, 0.3);
    }

    const double row_top = static_cast<double>(page.first_row) * spec.row_height;
    const double row_bottom = row_top + static_cast<double>(page.row_count) * spec.row_height;
    const double offset = header_bottom - row_top;

    for (const core::gantt::LinkRoute& route : gantt.links) {
        if (route.points.size() < 2 || route.bounds.bottom() < row_top ||
            route.bounds.y > row_bottom) {
            continue;
        }
        QPolygonF polyline;
        for (const core::gantt::Point& point : route.points) {
            polyline << QPointF(chart_x + point.x, offset + point.y);
        }
        painter.setPen(QPen(route.critical ? kCritical : kDim, 0.7));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(polyline);
    }

    for (const core::gantt::Bar& bar : gantt.bars) {
        if (bar.row < static_cast<int>(page.first_row) ||
            bar.row >= static_cast<int>(page.first_row + page.row_count)) {
            continue;
        }
        const double x = chart_x + bar.rect.x;
        const double y = offset + bar.rect.y;
        const double width = std::max(1.0, bar.rect.w);
        const double height = std::max(1.0, bar.rect.h);
        if (bar.kind == core::gantt::BarKind::milestone) {
            QPolygonF diamond;
            diamond << QPointF(x + width / 2.0, y) << QPointF(x + width, y + height / 2.0)
                    << QPointF(x + width / 2.0, y + height) << QPointF(x, y + height / 2.0);
            painter.setPen(Qt::NoPen);
            painter.setBrush(bar.critical ? kCritical : kInk);
            painter.drawPolygon(diamond);
        } else if (bar.kind == core::gantt::BarKind::summary) {
            fill_rect(painter, QRectF(x, y, width, height), kInk);
            fill_rect(painter, QRectF(x, y + height, 2.0, 4.0), kInk);
            fill_rect(painter, QRectF(x + width - 2.0, y + height, 2.0, 4.0), kInk);
        } else {
            fill_rect(painter, QRectF(x, y, width, height), bar.critical ? kCritical : kBar);
            if (bar.fill.w > 0.0) {
                fill_rect(painter, QRectF(x, y, bar.fill.w, height),
                          bar.critical ? kBar : kDone);
            }
            painter.setPen(QPen(bar.critical ? kCritical : kBar, 0.8));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRectF(x, y, width, height));
        }
    }
    painter.restore();
}

// The QPrinter a print request uses: the job's own PageSpec, so the printed page
// and core/render's PDF agree on the paper, orientation and margins.
void configure_printer(QPrinter& printer, const app::PageSpec& spec) {
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageOrientation(spec.width > spec.height ? QPageLayout::Landscape
                                                        : QPageLayout::Portrait);
    printer.setPageMargins(QMarginsF(0.0, 0.0, 0.0, 0.0), QPageLayout::Point);
    printer.setFullPage(true);
}

}  // namespace

app::PrintJob make_print_job(const app::PlanInput& plan,
                             const std::vector<std::size_t>& rows,
                             const std::string& filter_description,
                             const std::string& date_text, app::ZoomLevel zoom) {
    app::PrintRequest request;
    request.kind = app::PrintKind::gantt;
    request.project_name = plan.title.empty() ? plan.source_name : plan.title;
    request.report_title = "Gantt report";
    request.subtitle = app::export_subtitle(app::ReportKind::gantt, filter_description, rows.size(),
                                            plan.tasks.size());
    request.date_text = date_text;
    request.spec = app::PageSpec::a4_landscape();
    request.rows = rows;
    (void)zoom;  // the zoom is a paint-time property, not a pagination one
    return app::build_print_job(request, app::PrintRoute::printer);
}

std::vector<std::size_t> print_rows(std::size_t record_row_count,
                                    const std::vector<std::size_t>& display_rows,
                                    bool filter_active) {
    if (filter_active) {
        return display_rows;
    }
    std::vector<std::size_t> rows;
    rows.reserve(record_row_count);
    for (std::size_t row = 0; row < record_row_count; ++row) {
        rows.push_back(row);
    }
    return rows;
}

bool print_job(QWidget* parent, const app::PrintJob& job,
               const core::sheet::SheetModel& sheet, const app::PlanInput& plan,
               const app::Preferences& preferences, QString* error) {
    if (job.pages.empty()) {
        if (error != nullptr) {
            *error = QStringLiteral("the print job has no page");
        }
        return false;
    }
    QPrinter printer(QPrinter::HighResolution);
    configure_printer(printer, job.spec);
    QPrintDialog dialog(&printer, parent);
    if (dialog.exec() != QDialog::Accepted) {
        if (error != nullptr) {
            error->clear();
        }
        return false;
    }
    // The page geometry is the job's, not whatever the dialog's last choice was:
    // re-apply it after the panel so every route uses app::PageSpec.
    configure_printer(printer, job.spec);

    core::gantt::GanttLayout gantt;
    if (job.kind == app::PrintKind::gantt) {
        gantt = build_print_gantt(plan, job.rows, job.spec, preferences.zoom);
    }

    QPainter painter;
    if (!painter.begin(&printer)) {
        if (error != nullptr) {
            *error = QStringLiteral("the printer could not be painted");
        }
        return false;
    }
    const QRect page_rect(0, 0, static_cast<int>(std::lround(job.spec.width)),
                          static_cast<int>(std::lround(job.spec.height)));
    for (std::size_t page = 0; page < job.pages.size(); ++page) {
        if (page != 0 && !printer.newPage()) {
            break;
        }
        painter.setWindow(page_rect);
        painter.setViewport(0, 0, printer.width(), printer.height());
        draw_page(painter, job, page, sheet, gantt, preferences);
    }
    painter.end();
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

}  // namespace msqt
