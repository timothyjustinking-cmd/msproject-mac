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
    // Jan 2027 (2026-10-13, 11-10, 12-08, 2027-01-12).
    {
        const cal::RecurrenceExpansion second_tuesday = expand(5, 15627, 15718);
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

    // PjExceptionType 7 = day-count: "every N days" has no derivable period.
    {
        const cal::RecurrenceExpansion day_count = expand(7, 15633, 15647);
        ctx.check(!day_count.applied, "recurrence day-count: refused");
        ctx.check(day_count.days.empty(), "recurrence day-count: no days installed");
        ctx.check(std::string(day_count.why).find("period") != std::string::npos,
                  "recurrence day-count: the reason names the missing period");
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

    // A type-7 exception is counted, not applied, and changes nothing.
    {
        const cal::CalendarTable table = make_recurring_table(7, kMon, kMon + 14, 0);
        cal::RecurrenceReport report;
        const sched::Calendar calendar = cal::build_project_calendar(table, "", &report);
        ctx.equal(report.applied, std::size_t{0}, "recurrence builder type 7: not applied");
        ctx.equal(report.not_applied, std::size_t{1}, "recurrence builder type 7: counted");
        ctx.equal(calendar.day_total_ticks(kMon), 4800,
                  "recurrence builder type 7: the calendar is unchanged");
        ctx.equal(calendar.work_week_count(), 0u,
                  "recurrence builder type 7: no week override installed");
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

    // --- corpus-gated cases --------------------------------------------------
    const std::string probe_dir = CAL_PROBE_DIR;
    const std::string corpus_dir = CAL_CORPUS_DIR;
    const std::string base_path = join_path(probe_dir, "base.mpp");
    const std::string holiday_path = join_path(probe_dir, "holiday1.mpp");
    const std::string w3o1_path = join_path(probe_dir, "w3-o1.mpp");
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
    if (file_exists(project_a_path)) {
        test_project_a_counts(ctx, project_a_path);
    } else {
        std::cout << "SKIP: " << project_a_path << " is absent\n";
    }

    return ctx.finish("cal_tests");
}
