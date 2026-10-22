// tests/sched_duration_tests.cpp -- duration ticks, the [B] abbreviation table and
// the two gates over inference: elapsed forms and the working-unit constants.
#include <cstdint>
#include <optional>
#include <string>

#include "duration.h"
#include "sched_test_support.h"

namespace sched = core::sched;

// --- compile-time forms of the §0.2 / §1.5 constants -----------------------
static_assert(sched::kTicksPerMinute == 10);
static_assert(sched::kTicksPerHour == 600);
static_assert(sched::kTicksPerDay8h == 4800, "4800 ticks = one eight-hour day");
static_assert(sched::DurationOptions{}.minutes_per_day() == 480);
static_assert(sched::DurationOptions{}.minutes_per_week() == 2400);
static_assert(sched::DurationOptions{}.minutes_per_month() == 9600);

namespace {

sched::ticks ticks_of(const char* text) {
    const std::optional<sched::ticks> value =
        sched::duration_to_ticks(sched::parse_duration(text));
    return value.has_value() ? *value : -999999;
}

}  // namespace

int main() {
    sched_test::TestContext ctx;

    // --- parsing: the [B] abbreviation families -------------------------
    struct ParseCase {
        const char* text;
        std::int64_t amount;
        sched::DurationUnit unit;
        sched::ticks ticks;
    };
    const ParseCase canonical[] = {
        {"5d", 5, sched::DurationUnit::day, 24000},
        {"4h", 4, sched::DurationUnit::hour, 2400},
        {"2w", 2, sched::DurationUnit::week, 48000},
        {"1d", 1, sched::DurationUnit::day, 4800},
        {"0d", 0, sched::DurationUnit::day, 0},
        {"8 hours", 8, sched::DurationUnit::hour, 4800},
        {"12 hrs", 12, sched::DurationUnit::hour, 7200},
        {"1 hour", 1, sched::DurationUnit::hour, 600},
        {"5 days", 5, sched::DurationUnit::day, 24000},
        {"1 week", 1, sched::DurationUnit::week, 24000},
        {"2 wks", 2, sched::DurationUnit::week, 48000},
        {"1 mo", 1, sched::DurationUnit::month, 96000},
        {"2 months", 2, sched::DurationUnit::month, 192000},
    };
    for (const ParseCase& test : canonical) {
        const sched::ParsedDuration parsed = sched::parse_duration(test.text);
        ctx.check(parsed.ok, std::string("parses \"") + test.text + "\"");
        ctx.equal(parsed.amount, test.amount, std::string("amount of \"") + test.text + "\"");
        ctx.equal(static_cast<int>(parsed.unit), static_cast<int>(test.unit),
                  std::string("unit of \"") + test.text + "\"");
        ctx.check(!parsed.elapsed, std::string("not elapsed: \"") + test.text + "\"");
        ctx.check(!parsed.estimated, std::string("not estimated: \"") + test.text + "\"");
        ctx.equal(ticks_of(test.text), test.ticks,
                  std::string("ticks of \"") + test.text + "\"");
    }

    // W3-D2: "4h" stores 2400 raw units; "1d" with HoursPerDay = 8 stores 4800.
    ctx.equal(ticks_of("4h"), sched::ticks{2400}, "W3-D2 4h is 2400");
    ctx.equal(ticks_of("1d"), sched::ticks{4800}, "W3-D2 1d is 4800");

    // --- estimated suffix (§2.2 [B]: must round-trip) -------------------
    {
        const sched::ParsedDuration parsed = sched::parse_duration("1d?");
        ctx.check(parsed.ok, "parses \"1d?\"");
        ctx.check(parsed.estimated, "\"1d?\" is estimated");
        ctx.equal(ticks_of("1d?"), sched::ticks{4800}, "ticks of \"1d?\"");
    }

    // --- elapsed forms: parsed, semantics gated ([I]) -------------------
    {
        const sched::ParsedDuration parsed = sched::parse_duration("3ed");
        ctx.check(parsed.ok, "parses \"3ed\"");
        ctx.check(parsed.elapsed, "\"3ed\" is elapsed");
        ctx.equal(parsed.amount, std::int64_t{3}, "amount of \"3ed\"");
        ctx.equal(static_cast<int>(parsed.unit), static_cast<int>(sched::DurationUnit::day),
                  "unit of \"3ed\"");
        // TODO(open: W3-E1): elapsed conversion constants are [I], so the gate
        // must refuse instead of inventing 14400.
        ctx.check(!sched::duration_to_ticks(parsed).has_value(),
                  "\"3ed\" does not convert to ticks (gated, W3-E1)");
    }
    {
        const sched::ParsedDuration parsed = sched::parse_duration("1eweek");
        ctx.check(parsed.ok && parsed.elapsed, "parses \"1eweek\" as elapsed");
        ctx.check(!sched::duration_to_ticks(parsed).has_value(),
                  "\"1eweek\" does not convert to ticks (gated, W3-E1)");
    }
    // The same gate on the formatting side.
    ctx.check(!sched::format_elapsed(14400, sched::DurationUnit::day).has_value(),
              "elapsed formatting is gated (W3-E1)");

    // --- rejects ---------------------------------------------------------
    const char* rejected[] = {"",        "d",    "5",     "5x",  "+5d",
                              "*5d",     "5 m",  "5 min", "5d junk",
                              "5 d x",   "-",    "  "};
    for (const char* text : rejected) {
        ctx.check(!sched::parse_duration(text).ok,
                  std::string("rejects \"") + text + "\"");
    }
    // [B] §2.2: a leading '+' is a UI accelerator, not accepted text.
    ctx.check(!sched::parse_duration("+5d").ok, "rejects leading '+'");

    // --- inference, exercised but tagged --------------------------------
    // TODO(open: W3-O2): case-insensitive match is inferred from Excel-style
    // parsing; the [B] table itself is lowercase.
    ctx.check(sched::parse_duration("5D").ok, "accepts \"5D\" ([I] case policy, W3-O2)");
    // TODO(open: W3-O3): a negative duration is legal as a stored value (§0.2);
    // its text form is not exercised by a recipe.
    ctx.equal(ticks_of("-2h"), sched::ticks{-1200}, "\"-2h\" is -1200 ([I], W3-O3)");
    // TODO(open: W3-O5): HoursPerDay is a conversion constant; 1d with 10 h/day.
    {
        sched::DurationOptions options;
        options.hours_per_day = 10;
        const std::optional<sched::ticks> value =
            sched::duration_to_ticks(sched::parse_duration("1d"), options);
        ctx.equal(value.value_or(-1), sched::ticks{6000},
                  "1d with HoursPerDay = 10 is 6000 ([I] §1.5, W3-O5)");
    }

    // --- formatting ------------------------------------------------------
    ctx.equal_string(sched::format_duration(24000, sched::DurationUnit::day), "5d",
                     "format 24000 as days");
    ctx.equal_string(sched::format_duration(2400, sched::DurationUnit::hour), "4h",
                     "format 2400 as hours");
    ctx.equal_string(sched::format_duration(48000, sched::DurationUnit::week), "2w",
                     "format 48000 as weeks");
    ctx.equal_string(sched::format_duration(96000, sched::DurationUnit::month), "1mo",
                     "format 96000 as months");
    ctx.equal_string(sched::format_duration(-24000, sched::DurationUnit::day), "-5d",
                     "format -24000 as days");
    ctx.equal_string(sched::format_duration(0, sched::DurationUnit::day), "0d",
                     "format zero");
    ctx.equal_string(sched::format_duration(2400, sched::DurationUnit::day), "0.5d",
                     "format half a day");
    ctx.equal_string(sched::format_duration(24000, sched::DurationUnit::day, {},
                                            /*estimated=*/true),
                     "5d?", "format estimated");

    ctx.equal_string(sched::format_duration_auto(24000), "5d", "auto format 24000");
    ctx.equal_string(sched::format_duration_auto(2400), "4h", "auto format 2400");
    ctx.equal_string(sched::format_duration_auto(0), "0d", "auto format 0");

    // Format then parse is identity for whole-unit values.  A fractional
    // rendering ("0.5d") is display only and deliberately is not a parse form
    // (§2.2's table has no decimal grammar), so it is not in this table.
    struct RoundTrip {
        std::int64_t value;
        sched::DurationUnit unit;
    };
    const RoundTrip round_trips[] = {
        {24000, sched::DurationUnit::day},  {4800, sched::DurationUnit::day},
        {2400, sched::DurationUnit::hour},  {0, sched::DurationUnit::day},
        {48000, sched::DurationUnit::week}, {96000, sched::DurationUnit::month},
        {-24000, sched::DurationUnit::day},
    };
    for (const RoundTrip& test : round_trips) {
        const std::string text = sched::format_duration(test.value, test.unit);
        const std::optional<sched::ticks> back =
            sched::duration_to_ticks(sched::parse_duration(text));
        ctx.equal(back.value_or(-999999), test.value,
                  std::string("format/parse round trip \"") + text + "\"");
    }

    return ctx.finish("sched_duration_tests");
}
