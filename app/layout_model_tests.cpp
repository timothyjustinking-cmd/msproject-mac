// app/layout_model_tests.cpp -- the headless test CI can run.
//
// Covers the portable half of the shell: the date32 conversions, the
// reader-row -> RecordSet mapping, the row strings the sheet presents, and the
// GanttSource/GanttLayout the window draws (bars, kinds, links, shading, status
// marker, row alignment).  Synthetic rows always run; two corpus probes run
// only when corpus/probe/ is on disk (CI has no corpus, so they print SKIP).
//
// It prints counts and SUIDs only -- never a task name from the corpus, per the
// client-data rule in docs/coordination.md.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B app\build\vs -G "Visual Studio 17 2022" -A x64
//   cmake --build app\build\vs --config Release
//   .\app\build\vs\Release\layout_model_tests.exe

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "date.h"
#include "gantt.h"
#include "layout_model.h"
#include "page_layout.h"
#include "reader_adapter.h"
#include "sheet.h"

namespace {

using app::date32;
using app::kDateNa;
using core::sched::from_ymd;
using core::sched::to_civil;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

void check_size(std::size_t got, std::size_t want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got %llu want %llu\n", what,
                    static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
    }
}

void check_int(std::int64_t got, std::int64_t want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got %lld want %lld\n", what, static_cast<long long>(got),
                    static_cast<long long>(want));
    }
}

void check_near(double got, double want, double tolerance, const char* what) {
    ++g_checks;
    if (!std::isfinite(got) || std::abs(got - want) > tolerance) {
        ++g_failures;
        std::printf("FAIL: %s: got %.6f want %.6f\n", what, got, want);
    }
}

void check_text(const std::string& got, const std::string& want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got \"%s\" want \"%s\"\n", what, got.c_str(), want.c_str());
    }
}

void check_date(date32 got, date32 want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got %s want %s\n", what, core::sched::to_string(got).c_str(),
                    core::sched::to_string(want).c_str());
    }
}

bool date_in_range(date32 day, std::int32_t first_year, std::int32_t last_year) {
    if (day == kDateNa || core::sched::is_overflow(day)) {
        return false;
    }
    const core::sched::CivilDate civil = to_civil(day);
    return civil.year >= first_year && civil.year <= last_year;
}

app::TaskRow make_task(std::uint32_t uid, std::uint32_t id, const char* outline, const char* name,
                       std::int64_t ticks, date32 start, date32 finish, std::uint16_t percent,
                       int level, bool summary, bool milestone, bool critical) {
    app::TaskRow row;
    row.uid = uid;
    row.id = id;
    row.outline_number = outline;
    row.name = name;
    row.duration_ticks = ticks;
    row.percent_complete = percent;
    row.start = start;
    row.finish = finish;
    row.outline_level = level;
    row.summary = summary;
    row.milestone = milestone;
    row.critical = critical;
    return row;
}

app::LinkRow make_link(std::uint32_t predecessor, std::uint32_t successor,
                       core::gantt::LinkType type, bool critical) {
    app::LinkRow row;
    row.predecessor_uid = predecessor;
    row.successor_uid = successor;
    row.type = type;
    row.critical = critical;
    return row;
}

// One summary (20-23 Oct), two children (19-21 Oct, 22-23 Oct), one milestone
// (26 Oct).  Starts/finishes are the reader's wall-clock instants (08:00/17:00).
constexpr std::uint32_t kSummaryUid = 1;
constexpr std::uint32_t kFirstChildUid = 2;
constexpr std::uint32_t kSecondChildUid = 3;
constexpr std::uint32_t kMilestoneUid = 4;

app::PlanInput synthetic_plan() {
    app::PlanInput input;
    input.title = "Synthetic plan";
    input.source_name = "synthetic";
    input.project_start = from_ymd(2026, 10, 19, 8, 0);
    input.project_finish = from_ymd(2026, 10, 26, 17, 0);
    input.tasks.push_back(make_task(kSummaryUid, 1, "1", "Summary", 5 * 4800,
                                    from_ymd(2026, 10, 19, 8, 0),
                                    from_ymd(2026, 10, 23, 17, 0), 0, 0, true, false, false));
    input.tasks.push_back(make_task(kFirstChildUid, 2, "1.1", "First child", 3 * 4800,
                                    from_ymd(2026, 10, 19, 8, 0),
                                    from_ymd(2026, 10, 21, 17, 0), 50, 1, false, false, true));
    input.tasks.push_back(make_task(kSecondChildUid, 3, "1.2", "Second child", 2 * 4800,
                                    from_ymd(2026, 10, 22, 8, 0),
                                    from_ymd(2026, 10, 23, 17, 0), 100, 1, false, false, false));
    input.tasks.push_back(make_task(kMilestoneUid, 4, "1.3", "Milestone", 0,
                                    from_ymd(2026, 10, 26, 8, 0),
                                    from_ymd(2026, 10, 26, 17, 0), 0, 1, false, true, false));
    input.links.push_back(make_link(kFirstChildUid, kSecondChildUid,
                                    core::gantt::LinkType::finish_to_start, true));
    input.links.push_back(make_link(kFirstChildUid, kMilestoneUid,
                                    core::gantt::LinkType::start_to_start, false));
    input.links.push_back(make_link(kSecondChildUid, kMilestoneUid,
                                    core::gantt::LinkType::finish_to_finish, false));
    input.links.push_back(make_link(kMilestoneUid, kFirstChildUid,
                                    core::gantt::LinkType::start_to_finish, false));
    // An endpoint with no task is kept in the model and skipped by the Gantt.
    input.links.push_back(make_link(kFirstChildUid, 99, core::gantt::LinkType::finish_to_start,
                                    false));
    return input;
}

constexpr std::size_t kUidColumn = 0;
constexpr std::size_t kIdColumn = 1;
constexpr std::size_t kOutlineColumn = 2;
constexpr std::size_t kNameColumn = 3;
constexpr std::size_t kStartColumn = 4;
constexpr std::size_t kFinishColumn = 5;
constexpr std::size_t kDurationColumn = 6;

// ---------------------------------------------------------------------------

