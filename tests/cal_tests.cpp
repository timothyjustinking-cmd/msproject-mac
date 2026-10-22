// tests/cal_tests.cpp -- 114/TBkndCal: day patterns, dated exceptions, work
// weeks, the CalendarTable builder, and the three MS Project dates of
// corpus/probe/holiday1.mpp.
//
// Ground truth for the corpus case is docs/analysis/oracle/holiday-probe.json
// (Project's own calculation, synthetic fixture):
//
//   T1 2d from Mon 2026-10-19 08:00 -> Tue 2026-10-20 17:00
//   T2 5d FS after T1                -> Thu 2026-10-22 08:00 .. Thu 2026-10-29 12:00
//   T3 1d from Fri 2026-10-23 08:00 -> Mon 2026-10-26 12:00
//
// The synthetic calendar ("SiteCal", base Standard) has Friday working
// 08:00-12:00 and one non-working dated exception on Wed 2026-10-21.  Both names
// are synthetic: the coordinator built holiday1.mpp over COM, so no client data
// is in this file.  Nothing here prints a name from corpus/project_a.mpp.
//
// CI-safe cases (no corpus needed): decode_calendar_data() on hand-built byte
// blocks and the builder on a hand-built CalendarTable.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "cal.h"
#include "sched_test_support.h"

namespace cal = core::cal;
namespace sched = core::sched;

namespace {

// --- day numbers around the week of Monday 2026-10-19 (day 1 = 1984-01-01) ---
constexpr std::int32_t kMon = 15633;      // 2026-10-19
constexpr std::int32_t kTue = 15634;      // 2026-10-20
constexpr std::int32_t kWed = 15635;      // 2026-10-21, the exception
constexpr std::int32_t kThu = 15636;      // 2026-10-22
constexpr std::int32_t kFri = 15637;      // 2026-10-23, 4 working hours
constexpr std::int32_t kSat = 15638;      // 2026-10-24
constexpr std::int32_t kSun = 15639;      // 2026-10-25
constexpr std::int32_t kNextMon = 15640;  // 2026-10-26
constexpr std::int32_t kNextThu = 15643;  // 2026-10-29

constexpr sched::ticks kDay8h = 4800;   // 8 working hours in tenths of a minute
constexpr sched::ticks kDay4h = 2400;   // the short Friday

sched::date32 at(std::int32_t day, std::int32_t hour, std::int32_t minute) {
    return sched::pack(day, (hour * 60 + minute) * sched::kTenthsPerMinute);
}

std::string join_path(std::string const& directory, std::string const& name) {
    if (directory.empty()) {
        return name;
    }
    const char last = directory.back();
    if (last == '/' || last == '\\') {
        return directory + name;
    }
    return directory + "/" + name;
}

// Same convention as tests/mpp_reader_tests.cpp: any non-empty CAL_NO_CORPUS
// makes the corpus-gated cases behave exactly as they do when corpus/ is absent,
// so the CI path can be exercised on a machine that has the corpus.
std::string environment_value(char const* name) {
    // std::getenv is ISO C++, but MSVC's CRT deprecates it (C4996, fatal under
    // /WX).  _dupenv_s is the sanctioned replacement and owns a malloc buffer.
#ifdef _MSC_VER
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr) {
        return std::string();
    }
    const std::string value(buffer);
    std::free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
#endif
}

bool corpus_hidden() { return !environment_value("CAL_NO_CORPUS").empty(); }

bool file_exists(std::string const& path) {
    if (corpus_hidden()) {
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    return input.good();
}

// --- synthetic byte blocks --------------------------------------------------

void set_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
}

void set_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
    }
}

void append_u16(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu));
    }
}

// One 60-byte day block.  `intervals` are [start minute, end minute) pairs.
std::vector<std::uint8_t> make_day_block(
    bool use_base, std::initializer_list<std::pair<std::int32_t, std::int32_t>> intervals) {
    std::vector<std::uint8_t> block(cal::kDayBlockBytes, 0);
    if (use_base) {
        set_u16(block, 0, 1);
        return block;
    }
    std::size_t index = 0;
    std::uint32_t total = 0;
    for (std::pair<std::int32_t, std::int32_t> const& interval : intervals) {
        if (index >= 8) {
            break;
        }
        const std::uint32_t start_ticks = static_cast<std::uint32_t>(interval.first) * 10u;
        const std::uint32_t length_ticks =
            static_cast<std::uint32_t>(interval.second - interval.first) * 10u;
        set_u16(block, 8 + index * 2, start_ticks);
        set_u32(block, 20 + index * 4, length_ticks);
        total += length_ticks;
        ++index;
    }
    set_u16(block, 2, static_cast<std::uint32_t>(index));
    set_u32(block, 4, total);
    return block;
}

// Monday-Friday 09:00-17:00, weekend custom non-working.
std::vector<std::uint8_t> make_five_day_week() {
    const std::vector<std::uint8_t> working =
        make_day_block(false, {{9 * 60, 17 * 60}});
    const std::vector<std::uint8_t> off = make_day_block(false, {});
    std::vector<std::uint8_t> blob;
    for (std::size_t day = 0; day < 7; ++day) {
        const std::vector<std::uint8_t>& block = (day == 0 || day == 6) ? off : working;
        blob.insert(blob.end(), block.begin(), block.end());
    }
    return blob;
}

// 92-byte exception block followed by a zero name length (4-byte aligned = 0).
void append_exception(std::vector<std::uint8_t>& blob, std::int32_t start_day,
                      std::int32_t end_day,
                      std::initializer_list<std::pair<std::int32_t, std::int32_t>> intervals,
                      std::int32_t recurrence_type, std::int32_t occurrences = 0) {
    std::vector<std::uint8_t> block(92, 0);
    set_u16(block, 0, static_cast<std::uint32_t>(start_day));
    set_u16(block, 2, static_cast<std::uint32_t>(end_day));
    set_u16(block, 4, static_cast<std::uint32_t>(occurrences));
    set_u16(block, 72, static_cast<std::uint32_t>(recurrence_type));
    std::size_t index = 0;
    for (std::pair<std::int32_t, std::int32_t> const& interval : intervals) {
        if (index >= 8) {
            break;
        }
        set_u16(block, 20 + index * 2, static_cast<std::uint32_t>(interval.first) * 10u);
        set_u32(block, 32 + index * 4,
                static_cast<std::uint32_t>(interval.second - interval.first) * 10u);
        ++index;
    }
    set_u16(block, 14, static_cast<std::uint32_t>(index));
    set_u32(block, 88, 0);  // exception name length: skipped by the decoder
    blob.insert(blob.end(), block.begin(), block.end());
}

// One recurring exception record with its recurrence parameter words, encoded
// the way corpus/probe/w12-exceptions.mpp stores them (cal.h documents the
// offset-by-type table).  `type` selects which words are written, so a test can
// only ask for parameters that the type actually carries.
struct RecurringSpec {
    std::int32_t type = 0;
    std::int32_t start_day = 0;
    std::int32_t end_day = 0;
    std::int32_t occurrences = 0;
    std::int32_t period = 0;
    std::int32_t days_of_week = 0;
    std::int32_t month = 0;  // 1..12; stored as month - 1
    std::int32_t month_day = 0;
    std::int32_t month_item = 0;
    std::int32_t month_position = 0;
};

void append_recurring_exception(std::vector<std::uint8_t>& blob, RecurringSpec const& spec) {
    std::vector<std::uint8_t> block(92, 0);
    set_u16(block, 0, static_cast<std::uint32_t>(spec.start_day));
    set_u16(block, 2, static_cast<std::uint32_t>(spec.end_day));
    set_u16(block, 4, static_cast<std::uint32_t>(spec.occurrences));
    set_u16(block, 72, static_cast<std::uint32_t>(spec.type));
    const std::uint32_t month_byte =
        spec.month > 0 ? static_cast<std::uint32_t>(spec.month - 1) : 0u;
    switch (spec.type) {
        case 2:  // +76 low = month - 1, +77 = MonthDay
            set_u16(block, 76, month_byte | (static_cast<std::uint32_t>(spec.month_day) << 8));
            break;
        case 3:  // +76 low = month - 1, +77 = MonthPosition, +78 = MonthItem
            set_u16(block, 76,
                    month_byte | (static_cast<std::uint32_t>(spec.month_position) << 8));
            set_u16(block, 78, static_cast<std::uint32_t>(spec.month_item));
            break;
        case 4:  // +76 = MonthDay, +78 low = Period
            set_u16(block, 76, static_cast<std::uint32_t>(spec.month_day));
            set_u16(block, 78, static_cast<std::uint32_t>(spec.period));
            break;
        case 5:  // +76 = MonthPosition, +77 = MonthItem, +78 = Period
            set_u16(block, 76,
                    static_cast<std::uint32_t>(spec.month_position) |
                        (static_cast<std::uint32_t>(spec.month_item) << 8));
            set_u16(block, 78, static_cast<std::uint32_t>(spec.period));
            break;
        case 6:  // +76 = DaysOfWeek bitmap, +78 = Period
            set_u16(block, 76, static_cast<std::uint32_t>(spec.days_of_week));
            set_u16(block, 78, static_cast<std::uint32_t>(spec.period));
            break;
        case 7:  // +76 = Period
            set_u16(block, 76, static_cast<std::uint32_t>(spec.period));
            break;
        default:
            break;
    }
    set_u32(block, 88, 0);  // exception name length: skipped by the decoder
    blob.insert(blob.end(), block.begin(), block.end());
}

cal::DatedException exception_of(RecurringSpec const& spec) {
    cal::DatedException exception;
    exception.start_day = spec.start_day;
    exception.end_day = spec.end_day;
    exception.recurrence_type = spec.type;
    exception.occurrences = spec.occurrences;
    exception.period = spec.period;
    exception.days_of_week = spec.days_of_week;
    exception.month = spec.month;
    exception.month_day = spec.month_day;
    exception.month_item = spec.month_item;
    exception.month_position = spec.month_position;
    return exception;
}

cal::RecurrenceExpansion expand_spec(RecurringSpec const& spec) {
    return cal::expand_recurring_exception(exception_of(spec));
}

