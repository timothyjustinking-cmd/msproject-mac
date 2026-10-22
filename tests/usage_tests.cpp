// tests/usage_tests.cpp -- W43: time-phased work buckets.
//
// Synthetic checks (no corpus needed):
//   * period enumeration (day across a weekend, Sunday-start weeks, civil
//     months, empty/NA ranges);
//   * the flat spread: cells sum to the assignment's Work and weekends and
//     calendar holidays get no buckets;
//   * task-usage == the sum of that task's assignments;
//   * resource-usage == the sum of that resource's assignments, including one
//     resource with two assignments;
//   * empty project and empty-assignment cases, unplaceable work, rendering;
//   * the mpp::Document bridge (usage_mpp.h) on a hand-built document, which
//     also covers the null-resource rule (PROJECT_A itself stores that sentinel on
//     every assignment, so the corpus case cannot exercise resource usage).
//
// Corpus-gated (SKIP when corpus/ is absent or MPP_NO_CORPUS is set; counts
// only, never a client name): PROJECT_A has 83 tasks / 22 resources / 67
// assignments; the projection's row counts and totals are pinned and every
// assignment's cells plus its unplaced work must equal its stored Work.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "cal.h"
#include "calendar.h"
#include "date.h"
#include "reader.h"
#include "usage.h"
#include "usage_mpp.h"
#include "view_test_support.h"

namespace {

using core::sched::Calendar;
using core::sched::date32;
using core::sched::DayPattern;
using core::sched::Week;
using core::usage::AssignmentInput;
using core::usage::Period;
using core::usage::PeriodSpan;
using core::usage::UsageInput;
using core::usage::UsageProjection;
using view_test::TestContext;

constexpr date32 kNa = core::sched::kDateNa;

// Monday-Friday, 08:00-12:00 and 13:00-17:00: the calendar every test in this
// file uses (the same standard week core/plan exposes).
Calendar standard_calendar() {
    const DayPattern day =
        core::sched::day_pattern({{8 * 60, 12 * 60}, {13 * 60, 17 * 60}});
    return Calendar(core::sched::make_week({
        {1, day},
        {2, day},
        {3, day},
        {4, day},
        {5, day},
    }));
}

date32 civil(int year, int month, int day, int hour = 0, int minute = 0) {
    return core::sched::from_ymd(year, month, day, hour, minute);
}

std::int32_t day_of(date32 value) { return core::sched::day_of(value); }

std::int64_t sum_cells(const std::vector<std::int64_t>& cells) {
    std::int64_t total = 0;
    for (const std::int64_t value : cells) {
        total += value;
    }
    return total;
}

std::string environment_value(const char* name) {
#ifdef _MSC_VER
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr) {
        return std::string();
    }
    const std::string value(buffer);
    std::free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
#endif
}

bool corpus_hidden() { return !environment_value("MPP_NO_CORPUS").empty(); }

bool file_exists(const std::string& path) {
    std::error_code error;
    return std::filesystem::exists(path, error);
}

AssignmentInput assignment(std::uint32_t uid, std::uint32_t task_uid,
                           std::uint32_t resource_uid, date32 start, date32 finish,
                           std::int64_t work_minutes) {
    AssignmentInput row;
    row.uid = uid;
    row.task_uid = task_uid;
    row.resource_uid = resource_uid;
    row.start = start;
    row.finish = finish;
    row.work_minutes = work_minutes;
    return row;
}

// ---------------------------------------------------------------------------

