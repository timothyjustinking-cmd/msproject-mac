// app/usage_grid_tests.cpp -- the usage grid's rows, details and per-period
// values, proved headlessly against the committed wave-31 oracle facts.
//
// The fixture tests/data/wave31-usage.mpp is loaded READ-ONLY through the
// reader; the expected numbers are copied from
// tests/data/reference/wave31-usage.measures.json, whose `com.tasks`,
// `com.assignments`, `com.resources` and `com.per_period` blocks are Project
// 2021's own COM TimeScaleData over 5-9 January 2026 (daily buckets, Work and
// Actual Work in minutes, Cost in cents).  This test is the port's side of the
// brief's "work values per period measured against Project's own time-phased
// values".
//
// Its row order is the measured rendered_grid.*.logical_rows: a task (or
// resource) row followed by one assignment row per assignment, the assignment
// labelled with the resource (Task Usage) or the task (Resource Usage).  The
// port's final Total row is an affordance, not a Project row: the same measures
// record `no_total_row_or_column`.
//
// The measures key their series by Project's session Assignment.UniqueIDs
// (2097156..2097161).  The file's own assignment rows carry uids 4..9; the test
// walks the measured structural order (task 1's two assignments, then task 2's,
// then task 3's) and maps it to the file uids, so no session id is guessed.
//
// The last section pins the fallbacks: no stored usage view -> one Work detail
// and day columns; stored day/week/month counts; an unsupported stored unit ->
// days with a warning; a stored empty Details list -> no detail lines.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\w31-fresh-app -G "Visual Studio 17 2022" -A x64
//   cmake --build build\w31-fresh-app --config Release
//   ctest --test-dir build\w31-fresh-app -C Release -R "^app_usage_grid$"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <string>
#include <vector>

#include "cal.h"
#include "date.h"
#include "layout_model.h"
#include "reader_adapter.h"
#include "usage_grid.h"
#include "usage_style.h"
#include "view_model.h"
#include "view_style.h"

#ifndef APP_TEST_DATA_DIR
#error "APP_TEST_DATA_DIR must name the repository's tests/data directory"
#endif

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

void check_text(const std::string& got, const std::string& want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got \"%s\" want \"%s\"\n", what, got.c_str(),
                    want.c_str());
    }
}

void check_i64(std::int64_t got, std::int64_t want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got %lld want %lld\n", what,
                    static_cast<long long>(got), static_cast<long long>(want));
    }
}

void check_values(const std::vector<std::int64_t>& got,
                  std::initializer_list<std::int64_t> want, const char* what) {
    ++g_checks;
    if (got.size() != want.size()) {
        ++g_failures;
        std::printf("FAIL: %s: got %zu column(s), want %zu\n", what, got.size(),
                    want.size());
        return;
    }
    std::size_t index = 0;
    bool ok = true;
    for (std::int64_t value : want) {
        if (got[index] != value) {
            std::printf("FAIL: %s: column %zu got %lld want %lld\n", what, index,
                        static_cast<long long>(got[index]),
                        static_cast<long long>(value));
            ok = false;
        }
        ++index;
    }
    if (!ok) {
        ++g_failures;
    }
}

const app::UsageGridRow* assignment_row(const app::UsageGrid& grid,
                                        std::uint32_t uid) {
    for (const app::UsageGridRow& row : grid.rows) {
        if (row.kind == app::UsageGridRowKind::assignment && row.uid == uid) {
            return &row;
        }
    }
    return nullptr;
}

std::string task_name(const app::PlanInput& plan, std::uint32_t uid) {
    for (const app::TaskRow& task : plan.tasks) {
        if (task.uid == uid) {
            return task.name;
        }
    }
    return {};
}

std::string resource_name(const app::PlanInput& plan, std::uint32_t uid) {
    for (const app::ResourceRow& resource : plan.resources) {
        if (resource.uid == uid) {
            return resource.name;
        }
    }
    return {};
}