void test_date_conversion() {
    // The file's day 0 is 1983-12-31 and date32's day 1 is 1984-01-01, so both
    // count from 1983-12-31: file day D IS date32 day D (identity).  A packed
    // 08:00 is tick 4800 (8 h * 3600 s / 6 s), and one tick is one 1/10 minute.
    //
    // W38: these expectations used to add one day (day 0 -> 1984-01-01), which
    // made every date in the shell one day late.  spec/mpp-records.md §3 pins
    // 15346 = 2026-01-05 and 15636 = 2026-10-22; both hold under the identity and
    // not under the shift.
    check_date(app::date32_from_packed(0u), kDateNa, "packed 0 is NA");
    check_date(app::date32_from_packed(0xFFFFFFFFu), kDateNa, "packed 0xffffffff is NA");
    check_date(app::date32_from_packed(4800u), from_ymd(1983, 12, 31, 8, 0),
               "packed day 0 08:00 -> 1983-12-31 08:00");
    check_date(app::date32_from_packed(0x00010000u), from_ymd(1984, 1, 1),
               "packed day 1 00:00 -> 1984-01-01");
    check_date(app::date32_from_packed(0x000113ECu), from_ymd(1984, 1, 1, 8, 30),
               "packed day 1 08:30 -> 1984-01-01 08:30");
    // The two values spec/mpp-records.md §3 pins, in both directions.
    check_date(app::date32_from_packed(15346u << 16), from_ymd(2026, 1, 5),
               "packed 15346 is 2026-01-05");
    check_date(app::date32_from_packed((15636u << 16) | 4800u), from_ymd(2026, 10, 22, 8, 0),
               "packed 15636 08:00 is 2026-10-22 08:00");
    check_int(static_cast<std::int64_t>(app::packed_from_date32(from_ymd(2026, 10, 22, 8, 0)) >> 16),
              15636, "2026-10-22 packs to day 15636");

    // Round trip, including the time of day.  The mapping is the identity, so the
    // packed day equals the engine day.
    const date32 instant = from_ymd(2026, 10, 19, 8, 30);
    const std::uint32_t packed = app::packed_from_date32(instant);
    check_int(static_cast<std::int64_t>(packed >> 16),
              static_cast<std::int64_t>(core::sched::day_of(instant)),
              "packed round trip day is engine day");
    check_int(static_cast<std::int64_t>(packed & 0xFFFFu), 8 * 600 + 30 * 10,
              "packed round trip tick == tenths of minute");
    check_date(app::date32_from_packed(packed), instant, "packed round trip is exact");
    // PROJECT_A's stored start is a Monday (2026-10-19).  Under the old +1 shift this
    // read as Tuesday, which is the bug this pins.
    check_int(core::sched::weekday(app::date32_from_packed(15633u << 16)), 1,
              "2026-10-19 file day 15633 reads back as a Monday");
    check_int(static_cast<std::int64_t>(app::packed_from_date32(kDateNa)), 0xFFFFFFFFll,
              "NA packs back to 0xffffffff");

    // DateValue <-> date32.
    core::view::DateValue value;
    value.year = 2026;
    value.month = 10;
    value.day = 19;
    value.hour = 8;
    value.minute = 30;
    value.second = 0;
    const date32 from_value = app::date32_from_date_value(value);
    check_date(from_value, instant, "DateValue -> date32");
    const core::view::DateValue back = app::date_value_from_date32(from_value);
    check_int(back.year, 2026, "DateValue round trip year");
    check_int(back.month, 10, "DateValue round trip month");
    check_int(back.day, 19, "DateValue round trip day");
    check_int(back.hour, 8, "DateValue round trip hour");
    check_int(back.minute, 30, "DateValue round trip minute");
    check_int(back.second, 0, "DateValue round trip second");
    check_date(app::date32_from_date_value(core::view::DateValue{}), kDateNa,
               "invalid DateValue -> NA");
    check(!core::view::DateValue{}.valid(), "default DateValue is invalid");

    // The status-line default: a real civil date.
    const date32 today = app::today_date32();
    const core::sched::CivilDate civil = to_civil(today);
    check(today != kDateNa, "today is a real date");
    check(civil.year >= 2020 && civil.year <= 2100, "today is in a sane year range");
}

void test_record_set() {
    const app::PlanInput input = synthetic_plan();
    core::view::RecordSet records = app::make_record_set(input);

    check_size(records.row_count(), 4, "record set row count");
    check(records.record_type() == core::model::RecordType::Task, "record set is a task set");
    check(records.uid(0) == kSummaryUid, "row 0 uid");
    check(records.uid(3) == kMilestoneUid, "row 3 uid");
    check(app::name_field_id() != 0, "Task.Name resolved");
    check(records.has_field(app::kUidField), "UID carrier field present");
    check(records.has_field(app::name_field_id()), "Name field present");

    const core::view::FieldId level_field = app::task_field_id("OutlineLevel");
    check(level_field != 0, "Task.OutlineLevel resolved");
    const core::view::Value* level = records.get(1, level_field);
    check(level != nullptr && level->kind == core::view::ValueKind::Integer && level->integer == 1,
          "row 1 stores outline level 1 (0-based)");

    const core::view::FieldId duration_field = app::task_field_id("Duration");
    const core::view::Value* duration = records.get(0, duration_field);
    check(duration != nullptr && duration->kind == core::view::ValueKind::Duration &&
              duration->integer == 5 * 4800,
          "row 0 stores 5 days of ticks");

    const core::view::FieldId summary_field = app::task_field_id("Summary");
    const core::view::Value* summary = records.get(0, summary_field);
    check(summary != nullptr && summary->kind == core::view::ValueKind::Bool && summary->boolean,
          "row 0 stores Summary = true");
    const core::view::Value* milestone = records.get(3, app::task_field_id("Milestone"));
    check(milestone != nullptr && milestone->kind == core::view::ValueKind::Bool &&
              milestone->boolean,
          "row 3 stores Milestone = true");

    // 0-based outline level for the sheet: the reader's 1-based level minus one.
    const core::view::Value* name = records.get(1, app::name_field_id());
    check(name != nullptr && name->kind == core::view::ValueKind::Text &&
              name->text == "First child",
          "row 1 name");

    // Dates round trip through the RecordSet's civil DateValue.
    const core::view::Value* start = records.get(1, app::task_field_id("Start"));
    check(start != nullptr && start->kind == core::view::ValueKind::Date, "row 1 start is a date");
    if (start != nullptr && start->kind == core::view::ValueKind::Date) {
        check_date(app::date32_from_date_value(start->date), from_ymd(2026, 10, 19, 8, 0),
                   "row 1 start round trips");
    }

    const app::SheetRowText text = app::sheet_row_text(input.tasks[1], core::sheet::SheetFormat{});
    check_text(text.uid, "2", "row text uid");
    check_text(text.id, "2", "row text id");
    check_text(text.outline, "1.1", "row text outline number");
    check_text(text.name, "First child", "row text name");
}

