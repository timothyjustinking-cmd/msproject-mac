// tests/gantt_tests.cpp -- exact geometry for the Gantt layout model.
//
// Almost everything here is synthetic: fixed dates, fixed 20 px/day, row height
// 22.  The one corpus test is the holiday probe at the bottom: it decodes
// corpus/probe/holiday1.mpp (a synthetic COM-built fixture, not client data)
// through core::cal::load_project_calendar and skips with "corpus absent" when
// the file is missing.
//
// The numbers are the contract.  If a decision changes (row height, bar
// height, link gap, milestone centring, label fitting), this file must change
// with it -- memory/gantt.md lists which of those are ours and which are the
// product's.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "fixture_guard.h"
#include "gantt.h"

#if defined(GANTT_HAS_CAL)
#include "cal.h"
#endif

namespace gantt = core::gantt;
using core::sched::date32;
using core::sched::from_ymd;

namespace {

[[maybe_unused]] bool near(double actual, double expected, double epsilon = 1e-9) {
    return std::abs(actual - expected) <= epsilon;
}

class Context {
  public:
    void check(bool condition, const std::string& what) {
        if (condition) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << '\n';
    }

    // No streaming of the values: several call sites compare enum class values
    // that have no operator<<.
    template <typename A, typename B>
    void equal(const A& actual, const B& expected, const std::string& what) {
        if (actual == expected) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << '\n';
    }

    void near(double actual, double expected, const std::string& what,
              double epsilon = 1e-9) {
        if (std::abs(actual - expected) <= epsilon) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << " (actual=" << actual
                  << ", expected=" << expected << ")\n";
    }

    void rect(const gantt::Rect& actual, double x, double y, double w, double h,
              const std::string& what) {
        near(actual.x, x, what + ".x");
        near(actual.y, y, what + ".y");
        near(actual.w, w, what + ".w");
        near(actual.h, h, what + ".h");
    }

    void point(const gantt::Point& actual, double x, double y, const std::string& what) {
        near(actual.x, x, what + ".x");
        near(actual.y, y, what + ".y");
    }

    void points(const std::vector<gantt::Point>& actual,
                const std::vector<gantt::Point>& expected, const std::string& what) {
        if (actual.size() != expected.size()) {
            ++failures_;
            std::cerr << "FAIL: " << what << " (point count " << actual.size()
                      << ", expected " << expected.size() << ")\n";
            return;
        }
        ++checks_;
        for (std::size_t i = 0; i < expected.size(); ++i) {
            point(actual[i], expected[i].x, expected[i].y,
                  what + "[" + std::to_string(i) + "]");
        }
    }

    int finish(const std::string& name) {
        if (failures_ == 0) {
            std::cout << name << ": " << checks_ << " checks passed\n";
            return 0;
        }
        std::cerr << name << ": " << failures_ << " of " << (checks_ + failures_)
                  << " checks FAILED\n";
        return 1;
    }