bool load_fixture(const std::string& path, app::PlanInput& plan,
                  core::sched::Calendar& calendar) {
    try {
        plan = app::plan_input_from_document(mpp::load_file(path));
        calendar = core::cal::load_project_calendar(path);
        return true;
    } catch (const std::exception& failure) {
        std::printf("LOAD ERROR: %s\n", failure.what());
        return false;
    }
}

app::UsageViewStyle style_with(
    msproject::viewstate::TimescaleUnit unit, int count,
    std::initializer_list<msproject::viewstate::UsageDetailKind> details) {
    app::UsageViewStyle style;
    style.decoded = true;
    for (msproject::viewstate::UsageDetailKind kind : details) {
        msproject::viewstate::UsageDetail detail;
        detail.kind = kind;
        detail.stored_id = static_cast<std::uint8_t>(kind);
        style.stored.details.push_back(detail);
    }
    style.stored.minor.unit = unit;
    style.stored.minor.count = count;
    return style;
}

// One task with one assignment on `[first, last]` (working days only) at
// 50 % complete and a $60/h work resource.
app::PlanInput make_tiny_plan(core::sched::date32 start,
                              core::sched::date32 finish,
                              std::int64_t work_minutes) {
    app::PlanInput plan;
    app::TaskRow task;
    task.uid = 1;
    task.id = 1;
    task.name = "task";
    task.percent_complete = 50;
    plan.tasks.push_back(task);

    app::ResourceRow resource;
    resource.uid = 1;
    resource.id = 1;
    resource.name = "resource";
    resource.type = core::view::ResourceType::work;
    resource.standard_rate_per_hour = 60.0;
    resource.cost_fields_stored = true;
    plan.resources.push_back(resource);

    app::AssignmentRow assignment;
    assignment.uid = 10;
    assignment.task_uid = 1;
    assignment.resource_uid = 1;
    assignment.start = start;
    assignment.finish = finish;
    assignment.work_ticks = work_minutes * core::view::kTicksPerMinute;
    plan.assignments.push_back(assignment);
    return plan;
}

