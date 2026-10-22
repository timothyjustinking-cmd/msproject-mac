// core/usage/usage.cpp -- period enumeration and the flat time-phased spread.
//
// The algorithm is deliberately small: periods are built from the engine's
// day numbers, and an assignment's integer Work is spread over its working
// time in proportion to each period's working ticks.  Cumulative rounding makes
// the cells sum to Work exactly.  See usage.h for the measured facts and the
// decisions (week starts Sunday, flat contour, unplaced work).

#include "usage.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <utility>

namespace core::usage {
namespace {

// Day number of a packed date, or 0 when it is not a real date.  day 0 is the
// invalid/placeholder value; date32 days are 1-based.  No floating point is
// involved in a date.
std::int32_t day_number(date32 value) {
    if (sched::is_na(value) || sched::is_overflow(value)) {
        return 0;
    }
    const std::int32_t day = sched::day_of(value);
    return day >= sched::kMinDay ? day : 0;
}

date32 date_only(std::int32_t day) { return static_cast<date32>(day) << 16; }

std::string format_day(std::int32_t day) {
    char buffer[32] = {};
    const sched::CivilDate civil = sched::to_civil(date_only(day));
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", civil.year, civil.month,
                  civil.day);
    return std::string(buffer);
}

// Distribute `assignment.work_minutes` into `out` (one cell per period).
// Returns the number of minutes that could not be placed.  The cells sum to
// work_minutes - unplaced exactly.
std::int64_t spread_assignment(const AssignmentInput& assignment,
                               const sched::Calendar& calendar,
                               const std::vector<PeriodSpan>& periods,
                               std::vector<std::int64_t>& out) {
    out.assign(periods.size(), 0);
    const std::int64_t work = assignment.work_minutes > 0 ? assignment.work_minutes : 0;
    if (work == 0) {
        return 0;
    }
    const std::int32_t start_day = day_number(assignment.start);
    const std::int32_t finish_day = day_number(assignment.finish);
    if (start_day == 0 || finish_day == 0 || finish_day < start_day) {
        return work;
    }
    const sched::ticks span_ticks =
        calendar.work_time_between(assignment.start, assignment.finish);
    if (span_ticks <= 0) {
        return work;
    }

    // Working ticks per period, clipped to the assignment's own span.
    std::vector<std::int64_t> ticks(periods.size(), 0);
    std::int64_t span_sum = 0;
    for (std::size_t i = 0; i < periods.size(); ++i) {
        const date32 period_start = periods[i].first_date();
        const date32 period_end = periods[i].end_date();
        const date32 lo = std::max(period_start, assignment.start);
        const date32 hi = std::min(period_end, assignment.finish);
        if (sched::is_na(lo) || sched::is_na(hi) || lo >= hi) {
            continue;
        }
        const sched::ticks value = calendar.work_time_between(lo, hi);
        ticks[i] = value;
        span_sum += value;
    }
    if (span_sum <= 0) {
        return work;
    }

    // Cumulative rounding: cell i is the difference of two rounded cumulative
    // shares, so every cell is >= 0 and the final cumulative share is exactly
    // work (cumulative == span_sum for a covering period list).
    std::int64_t cumulative = 0;
    std::int64_t previous = 0;
    for (std::size_t i = 0; i < periods.size(); ++i) {
        cumulative += ticks[i];
        const std::int64_t target = (work * cumulative + span_sum / 2) / span_sum;
        out[i] = target - previous;
        previous = target;
    }
    if (previous != work && !periods.empty()) {
        out.back() += work - previous;
    }
    return 0;
}

std::size_t index_of(const std::unordered_map<std::uint32_t, std::size_t>& map,
                     std::uint32_t uid) {
    const auto found = map.find(uid);
    return found == map.end() ? static_cast<std::size_t>(-1) : found->second;
}

}  // namespace

const char* period_name(Period period) noexcept {
    switch (period) {
        case Period::day:
            return "day";
        case Period::week:
            return "week";
        case Period::month:
            return "month";
    }
    return "day";
}

bool period_from_name(std::string_view name, Period& out) noexcept {
    if (name == "day") {
        out = Period::day;
        return true;
    }
    if (name == "week") {
        out = Period::week;
        return true;
    }
    if (name == "month") {
        out = Period::month;
        return true;
    }
    return false;
}

date32 PeriodSpan::first_date() const noexcept { return date_only(first_day); }

date32 PeriodSpan::last_date() const noexcept { return date_only(last_day); }