// --- synthetic table --------------------------------------------------------

cal::CalendarTable make_synthetic_table() {
    cal::CalendarTable table;
    table.default_week = cal::stock_default_week();
    table.default_calendar_name = "Synthetic";
    cal::CalendarDefinition calendar;
    calendar.uid = 1;
    calendar.base_uid = -1;
    calendar.resource_uid = -1;
    calendar.name = "Synthetic";
    calendar.has_week_data = false;
    cal::DatedException holiday;
    holiday.start_day = kWed;
    holiday.end_day = kWed;
    holiday.recurrence_type = 1;
    cal::DatedException saturday;
    saturday.start_day = kSat;
    saturday.end_day = kSat;
    saturday.working = true;
    saturday.recurrence_type = 1;
    saturday.intervals.push_back(cal::WorkInterval{9 * 600, 8 * 600});  // 09:00-17:00
    calendar.exceptions.push_back(holiday);
    calendar.exceptions.push_back(saturday);
    table.calendars.push_back(calendar);
    return table;
}

// A table with one calendar carrying exactly one exception, for the recurrence
// tests.  `type` is the stored PjExceptionType and `occurrences` the +4 word.
cal::CalendarTable make_recurring_table(std::int32_t type, std::int32_t start_day,
                                        std::int32_t end_day, std::int32_t occurrences) {
    cal::CalendarTable table;
    table.default_week = cal::stock_default_week();
    table.default_calendar_name = "Recurring";
    cal::CalendarDefinition calendar;
    calendar.uid = 1;
    calendar.base_uid = -1;
    calendar.resource_uid = -1;
    calendar.name = "Recurring";
    calendar.has_week_data = false;
    cal::DatedException exception;
    exception.start_day = start_day;
    exception.end_day = end_day;
    exception.recurrence_type = type;
    exception.occurrences = occurrences;
    calendar.exceptions.push_back(exception);
    table.calendars.push_back(calendar);
    if (type > 1) {
        ++table.recurring_exceptions;
    }
    return table;
}

cal::RecurrenceExpansion expand(std::int32_t type, std::int32_t start_day, std::int32_t end_day,
                                std::int32_t occurrences = 0) {
    cal::DatedException exception;
    exception.start_day = start_day;
    exception.end_day = end_day;
    exception.recurrence_type = type;
    exception.occurrences = occurrences;
    return cal::expand_recurring_exception(exception);
}

// --- recurring exceptions ([I] parameters: see cal.h) -----------------------
//
// The record carries the range and the type; the recurrence PARAMETERS are not
// decoded, so the expansion derives the rule from the range's first day and
// assumes the shortest period.  These tests pin the derived rule, the refusals,
// and the occurrence-word check -- NOT a measurement of Project (no file on disk
// has a recurring exception; the fixture that would settle it is named in
// memory/cal.md).
void test_recurrence(sched_test::TestContext& ctx) {
    // PjExceptionType 6 = weekly: "every Monday", range Mon 2026-10-19 (day
    // 15633) .. Mon 2026-11-02 (15647) -> three Mondays.
    {
        const cal::RecurrenceExpansion weekly = expand(6, 15633, 15647);
        ctx.check(weekly.applied, "recurrence weekly: expands");
        ctx.equal(weekly.days.size(), std::size_t{3}, "recurrence weekly: three Mondays");
        if (weekly.days.size() == 3) {
            ctx.equal(weekly.days[0], std::int32_t{15633}, "recurrence weekly: first Monday");
            ctx.equal(weekly.days[1], std::int32_t{15640}, "recurrence weekly: one week later");
            ctx.equal(weekly.days[2], std::int32_t{15647}, "recurrence weekly: two weeks later");
        }
        // The +4 occurrence word agreeing is accepted...
        const cal::RecurrenceExpansion check_ok = expand(6, 15633, 15647, 3);
        ctx.check(check_ok.applied, "recurrence weekly: an agreeing occurrence word is applied");
        // ...and disagreeing refuses the expansion instead of guessing a period.
        const cal::RecurrenceExpansion mismatch = expand(6, 15633, 15647, 4);
        ctx.check(!mismatch.applied, "recurrence weekly: a disagreeing occurrence word refuses");
        ctx.check(mismatch.days.empty(), "recurrence weekly: a refused expansion has no days");
        ctx.check(mismatch.why[0] != '\0', "recurrence weekly: the refusal carries a reason");
    }

    // PjExceptionType 2 = yearly, month + day-of-month: "every 21 October",
    // 2026..2028 (2028 is a leap year, so the second gap is 366 days).
    {
        const cal::RecurrenceExpansion yearly = expand(2, 15635, 16366);
        ctx.check(yearly.applied, "recurrence yearly: expands");
        ctx.equal(yearly.days.size(), std::size_t{3}, "recurrence yearly: three years");
        if (yearly.days.size() == 3) {
            ctx.equal(yearly.days[0], std::int32_t{15635}, "recurrence yearly: 2026-10-21");
            ctx.equal(yearly.days[1], std::int32_t{16000}, "recurrence yearly: 2027-10-21");
            ctx.equal(yearly.days[2], std::int32_t{16366}, "recurrence yearly: 2028-10-21");
        }
    }

    // PjExceptionType 4 = monthly, day-of-month: the 15th, Oct 2026 .. Jan 2027.
    {
        const cal::RecurrenceExpansion monthly = expand(4, 15629, 15721);
        ctx.check(monthly.applied, "recurrence monthly: expands");
        ctx.equal(monthly.days.size(), std::size_t{4}, "recurrence monthly: four months");
        if (monthly.days.size() == 4) {
            ctx.equal(monthly.days[0], std::int32_t{15629}, "recurrence monthly: 2026-10-15");
            ctx.equal(monthly.days[3], std::int32_t{15721}, "recurrence monthly: 2027-01-15");
        }
    }

    // PjExceptionType 5 = monthly positional: the second Tuesday, Oct 2026 ..
    // Jan 2027 (2026-10-13, 11-10, 12-08, 2027-01-12).  The position comes from
    // the record (+76 = 1 for "second"); the weekday is not stored for monthly
    // positional (the measured gap documented in cal.h), so 0 means "take the
    // start day's".
    {
        RecurringSpec spec;
        spec.type = 5;
        spec.start_day = 15627;
        spec.end_day = 15718;
        spec.month_position = 1;
        const cal::RecurrenceExpansion second_tuesday = expand_spec(spec);
        ctx.check(second_tuesday.applied, "recurrence monthly positional: expands");
        ctx.equal(second_tuesday.days.size(), std::size_t{4},
                  "recurrence monthly positional: four second Tuesdays");
        if (second_tuesday.days.size() == 4) {
            ctx.equal(second_tuesday.days[0], std::int32_t{15627},
                      "recurrence monthly positional: 2026-10-13");
            ctx.equal(second_tuesday.days[1], std::int32_t{15655},
                      "recurrence monthly positional: 2026-11-10");
            ctx.equal(second_tuesday.days[3], std::int32_t{15718},
                      "recurrence monthly positional: 2027-01-12");
        }
        // The same range with the stored position 0 is the FIRST Tuesday, and
        // October's first Tuesday (10-06) is before the range, so there are only
        // three.  A record whose +4 word said 4 would then refuse, which is the
        // check that exposes a misread position.
        spec.month_position = 0;
        spec.occurrences = 0;
        const cal::RecurrenceExpansion first_tuesday = expand_spec(spec);
        ctx.check(first_tuesday.applied, "recurrence monthly positional: position 0 expands");
        ctx.equal(first_tuesday.days.size(), std::size_t{3},
                  "recurrence monthly positional: three first Tuesdays in the range");
        if (first_tuesday.days.size() == 3) {
            ctx.equal(first_tuesday.days[0], sched::day_of(sched::from_ymd(2026, 11, 3, 0, 0)),
                      "recurrence monthly positional: first Tuesday in range is 2026-11-03");
            ctx.equal(first_tuesday.days[2], sched::day_of(sched::from_ymd(2027, 1, 5, 0, 0)),
                      "recurrence monthly positional: last is 2027-01-05");
        }
        spec.occurrences = 3;
        ctx.check(expand_spec(spec).applied,
                  "recurrence monthly positional: the occurrence word agrees with position 0");
        spec.occurrences = 4;
        ctx.check(!expand_spec(spec).applied,
                  "recurrence monthly positional: a second-Tuesday count refuses position 0");
    }

    // PjExceptionType 3 = yearly positional: the first Monday of November,
    // 2026 (11-02 = 15647) .. 2028 (11-06 = 16382).
    {
        const cal::RecurrenceExpansion first_monday = expand(3, 15647, 16382);
        ctx.check(first_monday.applied, "recurrence yearly positional: expands");
        ctx.equal(first_monday.days.size(), std::size_t{3},
                  "recurrence yearly positional: three years");
        if (first_monday.days.size() == 3) {
            ctx.equal(first_monday.days[0], std::int32_t{15647},
                      "recurrence yearly positional: 2026-11-02");
            ctx.equal(first_monday.days[1], std::int32_t{16011},
                      "recurrence yearly positional: 2027-11-01");
            ctx.equal(first_monday.days[2], std::int32_t{16382},
                      "recurrence yearly positional: 2028-11-06");
        }
    }

    // PjExceptionType 7 = "every N days".  The period is the +76 word; a record
    // with 0 there (a type-7 record always stores at least 1, measured) falls
    // back to every day, which is the same day set as a plain range.
    {
        const cal::RecurrenceExpansion daily = expand(7, 15633, 15647);
        ctx.check(daily.applied, "recurrence day-count: expands with an absent period");
        ctx.equal(daily.days.size(), std::size_t{15},
                  "recurrence day-count: period 0 means every day over the range");

        RecurringSpec spec;
        spec.type = 7;
        spec.start_day = 15633;
        spec.end_day = 15647;
        spec.period = 3;
        spec.occurrences = 5;
        const cal::RecurrenceExpansion every_three = expand_spec(spec);
        ctx.check(every_three.applied, "recurrence day-count: every 3 days expands");
        ctx.equal(every_three.days.size(), std::size_t{5},
                  "recurrence day-count: five occurrences over 15 days at period 3");
        if (every_three.days.size() == 5) {
            ctx.equal(every_three.days[0], std::int32_t{15633},
                      "recurrence day-count: first day is the range start");
            ctx.equal(every_three.days[1], std::int32_t{15636},
                      "recurrence day-count: second day is three days later");
            ctx.equal(every_three.days[4], std::int32_t{15645},
                      "recurrence day-count: last day is the start + 12");
        }
        spec.occurrences = 6;
        ctx.check(!expand_spec(spec).applied,
                  "recurrence day-count: a disagreeing occurrence word refuses");
    }

    // A plain range is not a recurring exception, and an out-of-range type is
    // refused rather than treated as one.
    {
        ctx.check(!expand(1, 15633, 15647).applied,
                  "recurrence: PjExceptionType 1 is not recurring");
        ctx.check(!expand(0, 15633, 15647).applied,
                  "recurrence: PjExceptionType 0 is not recurring");
        ctx.check(!expand(9, 15633, 15647).applied,
                  "recurrence: a type outside 0..7 is refused");
    }

    // The day cap is a refusal, not a silent truncation.
    {
        cal::DatedException weekly;
        weekly.start_day = 15633;
        weekly.end_day = 15633 + 7 * 400;  // 401 Mondays
        weekly.recurrence_type = 6;
        const cal::RecurrenceExpansion capped = cal::expand_recurring_exception(weekly, 300);
        ctx.check(!capped.applied, "recurrence: a cap smaller than the expansion refuses");
        ctx.check(capped.days.empty(), "recurrence: a capped expansion installs nothing");
    }

    // --- builder: a recurring exception is really applied -------------------
    {
        // Every Monday for three weeks, non-working.
        const cal::CalendarTable table = make_recurring_table(6, kMon, kMon + 14, 0);
        cal::RecurrenceReport report;
        const sched::Calendar calendar = cal::build_project_calendar(table, "", &report);
        ctx.equal(report.applied, std::size_t{1},
                  "recurrence builder: one recurring exception applied");
        ctx.equal(report.not_applied, std::size_t{0}, "recurrence builder: nothing refused");
        ctx.equal(table.recurring_exceptions, std::size_t{1},
                  "recurrence builder: the table counts it as recurring");
        ctx.equal(calendar.day_total_ticks(kMon), 0, "recurrence builder: the first Monday is free");
        ctx.equal(calendar.day_total_ticks(kNextMon), 0,
                  "recurrence builder: the second Monday is free");
        ctx.equal(calendar.day_total_ticks(kTue), 4800,
                  "recurrence builder: the Tuesday between them still works");
        // 3 d from Monday 08:00 must skip the two Mondays in the way: Tue+Wed
        // (2 d), Thu+Fri (3 d).
        ctx.equal_date(calendar.add_work(at(kMon, 8, 0), 3 * 4800), at(kThu, 17, 0),
                       "recurrence builder: 3 d from Mon 08:00 ends Thu 17:00");
    }

    // A type-7 exception (every N days) is applied: with no period word it is
    // every day, so the whole 15-day range becomes non-working.
    {
        const cal::CalendarTable table = make_recurring_table(7, kMon, kMon + 14, 0);
        cal::RecurrenceReport report;
        const sched::Calendar calendar = cal::build_project_calendar(table, "", &report);
        ctx.equal(report.applied, std::size_t{1}, "recurrence builder type 7: applied");
        ctx.equal(report.not_applied, std::size_t{0}, "recurrence builder type 7: nothing refused");
        ctx.equal(calendar.day_total_ticks(kMon), 0,
                  "recurrence builder type 7: the first day is free");
        ctx.equal(calendar.day_total_ticks(kMon + 14), 0,
                  "recurrence builder type 7: the last day is free");
        ctx.equal(calendar.day_total_ticks(kMon + 15), 4800,
                  "recurrence builder type 7: the day after the range still works");
    }

    // A weekly exception whose occurrence word disagrees with the range is
    // counted, not applied.
    {
        const cal::CalendarTable table = make_recurring_table(6, kMon, kMon + 14, 5);
        cal::RecurrenceReport report;
        const sched::Calendar calendar = cal::build_project_calendar(table, "", &report);
        ctx.equal(report.not_applied, std::size_t{1},
                  "recurrence builder: an inconsistent occurrence word is counted");
        ctx.equal(calendar.day_total_ticks(kMon), 4800,
                  "recurrence builder: an inconsistent occurrence word changes nothing");
    }
}

