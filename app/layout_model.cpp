// app/layout_model.cpp -- see layout_model.h for the contract.
//
// No AppKit, no file I/O, no reader types: everything here is arithmetic over
// date32 / RecordSet / GanttSource, which is what makes it testable off a Mac
// (app/layout_model_tests.cpp) and what the CI macOS job only has to link.

#include "layout_model.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "date.h"
#include "field_ref.h"

namespace app {
namespace {

using core::view::FieldId;
using core::view::Value;

// Resolved once per process: the descriptor table lookup is by name, so a
// table regeneration cannot silently point a column at the wrong field.
struct TaskFields {
    FieldId uid = kUidField;
    FieldId id = 0;
    FieldId outline_number = 0;
    FieldId name = 0;
    FieldId start = 0;
    FieldId finish = 0;
    FieldId duration = 0;
    FieldId percent_complete = 0;
    FieldId summary = 0;
    FieldId milestone = 0;
    FieldId critical = 0;
    FieldId outline_level = 0;
    // The *text* variants Project rewrites a formula's [Start]/[Finish] reference
    // to (StartText 0x0b400505, FinishText 0x0b400506; measured in
    // docs/analysis/oracle/w10-oracle.md section 2 / memory/props.md).  The app
    // fills them from Start/Finish so a stored formula sees the date.
    FieldId start_text = 0;
    FieldId finish_text = 0;

    TaskFields() {
        resolve("ID", id);
        resolve("OutlineNumber", outline_number);
        resolve("Name", name);
        resolve("Start", start);
        resolve("Finish", finish);
        resolve("Duration", duration);
        resolve("PercentComplete", percent_complete);
        resolve("Summary", summary);
        resolve("Milestone", milestone);
        resolve("Critical", critical);
        resolve("OutlineLevel", outline_level);
        resolve("StartText", start_text);
        resolve("FinishText", finish_text);
    }

    static FieldId resolve_id(std::string_view field_name) {
        FieldId resolved = 0;
        if (!core::view::field_id_by_name(core::model::RecordType::Task, field_name, resolved)) {
            return 0;
        }
        return resolved;
    }

