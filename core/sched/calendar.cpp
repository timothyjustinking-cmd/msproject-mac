// core/sched/calendar.cpp -- day patterns, effective-week cache, working-time walks.
//
// The two walks are the heart of the engine (§1.4): the outer layer consumes whole
// weeks, the middle layer whole days, the inner layer positions the remainder
// inside one day's interval list.  The layers are an optimisation: the day-loop
// result and the layered result are the same dates, which the calendar tests
// check with the WorkTimeBetween invariant.
#include "calendar.h"

#include <algorithm>
#include <cassert>

namespace core::sched {
namespace {

constexpr std::int32_t kNoIndex = -1;

constexpr std::size_t count_of(const DayPattern& pattern) noexcept {
    return static_cast<std::size_t>(pattern.count);
}

}  // namespace

// --- day patterns -----------------------------------------------------------

ticks DayPattern::total_ticks() const noexcept {
    ticks total = 0;
    for (std::size_t i = 0; i < count_of(*this); ++i) {
        total += static_cast<ticks>(length_minutes[i]) * kTenthsPerMinute;
    }
    return total;
}

std::int32_t DayPattern::start_ticks(std::size_t index) const noexcept {
    return static_cast<std::int32_t>(start_minute[index]) * kTenthsPerMinute;
}

std::int32_t DayPattern::end_ticks(std::size_t index) const noexcept {
    return start_ticks(index) +
           static_cast<std::int32_t>(length_minutes[index]) * kTenthsPerMinute;
}

// [B] §1.1 layout: count at +2, starts at +8 + 2*i, lengths at +0x1c + 4*i.
DayPattern decode_day_record(const DayRecord& record) {
    DayPattern pattern;
    // The record holds at most eight intervals; a corrupt count is clamped rather
    // than read past the end of the array.
    const std::uint16_t count =
        record.count > static_cast<std::uint16_t>(kMaxDayIntervals)
            ? static_cast<std::uint16_t>(kMaxDayIntervals)
            : record.count;
    pattern.count = static_cast<std::uint8_t>(count);
    for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
        pattern.start_minute[i] = record.start[i];
        pattern.length_minutes[i] = record.length[i];
    }
    return pattern;
}

DayRecord encode_day_record(const DayPattern& pattern) {
    // [B] §1.1: at most 8 intervals.
    assert(count_of(pattern) <= kMaxDayIntervals);
    DayRecord record;
    record.count = static_cast<std::uint16_t>(pattern.count);
    for (std::size_t i = 0; i < count_of(pattern); ++i) {
        record.start[i] = pattern.start_minute[i];
        record.length[i] = pattern.length_minutes[i];
    }
    return record;
}

DayPattern day_pattern(
    std::initializer_list<std::pair<std::int32_t, std::int32_t>> intervals) {
    DayPattern pattern;
    for (const std::pair<std::int32_t, std::int32_t>& interval : intervals) {
        if (count_of(pattern) >= kMaxDayIntervals) {
            break;
        }
        assert(interval.first >= 0 && interval.second > interval.first &&
               interval.second <= kMinutesPerDay);
        if (interval.first < 0 || interval.second <= interval.first ||
            interval.second > kMinutesPerDay) {
            continue;
        }
        const std::size_t slot = count_of(pattern);
        pattern.start_minute[slot] = static_cast<std::uint16_t>(interval.first);
        pattern.length_minutes[slot] =
            static_cast<std::uint32_t>(interval.second - interval.first);
        pattern.count = static_cast<std::uint8_t>(slot + 1);
    }
    return pattern;
}

Week make_week(std::initializer_list<std::pair<std::int32_t, DayPattern>> days) {
    Week week{};
    for (const std::pair<std::int32_t, DayPattern>& entry : days) {
        assert(entry.first >= 0 && entry.first < kDaysPerWeek);
        if (entry.first < 0 || entry.first >= kDaysPerWeek) {
            continue;
        }
        week[static_cast<std::size_t>(entry.first)] = entry.second;
    }
    return week;
}

// --- Calendar ---------------------------------------------------------------

Calendar::Calendar(const Week& default_week) : default_week_(default_week) {}