// --- recurrence parameters: decode + expansion (CI-safe) --------------------
//
// The seven u16 words at +74..+86 are measured on corpus/probe/w12-exceptions.mpp
// (cal.h has the offset-by-type table).  These cases are hand-built from those
// offsets, so they run without the corpus; the corpus-gated case below pins the
// same values against Project's own file.
std::int32_t day_number(std::int32_t year, std::int32_t month, std::int32_t day) {
    return sched::day_of(sched::from_ymd(year, month, day, 0, 0));
}

void test_recurrence_parameters(sched_test::TestContext& ctx) {
    // The record encodes parameters in the word at +76 (low/high byte) and the
    // word at +78; walking one exception of every type through the decoder and
    // then the expansion checks both halves of the round trip.
    {
        struct Case {
            RecurringSpec spec;
            std::size_t expanded;
        };
        std::vector<Case> cases;
        RecurringSpec daily;
        daily.type = 1;
        daily.start_day = 15647;
        daily.end_day = 15675;
        daily.occurrences = 29;
        cases.push_back({daily,
                         29});  // type 1 is a plain range, expanded by the builder

        RecurringSpec weekly;
        weekly.type = 6;
        weekly.start_day = 15647;
        weekly.end_day = 15706;
        weekly.occurrences = 18;
        weekly.period = 1;
        weekly.days_of_week = 10;  // Monday | Wednesday
        cases.push_back({weekly, 18});

        RecurringSpec monthly_pos;
        monthly_pos.type = 5;
        monthly_pos.start_day = 15647;
        monthly_pos.end_day = 15858;
        monthly_pos.occurrences = 7;
        monthly_pos.period = 1;
        monthly_pos.month_position = 0;  // first
        monthly_pos.month_item = 4;      // Monday
        cases.push_back({monthly_pos, 7});

        RecurringSpec monthly_day;
        monthly_day.type = 4;
        monthly_day.start_day = 15660;
        monthly_day.end_day = 15780;
        monthly_day.occurrences = 5;
        monthly_day.period = 1;
        monthly_day.month_day = 15;
        cases.push_back({monthly_day, 5});

        RecurringSpec yearly_pos;
        yearly_pos.type = 3;
        yearly_pos.start_day = 15672;
        yearly_pos.end_day = 17133;
        yearly_pos.occurrences = 4;
        yearly_pos.month = 11;
        yearly_pos.month_position = 4;  // last
        yearly_pos.month_item = 8;      // Friday
        cases.push_back({yearly_pos, 4});

        RecurringSpec day_count;
        day_count.type = 7;
        day_count.start_day = 15696;
        day_count.end_day = 15700;
        day_count.occurrences = 5;
        day_count.period = 1;
        cases.push_back({day_count, 5});

        RecurringSpec yearly_day;
        yearly_day.type = 2;
        yearly_day.start_day = 15780;
        yearly_day.end_day = 16876;
        yearly_day.occurrences = 4;
        yearly_day.month = 3;
        yearly_day.month_day = 15;
        cases.push_back({yearly_day, 4});

        std::vector<std::uint8_t> blob = make_five_day_week();
        append_u16(blob, static_cast<std::uint32_t>(cases.size()));  // exception count
        append_u16(blob, 0);                                        // padding
        for (Case const& item : cases) {
            append_recurring_exception(blob, item.spec);
        }
        append_u32(blob, 0);  // empty work-week section
        const cal::CalendarData data = cal::decode_calendar_data(blob);
        ctx.check(!data.truncated, "parameter decode: the block decodes completely");
        ctx.equal(data.exceptions.size(), cases.size(), "parameter decode: seven exceptions");
        if (data.exceptions.size() != cases.size()) {
            return;
        }
        for (std::size_t i = 0; i < cases.size(); ++i) {
            const cal::DatedException& exception = data.exceptions[i];
            const RecurringSpec& spec = cases[i].spec;
            const std::string label = "parameter decode type " + std::to_string(spec.type);
            ctx.equal(exception.recurrence_type, spec.type, label + ": type");
            ctx.equal(exception.start_day, spec.start_day, label + ": start day");
            ctx.equal(exception.end_day, spec.end_day, label + ": end day");
            ctx.equal(exception.occurrences, spec.occurrences, label + ": occurrence word");
            ctx.equal(exception.period, spec.period, label + ": period");
            ctx.equal(exception.days_of_week, spec.days_of_week, label + ": days-of-week mask");
            ctx.equal(exception.month, spec.month, label + ": month");
            ctx.equal(exception.month_day, spec.month_day, label + ": month day");
            ctx.equal(exception.month_item, spec.month_item, label + ": month item");
            ctx.equal(exception.month_position, spec.month_position, label + ": month position");
            // +74 and +80 are not parameters; the words are kept verbatim.
            ctx.equal(exception.raw_recurrence_words[0], std::uint16_t{0},
                      label + ": the +74 word is kept raw");
            ctx.equal(exception.raw_recurrence_words[3], std::uint16_t{0},
                      label + ": the +80 word is kept raw");
            // The expansion of the decoded record must reproduce Project's own
            // occurrence count, which is what validates the parameter decode.
            if (spec.type == 1) {
                ctx.check(!cal::expand_recurring_exception(exception).applied,
                          label + ": a plain range is not expanded");
                continue;
            }
            const cal::RecurrenceExpansion expansion =
                cal::expand_recurring_exception(exception);
            ctx.check(expansion.applied, label + ": expands from the decoded parameters");
            ctx.equal(expansion.days.size(), cases[i].expanded, label + ": expanded count");
        }
    }

    // PjMonthItem is Sunday-first: 3 = Sunday, 8 = Friday, 9 = Saturday.
    {
        ctx.equal(cal::weekday_of_month_item(3), std::int32_t{0},
                  "MonthItem 3 is weekday 0 (Sunday)");
        ctx.equal(cal::weekday_of_month_item(4), std::int32_t{1},
                  "MonthItem 4 is weekday 1 (Monday)");
        ctx.equal(cal::weekday_of_month_item(8), std::int32_t{5},
                  "MonthItem 8 is weekday 5 (Friday)");
        ctx.equal(cal::weekday_of_month_item(9), std::int32_t{6},
                  "MonthItem 9 is weekday 6 (Saturday)");
        ctx.equal(cal::weekday_of_month_item(0), std::int32_t{-1},
                  "MonthItem 0 means 'not stored', not a weekday");
        ctx.equal(cal::weekday_of_month_item(1), std::int32_t{-1},
                  "MonthItem 1 (pjDay) is not a weekday");
        ctx.equal(cal::weekday_of_month_item(10), std::int32_t{-1},
                  "MonthItem 10 is outside the enum");
    }

    // A 29 February recurrence skips the three common years between two leap
    // years: 2024-02-29 .. 2028-02-29 is exactly two days.
    {
        RecurringSpec spec;
        spec.type = 2;
        spec.start_day = day_number(2024, 2, 29);
        spec.end_day = day_number(2028, 2, 29);
        spec.month = 2;
        spec.month_day = 29;
        spec.occurrences = 2;
        const cal::RecurrenceExpansion leap = expand_spec(spec);
        ctx.check(leap.applied, "leap day: a 29 February recurrence expands");
        ctx.equal(leap.days.size(), std::size_t{2}, "leap day: only the two leap years");
        if (leap.days.size() == 2) {
            ctx.equal(leap.days[0], day_number(2024, 2, 29), "leap day: 2024-02-29");
            ctx.equal(leap.days[1], day_number(2028, 2, 29), "leap day: 2028-02-29");
            ctx.equal(leap.days[1] - leap.days[0], std::int32_t{1461},
                      "leap day: 2024-02-29 .. 2028-02-29 is 1461 days");
        }
    }

    // A day-of-month the month does not have is skipped, not clamped: the 31st
    // over January..May 2027 is January, March and May only.
    {
        RecurringSpec spec;
        spec.type = 4;
        spec.start_day = day_number(2027, 1, 31);
        spec.end_day = day_number(2027, 5, 31);
        spec.month_day = 31;
        spec.occurrences = 3;
        const cal::RecurrenceExpansion month_end = expand_spec(spec);
        ctx.check(month_end.applied, "month-end: the 31st expands");
        ctx.equal(month_end.days.size(), std::size_t{3},
                  "month-end: February and April have no 31st");
        if (month_end.days.size() == 3) {
            ctx.equal(month_end.days[0], day_number(2027, 1, 31), "month-end: 2027-01-31");
            ctx.equal(month_end.days[1], day_number(2027, 3, 31), "month-end: 2027-03-31");
            ctx.equal(month_end.days[2], day_number(2027, 5, 31), "month-end: 2027-05-31");
        }
    }

    // "The last Monday" in a month with only four Mondays is the fourth Monday;
    // the same month's "fourth Monday" is the same day, and March (five Mondays)
    // separates the two rules.
    {
        RecurringSpec spec;
        spec.type = 5;
        spec.start_day = day_number(2027, 2, 1);
        spec.end_day = day_number(2027, 3, 31);
        spec.month_item = 4;       // Monday
        spec.month_position = 4;   // pjLast
        spec.occurrences = 2;
        const cal::RecurrenceExpansion last = expand_spec(spec);
        ctx.check(last.applied, "fifth weekday absent: 'last Monday' expands");
        ctx.equal(last.days.size(), std::size_t{2}, "fifth weekday absent: two months");
        if (last.days.size() == 2) {
            ctx.equal(last.days[0], day_number(2027, 2, 22),
                      "fifth weekday absent: February's last Monday is the 22nd");
            ctx.equal(last.days[1], day_number(2027, 3, 29),
                      "fifth weekday absent: March's last Monday is the 29th");
        }
        spec.month_position = 3;  // "fourth"
        const cal::RecurrenceExpansion fourth = expand_spec(spec);
        ctx.check(fourth.applied, "fifth weekday absent: 'fourth Monday' expands");
        if (fourth.days.size() == 2) {
            ctx.equal(fourth.days[0], day_number(2027, 2, 22),
                      "fifth weekday absent: February's fourth Monday is the 22nd");
            ctx.equal(fourth.days[1], day_number(2027, 3, 22),
                      "fifth weekday absent: March's fourth Monday is the 22nd, not the last");
        }
    }

    // Weekly with an every-other-week period: the selected weekdays of weeks 0
    // and 2 only.
    {
        RecurringSpec spec;
        spec.type = 6;
        spec.start_day = day_number(2026, 11, 2);
        spec.end_day = day_number(2026, 11, 18);
        spec.period = 2;
        spec.days_of_week = 10;  // Monday | Wednesday
        spec.occurrences = 4;
        const cal::RecurrenceExpansion fortnightly = expand_spec(spec);
        ctx.check(fortnightly.applied, "weekly period 2: expands");
        ctx.equal(fortnightly.days.size(), std::size_t{4},
                  "weekly period 2: two weeks of Monday+Wednesday");
        if (fortnightly.days.size() == 4) {
            ctx.equal(fortnightly.days[0], day_number(2026, 11, 2), "weekly period 2: Mon 11-02");
            ctx.equal(fortnightly.days[1], day_number(2026, 11, 4), "weekly period 2: Wed 11-04");
            ctx.equal(fortnightly.days[2], day_number(2026, 11, 16),
                      "weekly period 2: Mon 11-16 (the week between is skipped)");
            ctx.equal(fortnightly.days[3], day_number(2026, 11, 18), "weekly period 2: Wed 11-18");
        }
    }

    // Monthly with a period across a year boundary: every second month from
    // November 2026 is January and March 2027.
    {
        RecurringSpec spec;
        spec.type = 4;
        spec.start_day = day_number(2026, 11, 15);
        spec.end_day = day_number(2027, 3, 15);
        spec.period = 2;
        spec.month_day = 15;
        spec.occurrences = 3;
        const cal::RecurrenceExpansion two_monthly = expand_spec(spec);
        ctx.check(two_monthly.applied, "monthly period 2: expands across the year boundary");
        ctx.equal(two_monthly.days.size(), std::size_t{3}, "monthly period 2: three months");
        if (two_monthly.days.size() == 3) {
            ctx.equal(two_monthly.days[0], day_number(2026, 11, 15), "monthly period 2: 11-15");
            ctx.equal(two_monthly.days[1], day_number(2027, 1, 15), "monthly period 2: 01-15");
            ctx.equal(two_monthly.days[2], day_number(2027, 3, 15), "monthly period 2: 03-15");
        }
    }

    // Zero occurrences means "no check", and a one-day range is a valid
    // recurrence: the same day is the only occurrence.
    {
        RecurringSpec spec;
        spec.type = 7;
        spec.start_day = day_number(2026, 12, 21);
        spec.end_day = day_number(2026, 12, 21);
        spec.period = 3;
        spec.occurrences = 0;
        const cal::RecurrenceExpansion single = expand_spec(spec);
        ctx.check(single.applied, "zero occurrences: an unchecked one-day range applies");
        ctx.equal(single.days.size(), std::size_t{1}, "zero occurrences: exactly one day");
        if (single.days.size() == 1) {
            ctx.equal(single.days[0], day_number(2026, 12, 21), "zero occurrences: the start day");
        }
        // A weekly record with no mask falls back to the start day's weekday.
        spec.type = 6;
        spec.period = 0;
        spec.days_of_week = 0;
        const cal::RecurrenceExpansion fallback = expand_spec(spec);
        ctx.check(fallback.applied, "zero occurrences: an empty weekly mask uses the start weekday");
        ctx.equal(fallback.days.size(), std::size_t{1},
                  "zero occurrences: the fallback is the single start day");
    }

    // Parameters that are outside their enum are refused, never guessed.
    {
        RecurringSpec spec;
        spec.type = 5;
        spec.start_day = day_number(2027, 2, 1);
        spec.end_day = day_number(2027, 3, 31);
        spec.month_item = 1;  // pjDay: not a weekday
        const cal::RecurrenceExpansion bad_item = expand_spec(spec);
        ctx.check(!bad_item.applied, "parameter guard: MonthItem 1 is refused");
        ctx.check(std::string(bad_item.why).find("MonthItem") != std::string::npos,
                  "parameter guard: the refusal names MonthItem");
        spec.month_item = 0;
        spec.month_position = 5;  // outside PjMonthPosition 0..4
        const cal::RecurrenceExpansion bad_position = expand_spec(spec);
        ctx.check(!bad_position.applied, "parameter guard: MonthPosition 5 is refused");
        ctx.check(std::string(bad_position.why).find("MonthPosition") != std::string::npos,
                  "parameter guard: the refusal names MonthPosition");
    }
}

