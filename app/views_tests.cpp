// app/views_tests.cpp -- the wave-11 views, proved headlessly.
//
// Covers:
//   * the resource sheet: rows/fields from a synthetic plan, the registry-backed
//     columns, the Type display name and the Work total;
//   * the usage views: day and week bucket sums for a flat assignment, resource
//     rows, the totals row and the totals column, and the period labels;
//   * the computed columns: [Duration]*2 and [Start]+7 evaluate through
//     core::formula::ComputedColumns, reach the task sheet, and are READ-ONLY
//     (no edit session, and the save diff never sees them);
//   * view-mode identification and the preference round trip (view, usage
//     period, visible formula columns) including a wave-10 string without the
//     new keys.
//
// Synthetic names/dates only: no corpus file, no client data, no file I/O.  The
// only names in this file are "Alpha", "Beta", "Task A", "Task B".

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "calendar.h"
#include "columns.h"
#include "date.h"
#include "document.h"
#include "layout_model.h"
#include "prefs.h"
#include "resource_tables.h"
#include "sheet.h"
#include "usage.h"
#include "value.h"
#include "view_model.h"
#include "viewstate.h"  // msproject::viewstate::TableDef (wave 21)

namespace {

using app::PlanInput;
using core::usage::Period;
using core::view::Value;
using core::view::ValueKind;

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
        std::printf("FAIL: %s: got \"%s\" want \"%s\"\n", what, got.c_str(), want.c_str());
    }
}

void check_size(std::size_t got, std::size_t want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got %zu want %zu\n", what, got, want);
    }
}

core::sched::date32 civil(int year, int month, int day, int hour = 0, int minute = 0) {
    return core::sched::from_ymd(year, month, day, hour, minute);
}

app::TaskRow task(std::uint32_t uid, const char* name, core::sched::date32 start,
                  core::sched::date32 finish, std::int64_t duration_ticks) {
    app::TaskRow row;
    row.uid = uid;
    row.id = uid;
    row.outline_number = std::to_string(uid);
    row.name = name;
    row.start = start;
    row.finish = finish;
    row.duration_ticks = duration_ticks;
    return row;
}

app::ResourceRow resource(std::uint32_t uid, const char* name, const char* initials,
                          double max_units, core::view::ResourceType type) {
    app::ResourceRow row;
    row.uid = uid;
    row.id = uid;
    row.name = name;
    row.initials = initials;
    row.max_units_percent = max_units;
    row.type = type;
    return row;
}

app::AssignmentRow assignment(std::uint32_t uid, std::uint32_t task_uid,
                              std::uint32_t resource_uid, std::int64_t work_ticks,
                              core::sched::date32 start, core::sched::date32 finish) {
    app::AssignmentRow row;
    row.uid = uid;
    row.task_uid = task_uid;
    row.resource_uid = resource_uid;
    row.work_ticks = work_ticks;
    row.units_percent = 100.0;
    row.start = start;
    row.finish = finish;
    return row;
}

// Two tasks, two resources, two assignments.  Assignment 101 (= 5 working days of
// 8 h) spans Monday-Friday; assignment 102 (= 1 working day) is Monday only.
// Both assignments belong to Task A, so Task A's row and the day totals sum both.
PlanInput make_plan() {
    PlanInput input;
    input.title = "Synthetic views plan";
    input.project_start = civil(2026, 10, 19, 8, 0);
    input.tasks.push_back(task(1, "Task A", civil(2026, 10, 19, 8, 0),
                               civil(2026, 10, 23, 17, 0), 5 * core::view::kTicksPerDay));
    input.tasks.push_back(task(2, "Task B", civil(2026, 10, 19, 8, 0),
                               civil(2026, 10, 20, 17, 0), core::view::kTicksPerDay));
    input.resources.push_back(
        resource(1, "Alpha", "AL", 100.0, core::view::ResourceType::work));
    input.resources.push_back(
        resource(2, "Beta", "BE", 50.0, core::view::ResourceType::material));
    input.assignments.push_back(assignment(101, 1, 1, 5 * core::view::kTicksPerDay,
                                           civil(2026, 10, 19, 8, 0),
                                           civil(2026, 10, 23, 17, 0)));
    input.assignments.push_back(assignment(102, 1, 2, core::view::kTicksPerDay,
                                           civil(2026, 10, 19, 8, 0),
                                           civil(2026, 10, 19, 17, 0)));
    return input;
}

void test_standard_calendar() {
    const core::sched::Calendar calendar = app::standard_calendar();
    const std::int32_t monday = core::sched::day_of(civil(2026, 10, 19));
    const std::int32_t saturday = core::sched::day_of(civil(2026, 10, 24));
    check(calendar.is_working_day(monday), "the stock calendar works Monday");
    check(!calendar.is_working_day(saturday), "the stock calendar does not work Saturday");
    check(core::usage::period_name(Period::day)[0] != '\0', "day periods have a name");
}

