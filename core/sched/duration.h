// core/sched/duration.h -- durations and work as integer tenths of a minute.
//
// spec/scheduling.md §0.2 (storage) and §2.2 (text forms).
//
// [B] Durations and work are integers in tenths of a minute; the raw stored
//     "5 days" is 24000 (= 5 * 480 * 10) and negative values are legal.
// [B] The accepted abbreviation families are the table baked into the binary
//     (resource 537): h/d/w/mo plus an "e" (elapsed) prefix, and a "?" suffix
//     for estimated durations.
// [I] The working-unit conversion constants (HoursPerDay/Week/DaysPerMonth) and
//     the elapsed constants are inference: they are NOT applied here.  Elapsed
//     conversion is refused by design -- see TODO(open: W3-E1).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace core::sched {

// Tenths of a minute.  64-bit so that a whole-day/week skip cannot overflow the
// intermediate arithmetic; every stored field is 32-bit (§0.2).
using ticks = std::int64_t;

// [B] §0.2: -1 is "not set" in several stored fields; it is not a duration.
inline constexpr ticks kNaTicks = -1;

inline constexpr ticks kTicksPerMinute = 10;
inline constexpr ticks kTicksPerHour = 60 * kTicksPerMinute;
// Brief and §0.2: 4800 ticks = one eight-hour day.
inline constexpr ticks kTicksPerDay8h = 4800;

// Project options that turn a typed duration into minutes (§1.5).  The defaults
// are MS Project's stock values, but the claim that these options are
// *conversion constants only* is inference.
//
// TODO(open: W3-O5): with HoursPerDay = 10 a task typed as "1d" must store 6000
// while still spanning 600 working minutes; that fixture also confirms that the
// calendar's own working day is independent of these numbers.
struct DurationOptions {
    std::int32_t hours_per_day = 8;
    std::int32_t hours_per_week = 40;
    std::int32_t days_per_month = 20;

    constexpr ticks minutes_per_day() const noexcept {
        return static_cast<ticks>(hours_per_day) * 60;
    }
    constexpr ticks minutes_per_week() const noexcept {
        return static_cast<ticks>(hours_per_week) * 60;
    }
    constexpr ticks minutes_per_month() const noexcept {
        return static_cast<ticks>(days_per_month) * minutes_per_day();
    }
};

// PjFormatUnit families (§0.4).  Only the four families in the [B] abbreviation
// table are represented: resource 537 has no minute family, so "5m" is not a
// parse form here.  The numeric enum order is not the PjFormatUnit order and is
// not used for anything but the parser table.
enum class DurationUnit : std::uint8_t { minute = 0, hour, day, week, month };

// One parsed duration text.
struct ParsedDuration {
    bool ok = false;
    std::int64_t amount = 0;  // integral amount only; fractional text is not in §2.2
    DurationUnit unit = DurationUnit::day;
    bool elapsed = false;   // "3ed" -- semantics gated, see TODO(open: W3-E1)
    bool estimated = false; // trailing '?' (§2.2)
};

// Accepts the [B] abbreviation families, case-insensitively, with an optional
// space between number and unit: "5d", "4 hrs", "2w", "1 mo", "3ed", "0 days?".
// A leading '+' and '*' are rejected: §2.2 says they are UI accelerators, not
// part of the accepted text.
//
// TODO(open: W3-O3): the leading '-' of a negative duration is accepted on the
// strength of §0.2 "negative values are legal"; the text form of a negative
// duration (and of a negative lag) is not itself exercised by a recipe.
// TODO(open: W3-O2): case-insensitivity and the space before the unit are
// inference; the fixture should read back DurationText for both spellings.
ParsedDuration parse_duration(std::string_view text);

// Converts to ticks using the working-unit constants (§1.5).
//
// Returns nullopt when the text did not parse, and when it is an *elapsed* form:
// the elapsed constants (1440/10080/43200 minutes) are [I], so this refuses to
// invent them.
// TODO(open: W3-E1): 1d = 4800 and 1ed = 14400 with HoursPerDay = 8.
// TODO(open: W3-E2): the elapsed month length (spec says 30 days; unverified).
std::optional<ticks> duration_to_ticks(const ParsedDuration& parsed,
                                       const DurationOptions& options = {});

// Renders `value` in `unit`, e.g. 24000 -> "5d", 2400 -> "4h", 48000 -> "2w".
// Values that are not a whole unit render with two decimals ("1.25d"); that is
// display only and the policy is inference.
// TODO(open: W3-O2): DurationText is generated from the number plus
// DurationFormat; the unit actually chosen is the user's format, not this helper.
std::string format_duration(ticks value, DurationUnit unit,
                            const DurationOptions& options = {},
                            bool estimated = false);

// Largest exact unit, preferring day over week over month over hour.
// TODO(open: W3-O2): provisional display policy, not a proven engine rule.
std::string format_duration_auto(ticks value, const DurationOptions& options = {},
                                 bool estimated = false);

// Renders an elapsed duration ("3ed").  Gated: the elapsed tick constants are
// [I], so this returns nullopt until W3-E1 measures them.
// TODO(open: W3-E1): elapsed day = 24 h, week = 7 d, month = 30 d.
std::optional<std::string> format_elapsed(ticks value, DurationUnit unit,
                                          bool estimated = false);

}  // namespace core::sched
