// core/cal/cal.h -- decode the base calendars of MPP14's "114/TBkndCal" table
// and build a core::sched::Calendar from one of them.
//
// The table is a fixed-row table (FixedMeta/FixedData) whose per-calendar
// payloads live in Var2Data.  Everything below was measured on
// corpus/probe/{base,holiday1,w3-o1,lagff1}.mpp and on the two real files, and
// cross-checked against the layout mpxj's MPP14CalendarFactory implements
// (see core/cal/README.md for the byte evidence and the command that dumps it).
//
// Layout summary (all offsets verified, see README):
//
//   114/TBkndCal/FixedMeta : 16-byte header + n x 10-byte entries
//       +0  u32 magic 0xFADFADBA (stored ba ad df fa)
//       +4  u32 version, 4
//       +8  u32 entry count
//       +12 u32 FixedData byte size
//       entry: +0 u16 tag (4 for the four header descriptors, 0 for rows)
//              +2 u16 field count
//              +4 u32 byte offset into FixedData
//              +8 u16 block length
//
//   114/TBkndCal/FixedData : four 16-byte header descriptors (64 bytes), then
//       a 12-byte row per calendar:
//       +0 i32 base calendar uid   (<= 0: this row is a base calendar)
//       +4 i32 resource uid        (-1: not a resource calendar)
//       +8 i32 calendar uid        (> 0, the key into VarMeta/Var2Data)
//
//   114/TBkndCal/VarMeta  : 24-byte header + n x 12-byte (uid, offset, key).
//       key 0x0D400001 (low word 1) = calendar name, UTF-16LE NUL-terminated.
//       key 0x0D400008 (low word 8) = calendar data block, layout below.
//
//   114/Props : 16-byte header + records of (u32 length, u32 key, u32 unused,
//       data).  key 37753736 (0x02401388, DEFAULT_CALENDAR_HOURS) holds 420
//       bytes = seven 60-byte day blocks: this is where the *Standard* base
//       calendar lives (it has no Var2Data payload of its own).
//       key 37748750 (0x0240000E, DEFAULT_CALENDAR_NAME) holds the project's
//       default calendar name as UTF-16LE.
//
//   calendar data block:
//       0    .. 419 : seven 60-byte day blocks, index 0 = Sunday
//       420        : u16 exception count
//       count != 0 : count x (92-byte block + 4-byte-aligned UTF-16 name)
//       then       : u32 work-week header, then work-week records:
//                    7 x 60-byte day blocks, u16 start day, u16 finish day,
//                    8 unknown bytes, u32 name length (+ padded name)
//
//   60-byte day block:
//       +0  u16 day type: 1 = "use the inherited times", anything else = custom
//       +2  u16 interval count (meaningless when day type is 1)
//       +4  u32 total tenths of working time that day
//       +8  u16 start[i], tenths of a minute from midnight
//       +20 u32 length[i], tenths of a minute
//
// Client-data rule: calendar names are decoded because lookup needs them, but
// exception and work-week names are skipped (only their padded length is used),
// and no tool or test in this stream prints a calendar name from a corpus file.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "calendar.h"
#include "cfb.h"

namespace core::cal {

// A day record is 60 bytes and a calendar week holds seven of them (spec
// §1.1 and the measured data block; exposed so tests and tools can describe
// the layout without copying the number).
inline constexpr std::size_t kDayBlockBytes = 60;
inline constexpr std::size_t kWeekBlockBytes = 7 * kDayBlockBytes;  // 420

// One working interval as stored: tenths of a minute from midnight (the same
// 6-second tick as core::sched::date32, spec/scheduling.md §0.1).
struct WorkInterval {
    std::int32_t start_ticks = 0;
    std::int32_t length_ticks = 0;
    std::int32_t end_ticks() const noexcept { return start_ticks + length_ticks; }
};

// One weekday slot of a 60-byte day block.
//
// `use_base` is the file's day-type word: true when it is 1, which means "use
// the inherited times" -- the project's default week for a base calendar, the
// parent calendar for a derived one.  The other 58 bytes of a "use base" block
// are stale buffer contents in real files and must not be read.
struct DaySpec {
    bool use_base = true;
    std::vector<WorkInterval> intervals;  // empty + !use_base = non-working
};

// index 0 = Sunday, matching core::sched::weekday_of_day (spec §0.1).
using WeekSpec = std::array<DaySpec, 7>;

// One dated exception.  From/to are inclusive day numbers (the same day
// counter as date32's high half; 15635 = 2026-10-21).
//
// `working` false is a non-working exception (its 92-byte block has no
// intervals); true is a special working day with its own intervals.
//
// `recurrence_type` is PjExceptionType as stored at block +72.  0 and 1 are a
// plain dated range (measured: every exception in every file on disk is 1).
// Types 2..7 are genuinely recurring: they cannot be expressed as per-week
// overrides without expanding the recurrence, so the builder reports them
// through CalendarTable::recurring_exceptions and does not apply them.  No
// fixture on disk contains one.
struct DatedException {
    std::int32_t start_day = 0;
    std::int32_t end_day = 0;
    bool working = false;
    std::vector<WorkInterval> intervals;
    std::int32_t recurrence_type = 0;
};

// A dated work-week override ("Calendar > Work Weeks"): a seven-day pattern
// that applies to the calendar dates [start_day, end_day].  Measured: zero of
// these exist in any file on disk, so the layout is implemented from mpxj's
// MPP14 reader and exercised only by the synthetic test.
struct DatedWorkWeek {
    std::int32_t start_day = 0;
    std::int32_t end_day = 0;
    WeekSpec days{};
};

// One row of 114/TBkndCal/FixedData plus its Var2Data payloads.
struct CalendarDefinition {
    std::int32_t uid = 0;           // +8 in the row
    std::int32_t base_uid = -1;     // +0 in the row; <= 0 means "base calendar"
    std::int32_t resource_uid = -1; // +4 in the row; -1 means "not a resource calendar"
    std::string name;               // Var2Data key 0x0D400001; empty when absent
    bool has_week_data = false;     // Var2Data key 0x0D400008 present
    WeekSpec week{};                // from the data block (flags still unresolved)
    std::vector<DatedException> exceptions;
    std::vector<DatedWorkWeek> work_weeks;
};

// Everything 114/TBkndCal plus the project defaults say about calendars.
struct CalendarTable {
    std::vector<CalendarDefinition> calendars;  // FixedData row order
    WeekSpec default_week{};                    // 114/Props DEFAULT_CALENDAR_HOURS
    std::string default_calendar_name;          // 114/Props DEFAULT_CALENDAR_NAME
    // Counters, for reporting rather than guessing:
    std::size_t metadata_anomalies = 0;    // FixedMeta/VarMeta disagreed with FixedData
    std::size_t recurring_exceptions = 0;  // PjExceptionType 2..7, not applied
    std::size_t sub_minute_intervals = 0;  // intervals not a whole minute