void test_sheet() {
    const app::PlanInput input = synthetic_plan();
    core::view::RecordSet records = app::make_record_set(input);
    core::sheet::SheetModel sheet(records, app::default_sheet_columns());
    const core::sheet::SheetMetrics metrics = app::default_sheet_metrics();

    check_size(sheet.row_count(), 4, "sheet row count");
    check_size(sheet.visible_row_count(), 4, "sheet visible row count");
    check_size(sheet.column_count(), 7, "sheet column count");
    check_text(sheet.column_title(kUidColumn), "UID", "UID header");
    check_text(sheet.column_title(kNameColumn), "Name", "Name header");
    check(sheet.column(kNameColumn).field == app::name_field_id(), "name column field");

    // Outline facts come from OutlineLevel/Summary in the RecordSet (0-based).
    check_int(sheet.outline_level(0), 0, "summary row is level 0");
    check_int(sheet.outline_level(1), 1, "first child is level 1");
    check(sheet.is_summary(0), "row 0 is a summary");
    check(!sheet.is_summary(1), "row 1 is not a summary");
    check(sheet.has_children(0), "row 0 has children");
    check(sheet.is_critical(1), "row 1 is critical");
    check(!sheet.is_critical(2), "row 2 is not critical");

    // The model's own formatting agrees with layout_model's row strings.
    for (std::size_t row = 0; row < input.tasks.size(); ++row) {
        const app::SheetRowText expected = app::sheet_row_text(input.tasks[row], sheet.format());
        check_text(sheet.cell_text(row, kUidColumn), expected.uid, "sheet UID cell");
        check_text(sheet.cell_text(row, kIdColumn), expected.id, "sheet ID cell");
        check_text(sheet.cell_text(row, kOutlineColumn), expected.outline, "sheet Outline cell");
        check_text(sheet.cell_text(row, kNameColumn), expected.name, "sheet Name cell");
        check_text(sheet.cell_text(row, kStartColumn), expected.start, "sheet Start cell");
        check_text(sheet.cell_text(row, kFinishColumn), expected.finish, "sheet Finish cell");
        check_text(sheet.cell_text(row, kDurationColumn), expected.duration,
                   "sheet Duration cell");
    }
    check_text(sheet.cell_text(1, kDurationColumn), "3 days", "3-day duration text");
    check_text(sheet.cell_text(3, kDurationColumn), "0 days", "milestone duration text");

    // Style data the shell maps to attributes.
    const core::sheet::CellStyle summary_style = sheet.cell_style(0, kNameColumn);
    check(summary_style.bold, "summary cell is bold");
    const core::sheet::CellStyle critical_style = sheet.cell_style(1, kNameColumn);
    check(critical_style.red, "critical cell is red");
    const core::sheet::CellStyle uid_style = sheet.cell_style(1, kUidColumn);
    check(uid_style.align == core::sheet::CellAlign::Right, "UID cell is right aligned");

    // Geometry: seven columns, the name column indented one step under a summary.
    const std::vector<core::sheet::ColumnSegment> columns = sheet.layout_columns(metrics, 0);
    check_size(columns.size(), 7, "laid-out columns");
    check(columns.back().width > 0, "last column has a width");
    check_int(sheet.sheet_width_points(metrics, 0),
              static_cast<std::int64_t>(columns.back().x + columns.back().width),
              "sheet width is the sum of columns");
    check_int(sheet.indent_points(1, metrics), metrics.indent_per_level,
              "child indent is one level");
    check_int(sheet.indent_points(0, metrics), 0, "top-level indent is zero");
    check_int(sheet.row_top_points(0, metrics), metrics.header_height,
              "first row starts below the header");
    check_int(sheet.row_top_points(1, metrics), metrics.header_height + metrics.row_height,
              "uniform row height");
}

