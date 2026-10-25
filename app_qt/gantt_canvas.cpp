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
const QColor kGanttHeaderText(0x33, 0x33, 0x33);  // distinct from shell_style's kHeaderText
const QColor kMonthRule(0x9a, 0x9a, 0x9a);

constexpr int kLabelInset = 3;
// The bar-label padding budget on EACH side of the text: the model's
// GanttStyle::label_gap (4 px) is what decides whether a label fits inside a
// bar, so the renderer uses the same number rather than its own.
constexpr double kBarLabelPadding = 4.0;
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

// Wave 25 (W25-2/S2): the row's scheduling mode chooses the bar style, not
// its critical flag: Project's default has Critical Tasks highlighting OFF, so
// a critical auto task is painted like any auto task.  kBarCritical is only for
// the highlight-ON state (the measured (247,138,136)).
//
// Wave 26 (W26-3.2): this is the STOCK fallback.  A file whose Gantt style
// record decodes uses GanttCanvas::bar_colour_for()'s file palette instead.
QColor bar_color(const core::gantt::Bar& bar, bool critical_highlight) {
    switch (bar.kind) {
        case core::gantt::BarKind::summary:
            return bar.manual ? kBarSummaryManual : kBarSummary;
        case core::gantt::BarKind::milestone: return kBarMilestone;
        case core::gantt::BarKind::normal:
            break;
    }
    // The measured table: a manual bar does NOT change with Critical Tasks ON;
    // only an auto bar takes the highlight colour (wave 25 review).
    if (bar.manual) {
        return kBarManual;
    }
    return bar.critical && critical_highlight ? kBarCritical : kBarTask;
}

QColor to_qcolor(const msproject::viewstate::Rgb& colour) {
    return QColor(colour.red, colour.green, colour.blue);
}

}  // namespace

QColor GanttCanvas::bar_colour(std::size_t index) const {
    if (index >= layout_.bars.size()) {
        return {};
    }
    return bar_colour_for(layout_.bars[index]);
}

QColor GanttCanvas::bar_colour_for(const core::gantt::Bar& bar) const {
    // Critical Tasks highlighting ON keeps its wave-25 measured override: an
    // auto critical bar is red along its whole length whatever the file says.
    if (critical_highlight_ && bar.critical &&
        bar.kind == core::gantt::BarKind::normal) {
        return kBarCritical;
    }
    if (palette_.decoded) {
        switch (bar.kind) {
            case core::gantt::BarKind::summary:
                // The project row is a summary too.  Its synthetic TaskRow is
                // not a live task (uid 0), so the reader carries no critical
                // flag for it; PROJECT_A's clean capture shows its project bar
                // in the file's summary+critical red, so that style is used
                // when the file defines one.  A critical summary uses the same
                // style; a noncritical summary the Summary style.
                if (bar.task_uid == 0) {
                    if (palette_.has_summary_critical) {
                        return to_qcolor(palette_.summary_critical);
                    }
                    if (palette_.has_project_summary) {
                        return to_qcolor(palette_.project_summary);
                    }
                } else if (bar.critical && palette_.has_summary_critical) {
                    return to_qcolor(palette_.summary_critical);
                }
                if (palette_.has_summary) {
                    return to_qcolor(palette_.summary);
                }
                break;
            case core::gantt::BarKind::milestone:
                if (palette_.has_milestone) {
                    return to_qcolor(palette_.milestone);
                }
                break;
            case core::gantt::BarKind::normal:
                if (bar.manual && palette_.has_manual_normal) {
                    return to_qcolor(palette_.manual_normal);
                }
                if (bar.critical && palette_.has_critical) {
                    return to_qcolor(palette_.critical);
                }
                if (palette_.has_normal) {
                    return to_qcolor(palette_.normal);
                }
                break;
        }
    }
    return bar_color(bar, critical_highlight_);
}

void GanttCanvas::set_gantt_style(const msproject::viewstate::GanttStyle* style) {
    style_ = style;
    palette_ = style != nullptr ? app::bar_palette_from(*style) : app::BarPalette{};
    update();
}