date32 PeriodSpan::end_date() const noexcept {
    return date_only(last_day >= sched::kMaxDay ? last_day : last_day + 1);
}

std::vector<PeriodSpan> enumerate_periods(Period period, std::int32_t first_day,
                                          std::int32_t last_day) {
    std::vector<PeriodSpan> spans;
    if (first_day < sched::kMinDay || last_day > sched::kMaxDay || last_day < first_day) {
        return spans;
    }
    switch (period) {
        case Period::day: {
            spans.reserve(static_cast<std::size_t>(last_day - first_day + 1));
            for (std::int32_t day = first_day; day <= last_day; ++day) {
                spans.push_back(PeriodSpan{day, day});
            }
            break;
        }
        case Period::week: {
            // Decision: weekday 0 (Sunday) starts a week.  See usage.h.
            std::int32_t cursor = first_day - sched::weekday_of_day(first_day);
            for (;;) {
                const std::int32_t span_first = std::max(cursor, first_day);
                const std::int32_t span_last = std::min(cursor + 6, last_day);
                spans.push_back(PeriodSpan{span_first, span_last});
                if (cursor + 6 >= last_day) {
                    break;
                }
                cursor += 7;
            }
            break;
        }
        case Period::month: {
            const sched::CivilDate civil = sched::to_civil(date_only(first_day));
            std::int32_t year = civil.year;
            std::int32_t month = civil.month;
            for (;;) {
                const std::int32_t month_start =
                    sched::day_of(sched::from_ymd(year, month, 1));
                std::int32_t next_year = year;
                std::int32_t next_month = month + 1;
                if (next_month == 13) {
                    next_month = 1;
                    ++next_year;
                }
                const std::int32_t next_start =
                    sched::day_of(sched::from_ymd(next_year, next_month, 1));
                const std::int32_t month_last = next_start - 1;
                const std::int32_t span_first = std::max(month_start, first_day);
                const std::int32_t span_last = std::min(month_last, last_day);
                spans.push_back(PeriodSpan{span_first, span_last});
                if (month_last >= last_day) {
                    break;
                }
                year = next_year;
                month = next_month;
            }
            break;
        }
    }
    return spans;
}

std::vector<PeriodSpan> enumerate_periods(Period period, date32 first, date32 last) {
    const std::int32_t first_day = day_number(first);
    const std::int32_t last_day = day_number(last);
    if (first_day == 0 || last_day == 0) {
        return {};
    }
    return enumerate_periods(period, first_day, last_day);
}

int UsageTable::row_of_uid(std::uint32_t uid) const noexcept {
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].uid == uid) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const UsageRow* UsageTable::row(std::uint32_t uid) const noexcept {
    const int index = row_of_uid(uid);
    return index < 0 ? nullptr : &rows[static_cast<std::size_t>(index)];
}

std::int64_t UsageTable::minutes_at(std::size_t row_index,
                                    std::size_t period_index) const noexcept {
    if (row_index >= rows.size() || period_index >= rows[row_index].minutes.size()) {
        return 0;
    }
    return rows[row_index].minutes[period_index];
}

std::int64_t UsageTable::row_total(std::size_t row_index) const noexcept {
    if (row_index >= rows.size()) {
        return 0;
    }
    std::int64_t total = 0;
    for (const std::int64_t value : rows[row_index].minutes) {
        total += value;
    }
    return total;
}

std::int64_t UsageTable::period_total(std::size_t period_index) const noexcept {
    std::int64_t total = 0;
    for (const UsageRow& row : rows) {
        if (period_index < row.minutes.size()) {
            total += row.minutes[period_index];
        }
    }
    return total;
}

std::int64_t UsageTable::peak_minutes() const noexcept {
    std::int64_t peak = 0;
    for (const UsageRow& row : rows) {
        for (const std::int64_t value : row.minutes) {
            peak = std::max(peak, value);
        }
    }
    return peak;
}

