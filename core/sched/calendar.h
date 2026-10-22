// core/sched/calendar.h -- working-time calendar: day patterns, the effective
// week cache, and the forward/backward working-time walks.
//
// spec/scheduling.md §1.1-§1.4.  The proven parts of the model are:
//
// [B] A day is a 60-byte record holding at most 8 (start, length) intervals;
//     count at +0x02, interval starts at +0x08 + 2*i (minutes from midnight),
//     interval lengths at +0x1c + 4*i (minutes).  Seven of them per week.
// [B] The calendar caches the effective week containing a day: week start,
//     week finish and the weekly total at +0x1c0/+0x1c2/+0x1c4; invalidation
//     zeroes the 0x1a4-byte day block.
// [B] WorkTimeBetween(a, b) is the day-loop in §1.3; AddWork/SubWork are the
//     walks in §1.4.
//
// Anything not proven -- base-calendar merging, dated exceptions, the exact
// precedence of a work week over an exception, resource calendars -- is left as
// a TODO(open: ...) and is NOT implemented, so no test can depend on a guess.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "date.h"
#include "duration.h"

namespace core::sched {

// [B] §1.1: the record is 60 bytes and holds at most 8 intervals.
inline constexpr std::size_t kMaxDayIntervals = 8;
inline constexpr std::size_t kDayRecordBytes = 60;

// One day's working intervals.  Units are MINUTES, matching the record (§1.1);
// use start_ticks()/end_ticks()/total_ticks() for tenths.
struct DayPattern {
    std::uint8_t count = 0;
    std::uint16_t start_minute[kMaxDayIntervals] = {};
    std::uint32_t length_minutes[kMaxDayIntervals] = {};

    bool empty() const noexcept { return count == 0; }
    ticks total_ticks() const noexcept;
    std::int32_t start_ticks(std::size_t index) const noexcept;  // tenths from midnight
    std::int32_t end_ticks(std::size_t index) const noexcept;
};

// [B] §1.1 raw layout of the 60-byte day record.  Two fields are kept only to
// make the layout exact; their meaning is not decoded.
struct DayRecord {
    std::uint16_t flags = 0;        // +0x00
    std::uint16_t count = 0;        // +0x02
    std::uint32_t reserved_04 = 0;  // +0x04
    std::uint16_t start[kMaxDayIntervals] = {};   // +0x08 + 2*i
    std::uint32_t reserved_18 = 0;                // +0x18
    std::uint32_t length[kMaxDayIntervals] = {};  // +0x1c + 4*i
};

static_assert(sizeof(DayRecord) == kDayRecordBytes, "day record is 60 bytes (§1.1)");
static_assert(offsetof(DayRecord, count) == 0x02, "count at +2 (§1.1)");
static_assert(offsetof(DayRecord, start) == 0x08, "interval starts at +8 (§1.1)");
static_assert(offsetof(DayRecord, length) == 0x1c, "interval lengths at +0x1c (§1.1)");

DayPattern decode_day_record(const DayRecord& record);
DayRecord encode_day_record(const DayPattern& pattern);

// The effective week cached by the calendar (§1.1).
struct EffectiveWeek {
    // false is the "no source covers this week" marker (0xFFFF in the binary).
    // With a default week there is always a source, so this is true; exceptions
    // and the two-level week source list are TODO(open: W3-C3).
    bool present = false;
    std::int32_t week_start_day = 0;
    std::int32_t week_finish_day = 0;  // normally week_start_day + 6
    ticks total_ticks = 0;             // +0x1c4, tenths of a minute
    std::array<DayPattern, kDaysPerWeek> day{};  // index 0 = Sunday (§0.1)
};

using Week = std::array<DayPattern, kDaysPerWeek>;

// Convenience builders for patterns and weeks.  `intervals` are
// [start_minute, end_minute) pairs in wall-clock minutes, e.g.
//   day_pattern({{8 * 60, 12 * 60}, {13 * 60, 17 * 60}})
DayPattern day_pattern(
    std::initializer_list<std::pair<std::int32_t, std::int32_t>> intervals);

// `days` are (weekday index, pattern) pairs, weekday 0 = Sunday.
Week make_week(std::initializer_list<std::pair<std::int32_t, DayPattern>> days);

class Calendar {
  public:
    Calendar() = default;
    explicit Calendar(const Week& default_week);

