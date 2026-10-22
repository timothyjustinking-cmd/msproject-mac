// tests/sched_date_tests.cpp -- date32 packing, epoch, weekday, civil and seconds
// conversion.  Every expected value below is derivable from spec/scheduling.md
// §0.1 ([B]), which is why there is no randomness and no reference implementation.
#include <cstdint>
#include <string>

#include "date.h"
#include "sched_test_support.h"

namespace sched = core::sched;
using sched::date32;

// --- compile-time forms of the §0.1 claims --------------------------------
static_assert(sched::kTenthsPerMinute == 10);
static_assert(sched::kMinutesPerDay == 1440);
static_assert(sched::kTenthsPerDay == 14400);
static_assert(sched::kSecondsPerTenth == 6);
static_assert(sizeof(date32) == 4);

// bits 31..16 = day, bits 15..0 = tenths of a minute.
static_assert(sched::pack(15634, 4800) == 0x3D1212C0u, "W3-D1: 2026-10-20 08:00");
static_assert(sched::day_of(0x3D1212C0u) == 15634);
static_assert(sched::time_of(0x3D1212C0u) == 4800);
// 2026-10-20 is a Tuesday, so weekday 2 with 0 = Sunday.
static_assert(sched::weekday(0x3D1212C0u) == 2);
// 1984-01-01 is day 1 and a Sunday.
static_assert(sched::weekday_of_day(1) == 0);
// FUN_140246be8 returns 6 for day 0.
static_assert(sched::weekday_of_day(0) == 6);
// 24:00 is stored as the next day 00:00 (§1.4 consequence 4).
static_assert(sched::pack(1, sched::kTenthsPerDay) == sched::pack(2, 0));
static_assert(sched::pack(1, sched::kTenthsPerDay) == 0x00020000u);
// 0xECD73836 is day 60631 time 14390 (§0.1 overflow sentinel).
static_assert(sched::day_of(sched::kDateOverflow) == 60631);
static_assert(sched::time_of(sched::kDateOverflow) == 14390);
static_assert(sched::is_na(sched::kDateNa));
static_assert(!sched::is_na(sched::kDateOverflow));

