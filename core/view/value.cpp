#include "value.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace core::view {
namespace {

bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

bool read_uint(std::string_view text, std::size_t& pos, int digits, int& out) noexcept {
    if (pos + static_cast<std::size_t>(digits) > text.size()) {
        return false;
    }
    int value = 0;
    for (int i = 0; i < digits; ++i) {
        const char c = text[pos + static_cast<std::size_t>(i)];
        if (!is_digit(c)) {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    pos += static_cast<std::size_t>(digits);
    out = value;
    return true;
}

int days_in_month(int year, int month) noexcept {
    static constexpr int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2) {
        const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return kDays[month - 1];
}

/* "9400" -> "9400"; "1.5" style days with at most four decimals and no
 * trailing zeros.  Matches mpp::format_duration_days. */
std::string format_decimal(long double value) {
    if (value == 0.0L) {
        return "0";
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.4Lf", value);
    std::string text(buffer);
    while (!text.empty() && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    if (text == "-0") {
        text = "0";
    }
    return text;
}

std::string format_number(double value) {
    if (std::isnan(value)) {
        return "";
    }
    if (std::isinf(value)) {
        return value < 0 ? "-inf" : "inf";
    }
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{}) {
        return "";
    }
    return std::string(buffer, result.ptr);
}

int kind_rank(ValueKind kind) noexcept {
    switch (kind) {
        case ValueKind::Null: return 0;
        case ValueKind::Bool: return 1;
        case ValueKind::Integer: return 2;
        case ValueKind::Number: return 3;
        case ValueKind::Duration: return 4;
        case ValueKind::Date: return 5;
        case ValueKind::Text: return 6;
    }
    return 0;
}

}  // namespace

bool DateValue::valid() const noexcept {
    if (month < 1 || month > 12 || day < 1) {
        return false;
    }
    if (day > days_in_month(year, month)) {
        return false;
    }
    return hour <= 23 && minute <= 59 && second <= 59;
}

bool operator==(const DateValue& a, const DateValue& b) noexcept {
    return a.year == b.year && a.month == b.month && a.day == b.day &&
           a.hour == b.hour && a.minute == b.minute && a.second == b.second;
}

bool operator<(const DateValue& a, const DateValue& b) noexcept {
    if (a.year != b.year) return a.year < b.year;
    if (a.month != b.month) return a.month < b.month;
    if (a.day != b.day) return a.day < b.day;
    if (a.hour != b.hour) return a.hour < b.hour;
    if (a.minute != b.minute) return a.minute < b.minute;
    return a.second < b.second;
}

const char* value_kind_name(ValueKind kind) noexcept {
    switch (kind) {
        case ValueKind::Null: return "null";
        case ValueKind::Bool: return "bool";
        case ValueKind::Integer: return "integer";
        case ValueKind::Number: return "number";
        case ValueKind::Text: return "text";
        case ValueKind::Duration: return "duration";
        case ValueKind::Date: return "date";
    }
    return "null";
}

bool value_kind_from_name(std::string_view name, ValueKind& out) noexcept {
    if (name == "null") { out = ValueKind::Null; return true; }
    if (name == "bool") { out = ValueKind::Bool; return true; }
    if (name == "integer") { out = ValueKind::Integer; return true; }
    if (name == "number") { out = ValueKind::Number; return true; }
    if (name == "text") { out = ValueKind::Text; return true; }
    if (name == "duration") { out = ValueKind::Duration; return true; }
    if (name == "date") { out = ValueKind::Date; return true; }
    return false;
}

Value Value::of_bool(bool v) noexcept {
    Value value;
    value.kind = ValueKind::Bool;
    value.boolean = v;
    return value;
}

Value Value::of_int(std::int64_t v) noexcept {
    Value value;
    value.kind = ValueKind::Integer;
    value.integer = v;
    return value;
}

Value Value::of_number(double v) noexcept {
    Value value;
    value.kind = ValueKind::Number;
    value.number = v;
    return value;
}

Value Value::of_text(std::string v) {
    Value value;
    value.kind = ValueKind::Text;
    value.text = std::move(v);
    return value;
}

Value Value::of_duration(std::int64_t ticks) noexcept {
    Value value;
    value.kind = ValueKind::Duration;
    value.integer = ticks;
    return value;
}

Value Value::of_date(DateValue v) noexcept {
    Value value;
    value.kind = ValueKind::Date;
    value.date = v;
    return value;
}

bool parse_iso_duration(std::string_view text, std::int64_t& ticks) noexcept {
    ticks = 0;
    if (text.empty()) {
        return false;
    }
    std::size_t pos = 0;
    bool negative = false;
    if (text[pos] == '-') {
        negative = true;
        ++pos;
    } else if (text[pos] == '+') {
        ++pos;
    }
    if (pos >= text.size() || (text[pos] != 'P' && text[pos] != 'p')) {
        return false;
    }
    ++pos;
    bool in_time = false;
    bool any = false;
    long double seconds = 0.0L;
    while (pos < text.size()) {
        if (text[pos] == 'T' || text[pos] == 't') {
            in_time = true;
            ++pos;
            continue;
        }
        if (!is_digit(text[pos]) && text[pos] != '.') {
            return false;
        }
        const std::size_t number_start = pos;
        while (pos < text.size() && (is_digit(text[pos]) || text[pos] == '.')) {
            ++pos;
        }
        if (pos >= text.size()) {
            return false;
        }
        const std::string_view number_text = text.substr(number_start, pos - number_start);
        /* std::from_chars for floating point is available on MSVC 2019+; use
         * strtold for portability of the trimmed substring. */
        const std::string number_copy(number_text);
        char* end = nullptr;
        const long double amount = std::strtold(number_copy.c_str(), &end);
        if (end == number_copy.c_str() || *end != '\0') {
            return false;
        }
        const char unit = static_cast<char>(text[pos++]);
        switch (unit) {
            case 'D': case 'd':
                if (in_time) return false;
                seconds += amount * 86400.0L;
                break;
            case 'H': case 'h':
                if (!in_time) return false;
                seconds += amount * 3600.0L;
                break;
            case 'M': case 'm':
                if (!in_time) return false; /* months are ambiguous */
                seconds += amount * 60.0L;
                break;
            case 'S': case 's':
                if (!in_time) return false;
                seconds += amount;
                break;
            case 'W': case 'w':
                if (in_time) return false;
                seconds += amount * 7.0L * 86400.0L;
                break;
            default:
                return false;
        }
        any = true;
    }
    if (!any) {
        return false;
    }
    if (negative) {
        seconds = -seconds;
    }
    const long double tick_value = seconds / 6.0L;
    if (tick_value > static_cast<long double>(std::numeric_limits<std::int64_t>::max()) ||
        tick_value < static_cast<long double>(std::numeric_limits<std::int64_t>::min())) {
        return false;
    }
    ticks = static_cast<std::int64_t>(std::llround(tick_value));
    return true;
}

bool parse_iso_datetime(std::string_view text, DateValue& out) noexcept {
    out = DateValue{};
    if (text.size() < 10) {
        return false;
    }
    std::size_t pos = 0;
    int year = 0;
    int month = 0;
    int day = 0;
    if (!read_uint(text, pos, 4, year) || pos >= text.size() || text[pos] != '-' ||
        !read_uint(text, ++pos, 2, month) || pos >= text.size() || text[pos] != '-' ||
        !read_uint(text, ++pos, 2, day)) {
        return false;
    }
    out.year = year;
    out.month = static_cast<std::uint8_t>(month);
    out.day = static_cast<std::uint8_t>(day);
    if (pos < text.size()) {
        if (text[pos] != 'T' && text[pos] != 't' && text[pos] != ' ') {
            return false;
        }
        ++pos;
        int hour = 0;
        int minute = 0;
        if (!read_uint(text, pos, 2, hour) || pos >= text.size() || text[pos] != ':') {
            return false;
        }
        if (!read_uint(text, ++pos, 2, minute)) {
            return false;
        }
        out.hour = static_cast<std::uint8_t>(hour);
        out.minute = static_cast<std::uint8_t>(minute);
        if (pos < text.size() && text[pos] == ':') {
            int second = 0;
            if (!read_uint(text, ++pos, 2, second)) {
                return false;
            }
            out.second = static_cast<std::uint8_t>(second);
            /* Fractional seconds are accepted and truncated: the value type
             * stores whole seconds, and MSPDI never needs sub-second. */
            if (pos < text.size() && text[pos] == '.') {
                ++pos;
                while (pos < text.size() && is_digit(text[pos])) {
                    ++pos;
                }
            }
        }
        /* Accept and ignore a timezone designator: see value.h. */
        if (pos < text.size()) {
            if (text[pos] == 'Z' || text[pos] == 'z') {
                ++pos;
            } else if (text[pos] == '+' || text[pos] == '-') {
                ++pos;
                int ignored = 0;
                if (read_uint(text, pos, 2, ignored) && pos < text.size() && text[pos] == ':') {
                    ++pos;
                    read_uint(text, pos, 2, ignored);
                }
            }
        }
    }
    return pos == text.size() && out.valid();
}

std::string format_date(const DateValue& date) {
    if (!date.valid()) {
        return "";
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", static_cast<int>(date.year),
                  static_cast<int>(date.month), static_cast<int>(date.day));
    std::string text(buffer);
    if (date.has_time_of_day()) {
        std::snprintf(buffer, sizeof(buffer), " %02d:%02d", static_cast<int>(date.hour),
                      static_cast<int>(date.minute));
        text += buffer;
        if (date.second != 0) {
            std::snprintf(buffer, sizeof(buffer), ":%02d", static_cast<int>(date.second));
            text += buffer;
        }
    }
    return text;
}

std::string format_duration(std::int64_t ticks, DurationStyle style) {
    switch (style) {
        case DurationStyle::Ticks:
            return std::to_string(ticks);
        case DurationStyle::Days:
            return format_decimal(static_cast<long double>(ticks) /
                                  static_cast<long double>(kTicksPerDay)) +
                   "d";
        case DurationStyle::Iso8601:
        default:
            break;
    }
    const bool negative = ticks < 0;
    const std::uint64_t remaining =
        static_cast<std::uint64_t>(negative ? -ticks : ticks);
    /* Project writes durations as an xsd:duration in time units, even when the
     * value is a whole number of 8-hour days: two days is "PT16H0M0S", never
     * "P2D".  Emit the full H/M/S triple so the form matches MSPDI output and a
     * text comparison against Project's own XML is stable. */
    std::uint64_t seconds = remaining * 6;
    const std::uint64_t hours = seconds / 3600;
    seconds %= 3600;
    const std::uint64_t minutes = seconds / 60;
    seconds %= 60;
    std::string text;
    if (negative) {
        text += '-';
    }
    text += "PT";
    text += std::to_string(hours);
    text += 'H';
    text += std::to_string(minutes);
    text += 'M';
    text += std::to_string(seconds);
    text += 'S';
    return text;
}

std::string format_value(const Value& value, DurationStyle duration_style) {
    switch (value.kind) {
        case ValueKind::Null: return "";
        case ValueKind::Bool: return value.boolean ? "1" : "0";
        case ValueKind::Integer: return std::to_string(value.integer);
        case ValueKind::Number: return format_number(value.number);
        case ValueKind::Text: return value.text;
        case ValueKind::Duration: return format_duration(value.integer, duration_style);
        case ValueKind::Date: return format_date(value.date);
    }
    return "";
}

Value coerce_value(const Value& value, ValueKind target) noexcept {
    if (value.kind == target) {
        return value;
    }
    if (value.kind == ValueKind::Null) {
        return Value{};
    }
    auto text_to = [&](ValueKind kind) -> Value {
        if (value.kind != ValueKind::Text) {
            return Value{};
        }
        /* std::from_chars(double) is DELETED in libc++ (measured wave 19: the
         * Qt shell's macOS job fails to compile the two calls below with "call
         * to deleted function 'from_chars'"), and GCC's libstdc++ only grew the
         * floating-point overload in GCC 11.  One strict strtod path is used on
         * every platform instead of a platform split, so a cell coerces the same
         * way on Windows, macOS and Linux: whole string consumed, finite, and a
         * leading character the integer overload would accept (no whitespace,
         * no "inf"/"nan" from strtod's looser grammar). */
        auto parse_real = [](std::string const& text, double& out) {
            if (text.empty()) {
                return false;
            }
            const char first = text.front();
            if (!(first == '-' || first == '+' || first == '.' ||
                  (first >= '0' && first <= '9'))) {
                return false;
            }
            char* end = nullptr;
            const double parsed = std::strtod(text.c_str(), &end);
            if (end != text.c_str() + text.size() || !std::isfinite(parsed)) {
                return false;
            }
            out = parsed;
            return true;
        };
        if (kind == ValueKind::Integer) {
            std::int64_t parsed = 0;
            const char* begin = value.text.data();
            const char* end = begin + value.text.size();
            auto result = std::from_chars(begin, end, parsed);
            if (result.ec != std::errc{} || result.ptr != end) {
                /* Accept "8.0" when the fraction is zero. */
                double as_double = 0.0;
                if (!parse_real(value.text, as_double) ||
                    std::floor(as_double) != as_double ||
                    as_double < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
                    as_double > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
                    return Value{};
                }
                parsed = static_cast<std::int64_t>(as_double);
            }
            return Value::of_int(parsed);
        }
        if (kind == ValueKind::Number) {
            double parsed = 0.0;
            if (!parse_real(value.text, parsed)) {
                return Value{};
            }
            return Value::of_number(parsed);
        }
        if (kind == ValueKind::Duration) {
            std::int64_t ticks = 0;
            if (!parse_iso_duration(value.text, ticks)) {
                return Value{};
            }
            return Value::of_duration(ticks);
        }
        if (kind == ValueKind::Date) {
            DateValue date;
            if (!parse_iso_datetime(value.text, date)) {
                return Value{};
            }
            return Value::of_date(date);
        }
        if (kind == ValueKind::Bool) {
            if (value.text == "1" || value.text == "true" || value.text == "True" ||
                value.text == "yes" || value.text == "Yes") {
                return Value::of_bool(true);
            }
            if (value.text == "0" || value.text == "false" || value.text == "False" ||
                value.text == "no" || value.text == "No") {
                return Value::of_bool(false);
            }
            return Value{};
        }
        return Value{};
    };
    switch (target) {
        case ValueKind::Text:
            if (value.kind == ValueKind::Bool ||
                value.kind == ValueKind::Integer ||
                value.kind == ValueKind::Number ||
                value.kind == ValueKind::Duration ||
                value.kind == ValueKind::Date) {
                return Value::of_text(format_value(value));
            }
            return Value{};
        case ValueKind::Integer:
            if (value.kind == ValueKind::Bool) return Value::of_int(value.boolean ? 1 : 0);
            if (value.kind == ValueKind::Number) {
                if (std::floor(value.number) != value.number) return Value{};
                return Value::of_int(static_cast<std::int64_t>(value.number));
            }
            if (value.kind == ValueKind::Text) return text_to(ValueKind::Integer);
            return Value{};
        case ValueKind::Number:
            if (value.kind == ValueKind::Bool) return Value::of_number(value.boolean ? 1.0 : 0.0);
            if (value.kind == ValueKind::Integer) {
                return Value::of_number(static_cast<double>(value.integer));
            }
            if (value.kind == ValueKind::Text) return text_to(ValueKind::Number);
            return Value{};
        case ValueKind::Duration:
            if (value.kind == ValueKind::Text) return text_to(ValueKind::Duration);
            return Value{};
        case ValueKind::Date:
            if (value.kind == ValueKind::Text) return text_to(ValueKind::Date);
            return Value{};
        case ValueKind::Bool:
            if (value.kind == ValueKind::Integer) return Value::of_bool(value.integer != 0);
            if (value.kind == ValueKind::Number) return Value::of_bool(value.number != 0.0);
            if (value.kind == ValueKind::Text) return text_to(ValueKind::Bool);
            return Value{};
        case ValueKind::Null:
            return Value{};
    }
    return Value{};
}

int compare_values(const Value& a, const Value& b) noexcept {
    if (a.kind == b.kind) {
        switch (a.kind) {
            case ValueKind::Null: return 0;
            case ValueKind::Bool:
                return (a.boolean == b.boolean) ? 0 : (a.boolean ? 1 : -1);
            case ValueKind::Integer:
            case ValueKind::Duration:
                return (a.integer == b.integer) ? 0 : (a.integer < b.integer ? -1 : 1);
            case ValueKind::Number:
                return (a.number == b.number) ? 0 : (a.number < b.number ? -1 : 1);
            case ValueKind::Text:
                return a.text.compare(b.text) < 0 ? -1 : (a.text == b.text ? 0 : 1);
            case ValueKind::Date:
                if (a.date == b.date) return 0;
                return a.date < b.date ? -1 : 1;
        }
    }
    /* Try to compare in the more specific of the two kinds rather than
     * falling back on rank immediately: a text constant is how filters and
     * MSPDI carry numbers and dates. */
    if (a.kind == ValueKind::Text && b.kind != ValueKind::Text) {
        const Value converted = coerce_value(a, b.kind);
        if (!converted.is_null()) {
            return compare_values(converted, b);
        }
    }
    if (b.kind == ValueKind::Text && a.kind != ValueKind::Text) {
        const Value converted = coerce_value(b, a.kind);
        if (!converted.is_null()) {
            return compare_values(a, converted);
        }
    }
    if (a.kind == ValueKind::Integer && b.kind == ValueKind::Number) {
        return compare_values(Value::of_number(static_cast<double>(a.integer)), b);
    }
    if (a.kind == ValueKind::Number && b.kind == ValueKind::Integer) {
        return compare_values(a, Value::of_number(static_cast<double>(b.integer)));
    }
    if (a.kind == ValueKind::Bool && b.kind == ValueKind::Integer) {
        return compare_values(Value::of_int(a.boolean ? 1 : 0), b);
    }
    if (a.kind == ValueKind::Integer && b.kind == ValueKind::Bool) {
        return compare_values(a, Value::of_int(b.boolean ? 1 : 0));
    }
    const int rank_a = kind_rank(a.kind);
    const int rank_b = kind_rank(b.kind);
    return rank_a == rank_b ? 0 : (rank_a < rank_b ? -1 : 1);
}

}  // namespace core::view