// --- corpus-gated cases -----------------------------------------------------

void test_standard_probe(sched_test::TestContext& ctx, std::string const& path) {
    const cal::CalendarTable table = cal::decode_file(path);
    ctx.equal(table.calendars.size(), 3u, "base.mpp: three calendar rows (1 base + 2 resource)");
    ctx.equal(table.base_calendar_count(), 1u, "base.mpp: one base calendar");
    ctx.equal(table.derived_calendar_count(), 2u, "base.mpp: two resource calendars");
    ctx.equal(table.exception_count(), 0u, "base.mpp: no dated exceptions");
    ctx.equal(table.work_week_count(), 0u, "base.mpp: no dated work weeks");
    ctx.equal(table.metadata_anomalies, 0u, "base.mpp: FixedMeta/VarMeta agree with FixedData");
    ctx.equal_string(table.default_calendar_name, "Standard",
                     "base.mpp: 114/Props names the project default calendar");

    const cal::CalendarDefinition* standard = cal::find_calendar(table, "Standard");
    ctx.check(standard != nullptr, "base.mpp: the Standard base calendar is present");
    if (standard == nullptr) {
        return;
    }
    ctx.check(standard->base_uid <= 0, "base.mpp: Standard is a base calendar");
    ctx.check(!standard->has_week_data,
              "base.mpp: Standard has no Var2Data block; its week is 114/Props "
              "DEFAULT_CALENDAR_HOURS");
    ctx.equal(standard->exceptions.size(), 0u, "base.mpp: Standard has no exceptions");

    // The project default takes the empty-name path of the API.
    const sched::Calendar calendar = cal::load_project_calendar(path);
    const sched::Week& week = calendar.default_week();
    ctx.equal(week[0].count, 0, "Standard: Sunday is non-working");
    for (std::size_t day = 1; day <= 5; ++day) {
        const sched::DayPattern& pattern = week[day];
        ctx.equal(pattern.count, 2, "Standard: Mon-Fri has two intervals");
        ctx.equal(pattern.start_minute[0], 480, "Standard: morning starts 08:00");
        ctx.equal(pattern.length_minutes[0], 240, "Standard: morning is 4 h");
        ctx.equal(pattern.start_minute[1], 780, "Standard: afternoon starts 13:00");
        ctx.equal(pattern.length_minutes[1], 240, "Standard: afternoon is 4 h");
        ctx.equal(pattern.total_ticks(), kDay8h, "Standard: the day is 8 working hours");
    }
    ctx.equal(week[6].count, 0, "Standard: Saturday is non-working");
    ctx.equal(calendar.work_week_count(), 0u, "Standard: no week overrides are needed");

    // Calendar arithmetic the project has already measured (spec §1.4):
    // 1 d from Mon 08:00 finishes Mon 17:00; 5 d from Tue 08:00 finishes the
    // following Monday 17:00.
    ctx.equal_date(calendar.add_work(at(kMon, 8, 0), kDay8h), at(kMon, 17, 0),
                   "Standard: 1 d from Mon 08:00 ends Mon 17:00");
    ctx.equal_date(calendar.add_work(at(kTue, 8, 0), 5 * kDay8h), at(kNextMon, 17, 0),
                   "Standard: 5 d from Tue 08:00 ends next Mon 17:00");
    ctx.equal(calendar.day_total_ticks(kSat), 0, "Standard: Saturday has no working time");

    // The CFB overload must agree with the path overload.
    const mpp::CompoundFile file = mpp::CompoundFile::from_file(path);
    const sched::Calendar from_file = cal::load_project_calendar(file);
    ctx.equal_date(from_file.add_work(at(kMon, 8, 0), 5 * kDay8h), at(kFri, 17, 0),
                   "CFB overload: 5 d from Mon 08:00 ends Fri 17:00");
    ctx.equal(from_file.day_total_ticks(kFri), kDay8h, "CFB overload: Friday is 8 h");
}