void test_period_enumeration(TestContext& ctx) {
    ctx.equal_string(core::usage::period_name(Period::day), "day", "day name");
    ctx.equal_string(core::usage::period_name(Period::week), "week", "week name");
    ctx.equal_string(core::usage::period_name(Period::month), "month", "month name");
    Period parsed = Period::day;
    ctx.check(core::usage::period_from_name("month", parsed) && parsed == Period::month,
              "period_from_name month");
    ctx.check(!core::usage::period_from_name("year", parsed),
              "period_from_name rejects year");

    // --- day: Friday through Monday includes the weekend as (empty) rows.
    const std::vector<PeriodSpan> days = core::usage::enumerate_periods(
        Period::day, civil(2026, 10, 16, 8, 0), civil(2026, 10, 19, 17, 0));
    ctx.equal(days.size(), std::size_t{4}, "Fri-Mon is four day periods");
    if (days.size() == 4) {
        ctx.equal(days[0].first_day, day_of(civil(2026, 10, 16)), "first day is Friday");
        ctx.equal(days[1].first_day, day_of(civil(2026, 10, 17)), "second day is Saturday");
        ctx.equal(days[2].first_day, day_of(civil(2026, 10, 18)), "third day is Sunday");
        ctx.equal(days[3].first_day, day_of(civil(2026, 10, 19)), "fourth day is Monday");
        ctx.equal(days[1].day_count(), 1, "Saturday span is one day");
        ctx.check(days[1].contains(day_of(civil(2026, 10, 17))), "contains Saturday");
        ctx.check(!days[1].contains(day_of(civil(2026, 10, 18))), "does not contain Sunday");
        ctx.equal(days[1].first_date(), (static_cast<date32>(day_of(civil(2026, 10, 17))) << 16),
                  "first_date is date-only packed");
        ctx.equal(days[2].first_date(), civil(2026, 10, 18), "Sunday date-only");
        const Calendar calendar = standard_calendar();
        ctx.check(calendar.is_working_day(days[0].first_day), "Friday is a working day");
        ctx.check(!calendar.is_working_day(days[1].first_day), "Saturday is not working");
        ctx.check(!calendar.is_working_day(days[2].first_day), "Sunday is not working");
        ctx.check(calendar.is_working_day(days[3].first_day), "Monday is a working day");
    }

    // --- week: decision, weeks start on Sunday (weekday 0).
    const std::vector<PeriodSpan> weeks = core::usage::enumerate_periods(
        Period::week, civil(2026, 10, 14), civil(2026, 10, 27));
    ctx.equal(weeks.size(), std::size_t{3}, "Wed-Tue covers three Sunday weeks");
    if (weeks.size() == 3) {
        ctx.equal(weeks[0].first_day, day_of(civil(2026, 10, 14)), "first week clipped start");
        ctx.equal(weeks[0].last_day, day_of(civil(2026, 10, 17)), "first week ends Saturday");
        ctx.equal(weeks[0].day_count(), 4, "first week is four clipped days");
        ctx.equal(weeks[1].first_day, day_of(civil(2026, 10, 18)), "second week starts Sunday");
        ctx.equal(weeks[1].last_day, day_of(civil(2026, 10, 24)), "second week ends Saturday");
        ctx.equal(weeks[1].day_count(), 7, "middle week is seven days");
        ctx.equal(weeks[2].first_day, day_of(civil(2026, 10, 25)), "third week starts Sunday");
        ctx.equal(weeks[2].last_day, day_of(civil(2026, 10, 27)), "third week clipped end");
    }

    // --- month: civil months, clipped at both ends.  2026 is not a leap year.
    const std::vector<PeriodSpan> months = core::usage::enumerate_periods(
        Period::month, civil(2026, 1, 31), civil(2026, 3, 1));
    ctx.equal(months.size(), std::size_t{3}, "Jan 31 - Mar 1 covers three months");
    if (months.size() == 3) {
        ctx.equal(months[0].first_day, day_of(civil(2026, 1, 31)), "January clipped start");
        ctx.equal(months[0].last_day, day_of(civil(2026, 1, 31)), "January clipped end");
        ctx.equal(months[1].first_day, day_of(civil(2026, 2, 1)), "February starts the 1st");
        ctx.equal(months[1].last_day, day_of(civil(2026, 2, 28)), "February ends the 28th");
        ctx.equal(months[1].day_count(), 28, "February is 28 days");
        ctx.equal(months[2].first_day, day_of(civil(2026, 3, 1)), "March starts the 1st");
        ctx.equal(months[2].last_day, day_of(civil(2026, 3, 1)), "March clipped end");
    }

    // Empty and invalid ranges.
    ctx.check(core::usage::enumerate_periods(Period::day, civil(2026, 10, 20),
                                             civil(2026, 10, 19)).empty(),
              "reversed range is empty");
    ctx.check(core::usage::enumerate_periods(Period::day, kNa, civil(2026, 10, 19)).empty(),
              "NA first date is empty");
    ctx.check(core::usage::enumerate_periods(Period::day, civil(2026, 10, 19), kNa).empty(),
              "NA last date is empty");
    const std::vector<PeriodSpan> one =
        core::usage::enumerate_periods(Period::day, civil(2026, 10, 19), civil(2026, 10, 19));
    ctx.equal(one.size(), std::size_t{1}, "same-day range is one period");
}

