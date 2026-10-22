// app_qt/gantt_canvas.cpp -- see gantt_canvas.h.

#include "gantt_canvas.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QPainter>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QRectF>
#include <QSizePolicy>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "shell_style.h"

namespace msqt {
namespace {

const QColor kHeaderMajor(0xfb, 0xfb, 0xfb);
const QColor kHeaderMinor(0xff, 0xff, 0xff);
const QColor kHeaderText(0x33, 0x33, 0x33);
const QColor kMonthRule(0x9a, 0x9a, 0x9a);

constexpr int kLabelInset = 3;
constexpr int kMaxDayCells = 4096;

const char* const kMonthShort[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* const kDayLetter[7] = {"S", "M", "T", "W", "T", "F", "S"};

int pixel(double value) { return static_cast<int>(std::lround(value)); }

std::string week_label(core::sched::date32 monday) {
    if (core::sched::is_na(monday)) {
        return std::string();
    }
    const core::sched::CivilDate civil = core::sched::to_civil(monday);
    const int short_year = civil.year % 100;
    char buffer[32] = {0};
    std::snprintf(buffer, sizeof(buffer), "%d %s '%02d", civil.day,
                  kMonthShort[(civil.month - 1) % 12], short_year);
    return std::string(buffer);
}

QColor bar_color(const core::gantt::Bar& bar) {
    switch (bar.kind) {
        case core::gantt::BarKind::summary: return kBarSummary;
        case core::gantt::BarKind::milestone: return kBarMilestone;
        case core::gantt::BarKind::normal:
            break;
    }
    return bar.critical ? kBarCritical : kBarTask;
}

}  // namespace

GanttCanvas::GanttCanvas(QWidget* parent) : QWidget(parent) {
    QFont body = QApplication::font();
    body.setPointSize(kBodyPointSize);
    setFont(body);
    setMinimumWidth(240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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

// ---------------------------------------------------------------------------
// Header geometry
// ---------------------------------------------------------------------------

std::vector<GanttCanvas::DayCell> GanttCanvas::visible_days(double x0, double x1) const {
    std::vector<DayCell> cells;
    const core::gantt::Timescale& scale = layout_.timescale;
    if (!scale.valid() || !std::isfinite(x0) || !std::isfinite(x1) || x1 <= x0) {
        return cells;
    }
    core::sched::date32 date = core::sched::date_only(scale.date_at_x(x0));
    if (core::sched::is_na(date) || core::sched::is_overflow(date)) {
        return cells;
    }
    // Walk backwards one day so a partially visible first column is complete.
    date = core::sched::add_days(date, -1);
    for (int guard = 0; guard < kMaxDayCells; ++guard) {
        const double x = scale.x_for_day_start(date);
        const double next = scale.x_for_day_start(core::sched::add_days(date, 1));
        if (!std::isfinite(x) || !std::isfinite(next)) {
            break;
        }
        if (x >= x1) {
            break;
        }
        if (next > x0) {
            DayCell cell;
            cell.date = date;
            cell.x = x;
            cell.width = next - x;
            cell.weekday = core::sched::weekday(date);
            cells.push_back(cell);
        }
        date = core::sched::add_days(date, 1);
        if (core::sched::is_na(date) || core::sched::is_overflow(date)) {
            break;
        }
    }
    return cells;
}

std::vector<GanttCanvas::WeekCell> GanttCanvas::visible_weeks(double x0, double x1) const {
    std::vector<WeekCell> weeks;
    const std::vector<DayCell> days = visible_days(x0, x1);
    if (days.empty()) {
        return weeks;
    }
    // Weeks are Sunday-first (the reference's S M T W T F S day row); each week
    // cell is labelled with its Monday, so the first S of a week shows the
    // Monday's date above it ("12 Oct '26").
    WeekCell current;
    current.x = days.front().x;
    current.label_date = days.front().weekday == 0
                             ? core::sched::add_days(days.front().date, 1)
                             : days.front().date;
    for (std::size_t index = 0; index < days.size(); ++index) {
        if (index > 0 && days[index].weekday == 0) {
            current.width = days[index].x - current.x;
            if (current.width > 0.0) {
                weeks.push_back(current);
            }
            current = WeekCell{};
            current.x = days[index].x;
            current.label_date = core::sched::add_days(days[index].date, 1);
        }
    }
    if (!days.empty()) {
        const DayCell& last = days.back();
        current.width = last.x + last.width - current.x;
        if (current.width > 0.0) {
            weeks.push_back(current);
        }
    }
    return weeks;
}

GanttHeaderInfo GanttCanvas::header_info(int width) const {
    GanttHeaderInfo info;
    info.header_height = kGanttHeaderHeight;
    info.tier_height = kGanttTierHeight;
    info.day_width = layout_.timescale.valid() ? layout_.timescale.pixels_per_day() : 0.0;
    const std::vector<DayCell> days = visible_days(0.0, static_cast<double>(width));
    const std::vector<WeekCell> weeks = visible_weeks(0.0, static_cast<double>(width));
    info.major_cells = static_cast<int>(weeks.size());
    info.minor_cells = static_cast<int>(days.size());
    if (!weeks.empty()) {
        info.first_major_label = week_label(weeks.front().label_date);
    }
    if (!days.empty() && days.front().weekday >= 0) {
        info.first_minor_label = kDayLetter[days.front().weekday % 7];
    }
    return info;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void GanttCanvas::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), kSheetBackground);

    const int canvas_width = width();
    const int canvas_height = height();
    if (canvas_width <= 0 || canvas_height <= 0) {
        return;
    }

    // The header band is the sheet's, so row 0 starts at the same y in the two
    // halves (shell_style.h: both 40 px).
    const int body_top = kGanttHeaderHeight;
    const std::vector<DayCell> days = visible_days(0.0, static_cast<double>(canvas_width));
    const std::vector<WeekCell> weeks = visible_weeks(0.0, static_cast<double>(canvas_width));

    // Non-working day shading, from the layout's own calendar/predicate.  This
    // is the model's is_working_day(), not a hard-coded weekend.
    if (shading_) {
        for (const DayCell& day : days) {
            if (layout_.is_working_day(day.date)) {
                continue;
            }
            const int x = pixel(day.x);
            const int w = std::max(1, pixel(day.x + day.width) - x);
            painter.fillRect(QRect(x, kGanttTierHeight, w, canvas_height - kGanttTierHeight),
                             kWeekend);
        }
    }

    // The selected task's row band (Project highlights the whole row).
    for (const core::gantt::Bar& bar : layout_.bars) {
        if (selected_uid_ != 0 && bar.task_uid == selected_uid_) {
            const double top = static_cast<double>(body_top) + layout_.rows.y_for_row(bar.row);
            painter.fillRect(QRect(0, pixel(top), canvas_width,
                                   std::max(1, pixel(layout_.rows.row_height))),
                             kSelection);
        }
    }

    // Day grid; Monday and month-first boundaries are emphasised.
    for (const DayCell& day : days) {
        const int x = pixel(day.x);
        if (x < 0 || x > canvas_width) {
            continue;
        }
        const core::sched::CivilDate civil = core::sched::to_civil(day.date);
        QColor colour = kGridLine;
        int thickness = 1;
        if (civil.day == 1) {
            colour = kMonthRule;
        } else if (day.weekday == 1) {
            colour = kGridLineStrong;
        }
        painter.setPen(QPen(colour, thickness));
        painter.drawLine(x, body_top, x, canvas_height);
    }
    // Horizontal row lines, one per 22 px row, including the empty rows below
    // the plan (the reference's sheet and Gantt stay ruled to the bottom).
    painter.setPen(QPen(kGridLine, 1));
    const int row_height = std::max(1, pixel(layout_.rows.row_height));
    for (int y = body_top; y <= canvas_height; y += row_height) {
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
        painter.setPen(QPen(link.critical ? kBarCritical : kLinkLine, 1.0));
        painter.drawPolyline(polyline);

        QPolygonF arrow;
        arrow << QPointF(link.arrow.tip.x, link.arrow.tip.y + body_top)
              << QPointF(link.arrow.wing[0].x, link.arrow.wing[0].y + body_top)
              << QPointF(link.arrow.wing[1].x, link.arrow.wing[1].y + body_top);
        painter.setPen(Qt::NoPen);
        painter.setBrush(link.critical ? kBarCritical : kLinkLine);
        painter.drawPolygon(arrow);
    }

    // Bars, completion fills, summary caps and milestones, all from the layout.
    for (const core::gantt::Bar& bar : layout_.bars) {
        const QRectF bar_rect(bar.rect.x, bar.rect.y + body_top, bar.rect.w, bar.rect.h);
        painter.setPen(QPen(kBarOutline, 1.0));
        painter.setBrush(bar_color(bar));

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
                painter.setBrush(kBarSummary);
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
            painter.setPen(QPen(kBarOutline, 2.0));
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
                painter.setPen(bar.label.inside ? QColor(0xff, 0xff, 0xff) : kBodyText);
                painter.drawText(label_rect.adjusted(kLabelInset, 0.0, -kLabelInset, 0.0),
                                 static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), text);
            }
        }
    }

    paint_header(painter, days, weeks, canvas_width);

    // The status-date marker, when a layout carries one (this wave passes none).
    if (layout_.has_status_line()) {
        const int x = pixel(layout_.status_x);
        painter.setPen(QPen(kBarCritical, 1.0));
        painter.drawLine(x, body_top, x, canvas_height);
    }

    // The split's own divider.
    painter.setPen(QPen(kGridLineStrong, 1.0));
    painter.drawLine(0, 0, 0, canvas_height);
}