void test_gantt() {
    const app::PlanInput input = synthetic_plan();
    const std::vector<date32> holidays{from_ymd(2026, 10, 28)};  // a Wednesday
    const date32 status = from_ymd(2026, 10, 22);
    const core::gantt::GanttSource source = app::make_gantt_source(input, holidays, status);

    check_size(source.tasks.size(), 4, "gantt source rows");
    check_size(source.links.size(), 5, "gantt source links");
    check(source.tasks[0].kind == core::gantt::BarKind::summary, "row 0 is a summary bar");
    check(source.tasks[3].kind == core::gantt::BarKind::milestone, "row 3 is a milestone");
    check_near(source.tasks[1].percent_complete, 0.5, 1e-9, "percent is 0..1");
    check(source.tasks[1].critical, "critical flag carried");
    check(!source.working_week.is_working(0), "Sunday is non-working");
    check(source.working_week.is_working(1), "Monday is working");
    check(!source.working_week.is_working(6), "Saturday is non-working");
    check_size(source.nonworking_days.size(), 1, "holiday list carried");
    check_date(source.status_date, status, "status date carried");

    const core::gantt::Timescale timescale = app::timescale_for_plan(input, 12.0);
    check_near(timescale.pixels_per_unit, 12.0, 1e-9, "pixels per day");
    check(timescale.unit == core::gantt::TimescaleUnit::day, "day unit");
    check_date(timescale.origin, core::sched::date_only(from_ymd(2026, 10, 19)),
               "timescale origin seeded from the project start");

    const core::gantt::GanttLayout layout =
        core::gantt::build_gantt(source, app::default_gantt_style(), timescale);
    const core::sheet::SheetMetrics metrics = app::default_sheet_metrics();

    check_int(layout.row_count, 4, "gantt row count");
    check_size(layout.bars.size(), 4, "one bar per dated task");
    check_near(layout.rows.row_height, static_cast<double>(metrics.row_height), 1e-9,
               "gantt and sheet row heights match");
    check_near(layout.style.row_height, static_cast<double>(metrics.row_height), 1e-9,
               "gantt style row height matches the sheet");
    check(layout.content_width > 0.0, "content width is positive");
    check_date(layout.timescale.origin,
               core::sched::date_only(from_ymd(2026, 10, 19)), "origin snapped to the first day");

    for (std::size_t index = 0; index < layout.bars.size(); ++index) {
        check_int(layout.bars[index].row, static_cast<std::int64_t>(index), "bar row == row index");
        check(layout.bars[index].task_uid == input.tasks[index].uid, "bar uid matches the task");
        check(layout.bars[index].rect.w >= 0.0, "bar width is not negative");
    }
    const core::gantt::Bar* milestone = layout.bar(kMilestoneUid);
    check(milestone != nullptr, "milestone bar exists");
    if (milestone != nullptr) {
        check(milestone->kind == core::gantt::BarKind::milestone, "milestone kind");
        check_near(milestone->rect.w, layout.style.milestone_size, 1e-9, "milestone width");
        check_near(milestone->rect.h, layout.style.milestone_size, 1e-9, "milestone height");
    }
    const core::gantt::Bar* summary = layout.bar(kSummaryUid);
    check(summary != nullptr, "summary bar exists");
    if (summary != nullptr) {
        check(summary->kind == core::gantt::BarKind::summary, "summary kind");
        check_near(summary->rect.h, layout.style.summary_height, 1e-9, "summary height");
    }
    // A 3-working-day task across a weekend spans more day columns than work days.
    const core::gantt::Bar* child = layout.bar(kFirstChildUid);
    check(child != nullptr, "first child bar exists");
    if (child != nullptr) {
        check_near(child->rect.w, 3.0 * timescale.pixels_per_unit, 1e-6,
                   "whole-day bar spans its calendar days");
    }

    // Links: one per routable link; the one with a missing endpoint is skipped.
    check_size(layout.links.size(), 4, "routed links");
    for (const core::gantt::LinkRoute& route : layout.links) {
        check(route.points.size() >= 2, "link has at least one segment");
        const core::gantt::Bar* successor = layout.bar(route.successor_uid);
        check(successor != nullptr, "link successor has a bar");
        if (successor == nullptr) {
            continue;
        }
        const bool attach_right = route.type == core::gantt::LinkType::finish_to_finish ||
                                  route.type == core::gantt::LinkType::start_to_finish;
        const double edge = attach_right ? successor->rect.right() : successor->rect.x;
        check_near(route.arrow.tip.x, edge, 1e-6, "arrow tip is on the successor edge");
        check(std::abs(route.arrow.direction.y) < 1e-9, "arrow direction is horizontal");
        check(route.bounds.w >= 0.0 && route.bounds.h >= 0.0, "link bounds are sane");
    }

    // Non-working shading: weekends plus the supplied holiday, never bar geometry.
    // The timescale is unbounded, so ask past the last bar to see the holiday.
    const double shade_width = layout.content_width + 40.0 * timescale.pixels_per_unit;
    const std::vector<core::gantt::NonworkingSpan> spans =
        layout.nonworking_spans(0.0, shade_width);
    check(!spans.empty(), "weekend shading is non-empty");
    bool holiday_seen = false;
    for (const core::gantt::NonworkingSpan& span : spans) {
        const std::int32_t weekday = core::sched::weekday(span.date);
        const bool weekend = weekday == 0 || weekday == 6;
        const bool holiday = span.date == core::sched::date_only(from_ymd(2026, 10, 28));
        check(weekend || holiday, "a shaded day is a weekend or the supplied holiday");
        check(span.width > 0.0, "shaded span has width");
        if (holiday) {
            holiday_seen = true;
        }
    }
    check(holiday_seen, "the supplied holiday is shaded");

    // Header cells: two tiers in the same x space, day numbers in the minor tier.
    const std::vector<core::gantt::HeaderCell> major =
        layout.header(core::gantt::HeaderTier::major, 0.0, layout.content_width);
    const std::vector<core::gantt::HeaderCell> minor =
        layout.header(core::gantt::HeaderTier::minor, 0.0, layout.content_width);
    check(!major.empty(), "major header cells");
    check(!minor.empty(), "minor header cells");
    check(major.front().tier == core::gantt::HeaderTier::major, "major tier tag");
    check(minor.front().tier == core::gantt::HeaderTier::minor, "minor tier tag");
    check(minor.front().width > 0.0, "header cell width");
    check_int(minor.front().weekday >= 0 ? 1 : 0, 1, "day header carries a weekday");

    // Status marker.
    check(layout.has_status_line(), "status line present");
    check(layout.status_x > 0.0, "status x is positive");

    // Summary counts.
    const app::PlanSummary plan_summary = app::summarize(input);
    check_size(plan_summary.tasks, 4, "summary tasks");
    check_size(plan_summary.links, 5, "summary links");
    check_size(plan_summary.summaries, 1, "summary summaries");
    check_size(plan_summary.milestones, 1, "summary milestones");
    check_size(plan_summary.critical, 1, "summary critical");
    check_date(plan_summary.first_start, from_ymd(2026, 10, 19, 8, 0), "summary first start");
    check_date(plan_summary.last_finish, from_ymd(2026, 10, 26, 17, 0), "summary last finish");
}

