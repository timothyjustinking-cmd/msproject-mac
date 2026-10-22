/*
 * mpp_bridge.h -- adapt the .mpp reader's v0 model to the shared RecordSet.
 *
 * The reader (core/mpp, another stream's code) returns mpp::Document with one
 * record per task and eight fixed members.  The view layer reads
 * core::view::RecordSet.  Rather than duplicate either model, this header
 * converts one to the other, and it is the contract the reader can replace
 * later by emitting RecordSet directly:
 *
 *   Name              -> Task.Name             (0x0b40000e)
 *   duration_ticks    -> Task.Duration         (0x0b40001d)
 *   priority          -> Task.Priority         (0x0b400019)
 *   percent_complete  -> Task.PercentComplete  (0x0b400020)
 *   start             -> Task.Start            (0x0b400023)
 *   finish            -> Task.Finish           (0x0b400024)
 *   uid               -> RecordSet UniqueID
 *
 * Field ids are resolved by name through the descriptor table, never hardcoded,
 * so a reader change or a table regeneration cannot silently point at the wrong
 * field.  Packed dates use the reader's format (spec/mpp-records.md section 3):
 * one u32, (day << 16) | seconds_of_day/6, day 0 = 1983-12-31; the conversion
 * keeps the file's civil time and applies no timezone.
 *
 * HEADER-ONLY AND OPTIONAL.  The include of "reader.h" is guarded, and CMake
 * only adds core/mpp to the include path when that file exists, so core/io
 * builds whether or not the reader stream is present or healthy.  No reader
 * symbol is linked: mpp::Document is a header-only value type.
 */

#ifndef CORE_IO_MPP_BRIDGE_H
#define CORE_IO_MPP_BRIDGE_H

#include <cstdint>
#include <string_view>

#include "field_ref.h"
#include "record_set.h"
#include "value.h"

#if defined(__has_include)
#if __has_include("reader.h")
#define CORE_IO_HAVE_MPP_READER 1
#include "reader.h"
#endif
#endif

#ifdef CORE_IO_HAVE_MPP_READER

namespace core::io {

namespace bridge_detail {

/* Howard Hinnant's civil-date algorithms, shared with core/view/group.cpp. */
inline std::int64_t days_from_civil(int year, unsigned month, unsigned day) noexcept {
    year -= month <= 2;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - static_cast<int>(era * 400));
    const unsigned doy =
        (153u * (month > 2u ? month - 3u : month + 9u) + 2u) / 5u + day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

inline core::view::DateValue civil_from_serial(std::int64_t serial) noexcept {
    serial += 719468;
    const std::int64_t era = (serial >= 0 ? serial : serial - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(serial - era * 146097);
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const int year = static_cast<int>(yoe) + static_cast<int>(era * 400);
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    const unsigned day = doy - (153u * mp + 2u) / 5u + 1u;
    const unsigned month = mp < 10u ? mp + 3u : mp - 9u;
    core::view::DateValue date;
    date.year = year + (month <= 2 ? 1 : 0);
    date.month = static_cast<std::uint8_t>(month);
    date.day = static_cast<std::uint8_t>(day);
    return date;
}

/* Serial number of the reader's day 0 (1983-12-31) in the civil calendar. */
inline std::int64_t reader_serial_base() noexcept {
    return days_from_civil(1983, 12, 31);
}

inline core::view::DateValue convert_date(const mpp::PackedDate& packed) noexcept {
    core::view::DateValue date;
    if (!packed.valid()) {
        return date;
    }
    date = civil_from_serial(reader_serial_base() + packed.day());
    const int seconds = packed.seconds_of_day();
    date.hour = static_cast<std::uint8_t>(seconds / 3600);
    date.minute = static_cast<std::uint8_t>((seconds % 3600) / 60);
    date.second = static_cast<std::uint8_t>(seconds % 60);
    return date;
}

inline core::view::FieldId field_id(core::model::RecordType record_type, const char* name) {
    core::view::FieldId field = 0;
    core::view::field_id_by_name(record_type, name, field);
    return field;
}

}  // namespace bridge_detail

/*
 * Converts the reader's task records into a Task RecordSet.  Fields the
 * descriptor table no longer names are skipped rather than assigned a guessed
 * id.  The project row is not converted: the descriptor table has no named
 * Project fields, so project-level values stay in mpp::Document::project.
 */
inline core::view::RecordSet to_record_set(const mpp::Document& document) {
    using core::model::RecordType;
    using bridge_detail::convert_date;
    using bridge_detail::field_id;

    const core::view::FieldId name_field = field_id(RecordType::Task, "Name");
    const core::view::FieldId duration_field = field_id(RecordType::Task, "Duration");
    const core::view::FieldId priority_field = field_id(RecordType::Task, "Priority");
    const core::view::FieldId percent_field = field_id(RecordType::Task, "PercentComplete");
    const core::view::FieldId start_field = field_id(RecordType::Task, "Start");
    const core::view::FieldId finish_field = field_id(RecordType::Task, "Finish");

    core::view::RecordSet records(RecordType::Task);
    if (name_field != 0) records.add_field(name_field);
    if (duration_field != 0) records.add_field(duration_field);
    if (priority_field != 0) records.add_field(priority_field);
    if (percent_field != 0) records.add_field(percent_field);
    if (start_field != 0) records.add_field(start_field);
    if (finish_field != 0) records.add_field(finish_field);

    for (const mpp::Task& task : document.tasks) {
        const std::size_t row = records.add_row(task.uid);
        if (name_field != 0 && !task.name.empty()) {
            records.set_field(row, name_field, core::view::Value::of_text(task.name));
        }
        if (duration_field != 0) {
            records.set_field(row, duration_field,
                              core::view::Value::of_duration(task.duration_ticks));
        }
        if (priority_field != 0) {
            records.set_field(row, priority_field,
                              core::view::Value::of_int(task.priority));
        }
        if (percent_field != 0) {
            records.set_field(row, percent_field,
                              core::view::Value::of_int(task.percent_complete));
        }
        if (start_field != 0 && task.start.valid()) {
            records.set_field(row, start_field,
                              core::view::Value::of_date(convert_date(task.start)));
        }
        if (finish_field != 0 && task.finish.valid()) {
            records.set_field(row, finish_field,
                              core::view::Value::of_date(convert_date(task.finish)));
        }
    }
    return records;
}

}  // namespace core::io

#endif  // CORE_IO_HAVE_MPP_READER

#endif  // CORE_IO_MPP_BRIDGE_H
