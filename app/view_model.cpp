// app/view_model.cpp -- see view_model.h for the contract, the calendar choice
// and the app-private field ids.
//
// No AppKit, no file I/O, no reader types: everything here is arithmetic over
// PlanInput / RecordSet / core::usage tables, which is what lets the whole view
// model be proved on Windows (app/views_tests.cpp).

#include "view_model.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "date.h"
#include "field_ref.h"
#include "layout_model.h"  // task_field_id()
#include "viewstate.h"     // msproject::viewstate::TableDef / ColumnDef

namespace app {
namespace {

using core::view::FieldId;
using core::view::Value;

const char* kMonthNames[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

std::string uid_label(const char* kind, std::uint32_t uid) {
    return std::string(kind) + " " + std::to_string(uid);
}

std::string task_name_of(const PlanInput& input, std::uint32_t uid) {
    for (const TaskRow& row : input.tasks) {
        if (row.uid == uid && !row.name.empty()) {
            return row.name;
        }
    }
    return uid_label("task", uid);
}

std::string resource_name_of(const PlanInput& input, std::uint32_t uid) {
    for (const ResourceRow& row : input.resources) {
        if (row.uid == uid && !row.name.empty()) {
            return row.name;
        }
    }
    return uid_label("resource", uid);
}

// One period header label.  Deterministic from the span and the sheet format;
// see view_model.h for the three shapes.
std::string period_label(const core::usage::PeriodSpan& span, core::usage::Period period,
                         const core::sheet::SheetFormat& format) {
    const date32 first = span.first_date();
    if (core::sched::is_na(first)) {
        return {};
    }
    if (period == core::usage::Period::month) {
        const core::sched::CivilDate civil = core::sched::to_civil(first);
        if (civil.month < 1 || civil.month > 12) {
            return {};
        }
        return std::string(kMonthNames[civil.month - 1]) + " " + std::to_string(civil.year);
    }
    const std::string date = core::sheet::format_date_text(date_value_from_date32(first), format);
    if (period == core::usage::Period::week) {
        return "Wk " + date;
    }
    return date;
}

// The resource fields the sheet shows, in Entry-table order.
struct ResourceColumnSpec {
    FieldId field = 0;
    core::sheet::CellKind kind = core::sheet::CellKind::Auto;
    const char* fallback_title = "";
    int fallback_width = 0;
};

const ResourceColumnSpec kResourceColumns[] = {
    {core::view::kResourceIdField, core::sheet::CellKind::Integer, "ID", 5},
    {core::view::kResourceNameField, core::sheet::CellKind::Text, "Resource Name", 24},
    {core::view::kResourceTypeField, core::sheet::CellKind::Text, "Type", 9},
    {core::view::kResourceInitialsField, core::sheet::CellKind::Text, "Initials", 9},
    {core::view::kResourceMaxUnitsField, core::sheet::CellKind::Percent, "Max. Units", 10},
};

// W31-3.5: Project's measured display currency symbol.  The FILE stores only
// the currency CODE (114/Props 0x024013bb = "AUD"); COM on the same fixture
// reports Project.CurrencySymbol "$" (build/w31/W31-G3-probe.json), an
// application option rather than file data.  The rate/cost strings below are
// that measured rendering.
constexpr const char* kMeasuredCurrencySymbol = "$";

// "$50.00", "$1,234.50" -- the sheet's own Cost formatting, so the group
// separator and two decimals are one implementation.
std::string currency_text(double value) {
    core::sheet::SheetFormat format;
    format.currency_symbol = kMeasuredCurrencySymbol;
    return core::sheet::format_value_text(Value::of_number(value),
                                          core::sheet::CellKind::Cost, format);
}

// PjAccrueAt -> the word Project shows.  nullptr for a value outside 1..3.
const char* accrue_at_text(std::int64_t value) {
    switch (value) {
        case 1: return "Start";
        case 2: return "End";
        case 3: return "Prorated";
        default: return nullptr;
    }
}

// The caption Project displays for an Entry-table field whose stored title is
// empty.  Measured sources: analysis/res/pjintl_strings.tsv RT_RCDATA 436_1033
// (the field captions: ID, Type, Material Label, Initials, Group, Accrue At,
// Base Calendar, Code), 173_1033 (the table's own captions: Resource Name,
// Max. Units, Std. Rate, Ovt. Rate, Cost/Use) and the W31-3.5 capture for
// Indicators, whose header carries the (i) glyph and no text.
std::string resource_column_caption(FieldId field) {
    switch (field) {
        case core::view::kResourceIdField: return "ID";
        case core::view::kResourceIndicatorsField: return "";
        case core::view::kResourceNameField: return "Resource Name";
        case core::view::kResourceTypeField: return "Type";
        case core::view::kResourceMaterialLabelField: return "Material Label";
        case core::view::kResourceInitialsField: return "Initials";
        case core::view::kResourceGroupField: return "Group";
        case core::view::kResourceMaxUnitsField: return "Max. Units";
        case core::view::kResourceStandardRateField: return "Std. Rate";
        case core::view::kResourceOvertimeRateField: return "Ovt. Rate";
        case core::view::kResourceCostPerUseField: return "Cost/Use";
        case core::view::kResourceAccrueAtField: return "Accrue At";
        case core::view::kResourceBaseCalendarField: return "Base Calendar";
        case core::view::kResourceCodeField: return "Code";
        default:
            return core::view::field_name(field, core::model::RecordType::Resource);
    }
}

// COM's TableField.AlignData on the fixture's stored Entry table: ID is centre
// (1), the numeric fields are right (2) and every text field is left (0).
core::view::ColumnAlign resource_column_align(FieldId field) noexcept {
    switch (field) {
        case core::view::kResourceIdField:
            return core::view::ColumnAlign::Center;
        case core::view::kResourceMaxUnitsField:
        case core::view::kResourceStandardRateField:
        case core::view::kResourceOvertimeRateField:
        case core::view::kResourceCostPerUseField:
        case core::view::kResourcePeakUnitsField:
            return core::view::ColumnAlign::Right;
        default:
            return core::view::ColumnAlign::Left;
    }
}

// The cell rendering for a stored resource column.  Rate and Cost/Use cells
// already hold Project's display TEXT (make_resource_view_records), so their
// kind must be Text or the sheet would reformat them; Max. Units stays a
// Number rendered as a percentage ("100%").
core::sheet::CellKind resource_column_kind(FieldId field) noexcept {
    switch (field) {
        case core::view::kResourceIdField:
            return core::sheet::CellKind::Integer;
        case core::view::kResourceMaxUnitsField:
        case core::view::kResourcePeakUnitsField:
            return core::sheet::CellKind::Percent;
        case core::view::kResourceOverallocatedField:
            return core::sheet::CellKind::Bool;
        case core::view::kResourceNameField:
        case core::view::kResourceIndicatorsField:
        case core::view::kResourceTypeField:
        case core::view::kResourceMaterialLabelField:
        case core::view::kResourceInitialsField:
        case core::view::kResourceGroupField:
        case core::view::kResourceStandardRateField:
        case core::view::kResourceOvertimeRateField:
        case core::view::kResourceCostPerUseField:
        case core::view::kResourceAccrueAtField:
        case core::view::kResourceBaseCalendarField:
        case core::view::kResourceCodeField:
            return core::sheet::CellKind::Text;
        default:
            return core::sheet::CellKind::Auto;
    }
}

// One boundary of an assignment's load interval.
struct LoadEvent {
    date32 day = kDateNa;
    bool start = false;
    double units = 0.0;
};

core::sheet::SheetColumn make_column(FieldId field, std::string title, int width,
                                     core::view::ColumnAlign align, core::sheet::CellKind kind,
                                     bool read_only) {
    core::sheet::SheetColumn column;
    column.field = field;
    column.title = std::move(title);
    column.width = width;
    column.align = align;
    column.kind = kind;
    column.visible = true;
    column.read_only = read_only;
    return column;
}

}  // namespace

// ---------------------------------------------------------------------------
// View modes
// ---------------------------------------------------------------------------

const char* view_mode_name(ViewMode mode) noexcept {
    switch (mode) {
        case ViewMode::task_sheet: return "task_sheet";
        case ViewMode::resource_sheet: return "resource_sheet";
        case ViewMode::task_usage: return "task_usage";
        case ViewMode::resource_usage: return "resource_usage";
    }
    return "task_sheet";
}

const char* view_mode_title(ViewMode mode) noexcept {
    switch (mode) {
        case ViewMode::task_sheet: return "Task Sheet";
        case ViewMode::resource_sheet: return "Resource Sheet";
        case ViewMode::task_usage: return "Task Usage";
        case ViewMode::resource_usage: return "Resource Usage";
    }
    return "Task Sheet";
}

bool view_mode_from_name(std::string_view name, ViewMode& out) noexcept {
    if (name == "task_sheet") {
        out = ViewMode::task_sheet;
        return true;
    }
    if (name == "resource_sheet") {
        out = ViewMode::resource_sheet;
        return true;
    }
    if (name == "task_usage") {
        out = ViewMode::task_usage;
        return true;
    }
    if (name == "resource_usage") {
        out = ViewMode::resource_usage;
        return true;
    }
    return false;
}

bool view_mode_is_task(ViewMode mode) noexcept { return mode == ViewMode::task_sheet; }

bool view_mode_is_usage(ViewMode mode) noexcept {
    return mode == ViewMode::task_usage || mode == ViewMode::resource_usage;
}

bool view_mode_has_gantt(ViewMode mode) noexcept { return mode == ViewMode::task_sheet; }

bool view_mode_has_selection(ViewMode mode) noexcept { return mode == ViewMode::task_sheet; }

core::usage::Period default_usage_period() noexcept { return core::usage::Period::day; }

// ---------------------------------------------------------------------------
// Calendar and the usage input
// ---------------------------------------------------------------------------

core::sched::Calendar standard_calendar() {
    const core::sched::DayPattern day =
        core::sched::day_pattern({{8 * 60, 12 * 60}, {13 * 60, 17 * 60}});
    return core::sched::Calendar(core::sched::make_week({
        {1, day},
        {2, day},
        {3, day},
        {4, day},
        {5, day},
    }));
}

core::usage::UsageInput usage_input_from_plan(const PlanInput& input) {
    core::usage::UsageInput usage;
    usage.task_uids.reserve(input.tasks.size());
    for (const TaskRow& row : input.tasks) {
        if (row.uid != 0) {
            usage.task_uids.push_back(row.uid);
        }
    }
    usage.resource_uids.reserve(input.resources.size());
    for (const ResourceRow& row : input.resources) {
        if (row.uid != 0) {
            usage.resource_uids.push_back(row.uid);
        }
    }
    usage.assignments.reserve(input.assignments.size());
    for (const AssignmentRow& row : input.assignments) {
        core::usage::AssignmentInput assignment;
        assignment.uid = row.uid;
        assignment.task_uid = row.task_uid;
        assignment.resource_uid = row.resource_uid;
        assignment.start = row.start;
        assignment.finish = row.finish;
        assignment.work_minutes =
            row.work_ticks > 0 ? row.work_ticks / core::view::kTicksPerMinute : 0;
        usage.assignments.push_back(assignment);
    }
    return usage;
}

// ---------------------------------------------------------------------------
// Resource sheet
// ---------------------------------------------------------------------------

ResourceLoad resource_load(const PlanInput& input, std::uint32_t resource_uid) {
    ResourceLoad load;
    if (resource_uid == 0) {
        return load;
    }
    double fallback_units = 0.0;
    bool any_assignment = false;
    std::vector<LoadEvent> events;
    for (const AssignmentRow& assignment : input.assignments) {
        if (assignment.resource_uid != resource_uid) {
            continue;
        }
        any_assignment = true;
        fallback_units = std::max(fallback_units, assignment.units_percent);
        if (core::sched::is_na(assignment.start) ||
            core::sched::is_na(assignment.finish) ||
            assignment.finish <= assignment.start) {
            continue;  // cannot be placed on the time axis (see the header)
        }
        events.push_back(LoadEvent{assignment.start, true, assignment.units_percent});
        events.push_back(LoadEvent{assignment.finish, false, assignment.units_percent});
    }
    if (events.empty()) {
        load.peak_units_percent = fallback_units;
    } else {
        // Half-open [start, finish): one day's endings and beginnings are
        // summed together, so a finish and a start at the same instant never
        // overlap.  Only the day matters to the sweep.
        std::sort(events.begin(), events.end(),
                  [](const LoadEvent& left, const LoadEvent& right) {
                      return left.day < right.day;
                  });
        double current = 0.0;
        double peak = 0.0;
        std::size_t index = 0;
        while (index < events.size()) {
            const date32 day = events[index].day;
            double delta = 0.0;
            while (index < events.size() && events[index].day == day) {
                delta += events[index].start ? events[index].units : -events[index].units;
                ++index;
            }
            current += delta;
            peak = std::max(peak, current);
        }
        load.peak_units_percent = peak;
        load.computed = true;
    }
    if (!any_assignment) {
        return load;  // no assignments: Project's PeakUnits 0, not overallocated
    }
    for (const ResourceRow& resource : input.resources) {
        if (resource.uid == resource_uid) {
            load.overallocated =
                load.peak_units_percent > resource.max_units_percent + 1e-6;
            break;
        }
    }
    return load;
}

std::vector<core::view::ResourceRecord> resource_records_from_plan(const PlanInput& input) {
    std::vector<core::view::ResourceRecord> records;
    records.reserve(input.resources.size());
    for (const ResourceRow& row : input.resources) {
        core::view::ResourceRecord record;
        record.uid = row.uid;
        record.id = row.id;
        record.name = row.name;
        record.initials = row.initials;
        record.type = row.type;
        record.max_units_percent = row.max_units_percent;
        record.group = row.group;
        record.code = row.code;
        record.material_label = row.material_label;
        record.base_calendar = row.base_calendar;
        record.standard_rate_per_hour = row.standard_rate_per_hour;
        record.overtime_rate_per_hour = row.overtime_rate_per_hour;
        record.cost_per_use = row.cost_per_use;
        record.accrue_at = row.accrue_at;
        record.cost_fields_stored = row.cost_fields_stored;
        const ResourceLoad load = resource_load(input, row.uid);
        record.peak_units_percent = load.peak_units_percent;
        record.overallocated = load.overallocated;
        records.push_back(std::move(record));
    }
    return records;
}

core::view::RecordSet make_resource_view_records(const PlanInput& input,
                                                 const core::usage::UsageTable* work_by_resource) {
    core::view::RecordSet records =
        core::view::build_resource_record_set(resource_records_from_plan(input));
    records.add_field(kResourceWorkField);

    const std::size_t rows = records.row_count();
    for (std::size_t row = 0; row < rows; ++row) {
        // Type: the builder stores PjResourceTypes (0/1/2); this view shows the
        // product's name.  An out-of-range stored value keeps the builder value
        // rather than being relabelled as Work.
        bool work_resource = true;
        if (const Value* value = records.get(row, core::view::kResourceTypeField);
            value != nullptr && value->kind == core::view::ValueKind::Integer) {
            const std::int64_t type = value->integer;
            if (type >= 0 && type <= 2) {
                work_resource = type == static_cast<std::int64_t>(
                                          core::view::ResourceType::work);
                records.set_field(
                    row, core::view::kResourceTypeField,
                    Value::of_text(core::view::resource_type_name(
                        static_cast<core::view::ResourceType>(type))));
            }
        }
        // Accrue At: the stored PjAccrueAt word becomes the word Project shows.
        if (const Value* value = records.get(row, core::view::kResourceAccrueAtField);
            value != nullptr && value->kind == core::view::ValueKind::Integer) {
            if (const char* text = accrue_at_text(value->integer); text != nullptr) {
                records.set_field(row, core::view::kResourceAccrueAtField,
                                  Value::of_text(text));
            }
        }
        // Rates and Cost/Use: Project's measured display strings.  A Work
        // resource's rate is per hour ("$50.00/hr"); for a Material/Cost
        // resource the file's rate unit is not measured, so only the currency
        // amount is printed (see view_model.h).  Null (MPP12) stays blank.
        if (const Value* value = records.get(row, core::view::kResourceStandardRateField);
            value != nullptr && value->kind == core::view::ValueKind::Number) {
            records.set_field(
                row, core::view::kResourceStandardRateField,
                Value::of_text(currency_text(value->number) +
                               (work_resource ? "/hr" : "")));
        }
        if (const Value* value = records.get(row, core::view::kResourceOvertimeRateField);
            value != nullptr && value->kind == core::view::ValueKind::Number) {
            records.set_field(
                row, core::view::kResourceOvertimeRateField,
                Value::of_text(currency_text(value->number) +
                               (work_resource ? "/hr" : "")));
        }
        if (const Value* value = records.get(row, core::view::kResourceCostPerUseField);
            value != nullptr && value->kind == core::view::ValueKind::Number) {
            records.set_field(row, core::view::kResourceCostPerUseField,
                              Value::of_text(currency_text(value->number)));
        }
        // Work: the resource's total assignment work, in duration ticks.
        std::int64_t minutes = 0;
        if (work_by_resource != nullptr) {
            if (const core::usage::UsageRow* usage =
                    work_by_resource->row(records.uid(row));
                usage != nullptr && usage->work_minutes > 0) {
                minutes = usage->work_minutes;
            }
        }
        records.set_field(row, kResourceWorkField,
                          Value::of_duration(minutes * core::view::kTicksPerMinute));
    }
    return records;
}

std::vector<core::sheet::SheetColumn> resource_sheet_columns() {
    const core::view::TableDefinition* entry =
        core::view::find_resource_table("resource-entry");
    std::vector<core::sheet::SheetColumn> columns;
    columns.reserve(6);
    for (const ResourceColumnSpec& spec : kResourceColumns) {
        std::string title = spec.fallback_title;
        int width = spec.fallback_width;
        core::view::ColumnAlign align = core::view::ColumnAlign::Auto;
        if (entry != nullptr) {
            // The registry is the authority for the caption and width of a
            // configured column; the fallbacks above only cover a missing table.
            if (const core::view::Column* column = entry->find(spec.field);
                column != nullptr) {
                if (!column->title.empty()) {
                    title = column->title;
                }
                if (column->width > 0) {
                    width = column->width;
                }
                align = column->align;
            }
        }
        if (align == core::view::ColumnAlign::Auto &&
            spec.kind == core::sheet::CellKind::Integer) {
            align = core::view::ColumnAlign::Right;
        }
        columns.push_back(
            make_column(spec.field, title, width, align, spec.kind, true));
    }
    // Work is not part of the Entry table definition (it is the Usage/Work
    // tables' column), so it is appended here with the same 11-character width.
    columns.push_back(make_column(kResourceWorkField, "Work", 11,
                                  core::view::ColumnAlign::Right,
                                  core::sheet::CellKind::Duration, true));
    return columns;
}

std::vector<core::sheet::SheetColumn> resource_sheet_columns(
    const msproject::viewstate::TableDef& table) {
    if (!table.decoded || table.columns.empty()) {
        return resource_sheet_columns();
    }
    std::vector<core::sheet::SheetColumn> columns;
    columns.reserve(table.columns.size());
    for (const msproject::viewstate::ColumnDef& record : table.columns) {
        if (record.field_id == 0) {
            continue;  // not a column
        }
        const FieldId field = static_cast<FieldId>(record.field_id);
        core::sheet::SheetColumn column;
        column.field = field;
        // Keep the stored id too, like the task sheet's stored columns.
        column.table_field = record.field_id;
        column.title =
            record.title.empty() ? resource_column_caption(field) : record.title;
        // The stored character width: the first of the nine opaque header bytes.
        column.width = static_cast<int>(record.opaque_header[0]);
        column.align = resource_column_align(field);
        column.kind = resource_column_kind(field);
        // Width 0 is "Project does not draw this column" (the task sheet's
        // measured rule).  ID is the sheet's frozen row-number gutter, not a
        // scrolling column, so it stays in the model hidden.
        column.visible = column.width > 0 && field != core::view::kResourceIdField;
        // A resource view has no measured save path in this port: every column
        // refuses an edit rather than offering one Save would drop.
        column.read_only = true;
        columns.push_back(std::move(column));
    }
    if (columns.empty()) {
        return resource_sheet_columns();
    }
    return columns;
}

// ---------------------------------------------------------------------------
// Usage views
// ---------------------------------------------------------------------------

std::size_t usage_period_column_count(const UsageView& view) noexcept {
    return std::min(view.table.periods.size(), kUsageMaxPeriodColumns);
}

UsageView make_usage_view(const core::usage::UsageProjection& projection, bool resource,
                          const PlanInput& input, const core::sheet::SheetFormat& format) {
    UsageView view;
    view.resource = resource;
    view.table = resource ? projection.resources : projection.tasks;
    view.period = view.table.period;
    view.grand_total_minutes = view.table.total_minutes;

    view.row_labels.reserve(view.table.rows.size());
    for (const core::usage::UsageRow& row : view.table.rows) {
        view.row_labels.push_back(resource ? resource_name_of(input, row.uid)
                                           : task_name_of(input, row.uid));
    }
    view.period_labels.reserve(view.table.periods.size());
    for (const core::usage::PeriodSpan& span : view.table.periods) {
        view.period_labels.push_back(period_label(span, view.period, format));
    }
    return view;
}

core::view::RecordSet make_usage_record_set(const UsageView& view) {
    core::view::RecordSet records(core::model::RecordType::Assignment);
    records.add_field(kUsageLabelField);
    records.add_field(kUsageTotalField);
    const std::size_t period_columns = usage_period_column_count(view);
    for (std::size_t index = 0; index < period_columns; ++index) {
        records.add_field(static_cast<FieldId>(kUsagePeriodFieldBase + index));
    }

    const std::size_t rows = view.table.rows.size();
    for (std::size_t index = 0; index < rows; ++index) {
        const core::usage::UsageRow& row = view.table.rows[index];
        const std::size_t target = records.add_row(row.uid);
        const std::string label =
            index < view.row_labels.size() ? view.row_labels[index] : std::string();
        records.set_field(target, kUsageLabelField, Value::of_text(label));
        for (std::size_t period = 0; period < period_columns; ++period) {
            const std::int64_t minutes =
                std::max<std::int64_t>(view.table.minutes_at(index, period), 0);
            records.set_field(target, static_cast<FieldId>(kUsagePeriodFieldBase + period),
                              Value::of_duration(minutes * core::view::kTicksPerMinute));
        }
        records.set_field(target, kUsageTotalField,
                          Value::of_duration(std::max<std::int64_t>(row.work_minutes, 0) *
                                             core::view::kTicksPerMinute));
    }

    // The totals row.  Its cells are the column sums; its Total cell is the
    // table's own total work (which can exceed the visible cells when an
    // assignment could not be placed -- core/usage decision 4).
    const std::size_t total_row = records.add_row(0);
    records.set_field(total_row, kUsageLabelField, Value::of_text(std::string("Total")));
    for (std::size_t period = 0; period < period_columns; ++period) {
        const std::int64_t minutes =
            std::max<std::int64_t>(view.table.period_total(period), 0);
        records.set_field(total_row, static_cast<FieldId>(kUsagePeriodFieldBase + period),
                          Value::of_duration(minutes * core::view::kTicksPerMinute));
    }
    records.set_field(total_row, kUsageTotalField,
                      Value::of_duration(std::max<std::int64_t>(view.grand_total_minutes, 0) *
                                         core::view::kTicksPerMinute));
    return records;
}

std::vector<core::sheet::SheetColumn> usage_sheet_columns(const UsageView& view) {
    std::vector<core::sheet::SheetColumn> columns;
    const std::size_t period_columns = usage_period_column_count(view);
    columns.reserve(period_columns + 2);
    columns.push_back(make_column(kUsageLabelField,
                                  view.resource ? "Resource Name" : "Task Name", 30,
                                  core::view::ColumnAlign::Left,
                                  core::sheet::CellKind::Text, true));
    int period_width = 14;
    if (view.period == core::usage::Period::week) {
        period_width = 18;
    } else if (view.period == core::usage::Period::month) {
        period_width = 12;
    }
    for (std::size_t index = 0; index < period_columns; ++index) {
        const std::string title =
            index < view.period_labels.size() ? view.period_labels[index] : std::string();
        columns.push_back(make_column(static_cast<FieldId>(kUsagePeriodFieldBase + index),
                                      title, period_width,
                                      core::view::ColumnAlign::Right,
                                      core::sheet::CellKind::Duration, true));
    }
    columns.push_back(make_column(kUsageTotalField, "Total", 12,
                                  core::view::ColumnAlign::Right,
                                  core::sheet::CellKind::Duration, true));
    return columns;
}

// ---------------------------------------------------------------------------
// Computed columns
// ---------------------------------------------------------------------------

const std::vector<FormulaColumnSpec>& formula_column_specs() {
    static const std::vector<FormulaColumnSpec> specs = []() {
        std::vector<FormulaColumnSpec> columns;
        const FieldId number1 = task_field_id("Number1");
        const FieldId date1 = task_field_id("Date1");
        if (number1 != 0) {
            columns.push_back(FormulaColumnSpec{number1, "Duration x2", "[Duration]*2"});
        }
        if (date1 != 0) {
            columns.push_back(FormulaColumnSpec{date1, "Start +7 Days", "[Start]+7"});
        }
        return columns;
    }();
    return specs;
}

int formula_column_index(core::view::FieldId field) noexcept {
    const std::vector<FormulaColumnSpec>& specs = formula_column_specs();
    for (std::size_t index = 0; index < specs.size(); ++index) {
        if (specs[index].field == field) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

std::vector<core::sheet::SheetColumn> task_sheet_columns() {
    std::vector<core::sheet::SheetColumn> columns = default_sheet_columns();
    for (const FormulaColumnSpec& spec : formula_column_specs()) {
        const bool is_date = std::string_view(spec.formula) == "[Start]+7";
        core::sheet::SheetColumn column;
        column.field = spec.field;
        column.title = spec.title;
        column.width = is_date ? 14 : 12;
        column.align = is_date ? core::view::ColumnAlign::Left
                               : core::view::ColumnAlign::Right;
        column.kind = core::sheet::CellKind::Auto;  // the evaluated value's own kind
        column.visible = true;
        column.read_only = true;
        columns.push_back(std::move(column));
    }
    return columns;
}

bool build_computed_columns(core::formula::ComputedColumns& out, std::string& error) {
    out = core::formula::ComputedColumns{};
    error.clear();
    for (const FormulaColumnSpec& spec : formula_column_specs()) {
        if (!out.add(spec.field, spec.title, spec.formula, error)) {
            return false;
        }
    }
    return true;
}

void apply_computed_read_only(core::sheet::SheetModel& sheet) {
    for (std::size_t column = 0; column < sheet.column_count(); ++column) {
        if (formula_column_index(sheet.column(column).field) >= 0) {
            sheet.set_column_read_only(column, true);
        }
    }
}

// ---------------------------------------------------------------------------
// Wave 21: the file's stored `214/CTable` table (operator decision 7)
// ---------------------------------------------------------------------------

namespace {

// The task fields the sheet's default set is built from, resolved once through
// the descriptor table (a table regeneration moves an id, never this code).
struct StockFields {
    core::view::FieldId name = task_field_id("Name");
    core::view::FieldId start = task_field_id("Start");
    core::view::FieldId finish = task_field_id("Finish");
    core::view::FieldId duration = task_field_id("Duration");
};

const StockFields& stock_fields() {
    static const StockFields fields;
    return fields;
}

// The stock captions for stored columns that carry no title.  Measured: the
// synthetic fixture and both corpus Entry tables leave exactly these fields
// unlabelled, and core/view's registry (PJINTL RT_RCDATA 330, the table resource
// Project itself ships) carries the captions while the descriptor table does not
// name four of the fields at all.  Indicators keeps an EMPTY caption: the
// reference capture's Indicators header has no text (docs/handoff/qt2.md), and a
// caption here would put a word in the band that the product does not.
struct CaptionEntry {
    std::uint32_t field = 0;
    const char* caption = "";
};

const CaptionEntry kStoredCaptions[] = {
    {0x0b4000cdu, ""},            // Indicators (unnamed in the descriptor table)
    {0x0b400500u, "Task Mode"},   // Task.Manual, the Entry table's mode column
    {0x0b40002fu, "Predecessors"},
    {0x0b400030u, "Successors"},
    {0x0b400031u, "Resource Names"},
    {0x0b400020u, "% Complete"},  // Task.PercentComplete
};

// The sheet's own default column for `field`, or nullptr (its align/kind/width
// are the ones a loaded sheet copies so it renders exactly like the default set).
const core::sheet::SheetColumn* stock_column(core::view::FieldId field) {
    static const std::vector<core::sheet::SheetColumn> stock = default_sheet_columns();
    for (const core::sheet::SheetColumn& column : stock) {
        if (column.field == field) {
            return &column;
        }
    }
    return nullptr;
}

}  // namespace

core::view::FieldId table_column_field(std::uint32_t table_field_id) noexcept {
    // The three measured text variants (see view_model.h).
    if (table_field_id == 0x0b400507u) {  // Task.DurationText
        return stock_fields().duration;
    }
    if (table_field_id == 0x0b400505u) {  // Task.StartText
        return stock_fields().start;
    }
    if (table_field_id == 0x0b400506u) {  // Task.FinishText
        return stock_fields().finish;
    }
    return static_cast<core::view::FieldId>(table_field_id);
}

std::string task_column_caption(core::view::FieldId field) {
    if (const core::sheet::SheetColumn* stock = stock_column(field); stock != nullptr) {
        return stock->title;
    }
    for (const CaptionEntry& entry : kStoredCaptions) {
        if (entry.field == field) {
            return entry.caption;
        }
    }
    // The descriptor table's own name, or "" for a field it never named.
    return core::view::field_name(field, core::model::RecordType::Task);
}

bool task_column_editable(core::view::FieldId field) noexcept {
    const StockFields& stock = stock_fields();
    return field != 0 && (field == stock.name || field == stock.start || field == stock.finish ||
                          field == stock.duration);
}

std::vector<core::sheet::SheetColumn> table_sheet_columns(
    const msproject::viewstate::TableDef& table) {
    std::vector<core::sheet::SheetColumn> columns;
    columns.reserve(table.columns.size());
    for (const msproject::viewstate::ColumnDef& record : table.columns) {
        if (record.field_id == 0) {
            continue;  // not a column
        }
        const core::view::FieldId field = table_column_field(record.field_id);
        if (field == 0) {
            continue;  // the table lost the field the record pointed at
        }
        core::sheet::SheetColumn column;
        column.field = field;
        // W29-1.2: keep the stored id too.  The applied view's cell-format
        // record keys on `214/CTable` field ids, not the mapped model fields.
        column.table_field = record.field_id;
        column.title = record.title.empty() ? task_column_caption(field) : record.title;
        // The stored character width: the first of the nine opaque header bytes.
        column.width = static_cast<int>(record.opaque_header[0]);
        if (const core::sheet::SheetColumn* stock = stock_column(field); stock != nullptr) {
            column.align = stock->align;
            column.kind = stock->kind;
        }
        // W29-1.1 (measured, PROJECT_A): a stored width of 0 is not "auto" -- it
        // is a column Project does not draw.  The file's Indicators record stores
        // 0 and the clean 1920 capture shows Task Name at the sheet edge with no
        // Indicators column.  The record stays in the model (it is the file's
        // table) but is hidden, and the width is kept as stored.
        column.visible = column.width > 0;
        // Read-only unless the save path can write the field back (see the header).
        column.read_only = !task_column_editable(field);
        columns.push_back(std::move(column));
    }
    if (columns.empty()) {
        // No usable column: a new/empty project's table, and the fallback for a
        // file whose CTable is missing or unreadable.
        return default_sheet_columns();
    }
    return columns;
}

}  // namespace app