void test_stored_timescale() {
    // Wave 29 (W29-1.4).  The reference numbers are in
    // tests/data/reference/w29-projecta-timescale.measures.json: PROJECT_A's
    // applied view stores origin 220478400 and size bytes 06 01 (262), with a
    // months bottom tier; Project 2021's clean capture renders that view at
    // 3.287 px/day on a calendar-day axis (its month rules are proportional to
    // the 30/31 days, not equal-width columns).
    check_date(app::stored_timescale_origin(220478400u), from_ymd(2025, 12, 1),
               "the PROJECT_A stored origin decodes to 2025-12-01");
    check_date(app::stored_timescale_origin(14400u), from_ymd(1984, 1, 1),
               "stored 14400 is 1984-01-01");
    check_date(app::stored_timescale_origin(0u), kDateNa,
               "stored 0 means no saved timescale, not 1983-12-31");

    msproject::viewstate::GanttStyle stored;
    stored.decoded = true;
    stored.timescale_start = 220478400u;
    stored.timescale_size = 262;
    stored.bottom.unit = msproject::viewstate::TimescaleUnit::months;
    stored.bottom.count = 1;
    core::gantt::Timescale scale = app::default_timescale(20.0);
    scale.origin = from_ymd(2020, 1, 28);
    const app::StoredTimescale applied = app::apply_stored_timescale(stored, scale);
    check(applied.origin && applied.zoom, "the PROJECT_A view supplies both halves");
    check_date(scale.origin, from_ymd(2025, 12, 1),
               "the applied axis starts on the stored scroll origin");
    check(scale.pin_origin, "the stored origin is pinned against the task snap");
    check(scale.unit == core::gantt::TimescaleUnit::day,
          "the stored view draws a calendar-day axis, so months stay proportional");
    check_near(scale.pixels_per_unit, 3.287, 0.02,
               "262% with a months tier renders at the capture's 3.287 px/day");

    // A task before the stored origin must not pull the axis back.
    core::gantt::GanttSource source;
    core::gantt::TaskBarInput earlier;
    earlier.uid = 1;
    earlier.start = from_ymd(2025, 11, 1, 8, 0);
    earlier.finish = from_ymd(2025, 11, 5, 17, 0);
    source.tasks.push_back(earlier);
    const core::gantt::GanttLayout layout =
        core::gantt::build_gantt(source, app::default_gantt_style(), scale);
    check_date(layout.timescale.origin, from_ymd(2025, 12, 1),
               "build_gantt keeps the pinned stored origin");
    check(!layout.bars.empty() && layout.bars[0].rect.x < 0.0,
          "a task before the stored origin is drawn left of x = 0 (clipped)");

    // The stock fixture: a days bottom tier at 100% is Project's default record
    // (start 1984-01-01, the stock 100 % day tier).  Its width is exactly what
    // the shell's week zoom already draws (168 px / 7 = 24 px/day), so the
    // record must change nothing: re-basing the axis to days would move the
    // measured left edge from the week start to the project-start day (W29-C6).
    msproject::viewstate::GanttStyle stock;
    stock.decoded = true;
    stock.timescale_start = 14400u;
    stock.timescale_size = 100;
    stock.bottom.unit = msproject::viewstate::TimescaleUnit::days;
    stock.bottom.count = 1;
    core::gantt::Timescale stock_scale = app::default_timescale(20.0);
    const app::StoredTimescale stock_applied =
        app::apply_stored_timescale(stock, stock_scale);
    check(!stock_applied.origin, "the stock 1984-01-01 default does not pin the axis");
    check(!stock_applied.zoom,
          "the stock 100 % day record adds no zoom to the caller's view (W29-C6)");
    check_near(stock_scale.pixels_per_unit, 20.0, 1e-9,
               "the stock default leaves the caller's width alone");

    // The same stock start with a saved non-default size still applies: a zoom
    // can be stored without a saved scroll origin.
    msproject::viewstate::GanttStyle stock_wide = stock;
    stock_wide.timescale_size = 200;
    core::gantt::Timescale wide_scale = app::default_timescale(20.0);
    const app::StoredTimescale wide_applied =
        app::apply_stored_timescale(stock_wide, wide_scale);
    check(!wide_applied.origin && wide_applied.zoom,
          "a stored 200 % day tier applies even on the stock start");
    check(wide_scale.unit == core::gantt::TimescaleUnit::day && wide_scale.pixels_per_unit == 48.0,
          "stored size 200 is 48 px/day");

    // An unmeasured bottom unit keeps the shell zoom but still takes the origin.
    msproject::viewstate::GanttStyle weeks = stored;
    weeks.bottom.unit = msproject::viewstate::TimescaleUnit::weeks;
    core::gantt::Timescale week_scale = app::default_timescale(20.0);
    week_scale.pixels_per_unit = 168.0;
    const app::StoredTimescale week_applied =
        app::apply_stored_timescale(weeks, week_scale);
    check(week_applied.origin && !week_applied.zoom,
          "a weeks bottom tier applies the origin but not the unmeasured zoom");
    check_near(week_scale.pixels_per_unit, 168.0, 1e-9,
               "the unmeasured zoom leaves the shell's width alone");

    // A view with no stored state changes nothing.
    msproject::viewstate::GanttStyle empty;
    empty.decoded = true;
    core::gantt::Timescale untouched = app::default_timescale(20.0);
    untouched.origin = from_ymd(2020, 1, 28);
    const app::StoredTimescale none = app::apply_stored_timescale(empty, untouched);
    check(!none.origin && !none.zoom, "an unstored view applies nothing");
    check_date(untouched.origin, from_ymd(2020, 1, 28),
               "an unstored view keeps the plan origin");
}