GanttCanvas::GanttCanvas(QWidget* parent) : QWidget(parent) {
    QFont body = QApplication::font();
    body.setPointSize(kBodyPointSize);
    setFont(body);
    setMinimumWidth(240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void GanttCanvas::set_layout(const core::gantt::GanttLayout& layout) {
    layout_ = layout;
    // Wave 26 (W26-3.2): PlanDocument replaces its decoded style in place on
    // every load, so re-reading the palette here keeps the canvas in step
    // without the shell calling both setters at every layout change.
    palette_ = style_ != nullptr ? app::bar_palette_from(*style_) : app::BarPalette{};
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

void GanttCanvas::set_critical_highlight(bool enabled) {
    critical_highlight_ = enabled;
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
    info.day_width = layout_.timescale.valid() ? layout_.timescale.pixels_per_day() : 0.0;
    const std::vector<DayCell> days = visible_days(0.0, static_cast<double>(width));
    const std::vector<WeekCell> weeks = visible_weeks(0.0, static_cast<double>(width));
    const std::vector<HeaderTier> tiers = header_tiers(days, weeks);
    info.tier_height = tiers.empty() ? kGanttTierHeight
                                     : kGanttHeaderHeight / static_cast<int>(tiers.size());
    if (tiers.size() >= 2) {
        const HeaderTier& major = tiers[tiers.size() - 2];
        info.major_cells = static_cast<int>(major.cells.size());
        if (!major.cells.empty()) {
            info.first_major_label = major.cells.front().label;
        }
    }
    if (!tiers.empty()) {
        const HeaderTier& minor = tiers.back();
        info.minor_cells = static_cast<int>(minor.cells.size());
        if (!minor.cells.empty()) {
            info.first_minor_label = minor.cells.front().label;
        }
    }
    return info;
}

// ---------------------------------------------------------------------------
// Wave 26 (W26-3.3): the stored timescale tiers
// ---------------------------------------------------------------------------

namespace {

long long unit_key(core::sched::date32 date, msproject::viewstate::TimescaleUnit unit) {
    const core::sched::CivilDate civil = core::sched::to_civil(date);
    switch (unit) {
        case msproject::viewstate::TimescaleUnit::years:
            return civil.year;
        case msproject::viewstate::TimescaleUnit::half_years:
            return static_cast<long long>(civil.year) * 2 + (civil.month - 1) / 6;
        case msproject::viewstate::TimescaleUnit::quarters:
            return static_cast<long long>(civil.year) * 4 + (civil.month - 1) / 3;
        case msproject::viewstate::TimescaleUnit::months:
            return static_cast<long long>(civil.year) * 12 + (civil.month - 1);
        case msproject::viewstate::TimescaleUnit::thirds_of_months:
            return (static_cast<long long>(civil.year) * 12 + (civil.month - 1)) * 3 +
                   (civil.day - 1) / 10;
        default:
            return civil.day;
    }
}

const char* const kMonthLong[12] = {"January", "February", "March",     "April",
                                    "May",     "June",     "July",      "August",
                                    "September", "October", "November", "December"};

std::string tier_label(core::sched::date32 date,
                       const msproject::viewstate::TimescaleTier& spec) {
    const core::sched::CivilDate civil = core::sched::to_civil(date);
    switch (spec.unit) {
        case msproject::viewstate::TimescaleUnit::years:
            return std::to_string(civil.year);
        case msproject::viewstate::TimescaleUnit::half_years:
            return civil.month <= 6 ? "H1" : "H2";
        case msproject::viewstate::TimescaleUnit::quarters: {
            const int quarter = (civil.month - 1) / 3 + 1;
            if (spec.format == 2) {
                static const char* const kOrdinal[4] = {"1st Quarter", "2nd Quarter",
                                                        "3rd Quarter", "4th Quarter"};
                return kOrdinal[quarter - 1];
            }
            if (spec.format == 6) {
                return "Q" + std::to_string(quarter);
            }
            // Format 3 ("Qtr 1, 2002") is the measured PROJECT_A format.
            return "Qtr " + std::to_string(quarter) + ", " + std::to_string(civil.year);
        }
        case msproject::viewstate::TimescaleUnit::months:
            if (spec.format == 9) {
                return kMonthLong[(civil.month - 1) % 12];
            }
            if (spec.format == 7) {
                return std::string(kMonthLong[(civil.month - 1) % 12]) + " " +
                       std::to_string(civil.year);
            }
            if (spec.format == 11) {
                return std::string(1, kMonthLong[(civil.month - 1) % 12][0]);
            }
            return kMonthShort[(civil.month - 1) % 12];
        default:
            break;
    }
    return week_label(date);
}

}  // namespace

std::vector<GanttCanvas::HeaderTier> GanttCanvas::header_tiers(
    const std::vector<DayCell>& days, const std::vector<WeekCell>& weeks) const {
    using msproject::viewstate::TextStyleItem;
    using msproject::viewstate::TimescaleTier;
    using msproject::viewstate::TimescaleUnit;

    std::vector<HeaderTier> tiers;
    const bool file = style_ != nullptr && style_->decoded;

    const auto add = [&](const TimescaleTier& spec, TextStyleItem item) {
        HeaderTier tier;
        tier.spec = spec;
        if (spec.count <= 0) {
            tier.spec.count = 1;
        }
        if (file) {
            const auto index = static_cast<std::size_t>(item);
            if (index < style_->text.size() && style_->text[index].valid) {
                tier.has_text_style = true;
                tier.text = style_->text[index];
            }
        }
        if (spec.unit == TimescaleUnit::days) {
            // One cell per day; a count > 1 groups that many day columns.
            for (std::size_t index = 0; index < days.size();) {
                TierCell cell;
                cell.x = days[index].x;
                cell.weekday = days[index].weekday;
                std::size_t end = index;
                for (int taken = 0; taken < tier.spec.count && end < days.size();
                     ++taken, ++end) {
                }
                cell.width = days[end - 1].x + days[end - 1].width - cell.x;
                if (cell.weekday >= 0) {
                    cell.label = kDayLetter[cell.weekday % 7];
                }
                tier.cells.push_back(std::move(cell));
                index = end;
            }
        } else if (spec.unit == TimescaleUnit::weeks) {
            // Reuse visible_weeks (Sunday-first weeks labelled with the Monday),
            // which is the measured stock rendering, then merge `count` weeks.
            for (std::size_t index = 0; index < weeks.size();) {
                TierCell cell;
                cell.x = weeks[index].x;
                cell.label = week_label(weeks[index].label_date);
                std::size_t end = index;
                for (int taken = 0; taken < tier.spec.count && end < weeks.size();
                     ++taken, ++end) {
                }
                cell.width = weeks[end - 1].x + weeks[end - 1].width - cell.x;
                tier.cells.push_back(std::move(cell));
                index = end;
            }
        } else if (!days.empty()) {
            std::size_t index = 0;
            while (index < days.size()) {
                const long long start = unit_key(days[index].date, spec.unit);
                std::size_t end = index + 1;
                while (end < days.size() &&
                       unit_key(days[end].date, spec.unit) - start < tier.spec.count) {
                    ++end;
                }
                TierCell cell;
                cell.x = days[index].x;
                cell.width = days[end - 1].x + days[end - 1].width - cell.x;
                cell.label = tier_label(days[index].date, tier.spec);
                tier.cells.push_back(std::move(cell));
                index = end;
            }
        }
        tiers.push_back(std::move(tier));
    };

    if (file) {
        if (style_->top.unit != TimescaleUnit::none) {
            add(style_->top, TextStyleItem::top_timescale);
        }
        if (style_->middle.unit != TimescaleUnit::none) {
            add(style_->middle, TextStyleItem::middle_timescale);
        }
        if (style_->bottom.unit != TimescaleUnit::none) {
            add(style_->bottom, TextStyleItem::bottom_timescale);
        }
    }
    if (tiers.empty()) {
        // The measured stock scale: weekly labels over single-letter days.
        TimescaleTier weekly;
        weekly.unit = TimescaleUnit::weeks;
        weekly.count = 1;
        weekly.format = 13;
        weekly.align = 0;
        add(weekly, TextStyleItem::middle_timescale);
        TimescaleTier daily;
        daily.unit = TimescaleUnit::days;
        daily.count = 1;
        daily.format = 20;
        daily.align = 1;
        add(daily, TextStyleItem::bottom_timescale);
    }
    return tiers;
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
        // Wave 25 (W25-2/S2): a link takes the predecessor's scheduling style;
        // red only when Critical Tasks highlighting is on.
        // A manual link does not change with highlighting (measured); only an
        // auto critical link turns red.
        const QColor link_colour =
            link.manual ? kLinkManual
                        : (link.critical && critical_highlight_ ? kLinkCritical : kLinkAuto);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(link_colour, 1.0));
        painter.drawPolyline(polyline);

        QPolygonF arrow;
        arrow << QPointF(link.arrow.tip.x, link.arrow.tip.y + body_top)
              << QPointF(link.arrow.wing[0].x, link.arrow.wing[0].y + body_top)
              << QPointF(link.arrow.wing[1].x, link.arrow.wing[1].y + body_top);
        painter.setPen(Qt::NoPen);
        painter.setBrush(link_colour);
        painter.drawPolygon(arrow);
    }

    // Bars, completion fills, summary caps and milestones, all from the layout.
    for (const core::gantt::Bar& bar : layout_.bars) {
        const QRectF bar_rect(bar.rect.x, bar.rect.y + body_top, bar.rect.w, bar.rect.h);
        const QColor base_colour = bar_colour_for(bar);
        painter.setPen(QPen(kBarOutline, 1.0));
        painter.setBrush(base_colour);

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
                painter.setBrush(base_colour);
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
            } else if (bar.fill.w > 0.0 && !(bar.critical && critical_highlight_)) {
                // The completion fill.  Wave 26 (W26-3.2): the FILE's Progress
                // style supplies the colour, and the Finished style takes over
                // when the bar is complete; a stock fallback keeps the wave-21
                // blue.  A summary's progress is the thin line along the top,
                // which is how the capture shows it; a task's is the filled
                // portion.  With Critical Tasks highlighting ON the fill is
                // suppressed so a critical bar stays red along its whole
                // length (wave 21 decision 4).
                QColor fill_colour = kBarComplete;
                if (palette_.decoded && palette_.has_progress) {
                    fill_colour = to_qcolor(palette_.progress);
                }
                const bool complete = bar.fill.w >= bar.rect.w - 0.5;
                if (palette_.decoded && complete && palette_.has_finished) {
                    fill_colour = to_qcolor(palette_.finished);
                }
                if (bar.kind == core::gantt::BarKind::summary) {
                    const double line_height = std::min(3.0, bar.rect.h);
                    painter.fillRect(
                        QRectF(bar.fill.x, bar.fill.y + body_top, bar.fill.w,
                               line_height),
                        fill_colour);
                } else {
                    painter.fillRect(
                        QRectF(bar.fill.x, bar.fill.y + body_top, bar.fill.w, bar.fill.h),
                        fill_colour);
                }
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

        // The bar label.  The model decides WHETHER and WHAT to label; the
        // shell decides WHERE, because only the shell has the real font.  The
        // model measures with a 7 px/char fallback (GanttStyle::
        // label_char_width), so its rect is a conservative guess: eliding to it
        // cut "Task A" to "Ta..." even inside a 5-day bar.  MS Project draws a
        // short label inside the bar and a long one to its right, so:
        //   * inside (centred, white) when the label plus kBarLabelPadding on
        //     each side fits the bar -- "Task A" (~36 px) fits a 5-day bar at
        //     24 px/day (120 px) with 4 px of padding either side;
        //   * otherwise to the right of the bar in body text, elided only when
        //     it would run past the canvas edge.
        if (bar.label.visible && !bar.label_text.empty()) {
            const QFontMetricsF metrics(font());
            const QString text = QString::fromStdString(bar.label_text);
            const double text_width = metrics.horizontalAdvance(text);
            const double bar_left = bar.rect.x;
            const double bar_right = bar.rect.x + bar.rect.w;
            const bool milestone = bar.shape == core::gantt::BarShape::diamond ||
                                   bar.kind == core::gantt::BarKind::milestone;
            const bool fits_inside =
                !milestone && text_width + 2.0 * kBarLabelPadding <= bar.rect.w;
            double label_x = 0.0;
            double label_width = 0.0;
            if (fits_inside) {
                label_x = bar_left + (bar.rect.w - text_width) / 2.0;
                label_width = text_width;
            } else {
                label_x = bar_right + kBarLabelPadding;
                label_width = std::min(text_width,
                                       static_cast<double>(canvas_width) - label_x -
                                           kBarLabelPadding);
            }
            if (label_width > 1.0 && label_x < static_cast<double>(canvas_width)) {
                const QRectF label_rect(label_x, bar.rect.y + body_top, label_width,
                                        bar.rect.h);
                const QString shown = text_width > label_width
                                          ? metrics.elidedText(text, Qt::ElideRight,
                                                               label_width)
                                          : text;
                painter.setPen(fits_inside ? QColor(0xff, 0xff, 0xff) : kBodyText);
                painter.drawText(label_rect,
                                 static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                                 shown);
            }
        }

        // Wave 29 (W29-1.3): the labels the applied view's stored bar styles
        // call for.  The model has placed each one; the shell only aligns and
        // clips the text inside its rect.  A label entirely left of the canvas
        // is not drawn: the clean capture shows no left label on the first
        // bar, whose Summary text would sit before x = 0.
        for (const core::gantt::BarText& text : bar.texts) {
            if (text.text.empty() || text.rect.w <= 1.0) {
                continue;
            }
            const QRectF rect(text.rect.x, text.rect.y + body_top, text.rect.w,
                              text.rect.h);
            if (rect.right() <= 0.0 ||
                rect.left() >= static_cast<double>(canvas_width)) {
                continue;
            }
            // Wave 29 (W29-2.1): the Text Styles dialog's Bar Text - Left/
            // Right/Top/Bottom/Inside item, in the file's order.  The font's
            // family, size and four effect bits, the foreground and (when the
            // stored pattern is not Hollow) a solid background fill all come
            // from the selected style; a style the record left automatic keeps
            // the measured stock presentation.
            using msproject::viewstate::TextStyle;
            using msproject::viewstate::TextStyleItem;
            TextStyleItem item = TextStyleItem::bar_text_left;
            switch (text.side) {
                case core::gantt::BarLabelSide::left:
                    item = TextStyleItem::bar_text_left;
                    break;
                case core::gantt::BarLabelSide::right:
                    item = TextStyleItem::bar_text_right;
                    break;
                case core::gantt::BarLabelSide::top:
                    item = TextStyleItem::bar_text_top;
                    break;
                case core::gantt::BarLabelSide::bottom:
                    item = TextStyleItem::bar_text_bottom;
                    break;
                case core::gantt::BarLabelSide::inside:
                    item = TextStyleItem::bar_text_inside;
                    break;
            }
            const TextStyle* text_style = nullptr;
            if (style_ != nullptr && style_->decoded) {
                const auto index = static_cast<std::size_t>(item);
                if (index < style_->text.size() && style_->text[index].valid) {
                    text_style = &style_->text[index];
                }
            }
            QFont label_font = font();
            if (text_style != nullptr) {
                if (!text_style->font_name.empty()) {
                    label_font.setFamily(QString::fromStdString(text_style->font_name));
                }
                if (text_style->point_size > 0) {
                    label_font.setPointSize(text_style->point_size);
                }
                label_font.setBold(text_style->bold);
                label_font.setItalic(text_style->italic);
                label_font.setUnderline(text_style->underline);
                label_font.setStrikeOut(text_style->strikethrough);
            }
            if (text_style != nullptr && text_style->has_background &&
                text_style->background_pattern != 0) {
                painter.fillRect(rect, QColor(text_style->background.red,
                                              text_style->background.green,
                                              text_style->background.blue));
            }
            painter.setFont(label_font);
            const QFontMetricsF metrics(label_font);
            QString shown = QString::fromStdString(text.text);
            const double available =
                rect.right() > static_cast<double>(canvas_width)
                    ? static_cast<double>(canvas_width) - rect.left()
                    : rect.width();
            if (available > 0.0 && metrics.horizontalAdvance(shown) > available) {
                shown = metrics.elidedText(shown, Qt::ElideRight, available);
            }
            int flags = static_cast<int>(Qt::AlignVCenter);
            switch (text.side) {
                case core::gantt::BarLabelSide::left:
                    flags |= static_cast<int>(Qt::AlignRight);
                    break;
                case core::gantt::BarLabelSide::right:
                    flags |= static_cast<int>(Qt::AlignLeft);
                    break;
                case core::gantt::BarLabelSide::top:
                case core::gantt::BarLabelSide::bottom:
                case core::gantt::BarLabelSide::inside:
                    flags |= static_cast<int>(Qt::AlignHCenter);
                    break;
            }
            QColor pen = text.inside ? QColor(0xff, 0xff, 0xff) : kBodyText;
            if (text_style != nullptr && text_style->has_colour) {
                pen = QColor(text_style->colour.red, text_style->colour.green,
                             text_style->colour.blue);
            }
            painter.setPen(pen);
            painter.drawText(rect, flags, shown);
        }
    }

    paint_header(painter, days, weeks, canvas_width);

    // The status-date marker, when a layout carries one (this wave passes none).
    if (layout_.has_status_line()) {
        const int x = pixel(layout_.status_x);
        // Wave 25 review: a dedicated status colour, not the critical-bar red.
        painter.setPen(QPen(kStatusLine, 1.0));
        painter.drawLine(x, body_top, x, canvas_height);
    }

    // The split's own divider.
    painter.setPen(QPen(kGridLineStrong, 1.0));
    painter.drawLine(0, 0, 0, canvas_height);
}

void GanttCanvas::paint_header(QPainter& painter, const std::vector<DayCell>& days,
                               const std::vector<WeekCell>& weeks, int canvas_width) const {
    using msproject::viewstate::TimescaleUnit;
    const std::vector<HeaderTier> tiers = header_tiers(days, weeks);
    if (tiers.empty()) {
        return;
    }
    const int tier_height =
        std::max(1, kGanttHeaderHeight / static_cast<int>(tiers.size()));
    int top = 0;
    for (std::size_t tier_index = 0; tier_index < tiers.size(); ++tier_index) {
        const HeaderTier& tier = tiers[tier_index];
        painter.fillRect(QRect(0, top, canvas_width, tier_height),
                         tier_index == 0 ? kHeaderMajor : kHeaderMinor);

        QFont tier_font = font();
        if (tier.has_text_style) {
            if (!tier.text.font_name.empty()) {
                tier_font.setFamily(QString::fromStdString(tier.text.font_name));
            }
            if (tier.text.point_size > 0) {
                tier_font.setPointSize(tier.text.point_size);
            }
            tier_font.setBold(tier.text.bold);
            tier_font.setItalic(tier.text.italic);
        } else {
            tier_font.setPointSize(kTimescalePointSize);
        }
        painter.setFont(tier_font);
        QColor text_colour = kGanttHeaderText;
        if (tier.has_text_style && tier.text.has_colour) {
            text_colour = QColor(tier.text.colour.red, tier.text.colour.green,
                                 tier.text.colour.blue);
        }
        const QFontMetrics metrics(painter.font());

        for (const TierCell& cell : tier.cells) {
            const int x = pixel(cell.x);
            const int w = std::max(1, pixel(cell.x + cell.width) - x);
            if (tier.spec.unit == TimescaleUnit::days) {
                if (cell.weekday == 0 || cell.weekday == 6) {
                    painter.fillRect(QRect(x, top, w, tier_height), kWeekend);
                }
                painter.setPen(QPen(cell.weekday == 1 ? kGridLineStrong : kGridLine, 1));
                painter.drawLine(x, top, x, top + tier_height);
                if (cell.weekday >= 0 && w >= 4) {
                    painter.setPen(text_colour);
                    painter.drawText(QRect(x, top, w, tier_height), Qt::AlignCenter,
                                     QString::fromStdString(cell.label));
                }
                continue;
            }
            painter.setPen(QPen(kGridLineStrong, 1));
            painter.drawLine(x, top, x, top + tier_height);
            const int text_x = x + kLabelInset;
            const int available = w - 2 * kLabelInset;
            if (available <= 4 || cell.label.empty()) {
                continue;
            }
            Qt::Alignment alignment = Qt::AlignLeft;
            if (tier.spec.align == 1) {
                alignment = Qt::AlignHCenter;
            } else if (tier.spec.align == 2) {
                alignment = Qt::AlignRight;
            }
            const QString text = metrics.elidedText(QString::fromStdString(cell.label),
                                                    Qt::ElideRight, available);
            painter.setPen(text_colour);
            painter.drawText(QRect(text_x, top, available, tier_height),
                             static_cast<int>(alignment | Qt::AlignVCenter), text);
        }
        painter.setPen(QPen(kGridLineStrong, 1));
        painter.drawLine(0, top + tier_height - 1, canvas_width,
                         top + tier_height - 1);
        top += tier_height;
    }
}

}  // namespace msqt