void Calendar::set_default_week(const Week& week) {
    default_week_ = week;
    invalidate_week_cache();
}

void Calendar::set_work_week(std::int32_t week_start_day, const Week& week) {
    for (std::pair<std::int32_t, Week>& entry : work_weeks_) {
        if (entry.first == week_start_day) {
            entry.second = week;
            invalidate_week_cache();
            return;
        }
    }
    work_weeks_.emplace_back(week_start_day, week);
    invalidate_week_cache();
}

void Calendar::clear_work_weeks() {
    work_weeks_.clear();
    invalidate_week_cache();
}

void Calendar::invalidate_week_cache() { week_cache_.clear(); }

const Week* Calendar::find_work_week(std::int32_t week_start_day) const {
    for (const std::pair<std::int32_t, Week>& entry : work_weeks_) {
        if (entry.first == week_start_day) {
            return &entry.second;
        }
    }
    return nullptr;
}

const EffectiveWeek& Calendar::effective_week(std::int32_t day) const {
    const std::int32_t week_start = day - weekday_of_day(day);
    const auto cached = week_cache_.find(week_start);
    if (cached != week_cache_.end()) {
        return cached->second;
    }
    EffectiveWeek week;
    week.week_start_day = week_start;
    week.week_finish_day = week_start + kDaysPerWeek - 1;
    // [B] The calendar materialises the week from its source list and caches it.
    // A work-week override covers exactly the week it starts in; otherwise the
    // default week is the source.  Precedence against dated exceptions, and the
    // "no source covers the week" marker, are TODO(open: W3-C3).
    const Week* source = find_work_week(week_start);
    const Week& pattern = source != nullptr ? *source : default_week_;
    week.present = true;
    week.total_ticks = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(kDaysPerWeek); ++i) {
        week.day[i] = pattern[i];
        week.total_ticks += pattern[i].total_ticks();
    }
    ++week_materialisations_;
    const auto inserted = week_cache_.emplace(week_start, week);
    return inserted.first->second;
}

const DayPattern& Calendar::pattern_for_day(std::int32_t day) const {
    const EffectiveWeek& week = effective_week(day);
    return week.day[static_cast<std::size_t>(weekday_of_day(day))];
}

ticks Calendar::day_total_ticks(std::int32_t day) const {
    return pattern_for_day(day).total_ticks();
}

bool Calendar::is_working_day(std::int32_t day) const {
    return day_total_ticks(day) > 0;
}

bool Calendar::is_working_time(std::int32_t day, std::int32_t tenths) const {
    // §1.2: t inside some interval, start <= t < start + length.
    const DayPattern& pattern = pattern_for_day(day);
    for (std::size_t i = 0; i < count_of(pattern); ++i) {
        if (tenths >= pattern.start_ticks(i) && tenths < pattern.end_ticks(i)) {
            return true;
        }
    }
    return false;
}

bool Calendar::is_working_instant(date32 t) const {
    if (is_na(t) || is_overflow(t)) {
        return false;
    }
    return is_working_time(day_of(t), time_of(t));
}

// --- alignment --------------------------------------------------------------

std::int32_t Calendar::first_start(const DayPattern& pattern) noexcept {
    // The interval list is stored in order, so the first interval starts the day.
    return pattern.empty() ? kNoIndex : pattern.start_ticks(0);
}

std::int32_t Calendar::last_end(const DayPattern& pattern) noexcept {
    return pattern.empty() ? kNoIndex
                           : pattern.end_ticks(static_cast<std::size_t>(pattern.count) - 1);
}

std::int32_t Calendar::interval_containing(const DayPattern& pattern,
                                           std::int32_t tenths) const {
    for (std::size_t i = 0; i < count_of(pattern); ++i) {
        if (tenths >= pattern.start_ticks(i) && tenths < pattern.end_ticks(i)) {
            return static_cast<std::int32_t>(i);
        }
    }
    return kNoIndex;
}