void test_holiday_probe(sched_test::TestContext& ctx, std::string const& path) {
    const cal::CalendarTable table = cal::decode_file(path);
    ctx.equal(table.calendars.size(), 3u, "holiday1: three rows (2 base + 1 resource calendar)");
    ctx.equal(table.base_calendar_count(), 2u, "holiday1: two base calendars");
    ctx.equal(table.derived_calendar_count(), 1u, "holiday1: one resource calendar");
    ctx.equal(table.exception_count(), 1u, "holiday1: one dated exception");
    ctx.equal(table.work_week_count(), 0u, "holiday1: no dated work weeks");
    ctx.equal(table.metadata_anomalies, 0u, "holiday1: metadata is consistent");
    ctx.equal(table.recurring_exceptions, 0u, "holiday1: the exception is a plain date range");
    ctx.equal(table.sub_minute_intervals, 0u, "holiday1: every interval is whole minutes");

    // The fixture's own default is Standard; the three tasks each name SiteCal
    // as their task calendar (see the +178 check below).  Both names are
    // synthetic: holiday1.mpp was built over COM by the coordinator.
    ctx.equal_string(table.default_calendar_name, "Standard",
                     "holiday1: 114/Props still names Standard as the project default");
    const cal::CalendarDefinition* standard = cal::find_calendar(table, "Standard");
    const cal::CalendarDefinition* site = cal::find_calendar(table, "SiteCal");
    ctx.check(standard != nullptr, "holiday1: the Standard base calendar is present");
    ctx.check(site != nullptr, "holiday1: the synthetic SiteCal base calendar is present");
    if (standard == nullptr || site == nullptr) {
        return;
    }
    ctx.check(standard->uid != site->uid, "holiday1: Standard and SiteCal are different rows");
    ctx.check(site->base_uid <= 0,
              "holiday1: SiteCal is stored as a root calendar (base uid 0) and inherits the "
              "default week for every day it does not override");

    // The decoded week: Sunday..Saturday, only Friday is custom.
    ctx.check(site->has_week_data, "SiteCal: has its own Var2Data week block");
    for (std::size_t day = 0; day < 7; ++day) {
        if (day == 5) {
            ctx.check(!site->week[day].use_base, "SiteCal: Friday is a custom day");
        } else {
            ctx.check(site->week[day].use_base, "SiteCal: other days inherit the default times");
        }
    }
    ctx.equal(site->week[5].intervals.size(), 1u, "SiteCal: Friday has one interval");
    if (site->week[5].intervals.size() == 1) {
        ctx.equal(site->week[5].intervals[0].start_ticks, 4800,
                  "SiteCal: Friday starts 08:00 (4800 tenths)");
        ctx.equal(site->week[5].intervals[0].length_ticks, kDay4h,
                  "SiteCal: Friday is 4 h (2400 tenths)");
    }

    // The dated exception: Wed 2026-10-21, non-working, plain range.
    ctx.equal(site->exceptions.size(), 1u, "SiteCal: one exception decoded");
    if (site->exceptions.size() == 1) {
        const cal::DatedException& exception = site->exceptions[0];
        ctx.equal(exception.start_day, kWed, "SiteCal: exception starts 2026-10-21");
        ctx.equal(exception.end_day, kWed, "SiteCal: exception is one day");
        ctx.check(!exception.working, "SiteCal: the exception is a non-working day");
        ctx.equal(exception.recurrence_type, 1, "SiteCal: stored PjExceptionType is 1 (one range)");
    }

    const sched::Calendar calendar = cal::build_calendar(table, *site);
    ctx.equal(calendar.day_total_ticks(kMon), kDay8h, "SiteCal: Monday is 8 h");
    ctx.equal(calendar.day_total_ticks(kTue), kDay8h, "SiteCal: Tuesday is 8 h");
    ctx.equal(calendar.day_total_ticks(kWed), 0, "SiteCal: the exception makes Wednesday non-working");
    ctx.equal(calendar.day_total_ticks(kThu), kDay8h, "SiteCal: Thursday is 8 h");
    ctx.equal(calendar.day_total_ticks(kFri), kDay4h, "SiteCal: Friday is 4 h");
    ctx.equal(calendar.day_total_ticks(kSat), 0, "SiteCal: Saturday is non-working");
    ctx.equal(calendar.day_total_ticks(kSun), 0, "SiteCal: Sunday is non-working");
    // Wed 2026-10-21 and nothing else: exactly one week override.
    ctx.equal(calendar.work_week_count(), 1u, "SiteCal: one per-week override for the exception week");

    // --- the oracle: three task dates, docs/analysis/oracle/holiday-probe.json
    const sched::date32 t1_start = at(kMon, 8, 0);
    const sched::date32 t1_finish = calendar.add_work(t1_start, 2 * kDay8h);
    ctx.equal_date(t1_finish, at(kTue, 17, 0), "T1 (2 d from Mon 08:00) finishes Tue 17:00");

    // T2 is FS after T1: it starts at the next working instant after T1's finish,
    // which skips the non-working Wednesday.
    const sched::date32 t2_start = calendar.next_start(t1_finish);
    ctx.equal_date(t2_start, at(kThu, 8, 0), "T2 (FS after T1) starts Thu 08:00, not Wed");
    const sched::date32 t2_finish = calendar.add_work(t2_start, 5 * kDay8h);
    ctx.equal_date(t2_finish, at(kNextThu, 12, 0),
                   "T2 (5 d from Thu 08:00) finishes Thu 2026-10-29 12:00");
    ctx.equal(calendar.work_time_between(t2_start, t2_finish), 5 * kDay8h,
              "T2 spans exactly 5 x 8 h of working time");

    // T3 is constrained (SNET) to Fri 08:00; its 1 d crosses the short Friday.
    const sched::date32 t3_finish = calendar.add_work(at(kFri, 8, 0), kDay8h);
    ctx.equal_date(t3_finish, at(kNextMon, 12, 0),
                   "T3 (1 d from Fri 08:00) finishes Mon 2026-10-26 12:00");
    ctx.equal(calendar.work_time_between(at(kFri, 8, 0), t3_finish), kDay8h,
              "T3 spans 4 h on Friday + 4 h on Monday");

    // The fixture's evidence that the tasks (not the project default) select
    // SiteCal: TBkndTask FixedData row offset +178 is an i32 calendar uid, 3
    // (SiteCal) for the three tasks and -1 (NA) for the project row.  This is a
    // read-only cross-check of another stream's table, pinned as a finding.
    const mpp::CompoundFile file = mpp::CompoundFile::from_file(path);
    const std::vector<std::uint8_t> task_rows = file.read_stream("114/TBkndTask/FixedData");
    ctx.equal(task_rows.size(), 48u + 4u * 202u, "holiday1: TBkndTask has 4 x 202-byte rows");
    if (task_rows.size() >= 48u + 202u * 4u) {
        auto i32_at = [&task_rows](std::size_t offset) {
            return static_cast<std::int32_t>(
                static_cast<std::uint32_t>(task_rows[offset]) |
                (static_cast<std::uint32_t>(task_rows[offset + 1]) << 8) |
                (static_cast<std::uint32_t>(task_rows[offset + 2]) << 16) |
                (static_cast<std::uint32_t>(task_rows[offset + 3]) << 24));
        };
        ctx.equal(i32_at(48u + 178u), -1, "holiday1: the project row names no task calendar");
        for (std::size_t row = 1; row <= 3; ++row) {
            ctx.equal(i32_at(48u + 202u * row + 178u), site->uid,
                      "holiday1: task row names SiteCal as its task calendar");
        }
    }
}