void test_usage_input_from_plan() {
    const PlanInput input = make_plan();
    const core::usage::UsageInput usage = app::usage_input_from_plan(input);
    check_size(usage.task_uids.size(), 2, "the plan's two tasks enter the usage input");
    check_size(usage.resource_uids.size(), 2, "the plan's two resources enter the usage input");
    check_size(usage.assignments.size(), 2, "both assignments enter the usage input");
    if (usage.assignments.size() != 2) {
        return;
    }
    check(usage.assignments[0].uid == 101, "assignment uid is carried");
    check(usage.assignments[0].task_uid == 1, "assignment task uid is carried");
    check(usage.assignments[0].resource_uid == 1, "assignment resource uid is carried");
    check(usage.assignments[0].work_minutes == 5 * 8 * 60,
          "work ticks become minutes (4800 ticks = 1 working day = 480 min)");
    check(usage.assignments[1].work_minutes == 8 * 60, "the one-day assignment is 480 min");
    check(usage.assignments[0].start == civil(2026, 10, 19, 8, 0),
          "assignment start is date32 by identity");
    check(usage.assignments[0].finish == civil(2026, 10, 23, 17, 0),
          "assignment finish is date32 by identity");
}

void test_resource_sheet() {
    PlanInput input = make_plan();
    // W31-3.5: the stored Entry values Alpha's row carries.
    input.resources[0].group = "North";
    input.resources[0].code = "CR-A";
    input.resources[0].base_calendar = "Standard";
    input.resources[0].standard_rate_per_hour = 50.0;
    input.resources[0].overtime_rate_per_hour = 75.0;
    input.resources[0].cost_per_use = 10.0;
    input.resources[0].accrue_at = 3;
    input.resources[0].cost_fields_stored = true;
    const core::usage::UsageInput usage = app::usage_input_from_plan(input);
    const core::usage::UsageProjection projection =
        core::usage::project_usage(usage, app::standard_calendar(), Period::day);

    const core::view::RecordSet records =
        app::make_resource_view_records(input, &projection.resources);
    check_size(records.row_count(), 2, "one resource row per resource");
    if (records.row_count() != 2) {
        return;
    }
    check(records.uid(0) == 1, "resource row 0 carries resource uid 1");
    check(records.uid(1) == 2, "resource row 1 carries resource uid 2");

    const Value* id = records.get(0, core::view::kResourceIdField);
    check(id != nullptr && id->kind == ValueKind::Integer && id->integer == 1,
          "the resource ID field is the stored id");
    const Value* name = records.get(0, core::view::kResourceNameField);
    check(name != nullptr && name->kind == ValueKind::Text && name->text == "Alpha",
          "the resource Name field is the reader's name");
    const Value* initials = records.get(0, core::view::kResourceInitialsField);
    check(initials != nullptr && initials->kind == ValueKind::Text && initials->text == "AL",
          "the resource Initials field is carried");
    const Value* max_units = records.get(1, core::view::kResourceMaxUnitsField);
    check(max_units != nullptr && max_units->kind == ValueKind::Number &&
              max_units->number == 50.0,
          "MaxUnits is the view percentage (50.0 == 50 %)");
    const Value* type = records.get(0, core::view::kResourceTypeField);
    check(type != nullptr && type->kind == ValueKind::Text && type->text == "Work",
          "the Type field is the display name, not the stored integer");
    const Value* material = records.get(1, core::view::kResourceTypeField);
    check(material != nullptr && material->kind == ValueKind::Text &&
              material->text == "Material",
          "a Material resource reads Material");
    // W31-3.5: the stored Entry values render as Project prints them.
    const Value* group = records.get(0, core::view::kResourceGroupField);
    check(group != nullptr && group->kind == ValueKind::Text && group->text == "North",
          "Group is carried into the resource sheet");
    const Value* code = records.get(0, core::view::kResourceCodeField);
    check(code != nullptr && code->kind == ValueKind::Text && code->text == "CR-A",
          "Code is carried into the resource sheet");
    const Value* calendar = records.get(0, core::view::kResourceBaseCalendarField);
    check(calendar != nullptr && calendar->kind == ValueKind::Text &&
              calendar->text == "Standard",
          "Base Calendar is carried into the resource sheet");
    const Value* std_rate = records.get(0, core::view::kResourceStandardRateField);
    check(std_rate != nullptr && std_rate->kind == ValueKind::Text &&
              std_rate->text == "$50.00/hr",
          "Standard Rate renders as Project's '$50.00/hr'");
    const Value* ovt_rate = records.get(0, core::view::kResourceOvertimeRateField);
    check(ovt_rate != nullptr && ovt_rate->kind == ValueKind::Text &&
              ovt_rate->text == "$75.00/hr",
          "Overtime Rate renders as Project's '$75.00/hr'");
    const Value* cost_per_use = records.get(0, core::view::kResourceCostPerUseField);
    check(cost_per_use != nullptr && cost_per_use->kind == ValueKind::Text &&
              cost_per_use->text == "$10.00",
          "Cost/Use renders as Project's '$10.00'");
    const Value* accrue = records.get(0, core::view::kResourceAccrueAtField);
    check(accrue != nullptr && accrue->kind == ValueKind::Text &&
              accrue->text == "Prorated",
          "Accrue At 3 renders as Prorated");
    // Beta's MPP12-style row (no decoded rate words) stays blank, not "$0.00".
    const Value* no_rate = records.get(1, core::view::kResourceStandardRateField);
    check(no_rate != nullptr && no_rate->kind == ValueKind::Null,
          "an undecoded rate stays Null in the view");
    // One 100 % assignment on a 100 % resource: PeakUnits 100, not overallocated.
    const Value* peak = records.get(0, core::view::kResourcePeakUnitsField);
    check(peak != nullptr && peak->kind == ValueKind::Number && peak->number == 100.0,
          "PeakUnits is the assignment peak (100 %)");
    const Value* overallocated = records.get(0, core::view::kResourceOverallocatedField);
    check(overallocated != nullptr && !overallocated->boolean,
          "one 100 % assignment is not overallocated");
    const Value* work = records.get(0, app::kResourceWorkField);
    check(work != nullptr && work->kind == ValueKind::Duration &&
              work->integer == 2400 * core::view::kTicksPerMinute,
          "resource Work is the assignment total in duration ticks");
    check(records.get(1, app::kResourceWorkField) != nullptr,
          "the Work column exists for every resource row");

    // The columns come from the registry's Entry table where they can, and the
    // five builder fields plus Work are all read-only.
    const std::vector<core::sheet::SheetColumn> columns = app::resource_sheet_columns();
    check_size(columns.size(), 6, "the resource sheet has ID/Name/Type/Initials/MaxUnits/Work");
    if (columns.size() == 6) {
        check(columns[0].field == core::view::kResourceIdField, "column 0 is ID");
        check(columns[1].field == core::view::kResourceNameField, "column 1 is Name");
        check(columns[2].field == core::view::kResourceTypeField, "column 2 is Type");
        check(columns[3].field == core::view::kResourceInitialsField, "column 3 is Initials");
        check(columns[4].field == core::view::kResourceMaxUnitsField, "column 4 is MaxUnits");
        check(columns[5].field == app::kResourceWorkField, "column 5 is Work");
        check(columns[5].kind == core::sheet::CellKind::Duration, "Work renders as a duration");
        for (const core::sheet::SheetColumn& column : columns) {
            check(column.read_only, "every resource column is read-only");
        }
        const core::view::TableDefinition* entry =
            core::view::find_resource_table("resource-entry");
        check(entry != nullptr, "the registry has the resource Entry table");
        if (entry != nullptr) {
            check(entry->has(columns[0].field) && entry->has(columns[4].field),
                  "the registry defines the ID..MaxUnits columns this view uses");
        }
    }

    // An empty plan still declares the Work column, so the schema is data-free.
    PlanInput empty;
    const core::view::RecordSet none = app::make_resource_view_records(empty, nullptr);
    check_size(none.row_count(), 0, "an empty plan has no resource rows");
    check(none.has_field(app::kResourceWorkField), "the Work field exists without data");
}