void test_flat_spread(TestContext& ctx) {
    const Calendar calendar = standard_calendar();
    UsageInput input;
    input.task_uids = {1};
    input.resource_uids = {10};
    input.assignments.push_back(assignment(100, 1, 10, civil(2026, 10, 19, 8, 0),
                                           civil(2026, 10, 21, 17, 0), 1440));

    const UsageProjection projection = core::usage::project_usage(input, calendar, Period::day);
    ctx.equal(projection.periods.size(), std::size_t{3}, "Mon-Wed is three day periods");
    ctx.equal(projection.assignments.size(), std::size_t{1}, "one assignment row");
    ctx.equal(projection.total_minutes, std::int64_t{1440}, "projection total");
    ctx.equal(projection.unplaced_assignments, std::size_t{0}, "nothing is unplaced");

    ctx.equal(projection.tasks.rows.size(), std::size_t{1}, "one task row");
    ctx.equal(projection.resources.rows.size(), std::size_t{1}, "one resource row");
    const core::usage::UsageRow* task = projection.tasks.row(1);
    ctx.check(task != nullptr, "task row uid 1 exists");
    if (task != nullptr) {
        ctx.equal(task->work_minutes, std::int64_t{1440}, "task total is the assignment Work");
        ctx.equal(task->minutes.size(), std::size_t{3}, "task row has three cells");
        ctx.equal(task->minutes[0], std::int64_t{480}, "Monday cell");
        ctx.equal(task->minutes[1], std::int64_t{480}, "Tuesday cell");
        ctx.equal(task->minutes[2], std::int64_t{480}, "Wednesday cell");
        ctx.equal(sum_cells(task->minutes), task->work_minutes, "task cells sum to Work");
    }
    const core::usage::UsageRow* resource = projection.resources.row(10);
    ctx.check(resource != nullptr, "resource row uid 10 exists");
    if (resource != nullptr) {
        ctx.equal(resource->work_minutes, std::int64_t{1440}, "resource total");
        ctx.equal(sum_cells(resource->minutes), resource->work_minutes,
                  "resource cells sum to Work");
    }
    const core::usage::AssignmentUsage& row = projection.assignments[0];
    ctx.equal(row.uid, 100u, "assignment uid");
    ctx.equal(row.work_minutes, std::int64_t{1440}, "assignment Work");
    ctx.equal(sum_cells(row.minutes), row.work_minutes, "assignment cells sum to Work");
    ctx.equal(projection.tasks.row_total(0), std::int64_t{1440}, "row_total helper");
    ctx.equal(projection.tasks.period_total(0), std::int64_t{480}, "period_total helper");
    ctx.equal(projection.tasks.peak_minutes(), std::int64_t{480}, "peak_minutes helper");

    // A Work total smaller than the span still spreads proportionally, exactly.
    UsageInput partial;
    partial.task_uids = {1};
    partial.resource_uids = {10};
    partial.assignments.push_back(assignment(101, 1, 10, civil(2026, 10, 19, 8, 0),
                                             civil(2026, 10, 21, 17, 0), 720));
    const UsageProjection partial_projection =
        core::usage::project_usage(partial, calendar, Period::day);
    ctx.equal(partial_projection.tasks.rows[0].minutes[0], std::int64_t{240},
              "partial Monday share");
    ctx.equal(sum_cells(partial_projection.tasks.rows[0].minutes), std::int64_t{720},
              "partial Work sums exactly");

    // A half-day assignment keeps its exact total in one period.
    UsageInput half;
    half.task_uids = {1};
    half.resource_uids = {10};
    half.assignments.push_back(assignment(102, 1, 10, civil(2026, 10, 19, 8, 0),
                                          civil(2026, 10, 19, 12, 0), 240));
    const UsageProjection half_projection =
        core::usage::project_usage(half, calendar, Period::day);
    ctx.equal(half_projection.periods.size(), std::size_t{1}, "half day is one period");
    ctx.equal(half_projection.tasks.rows[0].minutes[0], std::int64_t{240},
              "half-day bucket");
}