// --- corpus-gated: every PjExceptionType, decoded and applied ---------------
//
// corpus/probe/w12-exceptions.mpp is the coordinator's synthetic fixture: one
// exception of each type on the project calendar ("Standard").  The expected
// values below are the COM read-back in docs/analysis/oracle/w12-exceptions.json
// (identical before and after the save/reopen), so this pins the decoder against
// MS Project itself, not against the decoder's own assumptions.
//
// Measured gap stated where it lives: for the monthly positional record COM
// reads MonthItem 0 and MonthPosition 0, and the +77 byte is 0 too, so Project
// did not store the weekday; the expansion derives Monday from the start date
// and the stored occurrence word still agrees (7), which is the evidence that
// the derivation is right for this record.
void test_w12_exceptions(sched_test::TestContext& ctx, std::string const& path) {
    const cal::CalendarTable table = cal::decode_file(path);
    ctx.equal(table.calendars.size(), 2u, "w12: two calendar rows");
    ctx.equal(table.base_calendar_count(), 1u, "w12: one base calendar");
    ctx.equal(table.derived_calendar_count(), 1u, "w12: one derived calendar");
    ctx.equal(table.exception_count(), 7u, "w12: seven dated exceptions");
    ctx.equal(table.recurring_exceptions, 6u, "w12: six of them are recurring");
    ctx.equal(table.work_week_count(), 0u, "w12: no dated work weeks");
    ctx.equal(table.metadata_anomalies, 0u, "w12: TBkndCal metadata is consistent");
    ctx.equal_string(table.default_calendar_name, "Standard",
                     "w12: the project default calendar is the synthetic Standard");

    const cal::CalendarDefinition* project = cal::find_calendar_by_uid(table, 1);
    ctx.check(project != nullptr, "w12: the project calendar row (uid 1) exists");
    if (project == nullptr) {
        return;
    }
    ctx.equal(project->exceptions.size(), 7u, "w12: all seven exceptions are on uid 1");

    // One row per record, in the file's order: the COM values and the expanded
    // day set they imply.
    struct Expected {
        std::int32_t type;
        std::int32_t start_day;
        std::int32_t end_day;
        std::int32_t occurrences;
        std::int32_t period;
        std::int32_t days_of_week;
        std::int32_t month;
        std::int32_t month_day;
        std::int32_t month_item;
        std::int32_t month_position;
        std::size_t expanded;
        std::int32_t first_expanded;
        std::int32_t last_expanded;
    };
    const Expected expected[] = {
        // daily, 2026-11-02 .. 2026-11-30: the range itself (29 days).
        {1, 15647, 15675, 29, 0, 0, 0, 0, 0, 0, 29, 15647, 15675},
        // weekly, Monday|Wednesday (mask 10), every week: 18 days.
        {6, 15647, 15706, 18, 1, 10, 0, 0, 0, 0, 18, 15647, 15705},
        // monthly positional, first Monday: 2026-11-02 .. 2027-05-03.
        {5, 15647, 15858, 7, 1, 0, 0, 0, 0, 0, 7, 15647, 15829},
        // monthly day 15: 2026-11-15 .. 2027-03-15.
        {4, 15660, 15780, 5, 1, 0, 0, 15, 0, 0, 5, 15660, 15780},
        // yearly positional, last Friday of November: 2029-11-30 is the fourth,
        // and 2030's last Friday (11-29) is past the range.
        {3, 15672, 17133, 4, 0, 0, 11, 0, 8, 4, 4, 15672, 16771},
        // every N days with N = 1: five consecutive days.
        {7, 15696, 15700, 5, 1, 0, 0, 0, 0, 0, 5, 15696, 15700},
        // yearly month + day: 15 March, 2027 .. 2030.
        {2, 15780, 16876, 4, 0, 0, 3, 15, 0, 0, 4, 15780, 16876},
    };
    const std::size_t record_count = sizeof(expected) / sizeof(expected[0]);
    ctx.equal(project->exceptions.size(), record_count, "w12: one record per expected row");

    std::size_t total_expanded = 0;
    for (std::size_t i = 0; i < record_count && i < project->exceptions.size(); ++i) {
        const cal::DatedException& exception = project->exceptions[i];
        const Expected& row = expected[i];
        const std::string label = "w12 type " + std::to_string(row.type);
        ctx.equal(exception.recurrence_type, row.type, label + ": PjExceptionType");
        ctx.equal(exception.start_day, row.start_day, label + ": start day (COM Start)");
        ctx.equal(exception.end_day, row.end_day, label + ": end day (COM Finish)");
        ctx.equal(exception.occurrences, row.occurrences, label + ": occurrence word (COM)");
        ctx.equal(exception.period, row.period, label + ": Period (COM)");
        ctx.equal(exception.days_of_week, row.days_of_week, label + ": DaysOfWeek (COM)");
        ctx.equal(exception.month, row.month, label + ": Month (COM)");
        ctx.equal(exception.month_day, row.month_day, label + ": MonthDay (COM)");
        ctx.equal(exception.month_item, row.month_item, label + ": MonthItem (COM)");
        ctx.equal(exception.month_position, row.month_position, label + ": MonthPosition (COM)");
        ctx.check(!exception.working, label + ": a non-working exception has no intervals");
        if (row.type == 1) {
            ctx.check(!cal::expand_recurring_exception(exception).applied,
                      label + ": a plain range is not expanded");
            total_expanded += row.expanded;
            continue;
        }
        const cal::RecurrenceExpansion expansion = cal::expand_recurring_exception(exception);
        ctx.check(expansion.applied, label + ": expands from the decoded parameters");
        ctx.equal(expansion.days.size(), row.expanded, label + ": expanded day count");
        if (!expansion.days.empty()) {
            ctx.equal(expansion.days.front(), row.first_expanded, label + ": first expanded day");
            ctx.equal(expansion.days.back(), row.last_expanded, label + ": last expanded day");
        }
        total_expanded += expansion.days.size();
    }
    ctx.equal(total_expanded, std::size_t{72}, "w12: the seven exceptions cover 72 days");

    // Builder: every recurring record is applied, i.e. the stored occurrence
    // word agrees with the expansion in all six cases.  That is the measurement
    // that validates the parameter decode end to end.
    cal::RecurrenceReport report;
    const sched::Calendar project_calendar = cal::build_project_calendar(table, "", &report);
    ctx.equal(report.applied, std::size_t{6}, "w12: all six recurring exceptions applied");
    ctx.equal(report.not_applied, std::size_t{0},
              "w12: none refused, so every expansion matches Project's own count");

    // Spot dates across the type families.
    ctx.equal(project_calendar.day_total_ticks(15647), 0, "w12: Mon 2026-11-02 is free (daily)");
    ctx.equal(project_calendar.day_total_ticks(15660), 0, "w12: Sun 2026-11-15 is free (daily)");
    ctx.equal(project_calendar.day_total_ticks(15675), 0, "w12: Mon 2026-11-30 is free (daily)");
    ctx.equal(project_calendar.day_total_ticks(15676), kDay8h,
              "w12: Tue 2026-12-01 works again (daily range ended)");
    ctx.equal(project_calendar.day_total_ticks(15677), 0,
              "w12: Wed 2026-12-02 is free (weekly mask)");
    ctx.equal(project_calendar.day_total_ticks(15678), kDay8h,
              "w12: Thu 2026-12-03 works (weekly mask skips it)");
    ctx.equal(project_calendar.day_total_ticks(15829), 0,
              "w12: Mon 2027-05-03 is free (monthly positional)");
    ctx.equal(project_calendar.day_total_ticks(16771), 0,
              "w12: Fri 2029-11-30 is free (yearly positional)");
    ctx.equal(project_calendar.day_total_ticks(17135), kDay8h,
              "w12: Fri 2030-11-29 works (past the yearly positional range)");
    ctx.equal(project_calendar.day_total_ticks(15780), 0,
              "w12: Mon 2027-03-15 is free (yearly month+day and monthly day 15)");
    ctx.equal(project_calendar.day_total_ticks(15696), 0,
              "w12: Mon 2026-12-21 is free (every-N-days range)");
    ctx.equal(project_calendar.day_total_ticks(15706), kDay8h,
              "w12: Thu 2026-12-31 works again (every-N-days range and weekly mask ended)");
    ctx.equal_date(project_calendar.add_work(at(15647, 8, 0), 2 * kDay8h), at(15678, 17, 0),
                   "w12: 2 d from Mon 2026-11-02 ends Thu 2026-12-03 17:00");

    // The derived calendar (uid 2, base uid 1, no week data of its own) is the
    // per-task path: it inherits the base calendar's exceptions.
    const cal::CalendarDefinition* derived = cal::find_calendar_by_uid(table, 2);
    ctx.check(derived != nullptr, "w12: the derived calendar (uid 2) exists");
    if (derived == nullptr) {
        return;
    }
    ctx.equal(derived->base_uid, std::int32_t{1}, "w12: uid 2 derives from uid 1");
    ctx.check(!derived->has_week_data, "w12: uid 2 has no week data of its own");
    cal::RecurrenceReport derived_report;
    const sched::Calendar task_calendar = cal::build_calendar(table, *derived, &derived_report);
    ctx.equal(derived_report.applied, std::size_t{6},
              "w12 derived: the base calendar's six recurring exceptions apply");
    ctx.equal(derived_report.not_applied, std::size_t{0}, "w12 derived: none refused");
    ctx.equal(task_calendar.day_total_ticks(15647), 0,
              "w12 derived: Mon 2026-11-02 is free");
    ctx.equal(task_calendar.day_total_ticks(16771), 0,
              "w12 derived: Fri 2029-11-30 is free");
    ctx.equal_date(task_calendar.add_work(at(15647, 8, 0), 2 * kDay8h), at(15678, 17, 0),
                   "w12 derived: 2 d from Mon 2026-11-02 ends Thu 2026-12-03 17:00");
}

