// core/gantt/gantt.cpp -- timescale, rows, bars, link routing and hit testing.
//
// No drawing and no windowing system: every function here returns data.
// Decisions with no oracle evidence are marked "decision:" in gantt.h and
// collected in memory/gantt.md.
#include "gantt.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

namespace core::gantt {
namespace {

using core::sched::add_days;
using core::sched::CivilDate;
using core::sched::date_only;
using core::sched::day_of;
using core::sched::from_ymd;
using core::sched::is_overflow;
using core::sched::kTenthsPerDay;
using core::sched::pack;
using core::sched::time_of;
using core::sched::to_civil;
using core::sched::weekday;
using core::sched::weekday_of_day;

constexpr const char* kMonthNames[12] = {"January",   "February", "March",    "April",
                                         "May",       "June",     "July",     "August",
                                         "September", "October",  "November", "December"};
constexpr const char* kMonthShort[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// A ceiling on generated header cells / shaded days for one call.  Guards a
// runaway loop if a caller asks for an absurd x window; 20,000 is far more than
// a real viewport can show.
constexpr std::size_t kMaxGeneratedCells = 20000;

constexpr double kEpsilon = 1e-9;

double quiet_nan() noexcept { return std::numeric_limits<double>::quiet_NaN(); }

// Proleptic Gregorian, exactly the calendar core::sched::to_civil/from_ymd use.
int days_in_month(int year, int month) noexcept {
    static constexpr int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2) {
        const bool leap = (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
        return leap ? 29 : 28;
    }
    return kDays[month - 1];
}

std::int64_t month_index(int year, int month) noexcept {
    return static_cast<std::int64_t>(year) * 12 + (month - 1);
}

struct YearMonth {
    int year = 0;
    int month = 1;
};

YearMonth year_month_from_index(std::int64_t index) noexcept {
    std::int64_t year = index / 12;
    std::int64_t month = index % 12;
    if (month < 0) {
        month += 12;
        --year;
    }
    return YearMonth{static_cast<int>(year), static_cast<int>(month + 1)};
}

int normalized_week_start(int week_start) noexcept {
    int ws = week_start % 7;
    if (ws < 0) {
        ws += 7;
    }
    return ws;
}

date32 month_start_of(date32 day) {
    const CivilDate civil = to_civil(day);
    return from_ymd(civil.year, civil.month, 1);
}

date32 next_month_start(date32 month_start) {
    const CivilDate civil = to_civil(month_start);
    if (civil.month == 12) {
        return from_ymd(civil.year + 1, 1, 1);
    }
    return from_ymd(civil.year, civil.month + 1, 1);
}

// Integer day offset from `origin`, clamped so a pathological x cannot overflow
// the int arithmetic inside date32 helpers.
int clamped_day_offset(double offset) noexcept {
    if (!(offset > -1000000.0)) {
        return -1000000;
    }
    if (offset > 1000000.0) {
        return 1000000;
    }
    return static_cast<int>(offset);
}

double clamp_unit(double value) noexcept {
    if (!(value > 0.0)) {  // also catches NaN
        return 0.0;
    }
    return value > 1.0 ? 1.0 : value;
}

std::size_t utf8_length(std::string_view text) noexcept {
    std::size_t count = 0;
    for (const char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

double measure_label(std::string_view text, const GanttStyle& style) {
    double width = 0.0;
    if (style.measure_text) {
        width = style.measure_text(text);
    } else {
        width = static_cast<double>(utf8_length(text)) * style.label_char_width;
    }
    return (std::isfinite(width) && width > 0.0) ? width : 0.0;
}

void place_label(Bar& bar, const GanttStyle& style) {
    LabelPlacement& placement = bar.label;
    placement = LabelPlacement{};
    if (bar.label_text.empty()) {
        return;
    }
    const double text_width = measure_label(bar.label_text, style);
    placement.visible = true;
    placement.rect.h = bar.rect.h;
    placement.rect.y = bar.rect.y;
    placement.rect.w = text_width;

    // decision: a label fits "inside" only when the text plus padding is no
    // wider than the bar.  Milestones never take an inside label (the diamond
    // is 12 px), matching the product's right-of-bar milestone labels.
    const bool fits_inside =
        bar.kind != BarKind::milestone && text_width + 2.0 * style.label_gap <= bar.rect.w;
    if (fits_inside) {
        placement.inside = true;
        placement.rect.x = bar.rect.x + (bar.rect.w - text_width) / 2.0;
    } else {
        placement.inside = false;
        placement.rect.x = bar.rect.right() + style.label_gap;
    }
}

// Removes coincident consecutive points so the polyline contract in gantt.h
// holds (and so the arrow's final segment has a real direction).
std::vector<Point> clean_polyline(std::vector<Point> points) {
    std::vector<Point> cleaned;
    cleaned.reserve(points.size());
    for (const Point& point : points) {
        if (cleaned.empty()) {
            cleaned.push_back(point);
            continue;
        }
        const Point& last = cleaned.back();
        if (std::abs(point.x - last.x) + std::abs(point.y - last.y) > kEpsilon) {
            cleaned.push_back(point);
        }
    }
    return cleaned;
}

Rect bounds_of(const std::vector<Point>& points) {
    if (points.empty()) {
        return Rect{};
    }
    double min_x = points.front().x;
    double max_x = points.front().x;
    double min_y = points.front().y;
    double max_y = points.front().y;
    for (const Point& point : points) {
        min_x = std::min(min_x, point.x);
        max_x = std::max(max_x, point.x);
        min_y = std::min(min_y, point.y);
        max_y = std::max(max_y, point.y);
    }
    return Rect{min_x, min_y, max_x - min_x, max_y - min_y};
}

// ---------------------------------------------------------------------------
// Link routing.
//
// Attachment sides (decision, matching the product's convention):
//   FS: predecessor's finish (right) edge -> successor's start (left) edge
//   SS: predecessor's start (left) edge   -> successor's start (left) edge
//   FF: predecessor's finish (right) edge -> successor's finish (right) edge
//   SF: predecessor's start (left) edge   -> successor's finish (right) edge
//
// The normal shape is a Z: a stub/run along the source bar's centre line, a
// vertical in the clear, then a horizontal into the target edge.  When the
// target edge is "behind" the source edge (the brief's successor-left-of-
// predecessor case) the route detours through the inter-row lane at the
// midpoint of the two centre lines.  SF always detours: it must cross from the
// left of both bars to the right of the successor, and a straight run would cut
// through the successor bar.
//
// Backward FS and SF therefore share ONE six-point shape -- exit, stub on the
// exit side, lane, approach, entry -- differing only in which sides they use;
// the lane is the same midpoint rule however far apart the two rows are.  That
// consistency is pinned twice in the tests (adjacent rows and two rows apart).
// ---------------------------------------------------------------------------
LinkRoute route_link(const Bar& pred, const Bar& succ, const LinkInput& input,
                     const GanttStyle& style) {
    LinkRoute route;
    route.predecessor_uid = input.predecessor_uid;
    route.successor_uid = input.successor_uid;
    route.type = input.type;
    route.critical = input.critical;
    route.manual = input.manual;

    const double gap = style.link_gap > 0.0 ? style.link_gap : 0.0;
    const bool exit_left =
        input.type == LinkType::start_to_start || input.type == LinkType::start_to_finish;
    const bool enter_left =
        input.type == LinkType::finish_to_start || input.type == LinkType::start_to_start;

    const double y0 = pred.rect.center_y();
    const double y1 = succ.rect.center_y();
    const double exit_x = exit_left ? pred.rect.x : pred.rect.right();
    const double entry_x = enter_left ? succ.rect.x : succ.rect.right();
    const double stub_x = exit_x + (exit_left ? -gap : gap);
    const double approach_x = entry_x + (enter_left ? -gap : gap);
    // The lane is the gap between the two rows' centre lines.  If both bars are
    // on the same row (not possible for two tasks in a Gantt, but the API does
    // not forbid it) the lane moves just below both.
    const double lane_y = std::abs(y1 - y0) > kEpsilon
                              ? (y0 + y1) / 2.0
                              : std::max(pred.rect.bottom(), succ.rect.bottom()) + gap;

    std::vector<Point> points;
    switch (input.type) {
        case LinkType::finish_to_start:
            if (approach_x > exit_x) {
                points = {{exit_x, y0},
                          {approach_x, y0},
                          {approach_x, y1},
                          {entry_x, y1}};
            } else {
                points = {{exit_x, y0},
                          {stub_x, y0},
                          {stub_x, lane_y},
                          {approach_x, lane_y},
                          {approach_x, y1},
                          {entry_x, y1}};
            }
            break;
        case LinkType::start_to_start:
            if (entry_x > stub_x) {
                points = {{exit_x, y0}, {stub_x, y0}, {stub_x, y1}, {entry_x, y1}};
            } else {
                points = {{exit_x, y0},
                          {stub_x, y0},
                          {stub_x, lane_y},
                          {approach_x, lane_y},
                          {approach_x, y1},
                          {entry_x, y1}};
            }
            break;
        case LinkType::finish_to_finish:
            if (approach_x > exit_x) {
                points = {{exit_x, y0},
                          {approach_x, y0},
                          {approach_x, y1},
                          {entry_x, y1}};
            } else {
                points = {{exit_x, y0}, {stub_x, y0}, {stub_x, y1}, {entry_x, y1}};
            }
            break;
        case LinkType::start_to_finish:
            points = {{exit_x, y0},
                      {stub_x, y0},
                      {stub_x, lane_y},
                      {approach_x, lane_y},
                      {approach_x, y1},
                      {entry_x, y1}};
            break;
    }

    route.points = clean_polyline(std::move(points));
    if (route.points.size() < 2) {
        // Defensive only: a bar has positive size, so this is unreachable.
        route.points = {{exit_x, y0}, {entry_x, y1}};
    }

    const Point tip = route.points.back();
    const Point previous = route.points[route.points.size() - 2];
    double dx = tip.x - previous.x;
    double dy = tip.y - previous.y;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length > 0.0) {
        dx /= length;
        dy /= length;
    }
    const double arrow_length = style.arrow_length > 0.0 ? style.arrow_length : 0.0;
    const double arrow_width = style.arrow_width > 0.0 ? style.arrow_width : 0.0;
    const Point base{tip.x - dx * arrow_length, tip.y - dy * arrow_length};
    const Point perpendicular{-dy, dx};

    route.arrow.tip = tip;
    route.arrow.direction = Point{dx, dy};
    route.arrow.wing[0] = Point{base.x + perpendicular.x * arrow_width / 2.0,
                                base.y + perpendicular.y * arrow_width / 2.0};
    route.arrow.wing[1] = Point{base.x - perpendicular.x * arrow_width / 2.0,
                                base.y - perpendicular.y * arrow_width / 2.0};
    route.bounds = bounds_of(route.points);
    return route;
}

// x of a boundary inside a day, given tenths of a minute since midnight,
// 0..14400.  `pack(day, 14400)` normalises to the next day 00:00, which is how
// the axis maps the end of a day column (month columns included).
double x_at_day_tenths(const Timescale& scale, date32 day, std::int32_t tenths) {
    return scale.x_for_date(pack(day_of(day), tenths));
}

// Append the NON-WORKING x spans of one calendar day: the complement of its
// working intervals inside [00:00, 24:00).  A day with no intervals is one
// whole-day span (partial == false); a partly working day yields one partial
// span per unworked stretch -- a 4-hour Friday 08:00-12:00 gives 00:00-08:00 and
// 12:00-24:00.  Spans that end at or before x0 are dropped (the shell clips the
// rest).
void append_calendar_day_spans(std::vector<NonworkingSpan>& spans, const Timescale& scale,
                               date32 day, const core::sched::DayPattern& pattern,
                               double x0) {
    // core::sched stores intervals in minutes from midnight (§1.1).  Convert to
    // the date32 time unit (tenths of a minute) and drop impossible records.
    std::array<std::pair<std::int32_t, std::int32_t>, core::sched::kMaxDayIntervals> working{};
    std::size_t count = 0;
    for (std::size_t i = 0;
         i < static_cast<std::size_t>(pattern.count) && i < working.size(); ++i) {
        const std::int32_t start =
            static_cast<std::int32_t>(pattern.start_minute[i]) * core::sched::kTenthsPerMinute;
        const std::int32_t length = static_cast<std::int32_t>(pattern.length_minutes[i]) *
                                    core::sched::kTenthsPerMinute;
        if (start >= kTenthsPerDay || length <= 0) {
            continue;
        }
        working[count++] = {start, std::min(start + length, kTenthsPerDay)};
    }
    std::sort(working.begin(), working.begin() + static_cast<std::ptrdiff_t>(count));

    auto emit = [&spans, &scale, day, x0](std::int32_t from, std::int32_t to) {
        if (to <= from) {
            return;
        }
        const double x = x_at_day_tenths(scale, day, from);
        const double x_end = x_at_day_tenths(scale, day, to);
        if (!std::isfinite(x) || !std::isfinite(x_end) || !(x_end > x) || !(x_end > x0)) {
            return;
        }
        NonworkingSpan span;
        span.date = day;
        span.x = x;
        span.width = x_end - x;
        span.partial = !(from == 0 && to == kTenthsPerDay);
        spans.push_back(span);
    };

    std::int32_t cursor = 0;  // end of the working time seen so far
    for (std::size_t i = 0; i < count; ++i) {
        if (working[i].first > cursor) {
            emit(cursor, working[i].first);
        }
        cursor = std::max(cursor, working[i].second);
    }
    emit(cursor, kTenthsPerDay);
}

// A whole-day span.  Written out rather than brace-initialised so no compiler
// sees a partially initialised aggregate (-Wmissing-field-initializers).
NonworkingSpan whole_day_span(date32 day, double x, double width) {
    NonworkingSpan span;
    span.date = day;
    span.x = x;
    span.width = width;
    span.partial = false;
    return span;
}

// ISO date for a bar label; empty for NA.  decision (unmeasured): the product's
// bar-label date format is not in the resources, so use the file format's
// unambiguous YYYY-MM-DD rather than inventing a localised pattern.
std::string format_iso_date(date32 d) {
    if (is_na(d) || is_overflow(d)) {
        return {};
    }
    const CivilDate civil = to_civil(d);
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", civil.year, civil.month,
                  civil.day);
    return buffer;
}

std::string format_percent(double value) {
    double clamped = value;
    if (!(clamped > 0.0)) {
        clamped = 0.0;
    }
    if (clamped > 1.0) {
        clamped = 1.0;
    }
    const int percent = static_cast<int>(clamped * 100.0 + 0.5);
    return std::to_string(percent) + "%";
}

std::string combine_text(std::string left, const std::string& right) {
    if (left.empty()) {
        return right;
    }
    if (right.empty()) {
        return left;
    }
    left += ' ';
    left += right;
    return left;
}

std::string task_date_text(const TaskBarInput& task) {
    const std::string start = format_iso_date(task.start);
    date32 finish = task.finish;
    if (is_na(finish) || is_overflow(finish)) {
        finish = task.start;
    }
    const std::string end = format_iso_date(finish);
    if (start.empty()) {
        return end;
    }
    if (end.empty() || end == start) {
        return start;
    }
    return start + " - " + end;
}

}  // namespace

// ---------------------------------------------------------------------------
// Points / rects
// ---------------------------------------------------------------------------

double distance(const Point& a, const Point& b) noexcept {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

double distance_to_segment(const Point& p, const Point& a, const Point& b) noexcept {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length_squared = dx * dx + dy * dy;
    if (!(length_squared > 0.0)) {
        return distance(p, a);
    }
    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / length_squared;
    t = std::clamp(t, 0.0, 1.0);
    const Point projection{a.x + t * dx, a.y + t * dy};
    return distance(p, projection);
}

// ---------------------------------------------------------------------------
// Timescale
// ---------------------------------------------------------------------------

date32 Timescale::unit_start(date32 d) const {
    if (is_na(d) || is_overflow(d)) {
        return kDateNa;
    }
    const date32 day = date_only(d);
    switch (unit) {
        case TimescaleUnit::day:
            return day;
        case TimescaleUnit::week: {
            const int weekday_index = weekday(day);
            if (weekday_index < 0) {
                return day;
            }
            const int start = normalized_week_start(week_start);
            const int delta = ((weekday_index - start) % 7 + 7) % 7;
            return add_days(day, -delta);
        }
        case TimescaleUnit::month:
            return month_start_of(day);
    }
    return day;
}

void Timescale::snap_origin(date32 earliest) {
    if (is_na(origin) || is_overflow(origin)) {
        origin = core::sched::from_ymd(1984, 1, 2);
    }
    date32 candidate = origin;
    if (!is_na(earliest) && !is_overflow(earliest) && earliest < candidate) {
        candidate = earliest;
    }
    const date32 snapped = unit_start(candidate);
    // Guard the epoch edge: a week unit asked to contain 1984-01-01 (a Sunday)
    // would snap to 1983-12-26, which date32 cannot represent.  Keep the old
    // origin in that case rather than wrapping the day field.
    if (!is_na(snapped) && !is_overflow(snapped) &&
        core::sched::day_of(snapped) >= core::sched::kMinDay) {
        origin = snapped;
    }
}

double Timescale::x_for_date(date32 d) const {
    if (!valid() || is_na(d) || is_overflow(d)) {
        return quiet_nan();
    }
    const double time_fraction =
        static_cast<double>(time_of(d)) / static_cast<double>(kTenthsPerDay);
    const double day_offset = static_cast<double>(day_of(d) - day_of(origin));
    switch (unit) {
        case TimescaleUnit::day:
            return (day_offset + time_fraction) * pixels_per_unit;
        case TimescaleUnit::week:
            return (day_offset + time_fraction) * (pixels_per_unit / 7.0);
        case TimescaleUnit::month: {
            const CivilDate civil = to_civil(d);
            const CivilDate origin_civil = to_civil(origin);
            const std::int64_t index = month_index(civil.year, civil.month);
            const std::int64_t origin_index = month_index(origin_civil.year, origin_civil.month);
            const double day_fraction =
                (static_cast<double>(civil.day - 1) + time_fraction) /
                static_cast<double>(days_in_month(civil.year, civil.month));
            return (static_cast<double>(index - origin_index) + day_fraction) *
                   pixels_per_unit;
        }
    }
    return quiet_nan();
}

double Timescale::x_for_day_start(date32 d) const {
    if (is_na(d) || is_overflow(d)) {
        return quiet_nan();
    }
    return x_for_date(date_only(d));
}

date32 Timescale::date_at_x(double x) const {
    if (!valid() || !std::isfinite(x)) {
        return kDateNa;
    }
    switch (unit) {
        case TimescaleUnit::day: {
            const double offset = std::floor(x / pixels_per_unit + kEpsilon);
            const int day_offset = clamped_day_offset(offset);
            if (core::sched::day_of(origin) + day_offset < core::sched::kMinDay) {
                return kDateNa;  // before date32's first representable day
            }
            return date_only(add_days(origin, day_offset));
        }
        case TimescaleUnit::week: {
            const double day_width = pixels_per_unit / 7.0;
            const double offset = std::floor(x / day_width + kEpsilon);
            const int day_offset = clamped_day_offset(offset);
            if (core::sched::day_of(origin) + day_offset < core::sched::kMinDay) {
                return kDateNa;
            }
            return date_only(add_days(origin, day_offset));
        }
        case TimescaleUnit::month: {
            const double months = x / pixels_per_unit;
            const double base_float = std::floor(months + kEpsilon);
            if (base_float < -10000000.0 || base_float > 10000000.0) {
                return kDateNa;  // no representable year that far out
            }
            const std::int64_t base = static_cast<std::int64_t>(base_float);
            double fraction = months - base_float;
            if (fraction < 0.0) {
                fraction = 0.0;
            }
            const CivilDate origin_civil = to_civil(origin);
            const std::int64_t origin_index =
                month_index(origin_civil.year, origin_civil.month);
            const YearMonth target =
                year_month_from_index(origin_index + static_cast<std::int64_t>(base));
            if (target.year < 1984) {
                return kDateNa;  // date32 day 1 is 1984-01-01
            }
            const int dim = days_in_month(target.year, target.month);
            int day = 1 + static_cast<int>(
                              std::floor(fraction * static_cast<double>(dim) + kEpsilon));
            day = std::clamp(day, 1, dim);
            return from_ymd(target.year, target.month, day);
        }
    }
    return kDateNa;
}

std::vector<HeaderCell> Timescale::header(HeaderTier tier, double x0, double x1) const {
    std::vector<HeaderCell> cells;
    if (!valid() || !std::isfinite(x0) || !std::isfinite(x1) || !(x1 > x0)) {
        return cells;
    }
    const date32 first = date_at_x(x0);
    if (is_na(first) || is_overflow(first)) {
        return cells;
    }

    if (tier == HeaderTier::minor) {
        switch (unit) {
            case TimescaleUnit::day: {
                date32 d = date_only(first);
                while (cells.size() < kMaxGeneratedCells) {
                    const double x = x_for_day_start(d);
                    if (!std::isfinite(x) || x >= x1) {
                        break;
                    }
                    const double next_x = x_for_day_start(add_days(d, 1));
                    const CivilDate civil = to_civil(d);
                    HeaderCell cell;
                    cell.label = std::to_string(civil.day);
                    cell.start = d;
                    cell.finish = d;
                    cell.x = x;
                    cell.width = next_x - x;
                    cell.tier = tier;
                    cell.weekday = weekday(d);
                    cells.push_back(std::move(cell));
                    d = add_days(d, 1);
                }
                break;
            }
            case TimescaleUnit::week: {
                date32 d = unit_start(first);
                while (cells.size() < kMaxGeneratedCells) {
                    const double x = x_for_day_start(d);
                    if (!std::isfinite(x) || x >= x1) {
                        break;
                    }
                    const date32 next = add_days(d, 7);
                    const double next_x = x_for_day_start(next);
                    const CivilDate civil = to_civil(d);
                    HeaderCell cell;
                    cell.label = std::to_string(civil.day) + " " + kMonthShort[civil.month - 1];
                    cell.start = d;
                    cell.finish = add_days(d, 6);
                    cell.x = x;
                    cell.width = next_x - x;
                    cell.tier = tier;
                    cells.push_back(std::move(cell));
                    d = next;
                }
                break;
            }
            case TimescaleUnit::month: {
                date32 d = month_start_of(first);
                while (cells.size() < kMaxGeneratedCells) {
                    const double x = x_for_day_start(d);
                    if (!std::isfinite(x) || x >= x1) {
                        break;
                    }
                    const date32 next = next_month_start(d);
                    const double next_x = x_for_day_start(next);
                    const CivilDate civil = to_civil(d);
                    HeaderCell cell;
                    cell.label = kMonthNames[civil.month - 1];
                    cell.start = d;
                    cell.finish = add_days(next, -1);
                    cell.x = x;
                    cell.width = next_x - x;
                    cell.tier = tier;
                    cells.push_back(std::move(cell));
                    d = next;
                }
                break;
            }
        }
        return cells;
    }

    // Major tier: months over day/week columns, years over month columns.
    if (unit == TimescaleUnit::month) {
        CivilDate civil = to_civil(first);
        int year = civil.year;
        date32 d = from_ymd(year, 1, 1);
        while (cells.size() < kMaxGeneratedCells) {
            const double x = x_for_day_start(d);
            if (!std::isfinite(x) || x >= x1) {
                break;
            }
            const date32 next = from_ymd(year + 1, 1, 1);
            const double next_x = x_for_day_start(next);
            HeaderCell cell;
            cell.label = std::to_string(year);
            cell.start = d;
            cell.finish = add_days(next, -1);
            cell.x = x;
            cell.width = next_x - x;
            cell.tier = tier;
            cells.push_back(std::move(cell));
            d = next;
            ++year;
        }
        return cells;
    }

    date32 d = month_start_of(first);
    while (cells.size() < kMaxGeneratedCells) {
        const double x = x_for_day_start(d);
        if (!std::isfinite(x) || x >= x1) {
            break;
        }
        const date32 next = next_month_start(d);
        const double next_x = x_for_day_start(next);
        const CivilDate civil = to_civil(d);
        HeaderCell cell;
        cell.label = std::string(kMonthNames[civil.month - 1]) + " " +
                     std::to_string(civil.year);
        cell.start = d;
        cell.finish = add_days(next, -1);
        cell.x = x;
        cell.width = next_x - x;
        cell.tier = tier;
        cells.push_back(std::move(cell));
        d = next;
    }
    return cells;
}

double Timescale::pixels_per_day() const noexcept {
    if (!valid()) {
        return quiet_nan();
    }
    if (unit == TimescaleUnit::day) {
        return pixels_per_unit;
    }
    if (unit == TimescaleUnit::week) {
        return pixels_per_unit / 7.0;
    }
    return quiet_nan();
}

// ---------------------------------------------------------------------------
// Zoom levels and bar labels
// ---------------------------------------------------------------------------

ZoomSpec zoom_spec(GanttZoom zoom) noexcept {
    switch (zoom) {
        case GanttZoom::week:
            return ZoomSpec{GanttZoom::week, TimescaleUnit::week, 140.0, "week",
                            "week start (d mmm)", "month and year"};
        case GanttZoom::month:
            return ZoomSpec{GanttZoom::month, TimescaleUnit::month, 300.0, "month",
                            "month name", "year"};
        case GanttZoom::day:
        default:
            return ZoomSpec{GanttZoom::day, TimescaleUnit::day, 20.0, "day", "day number",
                            "month and year"};
    }
}

const char* zoom_name(GanttZoom zoom) noexcept { return zoom_spec(zoom).name; }

Timescale timescale_for_zoom(GanttZoom zoom, date32 origin) noexcept {
    const ZoomSpec spec = zoom_spec(zoom);
    Timescale scale;
    scale.unit = spec.unit;
    scale.pixels_per_unit = spec.pixels_per_unit;
    scale.origin = origin;
    return scale;
}

GanttZoom zoom_for_timescale(const Timescale& scale) noexcept {
    switch (scale.unit) {
        case TimescaleUnit::week: return GanttZoom::week;
        case TimescaleUnit::month: return GanttZoom::month;
        case TimescaleUnit::day:
        default: return GanttZoom::day;
    }
}

const char* bar_label_content_name(BarLabelContent content) noexcept {
    switch (content) {
        case BarLabelContent::none: return "none";
        case BarLabelContent::name: return "name";
        case BarLabelContent::resource: return "resource";
        case BarLabelContent::dates: return "dates";
        case BarLabelContent::percent: return "percent";
        case BarLabelContent::name_and_percent: return "name_and_percent";
        case BarLabelContent::name_and_resource: return "name_and_resource";
        case BarLabelContent::name_and_dates: return "name_and_dates";
    }
    return "name";
}

bool bar_label_content_from_name(std::string_view name, BarLabelContent& out) noexcept {
    if (name == "none") { out = BarLabelContent::none; return true; }
    if (name == "name") { out = BarLabelContent::name; return true; }
    if (name == "resource") { out = BarLabelContent::resource; return true; }
    if (name == "dates") { out = BarLabelContent::dates; return true; }
    if (name == "percent") { out = BarLabelContent::percent; return true; }
    if (name == "name_and_percent") { out = BarLabelContent::name_and_percent; return true; }
    if (name == "name_and_resource") { out = BarLabelContent::name_and_resource; return true; }
    if (name == "name_and_dates") { out = BarLabelContent::name_and_dates; return true; }
    return false;
}

std::string bar_label_text(const TaskBarInput& task, const GanttStyle& style) {
    switch (style.bar_label) {
        case BarLabelContent::none:
            return {};
        case BarLabelContent::name:
            return task.label;
        case BarLabelContent::resource:
            return task.resource;
        case BarLabelContent::dates:
            return task_date_text(task);
        case BarLabelContent::percent:
            return format_percent(task.percent_complete);
        case BarLabelContent::name_and_percent:
            return combine_text(task.label, format_percent(task.percent_complete));
        case BarLabelContent::name_and_resource:
            return combine_text(task.label, task.resource);
        case BarLabelContent::name_and_dates:
            return combine_text(task.label, task_date_text(task));
    }
    return task.label;
}

double hit_test_tolerance(const Timescale& timescale, const GanttStyle& style) noexcept {
    const double fixed = style.hit_tolerance > 0.0 ? style.hit_tolerance : 0.0;
    if (!(style.hit_tolerance_days > 0.0)) {
        return fixed;
    }
    double day_width = timescale.pixels_per_day();
    if (!std::isfinite(day_width)) {
        if (!timescale.valid()) {
            return fixed;
        }
        // Month columns are equal-width, so use the first day's width as the
        // representative one.  x_for_day_start handles the piecewise mapping.
        day_width = timescale.x_for_day_start(add_days(timescale.origin, 1)) -
                    timescale.x_for_day_start(timescale.origin);
    }
    if (!std::isfinite(day_width) || !(day_width > 0.0)) {
        return fixed;
    }
    double tolerance = day_width * style.hit_tolerance_days;
    if (style.min_hit_tolerance > 0.0 && tolerance < style.min_hit_tolerance) {
        tolerance = style.min_hit_tolerance;
    }
    if (style.max_hit_tolerance > 0.0 && tolerance > style.max_hit_tolerance) {
        tolerance = style.max_hit_tolerance;
    }
    return tolerance;
}

// ---------------------------------------------------------------------------
// Rows
// ---------------------------------------------------------------------------

int RowLayout::row_at_y(double y) const noexcept {
    if (!(row_height > 0.0) || !std::isfinite(y) || y < 0.0) {
        return -1;
    }
    const double row = std::floor(y / row_height);
    if (!(row >= 0.0) || row >= static_cast<double>(std::numeric_limits<int>::max())) {
        return -1;
    }
    return static_cast<int>(row);
}

RowRange RowLayout::visible_rows(double scroll_y, double viewport_height,
                                 int row_count) const noexcept {
    if (!(row_height > 0.0) || !(viewport_height > 0.0) || row_count <= 0 ||
        !std::isfinite(scroll_y) || !std::isfinite(viewport_height)) {
        return RowRange{};
    }
    const double top = std::max(0.0, scroll_y);
    const double first_float = std::floor(top / row_height);
    const double last_float = std::ceil((scroll_y + viewport_height) / row_height);

    int first = 0;
    if (first_float >= static_cast<double>(row_count)) {
        first = row_count;
    } else if (first_float > 0.0) {
        first = static_cast<int>(first_float);
    }
    int last = 0;
    if (last_float <= 0.0) {
        last = 0;
    } else if (last_float >= static_cast<double>(row_count)) {
        last = row_count;
    } else {
        last = static_cast<int>(last_float);
    }
    if (last < first) {
        last = first;
    }
    return RowRange{first, last - first};
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void GanttSource::set_calendar(const core::sched::Calendar& source_calendar) {
    // Copy so the layout can keep the predicate alive after this source is gone.
    calendar = std::make_shared<const core::sched::Calendar>(source_calendar);
    working_day = nullptr;  // the calendar supersedes a whole-day predicate
}

const Bar* GanttLayout::bar(std::uint32_t uid) const {
    for (const Bar& candidate : bars) {
        if (candidate.task_uid == uid) {
            return &candidate;
        }
    }
    return nullptr;
}

HitResult GanttLayout::hit_test(double x, double y) const {
    HitResult result;
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return result;
    }
    const double tolerance = hit_test_tolerance(timescale, style);
    for (const Bar& candidate : bars) {
        if (!candidate.rect.contains(x, y, tolerance)) {
            continue;
        }
        result.kind = HitKind::bar;
        result.task_uid = candidate.task_uid;
        result.critical = candidate.critical;
        return result;
    }
    for (std::size_t index = 0; index < links.size(); ++index) {
        const LinkRoute& link = links[index];
        if (link.points.size() < 2) {
            continue;
        }
        double best = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i + 1 < link.points.size(); ++i) {
            best = std::min(best, distance_to_segment(Point{x, y}, link.points[i],
                                                      link.points[i + 1]));
        }
        if (best <= tolerance) {
            result.kind = HitKind::link;
            result.link_index = index;
            result.predecessor_uid = link.predecessor_uid;
            result.successor_uid = link.successor_uid;
            result.link_type = link.type;
            result.critical = link.critical;
            return result;
        }
    }
    return result;
}

bool GanttLayout::is_working_day(date32 day) const {
    if (is_na(day) || is_overflow(day)) {
        return false;
    }
    const date32 whole_day = date_only(day);
    if (std::binary_search(nonworking_days.begin(), nonworking_days.end(), whole_day)) {
        return false;  // a forced non-working day wins over every source
    }
    if (calendar != nullptr) {
        return calendar->day_total_ticks(day_of(whole_day)) > 0;
    }
    if (working_day) {
        return working_day(whole_day);
    }
    return working_week.is_working(weekday(whole_day));
}

std::vector<NonworkingSpan> GanttLayout::nonworking_spans(double x0, double x1) const {
    std::vector<NonworkingSpan> spans;
    if (!timescale.valid() || !std::isfinite(x0) || !std::isfinite(x1) || !(x1 > x0)) {
        return spans;
    }
    const date32 first = timescale.date_at_x(x0);
    if (is_na(first) || is_overflow(first)) {
        return spans;
    }
    date32 d = date_only(first);
    while (spans.size() < kMaxGeneratedCells) {
        const double x = timescale.x_for_day_start(d);
        if (!std::isfinite(x) || x >= x1) {
            break;
        }
        const double next_x = timescale.x_for_day_start(add_days(d, 1));
        const bool forced = std::binary_search(nonworking_days.begin(),
                                               nonworking_days.end(), d);
        if (forced) {
            spans.push_back(whole_day_span(d, x, next_x - x));
        } else if (calendar != nullptr) {
            append_calendar_day_spans(spans, timescale, d,
                                      calendar->pattern_for_day(day_of(d)), x0);
        } else if (working_day) {
            if (!working_day(d)) {
                spans.push_back(whole_day_span(d, x, next_x - x));
            }
        } else if (!working_week.is_working(weekday(d))) {
            spans.push_back(whole_day_span(d, x, next_x - x));
        }
        d = add_days(d, 1);
    }
    return spans;
}

GanttLayout build_gantt(const GanttSource& source, const GanttStyle& style,
                        const Timescale& timescale) {
    GanttLayout layout;
    layout.style = style;
    if (!(layout.style.row_height > 0.0)) {
        layout.style.row_height = 22.0;
    }
    layout.rows.row_height = layout.style.row_height;
    layout.timescale = timescale;
    layout.row_count = static_cast<int>(source.tasks.size());
    layout.working_week = source.working_week;
    layout.status_date = source.status_date;
    layout.calendar = source.calendar;
    layout.working_day = source.working_day;

    layout.nonworking_days.reserve(source.nonworking_days.size());
    for (const date32 day : source.nonworking_days) {
        if (is_na(day) || is_overflow(day)) {
            continue;
        }
        layout.nonworking_days.push_back(date_only(day));
    }
    std::sort(layout.nonworking_days.begin(), layout.nonworking_days.end());
    layout.nonworking_days.erase(
        std::unique(layout.nonworking_days.begin(), layout.nonworking_days.end()),
        layout.nonworking_days.end());

    // Snap the origin backwards over the earliest start so no bar can begin
    // left of x = 0 (the "never clipped" contract).
    date32 earliest = kDateNa;
    for (const TaskBarInput& task : source.tasks) {
        if (is_na(task.start) || is_overflow(task.start)) {
            continue;
        }
        if (is_na(earliest) || task.start < earliest) {
            earliest = task.start;
        }
    }

    // Week columns follow the calendar's own week anchor when there is one
    // (decision: data-driven where available; the engine anchors its effective
    // week on Sunday, which is also MS Project's stock work week).  Must run
    // before snap_origin(), because the anchor chooses the week column.
    if (layout.calendar != nullptr && layout.timescale.adopt_calendar_week_start) {
        const date32 probe =
            (is_na(earliest) || is_overflow(earliest)) ? layout.timescale.origin : earliest;
        if (!is_na(probe) && !is_overflow(probe)) {
            const core::sched::EffectiveWeek& week =
                layout.calendar->effective_week(day_of(probe));
            layout.timescale.week_start = weekday_of_day(week.week_start_day);
        }
    }
    layout.timescale.snap_origin(earliest);

    const Timescale& scale = layout.timescale;
    const double row_height = layout.rows.row_height;

    layout.bars.reserve(source.tasks.size());
    for (std::size_t index = 0; index < source.tasks.size(); ++index) {
        const TaskBarInput& task = source.tasks[index];
        if (is_na(task.start) || is_overflow(task.start)) {
            continue;  // the row exists, but it has no bar
        }
        const date32 start_day = date_only(task.start);
        const double left = scale.x_for_day_start(start_day);
        if (!std::isfinite(left)) {
            continue;
        }

        Bar bar;
        bar.task_uid = task.uid;
        bar.row = static_cast<int>(index);
        bar.kind = task.kind;
        bar.critical = task.critical;
        bar.manual = task.manual;
        bar.percent_complete = clamp_unit(task.percent_complete);
        bar.label_text = bar_label_text(task, layout.style);

        const double next_x = scale.x_for_day_start(add_days(start_day, 1));
        const double day_width = std::isfinite(next_x) ? next_x - left : 0.0;

        double height = layout.style.bar_height;
        if (bar.kind == BarKind::summary) {
            height = layout.style.summary_height;
        } else if (bar.kind == BarKind::milestone) {
            double side = layout.style.milestone_size > 0.0
                              ? layout.style.milestone_size
                              : 0.0;
            // Keep the diamond inside its own day column at every zoom.  At the
            // day/week zoom the column is 20 px so the size stays 12; at the
            // month zoom a 31-day column is ~9.7 px and the diamond shrinks.
            if (day_width > 0.0 && side > day_width) {
                side = day_width;
            }
            height = side;
        }
        if (!(height > 0.0)) {
            height = 0.0;
        }
        const double top =
            layout.rows.y_for_row(bar.row) + (row_height - height) / 2.0;

        if (bar.kind == BarKind::milestone) {
            // decision (settled, memory/gantt.md): the diamond is centred in its
            // own day column, not on the date line.  Rationale: it stays inside
            // the day cell the milestone belongs to, is never clipped at x = 0,
            // and never spills into the previous day; a date-line centre would
            // sit half a diamond left of the column and is unmeasured anyway.
            bar.shape = BarShape::diamond;
            const double size = height;
            bar.rect = Rect{left + (day_width - size) / 2.0, top, size, size};
            bar.diamond[0] = Point{bar.rect.center_x(), bar.rect.y};
            bar.diamond[1] = Point{bar.rect.right(), bar.rect.center_y()};
            bar.diamond[2] = Point{bar.rect.center_x(), bar.rect.bottom()};
            bar.diamond[3] = Point{bar.rect.x, bar.rect.center_y()};
        } else {
            date32 finish = task.finish;
            if (is_na(finish) || is_overflow(finish)) {
                finish = task.start;
            }
            if (finish < task.start) {
                finish = task.start;
            }
            const double right = scale.x_for_day_start(add_days(date_only(finish), 1));
            bar.rect = Rect{left, top, std::max(0.0, right - left), height};
            if (bar.kind == BarKind::summary) {
                bar.shape = BarShape::summary_bracket;
                // decision (unmeasured): two downward triangles at the lower outer
                // corners; clamped to half the bar so a short summary stays sane.
                const double cap_width =
                    layout.style.summary_cap_width > 0.0
                        ? std::min(layout.style.summary_cap_width, bar.rect.w / 2.0)
                        : 0.0;
                const double cap_height =
                    layout.style.summary_cap_height > 0.0
                        ? layout.style.summary_cap_height
                        : 0.0;
                if (cap_width > 0.0 && cap_height > 0.0) {
                    bar.cap_left[0] = Point{bar.rect.x, bar.rect.bottom()};
                    bar.cap_left[1] = Point{bar.rect.x + cap_width, bar.rect.bottom()};
                    bar.cap_left[2] = Point{bar.rect.x, bar.rect.bottom() + cap_height};
                    bar.cap_right[0] = Point{bar.rect.right(), bar.rect.bottom()};
                    bar.cap_right[1] = Point{bar.rect.right() - cap_width, bar.rect.bottom()};
                    bar.cap_right[2] = Point{bar.rect.right(), bar.rect.bottom() + cap_height};
                }
            } else {
                bar.shape = BarShape::rectangle;
            }
        }
        bar.fill = Rect{bar.rect.x, bar.rect.y, bar.rect.w * bar.percent_complete,
                        bar.rect.h};
        place_label(bar, layout.style);
        layout.bars.push_back(std::move(bar));
    }

    for (const LinkInput& link : source.links) {
        const Bar* predecessor = layout.bar(link.predecessor_uid);
        const Bar* successor = layout.bar(link.successor_uid);
        if (predecessor == nullptr || successor == nullptr) {
            continue;  // no geometry to attach to
        }
        layout.links.push_back(route_link(*predecessor, *successor, link, layout.style));
    }

    for (const Bar& bar : layout.bars) {
        layout.content_width = std::max(layout.content_width, bar.rect.right());
    }

    if (!is_na(source.status_date) && !is_overflow(source.status_date)) {
        const double x = scale.x_for_date(source.status_date);
        if (std::isfinite(x)) {
            layout.status_x = x;
            layout.has_status = true;
        }
    }
    return layout;
}

}  // namespace core::gantt
