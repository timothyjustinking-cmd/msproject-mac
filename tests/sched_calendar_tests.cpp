// tests/sched_calendar_tests.cpp -- the 60-byte day record, the effective-week
// cache, and the AddWork/SubWork walks.
//
// Ground rule: every expected value is hand-computed from a [B] statement of
// spec/scheduling.md and checked in a comment.  Calendars used below:
//
//   standard    Mon-Fri 08:00-12:00 + 13:00-17:00   480 min/day = 4800 ticks
//   continuous  Mon-Fri 08:00-17:00                 540 min/day = 5400 ticks
//   four_day    Mon-Thu 08:00-12:00 + 13:00-17:00   480 min/day, week = 19200
//   all_day     every day 00:00-24:00              1440 min/day = 14400 ticks
#include <cstdint>
#include <string>

#include "calendar.h"
#include "sched_test_support.h"

namespace sched = core::sched;

namespace {

// Day numbers around the week of Monday 2026-10-19 (§0.1: day 1 = 1984-01-01).
constexpr std::int32_t kMon = 15633;      // 2026-10-19
constexpr std::int32_t kTue = 15634;      // 2026-10-20
constexpr std::int32_t kWed = 15635;      // 2026-10-21
constexpr std::int32_t kThu = 15636;      // 2026-10-22
constexpr std::int32_t kFri = 15637;      // 2026-10-23
constexpr std::int32_t kSat = 15638;      // 2026-10-24
constexpr std::int32_t kSun = 15639;      // 2026-10-25
constexpr std::int32_t kNextMon = 15640;  // 2026-10-26
constexpr std::int32_t kPrevSun = 15632;  // 2026-10-18, Sunday that starts the week
constexpr std::int32_t kPrevFri = 15630;  // 2026-10-16

sched::date32 at(std::int32_t day, std::int32_t hour, std::int32_t minute) {
    return sched::pack(day, (hour * 60 + minute) * sched::kTenthsPerMinute);
}

sched::DayPattern eight_hour_day() {
    return sched::day_pattern({{8 * 60, 12 * 60}, {13 * 60, 17 * 60}});
}

sched::Week standard_week() {
    const sched::DayPattern day = eight_hour_day();
    return sched::make_week({{1, day}, {2, day}, {3, day}, {4, day}, {5, day}});
}

sched::Week continuous_week() {
    const sched::DayPattern day = sched::day_pattern({{8 * 60, 17 * 60}});
    return sched::make_week({{1, day}, {2, day}, {3, day}, {4, day}, {5, day}});
}

sched::Week four_day_week() {
    const sched::DayPattern day = eight_hour_day();
    return sched::make_week({{1, day}, {2, day}, {3, day}, {4, day}});
}

sched::Week all_day_week() {
    const sched::DayPattern day = sched::day_pattern({{0, 1440}});
    sched::Week week{};
    for (std::size_t i = 0; i < 7; ++i) {
        week[i] = day;
    }
    return week;
}

// add_work then SubWork then WorkTimeBetween must agree: the finish is where d
// tenths of working time have been consumed, and the backward walk inverts it.
void check_round_trip(sched_test::TestContext& ctx, const sched::Calendar& calendar,
                      sched::date32 start, sched::ticks duration,
                      const std::string& what) {
    const sched::date32 finish = calendar.add_work(start, duration);
    ctx.check(finish != sched::kDateOverflow, what + ": finish is a real date");
    ctx.equal(calendar.work_time_between(start, finish), duration,
              what + ": WorkTimeBetween(start, finish) == duration");
    ctx.equal_date(calendar.sub_work(finish, duration), start,
                   what + ": SubWork(finish, duration) == start");
}

}  // namespace

