// app/layout_model.cpp -- see layout_model.h for the contract.
//
// No AppKit, no file I/O, no reader types: everything here is arithmetic over
// date32 / RecordSet / GanttSource, which is what makes it testable off a Mac
// (app/layout_model_tests.cpp) and what the CI macOS job only has to link.

#include "layout_model.h"

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
    // date32 keeps the day in 16 bits and 0xffff is the NA day, so day 0xfffe
    // (+1) is already the last usable one.
    if (packed_day >= 0xFFFEu) {
        return kDateNa;
    }
    // The file's day 0 is 1983-12-31; the engine's day 1 is 1984-01-01.
    // One 6-second tick is one 1/10 minute, which is the engine's unit.
    return core::sched::pack(static_cast<std::int32_t>(packed_day) + 1,
                             static_cast<std::int32_t>(ticks));
}

std::uint32_t packed_from_date32(date32 day) noexcept {
    if (day == kDateNa || core::sched::is_overflow(day)) {
        return 0xFFFFFFFFu;
    }
    const std::int32_t engine_day = core::sched::day_of(day);
    if (engine_day < core::sched::kMinDay) {
        return 0xFFFFFFFFu;
    }
    return (static_cast<std::uint32_t>(engine_day - 1) << 16) |
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

core::view::RecordSet make_record_set(const PlanInput& input) {
    const TaskFields& field = task_fields();
    core::view::RecordSet records(core::model::RecordType::Task);

    const FieldId columns[] = {field.uid,    field.id,       field.outline_number, field.name,
                               field.start,  field.finish,   field.duration,       field.percent_complete,
                               field.summary, field.milestone, field.critical,     field.outline_level};
    for (const FieldId id : columns) {
        if (id != 0) {
            records.add_field(id);
        }
    }

    for (const TaskRow& row : input.tasks) {
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
        put(field.duration, Value::of_duration(row.duration_ticks));
        put(field.percent_complete, Value::of_int(static_cast<std::int64_t>(row.percent_complete)));
        put(field.summary, Value::of_bool(row.summary));
        put(field.milestone, Value::of_bool(row.milestone));
        put(field.critical, Value::of_bool(row.critical));
        put(field.outline_level, Value::of_int(static_cast<std::int64_t>(row.outline_level)));
    }
    return records;
}

core::view::FieldId task_field_id(std::string_view field_name) {
    return TaskFields::resolve_id(field_name);
}

// ---------------------------------------------------------------------------
// Gantt
// ---------------------------------------------------------------------------

core::gantt::GanttSource make_gantt_source(const PlanInput& input,
                                           const std::vector<date32>& holidays,
                                           date32 status_date) {
    core::gantt::GanttSource source;
    source.tasks.reserve(input.tasks.size());
    for (const TaskRow& row : input.tasks) {
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
        source.tasks.push_back(std::move(task));
    }
    source.links.reserve(input.links.size());
    for (const LinkRow& row : input.links) {
        core::gantt::LinkInput link;
        link.predecessor_uid = row.predecessor_uid;
        link.successor_uid = row.successor_uid;
        link.type = row.type;
        link.critical = row.critical;
        source.links.push_back(link);
    }
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