  private:
    int checks_ = 0;
    int failures_ = 0;
};

// 2026-10 dates, all at midnight unless stated.
date32 day(int month, int day_of_month) { return from_ymd(2026, month, day_of_month); }
date32 at(int month, int day_of_month, int hour, int minute) {
    return from_ymd(2026, month, day_of_month, hour, minute);
}

gantt::Timescale day_scale(double pixels_per_day = 20.0) {
    gantt::Timescale scale;
    scale.unit = gantt::TimescaleUnit::day;
    scale.origin = day(10, 1);
    scale.pixels_per_unit = pixels_per_day;
    return scale;
}

gantt::GanttStyle fixed_style() {
    gantt::GanttStyle style;  // defaults are the pinned numbers
    return style;
}

// Four tasks: a 1-day task, a 5-working-day task across the weekend, a
// milestone, and a summary.  Start/finish carry 08:00/17:00 to prove that bars
// are quantised to whole days.
gantt::GanttSource fixed_source() {
    gantt::GanttSource source;

    gantt::TaskBarInput one_day;
    one_day.uid = 1;
    one_day.label = "Task 1";
    one_day.start = at(10, 22, 8, 0);
    one_day.finish = at(10, 22, 17, 0);
    one_day.percent_complete = 0.5;
    source.tasks.push_back(one_day);

    gantt::TaskBarInput five_day;
    five_day.uid = 2;
    five_day.label = "Review";
    five_day.start = at(10, 23, 8, 0);   // Friday
    five_day.finish = at(10, 29, 17, 0); // Thursday: Fri + Mon..Thu = 5 working days
    five_day.percent_complete = 0.4;
    source.tasks.push_back(five_day);

    gantt::TaskBarInput milestone;
    milestone.uid = 3;
    milestone.label = "Ship";
    milestone.start = at(10, 26, 8, 0);
    milestone.finish = milestone.start;
    milestone.kind = gantt::BarKind::milestone;
    source.tasks.push_back(milestone);

    gantt::TaskBarInput summary;
    summary.uid = 4;
    summary.label = "Phase 1";
    summary.start = at(10, 20, 8, 0);
    summary.finish = at(10, 30, 17, 0);
    summary.kind = gantt::BarKind::summary;
    source.tasks.push_back(summary);

    return source;
}

gantt::Bar find_bar(const gantt::GanttLayout& layout, std::uint32_t uid) {
    const gantt::Bar* bar = layout.bar(uid);
    return bar == nullptr ? gantt::Bar{} : *bar;
}

// ---------------------------------------------------------------------------

void test_timescale_round_trip(Context& context) {
    for (const double pixels_per_day : {1.0, 30.0}) {
        const gantt::Timescale scale = day_scale(pixels_per_day);
        const date32 dates[] = {day(10, 1),  day(10, 22), day(10, 31), day(11, 1),
                                day(12, 31), from_ymd(2027, 1, 1),
                                from_ymd(2027, 2, 28)};
        for (const date32 date : dates) {
            const double x = scale.x_for_date(date);
            context.near(scale.x_for_date(date),
                         static_cast<double>(core::sched::day_of(date) -
                                             core::sched::day_of(scale.origin)) *
                             pixels_per_day,
                         "x for a midnight date at " + std::to_string(pixels_per_day) +
                             " px/day");
            context.equal(scale.date_at_x(x), date,
                          "date -> x -> date round trip at " +
                              std::to_string(pixels_per_day) + " px/day");
            context.equal(scale.date_at_x(x + pixels_per_day * 0.5), date,
                          "mid-column x maps back to the same day");
        }
        // The origin is x = 0 and the day after it is one column wide.
        context.near(scale.x_for_day_start(scale.origin), 0.0, "origin is x = 0");
        context.near(scale.x_for_day_start(day(10, 2)),
                     pixels_per_day, "next day is one column");
    }

    // Time of day is honoured by x_for_date but not by the bar/grid position.
    const gantt::Timescale scale = day_scale();
    context.near(scale.x_for_date(at(10, 22, 12, 0)), 430.0,
                 "noon is half a day into its column");
    context.near(scale.x_for_date(at(10, 22, 18, 0)), 435.0,
                 "18:00 is three quarters into its column");
    context.near(scale.x_for_day_start(at(10, 22, 18, 0)), 420.0,
                 "day start ignores the time of day");
    context.equal(scale.date_at_x(435.0), day(10, 22), "18:00 maps back to its day");

    // Negative x is a real position before the origin, not an error.
    context.equal(scale.date_at_x(-1.0), day(9, 30), "x < 0 maps before the origin");

    // NA is the only input with no x.
    context.check(std::isnan(scale.x_for_date(core::sched::kDateNa)),
                  "NA has no x");
    context.equal(scale.date_at_x(std::nan("")), core::sched::kDateNa,
                  "NaN x has no date");

    // date32 starts at day 1 (1984-01-01); an x far enough left has no date
    // rather than wrapping the 16-bit day field.
    context.equal(scale.date_at_x(-1.0e9), core::sched::kDateNa,
                  "x before the epoch has no date");
    gantt::Timescale month_guard;
    month_guard.unit = gantt::TimescaleUnit::month;
    month_guard.origin = day(10, 1);
    month_guard.pixels_per_unit = 300.0;
    context.equal(month_guard.date_at_x(-1.0e9), core::sched::kDateNa,
                  "month scale before 1984 has no date");
}

void test_header_month_boundary(Context& context) {
    const gantt::Timescale scale = day_scale();

    const std::vector<gantt::HeaderCell> majors = scale.header(gantt::HeaderTier::major, 0.0, 1200.0);
    context.equal(majors.size(), std::size_t{2}, "two month bands in the window");
    context.check(majors.size() == 2, "major band count");
    if (majors.size() == 2) {
        context.equal(majors[0].label, std::string("October 2026"), "October band label");
        context.equal(majors[0].start, day(10, 1), "October band start");
        context.equal(majors[0].finish, day(10, 31), "October band finish");
        context.near(majors[0].x, 0.0, "October band x");
        context.near(majors[0].width, 620.0, "October band width (31 days)");
        context.equal(majors[0].tier, gantt::HeaderTier::major, "major tier tag");

        context.equal(majors[1].label, std::string("November 2026"), "November band label");
        context.equal(majors[1].start, day(11, 1), "November band start");
        context.equal(majors[1].finish, day(11, 30), "November band finish");
        context.near(majors[1].x, 620.0, "November band x");
        context.near(majors[1].width, 600.0, "November band width (30 days)");

        context.near(majors[0].x + majors[0].width, majors[1].x,
                     "October ends exactly where November begins");
        context.near(majors[1].x, scale.x_for_day_start(day(11, 1)),
                     "month boundary is the 1 November day column");
    }

    // Day cells around the boundary: the band break falls between 31 and 1.
    const std::vector<gantt::HeaderCell> minors =
        scale.header(gantt::HeaderTier::minor, 590.0, 650.0);
    context.equal(minors.size(), std::size_t{4}, "four day cells intersect the window");
    context.check(minors.size() == 4, "day cell count");
    if (minors.size() == 4) {
        context.equal(minors[0].label, std::string("30"), "30 October");
        context.near(minors[0].x, 580.0, "30 October x");
        context.equal(minors[1].label, std::string("31"), "31 October");
        context.near(minors[1].x, 600.0, "31 October x");
        context.near(minors[1].width, 20.0, "day cell width");
        context.equal(minors[1].start, day(10, 31), "31 October start");
        context.equal(minors[1].finish, day(10, 31), "31 October finish");
        context.equal(minors[2].label, std::string("1"), "1 November");
        context.near(minors[2].x, 620.0, "1 November x");
        context.equal(minors[2].start, day(11, 1), "1 November start");
        context.equal(minors[2].weekday, 0, "1 November 2026 is a Sunday");
        context.equal(minors[3].label, std::string("2"), "2 November");
        context.near(minors[3].x, 640.0, "2 November x");
    }

    // The 22 October day cell carries the weekday as data (Thursday = 4).
    const std::vector<gantt::HeaderCell> one_day =
        scale.header(gantt::HeaderTier::minor, 420.0, 440.0);
    context.check(one_day.size() == 1, "one day cell for a one-column window");
    if (one_day.size() == 1) {
        context.equal(one_day[0].weekday, 4, "22 October 2026 is a Thursday");
        context.near(one_day[0].width, 20.0, "day column width");
    }
}

void test_bar_geometry(Context& context) {
    const gantt::GanttLayout layout =
        build_gantt(fixed_source(), fixed_style(), day_scale());
    context.equal(layout.bars.size(), std::size_t{4}, "four bars");
    context.equal(layout.row_count, 4, "four rows");
    context.near(layout.rows.row_height, 22.0, "row height 22");

    // 1-day task, 22 October: exactly one 20 px column.
    const gantt::Bar one_day = find_bar(layout, 1);
    context.equal(one_day.task_uid, 1u, "1-day task id");
    context.equal(one_day.row, 0, "1-day task row");
    context.rect(one_day.rect, 420.0, 5.0, 20.0, 12.0, "1-day bar");
    context.rect(one_day.fill, 420.0, 5.0, 10.0, 12.0, "1-day 50% fill");
    context.equal(one_day.kind, gantt::BarKind::normal, "1-day bar kind");
    context.check(!one_day.critical, "1-day bar is not critical");

    // 5-day task, Fri 23 Oct - Thu 29 Oct: 7 calendar columns = 140 px, so the
    // weekend is visible inside the bar (Sat = x 460, Sun = x 480).
    const gantt::Bar five_day = find_bar(layout, 2);
    context.equal(five_day.row, 1, "5-day task row");
    context.rect(five_day.rect, 440.0, 27.0, 140.0, 12.0, "5-day bar across weekend");
    context.rect(five_day.fill, 440.0, 27.0, 56.0, 12.0, "5-day 40% fill");
    context.near(five_day.rect.right(), 580.0, "5-day bar ends at 30 October");

    // Milestone: a 12 x 12 diamond centred in its 20 px day column.
    const gantt::Bar milestone = find_bar(layout, 3);
    context.equal(milestone.kind, gantt::BarKind::milestone, "milestone kind");
    context.rect(milestone.rect, 504.0, 49.0, 12.0, 12.0, "milestone rect");
    context.near(milestone.rect.center_x(), 510.0, "milestone centre is column centre");
    context.near(milestone.rect.center_y(), 55.0, "milestone centre is row centre");

    // Summary: the same span, a thinner bar (8 px) vertically centred.
    const gantt::Bar summary = find_bar(layout, 4);
    context.equal(summary.kind, gantt::BarKind::summary, "summary kind");
    context.rect(summary.rect, 380.0, 73.0, 220.0, 8.0, "summary bar");
    context.near(summary.rect.right(), 600.0, "summary ends at 31 October");

    // Bars are in row order and content width is the furthest bar edge.
    context.near(layout.content_width, 600.0, "content width");

    // A task with no start has a row but no bar; NA finish means the start day.
    gantt::GanttSource sparse;
    gantt::TaskBarInput missing;
    missing.uid = 10;
    sparse.tasks.push_back(missing);
    gantt::TaskBarInput no_finish;
    no_finish.uid = 11;
    no_finish.start = day(10, 22);
    no_finish.finish = core::sched::kDateNa;
    sparse.tasks.push_back(no_finish);
    const gantt::GanttLayout sparse_layout =
        build_gantt(sparse, fixed_style(), day_scale());
    context.equal(sparse_layout.row_count, 2, "NA-start task still occupies a row");
    context.equal(sparse_layout.bars.size(), std::size_t{1}, "NA-start task has no bar");
    const gantt::Bar inferred = find_bar(sparse_layout, 11);
    context.rect(inferred.rect, 420.0, 27.0, 20.0, 12.0, "NA finish -> one day bar");
}

void test_weekend_gap(Context& context) {
    const gantt::GanttLayout layout =
        build_gantt(fixed_source(), fixed_style(), day_scale());
    const gantt::Bar five_day = find_bar(layout, 2);

    // Calendar-day axis: Saturday and Sunday each consume a full 20 px column,
    // and the five working days are 140 px wide, not 100 px.
    context.equal(core::sched::weekday(day(10, 23)), 5, "23 October 2026 is a Friday");
    context.equal(core::sched::weekday(day(10, 24)), 6, "24 October 2026 is a Saturday");
    context.near(layout.timescale.x_for_day_start(day(10, 24)) -
                     layout.timescale.x_for_day_start(day(10, 23)),
                 20.0, "Saturday occupies one day column");
    context.near(layout.timescale.x_for_day_start(day(10, 26)) -
                     layout.timescale.x_for_day_start(day(10, 23)),
                 60.0, "Friday -> Monday spans the two weekend columns");

    int working_days = 0;
    for (date32 d = day(10, 23); d <= day(10, 29); d = core::sched::add_days(d, 1)) {
        const int weekday = core::sched::weekday(d);
        if (weekday >= 1 && weekday <= 5) {
            ++working_days;
        }
    }
    context.equal(working_days, 5, "23-29 October has five working days");
    context.near(five_day.rect.w, 7.0 * 20.0,
                 "the 5-working-day bar is 7 calendar columns wide");

    // Non-working shading data: Sat 24, Sun 25 and Sat 31 in this window.
    const std::vector<gantt::NonworkingSpan> spans =
        layout.nonworking_spans(440.0, 620.0);
    context.equal(spans.size(), std::size_t{3}, "three non-working columns shaded");
    if (spans.size() == 3) {
        context.equal(spans[0].date, day(10, 24), "first shaded day is Saturday 24");
        context.near(spans[0].x, 460.0, "Saturday x");
        context.near(spans[0].width, 20.0, "Saturday width");
        context.equal(spans[1].date, day(10, 25), "second shaded day is Sunday 25");
        context.near(spans[1].x, 480.0, "Sunday x");
        context.equal(spans[2].date, day(10, 31), "third shaded day is Saturday 31");
        context.near(spans[2].x, 600.0, "31 October x");
    }

    // A holiday makes a weekday non-working without moving any bar.
    gantt::GanttSource with_holiday = fixed_source();
    with_holiday.nonworking_days.push_back(day(10, 27));
    const gantt::GanttLayout holiday_layout =
        build_gantt(with_holiday, fixed_style(), day_scale());
    context.near(find_bar(holiday_layout, 2).rect.w, 140.0,
                 "a holiday does not change bar geometry");
    const std::vector<gantt::NonworkingSpan> holiday_spans =
        holiday_layout.nonworking_spans(440.0, 620.0);
    bool found_tuesday = false;
    for (const gantt::NonworkingSpan& span : holiday_spans) {
        if (span.date == day(10, 27)) {
            found_tuesday = true;
            context.near(span.x, 520.0, "holiday column x");
        }
    }
    context.check(found_tuesday, "the holiday is shaded");
}

void test_row_layout(Context& context) {
    gantt::RowLayout rows;
    context.near(rows.row_height, 22.0, "default row height");
    context.near(rows.y_for_row(0), 0.0, "row 0 top");
    context.near(rows.y_for_row(3), 66.0, "row 3 top");
    context.equal(rows.row_at_y(0.0), 0, "y 0 is row 0");
    context.equal(rows.row_at_y(21.9), 0, "y 21.9 is row 0");
    context.equal(rows.row_at_y(22.0), 1, "y 22 is row 1");
    context.equal(rows.row_at_y(43.5), 1, "y 43.5 is row 1");
    context.equal(rows.row_at_y(66.0), 3, "y 66 is row 3");
    context.equal(rows.row_at_y(-0.5), -1, "no row above the first");

    const gantt::RowRange partial = rows.visible_rows(11.0, 44.0, 10);
    context.equal(partial.first_row, 0, "scroll 11 starts at row 0");
    context.equal(partial.row_count, 3, "44 px shows rows 0, 1 and part of 2");
    context.equal(partial.end_row(), 3, "visible row end is exclusive");

    const gantt::RowRange exact = rows.visible_rows(0.0, 22.0, 10);
    context.equal(exact.first_row, 0, "top of the list");
    context.equal(exact.row_count, 1, "one exact row");

    const gantt::RowRange clamped = rows.visible_rows(100.0, 50.0, 3);
    context.equal(clamped.first_row, 3, "clamped to the row count");
    context.equal(clamped.row_count, 0, "nothing visible past the end");
    context.check(clamped.empty(), "empty range");

    context.check(rows.visible_rows(0.0, 0.0, 10).empty(), "zero height shows nothing");

    const gantt::GanttLayout layout =
        build_gantt(fixed_source(), fixed_style(), day_scale());
    const gantt::RowRange visible = layout.visible_rows(0.0, 66.0);
    context.equal(visible.row_count, 3, "layout clamps to its own row count");
}

void test_dependency_routing(Context& context) {
    gantt::GanttSource source;
    gantt::TaskBarInput pred;
    pred.uid = 1;
    pred.start = at(10, 22, 8, 0);
    pred.finish = at(10, 22, 17, 0);
    pred.critical = true;
    source.tasks.push_back(pred);
    gantt::TaskBarInput succ;
    succ.uid = 2;
    succ.start = at(10, 29, 8, 0);
    succ.finish = at(10, 29, 17, 0);
    source.tasks.push_back(succ);

    const gantt::Timescale scale = day_scale();
    const gantt::GanttStyle style = fixed_style();

    // FS: predecessor finish (right) edge -> successor start (left) edge.
    {
        gantt::LinkInput link;
        link.predecessor_uid = 1;
        link.successor_uid = 2;
        link.type = gantt::LinkType::finish_to_start;
        link.critical = true;
        source.links = {link};
        const gantt::GanttLayout layout = build_gantt(source, style, scale);
        context.equal(layout.links.size(), std::size_t{1}, "one FS route");
        const gantt::LinkRoute& route = layout.links[0];
        const std::vector<gantt::Point> expected = {
            {440.0, 11.0}, {552.0, 11.0}, {552.0, 33.0}, {560.0, 33.0}};
        context.points(route.points, expected, "FS points");
        context.point(route.arrow.tip, 560.0, 33.0, "FS arrow tip on successor left edge");
        context.point(route.arrow.direction, 1.0, 0.0, "FS arrow points right");
        context.point(route.arrow.wing[0], 554.0, 35.0, "FS arrow wing 0");
        context.point(route.arrow.wing[1], 554.0, 31.0, "FS arrow wing 1");
        context.rect(route.bounds, 440.0, 11.0, 120.0, 22.0, "FS bounds");
        context.check(route.critical, "FS critical hint is data");
        context.equal(route.type, gantt::LinkType::finish_to_start, "FS type tag");

        const gantt::HitResult hit = layout.hit_test(552.0, 22.0);
        context.equal(hit.kind, gantt::HitKind::link, "point on the FS route hits the link");
        context.equal(hit.predecessor_uid, 1u, "hit predecessor");
        context.equal(hit.successor_uid, 2u, "hit successor");
        context.check(hit.critical, "hit carries the critical hint");
        context.equal(layout.hit_test(552.0, 44.0).kind, gantt::HitKind::none,
                      "point 11 px below the FS route is a miss");
    }

    // SS: predecessor start (left) edge -> successor start (left) edge.
    {
        gantt::LinkInput link;
        link.predecessor_uid = 1;
        link.successor_uid = 2;
        link.type = gantt::LinkType::start_to_start;
        source.links = {link};
        const gantt::GanttLayout layout = build_gantt(source, style, scale);
        const std::vector<gantt::Point> expected = {
            {420.0, 11.0}, {412.0, 11.0}, {412.0, 33.0}, {560.0, 33.0}};
        context.points(layout.links[0].points, expected, "SS points");
        context.point(layout.links[0].arrow.tip, 560.0, 33.0, "SS arrow tip");
        context.point(layout.links[0].arrow.direction, 1.0, 0.0, "SS arrow points right");
    }

    // FF: predecessor finish edge -> successor finish (right) edge.
    {
        gantt::LinkInput link;
        link.predecessor_uid = 1;
        link.successor_uid = 2;
        link.type = gantt::LinkType::finish_to_finish;
        source.links = {link};
        const gantt::GanttLayout layout = build_gantt(source, style, scale);
        const std::vector<gantt::Point> expected = {
            {440.0, 11.0}, {588.0, 11.0}, {588.0, 33.0}, {580.0, 33.0}};
        context.points(layout.links[0].points, expected, "FF points");
        context.point(layout.links[0].arrow.tip, 580.0, 33.0, "FF arrow tip on right edge");
        context.point(layout.links[0].arrow.direction, -1.0, 0.0, "FF arrow points left");
    }

    // SF: predecessor start edge -> successor finish (right) edge, via the lane.
    {
        gantt::LinkInput link;
        link.predecessor_uid = 1;
        link.successor_uid = 2;
        link.type = gantt::LinkType::start_to_finish;
        source.links = {link};
        const gantt::GanttLayout layout = build_gantt(source, style, scale);
        const std::vector<gantt::Point> expected = {
            {420.0, 11.0}, {412.0, 11.0}, {412.0, 22.0},
            {588.0, 22.0}, {588.0, 33.0}, {580.0, 33.0}};
        context.points(layout.links[0].points, expected, "SF points");
        context.point(layout.links[0].arrow.tip, 580.0, 33.0, "SF arrow tip");
        context.point(layout.links[0].arrow.direction, -1.0, 0.0, "SF arrow points left");
    }

    // FS with the successor to the LEFT of the predecessor: the route detours
    // through the inter-row lane (y = 22) instead of entering the left edge from
    // the wrong side.
    {
        gantt::GanttSource backward;
        gantt::TaskBarInput first;
        first.uid = 1;
        first.start = at(10, 22, 8, 0);
        first.finish = at(10, 22, 17, 0);
        backward.tasks.push_back(first);
        gantt::TaskBarInput second;
        second.uid = 2;
        second.start = at(10, 20, 8, 0);
        second.finish = at(10, 20, 17, 0);
        backward.tasks.push_back(second);
        gantt::LinkInput link;
        link.predecessor_uid = 1;
        link.successor_uid = 2;
        link.type = gantt::LinkType::finish_to_start;
        backward.links = {link};

        const gantt::GanttLayout layout = build_gantt(backward, style, scale);
        const std::vector<gantt::Point> expected = {
            {440.0, 11.0}, {448.0, 11.0}, {448.0, 22.0},
            {372.0, 22.0}, {372.0, 33.0}, {380.0, 33.0}};
        context.points(layout.links[0].points, expected, "backward FS points");
        context.point(layout.links[0].arrow.tip, 380.0, 33.0,
                      "backward FS enters the left edge from the left");
        context.point(layout.links[0].arrow.direction, 1.0, 0.0,
                      "backward FS arrow points right");
    }

    // Endpoints sit on bar edges for all four types.
    const gantt::LinkType types[] = {
        gantt::LinkType::finish_to_start, gantt::LinkType::start_to_start,
        gantt::LinkType::finish_to_finish, gantt::LinkType::start_to_finish};
    for (const gantt::LinkType type : types) {
        gantt::LinkInput link;
        link.predecessor_uid = 1;
        link.successor_uid = 2;
        link.type = type;
        source.links = {link};
        const gantt::GanttLayout layout = build_gantt(source, style, scale);
        context.equal(layout.links.size(), std::size_t{1}, "one route per link type");
        if (layout.links.size() != 1) {
            continue;
        }
        const gantt::LinkRoute& route = layout.links[0];
        const gantt::Bar* pred_bar = layout.bar(1);
        const gantt::Bar* succ_bar = layout.bar(2);
        context.check(pred_bar != nullptr && succ_bar != nullptr,
                      "both endpoints have bars");
        if (pred_bar == nullptr || succ_bar == nullptr) {
            continue;
        }
        const bool exit_left = type == gantt::LinkType::start_to_start ||
                               type == gantt::LinkType::start_to_finish;
        const bool enter_left = type == gantt::LinkType::finish_to_start ||
                                type == gantt::LinkType::start_to_start;
        context.near(route.points.front().x,
                     exit_left ? pred_bar->rect.x : pred_bar->rect.right(),
                     "route starts on the predecessor edge");
        context.near(route.points.front().y, pred_bar->rect.center_y(),
                     "route leaves at the predecessor centre line");
        context.near(route.arrow.tip.x,
                     enter_left ? succ_bar->rect.x : succ_bar->rect.right(),
                     "arrow tip is on the successor edge");
        context.near(route.arrow.tip.y, succ_bar->rect.center_y(),
                     "arrow tip is at the successor centre line");
    }

    // A link to a task with no bar is skipped rather than given fake geometry.
    gantt::GanttSource dangling;
    dangling.tasks = source.tasks;
    gantt::LinkInput missing;
    missing.predecessor_uid = 1;
    missing.successor_uid = 99;
    dangling.links = {missing};
    context.equal(build_gantt(dangling, style, scale).links.size(), std::size_t{0},
                  "dangling link is skipped");
}

void test_hit_testing(Context& context) {
    const gantt::GanttLayout layout =
        build_gantt(fixed_source(), fixed_style(), day_scale());

    const gantt::HitResult inside = layout.hit_test(430.0, 11.0);
    context.equal(inside.kind, gantt::HitKind::bar, "centre of the 1-day bar");
    context.equal(inside.task_uid, 1u, "hit returns the task id");
    context.equal(layout.hit_test(430.0, 19.0).kind, gantt::HitKind::bar,
                  "2 px below the bar is still a hit (tolerance 3)");
    context.equal(layout.hit_test(430.0, 22.0).kind, gantt::HitKind::none,
                  "5 px below the bar is a miss");
    context.equal(layout.hit_test(443.0, 11.0).kind, gantt::HitKind::bar,
                  "3 px right of the bar is still a hit");
    context.equal(layout.hit_test(444.0, 11.0).kind, gantt::HitKind::none,
                  "4 px right of the bar is a miss");
    context.equal(layout.hit_test(-10.0, 11.0).kind, gantt::HitKind::none,
                  "left of the chart is a miss");

    const gantt::HitResult five_day = layout.hit_test(510.0, 33.0);
    context.equal(five_day.task_uid, 2u, "hit inside the 5-day bar");

    const gantt::HitResult milestone = layout.hit_test(510.0, 55.0);
    context.equal(milestone.kind, gantt::HitKind::bar, "milestone centre");
    context.equal(milestone.task_uid, 3u, "milestone task id");
    context.equal(layout.hit_test(510.0, 65.0).kind, gantt::HitKind::none,
                  "4 px below the milestone is a miss");

    // Bar rows are distinguishable: y between rows belongs to nobody.
    context.equal(layout.hit_test(430.0, 22.0).kind, gantt::HitKind::none,
                  "the gutter between rows 0 and 1 is empty");
}

void test_snapping(Context& context) {
    // The origin snaps BACK to the earliest start's unit, so no bar can be
    // clipped at x = 0.
    gantt::Timescale late = day_scale();
    late.origin = day(10, 25);
    const gantt::GanttLayout layout =
        build_gantt(fixed_source(), fixed_style(), late);
    // The summary task (uid 4) starts on 20 October, earlier than every other
    // bar, so that is the earliest start the origin must cover.
    context.equal(layout.timescale.origin, day(10, 20), "day origin snaps back to 20 Oct");
    context.near(find_bar(layout, 4).rect.x, 0.0, "earliest bar starts at x = 0");
    context.near(find_bar(layout, 1).rect.x, 40.0, "22 October is two columns in");
    for (const gantt::Bar& bar : layout.bars) {
        context.check(bar.rect.x >= 0.0, "no bar is clipped at x = 0");
    }

    // Week unit: snap to the containing week start (Monday, our decision).
    gantt::Timescale week;
    week.unit = gantt::TimescaleUnit::week;
    week.origin = day(11, 1);
    week.pixels_per_unit = 140.0;
    const gantt::GanttLayout week_layout =
        build_gantt(fixed_source(), fixed_style(), week);
    context.equal(week_layout.timescale.origin, day(10, 19),
                  "week origin snaps to Monday 19 Oct");
    context.near(week_layout.timescale.x_for_date(day(10, 22)), 60.0,
                 "Thursday is three day columns into the week (3 x 20 px)");

    // Month unit: snap to the first of the containing month.
    gantt::Timescale month;
    month.unit = gantt::TimescaleUnit::month;
    month.origin = day(11, 15);
    month.pixels_per_unit = 300.0;
    const gantt::GanttLayout month_layout =
        build_gantt(fixed_source(), fixed_style(), month);
    context.equal(month_layout.timescale.origin, day(10, 1),
                  "month origin snaps to 1 Oct");
    context.near(month_layout.timescale.x_for_day_start(day(10, 22)),
                 21.0 / 31.0 * 300.0, "22 Oct is 21/31 of the October column");
    context.near(month_layout.timescale.x_for_day_start(day(11, 1)), 300.0,
                 "1 Nov is the next month column");
    context.equal(month_layout.timescale.date_at_x(450.0), day(11, 16),
                  "half of the November column is 16 November");
    context.equal(month_layout.timescale.date_at_x(month_layout.timescale.x_for_date(
                      day(11, 5))),
                  day(11, 5), "month scale round trip");

    // Week 20 px/day bar: a 5-day task in a week scale is still 7 calendar days.
    context.near(find_bar(week_layout, 2).rect.w, 7.0 * 20.0,
                 "week-scale bar keeps calendar-day widths");

    // A default week timescale with no tasks must not snap below date32's day 1
    // (1984-01-01 is a Sunday; the default origin is Monday 1984-01-02).
    gantt::Timescale default_week;
    default_week.unit = gantt::TimescaleUnit::week;
    default_week.pixels_per_unit = 140.0;
    const gantt::GanttLayout empty = build_gantt(gantt::GanttSource{}, {}, default_week);
    context.check(core::sched::day_of(empty.timescale.origin) >= core::sched::kMinDay,
                  "empty week timescale stays inside the date32 domain");
    context.equal(empty.timescale.origin, core::sched::from_ymd(1984, 1, 2),
                  "default week origin is Monday 2 January 1984");

    // Wave 29 (W29-1.4): a saved scroll origin is the file's own left edge, not
    // a lower bound for the bars.  Pinning keeps it and lets an earlier bar sit
    // at x < 0 (Project clips it), instead of pulling the axis back.
    gantt::Timescale pinned = day_scale();
    pinned.origin = day(10, 25);
    pinned.pin_origin = true;
    const gantt::GanttLayout pinned_layout =
        build_gantt(fixed_source(), fixed_style(), pinned);
    context.equal(pinned_layout.timescale.origin, day(10, 25),
                  "a pinned stored origin is kept");
    context.near(find_bar(pinned_layout, 4).rect.x, -100.0,
                 "the earliest bar is five days left of a pinned 25 Oct origin");
    context.near(find_bar(pinned_layout, 1).rect.x, -60.0,
                 "22 October is three days left of the pinned origin");
}

void test_status_line(Context& context) {
    gantt::GanttSource source = fixed_source();
    source.status_date = day(10, 26);
    const gantt::GanttLayout layout =
        build_gantt(source, fixed_style(), day_scale());
    context.check(layout.has_status_line(), "status line present");
    context.near(layout.status_x, 500.0, "status line x for 26 October");

    gantt::GanttSource no_status = fixed_source();
    const gantt::GanttLayout bare =
        build_gantt(no_status, fixed_style(), day_scale());
    context.check(!bare.has_status_line(), "no status line without a status date");
    context.check(std::isnan(bare.status_x), "no status x");

    // The marker honours the time of day (the real product's status date is a
    // date, but the mapper must not corrupt a timestamp).
    gantt::GanttSource timed = fixed_source();
    timed.status_date = at(10, 26, 12, 0);
    context.near(build_gantt(timed, fixed_style(), day_scale()).status_x, 510.0,
                 "status marker at noon is half a column in");
}

void test_labels(Context& context) {
    const gantt::GanttLayout layout =
        build_gantt(fixed_source(), fixed_style(), day_scale());

    // A 1-day bar cannot fit "Task 1" (42 px of text in 20 px): right of the bar.
    const gantt::Bar one_day = find_bar(layout, 1);
    context.check(one_day.label.visible, "1-day label is visible");
    context.check(!one_day.label.inside, "1-day label is outside the bar");
    context.rect(one_day.label.rect, 444.0, 5.0, 42.0, 12.0, "1-day label rect");

    // A 140 px bar fits "Review" (42 px): centred inside.
    const gantt::Bar five_day = find_bar(layout, 2);
    context.check(five_day.label.inside, "5-day label fits inside");
    context.rect(five_day.label.rect, 489.0, 27.0, 42.0, 12.0, "5-day label rect");

    // A milestone never takes an inside label.
    const gantt::Bar milestone = find_bar(layout, 3);
    context.check(!milestone.label.inside, "milestone label is outside");
    context.rect(milestone.label.rect, 520.0, 49.0, 28.0, 12.0, "milestone label rect");

    // A summary bar is wide enough for "Phase 1" (49 px).
    const gantt::Bar summary = find_bar(layout, 4);
    context.check(summary.label.inside, "summary label fits inside");
    context.rect(summary.label.rect, 465.5, 73.0, 49.0, 8.0, "summary label rect");

    // The shell can supply real font metrics; that replaces the fallback.
    gantt::GanttStyle measured = fixed_style();
    measured.measure_text = [](std::string_view) { return 200.0; };
    const gantt::GanttLayout measured_layout =
        build_gantt(fixed_source(), measured, day_scale());
    const gantt::Bar wide = find_bar(measured_layout, 2);
    context.check(!wide.label.inside, "measured label wider than the bar goes outside");
    context.near(wide.label.rect.x, 584.0, "measured label starts right of the bar");
    context.near(wide.label.rect.w, 200.0, "measured label width is the callback's");

    // No text means no label rectangle.
    gantt::GanttSource unlabelled = fixed_source();
    unlabelled.tasks[0].label.clear();
    const gantt::GanttLayout unlabelled_layout =
        build_gantt(unlabelled, fixed_style(), day_scale());
    context.check(!find_bar(unlabelled_layout, 1).label.visible,
                  "empty label is not placed");
}

// Wave 29 (W29-1.3): the stored bar styles' five placements.  The fallback
// label must vanish when the caller supplies the stored answer, even an
// all-empty one (a stored style list with no label means a bare bar).
void test_stored_bar_texts(Context& context) {
    gantt::GanttSource source = fixed_source();
    gantt::TaskBarInput& task = source.tasks[0];  // 1-day bar at x = 420, w = 20
    task.stored_labels = true;
    task.sides[static_cast<std::size_t>(gantt::BarLabelSide::left)] = "Start";
    task.sides[static_cast<std::size_t>(gantt::BarLabelSide::right)] = "Name";
    task.sides[static_cast<std::size_t>(gantt::BarLabelSide::top)] = "T";
    task.sides[static_cast<std::size_t>(gantt::BarLabelSide::bottom)] = "B";
    task.sides[static_cast<std::size_t>(gantt::BarLabelSide::inside)] = "In";
    const gantt::GanttLayout layout =
        build_gantt(source, fixed_style(), day_scale());

    const gantt::Bar bar = find_bar(layout, 1);
    // The single task-name label is NOT also drawn: stored labels replace it.
    context.check(!bar.label.visible, "a stored-labelled bar has no fallback label");
    context.check(bar.texts.size() == 5, "all five stored sides are placed");
    if (bar.texts.size() == 5) {
        context.check(bar.texts[0].side == gantt::BarLabelSide::left,
                      "text 0 is the left label");
        context.check(bar.texts[1].side == gantt::BarLabelSide::right,
                      "text 1 is the right label");
        context.check(bar.texts[2].side == gantt::BarLabelSide::top,
                      "text 2 is the top label");
        context.check(bar.texts[3].side == gantt::BarLabelSide::bottom,
                      "text 3 is the bottom label");
        context.check(bar.texts[4].side == gantt::BarLabelSide::inside,
                      "text 4 is the inside label");
        // 7 px per character, label_gap 4: left is right-aligned to the bar,
        // right starts one gap after it.
        context.rect(bar.texts[0].rect, 381.0, 5.0, 35.0, 12.0,
                     "left label ends at the bar's edge less the gap");
        context.rect(bar.texts[1].rect, 444.0, 5.0, 28.0, 12.0,
                     "right label starts one gap after the bar");
        // Top/bottom are centred; inside spans the bar's interior.
        context.rect(bar.texts[2].rect, 426.5, -11.0, 7.0, 12.0,
                     "top label is centred one gap above the bar");
        context.rect(bar.texts[3].rect, 426.5, 21.0, 7.0, 12.0,
                     "bottom label is centred one gap below the bar");
        context.rect(bar.texts[4].rect, 424.0, 5.0, 12.0, 12.0,
                     "inside label spans the bar interior");
        context.check(bar.texts[4].inside, "the inside label is marked inside");
        context.check(!bar.texts[0].inside && !bar.texts[1].inside,
                      "left/right labels are outside the bar");
    }

    // A stored answer with no labels at all is a bare bar, not the fallback.
    gantt::GanttSource bare = fixed_source();
    bare.tasks[0].stored_labels = true;
    const gantt::GanttLayout bare_layout =
        build_gantt(bare, fixed_style(), day_scale());
    const gantt::Bar bare_bar = find_bar(bare_layout, 1);
    context.check(bare_bar.texts.empty(), "an all-empty stored answer places no text");
    context.check(!bare_bar.label.visible, "an all-empty stored answer shows no fallback");

    // A task with no stored answer keeps the bar_label_text fallback.
    const gantt::GanttLayout fallback_layout =
        build_gantt(fixed_source(), fixed_style(), day_scale());
    context.check(find_bar(fallback_layout, 1).label.visible,
                  "a task without stored labels keeps the fallback label");
    context.check(find_bar(fallback_layout, 1).texts.empty(),
                  "a task without stored labels has no stored text");
}

void test_week_and_month_headers(Context& context) {
    // Week tier: week columns start Monday; labels are "day short-month".
    gantt::Timescale week;
    week.unit = gantt::TimescaleUnit::week;
    week.origin = day(10, 19);  // Monday
    week.pixels_per_unit = 140.0;
    const std::vector<gantt::HeaderCell> weeks =
        week.header(gantt::HeaderTier::minor, 0.0, 400.0);
    context.equal(weeks.size(), std::size_t{3}, "three week columns in 400 px");
    if (weeks.size() == 3) {
        context.equal(weeks[0].label, std::string("19 Oct"), "week label 19 Oct");
        context.near(weeks[0].width, 140.0, "week column width");
        context.equal(weeks[0].start, day(10, 19), "week start");
        context.equal(weeks[0].finish, day(10, 25), "week finish is six days later");
        context.equal(weeks[1].label, std::string("26 Oct"), "week label 26 Oct");
        context.equal(weeks[2].label, std::string("2 Nov"), "week label 2 Nov");
        context.equal(weeks[2].start, day(11, 2), "a week column may cross a month");
    }
    const std::vector<gantt::HeaderCell> week_months =
        week.header(gantt::HeaderTier::major, 0.0, 400.0);
    context.check(week_months.size() >= 2, "two month bands above the weeks");
    if (week_months.size() >= 2) {
        context.equal(week_months[0].label, std::string("October 2026"),
                      "October above the week columns");
        context.near(week_months[0].x, -360.0, "October starts 18 days before Monday");
        context.near(week_months[0].width, 620.0, "October is 31 day columns wide");
        context.equal(week_months[1].label, std::string("November 2026"),
                      "November follows");
        context.near(week_months[1].x, 260.0, "November starts at 1 Nov");
    }

    // Month tier: equal-width month columns and a year band above them.
    gantt::Timescale month;
    month.unit = gantt::TimescaleUnit::month;
    month.origin = day(10, 1);
    month.pixels_per_unit = 300.0;
    const std::vector<gantt::HeaderCell> months =
        month.header(gantt::HeaderTier::minor, 0.0, 900.0);
    context.equal(months.size(), std::size_t{3}, "three month columns");
    if (months.size() == 3) {
        context.equal(months[0].label, std::string("October"), "October label");
        context.near(months[0].width, 300.0, "October column width");
        context.equal(months[1].label, std::string("November"), "November label");
        context.near(months[1].x, 300.0, "November column x");
        context.equal(months[1].finish, day(11, 30), "November finishes on the 30th");
        context.equal(months[2].label, std::string("December"), "December label");
    }
    const std::vector<gantt::HeaderCell> years =
        month.header(gantt::HeaderTier::major, 0.0, 1000.0);
    context.equal(years.size(), std::size_t{2}, "two year bands");
    if (years.size() == 2) {
        context.equal(years[0].label, std::string("2026"), "2026 band");
        context.near(years[0].x, -2700.0, "January 2026 is nine columns left");
        context.near(years[0].width, 3600.0, "twelve equal month columns");
        context.equal(years[1].label, std::string("2027"), "2027 band");
        context.near(years[1].x, 900.0, "January 2027 is three columns right");
    }

    // Month-scale bar geometry: a 2-day task in a 300 px month is 2/30 of the
    // November column.
    gantt::GanttSource source;
    gantt::TaskBarInput task;
    task.uid = 1;
    task.start = at(11, 5, 8, 0);
    task.finish = at(11, 6, 17, 0);
    source.tasks.push_back(task);
    const gantt::GanttLayout layout = build_gantt(source, fixed_style(), month);
    const gantt::Bar bar = find_bar(layout, 1);
    context.near(bar.rect.x, 340.0, "5 November starts 4/30 into the column");
    context.near(bar.rect.w, 20.0, "two November days are 2/30 of 300 px");
}

void test_bar_defaults(Context& context) {
    const gantt::GanttStyle style;
    context.near(style.row_height, 22.0, "default row height");
    context.near(style.bar_height, 12.0, "default bar height");
    context.near(style.summary_height, 8.0, "default summary height");
    context.near(style.milestone_size, 12.0, "default milestone size");
    context.near(style.link_gap, 8.0, "default link gap");
    context.near(style.hit_tolerance, 3.0, "default hit tolerance");
    context.check(!style.measure_text, "no text measurer by default");

    // Percent complete is clamped and fills from the left.
    gantt::GanttSource source = fixed_source();
    source.tasks[0].percent_complete = 1.5;
    source.tasks[1].percent_complete = -2.0;
    const gantt::GanttLayout layout =
        build_gantt(source, fixed_style(), day_scale());
    context.near(find_bar(layout, 1).fill.w, 20.0, "over 100% clamps to full");
    context.near(find_bar(layout, 2).fill.w, 0.0, "negative % clamps to empty");
}

// ---------------------------------------------------------------------------
// Working-time calendars (shading only; bars never move)
// ---------------------------------------------------------------------------

// Project's Standard week: Mon-Fri 08:00-12:00 + 13:00-17:00, weekend empty.
core::sched::Week standard_week() {
    const core::sched::DayPattern day =
        core::sched::day_pattern({{8 * 60, 12 * 60}, {13 * 60, 17 * 60}});
    return core::sched::make_week(
        {{1, day}, {2, day}, {3, day}, {4, day}, {5, day}});
}

// Standard with a 4-hour Friday (08:00-12:00): half the day is non-working.
core::sched::Week four_hour_friday_week() {
    core::sched::Week week = standard_week();
    week[5] = core::sched::day_pattern({{8 * 60, 12 * 60}});
    return week;
}

// Install `target` as non-working for its whole week.  That is the shape of
// every dated exception once core/cal has built the calendar: core::sched has
// no dated-exception API, so core/cal merges each exception into the Week of
// the week it falls in (see core/cal/cal.h).
void make_day_non_working(core::sched::Calendar& calendar, date32 target) {
    const std::int32_t weekday = core::sched::weekday(target);
    const date32 week_start = core::sched::add_days(target, -weekday);
    core::sched::Week week = calendar.default_week();
    week[static_cast<std::size_t>(weekday)] = core::sched::DayPattern{};
    calendar.set_work_week(core::sched::day_of(week_start), week);
}

// Replace one weekday slot of the week containing `target`.
void set_week_day(core::sched::Calendar& calendar, date32 target, std::int32_t weekday,
                  const core::sched::DayPattern& pattern) {
    const date32 week_start =
        core::sched::add_days(target, -core::sched::weekday(target));
    core::sched::Week week = calendar.default_week();
    week[static_cast<std::size_t>(weekday)] = pattern;
    calendar.set_work_week(core::sched::day_of(week_start), week);
}

// The shading source must never touch bar geometry: compare every bar and the
// content width against a reference layout.
void check_same_bars(Context& context, const gantt::GanttLayout& actual,
                     const gantt::GanttLayout& expected, const std::string& what) {
    context.equal(actual.bars.size(), expected.bars.size(), what + ": bar count");
    if (actual.bars.size() != expected.bars.size()) {
        return;
    }
    for (std::size_t i = 0; i < actual.bars.size(); ++i) {
        const gantt::Bar& a = actual.bars[i];
        const gantt::Bar& b = expected.bars[i];
        context.equal(a.task_uid, b.task_uid, what + ": bar uid");
        context.rect(a.rect, b.rect.x, b.rect.y, b.rect.w, b.rect.h, what + ": bar rect");
        context.rect(a.fill, b.fill.x, b.fill.y, b.fill.w, b.fill.h, what + ": bar fill");
    }
    context.near(actual.content_width, expected.content_width, what + ": content width");
}

void test_calendar_shading(Context& context) {
    const gantt::GanttStyle style = fixed_style();
    const gantt::Timescale scale = day_scale();

    // The reference layout has no calendar at all (legacy weekend mask).
    const gantt::GanttLayout plain = build_gantt(fixed_source(), style, scale);
    context.equal(plain.timescale.origin, day(10, 1), "shading test origin is 1 Oct");

    // 1. A real Mon-Fri calendar reproduces the weekend shading day for day.
    //    (A working day also carries intra-day partial spans -- the evening and
    //    lunch gaps -- which a day-granularity renderer filters out, so the
    //    whole-day set is what this compares against the legacy mask.)
    gantt::GanttSource standard_source = fixed_source();
    const core::sched::Calendar standard(standard_week());
    standard_source.set_calendar(standard);
    const gantt::GanttLayout standard_layout =
        build_gantt(standard_source, style, scale);
    context.check(standard_layout.calendar != nullptr, "layout keeps the calendar");
    check_same_bars(context, standard_layout, plain, "a calendar does not move bars");

    const std::vector<gantt::NonworkingSpan> week_spans =
        standard_layout.nonworking_spans(380.0, 500.0);  // 20..25 October
    std::vector<gantt::NonworkingSpan> whole_days;
    for (const gantt::NonworkingSpan& span : week_spans) {
        if (!span.partial) {
            whole_days.push_back(span);
        }
    }
    context.equal(whole_days.size(), std::size_t{2},
                  "calendar shades Sat 24 and Sun 25");
    if (whole_days.size() == 2) {
        context.equal(whole_days[0].date, day(10, 24), "first span is Saturday 24");
        context.near(whole_days[0].x, 460.0, "Saturday x");
        context.near(whole_days[0].width, 20.0, "Saturday is a whole column");
        context.equal(whole_days[1].date, day(10, 25), "second span is Sunday 25");
        context.near(whole_days[1].x, 480.0, "Sunday x");
    }
    // Four working days x three intra-day gaps (00:00-08:00, 12:00-13:00,
    // 17:00-24:00) are exposed for an hour-scale renderer.
    context.equal(week_spans.size() - whole_days.size(), std::size_t{12},
                  "each working day has three intra-day non-working spans");
    context.check(standard_layout.is_working_day(day(10, 23)), "Friday is a working day");
    context.check(!standard_layout.is_working_day(day(10, 24)), "Saturday is not");
    context.check(!standard_layout.is_working_day(day(10, 25)), "Sunday is not");

    // 2. A dated exception (moved into its week) makes a weekday non-working.
    gantt::GanttSource holiday_source = fixed_source();
    core::sched::Calendar with_exception(standard_week());
    make_day_non_working(with_exception, day(10, 21));
    holiday_source.set_calendar(with_exception);
    const gantt::GanttLayout holiday_layout =
        build_gantt(holiday_source, style, scale);
    context.check(!holiday_layout.is_working_day(day(10, 21)),
                  "the exception makes Wednesday 21 non-working");
    context.check(holiday_layout.is_working_day(day(10, 20)),
                  "the exception leaves Tuesday working");
    const std::vector<gantt::NonworkingSpan> holiday =
        holiday_layout.nonworking_spans(380.0, 520.0);
    whole_days.clear();
    for (const gantt::NonworkingSpan& span : holiday) {
        if (!span.partial) {
            whole_days.push_back(span);
        }
    }
    context.equal(whole_days.size(), std::size_t{3},
                  "exception week has three whole non-working days");
    if (whole_days.size() == 3) {
        context.equal(whole_days[0].date, day(10, 21), "the exception is the first whole span");
        context.near(whole_days[0].x, 400.0, "Wednesday 21 x");
        context.near(whole_days[0].width, 20.0, "the exception spans a whole column");
        context.equal(whole_days[1].date, day(10, 24), "then Saturday 24");
        context.equal(whole_days[2].date, day(10, 25), "then Sunday 25");
    }
    check_same_bars(context, holiday_layout, plain,
                    "a dated exception does not move bars");

    // 3. A 4-hour Friday (08:00-12:00) is partly non-working: the unworked
    //    stretches are 00:00-08:00 and 12:00-24:00, i.e. a third and a half of
    //    the 20 px column.
    gantt::GanttSource short_source = fixed_source();
    const core::sched::Calendar short_friday(four_hour_friday_week());
    short_source.set_calendar(short_friday);
    const gantt::GanttLayout short_layout = build_gantt(short_source, style, scale);
    context.check(short_layout.is_working_day(day(10, 23)),
                  "a 4-hour Friday is still a working day");
    const std::vector<gantt::NonworkingSpan> friday =
        short_layout.nonworking_spans(440.0, 460.0);  // Friday 23 only
    context.equal(friday.size(), std::size_t{2},
                  "the 4-hour Friday has two non-working stretches");
    if (friday.size() == 2) {
        context.equal(friday[0].date, day(10, 23), "the first Friday span is the morning gap");
        context.near(friday[0].x, 440.0, "the unworked morning starts at midnight");
        context.near(friday[0].width, 20.0 * 8.0 / 24.0,
                     "00:00-08:00 is a third of the column");
        context.check(friday[0].partial, "the morning gap is partial");
        context.equal(friday[1].date, day(10, 23), "the second Friday span is the afternoon");
        context.near(friday[1].x, 450.0, "Friday shading starts at 12:00");
        context.near(friday[1].width, 10.0, "12:00-24:00 is half the column");
        context.check(friday[1].partial, "the afternoon gap is partial");
    }
    check_same_bars(context, short_layout, plain,
                    "a short Friday does not move bars");

    // 4. A special working Saturday (08:00-12:00) is partly working, so the
    // whole Saturday column disappears and only its gaps plus Sunday shade.
    gantt::GanttSource saturday_source = fixed_source();
    core::sched::Calendar working_saturday(standard_week());
    set_week_day(working_saturday, day(10, 24), 6,
                 core::sched::day_pattern({{8 * 60, 12 * 60}}));
    saturday_source.set_calendar(working_saturday);
    const gantt::GanttLayout saturday_layout =
        build_gantt(saturday_source, style, scale);
    context.check(saturday_layout.is_working_day(day(10, 24)),
                  "a special working Saturday is a working day");
    const std::vector<gantt::NonworkingSpan> saturday =
        saturday_layout.nonworking_spans(460.0, 500.0);  // Sat 24, Sun 25
    context.equal(saturday.size(), std::size_t{3},
                  "working Saturday leaves two partial spans plus Sunday");
    if (saturday.size() == 3) {
        context.equal(saturday[0].date, day(10, 24), "first Saturday span is the morning gap");
        context.near(saturday[0].x, 460.0, "the Saturday gap starts at midnight");
        context.near(saturday[0].width, 20.0 * 8.0 / 24.0, "00:00-08:00 on the Saturday");
        context.check(saturday[0].partial, "the Saturday morning gap is partial");
        context.equal(saturday[1].date, day(10, 24), "second Saturday span is the afternoon");
        context.near(saturday[1].x, 470.0, "Saturday shading resumes at 12:00");
        context.near(saturday[1].width, 10.0, "12:00-24:00 on the Saturday");
        context.check(saturday[1].partial, "the Saturday afternoon gap is partial");
        context.equal(saturday[2].date, day(10, 25), "Sunday is still whole");
        context.near(saturday[2].width, 20.0, "Sunday is a whole column");
        context.check(!saturday[2].partial, "Sunday is not partial");
    }

    // 5. The day-predicate overload: whole-day shading without a Calendar.
    gantt::GanttSource predicate_source = fixed_source();
    predicate_source.working_day = [](date32 d) {
        return core::sched::weekday(d) != 3;  // every Wednesday off
    };
    const gantt::GanttLayout predicate_layout =
        build_gantt(predicate_source, style, scale);
    context.check(!predicate_layout.is_working_day(day(10, 21)),
                  "predicate: Wednesday is non-working");
    context.check(predicate_layout.is_working_day(day(10, 24)),
                  "predicate: Saturday is working");
    const std::vector<gantt::NonworkingSpan> predicate =
        predicate_layout.nonworking_spans(380.0, 520.0);
    context.equal(predicate.size(), std::size_t{1},
                  "predicate shades only the Wednesday");
    if (predicate.size() == 1) {
        context.equal(predicate[0].date, day(10, 21), "predicate span is Wednesday 21");
        context.near(predicate[0].x, 400.0, "predicate Wednesday x");
    }

    // 6. nonworking_days still forces a whole day when a calendar is present.
    gantt::GanttSource forced_source = fixed_source();
    forced_source.set_calendar(standard);
    forced_source.nonworking_days.push_back(day(10, 22));
    const gantt::GanttLayout forced_layout = build_gantt(forced_source, style, scale);
    context.check(!forced_layout.is_working_day(day(10, 22)),
                  "an explicit holiday wins over the calendar");
    const std::vector<gantt::NonworkingSpan> forced =
        forced_layout.nonworking_spans(420.0, 440.0);  // Thursday 22 only
    context.equal(forced.size(), std::size_t{1}, "the forced Thursday is shaded whole");
    if (forced.size() == 1) {
        context.near(forced[0].width, 20.0, "a forced day keeps the whole column");
        context.check(!forced[0].partial, "a forced day is not partial");
    }

    // 6b. The calendar outranks a predicate set afterwards, and set_calendar()
    // clears a predicate set before it.  The layout keeps the calendar alive
    // after the source is gone (it owns a copy through the shared_ptr).
    gantt::GanttSource both_source = fixed_source();
    both_source.set_calendar(standard);
    both_source.working_day = [](date32) { return true; };  // would say "all working"
    const gantt::GanttLayout both_layout = build_gantt(both_source, style, scale);
    context.check(!both_layout.is_working_day(day(10, 24)),
                  "the calendar wins over a predicate set afterwards");

    gantt::GanttSource cleared_source = fixed_source();
    cleared_source.working_day = [](date32) { return false; };
    cleared_source.set_calendar(standard);
    context.check(cleared_source.working_day == nullptr,
                  "set_calendar clears the whole-day predicate");

    gantt::GanttLayout owned_layout;
    {
        gantt::GanttSource temporary = fixed_source();
        temporary.set_calendar(core::sched::Calendar(standard_week()));
        owned_layout = build_gantt(temporary, style, scale);
    }
    context.check(!owned_layout.is_working_day(day(10, 24)),
                  "the layout owns its calendar after the source is destroyed");

    // 7. A calendar whose day covers all 24 hours has no non-working time.
    core::sched::Week every_day{};
    for (core::sched::DayPattern& pattern : every_day) {
        pattern = core::sched::day_pattern({{0, 24 * 60}});
    }
    gantt::GanttSource open_source = fixed_source();
    open_source.set_calendar(core::sched::Calendar(every_day));
    context.equal(
        build_gantt(open_source, style, scale).nonworking_spans(380.0, 520.0).size(),
        std::size_t{0}, "a 24-hour calendar shades nothing");

    // 8. Partial spans follow the month scale's piecewise x mapping: the two
    // gaps of a 4-hour Friday in a 31-day October column of 300 px.
    gantt::Timescale month;
    month.unit = gantt::TimescaleUnit::month;
    month.origin = day(10, 1);
    month.pixels_per_unit = 300.0;
    gantt::GanttSource month_source;
    month_source.set_calendar(core::sched::Calendar(four_hour_friday_week()));
    const gantt::GanttLayout month_layout = build_gantt(month_source, style, month);
    const std::vector<gantt::NonworkingSpan> month_spans =
        month_layout.nonworking_spans(0.0, 300.0);
    const gantt::NonworkingSpan* morning_gap = nullptr;
    const gantt::NonworkingSpan* afternoon_gap = nullptr;
    for (const gantt::NonworkingSpan& span : month_spans) {
        if (span.date != day(10, 23)) {
            continue;
        }
        if (span.x < 300.0 * 22.4 / 31.0) {
            morning_gap = &span;
        } else {
            afternoon_gap = &span;
        }
    }
    context.check(morning_gap != nullptr && afternoon_gap != nullptr,
                  "the 4-hour Friday has two shaded gaps on the month scale");
    if (morning_gap != nullptr && afternoon_gap != nullptr) {
        context.near(morning_gap->x, 300.0 * 22.0 / 31.0,
                     "month-scale morning gap starts at midnight");
        context.near(morning_gap->width, 300.0 * (8.0 / 24.0) / 31.0,
                     "month-scale morning gap width");
        context.near(afternoon_gap->x, 300.0 * 22.5 / 31.0,
                     "month-scale afternoon gap x");
        context.near(afternoon_gap->width, 300.0 * 0.5 / 31.0,
                     "month-scale afternoon gap width");
        context.check(morning_gap->partial && afternoon_gap->partial,
                      "both month-scale Friday gaps are partial");
    }
}

void test_calendar_week_anchor(Context& context) {
    // Week columns follow the calendar's week anchor where there is one; the
    // legacy default (no calendar) stays ISO Monday.
    gantt::Timescale week;
    week.unit = gantt::TimescaleUnit::week;
    week.origin = day(11, 1);
    week.pixels_per_unit = 140.0;  // 20 px/day

    gantt::GanttSource source = fixed_source();
    const core::sched::Calendar standard(standard_week());
    source.set_calendar(standard);
    const gantt::GanttLayout layout = build_gantt(source, fixed_style(), week);
    context.equal(layout.timescale.week_start, 0,
                  "week anchor adopted from the calendar (0 = Sunday)");
    context.equal(layout.timescale.origin, day(10, 18),
                  "week origin snaps to Sunday 18 October");
    context.near(layout.timescale.x_for_date(day(10, 22)), 80.0,
                 "Thursday is four day columns into a Sunday week");
    const std::vector<gantt::HeaderCell> cells =
        layout.header(gantt::HeaderTier::minor, 0.0, 300.0);
    context.check(!cells.empty(), "week header cells exist");
    if (!cells.empty()) {
        context.equal(cells[0].label, std::string("18 Oct"),
                      "week label starts on Sunday 18 October");
        context.equal(cells[0].start, day(10, 18), "week cell start is the Sunday");
        context.equal(cells[0].finish, day(10, 24), "week cell ends on the Saturday");
    }

    // Opting out keeps the ISO Monday week (the no-calendar default).
    gantt::Timescale monday_week = week;
    monday_week.adopt_calendar_week_start = false;
    const gantt::GanttLayout monday_layout =
        build_gantt(source, fixed_style(), monday_week);
    context.equal(monday_layout.timescale.week_start, 1, "adoption can be switched off");
    context.equal(monday_layout.timescale.origin, day(10, 19),
                  "opt-out snaps to Monday 19 October");
    context.near(monday_layout.timescale.x_for_date(day(10, 22)), 60.0,
                 "Thursday is three columns into a Monday week");
}

void test_milestone_centring(Context& context) {
    // Settled decision: the diamond is centred in its own day column, not on
    // the date line.  It therefore keeps equal clearance on both sides of the
    // column and is never clipped at x = 0.
    gantt::GanttSource source;
    gantt::TaskBarInput milestone;
    milestone.uid = 1;
    milestone.start = at(10, 20, 8, 0);
    milestone.finish = milestone.start;
    milestone.kind = gantt::BarKind::milestone;
    source.tasks.push_back(milestone);
    const gantt::GanttLayout layout = build_gantt(source, fixed_style(), day_scale());
    const gantt::Bar bar = find_bar(layout, 1);
    context.rect(bar.rect, 384.0, 5.0, 12.0, 12.0, "milestone in its day column");
    context.near(bar.rect.center_x(), 390.0, "diamond centre is the column centre");
    context.near(bar.rect.x - 380.0, 4.0, "4 px clearance left of the column");
    context.near(400.0 - bar.rect.right(), 4.0, "4 px clearance right of the column");
    context.check(bar.rect.x >= 0.0, "the diamond stays inside the chart");
}

void test_backward_fs_lane(Context& context) {
    // Backward FS uses the same six-point lane shape as the adjacent-row pin
    // above, with the lane on the midpoint of the two row centre lines.  Here
    // the rows are two apart, so the lane is (11 + 55) / 2 = 33.
    gantt::GanttSource source;
    gantt::TaskBarInput pred;
    pred.uid = 1;
    pred.start = at(10, 22, 8, 0);
    pred.finish = at(10, 22, 17, 0);
    source.tasks.push_back(pred);
    gantt::TaskBarInput middle;
    middle.uid = 2;
    middle.start = at(10, 21, 8, 0);
    middle.finish = at(10, 21, 17, 0);
    source.tasks.push_back(middle);
    gantt::TaskBarInput succ;
    succ.uid = 3;
    succ.start = at(10, 20, 8, 0);
    succ.finish = at(10, 20, 17, 0);
    source.tasks.push_back(succ);
    gantt::TaskBarInput summary;
    summary.uid = 4;
    summary.start = at(10, 19, 8, 0);
    summary.finish = at(10, 25, 17, 0);
    summary.kind = gantt::BarKind::summary;
    source.tasks.push_back(summary);

    gantt::LinkInput link;
    link.predecessor_uid = 1;
    link.successor_uid = 3;
    link.type = gantt::LinkType::finish_to_start;
    source.links = {link};

    const gantt::GanttLayout layout = build_gantt(source, fixed_style(), day_scale());
    context.equal(layout.links.size(), std::size_t{1}, "one backward route");
    if (layout.links.size() != 1) {
        return;
    }
    const gantt::LinkRoute& route = layout.links[0];
    const std::vector<gantt::Point> expected = {
        {440.0, 11.0}, {448.0, 11.0}, {448.0, 33.0},
        {372.0, 33.0}, {372.0, 55.0}, {380.0, 55.0}};
    context.points(route.points, expected, "two-row backward FS points");
    context.near(route.points[2].y, 33.0,
                 "lane is the midpoint of the two row centre lines");
    context.point(route.arrow.tip, 380.0, 55.0,
                  "backward arrow tip on the successor left edge");
    context.point(route.arrow.direction, 1.0, 0.0, "backward arrow points right");
    context.rect(route.bounds, 372.0, 11.0, 76.0, 44.0, "backward route bounds");
}

void test_zoom_breadth(Context& context) {
    // Zoom specs: unit, default pixels-per-unit and header vocabulary.
    const gantt::ZoomSpec day_spec = gantt::zoom_spec(gantt::GanttZoom::day);
    const gantt::ZoomSpec week_spec = gantt::zoom_spec(gantt::GanttZoom::week);
    const gantt::ZoomSpec month_spec = gantt::zoom_spec(gantt::GanttZoom::month);
    context.equal(day_spec.unit, gantt::TimescaleUnit::day, "day zoom unit");
    context.near(day_spec.pixels_per_unit, 20.0, "day zoom 20 px/day");
    context.equal(week_spec.unit, gantt::TimescaleUnit::week, "week zoom unit");
    context.near(week_spec.pixels_per_unit, 140.0, "week zoom 140 px/week");
    context.equal(month_spec.unit, gantt::TimescaleUnit::month, "month zoom unit");
    context.near(month_spec.pixels_per_unit, 300.0, "month zoom 300 px/month");
    // As strings: two const char* compare by address (CI-5: clang under ASan keeps the literals apart).
    context.equal(std::string(gantt::zoom_name(gantt::GanttZoom::month)), std::string("month"),
                         "month zoom name");
    const gantt::Timescale day_scale_zoom =
        gantt::timescale_for_zoom(gantt::GanttZoom::day, day(10, 1));
    const gantt::Timescale week_scale_zoom =
        gantt::timescale_for_zoom(gantt::GanttZoom::week, day(10, 19));
    const gantt::Timescale month_scale_zoom =
        gantt::timescale_for_zoom(gantt::GanttZoom::month, day(10, 1));
    context.equal(gantt::zoom_for_timescale(day_scale_zoom), gantt::GanttZoom::day,
                  "zoom_for_timescale day");
    context.equal(gantt::zoom_for_timescale(week_scale_zoom), gantt::GanttZoom::week,
                  "zoom_for_timescale week");
    context.equal(gantt::zoom_for_timescale(month_scale_zoom), gantt::GanttZoom::month,
                  "zoom_for_timescale month");

    // Header text at each zoom.
    const auto day_minor = day_scale_zoom.header(gantt::HeaderTier::minor, 420.0, 440.0);
    context.check(day_minor.size() == 1 && day_minor[0].label == "22",
                  "day minor header is the day number");
    const auto day_major = day_scale_zoom.header(gantt::HeaderTier::major, 0.0, 1200.0);
    context.check(!day_major.empty() && day_major[0].label == "October 2026",
                  "day major header is month + year");
    const auto week_minor = week_scale_zoom.header(gantt::HeaderTier::minor, 0.0, 400.0);
    context.check(!week_minor.empty() && week_minor[0].label == "19 Oct",
                  "week minor header is the week start");
    const auto week_major = week_scale_zoom.header(gantt::HeaderTier::major, 0.0, 400.0);
    context.check(!week_major.empty() && week_major[0].label == "October 2026",
                  "week major header is month + year");
    const auto month_minor = month_scale_zoom.header(gantt::HeaderTier::minor, 0.0, 900.0);
    context.check(!month_minor.empty() && month_minor[0].label == "October",
                  "month minor header is the month name");
    const auto month_major = month_scale_zoom.header(gantt::HeaderTier::major, 0.0, 1000.0);
    context.check(!month_major.empty() && month_major[0].label == "2026",
                  "month major header is the year");

    // Bar label content: all eight choices, date ranges, and the percent clamp.
    gantt::TaskBarInput task;
    task.uid = 1;
    task.label = "Task 1";
    task.resource = "Crew A";
    task.start = at(10, 22, 8, 0);
    task.finish = at(10, 23, 17, 0);
    task.percent_complete = 0.42;
    gantt::GanttStyle style = fixed_style();
    style.bar_label = gantt::BarLabelContent::none;
    context.equal(gantt::bar_label_text(task, style), "", "label none");
    style.bar_label = gantt::BarLabelContent::name;
    context.equal(gantt::bar_label_text(task, style), "Task 1", "label name");
    style.bar_label = gantt::BarLabelContent::resource;
    context.equal(gantt::bar_label_text(task, style), "Crew A", "label resource");
    style.bar_label = gantt::BarLabelContent::dates;
    context.equal(gantt::bar_label_text(task, style), "2026-10-22 - 2026-10-23",
                         "label dates range");
    style.bar_label = gantt::BarLabelContent::percent;
    context.equal(gantt::bar_label_text(task, style), "42%", "label percent");
    style.bar_label = gantt::BarLabelContent::name_and_percent;
    context.equal(gantt::bar_label_text(task, style), "Task 1 42%",
                         "label name + percent");
    style.bar_label = gantt::BarLabelContent::name_and_resource;
    context.equal(gantt::bar_label_text(task, style), "Task 1 Crew A",
                         "label name + resource");
    style.bar_label = gantt::BarLabelContent::name_and_dates;
    context.equal(gantt::bar_label_text(task, style),
                         "Task 1 2026-10-22 - 2026-10-23", "label name + dates");
    gantt::TaskBarInput same_day = task;
    same_day.finish = same_day.start;
    style.bar_label = gantt::BarLabelContent::dates;
    context.equal(gantt::bar_label_text(same_day, style), "2026-10-22",
                         "label single-day dates");
    gantt::BarLabelContent content = gantt::BarLabelContent::none;
    context.check(gantt::bar_label_content_from_name("name_and_resource", content) &&
                      content == gantt::BarLabelContent::name_and_resource,
                  "bar label content parses from a name");
    context.check(!gantt::bar_label_content_from_name("nope", content),
                  "unknown bar label content rejected");

    // Shapes: rectangle / bracket / diamond, and the month zoom shrinks the
    // diamond so it stays inside its ~9.7 px day column.
    const gantt::GanttLayout day_layout =
        build_gantt(fixed_source(), fixed_style(), day_scale_zoom);
    context.equal(find_bar(day_layout, 1).shape, gantt::BarShape::rectangle,
                  "normal bar shape");
    context.equal(find_bar(day_layout, 4).shape, gantt::BarShape::summary_bracket,
                  "summary bar shape");
    context.equal(find_bar(day_layout, 3).shape, gantt::BarShape::diamond,
                  "milestone bar shape");
    const gantt::Bar day_summary = find_bar(day_layout, 4);
    context.near(day_summary.cap_left[0].x, day_summary.rect.x, "summary left cap starts at x");
    context.near(day_summary.cap_left[0].y, day_summary.rect.bottom(), "summary cap sits on the bar bottom");
    context.near(day_summary.cap_right[0].x, day_summary.rect.right(), "summary right cap starts at the right edge");
    const gantt::Bar month_milestone = find_bar(
        build_gantt(fixed_source(), fixed_style(), month_scale_zoom), 3);
    const double month_day_width = month_scale_zoom.x_for_day_start(day(10, 27)) -
                                   month_scale_zoom.x_for_day_start(day(10, 26));
    context.near(month_milestone.rect.w, month_day_width,
                 "month-zoom diamond shrinks to the day column");
    context.check(month_milestone.rect.x >= month_scale_zoom.x_for_day_start(day(10, 26)) &&
                      month_milestone.rect.right() <=
                          month_scale_zoom.x_for_day_start(day(10, 27)),
                  "month-zoom diamond stays inside its day column");
    context.near(month_milestone.diamond[0].x, month_milestone.rect.center_x(),
                 "diamond top is centred");

    // Hit tolerance follows the day-column width: 3 px at day/week, 2 px floor
    // at month; the layout uses it.
    context.near(gantt::hit_test_tolerance(day_scale_zoom, fixed_style()), 3.0,
                 "day hit tolerance is the old 3 px");
    context.near(gantt::hit_test_tolerance(week_scale_zoom, fixed_style()), 3.0,
                 "week hit tolerance is 3 px");
    context.near(gantt::hit_test_tolerance(month_scale_zoom, fixed_style()), 2.0,
                 "month hit tolerance falls to the 2 px floor");
    gantt::GanttStyle fixed = fixed_style();
    fixed.hit_tolerance_days = 0.0;
    context.near(gantt::hit_test_tolerance(month_scale_zoom, fixed), 3.0,
                 "hit_tolerance_days=0 uses the fixed tolerance");
    const gantt::GanttLayout month_layout =
        build_gantt(fixed_source(), fixed_style(), month_scale_zoom);
    const gantt::Bar month_one_day = find_bar(month_layout, 1);
    context.equal(
        month_layout.hit_test(month_one_day.rect.center_x(), month_one_day.rect.bottom() + 2.0).kind,
        gantt::HitKind::bar, "month layout hits 2 px below a bar");
    context.equal(
        month_layout.hit_test(month_one_day.rect.center_x(), month_one_day.rect.bottom() + 3.0).kind,
        gantt::HitKind::none, "month layout misses 3 px below a bar");

    // Routing at the month zoom: endpoints stay on the bar edges, the arrow is
    // a unit vector, and the route is hit-testable.
    gantt::GanttSource routed = fixed_source();
    gantt::LinkInput link;
    link.predecessor_uid = 1;
    link.successor_uid = 2;
    link.type = gantt::LinkType::finish_to_start;
    routed.links = {link};
    const gantt::GanttLayout month_routed =
        build_gantt(routed, fixed_style(), month_scale_zoom);
    context.equal(month_routed.links.size(), std::size_t{1}, "month route exists");
    if (month_routed.links.size() == 1) {
        const gantt::LinkRoute& route = month_routed.links[0];
        const gantt::Bar* pred = month_routed.bar(1);
        const gantt::Bar* succ = month_routed.bar(2);
        context.check(pred != nullptr && succ != nullptr, "month route has both bars");
        if (pred != nullptr && succ != nullptr) {
            context.near(route.points.front().x, pred->rect.right(),
                         "month route exits the predecessor finish edge");
            context.near(route.arrow.tip.x, succ->rect.x,
                         "month route enters the successor start edge");
            context.near(std::sqrt(route.arrow.direction.x * route.arrow.direction.x +
                                   route.arrow.direction.y * route.arrow.direction.y),
                         1.0, "month route arrow direction is a unit vector");
            context.check(std::isfinite(route.bounds.w) && std::isfinite(route.bounds.h),
                          "month route bounds are finite");
            if (route.points.size() >= 3) {
                const gantt::Point mid{(route.points[1].x + route.points[2].x) / 2.0,
                                       (route.points[1].y + route.points[2].y) / 2.0};
                context.equal(month_routed.hit_test(mid.x, mid.y).kind, gantt::HitKind::link,
                              "month route is hit-testable");
            }
        }
    }

    // Calendar shading at the week and month zooms: whole-day weekends survive.
    // Turn off the calendar-driven week anchor so the plain and shaded layouts
    // use the same columns (the anchor itself is covered by its own test).
    gantt::Timescale week_no_adopt = week_scale_zoom;
    week_no_adopt.adopt_calendar_week_start = false;
    gantt::GanttSource shaded = fixed_source();
    const core::sched::Calendar standard_zoom(standard_week());
    shaded.set_calendar(standard_zoom);
    const gantt::GanttLayout week_shaded =
        build_gantt(shaded, fixed_style(), week_no_adopt);
    const std::vector<gantt::NonworkingSpan> week_spans =
        week_shaded.nonworking_spans(0.0, 180.0);
    std::size_t week_whole = 0;
    for (const gantt::NonworkingSpan& span : week_spans) {
        if (!span.partial) {
            ++week_whole;
        }
    }
    context.equal(week_whole, std::size_t{2}, "week zoom shades Sat and Sun");
    const gantt::GanttLayout month_shaded =
        build_gantt(shaded, fixed_style(), month_scale_zoom);
    std::size_t month_whole = 0;
    for (const gantt::NonworkingSpan& span : month_shaded.nonworking_spans(0.0, 300.0)) {
        if (!span.partial) {
            ++month_whole;
        }
    }
    context.equal(month_whole, std::size_t{9},
                  "month zoom shades the nine October weekend days");
    check_same_bars(context, week_shaded,
                    build_gantt(fixed_source(), fixed_style(), week_no_adopt),
                    "week shading does not move bars");
}

#if defined(GANTT_HAS_CAL)
// tests/cal_tests.cpp), so its calendar names are not client data.  It carries
// one dated exception (Wednesday non-working) and a 4-hour Friday in the task
// calendar the three tasks select.  No name is printed; a missing corpus file
// skips as "corpus absent", and one that does not decode records why it was not
// compared under the same wording.
void test_holiday_probe(Context& context) {
    const std::string path = std::string(GANTT_PROBE_DIR) + "/holiday1.mpp";
    if (!fixture::corpus_present(path, "decoded-calendar shading not run")) {
        return;
    }

    core::sched::Calendar calendar;
    try {
        calendar = core::cal::load_project_calendar(path, "SiteCal");
    } catch (const std::exception& error) {
        fixture::corpus_absent(path, error.what());
        return;
    }

    // The decoded calendar's measured shape: Wednesday 21 non-working, Friday
    // 23 has 4 working hours, Thursday 22 is a full 8-hour day.
    const core::sched::ticks four_hours =
        4 * 60 * core::sched::kTenthsPerMinute;
    const core::sched::ticks eight_hours =
        8 * 60 * core::sched::kTenthsPerMinute;
    context.equal(calendar.day_total_ticks(core::sched::day_of(day(10, 21))),
                  static_cast<core::sched::ticks>(0),
                  "holiday1: Wednesday 21 October is non-working");
    context.equal(calendar.day_total_ticks(core::sched::day_of(day(10, 23))), four_hours,
                  "holiday1: Friday 23 October has 4 working hours");
    context.equal(calendar.day_total_ticks(core::sched::day_of(day(10, 22))), eight_hours,
                  "holiday1: Thursday 22 October is a full 8-hour day");

    // A nine-day task plus a task on the short Friday.  The timescale origin is
    // set to Monday 19 October so x = 0 is the first task day.
    gantt::GanttSource source;
    gantt::TaskBarInput long_task;
    long_task.uid = 1;
    long_task.start = at(10, 19, 8, 0);
    long_task.finish = at(10, 27, 17, 0);
    source.tasks.push_back(long_task);
    gantt::TaskBarInput friday;
    friday.uid = 2;
    friday.start = at(10, 23, 8, 0);
    friday.finish = at(10, 23, 12, 0);
    source.tasks.push_back(friday);

    gantt::Timescale scale = day_scale();
    scale.origin = day(10, 19);

    // Without the calendar (weekend mask only) is the bar-geometry reference.
    const gantt::GanttLayout plain = build_gantt(source, fixed_style(), scale);
    gantt::GanttSource with_calendar = source;
    with_calendar.set_calendar(calendar);
    const gantt::GanttLayout shaded = build_gantt(with_calendar, fixed_style(), scale);

    // Shading over Mon 19 .. Tue 27 (19 Oct is a Monday, so x = (day - 19) * 20):
    // the exception day is one whole span; every working day contributes its
    // intra-day gaps, including the short Friday's 00:00-08:00 and 12:00-24:00.
    const std::vector<gantt::NonworkingSpan> spans = shaded.nonworking_spans(0.0, 180.0);
    std::vector<gantt::NonworkingSpan> whole_days;
    const gantt::NonworkingSpan* friday_morning = nullptr;
    const gantt::NonworkingSpan* friday_afternoon = nullptr;
    for (const gantt::NonworkingSpan& span : spans) {
        if (!span.partial) {
            whole_days.push_back(span);
        } else if (span.date == day(10, 23)) {
            if (span.x < 85.0) {
                friday_morning = &span;
            } else {
                friday_afternoon = &span;
            }
        }
    }
    context.equal(whole_days.size(), std::size_t{3},
                  "holiday1: Wednesday + the weekend are whole non-working days");
    if (whole_days.size() == 3) {
        context.equal(whole_days[0].date, day(10, 21),
                      "holiday1: Wednesday is the exception day");
        context.near(whole_days[0].x, 40.0, "holiday1: Wednesday x is two columns in");
        context.near(whole_days[0].width, 20.0,
                     "holiday1: the exception day is a whole column");
        context.equal(whole_days[1].date, day(10, 24), "holiday1: Saturday is shaded");
        context.near(whole_days[1].x, 100.0, "holiday1: Saturday x");
        context.equal(whole_days[2].date, day(10, 25), "holiday1: Sunday is shaded");
        context.near(whole_days[2].x, 120.0, "holiday1: Sunday x");
    }
    context.check(friday_morning != nullptr && friday_afternoon != nullptr,
                  "holiday1: the short Friday has two partial spans");
    if (friday_morning != nullptr && friday_afternoon != nullptr) {
        context.near(friday_morning->x, 80.0, "holiday1: Friday morning gap starts at midnight");
        context.near(friday_morning->width, 20.0 * 8.0 / 24.0,
                     "holiday1: Friday 00:00-08:00 is a third of the column");
        context.near(friday_afternoon->x, 90.0, "holiday1: Friday shading resumes at 12:00");
        context.near(friday_afternoon->width, 10.0, "holiday1: Friday 12:00-24:00 is half a column");
    }
    context.check(shaded.is_working_day(day(10, 22)), "holiday1: Thursday is working");
    context.check(!shaded.is_working_day(day(10, 21)),
                  "holiday1: the exception day is not working");
    context.check(shaded.is_working_day(day(10, 23)),
                  "holiday1: the short Friday is still a working day");

    // Bar geometry is identical with and without the calendar.
    check_same_bars(context, shaded, plain, "holiday1: a calendar does not change bars");
    context.rect(find_bar(shaded, 1).rect, 0.0, 5.0, 180.0, 12.0,
                 "holiday1: the nine-day bar is nine columns");
    context.rect(find_bar(shaded, 2).rect, 80.0, 27.0, 20.0, 12.0,
                 "holiday1: the short-Friday task bar");
}
#endif

}  // namespace

int main() {
    Context context;
    test_timescale_round_trip(context);
    test_header_month_boundary(context);
    test_bar_geometry(context);
    test_weekend_gap(context);
    test_row_layout(context);
    test_dependency_routing(context);
    test_hit_testing(context);
    test_snapping(context);
    test_status_line(context);
    test_labels(context);
    test_stored_bar_texts(context);
    test_week_and_month_headers(context);
    test_bar_defaults(context);
    test_calendar_shading(context);
    test_calendar_week_anchor(context);
    test_milestone_centring(context);
    test_backward_fs_lane(context);
    test_zoom_breadth(context);
#if defined(GANTT_HAS_CAL)
    test_holiday_probe(context);
#endif
    return context.finish("gantt_tests");
}