    std::size_t base_calendar_count() const noexcept;
    std::size_t derived_calendar_count() const noexcept;
    std::size_t exception_count() const noexcept;
    std::size_t work_week_count() const noexcept;
};

// The payload of one calendar data block.
struct CalendarData {
    WeekSpec days{};
    std::vector<DatedException> exceptions;
    std::vector<DatedWorkWeek> work_weeks;
    // True when the block ended in the middle of an exception or work week and
    // decoding stopped there (a damaged file, not a format feature).
    bool truncated = false;
};

// --- decoders ---------------------------------------------------------------

// Seven 60-byte day blocks starting at `offset`.  Throws mpp::Error when the
// buffer is short.
WeekSpec decode_day_blocks(std::vector<std::uint8_t> const& data, std::size_t offset);

// One calendar data block (Var2Data key 0x0D400008).  Throws mpp::Error when
// the block is shorter than the 420-byte week area.
CalendarData decode_calendar_data(std::vector<std::uint8_t> const& data);

// 114/TBkndCal (+ 114/Props for the default week and default calendar name) of
// an already-open compound file.  Throws mpp::Error when the table is absent or
// internally impossible (e.g. FixedData is not 64 + 12*n bytes).
CalendarTable decode_compound_file(mpp::CompoundFile const& file);

// Same, opening the file first.
CalendarTable decode_file(std::string const& mpp_path);

// MS Project's built-in default working week: Mon-Fri 08:00-12:00 + 13:00-17:00,
// weekend non-working.  Used only when 114/Props has no DEFAULT_CALENDAR_HOURS
// record; every file on disk carries that record and it holds exactly this week
// (measured), so this is a fallback, not a claim about the format.
WeekSpec stock_default_week();

// --- builder ----------------------------------------------------------------

// Convert one resolved day.  A DaySpec with use_base set becomes an empty
// pattern: the caller must have resolved it against its parent first.
// Ticks are converted to the minutes core::sched::DayPattern stores.
core::sched::DayPattern to_day_pattern(DaySpec const& day);

CalendarDefinition const* find_calendar(CalendarTable const& table, std::string_view name);
CalendarDefinition const* find_calendar_by_uid(CalendarTable const& table, std::int32_t uid);

// Build a core::sched::Calendar for one decoded calendar:
//   - the default week is the calendar's own seven resolved day patterns;
//   - every dated exception (and dated work week) is merged into the
//     core::sched::Week of the week it falls in and installed with
//     Calendar::set_work_week, because core::sched has no dated-exception API.
//     Two exceptions in one week merge into that week; exceptions win over a
//     dated work week; a child calendar's exceptions win over its base's.
// Recurring exceptions (PjExceptionType 2..7) are not applied.
core::sched::Calendar build_calendar(CalendarTable const& table,
                                     CalendarDefinition const& calendar);

// Name resolution: an empty `calendar_name` means "the project's default
// calendar" (114/Props DEFAULT_CALENDAR_NAME).  Throws mpp::Error when the name
// is not in the table; when the default name is missing and the file has
// exactly one base calendar that calendar is used instead.
core::sched::Calendar build_project_calendar(CalendarTable const& table,
                                             std::string const& calendar_name = {});

// Convenience: decode + build in one call.  `calendar_name` empty = project
// default.  Throws mpp::Error for an unreadable file or a missing calendar.
core::sched::Calendar load_project_calendar(std::string const& mpp_path,
                                            std::string const& calendar_name = {});
core::sched::Calendar load_project_calendar(mpp::CompoundFile const& file,
                                            std::string const& calendar_name = {});

}  // namespace core::cal