void test_resource_load() {
    // The measured shape of the wave-31 fixture: three 100 % assignments on one
    // resource overlap exactly, so PeakUnits is 300 % while MaxUnits is 100 %.
    PlanInput input;
    input.resources.push_back(
        resource(1, "Crew A", "C", 100.0, core::view::ResourceType::work));
    input.resources.push_back(
        resource(2, "Crew B", "C", 100.0, core::view::ResourceType::work));
    input.resources.push_back(
        resource(3, "Half", "H", 50.0, core::view::ResourceType::work));
    const core::sched::date32 monday = civil(2026, 10, 19, 8, 0);
    const core::sched::date32 friday = civil(2026, 10, 23, 17, 0);
    for (std::uint32_t index = 0; index < 3; ++index) {
        app::AssignmentRow row = assignment(200 + index, 1, 1, 4800,
                                            monday, friday);
        row.units_percent = 100.0;
        input.assignments.push_back(row);
    }
    // Crew B's two assignments are back to back: [Mon, Wed) and [Wed, Fri).
    app::AssignmentRow first = assignment(210, 2, 2, 4800, monday,
                                          civil(2026, 10, 21, 17, 0));
    app::AssignmentRow second = assignment(211, 3, 2, 4800,
                                           civil(2026, 10, 21, 17, 0), friday);
    input.assignments.push_back(first);
    input.assignments.push_back(second);
    // Half is 50 % MaxUnits with one 100 % assignment: overallocated.
    input.assignments.push_back(assignment(220, 4, 3, 4800, monday, friday));

    const app::ResourceLoad a = app::resource_load(input, 1);
    check(a.computed, "Crew A's peak is a real interval sweep");
    check(a.peak_units_percent == 300.0, "three overlapping 100 % assignments peak at 300 %");
    check(a.overallocated, "300 % exceeds MaxUnits 100 %: overallocated");

    const app::ResourceLoad b = app::resource_load(input, 2);
    check(b.computed, "Crew B's peak is a real interval sweep");
    check(b.peak_units_percent == 100.0,
          "back-to-back assignments do not overlap (peak 100 %, not 200 %)");
    check(!b.overallocated, "100 % on a 100 % resource is not overallocated");

    const app::ResourceLoad half = app::resource_load(input, 3);
    check(half.peak_units_percent == 100.0, "a single 100 % assignment peaks at 100 %");
    check(half.overallocated, "100 % on a 50 % resource is overallocated");

    const app::ResourceLoad none = app::resource_load(input, 99);
    check(!none.computed && none.peak_units_percent == 0.0 && !none.overallocated,
          "a resource with no assignments has no load and is not overallocated");

    // An assignment with no usable dates cannot be placed: the peak falls back
    // to the largest single assignment and `computed` says so.
    PlanInput untimed;
    untimed.resources.push_back(
        resource(4, "Untimed", "U", 100.0, core::view::ResourceType::work));
    app::AssignmentRow row = assignment(230, 1, 4, 4800, core::sched::kDateNa,
                                        core::sched::kDateNa);
    row.units_percent = 150.0;
    untimed.assignments.push_back(row);
    const app::ResourceLoad fallback = app::resource_load(untimed, 4);
    check(!fallback.computed, "an undated assignment is not a timed sweep");
    check(fallback.peak_units_percent == 150.0, "the fallback peak is the single assignment");
    check(fallback.overallocated, "a 150 % assignment on 100 % is overallocated regardless of dates");
}

