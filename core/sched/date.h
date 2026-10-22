// core/sched/date.h -- the engine's packed date/time value ("date32").
//
// Every claim implemented here is tagged in spec/scheduling.md §0.1; the tags are
// repeated in the comments.  [B] = proven from the binary, [I] = inferred,
// [O] = open.  Anything [I]/[O] is left as a TODO(open: <recipe>) rather than
// hard-coded, so the tests only pin what the binary proves.
//
//   bits 31..16 : day number, 1-based, day 1 = 1984-01-01
//   bits 15..0  : time of day in TENTHS OF A MINUTE, 0..14390
//
// Resolution is six seconds.  Never use floating point for a stored date.
#pragma once

#include <cstdint>
#include <string>

namespace core::sched {

using date32 = std::uint32_t;

// [B] §0.1: 0xFFFFFFFF is NA / not set.
inline constexpr date32 kDateNa = 0xFFFFFFFFu;
// [B] §0.1: the walker's overflow sentinel, day 60631 time 14390 (23:59).  The
// binary returns it when a walk leaves the calendar domain (FUN_140200b10,
// FUN_14038c410 -- i.e. both directions).
inline constexpr date32 kDateOverflow = 0xECD73836u;

// [B] §0.1: day 1 = 1984-01-01; 1984-01-01 was a Sunday, so weekday 0 = Sunday.
inline constexpr std::int32_t kEpochYear = 1984;
inline constexpr std::int32_t kEpochMonth = 1;
inline constexpr std::int32_t kEpochDay = 1;

// [B] §0.1: tenth of a minute resolution.
inline constexpr std::int32_t kTenthsPerMinute = 10;
inline constexpr std::int32_t kMinutesPerDay = 1440;
inline constexpr std::int32_t kTenthsPerDay = 14400;
inline constexpr std::int32_t kSecondsPerMinute = 60;
inline constexpr std::int32_t kSecondsPerDay = 86400;
inline constexpr std::int32_t kSecondsPerTenth = 6;

inline constexpr std::int32_t kMinDay = 1;
// The day number is a 16-bit field and 0xFFFF is the day component of the NA
// value, so 0xFFFE is the largest usable day.  A calendar may impose a tighter
// limit (Calendar::set_day_limit).  [B] field width; the exact domain end the
// binary uses is not separately cited -- see TODO(open: W3-O1) in calendar.cpp.
inline constexpr std::int32_t kMaxDay = 0xFFFE;

inline constexpr std::int32_t kDaysPerWeek = 7;

struct CivilDate {
    std::int32_t year = 0;
    std::int32_t month = 0;  // 1..12
    std::int32_t day = 0;    // 1..31
};

// An unnormalised position inside the calendar.  Keeping `tenths` able to equal
// kTenthsPerDay lets "end of day" be represented exactly while walking: the
// binary stores a 24:00 interval end as the next day 00:00 (§1.4 consequence 4,
// [B]), and both forms are the same instant.
struct DayTime {
    std::int32_t day = 0;
    std::int32_t tenths = 0;
};

constexpr bool operator==(const DayTime& a, const DayTime& b) noexcept {
    return a.day == b.day && a.tenths == b.tenths;
}
constexpr bool operator!=(const DayTime& a, const DayTime& b) noexcept { return !(a == b); }
constexpr bool operator<(const DayTime& a, const DayTime& b) noexcept {
    return a.day != b.day ? a.day < b.day : a.tenths < b.tenths;
}
constexpr bool operator<=(const DayTime& a, const DayTime& b) noexcept { return !(b < a); }
constexpr bool operator>(const DayTime& a, const DayTime& b) noexcept { return b < a; }
constexpr bool operator>=(const DayTime& a, const DayTime& b) noexcept { return !(a < b); }

// --- §0.1 helper set, exactly the one the spec lists -----------------------

constexpr std::int32_t day_of(date32 d) noexcept { return static_cast<std::int32_t>(d >> 16); }
constexpr std::int32_t time_of(date32 d) noexcept {
    return static_cast<std::int32_t>(d & 0xFFFFu);
}
constexpr bool is_na(date32 d) noexcept { return d == kDateNa; }
constexpr bool is_overflow(date32 d) noexcept { return d == kDateOverflow; }

// pack(day, tenths) -> u32.  A 24:00 interval end (tenths == kTenthsPerDay) is
// normalised to the next day 00:00: FUN_140200b10 compares the remaining time
// against 0x3840 (= 14400) and steps the day ([B] §1.4 consequence 4).
constexpr date32 pack(std::int32_t day, std::int32_t tenths) noexcept {
    day += tenths / kTenthsPerDay;
    tenths %= kTenthsPerDay;
    if (tenths < 0) {
        tenths += kTenthsPerDay;
        --day;
    }
    return (static_cast<date32>(day) << 16) | static_cast<date32>(tenths);
}

// weekday(d) -> (day - 1) % 7, 0 = Sunday.  FUN_140246be8 special-cases day 0 to
// 6, which the positive modulo below reproduces ([B] §0.1).
constexpr std::int32_t weekday_of_day(std::int32_t day) noexcept {
    std::int32_t rem = (day - 1) % kDaysPerWeek;
    if (rem < 0) {
        rem += kDaysPerWeek;
    }
    return rem;
}

constexpr std::int32_t weekday(date32 d) noexcept {
    return is_na(d) ? -1 : weekday_of_day(day_of(d));
}

// add_days(d, n) -> ((day(d) + n) << 16) | time(d).  NA stays NA.
constexpr date32 add_days(date32 d, std::int32_t count) noexcept {
    if (is_na(d)) {
        return kDateNa;
    }
    return (static_cast<date32>(day_of(d) + count) << 16) | static_cast<date32>(time_of(d));
}

constexpr date32 date_only(date32 d) noexcept {
    return is_na(d) ? kDateNa : (static_cast<date32>(day_of(d)) << 16);
}

// Same day, different time of day.
constexpr date32 at_time(date32 d, std::int32_t tenths) noexcept {
    if (is_na(d)) {
        return kDateNa;
    }
    return pack(day_of(d), tenths);
}

constexpr DayTime to_day_time(date32 d) noexcept {
    return DayTime{day_of(d), time_of(d)};
}

constexpr date32 from_day_time(DayTime p) noexcept { return pack(p.day, p.tenths); }

// --- civil conversions ------------------------------------------------------

// Proleptic Gregorian, the calendar the engine's date type uses ([B] the packed
// value is a day count; the civil mapping is arithmetic on that count).
CivilDate to_civil(date32 d);
date32 from_civil(const CivilDate& civil, std::int32_t tenths = 0);
date32 from_ymd(std::int32_t year, std::int32_t month, std::int32_t day,
                std::int32_t hour = 0, std::int32_t minute = 0);

// Seconds since 1984-01-01 00:00:00, six seconds per tenth.  Precondition:
// `d` is a real date (not NA, not the overflow sentinel).
std::int64_t to_seconds(date32 d);
date32 from_seconds(std::int64_t seconds);

// "1984-01-01 00:00:00", "NA" or "OVERFLOW" -- diagnostics only.
std::string to_string(date32 d);

}  // namespace core::sched