std::int32_t Calendar::interval_ending_at(const DayPattern& pattern,
                                         std::int32_t tenths) const {
    // The interval whose finish is `tenths`, or the one containing it strictly.
    for (std::size_t i = 0; i < count_of(pattern); ++i) {
        if (tenths > pattern.start_ticks(i) && tenths <= pattern.end_ticks(i)) {
            return static_cast<std::int32_t>(i);
        }
    }
    return kNoIndex;
}

DayTime Calendar::align_forward(DayTime p) const {
    if (p.day < kMinDay) {
        p.day = kMinDay;
        p.tenths = 0;
    }
    for (;;) {
        if (p.day > day_limit_) {
            return p;
        }
        const DayPattern& pattern = pattern_for_day(p.day);
        for (std::size_t i = 0; i < count_of(pattern); ++i) {
            const std::int32_t start = pattern.start_ticks(i);
            if (p.tenths < start) {
                return DayTime{p.day, start};
            }
            if (p.tenths < pattern.end_ticks(i)) {
                return p;  // inside this interval
            }
        }
        ++p.day;
        p.tenths = 0;
    }
}

std::optional<DayTime> Calendar::last_end_at_or_before(DayTime p) const {
    // Used by the backward walk: it returns the same instant when p is strictly
    // inside an interval, and otherwise the greatest interval finish <= p.  An
    // interval START therefore resolves to the previous interval's finish -- that
    // is the one case the spec's SubWork pseudocode leaves open (it would spin on
    // `avail == 0`); see TODO(open: W3-O4) in sub_work().
    for (;;) {
        if (p.day < kMinDay) {
            return std::nullopt;
        }
        const DayPattern& pattern = pattern_for_day(p.day);
        for (std::size_t i = 0; i < count_of(pattern); ++i) {
            const std::int32_t start = pattern.start_ticks(i);
            const std::int32_t end = pattern.end_ticks(i);
            if (start < p.tenths && p.tenths < end) {
                return p;
            }
        }
        std::int32_t best = kNoIndex;
        for (std::size_t i = 0; i < count_of(pattern); ++i) {
            const std::int32_t end = pattern.end_ticks(i);
            if (end <= p.tenths && end > best) {
                best = end;
            }
        }
        if (best != kNoIndex) {
            return DayTime{p.day, best};
        }
        --p.day;
        p.tenths = kTenthsPerDay;
    }
}

DayTime Calendar::first_working_instant_of_week(const EffectiveWeek& week) const {
    for (std::size_t i = 0; i < static_cast<std::size_t>(kDaysPerWeek); ++i) {
        const std::int32_t start = first_start(week.day[i]);
        if (start != kNoIndex) {
            return DayTime{week.week_start_day + static_cast<std::int32_t>(i), start};
        }
    }
    return DayTime{week.week_start_day, 0};  // empty week; never compared
}

DayTime Calendar::last_working_instant_of_week(const EffectiveWeek& week) const {
    for (std::size_t i = static_cast<std::size_t>(kDaysPerWeek); i-- > 0;) {
        const std::int32_t end = last_end(week.day[i]);
        if (end != kNoIndex) {
            return DayTime{week.week_start_day + static_cast<std::int32_t>(i), end};
        }
    }
    return DayTime{week.week_start_day, 0};  // empty week; never compared
}

// --- public walks -----------------------------------------------------------

date32 Calendar::next_start(date32 t) const {
    if (is_na(t)) {
        return kDateNa;
    }
    if (is_overflow(t)) {
        return kDateOverflow;
    }
    const DayTime p = align_forward(to_day_time(t));
    return p.day > day_limit_ ? kDateOverflow : from_day_time(p);
}

date32 Calendar::prev_finish(date32 t) const {
    if (is_na(t)) {
        return kDateNa;
    }
    if (is_overflow(t)) {
        return kDateOverflow;
    }
    const DayTime p = to_day_time(t);
    if (is_working_time(p.day, p.tenths)) {
        return t;  // §1.2: inside working time, prev_finish is t itself
    }
    for (std::int32_t day = p.day; day >= kMinDay; --day) {
        const DayPattern& pattern = pattern_for_day(day);
        const std::int32_t hi = day == p.day ? p.tenths : kTenthsPerDay;
        std::int32_t best = kNoIndex;
        for (std::size_t i = 0; i < count_of(pattern); ++i) {
            const std::int32_t end = pattern.end_ticks(i);
            if (end <= hi && end > best) {
                best = end;
            }
        }
        if (best != kNoIndex) {
            return pack(day, best);
        }
    }
    return kDateOverflow;
}