void test_stored_resource_columns() {
    // The stored Entry table as the wave-31 fixture carries it: 14 columns in
    // file order, each with its stored character width (W31-3.4 gate).
    msproject::viewstate::TableDef table;
    table.decoded = true;
    struct Stored {
        std::uint32_t field;
        std::uint8_t width;
    };
    const Stored stored[] = {
        {0x0c400000u, 6},  {0x0c400056u, 6},  {0x0c400001u, 20}, {0x0c40012cu, 11},
        {0x0c40012bu, 13}, {0x0c400002u, 11}, {0x0c400003u, 10}, {0x0c400004u, 10},
        {0x0c400006u, 11}, {0x0c400007u, 11}, {0x0c400012u, 11}, {0x0c400013u, 10},
        {0x0c400005u, 14}, {0x0c40000au, 13},
    };
    for (const Stored& column : stored) {
        msproject::viewstate::ColumnDef record;
        record.field_id = column.field;
        record.opaque_header[0] = column.width;
        table.columns.push_back(record);
    }
    // Project stores the table-specific captions for these five and "" for the
    // rest; the view supplies the field captions for the empty ones.
    table.columns[2].title = "Resource Name";
    table.columns[7].title = "Max. Units";
    table.columns[8].title = "Std. Rate";
    table.columns[9].title = "Ovt. Rate";
    table.columns[10].title = "Cost/Use";

    const std::vector<core::sheet::SheetColumn> columns =
        app::resource_sheet_columns(table);
    check_size(columns.size(), 14, "the stored resource table has 14 columns");
    if (columns.size() != 14) {
        return;
    }
    const char* const titles[] = {
        "ID", "", "Resource Name", "Type", "Material Label", "Initials", "Group",
        "Max. Units", "Std. Rate", "Ovt. Rate", "Cost/Use", "Accrue At",
        "Base Calendar", "Code",
    };
    for (std::size_t index = 0; index < columns.size(); ++index) {
        check(columns[index].field == stored[index].field,
              "stored column field id follows the file order");
        check(columns[index].width == stored[index].width,
              "stored column width is the record's opaque header byte");
        check_text(columns[index].title, titles[index], "stored column caption");
        check(columns[index].read_only, "every stored resource column is read-only");
    }
    check(!columns[0].visible, "ID is hidden: Project draws it as the row-number gutter");
    check(columns[0].align == core::view::ColumnAlign::Center,
          "ID is centre aligned (COM AlignData 1)");
    check(columns[1].visible, "Indicators is a visible column");
    check(columns[1].title.empty(), "Indicators' header carries no text");
    check(columns[1].kind == core::sheet::CellKind::Text,
          "Indicators keeps a text cell for the glyph column");
    check(columns[7].kind == core::sheet::CellKind::Percent,
          "Max. Units renders as a percentage");
    check(columns[7].align == core::view::ColumnAlign::Right,
          "Max. Units is right aligned (COM AlignData 2)");
    check(columns[8].kind == core::sheet::CellKind::Text &&
              columns[10].kind == core::sheet::CellKind::Text,
          "the pre-formatted rate/cost columns render their text verbatim");
    check(columns[11].kind == core::sheet::CellKind::Text,
          "Accrue At renders the word");
    check(columns[3].align == core::view::ColumnAlign::Left,
          "Type is left aligned (COM AlignData 0)");

    // No stored table -> the registry fallback (a new file and the Qt view gate).
    msproject::viewstate::TableDef absent;
    const std::vector<core::sheet::SheetColumn> fallback =
        app::resource_sheet_columns(absent);
    check_size(fallback.size(), 6, "an undecoded resource table falls back to six columns");
}