void test_zoom_fit() {
    // Wave 31 (W31-2.1/2.2).  The four reference windows are Project 2021's
    // measured Entire Project / Selected Tasks results on the two fixtures
    // (tests/data/reference/w31-zoom.measures.json).  The port's fit is 85 % of
    // the pane with a floored origin; the assertions below pin the exact port
    // window and its distance from Project's.
    check_near(app::span_days_between(from_ymd(2026, 1, 4, 13, 0),
                                      from_ymd(2026, 1, 9, 12, 0)),
               4.958333, 1e-5, "the base project spans 4.958 calendar days");

    const app::ZoomFit base = app::fit_span_to_width(from_ymd(2026, 1, 4, 13, 0),
                                                     from_ymd(2026, 1, 9, 12, 0), 1020.0);
    check(base.valid, "the base project fit is valid");
    check(base.timescale.unit == core::gantt::TimescaleUnit::day,
          "a five-day span fits on the day tier");
    check_date(base.timescale.origin, from_ymd(2026, 1, 4),
               "the base fit floors to 2026-01-04, as Project's window starts");
    check_near(base.visible_days, 5.833333, 1e-5,
               "the base window is the 85 % ideal (Project measured 5.874)");
    check_near(base.timescale.pixels_per_unit, 1020.0 / 5.833333, 1e-3,
               "the base width is the pane divided by the visible span");
    check(base.timescale.pin_origin, "a fit pins its origin against the task snap");

    const app::ZoomFit outline = app::fit_span_to_width(from_ymd(2026, 12, 11, 8, 0),
                                                        from_ymd(2026, 12, 18, 17, 0), 1020.0);
    check(outline.valid, "the outline project fit is valid");
    check_date(outline.timescale.origin, from_ymd(2026, 12, 10),
               "the outline fit floors to 2026-12-10 (Project's window start)");
    check_near(outline.visible_days, 8.708333, 1e-5,
               "the outline window grows to contain its finish (Project measured 8.660)");

    const app::ZoomFit selection = app::fit_span_to_width(
        from_ymd(2026, 12, 17, 8, 0), from_ymd(2026, 12, 18, 17, 0), 1020.0);
    check(selection.valid, "the selected-tasks fit is valid");
    check_date(selection.timescale.origin, from_ymd(2026, 12, 17),
               "the selection fit floors to its first day");
    check_near(selection.visible_days, 1.708333, 1e-5,
               "a sub-day margin cannot be shown on the day tier: the window is the "
               "finish minus the floored origin (Project's 2-hour tier measured 1.606)");

    // The unit thresholds are the zoom_spec tiers.
    check(app::zoom_fit_unit(90.0) == core::gantt::TimescaleUnit::day,
          "90 visible days still fit on days");
    check(app::zoom_fit_unit(90.5) == core::gantt::TimescaleUnit::week,
          "beyond 90 days the fit uses weeks");
    check(app::zoom_fit_unit(1096.0) == core::gantt::TimescaleUnit::month,
          "beyond three years the fit uses months");

    // The Zoom dialog's fixed spans measured on Project: 7, 14 and (mean-month
    // rounded to its axis) about 31 and 90 days.  The port times the window
    // from the current origin's unit start.
    const app::ZoomFit week = app::fixed_span_to_width(from_ymd(2026, 10, 11), 7.0, 1020.0);
    check(week.valid, "the 1-week preset is valid");
    check_date(week.timescale.origin, from_ymd(2026, 10, 11),
               "the 1-week preset starts on the current origin");
    check_date(week.span_finish, from_ymd(2026, 10, 18), "the 1-week preset ends 7 days on");
    check_near(week.timescale.pixels_per_unit, 1020.0 / 7.0, 1e-6,
               "the 1-week preset is the whole pane over 7 days");
    const app::ZoomFit two_weeks = app::fixed_span_to_width(from_ymd(2026, 10, 11), 14.0, 1020.0);
    check_date(two_weeks.span_finish, from_ymd(2026, 10, 25), "2 weeks ends 14 days on");
    check_near(two_weeks.visible_days, 14.0, 1e-9, "2 weeks shows 14 days");
    const app::ZoomFit month = app::fixed_span_to_width(from_ymd(2026, 10, 11),
                                                        app::kZoomMonthDays, 1020.0);
    check(month.timescale.unit == core::gantt::TimescaleUnit::day,
          "1 month still fits on the day tier");
    check_near(month.visible_days, app::kZoomMonthDays, 1e-9, "1 month shows a mean month");
    const app::ZoomFit quarter = app::fixed_span_to_width(from_ymd(2026, 10, 11),
                                                          3.0 * app::kZoomMonthDays, 1020.0);
    check(quarter.timescale.unit == core::gantt::TimescaleUnit::week,
          "3 months fits on the week tier");
    check_near(quarter.visible_days, 3.0 * app::kZoomMonthDays, 1e-9,
               "3 months shows three mean months");
    check_near(quarter.timescale.pixels_per_unit, (1020.0 / (3.0 * app::kZoomMonthDays)) * 7.0,
               1e-6, "a week column is seven of the fit's days");

    // Degenerate input changes nothing.
    check(!app::fit_span_to_width(from_ymd(2026, 1, 5), from_ymd(2026, 1, 5), 1020.0).valid,
          "an empty span cannot be fitted");
    check(!app::fit_span_to_width(from_ymd(2026, 1, 4), from_ymd(2026, 1, 5), 0.0).valid,
          "a zero-width pane cannot be fitted");
    check(!app::fixed_span_to_width(kDateNa, 7.0, 1020.0).valid,
          "a fixed span needs a real anchor");
}

void test_undated_row() {
    // A row with no start keeps its row (the sheet still shows it) and gets no
    // bar; the links that touch it are not routed.
    app::PlanInput input = synthetic_plan();
    input.tasks.push_back(make_task(5, 5, "1.4", "Undated", 0, kDateNa, kDateNa, 0, 1, false, false,
                                    false));
    const core::gantt::GanttSource source = app::make_gantt_source(input, {}, kDateNa);
    const core::gantt::GanttLayout layout =
        core::gantt::build_gantt(source, app::default_gantt_style(),
                                 app::timescale_for_plan(input, 12.0));
    check_int(layout.row_count, 5, "undated row still counts as a row");
    check_size(layout.bars.size(), 4, "undated row has no bar");
    check(!layout.has_status_line(), "no status date, no status line");
}

void test_empty() {
    const app::PlanInput empty;
    core::view::RecordSet records = app::make_record_set(empty);
    check_size(records.row_count(), 0, "empty record set");
    core::sheet::SheetModel sheet(records, app::default_sheet_columns());
    check_size(sheet.row_count(), 0, "empty sheet");
    const core::gantt::GanttSource source = app::make_gantt_source(empty);
    const core::gantt::GanttLayout layout =
        core::gantt::build_gantt(source, app::default_gantt_style(), app::default_timescale(12.0));
    check_int(layout.row_count, 0, "empty gantt");
    check_size(layout.bars.size(), 0, "empty gantt bars");
    check_near(layout.content_width, 0.0, 1e-12, "empty content width");
    const app::PlanSummary summary = app::summarize(empty);
    check_size(summary.tasks, 0, "empty summary");
    check_date(summary.first_start, kDateNa, "empty summary first start is NA");
}

// ---------------------------------------------------------------------------
// Corpus probes.  Counts and errors only, never a task name.
// ---------------------------------------------------------------------------