UsageProjection project_usage(const UsageInput& input, const sched::Calendar& calendar,
                              Period period) {
    UsageProjection projection;
    projection.period = period;
    projection.tasks.period = period;
    projection.resources.period = period;

    std::unordered_map<std::uint32_t, std::size_t> task_rows;
    std::unordered_map<std::uint32_t, std::size_t> resource_rows;

    for (const std::uint32_t uid : input.task_uids) {
        if (uid == 0 || task_rows.count(uid) != 0) {
            continue;
        }
        task_rows.emplace(uid, projection.tasks.rows.size());
        projection.tasks.rows.push_back(UsageRow{uid, 0, {}});
    }
    for (const std::uint32_t uid : input.resource_uids) {
        if (uid == 0 || resource_rows.count(uid) != 0) {
            continue;
        }
        resource_rows.emplace(uid, projection.resources.rows.size());
        projection.resources.rows.push_back(UsageRow{uid, 0, {}});
    }

    // Pass 1: the global date range is the union of the assignments that can
    // actually be phased (valid dates and at least one working tick).
    std::int32_t first_day = 0;
    std::int32_t last_day = 0;
    for (const AssignmentInput& assignment : input.assignments) {
        if (assignment.work_minutes <= 0) {
            continue;
        }
        const std::int32_t start_day = day_number(assignment.start);
        const std::int32_t finish_day = day_number(assignment.finish);
        if (start_day == 0 || finish_day == 0 || finish_day < start_day) {
            continue;
        }
        if (calendar.work_time_between(assignment.start, assignment.finish) <= 0) {
            continue;
        }
        first_day = first_day == 0 ? start_day : std::min(first_day, start_day);
        last_day = last_day == 0 ? finish_day : std::max(last_day, finish_day);
    }

    if (first_day != 0 && last_day != 0) {
        projection.periods = enumerate_periods(period, first_day, last_day);
    }
    projection.tasks.periods = projection.periods;
    projection.resources.periods = projection.periods;
    for (UsageRow& row : projection.tasks.rows) {
        row.minutes.assign(projection.periods.size(), 0);
    }
    for (UsageRow& row : projection.resources.rows) {
        row.minutes.assign(projection.periods.size(), 0);
    }

    // Pass 2: spread each assignment and add it to its task and resource rows.
    projection.assignments.reserve(input.assignments.size());
    for (const AssignmentInput& assignment : input.assignments) {
        std::vector<std::int64_t> cells;
        const std::int64_t unplaced =
            spread_assignment(assignment, calendar, projection.periods, cells);
        const std::int64_t work =
            assignment.work_minutes > 0 ? assignment.work_minutes : 0;

        AssignmentUsage row;
        row.uid = assignment.uid;
        row.task_uid = assignment.task_uid;
        row.resource_uid = assignment.resource_uid;
        row.work_minutes = work;
        row.unplaced_minutes = unplaced;
        row.minutes = std::move(cells);
        projection.assignments.push_back(std::move(row));
        projection.total_minutes += work;
        if (unplaced > 0) {
            ++projection.unplaced_assignments;
        }

        const std::size_t task_index = index_of(task_rows, assignment.task_uid);
        if (assignment.task_uid != 0 && task_index != static_cast<std::size_t>(-1)) {
            UsageRow& target = projection.tasks.rows[task_index];
            target.work_minutes += work;
            for (std::size_t i = 0; i < target.minutes.size(); ++i) {
                target.minutes[i] += projection.assignments.back().minutes[i];
            }
        }
        const std::size_t resource_index = index_of(resource_rows, assignment.resource_uid);
        if (assignment.resource_uid != 0 &&
            resource_index != static_cast<std::size_t>(-1)) {
            UsageRow& target = projection.resources.rows[resource_index];
            target.work_minutes += work;
            for (std::size_t i = 0; i < target.minutes.size(); ++i) {
                target.minutes[i] += projection.assignments.back().minutes[i];
            }
        }
    }

    for (const UsageRow& row : projection.tasks.rows) {
        projection.tasks.total_minutes += row.work_minutes;
    }
    for (const UsageRow& row : projection.resources.rows) {
        projection.resources.total_minutes += row.work_minutes;
    }
    return projection;
}

std::string render_usage_table(const UsageTable& table) {
    std::string out = "usage period=";
    out += period_name(table.period);
    out += " periods=" + std::to_string(table.periods.size());
    out += " rows=" + std::to_string(table.rows.size());
    out += " total_minutes=" + std::to_string(table.total_minutes);
    out += '\n';
    for (const UsageRow& row : table.rows) {
        out += "row uid=" + std::to_string(row.uid);
        out += " total=" + std::to_string(row.work_minutes) + ':';
        bool any = false;
        for (std::size_t i = 0; i < row.minutes.size() && i < table.periods.size(); ++i) {
            if (row.minutes[i] == 0) {
                continue;
            }
            any = true;
            out += ' ';
            out += format_day(table.periods[i].first_day);
            out += '=';
            out += std::to_string(row.minutes[i]);
        }
        if (!any) {
            out += " (none)";
        }
        out += '\n';
    }
    return out;
}

}  // namespace core::usage