int main() {
    sched_test::TestContext ctx;

    // --- packing ---------------------------------------------------------
    ctx.equal(sched::pack(1, 0), 0x00010000u, "pack(1,0)");
    ctx.equal(sched::day_of(0x00010000u), 1, "day_of(day 1)");
    ctx.equal(sched::time_of(0x00010000u), 0, "time_of(day 1)");
    ctx.equal(sched::pack(2, 14390), 0x00023836u, "pack(2, 23:59)");

    // --- epoch and civil mapping ----------------------------------------
    ctx.equal_date(sched::from_ymd(1984, 1, 1), sched::pack(1, 0), "epoch is day 1");
    const sched::CivilDate epoch = sched::to_civil(sched::pack(1, 0));
    ctx.equal(epoch.year, 1984, "epoch year");
    ctx.equal(epoch.month, 1, "epoch month");
    ctx.equal(epoch.day, 1, "epoch day");
    ctx.equal(sched::weekday(sched::from_ymd(1984, 1, 1)), 0, "1984-01-01 is a Sunday");

    // W3-D1: a task created on 2026-10-20 08:00 stores 0x3D1212C0.
    ctx.equal_date(sched::from_ymd(2026, 10, 20, 8, 0), 0x3D1212C0u,
                   "W3-D1 2026-10-20 08:00 packs to 0x3D1212C0");
    ctx.equal(sched::day_of(sched::from_ymd(2026, 10, 20)), 15634, "2026-10-20 is day 15634");
    // And Sunday 2026-10-18 has weekday 0.
    ctx.equal(sched::weekday(sched::from_ymd(2026, 10, 18)), 0,
              "W3-D1 2026-10-18 is weekday 0");

    // Weekday table around one week: Mon 19th .. Sun 25th Oct 2026.
    ctx.equal(sched::weekday(sched::from_ymd(2026, 10, 19)), 1, "2026-10-19 is Monday");
    ctx.equal(sched::weekday(sched::from_ymd(2026, 10, 20)), 2, "2026-10-20 is Tuesday");
    ctx.equal(sched::weekday(sched::from_ymd(2026, 10, 23)), 5, "2026-10-23 is Friday");
    ctx.equal(sched::weekday(sched::from_ymd(2026, 10, 24)), 6, "2026-10-24 is Saturday");
    ctx.equal(sched::weekday(sched::from_ymd(2026, 10, 25)), 0, "2026-10-25 is Sunday");

    // Leap years and century rules (proleptic Gregorian arithmetic on the day count).
    ctx.equal(sched::day_of(sched::from_ymd(1984, 2, 29)), 60, "1984-02-29 is day 60");
    ctx.equal(sched::day_of(sched::from_ymd(1985, 1, 1)), 367, "1985-01-01 is day 367");
    ctx.equal(sched::day_of(sched::from_ymd(2000, 2, 29)), 5904, "2000-02-29 is day 5904");
    ctx.equal(sched::day_of(sched::from_ymd(2100, 3, 1)), 42429,
              "2100-03-01 is day 42429 (2100 is not a leap year)");
    ctx.equal(sched::weekday(sched::from_ymd(2100, 3, 1)), 1, "2100-03-01 is a Monday");

    // Round trip the whole usable range of days we care about.
    int round_trip_failures = 0;
    for (std::int32_t day = sched::kMinDay; day <= 40000; ++day) {
        const date32 packed = sched::pack(day, 0);
        const sched::CivilDate civil = sched::to_civil(packed);
        if (sched::from_civil(civil) != packed) {
            ++round_trip_failures;
        }
    }
    ctx.equal(round_trip_failures, 0, "to_civil/from_civil round trip over days 1..40000");

    // --- add_days / date_only / at_time ---------------------------------
    ctx.equal_date(sched::add_days(sched::from_ymd(2026, 10, 19, 8, 0), 7),
                   sched::from_ymd(2026, 10, 26, 8, 0), "add_days +7 keeps the time of day");
    ctx.equal(sched::weekday(sched::add_days(sched::from_ymd(2026, 10, 19), 7)), 1,
              "add_days +7 keeps the weekday");
    ctx.equal(sched::weekday(sched::add_days(sched::from_ymd(2026, 10, 19), 5)), 6,
              "Monday +5 days is Saturday");
    ctx.equal(sched::add_days(sched::kDateNa, 3), sched::kDateNa, "add_days on NA stays NA");
    ctx.equal(sched::date_only(0x3D1212C0u), sched::pack(15634, 0),
              "date_only strips the time of day");
    ctx.equal(sched::at_time(sched::kDateNa, 4800), sched::kDateNa, "at_time on NA stays NA");

    // --- seconds ---------------------------------------------------------
    ctx.equal(sched::to_seconds(sched::pack(1, 0)), std::int64_t{0},
              "epoch is zero seconds");
    ctx.equal(sched::to_seconds(0x3D1212C0u), std::int64_t{1350720000},
              "2026-10-20 08:00 is 1350720000 s after 1984-01-01");
    ctx.equal_date(sched::from_seconds(1350720000), 0x3D1212C0u,
                   "from_seconds inverts to_seconds");
    ctx.equal(sched::to_seconds(sched::pack(1, 14390)), std::int64_t{86340},
              "23:59 on day 1 is 86340 s");
    // One tenth is six seconds, so sub-tick seconds truncate.
    ctx.equal_date(sched::from_seconds(5), sched::pack(1, 0), "5 s truncates to day 1 00:00");
    ctx.equal_date(sched::from_seconds(6), sched::pack(1, 1), "6 s is one tenth");

    // --- NA / overflow / diagnostics ------------------------------------
    ctx.check(sched::is_na(sched::kDateNa), "0xFFFFFFFF is NA");
    ctx.check(sched::is_overflow(sched::kDateOverflow), "0xECD73836 is the overflow sentinel");
    ctx.check(!sched::is_na(sched::kDateOverflow), "overflow is not NA");
    ctx.equal(sched::weekday(sched::kDateNa), -1, "weekday of NA is -1");
    ctx.equal_string(sched::to_string(sched::kDateNa), "NA", "to_string(NA)");
    ctx.equal_string(sched::to_string(sched::kDateOverflow), "OVERFLOW",
                     "to_string(overflow)");
    ctx.equal_string(sched::to_string(sched::from_ymd(1984, 1, 1)), "1984-01-01 00:00:00",
                     "to_string(epoch)");
    ctx.equal_string(sched::to_string(0x3D1212C0u), "2026-10-20 08:00:00",
                     "to_string(W3-D1)");

    return ctx.finish("sched_date_tests");
}