void test_weekend_gets_no_buckets(TestContext& ctx) {
    const Calendar calendar = standard_calendar();
    UsageInput input;
    input.task_uids = {1};
    input.resource_uids = {10};
    // Friday 08:00 to Monday 17:00 is two working days (960 min).
    input.assignments.push_back(assignment(100, 1, 10, civil(2026, 10, 16, 8, 0),
                                           civil(2026, 10, 19, 17, 0), 960));

    const UsageProjection projection = core::usage::project_usage(input, calendar, Period::day);
    ctx.equal(projection.periods.size(), std::size_t{4}, "Friday-Monday is four day periods");
    if (projection.tasks.rows[0].minutes.size() == 4) {
        ctx.equal(projection.tasks.rows[0].minutes[0], std::int64_t{480}, "Friday bucket");
        ctx.equal(projection.tasks.rows[0].minutes[1], std::int64_t{0}, "Saturday bucket is 0");
        ctx.equal(projection.tasks.rows[0].minutes[2], std::int64_t{0}, "Sunday bucket is 0");
        ctx.equal(projection.tasks.rows[0].minutes[3], std::int64_t{480}, "Monday bucket");
    }
    ctx.equal(sum_cells(projection.tasks.rows[0].minutes), std::int64_t{960},
              "weekend does not absorb Work");
    ctx.equal(projection.assignments[0].unplaced_minutes, std::int64_t{0},
              "weekend assignment is fully placed");

    // The same assignment over week periods: the period list starts at the
    // assignment's own first day (Friday), so week 1 is the clipped Fri-Sat
    // span and week 2 is the clipped Sun-Mon span.
    input.assignments[0].work_minutes = 960;
    const UsageProjection weekly =
        core::usage::project_usage(input, calendar, Period::week);
    ctx.equal(weekly.periods.size(), std::size_t{2}, "two Sunday-start weeks");
    if (weekly.periods.size() == 2) {
        ctx.equal(weekly.periods[0].first_day, day_of(civil(2026, 10, 16)),
                  "first week is clipped to the Friday start");
        ctx.equal(weekly.periods[0].last_day, day_of(civil(2026, 10, 17)),
                  "first week ends on Saturday");
        ctx.equal(weekly.periods[1].first_day, day_of(civil(2026, 10, 18)),
                  "second week starts Sunday the 18th");
        ctx.equal(sum_cells(weekly.assignments[0].minutes), std::int64_t{960},
                  "weekly cells sum to Work");
    }
}

// A dated calendar exception (a holiday) removes working time, so no bucket is
// created and the remaining Work is redistributed over the working days.  The
// calendar is the only source of working time; the usage code never reimplements
// day patterns.
void test_holiday_gets_no_buckets(TestContext& ctx) {
    const DayPattern day =
        core::sched::day_pattern({{8 * 60, 12 * 60}, {13 * 60, 17 * 60}});
    Week week = core::sched::make_week({
        {1, day},
        {2, day},
        {3, day},
        {4, day},
        {5, day},
    });
    week[3] = DayPattern{};  // Wednesday of this week is a holiday
    Calendar calendar(week);
    // Install the exception week that starts on Sunday 2026-10-18.  Mon-Fri
    // 19..23 October are then 480, 480, 0, 480, 480 working minutes.
    calendar.set_work_week(day_of(civil(2026, 10, 18)), week);

    UsageInput input;
    input.task_uids = {1};
    input.resource_uids = {10};
    // Four working days of Work across a five-day span.
    input.assignments.push_back(assignment(100, 1, 10, civil(2026, 10, 19, 8, 0),
                                           civil(2026, 10, 23, 17, 0), 1920));
    const UsageProjection projection = core::usage::project_usage(input, calendar, Period::day);
    ctx.equal(projection.periods.size(), std::size_t{5}, "five day periods");
    if (projection.tasks.rows[0].minutes.size() == 5) {
        ctx.equal(projection.tasks.rows[0].minutes[0], std::int64_t{480}, "Monday bucket");
        ctx.equal(projection.tasks.rows[0].minutes[1], std::int64_t{480}, "Tuesday bucket");
        ctx.equal(projection.tasks.rows[0].minutes[2], std::int64_t{0},
                  "holiday Wednesday bucket is 0");
        ctx.equal(projection.tasks.rows[0].minutes[3], std::int64_t{480}, "Thursday bucket");
        ctx.equal(projection.tasks.rows[0].minutes[4], std::int64_t{480}, "Friday bucket");
    }
    ctx.equal(sum_cells(projection.tasks.rows[0].minutes), std::int64_t{1920},
              "holiday does not absorb Work");
    ctx.equal(projection.unplaced_assignments, std::size_t{0},
              "holiday assignment is fully placed");
}