void test_task_usage(const app::PlanInput& plan,
                     const core::sched::Calendar& calendar,
                     const app::UsageViewStyle& style) {
    const app::UsageGrid grid = app::make_usage_grid(plan, style, false, calendar);
    check(grid.stored_style, "task usage: the grid uses the stored style");
    check(grid.warnings.empty(), "task usage: no warnings");
    check(grid.details.size() == 2, "task usage: two detail lines");
    if (grid.details.size() == 2) {
        check(grid.details[0].kind ==
                  msproject::viewstate::UsageDetailKind::work,
              "task usage: detail 0 is Work");
        check(grid.details[1].kind ==
                  msproject::viewstate::UsageDetailKind::actual_work,
              "task usage: detail 1 is Actual Work");
        check_text(grid.details[0].label, "Work", "task usage: Work label");
        check_text(grid.details[1].label, "Actual Work",
                   "task usage: Actual Work label");
        check(!grid.details[0].cost && !grid.details[1].cost,
              "task usage: neither detail is money");
    }

    check(grid.period == core::usage::Period::day,
          "task usage: day columns");
    check_i64(grid.periods_per_column, 1, "task usage: one day per column");
    // W31-4.4: the stored MIDDLE tier is the rendered TOP row and the stored
    // BOTTOM tier the rendered bottom row (core/viewstate/usage_style.h).
    check(grid.major_tier.unit ==
                  msproject::viewstate::TimescaleUnit::weeks &&
              grid.major_tier.count == 1 && grid.major_tier.format == 13 &&
              grid.major_tier.align == 0,
          "task usage: weeks/format 13/left renders as the top tier");
    check(grid.minor_tier.unit ==
                  msproject::viewstate::TimescaleUnit::days &&
              grid.minor_tier.count == 1 && grid.minor_tier.format == 20 &&
              grid.minor_tier.align == 1,
          "task usage: days/format 20/centre renders as the bottom tier");
    check(grid.periods.size() == 5, "task usage: five daily columns");
    if (grid.periods.size() == 5) {
        const core::sched::CivilDate first =
            core::sched::to_civil(grid.periods.front().first_date());
        const core::sched::CivilDate last =
            core::sched::to_civil(grid.periods.back().last_date());
        check(first.year == 2026 && first.month == 1 && first.day == 5,
              "task usage: the first column is 5 January 2026");
        check(last.year == 2026 && last.month == 1 && last.day == 9,
              "task usage: the last column is 9 January 2026");
        check(core::sched::weekday(grid.periods.front().first_date()) == 1,
              "task usage: the first column starts on a Monday");
        for (const core::usage::PeriodSpan& span : grid.periods) {
            check(span.day_count() == 1, "task usage: every column is one day");
        }
    }

    // Measured logical rows 0-8, then the port's Total row.
    check(grid.rows.size() == 10, "task usage: nine logical rows plus Total");
    if (grid.rows.size() != 10) {
        return;
    }
    const std::array<app::UsageGridRowKind, 10> kinds = {
        app::UsageGridRowKind::task,       app::UsageGridRowKind::assignment,
        app::UsageGridRowKind::assignment, app::UsageGridRowKind::task,
        app::UsageGridRowKind::assignment, app::UsageGridRowKind::assignment,
        app::UsageGridRowKind::task,       app::UsageGridRowKind::assignment,
        app::UsageGridRowKind::assignment, app::UsageGridRowKind::total};
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        check(grid.rows[index].kind == kinds[index],
              "task usage: the measured row kind order");
    }
    check_text(grid.rows[0].label, plan.tasks[0].name,
               "task usage: the first parent is the first task");
    check_text(grid.rows[3].label, plan.tasks[1].name,
               "task usage: the second parent is the second task");
    check_text(grid.rows[6].label, plan.tasks[2].name,
               "task usage: the third parent is the third task");
    check_text(grid.rows[9].label, "Total", "task usage: the Total row label");

    // W31-4.4: the frozen gutter numbers the parent rows with their stored ID
    // and leaves every assignment row (and the port's Total row) blank.
    const std::array<std::size_t, 3> parent_indices = {0, 3, 6};
    for (std::size_t index = 0; index < parent_indices.size(); ++index) {
        const app::UsageGridRow& row = grid.rows[parent_indices[index]];
        check(row.numbered && row.display_id != 0,
              "task usage: a parent row is numbered");
        check(row.display_id == plan.tasks[index].id,
              "task usage: the parent's gutter number is its stored task ID");
    }
    check(!grid.rows[9].numbered, "task usage: the Total row leaves the gutter blank");

    // Children hang off their parent and carry the RESOURCE name.  The uids are
    // the file's assignment-row uids (the reader's uid); Project's COM
    // Assignment.UniqueID values in the measures file are session ids, so the
    // expected series are matched by this measured structural order.
    const std::array<std::size_t, 6> parents = {0, 0, 3, 3, 6, 6};
    const std::array<std::size_t, 6> row_indices = {1, 2, 4, 5, 7, 8};
    const std::array<std::uint32_t, 6> assignment_uids = {4, 5, 6, 7, 8, 9};
    for (std::size_t index = 0; index < row_indices.size(); ++index) {
        check(!grid.rows[row_indices[index]].numbered,
              "task usage: an assignment row leaves the gutter blank");
    }
    for (std::size_t index = 0; index < assignment_uids.size(); ++index) {
        const app::UsageGridRow& row = grid.rows[row_indices[index]];
        check(row.child, "task usage: an assignment row is a child row");
        check(row.parent_index == parents[index],
              "task usage: the assignment row's parent index");
        check(row.uid == assignment_uids[index],
              "task usage: the measured assignment uid");
        for (const app::AssignmentRow& assignment : plan.assignments) {
            if (assignment.uid == assignment_uids[index]) {
                check_text(row.label, resource_name(plan, assignment.resource_uid),
                           "task usage: the row is labelled with its resource");
            }
        }
    }

    // Per-period Work and Actual Work, copied from com.per_period.
    const std::vector<std::int64_t> task_work[3] = {
        {960, 960, 960, 960, 960}, {960, 960, 960, 0, 0}, {960, 960, 0, 0, 0}};
    const std::vector<std::int64_t> task_actual[3] = {
        {960, 960, 960, 960, 960}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}};
    const std::int64_t task_work_total[3] = {4800, 2880, 1920};
    const std::int64_t task_actual_total[3] = {4800, 0, 0};
    for (std::size_t index = 0; index < 3; ++index) {
        const app::UsageGridRow& row = grid.rows[index * 3];
        check_values(row.values[0], {task_work[index][0], task_work[index][1],
                                     task_work[index][2], task_work[index][3],
                                     task_work[index][4]},
                     "task usage: the task's Work per period");
        check_values(row.values[1], {task_actual[index][0], task_actual[index][1],
                                     task_actual[index][2], task_actual[index][3],
                                     task_actual[index][4]},
                     "task usage: the task's Actual Work per period");
        check_i64(row.totals[0], task_work_total[index],
                  "task usage: the task's Work total");
        check_i64(row.totals[1], task_actual_total[index],
                  "task usage: the task's Actual Work total");
    }

    const std::vector<std::int64_t> work[6] = {
        {480, 480, 480, 480, 480}, {480, 480, 480, 480, 480},
        {480, 480, 480, 0, 0},     {480, 480, 480, 0, 0},
        {480, 480, 0, 0, 0},       {480, 480, 0, 0, 0}};
    const std::vector<std::int64_t> actual[6] = {
        {480, 480, 480, 480, 480}, {480, 480, 480, 480, 480},
        {0, 0, 0, 0, 0},           {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},           {0, 0, 0, 0, 0}};
    const std::int64_t work_total[6] = {2400, 2400, 1440, 1440, 960, 960};
    const std::int64_t actual_total[6] = {2400, 2400, 0, 0, 0, 0};
    for (std::size_t index = 0; index < 6; ++index) {
        const app::UsageGridRow* row = assignment_row(grid, assignment_uids[index]);
        check(row != nullptr, "task usage: the measured assignment row exists");
        if (row == nullptr) {
            continue;
        }
        check_values(row->values[0],
                     {work[index][0], work[index][1], work[index][2],
                      work[index][3], work[index][4]},
                     "task usage: the assignment's Work per period");
        check_values(row->values[1],
                     {actual[index][0], actual[index][1], actual[index][2],
                      actual[index][3], actual[index][4]},
                     "task usage: the assignment's Actual Work per period");
        check_i64(row->totals[0], work_total[index],
                  "task usage: the assignment's Work total");
        check_i64(row->totals[1], actual_total[index],
                  "task usage: the assignment's Actual Work total");
    }

    const app::UsageGridRow& total = grid.rows[9];
    check_values(total.values[0], {2880, 2880, 1920, 960, 960},
                 "task usage: the Total row's Work per period");
    check_values(total.values[1], {960, 960, 960, 960, 960},
                 "task usage: the Total row's Actual Work per period");
    check_i64(total.totals[0], 9600, "task usage: the Total row's Work total");
    check_i64(total.totals[1], 4800, "task usage: the Total row's Actual Work total");
}