// The forward/backward alignment pair used by the CPM link bounds (§4.2):
// next_start() moves a start forward to a working instant; align_back() moves a
// finish back to the instant by which a predecessor must be finished.  A finish
// that sits strictly inside a working interval is its own answer; an interval
// START is not, and resolves to the previous interval's finish.
date32 Calendar::align_back(date32 t) const {
    if (is_na(t)) {
        return kDateNa;
    }
    if (is_overflow(t)) {
        return kDateOverflow;
    }
    const std::optional<DayTime> aligned = last_end_at_or_before(to_day_time(t));
    if (!aligned.has_value()) {
        return kDateOverflow;
    }
    return from_day_time(*aligned);
}

ticks Calendar::work_time_between(date32 a, date32 b) const {
    // [B] §1.3, verbatim: day loop, half-open at the start, closed at the end.
    if (is_na(a) || is_na(b) || is_overflow(a) || is_overflow(b)) {
        return 0;
    }
    const DayTime pa = to_day_time(a);
    const DayTime pb = to_day_time(b);
    if (!(pa < pb)) {
        return 0;
    }
    ticks total = 0;
    for (std::int32_t day = pa.day; day <= pb.day; ++day) {
        const DayPattern& pattern = pattern_for_day(day);
        const std::int32_t lo = day == pa.day ? pa.tenths : 0;
        const std::int32_t hi = day == pb.day ? pb.tenths : kTenthsPerDay;
        for (std::size_t i = 0; i < count_of(pattern); ++i) {
            const std::int32_t start = pattern.start_ticks(i);
            const std::int32_t end = pattern.end_ticks(i);
            const std::int32_t overlap = std::min(hi, end) - std::max(lo, start);
            if (overlap > 0) {
                total += overlap;
            }
        }
    }
    return total;
}

date32 Calendar::add_work(date32 t, ticks d) const {
    if (is_na(t)) {
        return kDateNa;
    }
    assert(d >= 0);  // §1.4: AddWork takes a non-negative duration
    if (d < 0) {
        return t;
    }
    if (d == 0) {
        return t;  // §1.4: before any alignment, an exact zero returns t
    }
    // TODO(open: W3-C2): a start that is not a working instant is snapped forward
    // before any duration is consumed, per the GetEndDate pseudocode.  The
    // binary's alignment for a task *pinned* to non-working time (Saturday 10:00)
    // is not measured; do not rely on this case.
    DayTime p = align_forward(to_day_time(t));
    if (p.day > day_limit_) {
        return kDateOverflow;
    }
    for (;;) {
        const EffectiveWeek& week = effective_week(p.day);
        const DayPattern& pattern =
            week.day[static_cast<std::size_t>(weekday_of_day(p.day))];
        // Outer layer: whole weeks (§1.4 / FUN_14024e7c0).  Only when parked on
        // the week's first working instant, so that the count consumed is exact.
        if (week.total_ticks > 0 && p == first_working_instant_of_week(week) &&
            d > week.total_ticks) {
            d -= week.total_ticks;
            p = align_forward(DayTime{p.day + kDaysPerWeek, 0});
            if (p.day > day_limit_) {
                return kDateOverflow;
            }
            continue;
        }
        // Middle layer: whole days.  Strict `>`: landing exactly on the day's
        // finish must stay there instead of snapping to the next day
        // (§1.4 consequence 3, "not Thu 08:00").
        if (!pattern.empty() && p.tenths == first_start(pattern)) {
            const ticks day_total = pattern.total_ticks();
            if (d > day_total) {
                d -= day_total;
                p = align_forward(DayTime{p.day + 1, 0});
                if (p.day > day_limit_) {
                    return kDateOverflow;
                }
                continue;
            }
        }
        if (pattern.empty()) {  // unreachable after align_forward; keeps the walk total
            p = align_forward(DayTime{p.day + 1, 0});
            if (p.day > day_limit_) {
                return kDateOverflow;
            }
            continue;
        }
        // Inner layer: position inside one day (FUN_140200b10 / FUN_140200cac).
        const std::int32_t index = interval_containing(pattern, p.tenths);
        if (index == kNoIndex) {
            p = align_forward(DayTime{p.day, p.tenths});
            if (p.day > day_limit_) {
                return kDateOverflow;
            }
            continue;
        }
        const std::int32_t end = pattern.end_ticks(static_cast<std::size_t>(index));
        if (d <= static_cast<ticks>(end - p.tenths)) {
            // The finish may equal the interval end and does not snap there.
            return pack(p.day, p.tenths + static_cast<std::int32_t>(d));
        }
        d -= static_cast<ticks>(end - p.tenths);
        if (static_cast<std::size_t>(index) + 1 < count_of(pattern)) {
            p = DayTime{p.day, pattern.start_ticks(static_cast<std::size_t>(index) + 1)};
        } else {
            p = align_forward(DayTime{p.day + 1, 0});
            if (p.day > day_limit_) {
                return kDateOverflow;
            }
        }
    }
}