// `expected_project_start` is kDateNa except for a fixture whose project start
// reader.h pins: it proves the packed -> date32 mapping end to end on real file
// bytes (W38: the old mapping was one day late, and this is where that shows).
void test_corpus_file(const std::filesystem::path& path, const char* label,
                      std::size_t pinned_tasks,
                      date32 expected_project_start = kDateNa) {
    if (!std::filesystem::exists(path)) {
        std::printf("SKIP: %s not present at %s\n", label, path.string().c_str());
        return;
    }
    mpp::Document document;
    try {
        document = mpp::load_file(path.string());
    } catch (const std::exception& error) {
        ++g_checks;
        ++g_failures;
        std::printf("FAIL: %s failed to load: %s\n", label, error.what());
        return;
    }
    const app::PlanInput input = app::plan_input_from_document(document);
    if (pinned_tasks != 0) {
        check_size(input.tasks.size(), pinned_tasks, "corpus task count");
    } else {
        check(!input.tasks.empty(), "corpus has tasks");
    }
    check_size(input.tasks.size(), document.tasks.size(), "reader task count == plan rows");

    // The identity mapping, on every stored date in the file: packed -> date32 ->
    // packed must reproduce the file's own word.  A shift in either direction
    // fails here.
    std::size_t round_trip_mismatch = 0;
    for (const mpp::Task& task : document.tasks) {
        if (task.start.valid() &&
            app::packed_from_date32(app::date32_from_packed(task.start.raw)) != task.start.raw) {
            ++round_trip_mismatch;
        }
        if (task.finish.valid() &&
            app::packed_from_date32(app::date32_from_packed(task.finish.raw)) != task.finish.raw) {
            ++round_trip_mismatch;
        }
    }
    check_size(round_trip_mismatch, 0, "packed <-> date32 round trips on every stored date");
    if (!core::sched::is_na(expected_project_start)) {
        check_date(app::date32_from_packed(document.project.start.raw), expected_project_start,
                   "corpus project start is the pinned date (not one day later)");
    }

    std::size_t dated = 0;
    std::size_t bad_dates = 0;
    std::size_t negative_levels = 0;
    std::size_t zero_day = 0;
    for (const app::TaskRow& row : input.tasks) {
        if (!core::sched::is_na(row.start)) {
            ++dated;
        }
        if (!date_in_range(row.start, 1980, 2100) || !date_in_range(row.finish, 1980, 2100)) {
            ++bad_dates;
        }
        if (row.outline_level < 0) {
            ++negative_levels;
        }
        if (row.duration_ticks == 0 && !row.summary) {
            ++zero_day;
        }
    }
    check_size(bad_dates, 0, "corpus dates are in 1980..2100");
    check_size(negative_levels, 0, "corpus outline levels are 0-based and non-negative");
    // Milestone is the reader's derivation (duration == 0, not a summary); the
    // plan model must not invent or drop one.
    check_size(app::summarize(input).milestones, zero_day,
               "milestones are exactly the zero-day non-summary rows");

    core::view::RecordSet records = app::make_record_set(input);
    // Wave 25 (W25-8/S10): the expected row count must NOT be derived from
    // input.show_project_summary -- that is the value under test, and a reader
    // that decoded 0x03400009 wrongly would still agree with itself.  The
    // expectation is pinned independently: PROJECT_A stores the row SHOWN (1),
    // and the COM-built w3-o1/holiday1 probes store it HIDDEN (0).
    // Compare the strings, not the pointers: string-literal pooling is not
    // guaranteed (/GF- would make the pointer comparison false).
    const bool expected_project_row = std::string(label) == "project_a";
    check(input.show_project_summary == expected_project_row,
          "the file's stored project-row setting is the pinned value");
    const std::size_t project_rows = expected_project_row ? 1u : 0u;
    check_size(records.row_count(), input.tasks.size() + project_rows,
               "corpus record set rows");
    core::sheet::SheetModel sheet(records, app::default_sheet_columns());
    check_size(sheet.row_count(), input.tasks.size() + project_rows,
               "corpus sheet rows");
    // The sheet's own formatting is non-empty for a dated first row.
    check(!sheet.cell_text(0, kStartColumn).empty(), "corpus sheet shows a start date");

    const core::gantt::GanttSource source = app::make_gantt_source(
        input, std::vector<date32>{}, app::today_date32());
    const core::gantt::GanttLayout layout = core::gantt::build_gantt(
        source, app::default_gantt_style(), app::timescale_for_plan(input, 12.0));
    check_int(layout.row_count, static_cast<std::int64_t>(input.tasks.size()),
              "corpus gantt rows == plan rows");
    check_size(layout.bars.size(), dated, "corpus bars == dated tasks");
    if (dated != 0) {
        check(layout.content_width > 0.0, "corpus content width is positive");
        check(!layout.header(core::gantt::HeaderTier::major, 0.0, layout.content_width).empty(),
              "corpus header cells");
    }
    const app::PlanSummary summary = app::summarize(input);
    std::printf("CORPUS: %s: %llu tasks, %llu links (%llu skipped), %llu summaries, "
                "%llu milestones, %llu critical\n",
                label, static_cast<unsigned long long>(summary.tasks),
                static_cast<unsigned long long>(summary.links),
                static_cast<unsigned long long>(input.skipped_links),
                static_cast<unsigned long long>(summary.summaries),
                static_cast<unsigned long long>(summary.milestones),
                static_cast<unsigned long long>(summary.critical));
}

