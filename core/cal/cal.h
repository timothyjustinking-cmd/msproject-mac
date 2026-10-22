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
// plain dated range (measured: every exception in every file on disk before wave
// 12 is 1 -- "daily" over the range IS the whole range).  Types 2..7 are
// genuinely recurring.
//
// --- recurrence parameters (MEASURED, wave 12) ------------------------------
// The 92-byte record keeps its recurrence parameters in the 7 u16 words
// +74..+86 (raw_recurrence_words).  corpus/probe/w12-exceptions.mpp carries one
// exception of every PjExceptionType on the project calendar, and
// docs/analysis/oracle/w12-exceptions.json holds the COM read-back of the same
// seven records; both agree with mpxj's AbstractCalendarAndExceptionFactory
// (readRecurringData), which is the cross-check, not the source.  Per type, with
// the fixture value in brackets:
//
//   type 1  daily, every day        : no parameter word (+76 reads 0)
//   type 2  yearly, month + day     : +76 low byte = Month - 1  [2 -> March]
//                                     +77 byte     = MonthDay    [15]
//   type 3  yearly, nth weekday     : +76 low byte = Month - 1  [10 -> November]
//                                     +77 byte     = MonthPosition [4 = last]
//                                     +78 low byte = MonthItem     [8 = Friday]
//   type 4  monthly, day            : +76 low byte = MonthDay      [15]
//                                     +78 low byte = Period        [1]
//   type 5  monthly, nth weekday    : +76 low byte = MonthPosition [0 = first]
//                                     +77 byte     = MonthItem     [0 here]
//                                     +78 u16      = Period        [1]
//   type 6  weekly                  : +76 low byte = DaysOfWeek bitmap [10 =
//                                     Monday|Wednesday]; +78 u16 = Period [1]
//   type 7  every N days            : +76 u16      = Period        [1]
//
// `MonthItem` is PjMonthItem: 3 = Sunday .. 9 = Saturday (see
// weekday_of_month_item).  `MonthPosition` is PjMonthPosition: 0 = first,
// 1..3 = second..fourth, 4 = last.  `DaysOfWeek` is a bitmap with bit 0 =
// Sunday .. bit 6 = Saturday (mpxj's DAY_MASKS).
//
// A MONTHLY positional record has two measured shapes (wave 12, both fixtures):
//   * MonthItem and MonthPosition are STORED when they differ from the start day.
//     corpus/probe/w12-exceptions-monthpos.mpp ("ThirdMonday", COM JSON
//     w12-monthpos.json) is a type-5 record with Start 2026-11-01 (a Sunday),
//     +76 = 2 (third), +77 = 4 (Monday), +78 = 1, Occurrences 8 -- the weekday
//     and the position are separate words, and the expansion is the third Monday
//     of each month in range.
//   * Both are 0 when the start day already is the rule (the first fixture's
//     "FirstMonday": Start 2026-11-02 is itself the first Monday, and COM reads
//     MonthItem 0 / MonthPosition 0 back).  The expansion then derives the
//     weekday and the position from the start day, which is by definition a
//     matching instance.
// The +4 occurrence word is Project's own count for the range: every one of the
// seven recurring records across the two fixtures agrees with the expansion
// (29/18/7/5/4/5/4 and 8), so it is kept as a consistency CHECK, never as a
// source of truth.
struct DatedException {
    std::int32_t start_day = 0;
    std::int32_t end_day = 0;
    bool working = false;
    std::vector<WorkInterval> intervals;
    std::int32_t recurrence_type = 0;
    // +4, PjExceptionType's occurrence count.  Always recomputed by Project from
    // the range and the parameters (the fixture asks for 0 and reads back 29),
    // so it is used only as a consistency check on an expansion.
    std::int32_t occurrences = 0;
    // Decoded recurrence parameters (see the block comment above): every one is
    // read back from the record.  0 means "this type does not store it"; the one
    // substitution the expansion makes for a 0 is a monthly-positional MonthItem,
    // where the weekday comes from the start day (see above).
    std::int32_t period = 0;          // every N weeks / months / days
    std::int32_t days_of_week = 0;    // weekly bitmap, bit 0 = Sunday
    std::int32_t month = 0;           // 1..12 (stored as month - 1)
    std::int32_t month_day = 0;       // 1..31
    std::int32_t month_item = 0;      // PjMonthItem 3..9, 0 = not stored
    std::int32_t month_position = 0;  // PjMonthPosition 0..4 (0 = first)
    // The seven raw u16 words at +74..+86, exactly as stored, for diagnostics.
    // [0] = +74, [1] = +76, [2] = +78, [3] = +80 (a per-record ordinal, not a
    // parameter: it matched the creation order of all seven records), [4] = +82,
    // [5] = +84, [6] = +86.
    std::array<std::uint16_t, 7> raw_recurrence_words{};
};

// PjMonthItem boundaries: the stored value of the first weekday (Sunday) and of
// the last (Saturday).  3 -> weekday 0 (Sunday), 9 -> weekday 6 (Saturday).
inline constexpr std::int32_t kMonthItemSunday = 3;
inline constexpr std::int32_t kMonthItemSaturday = 9;
// PjMonthPosition: 4 is "the last such weekday", 0..3 are the first..fourth.
inline constexpr std::int32_t kMonthPositionLast = 4;