void test_usage_day_and_week() {
    const PlanInput input = make_plan();
    const core::usage::UsageInput usage = app::usage_input_from_plan(input);
    const core::sched::Calendar calendar = app::standard_calendar();
    core::sheet::SheetFormat format;

    const core::usage::UsageProjection day = core::usage::project_usage(usage, calendar,
                                                                        Period::day);
    check_size(day.periods.size(), 5, "a Monday-Friday flat assignment has five day periods");
    check(day.tasks.period_total(0) == 960,
          "the first day holds 960 task minutes (two 8 h assignments)");

    const app::UsageView task_day = app::make_usage_view(day, false, input, format);
    check_size(task_day.table.rows.size(), 2, "task usage has one row per task");
    check_size(task_day.row_labels.size(), 2, "task usage labels every row");
    check_text(task_day.row_labels[0], "Task A", "the task usage row label is the task name");
    check_text(task_day.period_labels[0], "Mon 19/10/26", "the first day label is the date");
    check(task_day.table.minutes_at(0, 0) == 960, "Task A has both assignments in day 0");
    check(task_day.table.minutes_at(0, 1) == 480,
          "Task A has only the five-day assignment in day 1");
    check(task_day.table.row_total(0) == 2880, "Task A's row total is both assignments");
    check(task_day.table.row_total(1) == 0, "Task B has no assignment work");

    const core::view::RecordSet task_records = app::make_usage_record_set(task_day);
    check_size(task_records.row_count(), 3, "the task-usage grid has rows plus a totals row");
    if (task_records.row_count() == 3) {
        const Value* label = task_records.get(0, app::kUsageLabelField);
        check(label != nullptr && label->kind == ValueKind::Text && label->text == "Task A",
              "the grid's label cell carries the name");
        const Value* first = task_records.get(0, app::kUsagePeriodFieldBase);
        check(first != nullptr && first->kind == ValueKind::Duration &&
                  first->integer == 960 * core::view::kTicksPerMinute,
              "a 960-minute bucket is stored as 9600 duration ticks");
        const Value* total = task_records.get(0, app::kUsageTotalField);
        check(total != nullptr && total->kind == ValueKind::Duration &&
                  total->integer == 2880 * core::view::kTicksPerMinute,
              "the Total column is the row's authoritative total");
        const Value* totals_label = task_records.get(2, app::kUsageLabelField);
        check(totals_label != nullptr && totals_label->text == "Total",
              "the last grid row is the totals row");
        const Value* day_total = task_records.get(2, app::kUsagePeriodFieldBase);
        check(day_total != nullptr && day_total->kind == ValueKind::Duration &&
                  day_total->integer == 960 * core::view::kTicksPerMinute,
              "the totals row holds the period total");
        const Value* grand = task_records.get(2, app::kUsageTotalField);
        check(grand != nullptr && grand->kind == ValueKind::Duration &&
                  grand->integer == day.tasks.total_minutes * core::view::kTicksPerMinute,
              "the totals row's Total cell is the table total");
    }

    const app::UsageView resource_day = app::make_usage_view(day, true, input, format);
    check_text(resource_day.row_labels[0], "Alpha", "the resource usage label is the name");
    const core::usage::UsageRow* beta = resource_day.table.row(2);
    check(beta != nullptr, "the second resource has a usage row");
    if (beta != nullptr) {
        check(beta->work_minutes == 480, "Beta's one-day assignment totals 480 min");
    }
    const core::view::RecordSet resource_records = app::make_usage_record_set(resource_day);
    check_size(resource_records.row_count(), 3, "two resource rows plus the totals row");

    // Week: the same work in one clipped week period.
    const core::usage::UsageProjection week = core::usage::project_usage(usage, calendar,
                                                                         Period::week);
    check_size(week.periods.size(), 1, "one clipped week covers Monday-Friday");
    const app::UsageView task_week = app::make_usage_view(week, false, input, format);
    check_text(task_week.period_labels[0], "Wk Mon 19/10/26",
               "a week period's label names the week");
    check(task_week.table.minutes_at(0, 0) == 2880,
          "the week bucket sums both of Task A's assignments");
    check_size(app::usage_period_column_count(task_week), 1,
               "one period means one period column");

    // Month: one civil month.
    const core::usage::UsageProjection month = core::usage::project_usage(usage, calendar,
                                                                          Period::month);
    check_size(month.periods.size(), 1, "both assignments fall in one month");
    const app::UsageView task_month = app::make_usage_view(month, false, input, format);
    check_text(task_month.period_labels[0], "Oct 2026", "a month period is named by month");
    check_size(app::usage_sheet_columns(task_month).size(), 3,
               "the usage sheet is Label + one period + Total");
}

void test_formula_columns() {
    const PlanInput input = make_plan();
    core::view::RecordSet records = app::make_record_set(input);
    core::formula::ComputedColumns computed;
    std::string error;
    check(app::build_computed_columns(computed, error),
          "both wave-11 formula columns parse");
    check(error.empty(), "no formula parse error");
    check_size(computed.size(), app::formula_column_specs().size(),
               "every formula spec registered");

    core::sheet::SheetFormat format;
    computed.set_format(format);
    const std::size_t written = computed.evaluate(records);
    check(written == records.row_count() * computed.size(),
          "evaluate writes one value per row per formula column");

    const core::view::FieldId number1 = app::task_field_id("Number1");
    const core::view::FieldId date1 = app::task_field_id("Date1");
    check(number1 != 0 && date1 != 0, "the descriptor table has Number1 and Date1");
    if (number1 == 0 || date1 == 0) {
        return;
    }

    // [Duration]*2 over the 5-day task.
    const Value doubled = computed.value_of_field(records, 0, number1);
    check(doubled.kind == ValueKind::Duration, "Duration x2 is a Duration");
    check(doubled.integer == 2 * 5 * core::view::kTicksPerDay,
          "Duration x2 doubles the 5-day duration");
    // [Start]+7 over the Monday 08:00 start.
    const Value shifted = computed.value_of_field(records, 0, date1);
    check(shifted.kind == ValueKind::Date, "Start +7 is a Date");
    check(app::date32_from_date_value(shifted.date) == civil(2026, 10, 26, 8, 0),
          "Start +7 is seven calendar days later at the same time");

    // The task sheet shows them as ordinary columns.
    core::sheet::SheetModel sheet(records, app::task_sheet_columns());
    sheet.set_format(format);
    app::apply_edit_policy(sheet);
    app::apply_computed_read_only(sheet);
    const std::size_t number_column = sheet.column_of_field(number1);
    const std::size_t date_column = sheet.column_of_field(date1);
    check(number_column != core::sheet::kNoIndex, "the task sheet has the Duration x2 column");
    check(date_column != core::sheet::kNoIndex, "the task sheet has the Start +7 column");
    if (number_column != core::sheet::kNoIndex) {
        check_text(sheet.cell_text(0, number_column), "10 days",
                   "Duration x2 renders as the sheet's duration text");
        check(sheet.column_read_only(number_column), "the computed duration column is read-only");
        check(!sheet.begin_edit(0, number_column), "a computed column refuses an edit session");
        check(!sheet.editing(), "the refused edit left no session open");
    }
    if (date_column != core::sheet::kNoIndex) {
        check_text(sheet.cell_text(0, date_column), "Mon 26/10/26",
                   "Start +7 renders as the sheet's date text");
        check(sheet.column_read_only(date_column), "the computed date column is read-only");
        check(!sheet.begin_edit(0, date_column), "a computed date refuses an edit session");
    }

    // "Never written by Save": the save diff reads task starts only, and the
    // computed columns changed nothing it can see.
    check(app::collect_start_edits(records, input).empty(),
          "evaluating computed columns produces no save edit");
    const PlanInput synced = app::sync_plan_from_records(input, records);
    check(synced.tasks.size() == input.tasks.size(), "sync keeps the task rows");
    if (!synced.tasks.empty()) {
        check(synced.tasks[0].start == input.tasks[0].start,
              "sync does not read a computed Date1 as the start");
        check(synced.tasks[0].duration_ticks == input.tasks[0].duration_ticks,
              "sync does not read a computed Number1 as the duration");
    }

    // A plain default sheet plus the read-only policy is enough too: the helper
    // is what a caller with default_sheet_columns() uses.
    core::view::RecordSet plain_records = app::make_record_set(input);
    computed.evaluate(plain_records);
    core::sheet::SheetModel plain(plain_records, app::default_sheet_columns());
    app::apply_computed_read_only(plain);
    for (std::size_t column = 0; column < plain.column_count(); ++column) {
        check(!plain.column_read_only(column),
              "apply_computed_read_only leaves ordinary columns alone");
    }
}