// Wave 25 (W25-1/S1): the displayed-row Gantt and the edit-entry guard on a
// file that SHOWS the project row and stores its tasks out of ID order.
// tests/data/wave24-order.mpp stores FixedData rows 1..5 as IDs 1,3,4,5,2, and
// its 214/Props record 0x03400009 is 1 (row shown), so RecordSet row 0 is the
// project row and row 1 is the task with ID 2 (uid 5).
//
// Under the old row arithmetic (input.tasks[row]) the project row took the
// first task's bar, every other bar belonged to the task one sheet row below,
// and the last displayed row produced no bar.  Each check below is false on
// that code and true on the uid-resolved source the shells now call.
void test_displayed_rows_by_uid() {
#if defined(APP_TEST_DATA_DIR)
    const std::filesystem::path path =
        std::filesystem::path(APP_TEST_DATA_DIR) / "wave24-order.mpp";
    if (!std::filesystem::exists(path)) {
        check(false, "the committed W25-1 fixture wave24-order.mpp is present");
        return;
    }
    const mpp::Document document = mpp::load_file(path.string());
    const app::PlanInput input = app::plan_input_from_document(document);
    check(input.show_project_summary, "wave24-order.mpp stores the project row SHOWN");
    check_size(input.tasks.size(), 5, "wave24-order.mpp has five tasks");

    // The adapter sorts by ID, so vector position is not the stored order.
    bool order_differs = false;
    for (std::size_t index = 0; index < input.tasks.size(); ++index) {
        if (static_cast<std::size_t>(input.tasks[index].file_row) != index + 1) {
            order_differs = true;
        }
    }
    check(order_differs, "wave24-order.mpp's stored order differs from its ID order");
    check(input.tasks.size() > 1 && input.tasks[0].uid != 0 && input.tasks[1].uid != 0,
          "the plan's task list carries real uids");

    core::view::RecordSet records = app::make_record_set(input);
    check_size(records.row_count(), input.tasks.size() + 1,
               "the RecordSet carries one row more than the plan (the project row)");
    check_size(records.uid(0), 0, "RecordSet row 0 is the project row");
    check(records.uid(1) == input.tasks[0].uid, "RecordSet row 1 is the first ID-sorted task");

    core::sheet::SheetModel sheet(records, app::default_sheet_columns());
    app::apply_edit_policy(sheet);
    const std::vector<std::size_t> rows = sheet.visible_rows();
    check_size(rows.size(), records.row_count(), "every displayed row is visible");

    const core::gantt::GanttSource source =
        app::make_gantt_source_for_displayed_rows(input, records, rows, {}, kDateNa);
    check_size(source.tasks.size(), rows.size(),
               "the Gantt source has one task per displayed row (row arithmetic drops the last)");
    std::size_t source_mismatches = 0;
    for (std::size_t index = 0; index < source.tasks.size() && index < rows.size(); ++index) {
        if (source.tasks[index].uid != sheet.row_uid(rows[index])) {
            ++source_mismatches;
        }
    }
    check_size(source_mismatches, 0,
               "every Gantt task carries its displayed row's uid, not input.tasks[row]");
    if (!source.tasks.empty()) {
        check_size(source.tasks.front().uid, 0,
                   "the first displayed row draws the project bar (uid 0)");
    }

    // The layout the window draws: one bar per displayed row, in row order.
    const core::gantt::GanttLayout layout = core::gantt::build_gantt(
        source, app::default_gantt_style(),
        app::timescale_for_zoom(input, core::gantt::GanttZoom::week));
    check_size(layout.bars.size(), rows.size(),
               "the chart has one bar per displayed row (the smoke path's count)");
    std::size_t bar_mismatches = 0;
    for (const core::gantt::Bar& bar : layout.bars) {
        if (bar.row < 0 || static_cast<std::size_t>(bar.row) >= rows.size() ||
            bar.task_uid != sheet.row_uid(rows[static_cast<std::size_t>(bar.row)])) {
            ++bar_mismatches;
        }
    }
    check_size(bar_mismatches, 0, "every bar names the uid of the row it is drawn on");

    // Wave 26 (W26-0.1): the print row list.  The shells used to build it as
    // `0 .. tasks.size()-1`, which is one row short when the project row is
    // shown: row 0 is the project row, so the highest-ID task (the last
    // RecordSet row) never printed.  Every check in this block is false under
    // that arithmetic and true on app::print_row_list().
    const std::vector<std::size_t> printed =
        app::print_row_list(records.row_count(), {}, false);
    check_size(printed.size(), records.row_count(),
               "the print row list holds every RecordSet row, not one per task");
    check_size(printed.size(), input.tasks.size() + 1,
               "the print row list includes the project row as well as every task");
    check(!printed.empty() && printed.back() + 1 == records.row_count(),
          "the print row list ends at the last RecordSet row (the old loop stopped one short)");
    std::size_t missing_task_rows = 0;
    for (const app::TaskRow& task : input.tasks) {
        bool found = false;
        for (const std::size_t row : printed) {
            found = found || records.uid(row) == task.uid;
        }
        if (!found) {
            ++missing_task_rows;
        }
    }
    check_size(missing_task_rows, 0,
               "every task uid reaches the printer (the old arithmetic omitted the last task)");
    const std::vector<std::size_t> filter_rows{1, 3};
    check(app::print_row_list(records.row_count(), filter_rows, true) == filter_rows,
          "an active filter's print rows are the filter's rows verbatim");
    check(app::print_row_list(records.row_count(), {}, true).empty(),
          "a zero-match filter's print row list is empty, not every row");

    // The edit-entry guard (R5): the project row refuses a session, a task row
    // opens one and can be cancelled cleanly.
    const std::size_t name_column = sheet.column_of_field(app::name_field_id());
    std::string error;
    check(!app::begin_cell_edit(sheet, 0, name_column, error),
          "uid 0 (the project row) refuses an edit session");
    check(!sheet.editing(), "no edit session is left open after the refusal");
    check(app::begin_cell_edit(sheet, 1, name_column, error),
          "a task row opens an edit session");
    sheet.cancel_edit();
    check(!sheet.editing(), "the task-row session can be cancelled");

    // W33-1.7 (A-8 / D-7): a read-only session refuses a row that would
    // otherwise open, before the uid-0/column checks, and names the reason.
    const std::string read_only_reason(app::kUnreferencedSectorsReason);
    check(!app::begin_cell_edit(sheet, 1, name_column, error, read_only_reason),
          "a read-only document refuses a task-row edit session");
    check(error == read_only_reason, "the refusal carries the read-only reason");
    check(!sheet.editing(), "no edit session is left open after the read-only refusal");
    check(app::begin_cell_edit(sheet, 1, name_column, error),
          "the same row still opens a session when no reason is set");
    sheet.cancel_edit();
    check(!sheet.editing(), "the session opened without a reason can be cancelled");
#else
    std::printf("SKIP: APP_TEST_DATA_DIR is not defined; W25-1 fixture test not run\n");
#endif
}

void test_corpus() {
#if defined(APP_CORPUS_PROBE_DIR)
    const std::filesystem::path probe{APP_CORPUS_PROBE_DIR};
    // w3-o1.mpp is the COM-built 4-task fixture (reader.h cites it, including its
    // 2026-10-19 08:00 project start); holiday1.mpp is the calendar fixture.
    // Neither count nor date is client data.
    test_corpus_file(probe / "w3-o1.mpp", "w3-o1", 4, from_ymd(2026, 10, 19, 8, 0));
    test_corpus_file(probe / "holiday1.mpp", "holiday1", 0);
    if (std::filesystem::exists(probe.parent_path() / "project_a.mpp")) {
        test_corpus_file(probe.parent_path() / "project_a.mpp", "project_a", 0);
    } else {
        std::printf("SKIP: project_a not present\n");
    }
#else
    std::printf("SKIP: APP_CORPUS_PROBE_DIR is not defined; corpus probes not run\n");
#endif
}

}  // namespace

int main() {
    // Unbuffered so a crash is reported at the point it happens, not after the
    // buffered output is lost.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_date_conversion();
    test_record_set();
    test_sheet();
    test_gantt();
    test_stored_timescale();
    test_zoom_fit();
    test_undated_row();
    test_empty();
    test_displayed_rows_by_uid();
    test_corpus();

    if (g_failures != 0) {
        std::printf("FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("app_layout_tests: %d checks passed\n", g_checks);
    return 0;
}