void test_task_usage_is_sum_of_assignments(TestContext& ctx) {
    const Calendar calendar = standard_calendar();
    UsageInput input;
    input.task_uids = {1};
    input.resource_uids = {10, 11};
    input.assignments.push_back(assignment(100, 1, 10, civil(2026, 10, 19, 8, 0),
                                           civil(2026, 10, 19, 17, 0), 480));
    input.assignments.push_back(assignment(101, 1, 11, civil(2026, 10, 20, 8, 0),
                                           civil(2026, 10, 20, 17, 0), 480));

    const UsageProjection projection = core::usage::project_usage(input, calendar, Period::day);
    ctx.equal(projection.periods.size(), std::size_t{2}, "two day periods");
    ctx.equal(projection.assignments.size(), std::size_t{2}, "two assignment rows");
    ctx.equal(projection.tasks.rows.size(), std::size_t{1}, "one task row");
    const core::usage::UsageRow& task = projection.tasks.rows[0];
    ctx.equal(task.uid, 1u, "task uid");
    ctx.equal(task.work_minutes, std::int64_t{960}, "task total is the sum of assignments");
    if (task.minutes.size() == 2) {
        ctx.equal(task.minutes[0], std::int64_t{480}, "task Monday cell");
        ctx.equal(task.minutes[1], std::int64_t{480}, "task Tuesday cell");
    }
    ctx.equal(sum_cells(task.minutes), std::int64_t{960}, "task cells sum");
    ctx.equal(projection.tasks.total_minutes, std::int64_t{960}, "task table total");

    std::int64_t assignment_sum = 0;
    for (const core::usage::AssignmentUsage& row : projection.assignments) {
        assignment_sum += row.work_minutes;
    }
    ctx.equal(task.work_minutes, assignment_sum, "task-usage == sum of its assignments");
    ctx.equal(projection.resources.rows.size(), std::size_t{2}, "two resource rows");
}

void test_resource_usage_is_sum_of_assignments(TestContext& ctx) {
    const Calendar calendar = standard_calendar();
    UsageInput input;
    input.task_uids = {1, 2};
    input.resource_uids = {10};
    // One resource, two tasks, two assignments.
    input.assignments.push_back(assignment(100, 1, 10, civil(2026, 10, 19, 8, 0),
                                           civil(2026, 10, 19, 12, 0), 240));
    input.assignments.push_back(assignment(101, 2, 10, civil(2026, 10, 19, 13, 0),
                                           civil(2026, 10, 19, 17, 0), 240));

    const UsageProjection projection = core::usage::project_usage(input, calendar, Period::day);
    ctx.equal(projection.periods.size(), std::size_t{1}, "both assignments are one day");
    ctx.equal(projection.resources.rows.size(), std::size_t{1},
              "one resource with two assignments is one row");
    const core::usage::UsageRow& resource = projection.resources.rows[0];
    ctx.equal(resource.uid, 10u, "resource uid");
    ctx.equal(resource.work_minutes, std::int64_t{480}, "resource total is the sum");
    ctx.equal(resource.minutes[0], std::int64_t{480}, "resource day cell");
    ctx.equal(projection.resources.total_minutes, std::int64_t{480}, "resource table total");
    ctx.equal(projection.total_minutes, std::int64_t{480}, "projection total");

    std::int64_t assignment_sum = 0;
    for (const core::usage::AssignmentUsage& row : projection.assignments) {
        assignment_sum += row.work_minutes;
    }
    ctx.equal(resource.work_minutes, assignment_sum,
              "resource-usage == sum of its assignments");

    // A resource with no assignment still has a row, at zero.
    UsageInput with_spare = input;
    with_spare.resource_uids.push_back(11);
    const UsageProjection spare =
        core::usage::project_usage(with_spare, calendar, Period::day);
    ctx.equal(spare.resources.rows.size(), std::size_t{2}, "spare resource gets a row");
    const core::usage::UsageRow* spare_row = spare.resources.row(11);
    ctx.check(spare_row != nullptr, "spare resource row exists");
    if (spare_row != nullptr) {
        ctx.equal(spare_row->work_minutes, std::int64_t{0}, "spare resource total is 0");
        ctx.equal(spare_row->minutes[0], std::int64_t{0}, "spare resource cell is 0");
    }
}