void test_view_modes_and_preferences() {
    check_size(app::kViewModeCount, 4, "there are four view modes");
    const app::ViewMode modes[] = {app::ViewMode::task_sheet, app::ViewMode::resource_sheet,
                                   app::ViewMode::task_usage, app::ViewMode::resource_usage};
    for (const app::ViewMode mode : modes) {
        check(app::view_mode_name(mode)[0] != '\0', "every view mode has a name");
        check(app::view_mode_title(mode)[0] != '\0', "every view mode has a title");
        app::ViewMode parsed = app::ViewMode::task_sheet;
        check(app::view_mode_from_name(app::view_mode_name(mode), parsed),
              "a view-mode name round-trips");
        check(parsed == mode, "the parsed view mode is the original");
        check(app::view_mode_is_task(mode) == (mode == app::ViewMode::task_sheet),
              "only Task Sheet is a task view");
        check(app::view_mode_has_gantt(mode) == (mode == app::ViewMode::task_sheet),
              "only Task Sheet has a Gantt");
        check(app::view_mode_has_selection(mode) == (mode == app::ViewMode::task_sheet),
              "only Task Sheet has a cell selection");
        check(app::view_mode_is_usage(mode) ==
                  (mode == app::ViewMode::task_usage || mode == app::ViewMode::resource_usage),
              "the two usage modes are the usage modes");
    }
    app::ViewMode unknown = app::ViewMode::task_sheet;
    check(!app::view_mode_from_name("bogus", unknown), "an unknown view-mode name is refused");

    app::Preferences prefs;
    check(prefs.view == app::ViewMode::task_sheet, "the default view is the task sheet");
    check(prefs.usage_period == Period::day, "the default usage period is a day");
    check(prefs.formula_duration_column && prefs.formula_date_column,
          "both formula columns are visible by default");
    check(!prefs.stored_custom_fields, "stored custom fields are off (pending)");

    prefs.view = app::ViewMode::resource_usage;
    prefs.usage_period = Period::week;
    prefs.formula_duration_column = false;
    prefs.formula_date_column = true;
    prefs.stored_custom_fields = true;
    prefs.date_style = app::DateStyle::iso;
    const std::string text = app::serialize_preferences(prefs);
    check(text.find("view=resource_usage") != std::string::npos,
          "the view mode is stored by name");
    check(text.find("usage=week") != std::string::npos,
          "the usage period is stored by name");
    check(text.find("formula1=0") != std::string::npos,
          "the first formula column's visibility is stored as 0/1");
    check(text.find("formula2=1") != std::string::npos,
          "the second formula column's visibility is stored as 0/1");
    check(text.find("custom=1") != std::string::npos,
          "the stored-custom-field flag is persisted (even while pending)");

    app::Preferences parsed;
    check(app::parse_preferences(text, parsed), "a written string parses");
    check(parsed.view == prefs.view, "the view mode round-trips");
    check(parsed.usage_period == prefs.usage_period, "the usage period round-trips");
    check(parsed.formula_duration_column == prefs.formula_duration_column,
          "the first formula column round-trips");
    check(parsed.formula_date_column == prefs.formula_date_column,
          "the second formula column round-trips");
    check(parsed.stored_custom_fields == prefs.stored_custom_fields,
          "the pending custom-field flag round-trips");

    // A wave-10 string has none of the new keys: it parses and keeps defaults.
    app::Preferences older;
    check(app::parse_preferences("version=1;date=iso;duration=hours;zoom=week;shading=0",
                                 older),
          "a wave-10 preferences string still parses");
    check(older.view == app::ViewMode::task_sheet,
          "a wave-10 string keeps the task-sheet view");
    check(older.usage_period == Period::day, "a wave-10 string keeps the day period");
    check(older.formula_duration_column && older.formula_date_column,
          "a wave-10 string shows the formula columns");

    // Unknown and malformed wave-11 values keep the defaults.
    app::Preferences tolerant;
    check(!app::parse_preferences("view=bogus;usage=fortnight;formula1=maybe;custom=nah",
                                  tolerant),
          "a string with only bad values reads nothing");
    check(tolerant.view == app::ViewMode::task_sheet, "a bad view keeps the default");
    check(tolerant.usage_period == Period::day, "a bad period keeps the default");
    check(tolerant.formula_duration_column, "a bad formula flag keeps the default");

    const std::string summary = app::preferences_summary(prefs);
    check(summary.find("view resource_usage") != std::string::npos,
          "the status summary names the view");
    check(summary.find("usage week") != std::string::npos,
          "the status summary names the usage period");
    check(summary.find("formula columns 1") != std::string::npos,
          "the status summary counts the visible formula columns");

    // The visibility helpers map the two formula columns to their two pref keys
    // and leave anything else alone (the shell uses them for View > Columns).
    app::Preferences visibility;
    check(app::formula_column_visible(visibility, 0) && app::formula_column_visible(visibility, 1),
          "both formula columns are visible by default");
    check(app::set_formula_column_visible(visibility, 0, false), "formula column 0 is storable");
    check(!visibility.formula_duration_column &&
              visibility.formula_date_column,
          "hiding formula column 0 does not touch column 1");
    check(!app::set_formula_column_visible(visibility, 7, true),
          "an index past the two formula columns is not storable");
    check(app::formula_column_visible(visibility, 7),
          "a column past the two formula columns is always visible");
}