int main() {
    sched_test::TestContext ctx;

    // --- §1.1 the 60-byte day record ------------------------------------
    ctx.equal(sizeof(sched::DayRecord), std::size_t{60}, "day record is 60 bytes");
    ctx.equal(sched::kMaxDayIntervals, std::size_t{8}, "at most 8 intervals");
    {
        sched::DayRecord record;
        record.count = 2;
        record.start[0] = 480;  // 08:00
        record.length[0] = 240;
        record.start[1] = 780;  // 13:00
        record.length[1] = 240;

        const sched::DayPattern pattern = sched::decode_day_record(record);
        ctx.equal(pattern.count, 2, "decoded interval count");
        ctx.equal(pattern.start_ticks(0), 4800, "interval 0 starts at 08:00");
        ctx.equal(pattern.end_ticks(0), 7200, "interval 0 ends at 12:00");
        ctx.equal(pattern.start_ticks(1), 7800, "interval 1 starts at 13:00");
        ctx.equal(pattern.end_ticks(1), 10200, "interval 1 ends at 17:00");
        ctx.equal(pattern.total_ticks(), sched::ticks{4800}, "day total is 4800 ticks");

        const sched::DayRecord encoded = sched::encode_day_record(pattern);
        ctx.equal(encoded.count, 2, "re-encoded count");
        ctx.equal(encoded.start[0], 480, "re-encoded interval 0 start");
        ctx.equal(encoded.length[1], 240, "re-encoded interval 1 length");
    }
    {
        sched::DayRecord empty_record;
        const sched::DayPattern empty = sched::decode_day_record(empty_record);
        ctx.equal(empty.count, 0, "a zeroed record has no intervals");
        ctx.equal(empty.total_ticks(), sched::ticks{0}, "non-working day has no time");
        ctx.check(empty.empty(), "empty() is true for a zeroed record");
    }
    {
        // Eight intervals: the maximum the record can hold.
        const sched::DayPattern pattern = sched::day_pattern({{0, 60},
                                                              {60, 120},
                                                              {120, 180},
                                                              {180, 240},
                                                              {240, 300},
                                                              {300, 360},
                                                              {360, 420},
                                                              {420, 480}});
        ctx.equal(pattern.count, 8, "eight intervals are accepted");
        const sched::DayPattern back = sched::decode_day_record(
            sched::encode_day_record(pattern));
        ctx.equal(back.count, 8, "eight intervals round trip");
        ctx.equal(back.start_ticks(7), 4200, "last interval starts at 07:00");
        ctx.equal(back.total_ticks(), sched::ticks{4800}, "eight hours in total");
    }

    // --- §1.1 the effective week and its cache ---------------------------
    sched::Calendar standard(standard_week());
    {
        const sched::EffectiveWeek& week = standard.effective_week(kWed);
        ctx.check(week.present, "the default week is always a source");
        ctx.equal(week.week_start_day, kPrevSun,
                  "the week starts on its Sunday (weekday 0, §0.1)");
        ctx.equal(week.week_finish_day, kSat, "week finish is Saturday (start + 6)");
        ctx.equal(week.total_ticks, sched::ticks{24000},
                  "five eight-hour days are 24000 ticks");
        ctx.equal(week.day[0].count, 0, "Sunday is non-working");
        ctx.equal(week.day[1].total_ticks(), sched::ticks{4800}, "Monday is 4800 ticks");
        ctx.equal(week.day[5].total_ticks(), sched::ticks{4800}, "Friday is 4800 ticks");
        ctx.equal(week.day[6].count, 0, "Saturday is non-working");
    }
    {
        const sched::EffectiveWeek& monday = standard.effective_week(kMon);
        const sched::EffectiveWeek& thursday = standard.effective_week(kThu);
        ctx.check(&monday == &thursday, "two days of one week share one cached week");
        ctx.equal(standard.week_materialisations(), std::size_t{1},
                  "the week was materialised once");
        ctx.equal(standard.cached_week_count(), std::size_t{1}, "one cached week");

        const sched::EffectiveWeek& next = standard.effective_week(kNextMon);
        ctx.equal(next.week_start_day, kSun, "the next week starts on the next Sunday");
        ctx.equal(standard.week_materialisations(), std::size_t{2},
                  "a different week is a second materialisation");
        ctx.check(&next != &monday, "different weeks are different cache entries");
    }
    {
        // §5.1: a calendar edit invalidates the week cache.
        standard.set_default_week(four_day_week());
        ctx.equal(standard.cached_week_count(), std::size_t{0},
                  "set_default_week drops the cached weeks");
        const sched::EffectiveWeek& rebuilt = standard.effective_week(kWed);
        ctx.equal(rebuilt.total_ticks, sched::ticks{19200},
                  "a four-day week is 19200 ticks");
        ctx.equal(rebuilt.day[5].count, 0, "Friday is non-working in a four-day week");
        ctx.equal(standard.week_materialisations(), std::size_t{3},
                  "the rebuilt week is a third materialisation");
    }

    // --- an explicit work week covers exactly its own week ---------------
    // [B] the calendar materialises a week from its source list; the source
    // lookup rule is §1.1 step 2 ([B]/[I]).  Exception precedence is not
    // exercised: TODO(open: W3-C3).
    {
        sched::Calendar override_calendar(standard_week());
        override_calendar.set_work_week(kPrevSun, four_day_week());
        ctx.equal(override_calendar.work_week_count(), std::size_t{1}, "one work week");
        ctx.equal(override_calendar.effective_week(kWed).total_ticks, sched::ticks{19200},
                  "the overridden week has four working days");
        ctx.equal(override_calendar.effective_week(kNextMon).total_ticks,
                  sched::ticks{24000}, "the next week falls back to the default week");
        // 5 d from Mon 08:00 with a 4-day override week: Mon-Thu = 19200, then
        // the remaining 4800 pays for Monday 2026-10-26 (17:00 finish).
        ctx.equal_date(override_calendar.add_work(at(kMon, 8, 0), 24000),
                       at(kNextMon, 17, 0), "5 d across an overridden four-day week");
    }

    // --- §1.2 predicates ------------------------------------------------
    const sched::Calendar std_cal(standard_week());
    ctx.check(std_cal.is_working_day(kMon), "Monday is a working day");
    ctx.check(!std_cal.is_working_day(kSat), "Saturday is not a working day");
    ctx.check(!std_cal.is_working_day(kSun), "Sunday is not a working day");
    ctx.check(std_cal.is_working_time(kMon, 4800), "08:00 is working time");
    ctx.check(std_cal.is_working_time(kMon, 7190), "11:59 is working time");
    ctx.check(!std_cal.is_working_time(kMon, 7200), "12:00 is half-open, not working");
    ctx.check(!std_cal.is_working_time(kMon, 7500), "12:30 is lunch");
    ctx.check(std_cal.is_working_time(kMon, 7800), "13:00 is working time");
    ctx.check(!std_cal.is_working_time(kMon, 10200), "17:00 is the interval end");
    ctx.check(!std_cal.is_working_instant(at(kSat, 10, 0)), "Saturday 10:00 is not working");
    ctx.check(std_cal.is_working_instant(at(kMon, 10, 0)), "Monday 10:00 is working");

    // §1.2 next_start / prev_finish.
    ctx.equal_date(std_cal.next_start(at(kMon, 12, 30)), at(kMon, 13, 0),
                   "next_start(12:30) is 13:00");
    ctx.equal_date(std_cal.next_start(at(kSat, 10, 0)), at(kNextMon, 8, 0),
                   "next_start(Saturday 10:00) is Monday 08:00");
    ctx.equal_date(std_cal.next_start(at(kMon, 10, 0)), at(kMon, 10, 0),
                   "next_start inside working time is the instant itself");
    ctx.equal_date(std_cal.prev_finish(at(kMon, 12, 30)), at(kMon, 12, 0),
                   "prev_finish(12:30) is 12:00");
    ctx.equal_date(std_cal.prev_finish(at(kMon, 7, 0)), at(kPrevFri, 17, 0),
                   "prev_finish(Monday 07:00) is the Friday before, 17:00");
    ctx.equal_date(std_cal.prev_finish(at(kMon, 10, 0)), at(kMon, 10, 0),
                   "prev_finish inside working time is the instant itself");

    // --- §1.3 WorkTimeBetween -------------------------------------------
    const sched::Calendar continuous(continuous_week());
    // W3-C1: Fri 16:00 -> Mon 09:00 = 60 + 60 minutes.
    ctx.equal(continuous.work_time_between(at(kFri, 16, 0), at(kNextMon, 9, 0)),
              sched::ticks{1200}, "W3-C1 Fri 16:00 -> Mon 09:00 is 120 minutes");
    ctx.equal(continuous.work_time_between(at(kMon, 8, 0), at(kMon, 17, 0)),
              sched::ticks{5400}, "§1.3 an 08:00-17:00 day is 540 minutes");
    ctx.equal(std_cal.work_time_between(at(kMon, 8, 0), at(kTue, 12, 0)),
              sched::ticks{7200}, "Mon 08:00 -> Tue 12:00 is 720 minutes");
    ctx.equal(std_cal.work_time_between(at(kMon, 12, 0), at(kMon, 13, 0)),
              sched::ticks{0}, "the lunch hour contributes nothing");
    ctx.equal(std_cal.work_time_between(at(kMon, 11, 0), at(kMon, 14, 0)),
              sched::ticks{1200}, "11:00 -> 14:00 is 60 + 60 minutes");
    ctx.equal(std_cal.work_time_between(at(kFri, 16, 0), at(kNextMon, 9, 0)),
              sched::ticks{1200}, "the weekend contributes nothing");
    ctx.equal(std_cal.work_time_between(at(kMon, 10, 0), at(kMon, 10, 0)),
              sched::ticks{0}, "an empty span is zero");
    ctx.equal(std_cal.work_time_between(at(kMon, 17, 0), at(kMon, 8, 0)),
              sched::ticks{0}, "b <= a is zero");

    // --- §1.4 AddWork ---------------------------------------------------
    // Consequence 1 / the brief's boundary case: 1 d = 4800 ticks from Mon 08:00.
    ctx.equal_date(std_cal.add_work(at(kMon, 8, 0), 4800), at(kMon, 17, 0),
                   "Mon 08:00 + 1 d = Mon 17:00");
    // Consequence 3: landing on an interval end does not snap forward.
    ctx.equal_date(std_cal.add_work(at(kWed, 17, 0), 4800), at(kThu, 17, 0),
                   "AddWork(Wed 17:00, 1 d) = Thu 17:00");
    // W3-C3: 5 d from Tue 08:00 finishes Monday 2026-10-26 at 17:00.
    ctx.equal_date(std_cal.add_work(at(kTue, 8, 0), 24000), at(kNextMon, 17, 0),
                   "W3-C3 Tue 08:00 + 5 d = next Monday 17:00");
    // W3-C4: 4 h from Fri 15:00 = 2 h Friday + 2 h Monday.
    ctx.equal_date(std_cal.add_work(at(kFri, 15, 0), 2400), at(kNextMon, 10, 0),
                   "W3-C4 Fri 15:00 + 4 h = Monday 10:00");
    // Consequence 2, on the continuous 08:00-17:00 day: 3 h Monday + 5 h Tuesday.
    ctx.equal_date(continuous.add_work(at(kMon, 14, 0), 4800), at(kTue, 13, 0),
                   "§1.4 consequence 2: Mon 14:00 + 1 d = Tue 13:00");
    // Lunch is skipped, not counted.
    ctx.equal_date(std_cal.add_work(at(kMon, 11, 0), 2400), at(kMon, 16, 0),
                   "Mon 11:00 + 4 h = Mon 16:00 (lunch skipped)");
    ctx.equal_date(std_cal.add_work(at(kMon, 11, 30), 600), at(kMon, 13, 30),
                   "Mon 11:30 + 1 h = Mon 13:30");
    ctx.equal_date(std_cal.add_work(at(kMon, 8, 0), 600), at(kMon, 9, 0),
                   "Mon 08:00 + 1 h = Mon 09:00");
    // One tick short of a day, and one tick past it.
    ctx.equal_date(std_cal.add_work(at(kMon, 8, 0), 4799), sched::pack(kMon, 10199),
                   "Mon 08:00 + 4799 ticks = Mon 16:59:54");
    ctx.equal_date(std_cal.add_work(at(kMon, 8, 0), 4801), sched::pack(kTue, 4801),
                   "Mon 08:00 + 4801 ticks = Tue 08:00:06");
    // A start on the lunch boundary snaps forward before consuming.
    ctx.equal_date(std_cal.add_work(at(kMon, 12, 0), 4800), at(kTue, 12, 0),
                   "Mon 12:00 + 1 d = Tue 12:00 (12:00 snaps to 13:00)");
    // Noon-ish start inside a single continuous interval.
    ctx.equal_date(continuous.add_work(at(kMon, 12, 0), 600), at(kMon, 13, 0),
                   "continuous: Mon 12:00 + 1 h = Mon 13:00");
    // Weekend crossing.
    ctx.equal_date(continuous.add_work(at(kFri, 16, 0), 600), at(kFri, 17, 0),
                   "continuous: Fri 16:00 + 1 h = Fri 17:00");
    ctx.equal_date(continuous.add_work(at(kFri, 16, 0), 601), sched::pack(kNextMon, 4801),
                   "continuous: Fri 16:00 + 1 h + 1 tick = Monday 08:00:06");
    ctx.equal_date(continuous.add_work(at(kFri, 16, 0), 1200), at(kNextMon, 9, 0),
                   "continuous: Fri 16:00 + 2 h = Monday 09:00");
    // A zero duration returns t before any alignment, even at the weekend.
    ctx.equal_date(std_cal.add_work(at(kSat, 10, 0), 0), at(kSat, 10, 0),
                   "AddWork(t, 0) = t");
    // Four-day work week.
    const sched::Calendar four_day(four_day_week());
    ctx.equal_date(four_day.add_work(at(kMon, 8, 0), 19200), at(kThu, 17, 0),
                   "four-day week: Mon 08:00 + 4 d = Thu 17:00");
    ctx.equal_date(four_day.add_work(at(kMon, 8, 0), 24000), at(kNextMon, 17, 0),
                   "four-day week: Mon 08:00 + 5 d = next Monday 17:00");
    // 24:00 is the next day 00:00 (§1.4 consequence 4).
    const sched::Calendar all_day(all_day_week());
    ctx.equal_date(all_day.add_work(sched::pack(1, 0), 14400), sched::pack(2, 0),
                   "24-hour calendar: day 1 00:00 + 24 h = day 2 00:00");
    ctx.equal_date(all_day.add_work(sched::pack(1, 0), 115200), sched::pack(9, 0),
                   "24-hour calendar: 8 whole days is day 9 00:00");
    // NA in, NA out; a walk off the end of the domain returns the sentinel.
    ctx.equal(std_cal.add_work(sched::kDateNa, 4800), sched::kDateNa, "AddWork(NA) = NA");
    ctx.equal(std_cal.next_start(sched::kDateNa), sched::kDateNa, "next_start(NA) = NA");
    {
        sched::Calendar bounded(standard_week());
        bounded.set_day_limit(kNextMon);
        ctx.equal(bounded.add_work(at(kMon, 8, 0), 240000), sched::kDateOverflow,
                  "a walk past the calendar domain returns the overflow sentinel");
    }

    // --- §1.4 SubWork ---------------------------------------------------
    ctx.equal_date(std_cal.sub_work(at(kMon, 17, 0), 4800), at(kMon, 8, 0),
                   "SubWork(Mon 17:00, 1 d) = Mon 08:00");
    // 1 h on Monday + 7 h on Friday; 17:00 - 7 working hours = Friday 09:00.
    ctx.equal_date(std_cal.sub_work(at(kMon, 9, 0), 4800), at(kPrevFri, 9, 0),
                   "SubWork(Mon 09:00, 1 d) = the Friday before, 09:00");
    // At an interval start the previous interval's finish is the anchor: a full
    // Friday before Monday 08:00.
    ctx.equal_date(std_cal.sub_work(at(kMon, 8, 0), 4800), at(kPrevFri, 8, 0),
                   "SubWork(Mon 08:00, 1 d) = the Friday before, 08:00");
    ctx.equal_date(continuous.sub_work(at(kTue, 13, 0), 4800), at(kMon, 14, 0),
                   "§1.4 consequence 2 inverse: Tue 13:00 - 1 d = Mon 14:00");
    ctx.equal_date(std_cal.sub_work(at(kNextMon, 17, 0), 24000), at(kTue, 8, 0),
                   "W3-C3 inverse: next Monday 17:00 - 5 d = Tue 08:00");
    ctx.equal_date(std_cal.sub_work(at(kThu, 17, 0), 4800), at(kThu, 8, 0),
                   "SubWork(Thu 17:00, 1 d) = Thu 08:00");
    ctx.equal_date(four_day.sub_work(at(kThu, 17, 0), 19200), at(kMon, 8, 0),
                   "four-day week: Thu 17:00 - 4 d = Mon 08:00");
    ctx.equal_date(std_cal.sub_work(at(kSat, 10, 0), 0), at(kSat, 10, 0),
                   "SubWork(t, 0) = t");
    ctx.equal(std_cal.sub_work(sched::kDateNa, 4800), sched::kDateNa, "SubWork(NA) = NA");

    // Six whole working days, i.e. more than one calendar week: this is the
    // path that consumes a whole week in both directions.
    ctx.equal_date(std_cal.add_work(at(kPrevFri, 8, 0), 28800), at(kFri, 17, 0),
                   "Fri 16th 08:00 + 6 d = Fri 23rd 17:00");
    ctx.equal_date(std_cal.sub_work(at(kFri, 17, 0), 28800), at(kPrevFri, 8, 0),
                   "Fri 23rd 17:00 - 6 d = Fri 16th 08:00");

    // --- the two walks are inverses -------------------------------------
    check_round_trip(ctx, std_cal, at(kMon, 8, 0), 4800, "standard Mon 08:00 + 1 d");
    check_round_trip(ctx, std_cal, at(kMon, 11, 0), 2400, "standard Mon 11:00 + 4 h");
    check_round_trip(ctx, std_cal, at(kFri, 15, 0), 2400, "standard Fri 15:00 + 4 h");
    check_round_trip(ctx, std_cal, at(kTue, 8, 0), 24000, "standard Tue 08:00 + 5 d");
    check_round_trip(ctx, four_day, at(kMon, 8, 0), 19200, "four-day Mon 08:00 + 4 d");
    check_round_trip(ctx, continuous, at(kMon, 14, 0), 4800, "continuous Mon 14:00 + 1 d");

    return ctx.finish("sched_calendar_tests");
}