void test_empty_and_unplaced(TestContext& ctx) {
    const Calendar calendar = standard_calendar();

    const UsageProjection empty =
        core::usage::project_usage(UsageInput{}, calendar, Period::day);
    ctx.check(empty.periods.empty(), "empty project has no periods");
    ctx.check(empty.assignments.empty(), "empty project has no assignments");
    ctx.check(empty.tasks.rows.empty(), "empty project has no task rows");
    ctx.check(empty.resources.rows.empty(), "empty project has no resource rows");
    ctx.equal(empty.total_minutes, std::int64_t{0}, "empty project total is 0");
    ctx.equal(empty.unplaced_assignments, std::size_t{0}, "empty project has no unplaced work");

    // Tasks/resources but no assignments: rows exist, buckets are empty.
    UsageInput bare;
    bare.task_uids = {1, 2};
    bare.resource_uids = {10};
    const UsageProjection bare_projection =
        core::usage::project_usage(bare, calendar, Period::day);
    ctx.equal(bare_projection.tasks.rows.size(), std::size_t{2}, "bare task rows");
    ctx.equal(bare_projection.resources.rows.size(), std::size_t{1}, "bare resource rows");
    ctx.check(bare_projection.periods.empty(), "bare project has no periods");
    ctx.check(bare_projection.tasks.rows[0].minutes.empty(), "bare row has no cells");
    ctx.equal(bare_projection.tasks.total_minutes, std::int64_t{0}, "bare total is 0");

    // Work with no usable dates cannot be phased, but is not lost.
    UsageInput unplaced;
    unplaced.task_uids = {1};
    unplaced.resource_uids = {10};
    unplaced.assignments.push_back(assignment(100, 1, 10, kNa, kNa, 120));
    const UsageProjection unplaced_projection =
        core::usage::project_usage(unplaced, calendar, Period::day);
    ctx.equal(unplaced_projection.unplaced_assignments, std::size_t{1},
              "unplaceable assignment is counted");
    ctx.equal(unplaced_projection.total_minutes, std::int64_t{120},
              "unplaced work stays in the projection total");
    ctx.equal(unplaced_projection.tasks.rows[0].work_minutes, std::int64_t{120},
              "unplaced work stays in the task total");
    ctx.check(unplaced_projection.tasks.rows[0].minutes.empty(),
              "unplaced work contributes no cells");
    ctx.equal(unplaced_projection.assignments[0].unplaced_minutes, std::int64_t{120},
              "assignment unplaced minutes");
    ctx.equal(sum_cells(unplaced_projection.assignments[0].minutes), std::int64_t{0},
              "assignment has no placed cells");

    // finish == start is not placeable either.
    UsageInput zero_span;
    zero_span.task_uids = {1};
    zero_span.assignments.push_back(
        assignment(101, 1, 0, civil(2026, 10, 19, 8, 0), civil(2026, 10, 19, 8, 0), 60));
    const UsageProjection zero_span_projection =
        core::usage::project_usage(zero_span, calendar, Period::day);
    ctx.equal(zero_span_projection.unplaced_assignments, std::size_t{1},
              "zero-span assignment is unplaced");
    ctx.check(zero_span_projection.resources.rows.empty(),
              "resource uid 0 creates no resource row");

    // Zero work is placed trivially: not an unplaced assignment.
    UsageInput zero_work;
    zero_work.task_uids = {1};
    zero_work.assignments.push_back(assignment(102, 1, 0, kNa, kNa, 0));
    const UsageProjection zero_work_projection =
        core::usage::project_usage(zero_work, calendar, Period::day);
    ctx.equal(zero_work_projection.unplaced_assignments, std::size_t{0},
              "zero work is not unplaced");
    ctx.equal(zero_work_projection.total_minutes, std::int64_t{0}, "zero work total");
}

void test_render(TestContext& ctx) {
    const Calendar calendar = standard_calendar();
    UsageInput input;
    input.task_uids = {1};
    input.resource_uids = {10};
    input.assignments.push_back(assignment(100, 1, 10, civil(2026, 10, 19, 8, 0),
                                           civil(2026, 10, 21, 17, 0), 1440));
    const UsageProjection projection = core::usage::project_usage(input, calendar, Period::day);
    const std::string text = core::usage::render_usage_table(projection.tasks);
    ctx.contains(text, "usage period=day periods=3 rows=1 total_minutes=1440",
                 "render header");
    ctx.contains(text, "row uid=1 total=1440:", "render row header");
    ctx.contains(text, "2026-10-19=480", "render Monday cell");
    ctx.contains(text, "2026-10-21=480", "render Wednesday cell");

    const std::string empty_text =
        core::usage::render_usage_table(core::usage::UsageTable{});
    ctx.contains(empty_text, "periods=0 rows=0 total_minutes=0", "render empty table");
}

