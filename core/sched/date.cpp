// core/sched/date.cpp -- civil conversion and seconds conversion for date32.
#include "date.h"

#include <cassert>
#include <cstdio>

namespace core::sched {
namespace {

// Howard Hinnant's proleptic-Gregorian algorithms (public domain), with the
// epoch shifted so that day numbers match the engine's.
constexpr std::int32_t days_from_civil(std::int32_t y, std::int32_t m,
                                       std::int32_t d) noexcept {
    y -= m <= 2 ? 1 : 0;
    const std::int32_t era = (y >= 0 ? y : y - 399) / 400;
    const std::int32_t yoe = y - era * 400;                                  // [0, 399]
    const std::int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;  // [0, 365]
    const std::int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // [0, 146096]
    return era * 146097 + doe - 719468;
}

constexpr std::int32_t days_from_1984_01_01() { return days_from_civil(1984, 1, 1); }

}  // namespace

static_assert(days_from_1984_01_01() == 5113, "1984-01-01 is 5113 days after 1970-01-01");

CivilDate to_civil(date32 d) {
    assert(!is_na(d) && !is_overflow(d));
    // day 1 == 1984-01-01 == 5113 days after 1970-01-01
    std::int32_t z = day_of(d) + 5112;
    z += 719468;
    const std::int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int32_t doe = z - era * 146097;
    const std::int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int32_t y = yoe + era * 400;
    const std::int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int32_t mp = (5 * doy + 2) / 153;
    const std::int32_t dd = doy - (153 * mp + 2) / 5 + 1;
    const std::int32_t mm = mp + (mp < 10 ? 3 : -9);
    return CivilDate{y + (mm <= 2 ? 1 : 0), mm, dd};
}

date32 from_civil(const CivilDate& civil, std::int32_t tenths) {
    assert(civil.month >= 1 && civil.month <= 12);
    assert(civil.day >= 1 && civil.day <= 31);
    const std::int32_t day =
        days_from_civil(civil.year, civil.month, civil.day) - days_from_1984_01_01() + 1;
    return pack(day, tenths);
}

date32 from_ymd(std::int32_t year, std::int32_t month, std::int32_t day,
                std::int32_t hour, std::int32_t minute) {
    return from_civil(CivilDate{year, month, day},
                      (hour * 60 + minute) * kTenthsPerMinute);
}

std::int64_t to_seconds(date32 d) {
    assert(!is_na(d) && !is_overflow(d));
    return static_cast<std::int64_t>(day_of(d) - kMinDay) * kSecondsPerDay +
           static_cast<std::int64_t>(time_of(d)) * kSecondsPerTenth;
}

date32 from_seconds(std::int64_t seconds) {
    std::int64_t days = seconds / kSecondsPerDay;
    std::int64_t rem = seconds % kSecondsPerDay;
    if (rem < 0) {
        rem += kSecondsPerDay;
        --days;
    }
    const std::int32_t day = static_cast<std::int32_t>(days) + kMinDay;
    const std::int32_t tenths = static_cast<std::int32_t>(rem / kSecondsPerTenth);
    return pack(day, tenths);
}

std::string to_string(date32 d) {
    if (is_na(d)) {
        return "NA";
    }
    if (is_overflow(d)) {
        return "OVERFLOW";
    }
    const CivilDate civil = to_civil(d);
    const std::int32_t tenths = time_of(d);
    const std::int32_t minutes = tenths / kTenthsPerMinute;
    const std::int32_t seconds = (tenths % kTenthsPerMinute) * kSecondsPerTenth;
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", civil.year,
                  civil.month, civil.day, minutes / 60, minutes % 60, seconds);
    return std::string(buffer);
}

}  // namespace core::sched