void test_resource_usage(const app::PlanInput& plan,
                         const core::sched::Calendar& calendar,
                         const app::UsageViewStyle& style) {
    const app::UsageGrid grid = app::make_usage_grid(plan, style, true, calendar);
    check(grid.resource, "resource usage: the resource grid flag");
    check(grid.stored_style, "resource usage: the grid uses the stored style");
    check(grid.warnings.empty(), "resource usage: no warnings");
    check(grid.details.size() == 2, "resource usage: two detail lines");
    if (grid.details.size() == 2) {
        check(grid.details[0].kind ==
                  msproject::viewstate::UsageDetailKind::work,
              "resource usage: detail 0 is Work");
        check(grid.details[1].kind ==
                  msproject::viewstate::UsageDetailKind::cost,
              "resource usage: detail 1 is Cost");
        check_text(grid.details[1].label, "Cost", "resource usage: Cost label");
        check(grid.details[1].cost, "resource usage: Cost is a money detail");
    }
    check(grid.major_tier.unit ==
                  msproject::viewstate::TimescaleUnit::weeks &&
              grid.major_tier.format == 13,
          "resource usage: the same stored top tier (weeks, format 13)");
    check(grid.minor_tier.unit ==
                  msproject::viewstate::TimescaleUnit::days &&
              grid.minor_tier.format == 20,
          "resource usage: the same stored bottom tier (days, format 20)");
    check(grid.periods.size() == 5, "resource usage: five daily columns");

    check(grid.rows.size() == 10, "resource usage: nine logical rows plus Total");
    if (grid.rows.size() != 10) {
        return;
    }
    const std::array<app::UsageGridRowKind, 10> kinds = {
        app::UsageGridRowKind::resource,   app::UsageGridRowKind::assignment,
        app::UsageGridRowKind::assignment, app::UsageGridRowKind::assignment,
        app::UsageGridRowKind::resource,   app::UsageGridRowKind::assignment,
        app::UsageGridRowKind::resource,   app::UsageGridRowKind::assignment,
        app::UsageGridRowKind::assignment, app::UsageGridRowKind::total};
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        check(grid.rows[index].kind == kinds[index],
              "resource usage: the measured row kind order");
    }
    check_text(grid.rows[0].label, plan.resources[0].name,
               "resource usage: the first parent is the first resource");
    check_text(grid.rows[4].label, plan.resources[1].name,
               "resource usage: the second parent is the second resource");
    check_text(grid.rows[6].label, plan.resources[2].name,
               "resource usage: the third parent is the third resource");
    const std::array<std::size_t, 3> parent_rows_check = {0, 4, 6};
    for (std::size_t index = 0; index < parent_rows_check.size(); ++index) {
        const app::UsageGridRow& row = grid.rows[parent_rows_check[index]];
        check(row.numbered && row.display_id == plan.resources[index].id,
              "resource usage: the parent's gutter number is its resource ID");
    }

    // Children hang off their resource and carry the TASK name, in the measured
    // row order: Crew A's three assignments, then Crew B's one, then Crew C's
    // two (the measures' logical_rows order, mapped to the file's uids).
    const std::array<std::size_t, 6> parents = {0, 0, 0, 4, 6, 6};
    const std::array<std::size_t, 6> row_indices = {1, 2, 3, 5, 7, 8};
    const std::array<std::uint32_t, 6> assignment_uids = {4, 6, 8, 5, 7, 9};
    for (std::size_t index = 0; index < row_indices.size(); ++index) {
        check(!grid.rows[row_indices[index]].numbered,
              "resource usage: an assignment row leaves the gutter blank");
    }
    for (std::size_t index = 0; index < assignment_uids.size(); ++index) {
        const app::UsageGridRow& row = grid.rows[row_indices[index]];
        check(row.child, "resource usage: an assignment row is a child row");
        check(row.parent_index == parents[index],
              "resource usage: the assignment row's parent index");
        for (const app::AssignmentRow& assignment : plan.assignments) {
            if (assignment.uid == assignment_uids[index]) {
                check_text(row.label, task_name(plan, assignment.task_uid),
                           "resource usage: the row is labelled with its task");
            }
        }
    }

    // Parent Work and Cost per period from com.per_period.resources.
    const std::vector<std::int64_t> work[3] = {
        {1440, 1440, 960, 480, 480}, {480, 480, 480, 480, 480},
        {960, 960, 480, 0, 0}};
    const std::vector<std::int64_t> cost[3] = {
        {120000, 120000, 80000, 40000, 40000},
        {48000, 48000, 48000, 48000, 48000},
        {64000, 64000, 32000, 0, 0}};
    const std::int64_t work_total[3] = {4800, 2400, 2400};
    const std::int64_t cost_total[3] = {400000, 240000, 160000};
    const std::array<std::size_t, 3> parent_rows = {0, 4, 6};
    for (std::size_t index = 0; index < 3; ++index) {
        const app::UsageGridRow& row = grid.rows[parent_rows[index]];
        check_values(row.values[0],
                     {work[index][0], work[index][1], work[index][2],
                      work[index][3], work[index][4]},
                     "resource usage: the resource's Work per period");
        check_values(row.values[1],
                     {cost[index][0], cost[index][1], cost[index][2],
                      cost[index][3], cost[index][4]},
                     "resource usage: the resource's Cost per period");
        check_i64(row.totals[0], work_total[index],
                  "resource usage: the resource's Work total");
        check_i64(row.totals[1], cost_total[index],
                  "resource usage: the resource's Cost total");
    }

    const std::vector<std::int64_t> assignment_cost[6] = {
        {40000, 40000, 40000, 40000, 40000},
        {40000, 40000, 40000, 0, 0},
        {40000, 40000, 0, 0, 0},
        {48000, 48000, 48000, 48000, 48000},
        {32000, 32000, 32000, 0, 0},
        {32000, 32000, 0, 0, 0}};
    const std::int64_t assignment_cost_total[6] = {200000, 120000, 80000,
                                                   240000, 96000,  64000};
    for (std::size_t index = 0; index < assignment_uids.size(); ++index) {
        const app::UsageGridRow* row = assignment_row(grid, assignment_uids[index]);
        check(row != nullptr, "resource usage: the measured assignment row exists");
        if (row == nullptr) {
            continue;
        }
        check_values(row->values[1],
                     {assignment_cost[index][0], assignment_cost[index][1],
                      assignment_cost[index][2], assignment_cost[index][3],
                      assignment_cost[index][4]},
                     "resource usage: the assignment's Cost per period");
        check_i64(row->totals[1], assignment_cost_total[index],
                  "resource usage: the assignment's Cost total");
    }

    const app::UsageGridRow& total = grid.rows[9];
    check_values(total.values[0], {2880, 2880, 1920, 960, 960},
                 "resource usage: the Total row's Work per period");
    check_values(total.values[1],
                 {232000, 232000, 160000, 88000, 88000},
                 "resource usage: the Total row's Cost per period");
    check_i64(total.totals[0], 9600, "resource usage: the Total row's Work total");
    check_i64(total.totals[1], 800000, "resource usage: the Total row's Cost total");
}