// The reader bridge (usage_mpp.h) needs no file: a hand-built mpp::Document
// pins the field mapping and the null-resource rule.
void test_mpp_bridge(TestContext& ctx) {
    mpp::Document document;
    mpp::Task first_task;
    first_task.uid = 1;
    document.tasks.push_back(first_task);
    mpp::Task second_task;
    second_task.uid = 2;
    document.tasks.push_back(second_task);
    mpp::Resource resource;
    resource.uid = 10;
    resource.row_id = 10;
    document.resources.push_back(resource);

    mpp::Assignment assignment_row;
    assignment_row.uid = 100;
    assignment_row.task_uid = 1;
    assignment_row.resource_uid_raw = 10;
    assignment_row.units_x100 = 10000.0;
    assignment_row.work_x1000 = 480000.0;  // 480 minutes
    assignment_row.start.raw = civil(2026, 10, 19, 8, 0);
    assignment_row.finish.raw = civil(2026, 10, 19, 17, 0);
    document.assignments.push_back(assignment_row);

    const UsageInput input = core::usage::usage_input_from_mpp(document);
    ctx.equal(input.task_uids.size(), std::size_t{2}, "bridge copies task uids");
    ctx.equal(input.resource_uids.size(), std::size_t{1}, "bridge copies resource uids");
    ctx.equal(input.assignments.size(), std::size_t{1}, "bridge copies assignments");
    if (input.assignments.size() == 1) {
        const AssignmentInput& row = input.assignments[0];
        ctx.equal(row.uid, 100u, "bridge assignment uid");
        ctx.equal(row.task_uid, 1u, "bridge task uid");
        ctx.equal(row.resource_uid, 10u, "bridge resource uid");
        ctx.equal(row.start, civil(2026, 10, 19, 8, 0), "bridge start is the packed value");
        ctx.equal(row.finish, civil(2026, 10, 19, 17, 0), "bridge finish");
        ctx.equal(row.work_minutes, std::int64_t{480}, "bridge Work minutes");
    }

    const Calendar calendar = standard_calendar();
    const UsageProjection projection = core::usage::project_usage(input, calendar, Period::day);
    ctx.equal(projection.resources.rows.size(), std::size_t{1}, "bridge resource row");
    ctx.equal(projection.resources.rows[0].work_minutes, std::int64_t{480},
              "bridge resource row Work");
    ctx.equal(projection.tasks.rows.size(), std::size_t{2}, "bridge task rows");

    // kNullResourceUid (and 0) becomes "no resource" in the neutral input.
    document.assignments[0].resource_uid_raw = mpp::kNullResourceUid;
    const UsageInput null_input = core::usage::usage_input_from_mpp(document);
    ctx.equal(null_input.assignments[0].resource_uid, 0u,
              "null-resource sentinel maps to 0");
    const UsageProjection null_projection =
        core::usage::project_usage(null_input, calendar, Period::day);
    ctx.equal(null_projection.resources.rows[0].work_minutes, std::int64_t{0},
              "null-resource assignment contributes no resource work");
}

// ---------------------------------------------------------------------------
// Corpus case: counts only.  PROJECT_A is 83 tasks / 22 resources / 67 assignments.
// ---------------------------------------------------------------------------