// core::sched weekday (0 = Sunday) for a stored PjMonthItem, or -1 when the value
// is not a weekday (0 = "not stored", 1/2 = PjMonthItem's day/weekend-day kinds).
std::int32_t weekday_of_month_item(std::int32_t month_item) noexcept;

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

// --- recurring exceptions ---------------------------------------------------

// Cap on the number of concrete days one recurring exception may expand into.
// Ten years of every-day occurrences; a recurrence that needs more is almost
// certainly a mis-decoded range, and refusing is better than installing 60,000
// week overrides.
inline constexpr std::int32_t kMaxExpandedExceptionDays = 3660;

// One recurring exception expanded into the concrete days it makes non-working
// (or specially working).
struct RecurrenceExpansion {
    std::vector<std::int32_t> days;  // ascending, distinct
    bool applied = false;            // `days` is usable
    const char* why = "";            // stable short reason when !applied
};

// Expand one PjExceptionType 2..7 record into the days it covers.
//
// MEASURED (wave 12, corpus/probe/w12-exceptions.mpp + the oracle JSON): the
// 92-byte record carries the recurrence RANGE (start/end day at +0/+2), the
// parameter words at +74..+86 (see DatedException), the interval list, the type
// (+72) and the occurrence count (+4).  Each type expands as Project's stored
// parameters say:
//   2 yearly month+day    -> that (month, day) every year
//   3 yearly positional   -> the position-th weekday of that month every year
//   4 monthly month+day   -> that day-of-month every `period` months
//   5 monthly positional  -> the position-th weekday of every `period`th month
//   6 weekly              -> every `period`th week, on every weekday in the mask
//   7 day-count           -> every `period` days from the start day
// A parameter the type does not store (0) falls back to the range's first day,
// which is by definition a matching instance: the weekday for types 3/5/6, the
// day-of-month for 2/4, the month for 2/3, and the period to 1.  Position is read
// as stored: 0 is PjMonthPosition's "first" (mpxj reads the same byte as
// dayNumber = value + 1).  When a monthly-positional record stores both words (the
// measured non-zero case, w12-exceptions-monthpos.mpp) they are used exactly as
// stored; when its MonthItem is 0 only the weekday is taken from the start day.
// Every loop is bounded by the day range itself (a 16-bit day word), and the day
// cap turns an over-long expansion into a refusal rather than a truncation.
// The stored occurrence word (+4) is a check, never a source of truth: when it
// is non-zero and disagrees with the expanded count, the expansion is refused.
// The fixtures validate the parameters exactly this way: all seven recurring
// records across the two files agree with Project's own count, including
// 2029-11-30 as the fourth "last Friday of November" (2030-11-29 is past the
// range) and the third Monday of each month for the 2026-11-01..2027-06-30
// positional record (8 occurrences).
RecurrenceExpansion expand_recurring_exception(
    DatedException const& exception, std::int32_t max_days = kMaxExpandedExceptionDays);

// Counters for one build_calendar() call; ask for one when the caller reports.
struct RecurrenceReport {
    std::size_t applied = 0;      // recurring exceptions expanded and installed
    std::size_t not_applied = 0;  // recurring exceptions left out
};

CalendarDefinition const* find_calendar(CalendarTable const& table, std::string_view name);
CalendarDefinition const* find_calendar_by_uid(CalendarTable const& table, std::int32_t uid);

// Build a core::sched::Calendar for one decoded calendar:
//   - the default week is the calendar's own seven resolved day patterns;
//   - every dated exception (and dated work week) is merged into the
//     core::sched::Week of the week it falls in and installed with
//     Calendar::set_work_week, because core::sched has no dated-exception API.
//     Two exceptions in one week merge into that week; exceptions win over a
//     dated work week; a child calendar's exceptions win over its base's.
//   - a recurring exception (PjExceptionType 2..7) is expanded through
//     expand_recurring_exception() and then applied exactly like a plain range;
//     one that cannot be expanded (an occurrence-count disagreement, a cap
//     overflow, a parameter that is not a weekday) is counted in
//     CalendarTable::recurring_exceptions and skipped, never guessed.  `report`,
//     when given, counts both outcomes.
core::sched::Calendar build_calendar(CalendarTable const& table,
                                     CalendarDefinition const& calendar,
                                     RecurrenceReport* report = nullptr);

// Name resolution: an empty `calendar_name` means "the project's default
// calendar" (114/Props DEFAULT_CALENDAR_NAME).  Throws mpp::Error when the name
// is not in the table; when the default name is missing and the file has
// exactly one base calendar that calendar is used instead.
core::sched::Calendar build_project_calendar(CalendarTable const& table,
                                             std::string const& calendar_name = {},
                                             RecurrenceReport* report = nullptr);

// Convenience: decode + build in one call.  `calendar_name` empty = project
// default.  Throws mpp::Error for an unreadable file or a missing calendar.
core::sched::Calendar load_project_calendar(std::string const& mpp_path,
                                            std::string const& calendar_name = {});
core::sched::Calendar load_project_calendar(mpp::CompoundFile const& file,
                                            std::string const& calendar_name = {});

}  // namespace core::cal