    const Week& default_week() const noexcept { return default_week_; }
    void set_default_week(const Week& week);

    // Explicit work-week override for the week that starts on `week_start_day`
    // ([B] the calendar keeps a week source list at +0x1d0 with count at
    // +0x1c8).  Precedence against a dated exception is TODO(open: W3-C3).
    void set_work_week(std::int32_t week_start_day, const Week& week);
    void clear_work_weeks();
    std::size_t work_week_count() const noexcept { return work_weeks_.size(); }

    // Drop the cache; the next effective_week() rebuilds it (§5.1: a calendar
    // edit invalidates the week cache).
    void invalidate_week_cache();
    std::size_t cached_week_count() const noexcept { return week_cache_.size(); }
    // Counts actual materialisations, so a test can prove a cache hit.
    std::size_t week_materialisations() const noexcept { return week_materialisations_; }

    const EffectiveWeek& effective_week(std::int32_t day) const;
    const DayPattern& pattern_for_day(std::int32_t day) const;
    ticks day_total_ticks(std::int32_t day) const;
    bool is_working_day(std::int32_t day) const;
    bool is_working_time(std::int32_t day, std::int32_t tenths) const;

    // §1.2 predicates.
    bool is_working_instant(date32 t) const;
    date32 next_start(date32 t) const;   // t if inside working time, else next interval start
    date32 prev_finish(date32 t) const;  // t if inside working time, else previous interval end
    // The backward alignment the CPM late bounds need (§4.2): t when it is
    // strictly inside a working interval, otherwise the finish of the previous
    // interval.  This is the instant a predecessor must be finished by so that a
    // successor starting at `t` is not delayed.  Measured use: a successor late
    // start of Mon 08:00 makes the predecessor late finish the previous Friday
    // 17:00 (w3-o4 MFO row), and a deadline at Mon 08:00 clamps the late finish
    // the same way (w3-o4b deadlines).
    date32 align_back(date32 t) const;

    // §1.3.
    ticks work_time_between(date32 a, date32 b) const;

    // §1.4.  `d` is tenths of a minute and must be >= 0.
    date32 add_work(date32 t, ticks d) const;  // finish = start + working duration
    date32 sub_work(date32 t, ticks d) const;  // symmetric backward walk

    // Day numbers are a 16-bit field; a walk that leaves the domain returns
    // kDateOverflow (§0.1).  The bound is settable so that the overflow path is
    // testable without relying on the exact domain end.
    // TODO(open: W3-O1): the binary's own domain end is not measured.
    void set_day_limit(std::int32_t last_day) noexcept { day_limit_ = last_day; }
    std::int32_t day_limit() const noexcept { return day_limit_; }

  private:
    const Week* find_work_week(std::int32_t week_start_day) const;

    // First working instant of the day / of the week; last working instant of
    // the day / of the week.  `tenths == kTenthsPerDay` means "end of day".
    static std::int32_t first_start(const DayPattern& pattern) noexcept;
    static std::int32_t last_end(const DayPattern& pattern) noexcept;
    DayTime first_working_instant_of_week(const EffectiveWeek& week) const;
    DayTime last_working_instant_of_week(const EffectiveWeek& week) const;
    std::int32_t interval_containing(const DayPattern& pattern, std::int32_t tenths) const;
    std::int32_t interval_ending_at(const DayPattern& pattern, std::int32_t tenths) const;

    // Move forward to the next working instant (inside an interval); returns a
    // position with day > day_limit_ when the walk runs out of calendar.
    DayTime align_forward(DayTime p) const;
    // Greatest working instant <= p, always at an interval end or strictly
    // inside one.  nullopt when the walk runs off the start of the calendar.
    std::optional<DayTime> last_end_at_or_before(DayTime p) const;

    Week default_week_{};
    std::vector<std::pair<std::int32_t, Week>> work_weeks_;
    mutable std::map<std::int32_t, EffectiveWeek> week_cache_;
    mutable std::size_t week_materialisations_ = 0;
    std::int32_t day_limit_ = kMaxDay;
};

}  // namespace core::sched