    static void resolve(std::string_view field_name, FieldId& out) {
        out = resolve_id(field_name);
    }
};

const TaskFields& task_fields() {
    static const TaskFields fields;
    return fields;
}

// Wave 23 (W23-6): Task.Flag10, Project's SUMMARY column.
constexpr core::view::FieldId kFlag10FieldId = 0x0B400051u;

core::sheet::SheetColumn make_column(FieldId field, const char* title, int width,
                                     core::view::ColumnAlign align, core::sheet::CellKind kind) {
    core::sheet::SheetColumn column;
    column.field = field;
    column.title = title;
    column.width = width;
    column.align = align;
    column.kind = kind;
    column.visible = true;
    return column;
}

double clamp_unit(std::uint16_t percent) {
    if (percent >= 100) {
        return 1.0;
    }
    return static_cast<double>(percent) / 100.0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Conversions
// ---------------------------------------------------------------------------

date32 date32_from_packed(std::uint32_t packed_raw) noexcept {
    if (packed_raw == 0u || packed_raw == 0xFFFFFFFFu) {
        return kDateNa;  // mpp::PackedDate::valid() is false for both
    }
    const std::uint32_t packed_day = packed_raw >> 16;
    const std::uint32_t ticks = packed_raw & 0xFFFFu;
    // The file's day 0 is 1983-12-31 and date32's day 1 is 1984-01-01, i.e. both
    // count days from 1983-12-31: file day D and date32 day D are the SAME date.
    // One 6-second tick is one 1/10 minute, which is also the same unit, so the
    // mapping is the identity (W38 correction; the earlier `+1` showed every
    // date one day late -- see app/README.md "date32 identity").
    if (packed_day > static_cast<std::uint32_t>(core::sched::kMaxDay)) {
        return kDateNa;  // day 0xffff would be the NA day's day field
    }
    return core::sched::pack(static_cast<std::int32_t>(packed_day),
                             static_cast<std::int32_t>(ticks));
}

std::uint32_t packed_from_date32(date32 day) noexcept {
    if (day == kDateNa || core::sched::is_overflow(day)) {
        return 0xFFFFFFFFu;
    }
    // The inverse of date32_from_packed(), also the identity.  One instant cannot
    // be expressed: 1983-12-31 00:00 packs to 0, which the file reads as "not
    // set".  Every other date32 value round-trips exactly.
    const std::int32_t engine_day = core::sched::day_of(day);
    if (engine_day < 0) {
        return 0xFFFFFFFFu;
    }
    return (static_cast<std::uint32_t>(engine_day) << 16) |
           static_cast<std::uint32_t>(core::sched::time_of(day));
}

date32 date32_from_date_value(const core::view::DateValue& value) noexcept {
    if (!value.valid()) {
        return kDateNa;
    }
    const std::int32_t tenths = static_cast<std::int32_t>(value.hour) * 600 +
                                static_cast<std::int32_t>(value.minute) * 10 +
                                static_cast<std::int32_t>(value.second) / 6;
    const date32 base = core::sched::from_ymd(static_cast<std::int32_t>(value.year),
                                             static_cast<std::int32_t>(value.month),
                                             static_cast<std::int32_t>(value.day));
    return core::sched::at_time(base, tenths);
}

core::view::DateValue date_value_from_date32(date32 day) noexcept {
    core::view::DateValue value;
    if (day == kDateNa || core::sched::is_overflow(day)) {
        return value;
    }
    const core::sched::CivilDate civil = core::sched::to_civil(day);
    const std::int32_t tenths = core::sched::time_of(day);
    value.year = civil.year;
    value.month = static_cast<std::uint8_t>(civil.month);
    value.day = static_cast<std::uint8_t>(civil.day);
    value.hour = static_cast<std::uint8_t>(tenths / 600);
    value.minute = static_cast<std::uint8_t>((tenths % 600) / 10);
    value.second = static_cast<std::uint8_t>((tenths % 10) * 6);
    return value;
}

date32 today_date32() {
    const auto now = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
    const std::chrono::year_month_day civil{now};
    if (!civil.ok()) {
        return kDateNa;
    }
    return core::sched::from_ymd(static_cast<std::int32_t>(static_cast<int>(civil.year())),
                                 static_cast<std::int32_t>(static_cast<unsigned>(civil.month())),
                                 static_cast<std::int32_t>(static_cast<unsigned>(civil.day())));
}

// ---------------------------------------------------------------------------
// Sheet
// ---------------------------------------------------------------------------

SheetRowText sheet_row_text(const TaskRow& row, const core::sheet::SheetFormat& format) {
    SheetRowText text;
    text.uid = std::to_string(row.uid);
    text.id = std::to_string(row.id);
    text.outline = row.outline_number;
    text.name = row.name;
    text.start = core::sheet::format_date_text(date_value_from_date32(row.start), format);
    text.finish = core::sheet::format_date_text(date_value_from_date32(row.finish), format);
    text.duration = core::sheet::format_duration_text(row.duration_ticks, format);
    return text;
}

std::vector<core::sheet::SheetColumn> default_sheet_columns() {
    const TaskFields& fields = task_fields();
    std::vector<core::sheet::SheetColumn> columns;
    columns.reserve(7);
    columns.push_back(make_column(fields.uid, "UID", 5, core::view::ColumnAlign::Right,
                                  core::sheet::CellKind::Integer));
    columns.push_back(make_column(fields.id, "ID", 5, core::view::ColumnAlign::Right,
                                  core::sheet::CellKind::Integer));
    columns.push_back(make_column(fields.outline_number, "Outline", 8,
                                  core::view::ColumnAlign::Left, core::sheet::CellKind::Text));
    columns.push_back(make_column(fields.name, "Name", 34, core::view::ColumnAlign::Left,
                                  core::sheet::CellKind::Text));
    columns.push_back(make_column(fields.start, "Start", 15, core::view::ColumnAlign::Left,
                                  core::sheet::CellKind::Date));
    columns.push_back(make_column(fields.finish, "Finish", 15, core::view::ColumnAlign::Left,
                                  core::sheet::CellKind::Date));
    columns.push_back(make_column(fields.duration, "Duration", 11,
                                  core::view::ColumnAlign::Left,
                                  core::sheet::CellKind::Duration));
    return columns;
}

core::sheet::SheetMetrics default_sheet_metrics() {
    core::sheet::SheetMetrics metrics;
    metrics.row_height = 20;
    metrics.header_height = 32;  // the two 16-point Gantt header tiers
    metrics.char_width = 7;
    metrics.indent_per_level = 10;
    metrics.min_column_width = 24;
    return metrics;
}

core::view::FieldId name_field_id() { return task_fields().name; }

namespace {

// The custom-field families the reader decodes into a core::view::Value.  A
// present-but-unmeasured value (Cost, Flag2..20) and an undecodable one are
// deliberately NOT guessed at: the cell stays blank.
core::view::Value custom_field_value(const mpp::CustomFieldValue& field) {
    if (!field.present || !field.storage_measured) {
        return Value::null();
    }
    switch (field.kind) {
        case mpp::FieldKind::text:
            return Value::of_text(field.text);
        case mpp::FieldKind::number:
            return Value::of_number(field.number);
        case mpp::FieldKind::date: {
            // The file stores the same packed word as Task.Start: day<<16|ticks,
            // and 0/0xffffffff mean "not set".
            const date32 day = date32_from_packed(field.packed_date);
            if (core::sched::is_na(day)) {
                return Value::null();
            }
            return Value::of_date(date_value_from_date32(day));
        }
        case mpp::FieldKind::duration:
            // File ticks already are core::view ticks (6 seconds each).
            return Value::of_duration(static_cast<std::int64_t>(field.duration_ticks));
        case mpp::FieldKind::flag:
            return Value::of_bool(field.flag);
        case mpp::FieldKind::cost:
        case mpp::FieldKind::unknown:
            return Value::null();
    }
    return Value::null();
}

}  // namespace

core::view::Value value_of_custom_field(const mpp::CustomFieldValue& field) {
    return custom_field_value(field);
}

TaskRow project_summary_row(const PlanInput& input) {
    TaskRow row;
    row.uid = 0;  // Project's project row is UniqueID/ID 0
    row.id = 0;
    row.name = input.title;
    row.duration_ticks = input.project_duration_ticks;
    row.percent_complete = input.project_percent_complete;
    row.start = input.project_start;
    row.finish = input.project_finish;
    // -1: same text indent as Project's top-level tasks (the clean capture), and
    // still the parent of every level-0 task, so collapsing it hides the plan.
    row.outline_level = -1;
    row.summary = true;
    row.milestone = false;
    row.critical = false;
    // Wave 24 (W24-9/R11): the SUMMARY column on the project row is the file's
    // own Flag10 bit for row 0 (FixedMeta entry +25 bit 0x04), decoded by the
    // reader.  Wave 23 hard-coded Yes from one capture; a file whose row does
    // not store the bit leaves the cell empty rather than inventing Yes.
    row.flag10 = input.project_flag10;
    row.flag10_stored = input.project_flag10_stored;
    return row;
}

core::view::RecordSet make_record_set(const PlanInput& input) {
    const TaskFields& field = task_fields();
    core::view::RecordSet records(core::model::RecordType::Task);

    const FieldId columns[] = {field.uid,      field.id,
                               field.outline_number, field.name,
                               field.start,    field.finish,
                               field.duration, field.percent_complete,
                               field.summary,  field.milestone,
                               field.critical, field.outline_level,
                               field.start_text, field.finish_text,
                               kFlag10FieldId};
    for (const FieldId id : columns) {
        if (id != 0) {
            records.add_field(id);
        }
    }
    // Wave 12: every stored custom field any row carries becomes a column, so the
    // schema is stable whether or not a particular row stores a value.
    for (const TaskRow& row : input.tasks) {
        for (const mpp::CustomFieldValue& custom : row.custom_fields) {
            if (custom.field_id != 0) {
                records.add_field(custom.field_id);
            }
        }
    }

    auto write_row = [&records, &field](const TaskRow& row) {
        const std::size_t index = records.add_row(row.uid);
        auto put = [&records, index](FieldId id, Value value) {
            if (id != 0) {
                records.set_field(index, id, std::move(value));
            }
        };
        put(field.uid, Value::of_int(static_cast<std::int64_t>(row.uid)));
        put(field.id, Value::of_int(static_cast<std::int64_t>(row.id)));
        if (!row.outline_number.empty()) {
            put(field.outline_number, Value::of_text(row.outline_number));
        }
        put(field.name, Value::of_text(row.name));  // empty text is a real value
        if (!core::sched::is_na(row.start)) {
            put(field.start, Value::of_date(date_value_from_date32(row.start)));
        }
        if (!core::sched::is_na(row.finish)) {
            put(field.finish, Value::of_date(date_value_from_date32(row.finish)));
        }
        // The formula alias fields, so `[Start]+7` (stored as
        // `[MSPJ188744965]+7`, StartText) evaluates like Project's formula view.
        // Inference: the stored token is measured; the mapping mirrors
        // core/formula's measured DurationText -> Duration fallback, which has no
        // StartText twin.
        if (!core::sched::is_na(row.start)) {
            put(field.start_text, Value::of_date(date_value_from_date32(row.start)));
        }
        if (!core::sched::is_na(row.finish)) {
            put(field.finish_text, Value::of_date(date_value_from_date32(row.finish)));
        }
        put(field.duration, Value::of_duration(row.duration_ticks));
        put(field.percent_complete, Value::of_int(static_cast<std::int64_t>(row.percent_complete)));
        put(field.summary, Value::of_bool(row.summary));
        put(field.milestone, Value::of_bool(row.milestone));
        put(field.critical, Value::of_bool(row.critical));
        put(field.outline_level, Value::of_int(static_cast<std::int64_t>(row.outline_level)));
        if (row.flag10_stored) {
            put(kFlag10FieldId, Value::of_bool(row.flag10));
        }
        for (const mpp::CustomFieldValue& custom : row.custom_fields) {
            Value value = custom_field_value(custom);
            if (!value.is_null()) {
                put(custom.field_id, std::move(value));
            }
        }
    };

    // Wave 23 (B-1): Project's project-summary row, ID 0, ahead of the tasks.
    if (input.show_project_summary) {
        write_row(project_summary_row(input));
    }
    for (const TaskRow& row : input.tasks) {
        write_row(row);
    }
    return records;
}

core::view::FieldId task_field_id(std::string_view field_name) {
    return TaskFields::resolve_id(field_name);
}

PlanInput sync_plan_from_records(const PlanInput& base, const core::view::RecordSet& records) {
    PlanInput input = base;
    const TaskFields& field = task_fields();
    // Wave 23 (B-1): the RecordSet carries the synthetic project row (uid 0)
    // ahead of the tasks when the presentation shows it; the plan itself does
    // not, so index 0 of the task list is RecordSet row 1 in that case.
    const std::size_t offset =
        (base.show_project_summary && records.row_count() > 0 && records.uid(0) == 0) ? 1 : 0;
    const std::size_t rows =
        std::min(input.tasks.size(), records.row_count() - offset);
    for (std::size_t index = 0; index < rows; ++index) {
        TaskRow& row = input.tasks[index];
        const std::size_t record = index + offset;
        row.uid = records.uid(record);

        if (const Value* value = records.get(record, field.name);
            value != nullptr && value->kind == core::view::ValueKind::Text) {
            row.name = value->text;
        }
        // A Null date is a cleared cell, not "leave the old value": clearing a
        // start makes the task undated, which the Gantt then draws no bar for.
        if (const Value* value = records.get(record, field.start); value != nullptr) {
            row.start = value->kind == core::view::ValueKind::Date
                            ? date32_from_date_value(value->date)
                            : kDateNa;
        }
        if (const Value* value = records.get(record, field.finish); value != nullptr) {
            row.finish = value->kind == core::view::ValueKind::Date
                             ? date32_from_date_value(value->date)
                             : kDateNa;
        }
        if (const Value* value = records.get(record, field.duration); value != nullptr &&
            (value->kind == core::view::ValueKind::Duration ||
             value->kind == core::view::ValueKind::Integer)) {
            row.duration_ticks = value->integer;
        }
        if (const Value* value = records.get(record, field.percent_complete);
            value != nullptr && value->kind == core::view::ValueKind::Integer) {
            const std::int64_t percent = std::clamp<std::int64_t>(value->integer, 0, 100);
            row.percent_complete = static_cast<std::uint16_t>(percent);
        }
        if (const Value* value = records.get(record, field.summary);
            value != nullptr && value->kind == core::view::ValueKind::Bool) {
            row.summary = value->boolean;
        }
        if (const Value* value = records.get(record, field.milestone);
            value != nullptr && value->kind == core::view::ValueKind::Bool) {
            row.milestone = value->boolean;
        }
        if (const Value* value = records.get(record, field.critical);
            value != nullptr && value->kind == core::view::ValueKind::Bool) {
            row.critical = value->boolean;
        }
        if (const Value* value = records.get(record, field.outline_level);
            value != nullptr && value->kind == core::view::ValueKind::Integer) {
            // -1 is the project-summary row (wave 23); every task level is >= 0.
            row.outline_level = static_cast<int>(std::max<std::int64_t>(value->integer, -1));
        }
        if (const Value* value = records.get(record, kFlag10FieldId);
            value != nullptr && value->kind == core::view::ValueKind::Bool) {
            row.flag10 = value->boolean;
            row.flag10_stored = true;
        }
    }
    input.skipped_links = base.skipped_links;
    return input;
}

void apply_edit_policy(core::sheet::SheetModel& sheet) {
    const TaskFields& field = task_fields();
    for (std::size_t index = 0; index < sheet.column_count(); ++index) {
        const core::view::FieldId id = sheet.column(index).field;
        if (id == field.uid || id == field.id || id == field.outline_number) {
            sheet.set_column_read_only(index, true);
        }
    }
}

// ---------------------------------------------------------------------------
// Gantt
// ---------------------------------------------------------------------------

namespace {

core::gantt::TaskBarInput bar_input_for(const TaskRow& row) {
    core::gantt::TaskBarInput task;
    task.uid = row.uid;
    task.label = row.name;
    task.start = row.start;
    task.finish = row.finish;
    task.percent_complete = clamp_unit(row.percent_complete);
    if (row.summary) {
        task.kind = core::gantt::BarKind::summary;
    } else if (row.milestone) {
        task.kind = core::gantt::BarKind::milestone;
    } else {
        task.kind = core::gantt::BarKind::normal;
    }
    task.critical = row.critical;
    return task;
}

std::vector<core::gantt::LinkInput> link_inputs_for(const PlanInput& input) {
    std::vector<core::gantt::LinkInput> links;
    links.reserve(input.links.size());
    for (const LinkRow& row : input.links) {
        core::gantt::LinkInput link;
        link.predecessor_uid = row.predecessor_uid;
        link.successor_uid = row.successor_uid;
        link.type = row.type;
        link.critical = row.critical;
        links.push_back(link);
    }
    return links;
}

}  // namespace

core::gantt::GanttSource make_gantt_source(const PlanInput& input,
                                           const std::vector<date32>& holidays,
                                           date32 status_date) {
    core::gantt::GanttSource source;
    source.tasks.reserve(input.tasks.size());
    for (const TaskRow& row : input.tasks) {
        source.tasks.push_back(bar_input_for(row));
    }
    source.links = link_inputs_for(input);
    source.working_week = core::gantt::WorkingWeek::monday_to_friday();
    source.nonworking_days = holidays;
    source.status_date = status_date;
    return source;
}

core::gantt::GanttSource make_gantt_source_for_rows(const PlanInput& input,
                                                    const std::vector<std::size_t>& rows,
                                                    const std::vector<date32>& holidays,
                                                    date32 status_date) {
    core::gantt::GanttSource source;
    source.tasks.reserve(rows.size());
    for (const std::size_t row : rows) {
        if (row < input.tasks.size()) {
            source.tasks.push_back(bar_input_for(input.tasks[row]));
        }
    }
    // Links stay in the source; build_gantt() skips one whose endpoint has no
    // bar, which is exactly a link to a hidden task.
    source.links = link_inputs_for(input);
    source.working_week = core::gantt::WorkingWeek::monday_to_friday();
    source.nonworking_days = holidays;
    source.status_date = status_date;
    return source;
}

core::gantt::Timescale default_timescale(double pixels_per_day) {
    core::gantt::Timescale timescale;
    timescale.unit = core::gantt::TimescaleUnit::day;
    timescale.pixels_per_unit = pixels_per_day > 0.0 ? pixels_per_day : 12.0;
    timescale.week_start = 1;  // Monday; the engine's week (core/sched) is Monday-first
    return timescale;
}

core::gantt::Timescale timescale_for_plan(const PlanInput& input, double pixels_per_day) {
    core::gantt::Timescale timescale = default_timescale(pixels_per_day);
    date32 origin = input.project_start;
    if (core::sched::is_na(origin)) {
        for (const TaskRow& row : input.tasks) {
            if (!core::sched::is_na(row.start) &&
                (core::sched::is_na(origin) || row.start < origin)) {
                origin = row.start;
            }
        }
    }
    if (!core::sched::is_na(origin)) {
        timescale.origin = core::sched::date_only(origin);
    }
    return timescale;
}

core::gantt::Timescale timescale_for_zoom(const PlanInput& input, core::gantt::GanttZoom zoom) {
    const core::gantt::ZoomSpec spec = core::gantt::zoom_spec(zoom);
    core::gantt::Timescale timescale = timescale_for_plan(input, spec.pixels_per_unit);
    timescale.unit = spec.unit;
    timescale.pixels_per_unit = spec.pixels_per_unit;
    return timescale;
}

core::gantt::GanttStyle default_gantt_style() {
    core::gantt::GanttStyle style;
    style.row_height = static_cast<double>(default_sheet_metrics().row_height);
    return style;
}

PlanSummary summarize(const PlanInput& input) {
    PlanSummary summary;
    summary.tasks = input.tasks.size();
    summary.links = input.links.size();
    for (const TaskRow& row : input.tasks) {
        if (row.summary) {
            ++summary.summaries;
        }
        if (row.milestone) {
            ++summary.milestones;
        }
        if (row.critical) {
            ++summary.critical;
        }
        if (!core::sched::is_na(row.start) &&
            (core::sched::is_na(summary.first_start) || row.start < summary.first_start)) {
            summary.first_start = row.start;
        }
        if (!core::sched::is_na(row.finish) &&
            (core::sched::is_na(summary.last_finish) || row.finish > summary.last_finish)) {
            summary.last_finish = row.finish;
        }
    }
    return summary;
}

}  // namespace app