date32 Calendar::sub_work(date32 t, ticks d) const {
    if (is_na(t)) {
        return kDateNa;
    }
    assert(d >= 0);  // §1.4: SubWork takes a non-negative duration
    if (d < 0) {
        return t;
    }
    if (d == 0) {
        return t;
    }
    const std::optional<DayTime> aligned = last_end_at_or_before(to_day_time(t));
    if (!aligned.has_value()) {
        return kDateOverflow;  // left the start of the calendar domain (§0.1)
    }
    DayTime p = *aligned;
    for (;;) {
        const EffectiveWeek& week = effective_week(p.day);
        const DayPattern& pattern =
            week.day[static_cast<std::size_t>(weekday_of_day(p.day))];
        // Mirror of the forward layers: whole weeks, then whole days, then the
        // interval walk (FUN_14038bdb0 -> FUN_14038c410 / FUN_140200b10).
        if (week.total_ticks > 0 && p == last_working_instant_of_week(week) &&
            d > week.total_ticks) {
            d -= week.total_ticks;
            const std::optional<DayTime> previous =
                last_end_at_or_before(DayTime{p.day - kDaysPerWeek, kTenthsPerDay});
            if (!previous.has_value()) {
                return kDateOverflow;
            }
            p = *previous;
            continue;
        }
        if (!pattern.empty() && p.tenths == last_end(pattern)) {
            const ticks day_total = pattern.total_ticks();
            if (d > day_total) {
                d -= day_total;
                const std::optional<DayTime> previous =
                    last_end_at_or_before(DayTime{p.day - 1, kTenthsPerDay});
                if (!previous.has_value()) {
                    return kDateOverflow;
                }
                p = *previous;
                continue;
            }
        }
        const std::int32_t index = interval_ending_at(pattern, p.tenths);
        if (index == kNoIndex) {
            const std::optional<DayTime> previous =
                last_end_at_or_before(DayTime{p.day, p.tenths - 1});
            if (!previous.has_value()) {
                return kDateOverflow;
            }
            p = *previous;
            continue;
        }
        const std::int32_t start = pattern.start_ticks(static_cast<std::size_t>(index));
        const ticks available = static_cast<ticks>(p.tenths - start);
        if (d <= available) {
            return pack(p.day, p.tenths - static_cast<std::int32_t>(d));
        }
        d -= available;
        // TODO(open: W3-O4): stepping back from an interval start is the open
        // case -- the spec's SubWork pseudocode would spin on `avail == 0` here.
        // This implementation takes the previous interval's finish, i.e. the
        // symmetric inverse of AddWork; the late-date fixture should confirm it.
        const std::optional<DayTime> previous =
            last_end_at_or_before(DayTime{p.day, start});
        if (!previous.has_value()) {
            return kDateOverflow;
        }
        p = *previous;
    }
}

}  // namespace core::sched