void test_fallbacks(const app::PlanInput& fixture_plan,
                    const core::sched::Calendar& fixture_calendar) {
    // No stored usage view: one Work detail, day columns, no warnings.
    const app::UsageViewStyle none;
    const app::UsageGrid plain =
        app::make_usage_grid(fixture_plan, none, false, fixture_calendar);
    check(!plain.stored_style, "default: the style is not from a file");
    check(plain.details.size() == 1 &&
              plain.details[0].kind ==
                  msproject::viewstate::UsageDetailKind::work,
          "default: the one detail is Work");
    check(plain.warnings.empty(), "default: no warnings");
    check(plain.period == core::usage::Period::day && plain.periods.size() == 5,
          "default: the five measured days");
    // W31-4.4's default tiers: weeks over days, the measured pair.
    check(plain.major_tier.unit == msproject::viewstate::TimescaleUnit::weeks &&
              plain.major_tier.format == 13 && plain.major_tier.align == 0 &&
              plain.major_tier.count == 1,
          "default: weeks/format 13/left is the top tier");
    check(plain.minor_tier.unit == msproject::viewstate::TimescaleUnit::days &&
              plain.minor_tier.format == 20 && plain.minor_tier.align == 1 &&
              plain.minor_tier.count == 1,
          "default: days/format 20/centre is the bottom tier");
    // The shell's Day/Week/Month choice applies only when nothing is stored.
    const app::UsageGrid weekly = app::make_usage_grid(
        fixture_plan, none, false, fixture_calendar, core::usage::Period::week);
    check(weekly.period == core::usage::Period::week &&
              weekly.minor_tier.unit ==
                  msproject::viewstate::TimescaleUnit::weeks &&
              weekly.minor_tier.format == 13,
          "default: a Week choice draws weeks");
    check(weekly.major_tier.unit ==
                  msproject::viewstate::TimescaleUnit::months &&
              weekly.major_tier.format == 13,
          "default: weeks draw under a months top tier");
    const app::UsageGrid monthly = app::make_usage_grid(
        fixture_plan, none, false, fixture_calendar, core::usage::Period::month);
    check(monthly.period == core::usage::Period::month &&
              monthly.minor_tier.unit ==
                  msproject::viewstate::TimescaleUnit::months &&
              monthly.major_tier.unit ==
                  msproject::viewstate::TimescaleUnit::none,
          "default: month columns draw no top tier");
    check(plain.rows.size() == 10, "default: the same logical rows");
    if (plain.rows.size() == 10) {
        check_values(plain.rows[0].values[0], {960, 960, 960, 960, 960},
                     "default: the parent's Work per period");
    }

    // A stored empty Details list (DetailStylesRemoveAll) draws no detail line.
    const app::UsageGrid no_details = app::make_usage_grid(
        fixture_plan, style_with(msproject::viewstate::TimescaleUnit::days, 1, {}),
        false, fixture_calendar);
    check(no_details.details.empty(), "remove-all: no detail lines");
    check(no_details.rows.size() == 10 && no_details.rows[0].values.empty(),
          "remove-all: the rows remain, with no detail series");

    // An unsupported bottom unit falls back to days and says so.
    const app::UsageGrid hours = app::make_usage_grid(
        fixture_plan, style_with(msproject::viewstate::TimescaleUnit::hours, 1,
                                 {msproject::viewstate::UsageDetailKind::work}),
        false, fixture_calendar);
    check(hours.period == core::usage::Period::day &&
              hours.periods_per_column == 1,
          "fallback: hours draw day columns");
    check(hours.warnings.size() == 1, "fallback: one warning");
    if (!hours.warnings.empty()) {
        check(hours.warnings[0].find("hours") != std::string::npos,
              "fallback: the warning names the stored unit");
    }

    // Stored day counts: two calendar days per column.
    const core::sched::date32 monday =
        core::sched::from_ymd(2026, 1, 5, 8, 0);
    const core::sched::date32 thursday =
        core::sched::from_ymd(2026, 1, 8, 17, 0);
    const app::PlanInput four_days = make_tiny_plan(monday, thursday, 1920);
    const app::UsageGrid two_days = app::make_usage_grid(
        four_days, style_with(msproject::viewstate::TimescaleUnit::days, 2,
                              {msproject::viewstate::UsageDetailKind::work,
                               msproject::viewstate::UsageDetailKind::actual_work,
                               msproject::viewstate::UsageDetailKind::cost}),
        false, app::standard_calendar());
    check(two_days.periods.size() == 2, "count: two 2-day columns");
    if (two_days.periods.size() == 2) {
        check(two_days.periods[0].day_count() == 2 &&
                  two_days.periods[1].day_count() == 2,
              "count: each column spans two days");
    }
    check(two_days.rows.size() == 3, "count: parent, assignment and Total");
    if (two_days.rows.size() == 3) {
        const app::UsageGridRow& parent = two_days.rows[0];
        check_values(parent.values[0], {960, 960}, "count: Work per column");
        check_values(parent.values[1], {480, 480},
                     "count: 50 % Actual Work per column");
        check_values(parent.values[2], {96000, 96000},
                     "count: $60/h Cost per column");
        check_i64(parent.totals[0], 1920, "count: the Work total");
        check_i64(parent.totals[1], 960, "count: the Actual Work total");
        check_i64(parent.totals[2], 192000, "count: the Cost total");
    }

    // Stored weeks: Monday boundaries, and a two-week count joins two weeks.
    const core::sched::date32 friday_two_weeks =
        core::sched::from_ymd(2026, 1, 16, 17, 0);
    const app::PlanInput two_weeks = make_tiny_plan(monday, friday_two_weeks, 4800);
    const app::UsageGrid weeks = app::make_usage_grid(
        two_weeks, style_with(msproject::viewstate::TimescaleUnit::weeks, 1,
                              {msproject::viewstate::UsageDetailKind::work}),
        false, app::standard_calendar());
    check(weeks.period == core::usage::Period::week, "weeks: week columns");
    check(weeks.periods.size() == 2, "weeks: two Monday-based columns");
    if (weeks.periods.size() == 2) {
        check(core::sched::weekday(weeks.periods[0].first_date()) == 1 &&
                  core::sched::weekday(weeks.periods[1].first_date()) == 1,
              "weeks: both columns start on Mondays");
        check(weeks.periods[0].day_count() == 7 && weeks.periods[1].day_count() == 5,
              "weeks: 7 + 5 days, clipped at the finish");
        check_values(weeks.rows[0].values[0], {2400, 2400},
                     "weeks: Work per week");
    }
    const app::UsageGrid fortnights = app::make_usage_grid(
        two_weeks, style_with(msproject::viewstate::TimescaleUnit::weeks, 2,
                              {msproject::viewstate::UsageDetailKind::work}),
        false, app::standard_calendar());
    check(fortnights.periods.size() == 1, "weeks: a two-week count is one column");
    if (fortnights.periods.size() == 1) {
        check_values(fortnights.rows[0].values[0], {4800},
                     "weeks: both weeks in one column");
    }

    // Stored months: civil month boundaries.
    const core::sched::date32 february_ten =
        core::sched::from_ymd(2026, 2, 10, 17, 0);
    const app::PlanInput two_months = make_tiny_plan(monday, february_ten, 12960);
    const app::UsageGrid months = app::make_usage_grid(
        two_months, style_with(msproject::viewstate::TimescaleUnit::months, 1,
                               {msproject::viewstate::UsageDetailKind::work}),
        false, app::standard_calendar());
    check(months.period == core::usage::Period::month, "months: month columns");
    check(months.periods.size() == 2, "months: January then February");
    if (months.periods.size() == 2) {
        const core::sched::CivilDate first =
            core::sched::to_civil(months.periods[0].first_date());
        const core::sched::CivilDate second =
            core::sched::to_civil(months.periods[1].first_date());
        check(first.month == 1 && second.month == 2 && second.day == 1,
              "months: the boundary is 1 February");
        check_values(months.rows[0].values[0], {9600, 3360},
                     "months: January's 20 working days then February's 7");
    }

    // A shown project-summary row is the first parent row.
    app::PlanInput summary;
    summary.show_project_summary = true;
    const app::UsageGrid summary_grid = app::make_usage_grid(
        summary, none, false, app::standard_calendar());
    check(summary_grid.rows.size() == 2 &&
              summary_grid.rows[0].kind == app::UsageGridRowKind::task &&
              summary_grid.rows[0].uid == 0 &&
              summary_grid.rows[1].kind == app::UsageGridRowKind::total,
          "summary: the project row precedes the Total row");
}

}  // namespace

int main() {
    const std::string data = APP_TEST_DATA_DIR;
    const std::string fixture = data + "/wave31-usage.mpp";

    app::PlanInput plan;
    core::sched::Calendar calendar;
    const bool loaded = load_fixture(fixture, plan, calendar);
    check(loaded, "the wave-31 usage fixture loads through the reader");
    if (loaded) {
        app::UsageViewStyle task_style;
        app::UsageViewStyle resource_style;
        std::string reason;
        check(app::read_usage_view_style(fixture, "Task Usage", task_style, &reason),
              "the fixture's Task Usage style decodes");
        check(app::read_usage_view_style(fixture, "Resource Usage", resource_style,
                                         &reason),
              "the fixture's Resource Usage style decodes");
        test_task_usage(plan, calendar, task_style);
        test_resource_usage(plan, calendar, resource_style);
        test_fallbacks(plan, calendar);
    }

    std::printf("usage grid tests: %d checks, %d failure(s)\n", g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