// --- corpus-gated: the non-zero monthly positional record -------------------
//
// corpus/probe/w12-exceptions-monthpos.mpp is the coordinator's sharper fixture:
// ONE type-5 exception whose start day is not the target weekday, so Project
// stores MonthItem/MonthPosition instead of leaving them 0.  COM read-back
// (docs/analysis/oracle/w12-monthpos.json, identical before and after reopen):
// Type 5, Start 2026-11-01 (a Sunday), Finish 2027-06-30, Occurrences 8,
// Period 1, MonthPosition 2 (third), MonthItem 4 (Monday), Month 0, MonthDay 0.
// The start weekday and the stored weekday differ, which is what the first
// fixture could not show.
void test_w12_monthpos(sched_test::TestContext& ctx, std::string const& path) {
    const cal::CalendarTable table = cal::decode_file(path);
    ctx.equal(table.calendars.size(), 2u, "monthpos: two calendar rows");
    ctx.equal(table.exception_count(), 1u, "monthpos: one dated exception");
    ctx.equal(table.recurring_exceptions, 1u, "monthpos: it is recurring");
    ctx.equal(table.metadata_anomalies, 0u, "monthpos: TBkndCal metadata is consistent");

    const cal::CalendarDefinition* project = cal::find_calendar_by_uid(table, 1);
    ctx.check(project != nullptr, "monthpos: the project calendar row (uid 1) exists");
    if (project == nullptr || project->exceptions.empty()) {
        ctx.check(false, "monthpos: the exception record is present");
        return;
    }
    const cal::DatedException& exception = project->exceptions.front();
    const std::int32_t start_day = 15646;  // 2026-11-01, a Sunday
    ctx.equal(exception.recurrence_type, std::int32_t{5}, "monthpos: PjExceptionType 5");
    ctx.equal(exception.start_day, start_day, "monthpos: start day 2026-11-01");
    ctx.equal(exception.end_day, std::int32_t{15887}, "monthpos: end day 2027-06-30");
    ctx.equal(exception.occurrences, std::int32_t{8}, "monthpos: occurrence word 8 (COM)");
    ctx.equal(exception.period, std::int32_t{1}, "monthpos: Period 1 (COM)");
    ctx.equal(exception.month_position, std::int32_t{2}, "monthpos: MonthPosition 2 = third (COM)");
    ctx.equal(exception.month_item, std::int32_t{4}, "monthpos: MonthItem 4 = Monday (COM)");
    ctx.equal(exception.month, std::int32_t{0}, "monthpos: Month is not stored");
    ctx.equal(exception.month_day, std::int32_t{0}, "monthpos: MonthDay is not stored");
    ctx.equal(exception.days_of_week, std::int32_t{0}, "monthpos: DaysOfWeek is not stored");
    // The two words are separate: +76 carries the position (2), +77 the month
    // item (4), +78 the period (1), and +80 is the per-record ordinal (1).
    ctx.equal(exception.raw_recurrence_words[1], std::uint16_t{1026},
              "monthpos: +76 word is 0x0402 (position 2, month item 4)");
    ctx.equal(exception.raw_recurrence_words[2], std::uint16_t{1}, "monthpos: +78 word is 1");
    ctx.equal(exception.raw_recurrence_words[3], std::uint16_t{1}, "monthpos: +80 word is 1");
    ctx.equal(cal::weekday_of_month_item(exception.month_item), std::int32_t{1},
              "monthpos: MonthItem 4 is Monday (weekday 1)");
    ctx.equal(sched::weekday_of_day(start_day), std::int32_t{0},
              "monthpos: the start day is a Sunday, not the target Monday");
    ctx.check(cal::weekday_of_month_item(exception.month_item) !=
                  sched::weekday_of_day(start_day),
              "monthpos: the stored weekday differs from the start day's weekday");

    // Third Monday of each month, 2026-11 .. 2027-06: eight days, which is
    // Project's own count (the +4 word), so the parameter decode is validated.
    const std::int32_t expected_days[] = {15661, 15696, 15724, 15752,
                                          15780, 15815, 15843, 15878};
    const cal::RecurrenceExpansion expansion = cal::expand_recurring_exception(exception);
    ctx.check(expansion.applied, "monthpos: expands from the stored parameters");
    ctx.equal(expansion.days.size(), std::size_t{8}, "monthpos: eight third Mondays");
    for (std::size_t i = 0; i < expansion.days.size() && i < 8; ++i) {
        ctx.equal(expansion.days[i], expected_days[i],
                  "monthpos: third Monday #" + std::to_string(i));
    }

    cal::RecurrenceReport report;
    const sched::Calendar calendar = cal::build_project_calendar(table, "", &report);
    ctx.equal(report.applied, std::size_t{1}, "monthpos: the builder applies the record");
    ctx.equal(report.not_applied, std::size_t{0},
              "monthpos: no refusal, so the expansion matches Project's count");
    for (const std::int32_t day : expected_days) {
        ctx.equal(calendar.day_total_ticks(day), 0, "monthpos: a third Monday is free");
    }
    ctx.equal(calendar.day_total_ticks(15662), kDay8h,
              "monthpos: the Tuesday after the first third Monday still works");
    ctx.equal(calendar.day_total_ticks(15879), kDay8h,
              "monthpos: Tue 2027-06-22 works (past the last third Monday)");
    ctx.equal_date(calendar.add_work(at(15658, 8, 0), 2 * kDay8h), at(15662, 17, 0),
                   "monthpos: 2 d from Fri 2026-11-13 skips Mon 11-16 and ends Tue 11-17");
}

void test_project_a_counts(sched_test::TestContext& ctx, std::string const& path) {
    const cal::CalendarTable table = cal::decode_file(path);
    ctx.equal(table.calendars.size(), 33u, "PROJECT_A: 33 calendar rows");
    ctx.equal(table.base_calendar_count(), 10u, "PROJECT_A: 10 base calendars");
    ctx.equal(table.derived_calendar_count(), 23u, "PROJECT_A: 23 resource calendars");
    ctx.equal(table.exception_count(), 1331u, "PROJECT_A: 1331 dated exceptions");
    ctx.equal(table.work_week_count(), 0u, "PROJECT_A: no dated work-week overrides");
    ctx.equal(table.recurring_exceptions, 0u, "PROJECT_A: every exception is a plain date range");
    ctx.equal(table.metadata_anomalies, 0u, "PROJECT_A: TBkndCal metadata is consistent");
    ctx.equal(table.sub_minute_intervals, 0u, "PROJECT_A: every interval is whole minutes");

    std::size_t working_exceptions = 0;
    std::int32_t first_day = 0;
    std::int32_t last_day = 0;
    bool have_day = false;
    const cal::CalendarDefinition* with_holiday = nullptr;
    const cal::DatedException* first_non_working = nullptr;
    const cal::CalendarDefinition* with_special = nullptr;
    const cal::DatedException* first_working = nullptr;
    for (cal::CalendarDefinition const& calendar : table.calendars) {
        for (cal::DatedException const& exception : calendar.exceptions) {
            if (exception.working) {
                ++working_exceptions;
                if (first_working == nullptr) {
                    first_working = &exception;
                    with_special = &calendar;
                }
            } else if (first_non_working == nullptr) {
                first_non_working = &exception;
                with_holiday = &calendar;
            }
            if (!have_day) {
                first_day = exception.start_day;
                last_day = exception.end_day;
                have_day = true;
            } else {
                first_day = std::min(first_day, exception.start_day);
                last_day = std::max(last_day, exception.end_day);
            }
        }
    }
    ctx.equal(working_exceptions, 9u, "PROJECT_A: 9 of the 1331 exceptions are special working days");
    ctx.equal(first_day, 6603, "PROJECT_A: the earliest exception is day 6603 (2002-01-28)");
    ctx.equal(last_day, 14976, "PROJECT_A: the latest exception is day 14976 (2024-12-31)");

    // Every base calendar must build; a non-working exception must really make
    // its day non-working, and a special working day must really be working.
    std::size_t built = 0;
    for (cal::CalendarDefinition const& calendar : table.calendars) {
        if (calendar.uid > 0 && calendar.base_uid <= 0) {
            const sched::Calendar built_calendar = cal::build_calendar(table, calendar);
            ++built;
            ctx.check(built_calendar.default_week().size() == 7u,
                      "PROJECT_A: a built base calendar has a seven-day week");
        }
    }
    ctx.equal(built, 10u, "PROJECT_A: all 10 base calendars build");
    if (with_holiday != nullptr && first_non_working != nullptr) {
        const sched::Calendar built_calendar = cal::build_calendar(table, *with_holiday);
        ctx.equal(built_calendar.day_total_ticks(first_non_working->start_day), 0,
                  "PROJECT_A: a decoded non-working exception is non-working in the built calendar");
    }
    if (with_special != nullptr && first_working != nullptr) {
        const sched::Calendar built_calendar = cal::build_calendar(table, *with_special);
        ctx.check(built_calendar.day_total_ticks(first_working->start_day) > 0,
                  "PROJECT_A: a decoded special working day has working time in the built calendar");
    }

    // Counts and dates only: never a calendar name from this file.
    std::cout << "note: PROJECT_A calendars " << table.calendars.size() << " (base "
              << table.base_calendar_count() << "), exceptions " << table.exception_count()
              << " (" << working_exceptions << " working), work weeks " << table.work_week_count()
              << ", exception days " << first_day << ".." << last_day << '\n';

    // This is the call the plan bridge makes: the project's default calendar
    // (114/Props DEFAULT_CALENDAR_NAME) must resolve by name and build.  Its
    // name is client data and is never printed.
    bool project_resolved = false;
    try {
        const sched::Calendar project = cal::load_project_calendar(path);
        project_resolved = true;
        ctx.equal(project.default_week().size(), 7u,
                  "PROJECT_A: the project default calendar builds a seven-day week");
        ctx.check(project.work_week_count() > 0,
                  "PROJECT_A: the project default calendar carries exception weeks");
        std::cout << "note: PROJECT_A project default: " << project.work_week_count()
                  << " per-week overrides installed\n";
    } catch (mpp::Error const&) {
        project_resolved = false;
    }
    ctx.check(project_resolved,
              "PROJECT_A: the project default calendar resolves by name (no fallback needed)");
}

}  // namespace

