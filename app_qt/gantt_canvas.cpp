// app_qt/gantt_canvas.cpp -- see gantt_canvas.h.

#include "gantt_canvas.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QRectF>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "layout_model.h"
#include "shell_style.h"

namespace msqt {
namespace {

// One palette, built here, so Fusion paints the same client area everywhere.
const QColor kBackground(255, 255, 255);
const QColor kHeaderMajor(0xee, 0xee, 0xee);
const QColor kHeaderMinor(0xf7, 0xf7, 0xf7);
const QColor kGrid(0xe2, 0xe2, 0xe2);
const QColor kNonworking(0xef, 0xef, 0xef);
const QColor kBarNormal(0x4a, 0x7d, 0xbd);
const QColor kBarCritical(0xb8, 0x3a, 0x2c);
const QColor kBarSummary(0x40, 0x40, 0x40);
const QColor kBarComplete(0x1f, 0x4f, 0x8a);
const QColor kLink(0x55, 0x55, 0x55);
const QColor kOutline(0x20, 0x20, 0x20);
const QColor kText(0x10, 0x10, 0x10);
const QColor kInsideText(255, 255, 255);
const QColor kSelection(0x00, 0x00, 0x00);
const QColor kStatus(0xaa, 0x33, 0x33);

constexpr double kDividerWidth = 1.0;
constexpr int kLabelInset = 3;

int pixel(double value) { return static_cast<int>(std::lround(value)); }

QColor bar_color(const core::gantt::Bar& bar) {
    if (bar.kind == core::gantt::BarKind::summary) {
        return kBarSummary;
    }
    return bar.critical ? kBarCritical : kBarNormal;
}

}  // namespace

GanttCanvas::GanttCanvas(QWidget* parent) : QWidget(parent) {
    QFont body = QApplication::font();
    body.setPointSize(kBodyPointSize);
    setFont(body);
    setMinimumWidth(240);
}

void GanttCanvas::set_layout(const core::gantt::GanttLayout& layout) {
    layout_ = layout;
    update();
}

void GanttCanvas::set_selected_uid(std::uint32_t uid) {
    selected_uid_ = uid;
    update();
}

void GanttCanvas::set_shading(bool enabled) {
    shading_ = enabled;
    update();
}

QSize GanttCanvas::sizeHint() const { return QSize(480, 360); }

QSize GanttCanvas::minimumSizeHint() const { return QSize(240, 200); }

void GanttCanvas::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), kBackground);

    const int canvas_width = width();
    const int canvas_height = height();
    if (canvas_width <= 0 || canvas_height <= 0) {
        return;
    }

    // The header band height is the sheet's, so the two halves' row 0 starts at
    // the same y (app::default_sheet_metrics().header_height == 32).
    const int header_height = app::default_sheet_metrics().header_height;
    const int tier_height = header_height / 2;
    const int body_top = header_height;

    painter.fillRect(QRect(0, 0, canvas_width, tier_height), kHeaderMajor);
    painter.fillRect(QRect(0, tier_height, canvas_width, tier_height), kHeaderMinor);

    // Non-working shading.  A calendar can report the non-working parts of a
    // partly working day (partial == true); this renderer is day-granular, so it
    // draws whole non-working days only (core/gantt/gantt.h nonworking_spans()).
    if (shading_) {
        const std::vector<core::gantt::NonworkingSpan> spans =
            layout_.nonworking_spans(0.0, static_cast<double>(canvas_width));
        for (const core::gantt::NonworkingSpan& span : spans) {
            if (span.partial) {
                continue;
            }
            const int x = static_cast<int>(std::floor(span.x));
            const int span_width = static_cast<int>(std::ceil(span.width));
            if (span_width <= 0 || x + span_width <= 0 || x >= canvas_width) {
                continue;
            }
            painter.fillRect(QRect(x, body_top, span_width, canvas_height - body_top),
                             kNonworking);
        }
    }

    // Grid: one vertical line per minor header cell, one horizontal line per row.
    painter.setPen(QPen(kGrid, 1.0));
    const std::vector<core::gantt::HeaderCell> minor_cells =
        layout_.header(core::gantt::HeaderTier::minor, 0.0, static_cast<double>(canvas_width));
    for (const core::gantt::HeaderCell& cell : minor_cells) {
        const int x = pixel(cell.x);
        if (x < 0 || x > canvas_width) {
            continue;
        }
        painter.drawLine(x, body_top, x, canvas_height);
    }
    for (int row = 0; row <= layout_.row_count; ++row) {
        const int y = body_top + pixel(layout_.rows.y_for_row(row));
        if (y > canvas_height) {
            break;
        }
        painter.drawLine(0, y, canvas_width, y);
    }

    // Dependency links, behind the bars so an arrowhead never hides a bar.
    for (const core::gantt::LinkRoute& link : layout_.links) {
        if (link.points.size() < 2) {
            continue;
        }
        QPolygonF polyline;
        for (const core::gantt::Point& point : link.points) {
            polyline << QPointF(point.x, point.y + body_top);
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(link.critical ? kBarCritical : kLink, 1.0));
        painter.drawPolyline(polyline);

        QPolygonF arrow;
        arrow << QPointF(link.arrow.tip.x, link.arrow.tip.y + body_top)
              << QPointF(link.arrow.wing[0].x, link.arrow.wing[0].y + body_top)
              << QPointF(link.arrow.wing[1].x, link.arrow.wing[1].y + body_top);
        painter.setPen(Qt::NoPen);
        painter.setBrush(link.critical ? kBarCritical : kLink);
        painter.drawPolygon(arrow);
    }

    // Bars, completion fills, summary caps and milestones, all from the layout.
    for (const core::gantt::Bar& bar : layout_.bars) {
        const QRectF bar_rect(bar.rect.x, bar.rect.y + body_top, bar.rect.w, bar.rect.h);
        const QColor fill = bar_color(bar);
        painter.setPen(QPen(kOutline, 1.0));
        painter.setBrush(fill);

        QPolygonF diamond;
        if (bar.shape == core::gantt::BarShape::diamond) {
            for (const core::gantt::Point& point : bar.diamond) {
                diamond << QPointF(point.x, point.y + body_top);
            }
            painter.drawPolygon(diamond);
        } else {
            painter.drawRect(bar_rect);
            if (bar.kind == core::gantt::BarKind::summary) {
                // The model leaves the cap points at the origin when a bar is too
                // short for a cap; check the width before drawing a degenerate
                // triangle at (0, 0).
                if (bar.cap_left[1].x > bar.cap_left[0].x) {
                    QPolygonF cap;
                    for (const core::gantt::Point& point : bar.cap_left) {
                        cap << QPointF(point.x, point.y + body_top);
                    }
                    painter.drawPolygon(cap);
                }
                if (bar.cap_right[1].x < bar.cap_right[0].x) {
                    QPolygonF cap;
                    for (const core::gantt::Point& point : bar.cap_right) {
                        cap << QPointF(point.x, point.y + body_top);
                    }
                    painter.drawPolygon(cap);
                }
            } else if (bar.fill.w > 0.0) {
                painter.fillRect(
                    QRectF(bar.fill.x, bar.fill.y + body_top, bar.fill.w, bar.fill.h),
                    kBarComplete);
            }
        }

        if (selected_uid_ != 0 && bar.task_uid == selected_uid_) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(kSelection, 2.0));
            if (bar.shape == core::gantt::BarShape::diamond) {
                painter.drawPolygon(diamond);
            } else {
                painter.drawRect(bar_rect.adjusted(-1.0, -1.0, 1.0, 1.0));
            }
        }

        // The bar label: text and placement are the model's; this only elides it
        // to the rectangle the model allocated.
        if (bar.label.visible && !bar.label_text.empty()) {
            const QRectF label_rect(bar.label.rect.x, bar.label.rect.y + body_top,
                                    bar.label.rect.w, bar.label.rect.h);
            if (label_rect.width() > 1.0 && label_rect.height() > 1.0) {
                const QFontMetricsF metrics(font());
                const QString text = metrics.elidedText(
                    QString::fromStdString(bar.label_text), Qt::ElideRight,
                    label_rect.width() - 2.0 * kLabelInset);
                painter.setPen(bar.label.inside ? kInsideText : kText);
                painter.drawText(label_rect.adjusted(kLabelInset, 0.0, -kLabelInset, 0.0),
                                 static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), text);
            }
        }
    }

    // Header labels last, so shading and grid never cover them.
    QFont header_font = font();
    header_font.setPointSize(kHeaderPointSize);
    painter.setFont(header_font);
    draw_header_tier(painter, core::gantt::HeaderTier::major, 0, tier_height, canvas_width);
    draw_header_tier(painter, core::gantt::HeaderTier::minor, tier_height, tier_height,
                     canvas_width);

    // The status-date marker, when a layout carries one (this wave passes none).
    if (layout_.has_status_line()) {
        const int x = pixel(layout_.status_x);
        painter.setPen(QPen(kStatus, 1.0));
        painter.drawLine(x, body_top, x, canvas_height);
    }

    // The divider between the sheet and the chart.
    painter.setPen(QPen(kGrid, kDividerWidth));
    painter.drawLine(0, 0, 0, canvas_height);
}

void GanttCanvas::draw_header_tier(QPainter& painter, core::gantt::HeaderTier tier, int top,
                                   int band_height, int canvas_width) {
    const std::vector<core::gantt::HeaderCell> cells =
        layout_.header(tier, 0.0, static_cast<double>(canvas_width));
    const QFontMetricsF metrics(painter.font());
    for (const core::gantt::HeaderCell& cell : cells) {
        const int x = pixel(cell.x);
        const int cell_width = pixel(cell.width);
        if (cell_width <= 0) {
            continue;
        }
        painter.setPen(QPen(kGrid, 1.0));
        painter.drawLine(x, top, x, top + band_height);

        const QRectF box(x + kLabelInset, top, cell_width - 2.0 * kLabelInset, band_height);
        if (box.width() <= 1.0) {
            continue;
        }
        const QString label = QString::fromStdString(cell.label);
        const QString text = metrics.elidedText(label, Qt::ElideRight, box.width());
        painter.setPen(kText);
        painter.drawText(box, static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), text);
    }
    painter.setPen(QPen(kGrid, 1.0));
    painter.drawLine(0, top + band_height - 1, canvas_width, top + band_height - 1);
}

}  // namespace msqt