void test_corpus_project_a(TestContext& ctx) {
    if (corpus_hidden()) {
        std::cout << "SKIP: PROJECT_A usage checks (MPP_NO_CORPUS is set: corpus forced absent)\n";
        return;
    }
    if (!file_exists(USAGE_PROJECT_A_PATH)) {
        std::cout << "SKIP: " << USAGE_PROJECT_A_PATH
                  << " is not present (corpus is git-ignored)\n";
        return;
    }

    mpp::Document document;
    try {
        document = mpp::load_file(USAGE_PROJECT_A_PATH);
    } catch (const std::exception& error) {
        ctx.check(false, std::string("PROJECT_A loads: ") + error.what());
        return;
    }

    Calendar calendar = standard_calendar();
    try {
        calendar = core::cal::load_project_calendar(USAGE_PROJECT_A_PATH);
    } catch (const std::exception& error) {
        std::cout << "  note: calendar decode failed, using the standard week: "
                  << error.what() << '\n';
    }

    const UsageInput input = core::usage::usage_input_from_mpp(document);
    ctx.equal(input.task_uids.size(), std::size_t{83}, "PROJECT_A live tasks");
    ctx.equal(input.resource_uids.size(), std::size_t{22}, "PROJECT_A live resources");
    ctx.equal(input.assignments.size(), std::size_t{67}, "PROJECT_A live assignments");

    const UsageProjection projection = core::usage::project_usage(input, calendar, Period::day);
    ctx.equal(projection.tasks.rows.size(), std::size_t{83}, "PROJECT_A task-usage rows");
    ctx.equal(projection.resources.rows.size(), std::size_t{22}, "PROJECT_A resource-usage rows");
    ctx.equal(projection.assignments.size(), std::size_t{67}, "PROJECT_A assignment rows");
    ctx.check(!projection.periods.empty(), "PROJECT_A has a non-empty day period list");

    std::int64_t input_total = 0;
    for (const AssignmentInput& row : input.assignments) {
        input_total += row.work_minutes > 0 ? row.work_minutes : 0;
    }
    std::int64_t task_total = 0;
    for (const core::usage::UsageRow& row : projection.tasks.rows) {
        task_total += row.work_minutes;
    }
    std::int64_t resource_total = 0;
    for (const core::usage::UsageRow& row : projection.resources.rows) {
        resource_total += row.work_minutes;
    }
    ctx.equal(projection.total_minutes, input_total, "PROJECT_A projection total is the assignment Work");
    ctx.equal(task_total, input_total, "PROJECT_A task rows carry all assignment Work");

    // MEASURED (core/mpp reader, W40, tests/mpp_reader_tests.cpp): all 67 live
    // PROJECT_A assignment rows store kNullResourceUid at +8, so PROJECT_A has no resource
    // attribution to project.  The 22 resource rows are present but all zero;
    // the resource-usage path is covered by the synthetic assign1-style input.
    std::size_t assignments_with_resource = 0;
    for (const AssignmentInput& row : input.assignments) {
        if (row.resource_uid != 0) {
            ++assignments_with_resource;
        }
    }
    ctx.equal(assignments_with_resource, std::size_t{0},
              "PROJECT_A assignment rows store the null-resource sentinel");
    ctx.equal(resource_total, std::int64_t{0}, "PROJECT_A resource rows are all zero work");

    // Every assignment's cells plus its unplaced Work must equal its stored Work.
    bool all_balanced = true;
    std::int64_t unplaced_total = 0;
    for (const core::usage::AssignmentUsage& row : projection.assignments) {
        const std::int64_t placed = sum_cells(row.minutes);
        if (placed + row.unplaced_minutes != row.work_minutes) {
            all_balanced = false;
        }
        unplaced_total += row.unplaced_minutes;
    }
    ctx.check(all_balanced, "PROJECT_A assignment cells plus unplaced equal stored Work");
    ctx.equal(unplaced_total, std::int64_t{0}, "PROJECT_A has no unplaced assignment work");
    ctx.equal(projection.unplaced_assignments, std::size_t{0}, "PROJECT_A unplaced count is 0");

    // Task rows and resource rows sum their own period cells.
    bool rows_balanced = true;
    for (const core::usage::UsageRow& row : projection.tasks.rows) {
        if (sum_cells(row.minutes) != row.work_minutes) {
            rows_balanced = false;
        }
    }
    for (const core::usage::UsageRow& row : projection.resources.rows) {
        if (sum_cells(row.minutes) != row.work_minutes) {
            rows_balanced = false;
        }
    }
    ctx.check(rows_balanced, "PROJECT_A row cells sum to row Work");

    std::cout << "CORPUS: project_a: " << projection.periods.size() << " day periods, "
              << projection.assignments.size() << " assignments, "
              << projection.resources.rows.size() << " resources, "
              << projection.tasks.rows.size() << " tasks, " << projection.total_minutes
              << " work minutes (task rows " << task_total << ", resource rows "
              << resource_total << ")\n";
}

}  // namespace

int main() {
    TestContext ctx;
    test_period_enumeration(ctx);
    test_flat_spread(ctx);
    test_weekend_gets_no_buckets(ctx);
    test_holiday_gets_no_buckets(ctx);
    test_task_usage_is_sum_of_assignments(ctx);
    test_resource_usage_is_sum_of_assignments(ctx);
    test_empty_and_unplaced(ctx);
    test_render(ctx);
    test_mpp_bridge(ctx);
    test_corpus_project_a(ctx);
    return ctx.finish("usage_tests");
}
