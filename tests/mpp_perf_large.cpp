/*
 * mpp_perf_large.cpp -- W40 performance harness on a synthetic programme.
 *
 * 4,000 tasks in a 6-level outline, one predecessor chain plus cross links,
 * 200 resources and 200 assignments.  Everything is generated in this process:
 * no corpus, no file on disk.  It times the four layers the brief names:
 *
 *   1. the reader's parse path, on a real (synthesised) CFB container with
 *      4,000 Task FixedData rows;
 *   2. the CPM / recalc path through core::plan::from_mpp + recalculate;
 *   3. the RecordSet build (core/io's reader bridge);
 *   4. the sheet and Gantt models over that RecordSet.
 *
 * The numbers are printed so a regression is visible; the assertions are a
 * deliberately generous ceiling (10 s total) so CI cannot flake on a slow
 * runner.  This is a smoke/scale test, not a benchmark.
 */

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "gantt.h"
#include "mpp_bridge.h"
#include "plan.h"
#include "reader.h"
#include "sheet.h"

#include "mpp_fixture_builder.h"

namespace {

constexpr std::size_t kTaskCount = 4000;
constexpr std::size_t kLevels = 6;
constexpr std::size_t kResourceCount = 200;
constexpr std::size_t kAssignmentCount = 200;
constexpr std::uint32_t kProjectStartDay = 15346; /* 2026-01-05 */
constexpr std::uint32_t kMorning = 0x12C0u;       /* 08:00 in packed-date ticks */
constexpr std::uint32_t kEvening = 0x27D8u;       /* 17:00 */
constexpr double kCeilingMs = 10000.0;

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

std::uint32_t packed(std::uint32_t day, std::uint32_t ticks) {
    return (day << 16) | ticks;
}

/* A 6-level outline over exactly 4,000 nodes.  Level i's parent is the most
 * recent level i-1 node, so every non-leaf node is a summary and the leaves are
 * the CPM graph.  Outline numbers are built from the parent's number. */
std::vector<mpp::Task> make_tasks() {
    std::vector<mpp::Task> tasks;
    tasks.reserve(kTaskCount);
    std::vector<std::size_t> last_at_level(kLevels + 1, 0);
    std::vector<std::uint32_t> child_counter(kLevels + 1, 0);
    std::uint32_t root_counter = 0;
    for (std::size_t index = 0; index < kTaskCount; ++index) {
        const std::size_t level = index % kLevels + 1;
        mpp::Task task;
        task.row = static_cast<std::uint32_t>(index + 1);
        task.row_id = static_cast<std::uint32_t>(index + 1);
        task.uid = static_cast<std::uint32_t>(index + 1);
        task.name = "T" + std::to_string(index + 1);
        task.duration_ticks = 4800;
        task.start.raw =
            packed(kProjectStartDay + static_cast<std::uint32_t>(level - 1), kMorning);
        task.finish.raw =
            packed(kProjectStartDay + static_cast<std::uint32_t>(level), kEvening);
        task.calculated_start = task.start;
        task.calculated_finish = task.finish;
        task.outline_level = static_cast<std::uint32_t>(level);
        if (level == 1) {
            ++root_counter;
            task.outline_number = std::to_string(root_counter);
        } else {
            const mpp::Task& parent = tasks[last_at_level[level - 1]];
            ++child_counter[level];
            task.outline_number =
                parent.outline_number + "." + std::to_string(child_counter[level]);
        }
        last_at_level[level] = index;
        for (std::size_t deeper = level + 1; deeper <= kLevels; ++deeper) {
            child_counter[deeper] = 0;
        }
        if (index > 0 && level > 1) {
            ++tasks[last_at_level[level - 1]].child_count;
        }
        tasks.push_back(std::move(task));
    }
    for (mpp::Task& task : tasks) {
        task.summary = task.child_count > 0;
        if (task.uid == 0) {
            task.milestone = true;
        }
    }
    return tasks;
}

mpp::Document make_document() {
    mpp::Document document;
    document.project.row_id = 0;
    document.project.uid = 0;
    document.project.title = "perf";
    document.project.start.raw = packed(kProjectStartDay, kMorning);
    document.project.finish.raw = packed(kProjectStartDay + kLevels + 1, kEvening);
    document.tasks = make_tasks();

    std::vector<std::uint32_t> leaves;
    leaves.reserve(document.tasks.size());
    for (const mpp::Task& task : document.tasks) {
        if (!task.summary) {
            leaves.push_back(task.uid);
        }
    }

    std::uint32_t link_uid = 1;
    for (std::size_t i = 1; i < leaves.size(); ++i) {
        mpp::Link link;
        link.row = link_uid;
        link.uid = link_uid++;
        link.predecessor_uid = leaves[i - 1];
        link.successor_uid = leaves[i];
        link.type_raw = 1; /* FS */
        link.lag_ticks = 0;
        link.predecessor_known = true;
        link.successor_known = true;
        document.links.push_back(link);
        if (i % 10 == 0 && i + 3 < leaves.size()) {
            mpp::Link cross = link;
            cross.row = link_uid;
            cross.uid = link_uid++;
            cross.predecessor_uid = leaves[i];
            cross.successor_uid = leaves[i + 3];
            document.links.push_back(cross);
        }
    }

    for (std::size_t i = 0; i < kResourceCount; ++i) {
        mpp::Resource resource;
        resource.row = static_cast<std::uint32_t>(i + 1);
        resource.row_id = static_cast<std::uint32_t>(i + 1);
        resource.uid = static_cast<std::uint32_t>(i + 1);
        resource.name = "R" + std::to_string(i + 1);
        resource.initials = "R";
        resource.max_units_x100 = 10000.0;
        document.resources.push_back(resource);
    }

    const std::size_t assignment_count =
        std::min(kAssignmentCount, document.tasks.size());
    for (std::size_t i = 0; i < assignment_count; ++i) {
        mpp::Assignment assignment;
        assignment.row = static_cast<std::uint32_t>(i);
        assignment.uid = static_cast<std::uint32_t>(i + 1);
        assignment.task_uid = document.tasks[i].uid;
        assignment.resource_uid_raw = static_cast<std::uint32_t>(i % kResourceCount + 1);
        assignment.units_x100 = 10000.0;
        assignment.work_x1000 = 2400000.0;
        assignment.start = document.tasks[i].start;
        assignment.finish = document.tasks[i].finish;
        assignment.resume = document.tasks[i].finish;
        document.assignments.push_back(assignment);
    }
    return document;
}

/* A real CFB with 4,000 Task rows.  No VarMeta: every row is live, names are
 * empty.  FixedMeta covers rows + 3 descriptors, so the parse emits only the
 * "no Fixed2Data" outline warning. */
std::vector<std::uint8_t> make_reader_fixture() {
    const std::size_t rows = kTaskCount + 1; /* project row + tasks */
    std::vector<std::uint8_t> fixed = mpp_fixture::task_fixed_data(rows);
    for (std::size_t row = 1; row < rows; ++row) {
        const std::size_t base = mpp_fixture::task_row_offset(row);
        mpp_fixture::put_u32(fixed, base + 0, static_cast<std::uint32_t>(row));
        mpp_fixture::put_u32(fixed, base + 4, static_cast<std::uint32_t>(row));
        mpp_fixture::put_u32(fixed, base + 84, 4800);
        mpp_fixture::put_u32(fixed, base + 104,
                             packed(kProjectStartDay, kMorning));
        mpp_fixture::put_u32(fixed, base + 108,
                             packed(kProjectStartDay + 1, kEvening));
    }
    std::vector<mpp_fixture::Stream> streams;
    streams.push_back({"114/TBkndTask/FixedData", std::move(fixed), 0});
    streams.push_back(
        {"114/TBkndTask/FixedMeta",
         mpp_fixture::task_meta(rows, static_cast<std::uint32_t>(rows + 3)), 0});
    return mpp_fixture::make_cfb(streams);
}

int run() {
    int failures = 0;
    const auto check = [&failures](bool condition, const std::string& what) {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << what << '\n';
        }
    };