void GanttCanvas::paint_header(QPainter& painter, const std::vector<DayCell>& days,
                               const std::vector<WeekCell>& weeks, int canvas_width) const {
    const int tier = kGanttTierHeight;
    painter.fillRect(QRect(0, 0, canvas_width, tier), kHeaderMajor);
    painter.fillRect(QRect(0, tier, canvas_width, tier), kHeaderMinor);

    QFont header_font = font();
    header_font.setPointSize(kTimescalePointSize);
    painter.setFont(header_font);

    // Tier 0: weekly labels ("12 Oct '26"), left aligned inside their week.
    const QFontMetrics metrics(painter.font());
    for (const WeekCell& week : weeks) {
        const int x = pixel(week.x);
        painter.setPen(QPen(kGridLineStrong, 1));
        painter.drawLine(x, 0, x, tier);
        const std::string label = week_label(week.label_date);
        if (label.empty()) {
            continue;
        }
        const int text_x = std::max(x, 0) + kLabelInset;
        const int available = pixel(week.x + week.width) - text_x - kLabelInset;
        if (available <= 4) {
            continue;
        }
        const QString text = metrics.elidedText(QString::fromStdString(label), Qt::ElideRight,
                                                available);
        painter.setPen(kHeaderText);
        painter.drawText(QRect(text_x, 0, available, tier),
                         static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), text);
    }
    painter.setPen(QPen(kGridLine, 1));
    painter.drawLine(0, tier - 1, canvas_width, tier - 1);

    // Tier 1: single-letter day cells, weekends shaded, Monday edges strong.
    for (const DayCell& day : days) {
        const int x = pixel(day.x);
        const int w = std::max(1, pixel(day.x + day.width) - x);
        if (day.weekday == 0 || day.weekday == 6) {
            painter.fillRect(QRect(x, tier, w, tier), kWeekend);
        }
        if (day.weekday == 1) {
            painter.setPen(QPen(kGridLineStrong, 1));
        } else {
            painter.setPen(QPen(kGridLine, 1));
        }
        painter.drawLine(x, tier, x, tier * 2);
        if (day.weekday >= 0 && w >= 4) {
            painter.setPen(kHeaderText);
            painter.drawText(QRect(x, tier, w, tier), Qt::AlignCenter,
                             QString::fromUtf8(kDayLetter[day.weekday % 7]));
        }
    }
    painter.setPen(QPen(kGridLineStrong, 1));
    painter.drawLine(0, tier * 2 - 1, canvas_width, tier * 2 - 1);
}

}  // namespace msqt