int main() {
    sched_test::TestContext ctx;

    // --- CI-safe: the low-level decoders on synthetic bytes ------------------
    {
        const std::vector<std::uint8_t> week = make_five_day_week();
        ctx.equal(week.size(), 420u, "synthetic: seven day blocks are 420 bytes");
        std::vector<std::uint8_t> blob = week;
        append_u16(blob, 0);  // exception count
        append_u16(blob, 0);  // padding to the 8-byte boundary
        append_u32(blob, 0);  // empty work-week section header
        ctx.equal(blob.size(), 428u, "synthetic: week + no exceptions + header is 428 bytes");

        const cal::CalendarData data = cal::decode_calendar_data(blob);
        ctx.check(!data.truncated, "synthetic: the block decodes completely");
        ctx.equal(data.exceptions.size(), 0u, "synthetic: no exceptions decoded");
        ctx.equal(data.work_weeks.size(), 0u, "synthetic: no work weeks decoded");
        ctx.check(!data.days[0].use_base && data.days[0].intervals.empty(),
                  "synthetic: Sunday is a custom non-working day");
        ctx.check(!data.days[1].use_base, "synthetic: Monday is a custom day");
        ctx.equal(data.days[1].intervals.size(), 1u, "synthetic: Monday has one interval");
        if (data.days[1].intervals.size() == 1) {
            ctx.equal(data.days[1].intervals[0].start_ticks, 9 * 600,
                      "synthetic: Monday starts 09:00 in tenths");
            ctx.equal(data.days[1].intervals[0].length_ticks, 8 * 600,
                      "synthetic: Monday is 8 h in tenths");
        }
        ctx.check(!data.days[6].use_base, "synthetic: Saturday is a custom day");

        const sched::DayPattern pattern = cal::to_day_pattern(data.days[1]);
        ctx.equal(pattern.count, 1, "synthetic: to_day_pattern keeps one interval");
        ctx.equal(pattern.start_minute[0], 540, "synthetic: to_day_pattern converts to minutes");
        ctx.equal(pattern.length_minutes[0], 480, "synthetic: to_day_pattern converts the length");
        ctx.equal(pattern.total_ticks(), 4800, "synthetic: the day is 4800 ticks");

        // A "use base" day block only carries its day-type word; the rest of
        // the block is stale buffer content in real files and must be ignored.
        const std::vector<std::uint8_t> inheriting = make_day_block(true, {});
        ctx.check(inheriting.size() == 60u, "synthetic: a 'use base' block is 60 bytes");
        ctx.check(inheriting[0] == 1 && inheriting[1] == 0, "synthetic: day type word is 1");
    }

    {
        // 420-byte week + 2 exceptions + empty work-week header.
        std::vector<std::uint8_t> blob = make_five_day_week();
        append_u16(blob, 2);   // exception count
        append_u16(blob, 0);   // padding
        append_exception(blob, kWed, kWed, {}, 1);                        // non-working
        append_exception(blob, kSat, kSat, {{9 * 60, 12 * 60}}, 1, 3);     // special working
        append_u32(blob, 0);   // empty work-week section
        const cal::CalendarData data = cal::decode_calendar_data(blob);
        ctx.check(!data.truncated, "synthetic 2: the block decodes completely");
        ctx.equal(data.exceptions.size(), 2u, "synthetic 2: two exceptions decoded");
        if (data.exceptions.size() == 2) {
            ctx.equal(data.exceptions[0].start_day, kWed, "synthetic 2: first exception date");
            ctx.check(!data.exceptions[0].working, "synthetic 2: first exception is non-working");
            ctx.equal(data.exceptions[0].recurrence_type, std::int32_t{1},
                      "synthetic 2: a plain range stores PjExceptionType 1");
            ctx.equal(data.exceptions[0].occurrences, std::int32_t{0},
                      "synthetic 2: a plain range stores no occurrence word");
            ctx.check(data.exceptions[1].working, "synthetic 2: second exception is working");
            ctx.equal(data.exceptions[1].occurrences, std::int32_t{3},
                      "synthetic 2: the +4 occurrence word is decoded");
            ctx.equal(data.exceptions[1].intervals.size(), 1u,
                      "synthetic 2: the working exception has one interval");
            if (data.exceptions[1].intervals.size() == 1) {
                ctx.equal(data.exceptions[1].intervals[0].start_ticks, 9 * 600,
                          "synthetic 2: the working exception starts 09:00");
                ctx.equal(data.exceptions[1].intervals[0].length_ticks, 3 * 600,
                          "synthetic 2: the working exception is 3 h");
            }
        }

        // Same block plus one dated work week (mpxj's layout, no file on disk
        // uses one): 7 day blocks + start/finish + 8 unknown bytes + name length.
        std::vector<std::uint8_t> with_week = blob;
        for (std::size_t day = 0; day < 7; ++day) {
            const std::vector<std::uint8_t> block =
                (day == 0 || day == 6) ? make_day_block(false, {})
                                       : make_day_block(false, {{8 * 60, 12 * 60}});
            with_week.insert(with_week.end(), block.begin(), block.end());
        }
        append_u16(with_week, kMon);
        append_u16(with_week, kFri);
        append_u32(with_week, 0);
        append_u32(with_week, 0);
        append_u32(with_week, 0);  // name length 0
        const cal::CalendarData week_data = cal::decode_calendar_data(with_week);
        ctx.equal(week_data.work_weeks.size(), 1u, "synthetic work week: one record decoded");
        if (week_data.work_weeks.size() == 1) {
            ctx.check(!week_data.work_weeks[0].days[1].use_base,
                      "synthetic work week: Monday is a custom day");
            ctx.equal(week_data.work_weeks[0].days[1].intervals.size(), 1u,
                      "synthetic work week: Monday has one interval");
            ctx.equal(week_data.work_weeks[0].start_day, kMon, "synthetic work week: start day");
            ctx.equal(week_data.work_weeks[0].end_day, kFri, "synthetic work week: finish day");
        }
    }

    // --- CI-safe: the builder on a synthetic table ---------------------------
    {
        const cal::CalendarTable table = make_synthetic_table();
        const sched::Calendar calendar = cal::build_project_calendar(table);
        ctx.equal(calendar.day_total_ticks(kMon), 4800,
                  "builder: Monday is the stock 8 h day");
        ctx.equal(calendar.day_total_ticks(kWed), 0,
                  "builder: the non-working exception zeroes Wednesday");
        ctx.equal(calendar.day_total_ticks(kSat), 4800,
                  "builder: the special working Saturday is 8 h");
        ctx.equal(calendar.day_total_ticks(kSun), 0, "builder: Sunday stays non-working");
        ctx.equal(calendar.work_week_count(), 1u,
                  "builder: Wednesday and Saturday share one week override");
        ctx.equal_date(calendar.add_work(at(kMon, 8, 0), 2 * 4800), at(kTue, 17, 0),
                       "builder: 2 d from Mon 08:00 ends Tue 17:00");
        ctx.equal_date(calendar.add_work(at(kTue, 17, 0), 4800), at(kThu, 17, 0),
                       "builder: 1 d after Tue 17:00 skips the non-working Wednesday");

        bool threw = false;
        try {
            static_cast<void>(cal::build_project_calendar(table, "NoSuchCalendar"));
        } catch (mpp::Error const&) {
            threw = true;
        }
        ctx.check(threw, "builder: an unknown calendar name throws mpp::Error");
    }

    // --- CI-safe: recurring exceptions --------------------------------------
    test_recurrence(ctx);
    test_recurrence_parameters(ctx);

    // --- corpus-gated cases --------------------------------------------------
    const std::string probe_dir = CAL_PROBE_DIR;
    const std::string corpus_dir = CAL_CORPUS_DIR;
    const std::string base_path = join_path(probe_dir, "base.mpp");
    const std::string holiday_path = join_path(probe_dir, "holiday1.mpp");
    const std::string w3o1_path = join_path(probe_dir, "w3-o1.mpp");
    const std::string w12_path = join_path(probe_dir, "w12-exceptions.mpp");
    const std::string w12_monthpos_path = join_path(probe_dir, "w12-exceptions-monthpos.mpp");
    const std::string project_a_path = join_path(corpus_dir, "project_a.mpp");

    if (file_exists(base_path)) {
        test_standard_probe(ctx, base_path);
    } else {
        std::cout << "SKIP: " << base_path << " is absent"
                  << (corpus_hidden() ? " (CAL_NO_CORPUS is set)" : "") << '\n';
    }
    if (file_exists(w3o1_path)) {
        const cal::CalendarTable table = cal::decode_file(w3o1_path);
        ctx.equal(table.base_calendar_count(), 1u, "w3-o1: one base calendar");
        ctx.equal(table.exception_count(), 0u, "w3-o1: no dated exceptions");
        ctx.equal(table.calendars.size(), 2u, "w3-o1: two rows (1 base + 1 resource calendar)");
    } else {
        std::cout << "SKIP: " << w3o1_path << " is absent\n";
    }
    if (file_exists(holiday_path)) {
        test_holiday_probe(ctx, holiday_path);
    } else {
        std::cout << "SKIP: " << holiday_path << " is absent\n";
    }
    if (file_exists(w12_path)) {
        test_w12_exceptions(ctx, w12_path);
    } else {
        std::cout << "SKIP: " << w12_path << " is absent"
                  << (corpus_hidden() ? " (CAL_NO_CORPUS is set)" : "") << '\n';
    }
    if (file_exists(w12_monthpos_path)) {
        test_w12_monthpos(ctx, w12_monthpos_path);
    } else {
        std::cout << "SKIP: " << w12_monthpos_path << " is absent"
                  << (corpus_hidden() ? " (CAL_NO_CORPUS is set)" : "") << '\n';
    }
    if (file_exists(project_a_path)) {
        test_project_a_counts(ctx, project_a_path);
    } else {
        std::cout << "SKIP: " << project_a_path << " is absent\n";
    }

    return ctx.finish("cal_tests");
}
