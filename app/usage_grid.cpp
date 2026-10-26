// app/usage_grid.cpp -- the usage views' logical rows, details and per-period
// values.  See usage_grid.h for the measured rules and their evidence.

#include "usage_grid.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "date.h"
#include "usage.h"
#include "view_model.h"

namespace app {
namespace {

using core::usage::Period;
using core::usage::PeriodSpan;
using msproject::viewstate::UsageDetailKind;

const char* detail_label(UsageDetailKind kind) noexcept {
    switch (kind) {
        case UsageDetailKind::work:
            return "Work";
        case UsageDetailKind::actual_work:
            return "Actual Work";
        case UsageDetailKind::cost:
            return "Cost";
        case UsageDetailKind::other:
            break;
    }
    return "";
}

bool detail_is_valued(UsageDetailKind kind) noexcept {
    return kind != UsageDetailKind::other;
}

// The rendered bottom tier for a fallback period.  Formats 20 (single-letter
// day) and 13 (week date) are the wave-31 fixture's stored choices; a week
// column's label is the week's Monday date.
msproject::viewstate::TimescaleTier tier_for_period(Period period) {
    using msproject::viewstate::TimescaleUnit;
    msproject::viewstate::TimescaleTier tier;
    tier.unit = period == Period::day     ? TimescaleUnit::days
                : period == Period::week  ? TimescaleUnit::weeks
                                          : TimescaleUnit::months;
    tier.count = 1;
    tier.format = period == Period::day ? 20 : 13;
    tier.align = 1;  // centred, the fixture's measured bottom alignment
    return tier;
}

// The rendered top tier for a fallback period: the measured weeks-over-days
// pair, months over weeks, and no top tier over months (no year label is
// measured).
msproject::viewstate::TimescaleTier default_major_for(Period period) {
    using msproject::viewstate::TimescaleUnit;
    msproject::viewstate::TimescaleTier tier;
    tier.count = 1;
    tier.align = 0;  // left, the fixture's measured middle alignment
    tier.format = 13;
    if (period == Period::day) {
        tier.unit = TimescaleUnit::weeks;
    } else if (period == Period::week) {
        tier.unit = TimescaleUnit::months;
    } else {
        tier.unit = TimescaleUnit::none;
        tier.format = 35;
    }
    return tier;
}

// round(work * percent / 100) with half-up rounding, so 0 % is exactly zero
// and 100 % is exactly the work total.
std::int64_t scaled_actual(std::int64_t work, int percent) noexcept {
    if (work <= 0 || percent <= 0) {
        return 0;
    }
    if (percent >= 100) {
        return work;
    }
    return (work * percent + 50) / 100;
}

// The resource's standard rate in cents per hour; 0 for a missing, non-work or
// rate-less resource (the value rules in usage_grid.h).
std::int64_t standard_rate_cents(const ResourceRow* resource) noexcept {
    if (resource == nullptr ||
        resource->type != core::view::ResourceType::work) {
        return 0;
    }
    const double cents = resource->standard_rate_per_hour * 100.0;
    if (!(cents > 0.0)) {
        return 0;
    }
    return static_cast<std::int64_t>(std::llround(cents));
}

// round(minutes * cents_per_hour / 60), half up.
std::int64_t cost_cents(std::int64_t minutes, std::int64_t cents_per_hour) noexcept {
    if (minutes <= 0 || cents_per_hour <= 0) {
        return 0;
    }
    return (minutes * cents_per_hour + 30) / 60;
}

int month_start_day(int day) noexcept {
    core::sched::CivilDate civil =
        core::sched::to_civil(core::sched::pack(day, 0));
    civil.day = 1;
    return core::sched::day_of(core::sched::from_civil(civil));
}

int add_months_day(int day, int months) noexcept {
    core::sched::CivilDate civil =
        core::sched::to_civil(core::sched::pack(day, 0));
    const int total = civil.year * 12 + (civil.month - 1) + months;
    core::sched::CivilDate moved;
    moved.year = total / 12;
    moved.month = total % 12 + 1;
    moved.day = 1;
    return core::sched::day_of(core::sched::from_civil(moved));
}

// The columns of one grid: `count` stored bottom-tier periods per column, with
// Monday week boundaries and civil month boundaries; the first and last column
// are clipped to [first_day, last_day] as Project clips them.
std::vector<PeriodSpan> build_periods(Period period, int count, int first_day,
                                      int last_day) {
    std::vector<PeriodSpan> spans;
    if (first_day > last_day) {
        return spans;
    }
    std::vector<int> starts;
    starts.push_back(first_day);
    if (period == Period::day) {
        for (int start = first_day + count; start <= last_day; start += count) {
            starts.push_back(start);
        }
    } else if (period == Period::week) {
        const int weekday = core::sched::weekday_of_day(first_day);  // 0 = Sunday
        const int monday = first_day - ((weekday + 6) % 7);
        for (int start = monday + 7 * count; start <= last_day; start += 7 * count) {
            starts.push_back(start);
        }
    } else {
        const int month = month_start_day(first_day);
        for (int start = add_months_day(month, count); start <= last_day;
             start = add_months_day(start, count)) {
            starts.push_back(start);
        }
    }
    for (std::size_t index = 0; index < starts.size(); ++index) {
        const int end = index + 1 < starts.size()
                            ? std::min(starts[index + 1] - 1, last_day)
                            : last_day;
        if (end >= starts[index]) {
            spans.push_back(PeriodSpan{starts[index], end});
        }
    }
    return spans;
}

// One assignment's Work minutes summed into the grid's columns.
std::vector<std::int64_t> column_work(
    const core::usage::AssignmentUsage& assignment,
    const std::vector<PeriodSpan>& day_periods,
    const std::vector<PeriodSpan>& columns) {
    std::vector<std::int64_t> minutes(columns.size(), 0);
    const std::size_t count =
        std::min(day_periods.size(), assignment.minutes.size());
    for (std::size_t day_index = 0; day_index < count; ++day_index) {
        const int day = day_periods[day_index].first_day;
        for (std::size_t column = 0; column < columns.size(); ++column) {
            if (columns[column].contains(day)) {
                minutes[column] += assignment.minutes[day_index];
                break;
            }
        }
    }
    return minutes;
}

std::string task_label_for(const PlanInput& input, std::uint32_t uid) {
    for (const TaskRow& task : input.tasks) {
        if (task.uid == uid) {
            return task.name.empty() ? "task " + std::to_string(uid) : task.name;
        }
    }
    return "task " + std::to_string(uid);
}

std::string resource_label_for(const PlanInput& input, std::uint32_t uid) {
    if (uid == 0) {
        return {};  // Project's usage row for an unassigned task has no label
    }
    for (const ResourceRow& resource : input.resources) {
        if (resource.uid == uid) {
            return resource.name.empty() ? "resource " + std::to_string(uid)
                                         : resource.name;
        }
    }
    return "resource " + std::to_string(uid);
}

const TaskRow* find_task(const PlanInput& input, std::uint32_t uid) noexcept {
    for (const TaskRow& task : input.tasks) {
        if (task.uid == uid) {
            return &task;
        }
    }
    return nullptr;
}

const ResourceRow* find_resource(const PlanInput& input, std::uint32_t uid) noexcept {
    if (uid == 0) {
        return nullptr;
    }
    for (const ResourceRow& resource : input.resources) {
        if (resource.uid == uid) {
            return &resource;
        }
    }
    return nullptr;
}

// One assignment row: values per stored detail in this grid's columns.
UsageGridRow assignment_row(const PlanInput& input, const UsageGrid& grid,
                            const AssignmentRow& assignment,
                            const core::usage::AssignmentUsage& usage,
                            const std::vector<PeriodSpan>& day_periods) {
    UsageGridRow row;
    row.kind = UsageGridRowKind::assignment;
    row.uid = assignment.uid;
    row.parent_uid = grid.resource ? assignment.resource_uid : assignment.task_uid;
    row.child = true;
    row.label = grid.resource ? task_label_for(input, assignment.task_uid)
                              : resource_label_for(input, assignment.resource_uid);

    const std::vector<std::int64_t> work =
        column_work(usage, day_periods, grid.periods);
    const TaskRow* task = find_task(input, assignment.task_uid);
    const int percent =
        task == nullptr ? 0 : std::clamp<int>(task->percent_complete, 0, 100);
    const std::int64_t rate =
        standard_rate_cents(find_resource(input, assignment.resource_uid));

    row.values.resize(grid.details.size());
    row.totals.assign(grid.details.size(), 0);
    for (std::size_t detail_index = 0; detail_index < grid.details.size();
         ++detail_index) {
        const UsageDetailKind kind = grid.details[detail_index].kind;
        std::vector<std::int64_t>& values = row.values[detail_index];
        values.assign(grid.periods.size(), 0);
        if (kind == UsageDetailKind::work) {
            values = work;
            row.totals[detail_index] = usage.work_minutes;
        } else if (kind == UsageDetailKind::actual_work) {
            std::int64_t cumulative_work = 0;
            std::int64_t previous = 0;
            for (std::size_t period = 0; period < work.size(); ++period) {
                cumulative_work += work[period];
                const std::int64_t cumulative =
                    scaled_actual(cumulative_work, percent);
                values[period] = cumulative - previous;
                previous = cumulative;
            }
            row.totals[detail_index] = scaled_actual(usage.work_minutes, percent);
        } else if (kind == UsageDetailKind::cost) {
            for (std::size_t period = 0; period < work.size(); ++period) {
                values[period] = cost_cents(work[period], rate);
            }
            row.totals[detail_index] = cost_cents(usage.work_minutes, rate);
        }
    }
    return row;
}

void add_values(UsageGridRow& target, const UsageGridRow& addition) {
    for (std::size_t detail = 0; detail < target.values.size(); ++detail) {
        for (std::size_t period = 0; period < target.values[detail].size();
             ++period) {
            target.values[detail][period] += addition.values[detail][period];
        }
        target.totals[detail] += addition.totals[detail];
    }
}

UsageGridRow parent_row(UsageGridRowKind kind, std::uint32_t uid,
                        const std::string& label, const UsageGrid& grid) {
    UsageGridRow row;
    row.kind = kind;
    row.uid = uid;
    row.label = label;
    row.values.assign(grid.details.size(), {});
    row.totals.assign(grid.details.size(), 0);
    for (std::vector<std::int64_t>& values : row.values) {
        values.assign(grid.periods.size(), 0);
    }
    return row;
}

}  // namespace

UsageGrid make_usage_grid(const PlanInput& input, const app::UsageViewStyle& style,
                          bool resource, const core::sched::Calendar& calendar,
                          core::usage::Period fallback_period) {
    UsageGrid grid;
    grid.resource = resource;
    grid.stored_style = style.decoded;

    if (style.decoded) {
        for (const msproject::viewstate::UsageDetail& detail :
             style.stored.details) {
            if (!detail_is_valued(detail.kind)) {
                // Measured on Project's stock files: a stored detail id with
                // no value rule draws no line in the usage sheet.  Keep the
                // decoder's warning, invent nothing.
                continue;
            }
            UsageGridDetail column;
            column.kind = detail.kind;
            column.label = detail_label(detail.kind);
            column.cost = detail.kind == UsageDetailKind::cost;
            grid.details.push_back(std::move(column));
        }
        grid.warnings = style.stored.warnings;
        grid.periods_per_column =
            style.stored.minor.count > 0 ? style.stored.minor.count : 1;
        grid.major_tier = style.stored.major;
        grid.minor_tier = style.stored.minor;
        switch (style.stored.minor.unit) {
            case msproject::viewstate::TimescaleUnit::days:
                grid.period = Period::day;
                break;
            case msproject::viewstate::TimescaleUnit::weeks:
                grid.period = Period::week;
                break;
            case msproject::viewstate::TimescaleUnit::months:
                grid.period = Period::month;
                break;
            default:
                grid.warnings.push_back(
                    std::string("the stored bottom timescale unit is ") +
                    msproject::viewstate::timescale_unit_name(
                        style.stored.minor.unit) +
                    "; this wave draws days");
                grid.period = Period::day;
                grid.periods_per_column = 1;
                grid.minor_tier = tier_for_period(Period::day);
                break;
        }
        // The top tier renders the measured week-date and month shapes; a
        // tierless view draws no top labels, and a unit this wave cannot draw
        // falls back to weeks with a warning.
        if (grid.major_tier.unit != msproject::viewstate::TimescaleUnit::weeks &&
            grid.major_tier.unit != msproject::viewstate::TimescaleUnit::months &&
            grid.major_tier.unit != msproject::viewstate::TimescaleUnit::none) {
            grid.warnings.push_back(
                std::string("the stored top timescale unit is ") +
                msproject::viewstate::timescale_unit_name(grid.major_tier.unit) +
                "; this wave draws weeks");
            grid.major_tier = default_major_for(grid.period);
        }
    } else {
        // The brief's default: Work, and the shell's Day/Week/Month choice.
        UsageGridDetail work;
        work.kind = UsageDetailKind::work;
        work.label = detail_label(UsageDetailKind::work);
        grid.details.push_back(std::move(work));
        grid.period = fallback_period;
        grid.periods_per_column = 1;
        grid.minor_tier = tier_for_period(fallback_period);
        grid.major_tier = default_major_for(fallback_period);
    }
    if (grid.minor_tier.count <= 0) {
        grid.minor_tier.count = 1;
    }
    if (grid.major_tier.count <= 0) {
        grid.major_tier.count = 1;
    }

    const core::usage::UsageProjection projection = core::usage::project_usage(
        usage_input_from_plan(input), calendar, core::usage::Period::day);
    if (!projection.periods.empty()) {
        grid.periods = build_periods(grid.period, grid.periods_per_column,
                                     projection.periods.front().first_day,
                                     projection.periods.back().last_day);
    }

    if (resource) {
        for (const ResourceRow& resource_row : input.resources) {
            if (resource_row.uid == 0) {
                continue;
            }
            const UsageGridRowKind kind = UsageGridRowKind::resource;
            const std::size_t parent_index = grid.rows.size();
            UsageGridRow parent = parent_row(
                kind, resource_row.uid,
                resource_label_for(input, resource_row.uid), grid);
            parent.parent_index = parent_index;
            parent.display_id = resource_row.id;
            parent.numbered = true;
            grid.rows.push_back(std::move(parent));
            for (std::size_t index = 0; index < input.assignments.size();
                 ++index) {
                const AssignmentRow& assignment = input.assignments[index];
                if (assignment.resource_uid != resource_row.uid) {
                    continue;
                }
                UsageGridRow row = assignment_row(
                    input, grid, assignment, projection.assignments[index],
                    projection.periods);
                row.parent_index = parent_index;
                add_values(grid.rows[parent_index], row);
                grid.rows.push_back(std::move(row));
            }
        }
    } else {
        if (input.show_project_summary) {
            const TaskRow project = project_summary_row(input);
            const std::size_t parent_index = grid.rows.size();
            UsageGridRow parent = parent_row(UsageGridRowKind::task, 0,
                                             project.name, grid);
            parent.parent_index = parent_index;
            parent.display_id = project.id;
            parent.numbered = true;
            grid.rows.push_back(std::move(parent));
            for (std::size_t index = 0; index < input.assignments.size();
                 ++index) {
                if (input.assignments[index].task_uid != 0) {
                    continue;
                }
                UsageGridRow row = assignment_row(
                    input, grid, input.assignments[index],
                    projection.assignments[index], projection.periods);
                row.parent_index = parent_index;
                add_values(grid.rows[parent_index], row);
                grid.rows.push_back(std::move(row));
            }
        }
        for (const TaskRow& task : input.tasks) {
            if (task.uid == 0) {
                continue;  // the synthetic project row was handled above
            }
            const std::size_t parent_index = grid.rows.size();
            UsageGridRow parent = parent_row(UsageGridRowKind::task, task.uid,
                                             task_label_for(input, task.uid),
                                             grid);
            parent.parent_index = parent_index;
            parent.display_id = task.id;
            parent.numbered = true;
            grid.rows.push_back(std::move(parent));
            for (std::size_t index = 0; index < input.assignments.size();
                 ++index) {
                const AssignmentRow& assignment = input.assignments[index];
                if (assignment.task_uid != task.uid) {
                    continue;
                }
                UsageGridRow row = assignment_row(
                    input, grid, assignment, projection.assignments[index],
                    projection.periods);
                row.parent_index = parent_index;
                add_values(grid.rows[parent_index], row);
                grid.rows.push_back(std::move(row));
            }
        }
    }

    // The port's Total row (Project's sheets have none -- usage_grid.h).
    UsageGridRow total = parent_row(UsageGridRowKind::total, 0, "Total", grid);
    for (const UsageGridRow& row : grid.rows) {
        if (row.kind == UsageGridRowKind::assignment) {
            add_values(total, row);
        }
    }
    total.parent_index = grid.rows.size();
    grid.rows.push_back(std::move(total));
    return grid;
}

}  // namespace app