// ---------------------------------------------------------------------------
// Wave 21 (operator decision 7): the file's stored `214/CTable` table
// ---------------------------------------------------------------------------

msproject::viewstate::ColumnDef stored_column(std::uint32_t field_id, const char* title,
                                              std::uint8_t width) {
    msproject::viewstate::ColumnDef column;
    column.field_id = field_id;
    column.opaque_header[0] = width;
    column.title = title;
    return column;
}

// A CONSTRUCTED non-Entry table: a different order, different titles and
// different widths from the Entry presentation, one field the app cannot write
// (Text1), one field with no descriptor name (% Complete) and a non-column.
msproject::viewstate::TableDef test_table() {
    msproject::viewstate::TableDef table;
    table.caption = "&Wave 21 Test";
    table.columns.push_back(stored_column(0x0b400017u, "", 6));             // ID
    table.columns.push_back(stored_column(0x0b40000eu, "Job", 30));         // Name, renamed
    table.columns.push_back(stored_column(0x0b400505u, "Begin", 18));       // StartText
    table.columns.push_back(stored_column(0x0b400507u, "Elapsed", 10));     // DurationText
    table.columns.push_back(stored_column(0x0b400033u, "Discipline", 22));  // Text1
    table.columns.push_back(stored_column(0x0b400020u, "", 8));             // % Complete
    table.columns.push_back(stored_column(0x0b40002fu, "Depends On", 20));  // Predecessors
    table.columns.push_back(stored_column(0u, "not a column", 3));          // field id 0
    return table;
}

