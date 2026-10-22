// core/sched/duration.cpp -- duration text parsing and formatting.
#include "duration.h"

#include <array>
#include <cctype>
#include <cstddef>

namespace core::sched {
namespace {

struct UnitForm {
    std::string_view text;
    DurationUnit unit;
};

// Exactly the families baked into the binary (§2.2, resource 537,
// analysis/res/pjintl_strings.tsv:17377-17384).  The elapsed spellings are the
// same words with an "e" prefix, so the parser strips one leading 'e' instead of
// duplicating the table.
constexpr UnitForm kUnitForms[] = {
    {"h", DurationUnit::hour},     {"hr", DurationUnit::hour},
    {"hrs", DurationUnit::hour},   {"hour", DurationUnit::hour},
    {"hours", DurationUnit::hour},

    {"d", DurationUnit::day},      {"dy", DurationUnit::day},
    {"dys", DurationUnit::day},    {"day", DurationUnit::day},
    {"days", DurationUnit::day},

    {"w", DurationUnit::week},     {"wk", DurationUnit::week},
    {"wks", DurationUnit::week},   {"week", DurationUnit::week},
    {"weeks", DurationUnit::week},

    {"mo", DurationUnit::month},   {"mon", DurationUnit::month},
    {"mons", DurationUnit::month}, {"month", DurationUnit::month},
    {"months", DurationUnit::month},
};

bool lookup_unit(std::string_view word, DurationUnit& unit) {
    for (const UnitForm& form : kUnitForms) {
        if (form.text == word) {
            unit = form.unit;
            return true;
        }
    }
    return false;
}

ticks ticks_in_unit(DurationUnit unit, const DurationOptions& options) {
    switch (unit) {
        case DurationUnit::minute:
            return kTicksPerMinute;
        case DurationUnit::hour:
            return kTicksPerHour;
        case DurationUnit::day:
            return options.minutes_per_day() * kTicksPerMinute;
        case DurationUnit::week:
            return options.minutes_per_week() * kTicksPerMinute;
        case DurationUnit::month:
            return options.minutes_per_month() * kTicksPerMinute;
    }
    return 0;
}

std::string_view unit_suffix(DurationUnit unit) {
    switch (unit) {
        case DurationUnit::minute:
            // Not in the [B] table (resource 537 has no minute family): the
            // rendered text is diagnostic only and is not a parse form.
            return "m";
        case DurationUnit::hour:
            return "h";
        case DurationUnit::day:
            return "d";
        case DurationUnit::week:
            return "w";
        case DurationUnit::month:
            return "mo";
    }
    return "";
}

constexpr bool is_space(char c) noexcept { return c == ' ' || c == '\t'; }

}  // namespace

ParsedDuration parse_duration(std::string_view text) {
    ParsedDuration out;
    std::size_t i = 0;
    while (i < text.size() && is_space(text[i])) {
        ++i;
    }
    bool negative = false;
    if (i < text.size() && text[i] == '-') {
        // TODO(open: W3-O3): §0.2 says negative values are legal; the text form
        // is not exercised by a recipe.  A leading '+' is rejected because §2.2
        // calls it a UI accelerator.
        negative = true;
        ++i;
    }
    const std::size_t digits_begin = i;
    std::int64_t amount = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        amount = amount * 10 + (text[i] - '0');
        ++i;
    }
    if (i == digits_begin) {
        return out;
    }
    while (i < text.size() && is_space(text[i])) {
        ++i;
    }
    const std::size_t word_begin = i;
    std::string word;
    while (i < text.size() && std::isalpha(static_cast<unsigned char>(text[i])) != 0) {
        word.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(text[i]))));
        ++i;
    }
    if (i == word_begin) {
        return out;
    }
    bool elapsed = false;
    if (word.size() > 1 && word.front() == 'e') {
        elapsed = true;
        word.erase(word.begin());
    }
    DurationUnit unit = DurationUnit::day;
    if (!lookup_unit(word, unit)) {
        return out;
    }
    while (i < text.size() && is_space(text[i])) {
        ++i;
    }
    bool estimated = false;
    if (i < text.size() && text[i] == '?') {
        estimated = true;
        ++i;
    }
    while (i < text.size() && is_space(text[i])) {
        ++i;
    }
    if (i != text.size()) {
        return out;  // trailing junk
    }
    out.ok = true;
    out.amount = negative ? -amount : amount;
    out.unit = unit;
    out.elapsed = elapsed;
    out.estimated = estimated;
    return out;
}

std::optional<ticks> duration_to_ticks(const ParsedDuration& parsed,
                                       const DurationOptions& options) {
    if (!parsed.ok) {
        return std::nullopt;
    }
    if (parsed.elapsed) {
        // TODO(open: W3-E1): the elapsed constants are [I] (24 h day, 7 d week,
        // 30 d month).  Refuse rather than invent: 1d = 4800, 1ed = 14400.
        return std::nullopt;
    }
    return static_cast<ticks>(parsed.amount) * ticks_in_unit(parsed.unit, options);
}

std::string format_duration(ticks value, DurationUnit unit,
                            const DurationOptions& options, bool estimated) {
    const ticks unit_ticks = ticks_in_unit(unit, options);
    if (unit_ticks <= 0) {
        return {};
    }
    const bool negative = value < 0;
    const ticks magnitude = negative ? -value : value;
    // Two decimal places, rounded half up, in integer arithmetic.
    const ticks hundredths = (magnitude * 100 + unit_ticks / 2) / unit_ticks;
    std::string out;
    if (negative) {
        out.push_back('-');
    }
    out += std::to_string(hundredths / 100);
    const ticks fraction = hundredths % 100;
    if (fraction != 0) {
        std::string digits = fraction < 10 ? "0" + std::to_string(fraction)
                                           : std::to_string(fraction);
        while (!digits.empty() && digits.back() == '0') {
            digits.pop_back();
        }
        out.push_back('.');
        out += digits;
    }
    out += unit_suffix(unit);
    if (estimated) {
        out.push_back('?');
    }
    return out;
}

std::string format_duration_auto(ticks value, const DurationOptions& options,
                                 bool estimated) {
    // TODO(open: W3-O2): display-unit choice is the user's DurationFormat; this
    // order is provisional.
    constexpr std::array<DurationUnit, 4> kOrder = {
        DurationUnit::day, DurationUnit::week, DurationUnit::month, DurationUnit::hour};
    for (DurationUnit unit : kOrder) {
        const ticks unit_ticks = ticks_in_unit(unit, options);
        if (unit_ticks > 0 && value % unit_ticks == 0) {
            return format_duration(value, unit, options, estimated);
        }
    }
    return format_duration(value, DurationUnit::hour, options, estimated);
}

std::optional<std::string> format_elapsed(ticks /*value*/, DurationUnit /*unit*/,
                                          bool /*estimated*/) {
    // TODO(open: W3-E1): elapsed formatting needs the elapsed tick constants,
    // which are inference (24 h day, 7 d week, 30 d month).  Gated.
    return std::nullopt;
}

}  // namespace core::sched