    const std::vector<std::uint8_t> fixture = make_reader_fixture();
    check(!fixture.empty(), "the synthetic CFB builds");

    double parse_ms = 0.0;
    mpp::Document parsed;
    try {
        const auto begin = Clock::now();
        parsed = mpp::load_bytes(fixture);
        parse_ms = elapsed_ms(begin);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: reader parse threw: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    check(parsed.tasks.size() == kTaskCount,
          "reader parsed all 4,000 synthetic tasks");

    const mpp::Document document = make_document();

    const auto plan_begin = Clock::now();
    const core::plan::PlanInput input = core::plan::from_mpp(document);
    const core::plan::PlanResult plan = core::plan::recalculate(input);
    const double plan_ms = elapsed_ms(plan_begin);
    check(!plan.tasks.empty(), "the CPM/recalc result is non-empty");
    check(plan.tasks.size() >= document.tasks.size() / 2,
          "recalc returned a result row per schedulable task");

    const auto record_begin = Clock::now();
    core::view::RecordSet records = core::io::to_record_set(document);
    const double record_ms = elapsed_ms(record_begin);
    check(records.row_count() == document.tasks.size(),
          "the RecordSet has a row per task");

    const auto sheet_begin = Clock::now();
    core::view::FieldId name_field = 0;
    (void)core::view::field_id_by_name(core::model::RecordType::Task, "Name",
                                       name_field);
    std::vector<core::sheet::SheetColumn> columns;
    core::sheet::SheetColumn name_column;
    name_column.field = name_field;
    name_column.width = 24;
    columns.push_back(name_column);
    core::sheet::SheetModel sheet(records, std::move(columns));
    const std::vector<std::size_t> visible = sheet.visible_rows();
    const auto layout = sheet.layout_columns(core::sheet::SheetMetrics{}, 80);
    const double sheet_ms = elapsed_ms(sheet_begin);
    check(visible.size() == records.row_count(),
          "the sheet's visible-row walk covers every row");
    check(!layout.empty(), "the sheet lays out at least the selection column");

    const auto gantt_begin = Clock::now();
    core::gantt::GanttSource source;
    source.tasks.reserve(document.tasks.size());
    for (const mpp::Task& task : document.tasks) {
        core::gantt::TaskBarInput bar;
        bar.uid = task.uid;
        bar.label = task.name;
        bar.start = task.start.raw;
        bar.finish = task.finish.raw;
        bar.percent_complete = task.percent_complete;
        bar.critical = task.critical;
        source.tasks.push_back(std::move(bar));
    }
    source.links.reserve(document.links.size());
    for (const mpp::Link& link : document.links) {
        core::gantt::LinkInput input_link;
        input_link.predecessor_uid = link.predecessor_uid;
        input_link.successor_uid = link.successor_uid;
        input_link.type = static_cast<core::gantt::LinkType>(link.type_raw & 0x3u);
        source.links.push_back(input_link);
    }
    const core::gantt::GanttLayout gantt =
        core::gantt::build_gantt(source, core::gantt::GanttStyle{},
                                 core::gantt::Timescale{});
    const double gantt_ms = elapsed_ms(gantt_begin);
    check(gantt.row_count == static_cast<int>(document.tasks.size()),
          "the Gantt layout has a row per task");
    check(!gantt.bars.empty(), "the Gantt layout produced bars");

    const double total_ms = parse_ms + plan_ms + record_ms + sheet_ms + gantt_ms;
    std::cout << "mpp_perf_large: tasks=" << document.tasks.size()
              << " links=" << document.links.size()
              << " resources=" << document.resources.size()
              << " assignments=" << document.assignments.size() << '\n';
    std::cout << "  reader parse      " << parse_ms << " ms\n";
    std::cout << "  CPM/recalc        " << plan_ms << " ms\n";
    std::cout << "  RecordSet build   " << record_ms << " ms\n";
    std::cout << "  sheet build       " << sheet_ms << " ms\n";
    std::cout << "  gantt build       " << gantt_ms << " ms\n";
    std::cout << "  total             " << total_ms << " ms (ceiling "
              << kCeilingMs << " ms)\n";

    check(parse_ms < kCeilingMs, "reader parse is under the 10 s ceiling");
    check(plan_ms < kCeilingMs, "CPM/recalc is under the 10 s ceiling");
    check(record_ms < kCeilingMs, "RecordSet build is under the 10 s ceiling");
    check(sheet_ms < kCeilingMs, "sheet build is under the 10 s ceiling");
    check(gantt_ms < kCeilingMs, "gantt build is under the 10 s ceiling");
    check(total_ms < kCeilingMs, "the whole harness is under the 10 s ceiling");

    std::cout << (failures == 0 ? "PASS" : "FAIL") << ": mpp_perf_large\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace

int main() { return run(); }