void test_table_sheet_columns() {
    const std::vector<core::sheet::SheetColumn> columns = app::table_sheet_columns(test_table());

    // The seven real records survive, in FILE order (not the Entry order), and
    // the field-id-0 record is not a column.
    check_size(columns.size(), 7, "the stored table's seven real columns survive, in order");
    if (columns.size() != 7) {
        return;
    }
    const char* const titles[] = {"ID",        "Job",          "Begin", "Elapsed",
                                  "Discipline", "% Complete", "Depends On"};
    const int widths[] = {6, 30, 18, 10, 22, 8, 20};
    for (std::size_t index = 0; index < columns.size(); ++index) {
        check_text(columns[index].title, titles[index], "a stored column keeps its title");
        check(columns[index].width == widths[index],
              "a stored column keeps its stored character width");
    }

    // The stored TEXT variants map onto the base task fields the sheet fills,
    // edits and saves; every other id is used as-is.
    check(columns[2].field == app::task_field_id("Start"),
          "the stored StartText column maps to Task.Start");
    check(columns[3].field == app::task_field_id("Duration"),
          "the stored DurationText column maps to Task.Duration");
    check(columns[5].field == 0x0b400020u, "an unmapped field id is used as-is");
    check(app::table_column_field(0x0b40000eu) == app::task_field_id("Name"),
          "a plain field id passes through table_column_field()");

    // Captions: the file's own title wins, "" resolves to the stock caption, an
    // unnamed field stays empty rather than printing its hex id.
    check_text(app::task_column_caption(app::task_field_id("Start")), "Start",
               "Start falls back to its stock caption");
    check_text(app::task_column_caption(0x0b400500u), "Task Mode",
               "the Task Mode column has a caption the descriptor table lacks");
    check_text(app::task_column_caption(0x0b40002fu), "Predecessors",
               "Predecessors has a caption");
    check_text(app::task_column_caption(0x0b400031u), "Resource Names",
               "Resource Names has a caption");
    check_text(app::task_column_caption(0x0b4000cdu), "",
               "Indicators stays unlabelled (the reference shows no caption)");
    check_text(app::task_column_caption(0x0b400020u), "% Complete", "% Complete has a caption");

    // The read-only policy: only the three writable task fields edit.
    check(columns[0].read_only, "the ID column is read-only (identity)");
    check(!columns[1].read_only, "Task Name is editable");
    // W32-0.2 (R31-2, High): Start is read-only -- Project's measured Start save
    // rewrites calculated and effective dates, slack, the project roll-up, a
    // Var2 record and DSI; this port's writer stores only the constraint window.
    check(columns[2].read_only, "Start is read-only (not measured against Project)");
    check(!columns[3].read_only, "Duration is editable");
    check(columns[4].read_only, "Text1 is read-only (no save path)");
    check(columns[5].read_only, "% Complete is read-only (no save path)");
    check(columns[6].read_only, "Predecessors is read-only (no save path)");

    // The stock align/kind are copied so a loaded sheet renders like the default.
    check(columns[1].kind == core::sheet::CellKind::Text, "Name keeps the stock Text kind");
    check(columns[2].kind == core::sheet::CellKind::Date, "Start keeps the stock Date kind");
    check(columns[3].kind == core::sheet::CellKind::Duration,
          "Duration keeps the stock Duration kind");
    check(columns[2].align == core::view::ColumnAlign::Left, "Start keeps the stock alignment");

    // A table with nothing usable is the sheet's default set (a new/empty
    // project, and the fallback for a file whose CTable is missing).
    msproject::viewstate::TableDef empty_table;
    const std::vector<core::sheet::SheetColumn> defaults = app::table_sheet_columns(empty_table);
    check_size(defaults.size(), app::default_sheet_columns().size(),
               "an empty table falls back to the sheet's default column set");
    check_text(defaults[0].title, "UID", "the fallback is the default set, not an empty table");
    msproject::viewstate::TableDef zero_only;
    zero_only.columns.push_back(stored_column(0u, "not a column", 5));
    check_size(app::table_sheet_columns(zero_only).size(), defaults.size(),
               "a table of only field-id-0 records falls back too");

    // W29-1.1 (measured, PROJECT_A): a stored width of 0 is not "auto" -- it is
    // a column Project does not draw.  PROJECT_A stores 0 for Indicators and the
    // clean 1920 capture shows Task Name at the sheet edge with no Indicators
    // column.  The record stays in the model, hidden, and its width is kept 0
    // rather than replaced with a stock width.
    msproject::viewstate::TableDef zero_width;
    zero_width.columns.push_back(stored_column(0x0b40000eu, "Job", 30));
    zero_width.columns.push_back(stored_column(0x0b4000cdu, "", 0));  // Indicators
    const std::vector<core::sheet::SheetColumn> kept = app::table_sheet_columns(zero_width);
    check_size(kept.size(), 2, "a stored zero-width column is kept in the model");
    if (kept.size() == 2) {
        check(kept[0].visible && kept[0].width == 30,
              "a stored positive width stays visible and unchanged");
        check(!kept[1].visible, "a stored zero width is hidden (Project draws no column)");
        check(kept[1].width == 0, "a stored zero width is not replaced by a stock width");
    }

    // The model really uses the set: the file's order, its captions, values in
    // the mapped fields, and the read-only columns refusing an edit.
    PlanInput input = make_plan();
    core::view::RecordSet records = app::make_record_set(input);
    core::sheet::SheetModel sheet(records, columns);
    check_size(sheet.column_count(), 7, "the sheet keeps the file's seven columns");
    check_text(sheet.column_title(2), "Begin", "the author's stored title is the header");
    check_text(sheet.column_title(3), "Elapsed", "the second renamed header is kept");
    check_text(sheet.cell_text(0, 2), "Mon 19/10/26",
               "the mapped Start column renders the task's start");
    check_text(sheet.cell_text(0, 3), "5 days",
               "the mapped Duration column renders the task's duration");
    check(!sheet.begin_edit(0, 4), "the read-only Text1 column refuses an edit");
    // W32-0.2: Start is read-only; the mapped Name column still proves the sheet
    // writes the model field the app reads back.
    check(!sheet.begin_edit(0, 2), "the read-only Start column refuses an edit");
    check(sheet.begin_edit(0, 1), "the Task Name column opens an edit session");
    sheet.set_edit_text("Task A renamed");
    const core::sheet::EditOutcome outcome = sheet.commit_edit(false);
    check(outcome.ok && outcome.changed, "the Name edit commits");
    check_text(sheet.cell_text(0, 1), "Task A renamed", "the edited Name is the cell text");
    check(sheet.undo(), "the edit is one undo step");
    check_text(sheet.cell_text(0, 1), "Task A", "undo restores the loaded Name");
    // The edit reached the RecordSet, which is what the Gantt and the save diff
    // read (the sheet is not a display-only copy).
    check(sheet.redo(), "redo re-applies the Name edit");
    const Value* name = records.get(0, app::task_field_id("Name"));
    check(name != nullptr && name->text == "Task A renamed",
          "the mapped column writes the model field the app reads back");
    const Value* start = records.get(0, app::task_field_id("Start"));
    check(start != nullptr && start->kind == ValueKind::Date,
          "the Start column still renders the model field");
}

}  // namespace

int main() {
    test_standard_calendar();
    test_usage_input_from_plan();
    test_resource_sheet();
    test_resource_load();
    test_stored_resource_columns();
    test_usage_day_and_week();
    test_formula_columns();
    test_view_modes_and_preferences();
    test_table_sheet_columns();

    if (g_failures != 0) {
        std::printf("FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("views_tests: %d checks passed\n", g_checks);
    return 0;
}
